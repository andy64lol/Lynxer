#include "ast.hpp"

#include "builtins.hpp"
#include "config.hpp"
#include "error.hpp"
#include "interrupt.hpp"
#include "ops.hpp"
#include "optimizer.hpp"
#include "parser.hpp"
#include "stdlib/lynxer_native_abi.h"
#include "types.hpp"

#include <chrono>
#include <cctype>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <thread>
#include <variant>
#if defined(__unix__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

namespace lynxer {


// --- ownership built-in arguments ---------------------------------------------
//
// The ownership family takes variable names, not values, so these calls are
// dispatched before their arguments are evaluated.

namespace {

// The variable name an argument refers to, or "" when it is not a bare
// variable (which the built-in reports as a bad argument).
std::string ownershipArgumentName(const Expression& expression) {
    if (const auto* variable =
            dynamic_cast<const VariableExpression*>(&expression)) {
        return variable->name();
    }
    if (const auto* access =
            dynamic_cast<const DotAccessExpression*>(&expression)) {
        const auto* object =
            dynamic_cast<const VariableExpression*>(&access->object());
        if (object != nullptr && object->name() == "global") {
            return access->fieldName();
        }
    }
    return "";
}

std::vector<std::string> ownershipArgumentNames(
    const std::vector<ExpressionPtr>& arguments) {
    std::vector<std::string> names;
    names.reserve(arguments.size());
    for (const auto& argument : arguments) {
        names.push_back(ownershipArgumentName(*argument));
    }
    return names;
}

} // namespace

// --- record/enum helpers -------------------------------------------------------

namespace {

std::string moduleNameFromPath(const std::string& path) {
    std::filesystem::path name(path);
    std::string value = name.filename().string();
    for (const std::string& suffix : {".lynx", ".lynxc", ".so"}) {
        if (value.size() > suffix.size() &&
            value.compare(value.size() - suffix.size(), suffix.size(),
                          suffix) == 0) {
            value.resize(value.size() - suffix.size());
            break;
        }
    }
    return value;
}

struct NativeRegistration {
    void* handle = nullptr;
    std::unordered_map<std::string, std::pair<void*, std::string>> functions;
    std::unordered_map<std::string, std::int64_t> constants;
    std::unordered_map<std::string, std::string> types;
    std::string error;
};

thread_local NativeRegistration* activeNativeRegistration = nullptr;

bool validNativeName(const char* name) {
    if (name == nullptr || *name == '\0' ||
        !(std::isalpha(static_cast<unsigned char>(*name)) || *name == '_')) {
        return false;
    }
    for (const char* cursor = name + 1; *cursor; ++cursor) {
        if (!(std::isalnum(static_cast<unsigned char>(*cursor)) ||
              *cursor == '_')) {
            return false;
        }
    }
    return true;
}

int nativeRegisterFunction(const char* name, const char* symbol,
                           const char* signature) {
#if defined(__unix__) || defined(__APPLE__)
    if (activeNativeRegistration == nullptr || !validNativeName(name) ||
        symbol == nullptr || signature == nullptr) {
        return 0;
    }
    void* address = dlsym(activeNativeRegistration->handle, symbol);
    if (activeNativeRegistration->functions.count(name) ||
        activeNativeRegistration->constants.count(name) ||
        activeNativeRegistration->types.count(name)) {
        activeNativeRegistration->error = "duplicate native registration";
        return 0;
    }
    if (address == nullptr) {
        activeNativeRegistration->error = "registered symbol not found";
        return 0;
    }
    activeNativeRegistration->functions[name] = {address, signature};
    return 1;
#else
    (void)name;
    (void)symbol;
    (void)signature;
    return 0;
#endif
}

int nativeRegisterConstant(const char* name, std::int64_t value) {
    if (activeNativeRegistration == nullptr || !validNativeName(name)) {
        return 0;
    }
    activeNativeRegistration->constants[name] = value;
    return 1;
}

int nativeRegisterType(const char* name, const char* layout) {
    if (activeNativeRegistration == nullptr || !validNativeName(name) ||
        layout == nullptr) {
        return 0;
    }
    activeNativeRegistration->types[name] = layout;
    return 1;
}

const std::string& nativeStringArg(const std::vector<Value>& args,
                                   std::size_t index, int line, int column) {
    if (index >= args.size() ||
        !std::holds_alternative<std::string>(args[index])) {
        throw SourceError("native call expected a string argument", line, column);
    }
    return std::get<std::string>(args[index]);
}

std::int64_t nativeIntArg(const std::vector<Value>& args, std::size_t index,
                          int line, int column) {
    if (index >= args.size() ||
        !std::holds_alternative<std::int64_t>(args[index])) {
        throw SourceError("native call expected an integer argument", line,
                          column);
    }
    return std::get<std::int64_t>(args[index]);
}

const char* nativeStringResult(const char* result) {
    return result == nullptr ? "" : result;
}

// A callback invoked by a native module can raise a C++ exception, but it
// cannot unwind through the native frames in between. `lynxerHostInvoke`
// stashes it here and `callNative` rethrows it once the native call returns.
std::exception_ptr deferredNativeError;

// The top-level program environment, for callbacks that must resolve against
// the program's own functions rather than a module's. Set by executeProgram.
Environment* hostInvokeEnvironment = nullptr;

int lynxerHostInvoke(void* context, const char* name, int hasArg, double arg) {
    Environment* environment = hostInvokeEnvironment != nullptr
                                   ? hostInvokeEnvironment
                                   : static_cast<Environment*>(context);
    if (environment == nullptr) {
        return 1;
    }
    try {
        std::vector<Value> arguments;
        if (hasArg != 0) {
            arguments.push_back(arg);
        }
        environment->callUserFunction(
            name, arguments,
            std::vector<std::shared_ptr<CodeblockValue>>(), 0, 0);
        return 0;
    } catch (const InterruptError&) {
        return 1;
    } catch (...) {
        deferredNativeError = std::current_exception();
        return 1;
    }
}

int lynxerHostInterrupted(void*) { return interruptRequested() ? 1 : 0; }

// Storage for the packed arguments of a `cdecl:<ret>(...)` native call. The
// string pointers stay valid until the vectors are destroyed, which outlives
// the call itself.
struct PackedNativeArgs {
    std::vector<double> numbers;
    std::vector<std::string> strings;
    std::vector<const char*> stringPointers;
};

// The C prototype a `cdecl:<ret>(...)` function must export is
// `<ret>(const double* nums, int64_t num_count, const char* const* strs,
// int64_t str_count)`. Passing four scalars rather than a struct lets a Rust
// `extern "C" fn` match it directly.
LynxerArgs packNativeArgs(const std::vector<Value>& args,
                          PackedNativeArgs& storage, int line, int column) {
    constexpr std::size_t kMaxPackedArgs = 64;
    if (args.size() > kMaxPackedArgs) {
        throw SourceError("native call has too many packed arguments", line,
                          column);
    }
    for (const auto& argument : args) {
        if (std::holds_alternative<std::int64_t>(argument)) {
            storage.numbers.push_back(
                static_cast<double>(std::get<std::int64_t>(argument)));
        } else if (std::holds_alternative<double>(argument)) {
            storage.numbers.push_back(std::get<double>(argument));
        } else if (std::holds_alternative<bool>(argument)) {
            storage.numbers.push_back(std::get<bool>(argument) ? 1.0 : 0.0);
        } else if (std::holds_alternative<std::string>(argument)) {
            storage.strings.push_back(std::get<std::string>(argument));
        } else {
            throw SourceError(
                "native call argument is not a number or string", line, column);
        }
    }
    storage.stringPointers.reserve(storage.strings.size());
    for (const auto& text : storage.strings) {
        storage.stringPointers.push_back(text.c_str());
    }
    LynxerArgs packed{};
    packed.num_count = static_cast<std::int64_t>(storage.numbers.size());
    packed.nums = storage.numbers.empty() ? nullptr : storage.numbers.data();
    packed.str_count = static_cast<std::int64_t>(storage.stringPointers.size());
    packed.strs =
        storage.stringPointers.empty() ? nullptr : storage.stringPointers.data();
    return packed;
}

using NativeCall = Value (*)(void*, const std::vector<Value>&, int, int);

const std::unordered_map<std::string, NativeCall>& nativeCallTable() {
    static const std::unordered_map<std::string, NativeCall> table = {
        {"int64()",
         [](void* address, const std::vector<Value>&, int, int) -> Value {
             return static_cast<std::int64_t>(
                 reinterpret_cast<std::int64_t (*)()>(address)());
         }},
        {"float64()",
         [](void* address, const std::vector<Value>&, int, int) -> Value {
             return reinterpret_cast<double (*)()>(address)();
         }},
        {"cstring()",
         [](void* address, const std::vector<Value>&, int, int) -> Value {
             return std::string(nativeStringResult(
                 reinterpret_cast<const char* (*)()>(address)()));
         }},
        {"int64(int64,int64)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             return static_cast<std::int64_t>(
                 reinterpret_cast<std::int64_t (*)(std::int64_t, std::int64_t)>(
                     address)(nativeIntArg(args, 0, line, column),
                              nativeIntArg(args, 1, line, column)));
         }},
        {"int64(int64)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             return static_cast<std::int64_t>(
                 reinterpret_cast<std::int64_t (*)(std::int64_t)>(address)(
                     nativeIntArg(args, 0, line, column)));
         }},
        {"cstring(cstring)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             return std::string(nativeStringResult(
                 reinterpret_cast<const char* (*)(const char*)>(address)(
                     nativeStringArg(args, 0, line, column).c_str())));
         }},
        {"int64(cstring)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             return static_cast<std::int64_t>(
                 reinterpret_cast<std::int64_t (*)(const char*)>(address)(
                     nativeStringArg(args, 0, line, column).c_str()));
         }},
        {"int64(cstring,cstring)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             return static_cast<std::int64_t>(
                 reinterpret_cast<std::int64_t (*)(const char*, const char*)>(
                     address)(nativeStringArg(args, 0, line, column).c_str(),
                              nativeStringArg(args, 1, line, column).c_str()));
         }},
        {"cstring(cstring,int64)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             return std::string(nativeStringResult(
                 reinterpret_cast<const char* (*)(const char*, std::int64_t)>(
                     address)(nativeStringArg(args, 0, line, column).c_str(),
                              nativeIntArg(args, 1, line, column))));
         }},
        {"int64(int64,int64,int64)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             return static_cast<std::int64_t>(
                 reinterpret_cast<std::int64_t (*)(std::int64_t, std::int64_t,
                                                   std::int64_t)>(address)(
                     nativeIntArg(args, 0, line, column),
                     nativeIntArg(args, 1, line, column),
                     nativeIntArg(args, 2, line, column)));
         }},
        {"float64(float64)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             return reinterpret_cast<double (*)(double)>(address)(
                 asNumber(args[0], line, column));
         }},
        {"float64(float64,float64)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             return reinterpret_cast<double (*)(double, double)>(address)(
                 asNumber(args[0], line, column),
                 asNumber(args[1], line, column));
         }},
        {"float64(float64,float64,float64)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             return reinterpret_cast<double (*)(double, double, double)>(
                 address)(asNumber(args[0], line, column),
                          asNumber(args[1], line, column),
                          asNumber(args[2], line, column));
         }},
        {"int64(float64)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             return static_cast<std::int64_t>(
                 reinterpret_cast<std::int64_t (*)(double)>(address)(
                     asNumber(args[0], line, column)));
         }},
        {"float64(float64,int64)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             return reinterpret_cast<double (*)(double, std::int64_t)>(address)(
                 asNumber(args[0], line, column),
                 nativeIntArg(args, 1, line, column));
         }},
        {"cstring(float64)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             return std::string(nativeStringResult(
                 reinterpret_cast<const char* (*)(double)>(address)(
                     asNumber(args[0], line, column))));
         }},
        {"float64(cstring)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             return reinterpret_cast<double (*)(const char*)>(address)(
                 nativeStringArg(args, 0, line, column).c_str());
         }},
        {"cstring(cstring,cstring)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             return std::string(nativeStringResult(
                 reinterpret_cast<const char* (*)(const char*, const char*)>(
                     address)(nativeStringArg(args, 0, line, column).c_str(),
                              nativeStringArg(args, 1, line, column).c_str())));
         }},
        {"float64(cstring,cstring)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             return reinterpret_cast<double (*)(const char*, const char*)>(
                 address)(nativeStringArg(args, 0, line, column).c_str(),
                          nativeStringArg(args, 1, line, column).c_str());
         }},
        {"cstring(cstring,cstring,cstring)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             return std::string(nativeStringResult(
                 reinterpret_cast<const char* (*)(const char*, const char*,
                                                  const char*)>(address)(
                     nativeStringArg(args, 0, line, column).c_str(),
                     nativeStringArg(args, 1, line, column).c_str(),
                     nativeStringArg(args, 2, line, column).c_str())));
         }},
        {"cstring(cstring,cstring,int64)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             return std::string(nativeStringResult(
                 reinterpret_cast<const char* (*)(const char*, const char*,
                                                  std::int64_t)>(address)(
                     nativeStringArg(args, 0, line, column).c_str(),
                     nativeStringArg(args, 1, line, column).c_str(),
                     nativeIntArg(args, 2, line, column))));
         }},
        {"cstring(cstring,cstring,cstring,int64)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             return std::string(nativeStringResult(
                 reinterpret_cast<const char* (*)(const char*, const char*,
                                                  const char*, std::int64_t)>(
                     address)(nativeStringArg(args, 0, line, column).c_str(),
                              nativeStringArg(args, 1, line, column).c_str(),
                              nativeStringArg(args, 2, line, column).c_str(),
                              nativeIntArg(args, 3, line, column))));
         }},
        {"cstring(cstring,cstring,cstring,cstring)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             return std::string(nativeStringResult(
                 reinterpret_cast<const char* (*)(const char*, const char*,
                                                  const char*, const char*)>(
                     address)(nativeStringArg(args, 0, line, column).c_str(),
                              nativeStringArg(args, 1, line, column).c_str(),
                              nativeStringArg(args, 2, line, column).c_str(),
                              nativeStringArg(args, 3, line, column).c_str())));
         }},
        {"int64(cstring,cstring,cstring)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             return static_cast<std::int64_t>(
                 reinterpret_cast<std::int64_t (*)(const char*, const char*,
                                                   const char*)>(address)(
                     nativeStringArg(args, 0, line, column).c_str(),
                     nativeStringArg(args, 1, line, column).c_str(),
                     nativeStringArg(args, 2, line, column).c_str()));
         }},
        {"int64(cstring,int64)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             return static_cast<std::int64_t>(
                 reinterpret_cast<std::int64_t (*)(const char*, std::int64_t)>(
                     address)(nativeStringArg(args, 0, line, column).c_str(),
                              nativeIntArg(args, 1, line, column)));
         }},
        {"cstring(int64)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             return std::string(nativeStringResult(
                 reinterpret_cast<const char* (*)(std::int64_t)>(address)(
                     nativeIntArg(args, 0, line, column))));
         }},
        {"cstring(int64,int64)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             return std::string(nativeStringResult(
                 reinterpret_cast<const char* (*)(std::int64_t,
                                                  std::int64_t)>(address)(
                     nativeIntArg(args, 0, line, column),
                     nativeIntArg(args, 1, line, column))));
         }},
        {"float64(int64)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             return reinterpret_cast<double (*)(std::int64_t)>(address)(
                 nativeIntArg(args, 0, line, column));
         }},
        {"int64(cstring,int64,int64)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             return static_cast<std::int64_t>(
                 reinterpret_cast<std::int64_t (*)(const char*, std::int64_t,
                                                   std::int64_t)>(address)(
                     nativeStringArg(args, 0, line, column).c_str(),
                     nativeIntArg(args, 1, line, column),
                     nativeIntArg(args, 2, line, column)));
         }},
        {"float64(cstring,float64)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             return reinterpret_cast<double (*)(const char*, double)>(address)(
                 nativeStringArg(args, 0, line, column).c_str(),
                 asNumber(args[1], line, column));
         }},
        {"cstring(float64,float64,int64)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             return std::string(nativeStringResult(
                 reinterpret_cast<const char* (*)(double, double,
                                                  std::int64_t)>(address)(
                     asNumber(args[0], line, column),
                     asNumber(args[1], line, column),
                     nativeIntArg(args, 2, line, column))));
         }},
        {"cstring(cstring,float64,float64)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             return std::string(nativeStringResult(
                 reinterpret_cast<const char* (*)(const char*, double,
                                                  double)>(address)(
                     nativeStringArg(args, 0, line, column).c_str(),
                     asNumber(args[1], line, column),
                     asNumber(args[2], line, column))));
         }},
        // Packed-argument shapes: `<ret>(...)` passes the numbers and strings
        // as four scalars, so modules are not limited to four typed
        // parameters and a Rust `extern "C" fn` matches the prototype
        // directly.
        {"int64(...)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             PackedNativeArgs storage;
             const LynxerArgs packed =
                 packNativeArgs(args, storage, line, column);
             return static_cast<std::int64_t>(
                 reinterpret_cast<std::int64_t (*)(const double*, std::int64_t,
                                                   const char* const*,
                                                   std::int64_t)>(address)(
                     packed.nums, packed.num_count, packed.strs,
                     packed.str_count));
         }},
        {"float64(...)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             PackedNativeArgs storage;
             const LynxerArgs packed =
                 packNativeArgs(args, storage, line, column);
             return reinterpret_cast<double (*)(const double*, std::int64_t,
                                                const char* const*,
                                                std::int64_t)>(address)(
                 packed.nums, packed.num_count, packed.strs, packed.str_count);
         }},
        {"cstring(...)",
         [](void* address, const std::vector<Value>& args, int line,
            int column) -> Value {
             PackedNativeArgs storage;
             const LynxerArgs packed =
                 packNativeArgs(args, storage, line, column);
             return std::string(nativeStringResult(
                 reinterpret_cast<const char* (*)(const double*, std::int64_t,
                                                  const char* const*,
                                                  std::int64_t)>(address)(
                     packed.nums, packed.num_count, packed.strs,
                     packed.str_count)));
         }},
    };
    return table;
}

Value callNativeInternal(void* address, const std::string& signature,
                         const std::vector<Value>& args, int line,
                         int column) {
    struct InterruptHandlerRestore {
        ~InterruptHandlerRestore() { installInterruptHandler(); }
    } restoreInterruptHandler;

    throwIfInterrupted();
    const std::string normalized =
        signature.rfind("cdecl:", 0) == 0 ? signature.substr(6) : signature;
    const auto open = normalized.find('(');
    const auto close = normalized.rfind(')');
    if (open == std::string::npos || close == std::string::npos) {
        throw SourceError("invalid native function signature", line, column);
    }
    std::string resultType = normalized.substr(0, open);
    if (resultType == "double") {
        resultType = "float64";
    }
    if (resultType == "uintptr" || resultType == "uint64" ||
        resultType == "int32" || resultType == "uint32" ||
        resultType == "int16" || resultType == "uint16" ||
        resultType == "int8" || resultType == "uint8") {
        resultType = "int64";
    }
    const std::string params = normalized.substr(open + 1, close - open - 1);
    std::vector<std::string> types;
    std::size_t start = 0;
    while (start < params.size()) {
        const auto comma = params.find(',', start);
        types.push_back(params.substr(start, comma == std::string::npos
                                             ? comma : comma - start));
        if (types.back() == "double") {
            types.back() = "float64";
        }
        if (types.back() == "uintptr" || types.back() == "uint64" ||
            types.back() == "int32" || types.back() == "uint32" ||
            types.back() == "int16" || types.back() == "uint16" ||
            types.back() == "int8" || types.back() == "uint8") {
            types.back() = "int64";
        }
        start = comma == std::string::npos ? params.size() : comma + 1;
    }
    std::string shape = resultType + "(";
    for (std::size_t index = 0; index < types.size(); ++index) {
        if (index > 0) {
            shape += ",";
        }
        shape += types[index];
    }
    shape += ")";
    const auto& shapes = nativeCallTable();
    const auto found = shapes.find(shape);
    if (found == shapes.end()) {
        throw SourceError("unsupported native signature '" + signature + "'",
                          line, column);
    }
    const bool packed = types.size() == 1 && types[0] == "...";
    if (!packed && types.size() != args.size()) {
        throw SourceError("native call argument count does not match signature '" +
                              signature + "'",
                          line, column);
    }
    Value result = found->second(address, args, line, column);
    // A native module may have invoked a Lynxer callback that failed; surface
    // that error instead of a silent success.
    if (deferredNativeError != nullptr) {
        const std::exception_ptr pending = deferredNativeError;
        deferredNativeError = nullptr;
        std::rethrow_exception(pending);
    }
    throwIfInterrupted();
    return result;
}

// Module sources and libraries carried by a compiled executable. Sources stay
// in memory; libraries are materialized to a temporary file at startup because
// dlopen needs a real path.
std::map<std::string, std::string>& embeddedModuleSources() {
    static std::map<std::string, std::string> instance;
    return instance;
}

std::map<std::string, std::string>& embeddedModuleLibraries() {
    static std::map<std::string, std::string> instance;
    return instance;
}

// Lookup keys for an embedded module: the reference as written, the reference
// plus ".lynx" when it has no extension, and the bare file name.
std::vector<std::string> embeddedLookupKeys(const std::string& requested) {
    std::vector<std::string> keys{requested};
    if (!std::filesystem::path(requested).has_extension()) {
        keys.push_back(requested + ".lynx");
    }
    keys.push_back(std::filesystem::path(requested).filename().string());
    return keys;
}

const std::string* findEmbeddedSource(const std::string& requested) {
    for (const auto& key : embeddedLookupKeys(requested)) {
        const auto found = embeddedModuleSources().find(key);
        if (found != embeddedModuleSources().end()) {
            return &found->second;
        }
    }
    return nullptr;
}

std::string findEmbeddedLibrary(const std::string& requested) {
    for (const auto& key : embeddedLookupKeys(requested)) {
        const auto found = embeddedModuleLibraries().find(key);
        if (found != embeddedModuleLibraries().end()) {
            return found->second;
        }
    }
    return "";
}

}  // namespace

Value callNative(void* address, const std::string& signature,
                 const std::vector<Value>& args, int line, int column) {
    return callNativeInternal(address, signature, args, line, column);
}

void setEmbeddedModuleSources(std::map<std::string, std::string> sources) {
    embeddedModuleSources() = std::move(sources);
}

void setEmbeddedModuleLibraries(std::map<std::string, std::string> libraries) {
    embeddedModuleLibraries() = std::move(libraries);
}

std::vector<ImportRecord> collectImports(const std::string& source,
                                         const std::string& display) {
    Lexer lexer(source, display);
    Parser parser(lexer.scan());
    parser.parseProgram();
    return parser.imports();
}

std::string resolveModulePath(const std::string& sourceDirectory,
                              const std::string& requested) {
    const std::string embedded = findEmbeddedLibrary(requested);
    if (!embedded.empty()) {
        return embedded;
    }
    const std::filesystem::path input(requested);
    std::vector<std::filesystem::path> candidates;
    if (input.has_extension()) {
        candidates.push_back(input);
    } else {
        candidates.push_back(input.string() + ".lynx");
    }

    if (!sourceDirectory.empty()) {
        for (const auto& candidate : candidates) {
            const auto path = std::filesystem::path(sourceDirectory) / candidate;
            if (std::filesystem::exists(path)) {
                return path.string();
            }
        }
    }
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate.string();
        }
    }
    const auto stdlib = std::filesystem::path("stdlib");
    for (const auto& candidate : candidates) {
        const auto path = stdlib / candidate.filename();
        if (std::filesystem::exists(path)) {
            return path.string();
        }
    }
    for (const auto& root : {std::filesystem::path("lynxer/stdlib"),
                             std::filesystem::path("../lynxer/stdlib")}) {
        for (const auto& candidate : candidates) {
            const auto path = root / candidate.filename();
            if (std::filesystem::exists(path)) {
                return path.string();
            }
        }
    }
    return "";
}

// --- Bridged bundled modules -----------------------------------------------
//
// A few built-in families are implemented by a bundled native module rather
// than by the interpreter itself: `sound*` reuses the Rust `sound` stdlib
// module, which already owns the audio backend. The module is loaded on first
// use and its operations are called through the same packed ABI an imported
// module uses, so there is one audio implementation rather than two.

namespace {

std::unordered_map<std::string, NativeRegistration>& bridgedModules() {
    static std::unordered_map<std::string, NativeRegistration> modules;
    return modules;
}

}  // namespace

const NativeRegistration* loadBridgedModule(const std::string& module) {
#if defined(__unix__) || defined(__APPLE__)
    auto& modules = bridgedModules();
    const auto found = modules.find(module);
    if (found != modules.end()) {
        return &found->second;
    }
    const std::string resolved = resolveModulePath("", module);
    if (resolved.empty()) {
        return nullptr;
    }
    void* handle = dlopen(resolved.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        return nullptr;
    }
    auto initializer =
        reinterpret_cast<int (*)(int (*)(const char*, const char*, const char*),
                                 int (*)(const char*, std::int64_t),
                                 int (*)(const char*, const char*))>(
            dlsym(handle, "lynxer_module_init_v1"));
    if (initializer == nullptr) {
        dlclose(handle);
        return nullptr;
    }
    NativeRegistration registration;
    registration.handle = handle;
    activeNativeRegistration = &registration;
    const int status = initializer(nativeRegisterFunction,
                                   nativeRegisterConstant, nativeRegisterType);
    activeNativeRegistration = nullptr;
    if (status != 0) {
        dlclose(handle);
        return nullptr;
    }
    return &modules.emplace(module, std::move(registration)).first->second;
#else
    (void)module;
    return nullptr;
#endif
}

Value callBridgedModule(const std::string& module, const std::string& operation,
                        const std::vector<Value>& args, int line, int column) {
    const NativeRegistration* registration = loadBridgedModule(module);
    if (registration == nullptr) {
        throw SourceError("cannot load the bundled '" + module +
                              "' backend that '" + operation + "' needs",
                          line, column);
    }
    const auto found = registration->functions.find(operation);
    if (found == registration->functions.end()) {
        throw SourceError("'" + module + "' does not provide '" + operation + "'",
                          line, column);
    }
    return callNative(found->second.first, found->second.second, args, line,
                      column);
}

namespace {

std::string findSourceModule(const Environment& environment,
                             const std::string& requested) {
    return resolveModulePath(environment.sourceDirectory(), requested);
}

RecordField* findRecordField(RecordValue& record, const std::string& name) {
    for (auto& field : record.fields) {
        if (field.name == name) {
            return &field;
        }
    }
    return nullptr;
}

// Field mutation with Lynxer error messages (docs: vargroups/structs/classes).
void setRecordField(RecordValue& record, const std::string& name,
                    const Value& value, int line, int column) {
    RecordField* field = findRecordField(record, name);
    if (field == nullptr) {
        throw SourceError("Instance of class '" + record.typeName +
                              "' has no field '" + name + "'",
                          line, column);
    }
    if (field->constant) {
        throw SourceError("Field '" + name + "' of instance '" +
                              record.typeName +
                              "' is const and cannot be changed",
                          line, column);
    }
    if (!typeMatches(field->type, value)) {
        throw SourceError("Field '" + name + "' of instance '" +
                              record.typeName + "' is declared as '" +
                              field->type + "' but received a '" +
                              typeNameOf(value) + "' value",
                          line, column);
    }
    field->value = value;
}

[[noreturn]] void noFieldError(const RecordValue& record,
                               const std::string& name, int line,
                               int column) {
    if (record.kind == RecordKind::VarGroup) {
        throw SourceError("vargroup '" + record.displayName +
                              "' has no field '" + name + "'",
                          line, column);
    }
    if (record.kind == RecordKind::Struct) {
        throw SourceError("Struct '" + record.typeName + "' has no field '" +
                              name + "'",
                          line, column);
    }
    throw SourceError("Instance of class '" + record.typeName +
                          "' has no field or method '" + name + "'",
                      line, column);
}

std::string methodDisplayKey(const std::string& name) { return name; }

// Runs a class method with `this` and parameter bindings; shared by method
// calls and `new` construction.
Value runClassMethod(const std::shared_ptr<RecordValue>& receiver,
                     const ClassMethod& method, std::vector<Value> arguments,
                     Environment& environment, int line, int column) {
    if (arguments.size() != method.params.size()) {
        throw SourceError("method '" + method.name + "' of class '" +
                              receiver->typeName + "' expects " +
                              std::to_string(method.params.size()) +
                              " arguments, received " +
                              std::to_string(arguments.size()),
                          line, column);
    }
    environment.pushScope();
    environment.setVariableRawCurrent(
        "this", Variable{receiver->typeName, receiver, false});
    for (std::size_t index = 0; index < method.params.size(); ++index) {
        Value converted = Environment::convertForType(
            std::move(arguments[index]), method.params[index].first, line,
            column);
        environment.setVariableRawCurrent(
            method.params[index].second,
            Variable{method.params[index].first, std::move(converted), false});
    }
    try {
        for (const auto& statement : method.body) {
            statement->execute(environment);
        }
    } catch (const ReturnControl& control) {
        const Value result = control.hasValue ? control.value : Value{};
        environment.popScope();
        return result;
    }
    environment.popScope();
    return Value{};
}

// Collects the user-variable names a codeblock body references, in source
// order (used by exec's inferred bindings).
template <typename StatementContainer>
void collectNames(const StatementContainer& statements,
                  std::vector<std::string>& names);

const Statement* rawStatement(const Statement* statement) { return statement; }

const Statement* rawStatement(const StatementPtr& statement) {
    return statement.get();
}

void collectExpressionNames(const Expression& expression,
                            std::vector<std::string>& names);

void collectExpressionNames(const Expression& expression,
                            std::vector<std::string>& names) {
    if (const auto* variable = dynamic_cast<const VariableExpression*>(&expression)) {
        names.push_back(variable->name());
        return;
    }
    if (const auto* unary = dynamic_cast<const UnaryExpression*>(&expression)) {
        collectExpressionNames(unary->operandExpr(), names);
        return;
    }
    if (const auto* binary = dynamic_cast<const BinaryExpression*>(&expression)) {
        collectExpressionNames(binary->leftExpr(), names);
        collectExpressionNames(binary->rightExpr(), names);
        return;
    }
    if (const auto* call = dynamic_cast<const CallExpression*>(&expression)) {
        for (const auto& argument : call->arguments()) {
            collectExpressionNames(*argument, names);
        }
        return;
    }
    if (const auto* method = dynamic_cast<const MethodCallExpression*>(&expression)) {
        if (dynamic_cast<const VariableExpression*>(&method->receiver()) == nullptr) {
            collectExpressionNames(method->receiver(), names);
        }
        for (const auto& argument : method->arguments()) {
            collectExpressionNames(*argument, names);
        }
        return;
    }
    if (const auto* dot = dynamic_cast<const DotAccessExpression*>(&expression)) {
        collectExpressionNames(dot->object(), names);
        return;
    }
    if (const auto* interp = dynamic_cast<const InterpStringExpression*>(&expression)) {
        for (const auto& part : interp->expressions()) {
            collectExpressionNames(*part, names);
        }
        return;
    }
    if (const auto* list = dynamic_cast<const ListLiteralExpression*>(&expression)) {
        for (const auto& element : list->elements()) {
            collectExpressionNames(*element, names);
        }
        return;
    }
    if (const auto* tuple = dynamic_cast<const TupleLiteralExpression*>(&expression)) {
        for (const auto& element : tuple->elements()) {
            collectExpressionNames(*element, names);
        }
        return;
    }
    if (const auto* coerce = dynamic_cast<const TypeCoerceExpression*>(&expression)) {
        collectExpressionNames(coerce->inner(), names);
        return;
    }
    if (const auto* created = dynamic_cast<const NewExpression*>(&expression)) {
        for (const auto& argument : created->arguments()) {
            collectExpressionNames(*argument, names);
        }
        return;
    }
}

template <typename StatementContainer>
void collectNames(const StatementContainer& statements,
                  std::vector<std::string>& names) {
    for (const auto& statement : statements) {
        if (const auto* assignment =
                dynamic_cast<const AssignmentStatement*>(rawStatement(statement))) {
            names.push_back(assignment->name());
            collectExpressionNames(assignment->valueExpr(), names);
            continue;
        }
        if (const auto* declaration =
                dynamic_cast<const DeclarationStatement*>(rawStatement(statement))) {
            collectExpressionNames(declaration->valueExpr(), names);
            continue;
        }
        if (const auto* expressionStatement =
                dynamic_cast<const ExpressionStatement*>(rawStatement(statement))) {
            collectExpressionNames(expressionStatement->expression(), names);
            continue;
        }
        if (const auto* ifStatement =
                dynamic_cast<const IfStatement*>(rawStatement(statement))) {
            collectExpressionNames(ifStatement->condition(), names);
            collectNames(ifStatement->thenStatements(), names);
            collectNames(ifStatement->elseStatements(), names);
            continue;
        }
        if (const auto* whileStatement =
                dynamic_cast<const WhileStatement*>(rawStatement(statement))) {
            collectExpressionNames(whileStatement->condition(), names);
            collectNames(whileStatement->statements(), names);
            continue;
        }
        if (const auto* iterateStatement =
                dynamic_cast<const IterateStatement*>(rawStatement(statement))) {
            collectExpressionNames(iterateStatement->count(), names);
            collectNames(iterateStatement->statements(), names);
            continue;
        }
        if (const auto* foreverStatement =
                dynamic_cast<const ForeverStatement*>(rawStatement(statement))) {
            collectNames(foreverStatement->statements(), names);
            continue;
        }
        if (const auto* printStatement =
                dynamic_cast<const ExpressionStatement*>(rawStatement(statement))) {
            (void)printStatement;
            continue;
        }
    }
}

} // namespace


TypeCoerceExpression::TypeCoerceExpression(ExpressionPtr inner,
                                           std::string type)
    : inner_(std::move(inner)), type_(std::move(type)) {}

DotAccessExpression::DotAccessExpression(ExpressionPtr object, std::string field,
                                         int line, int column)
    : object_(std::move(object)), field_(std::move(field)), line_(line),
      column_(column) {}

MethodCallExpression::MethodCallExpression(
    ExpressionPtr object, std::string method, std::vector<ExpressionPtr> arguments,
    int line, int column)
    : object_(std::move(object)), method_(std::move(method)),
      arguments_(std::move(arguments)), line_(line), column_(column) {}

NewExpression::NewExpression(std::string typeName,
                             std::vector<ExpressionPtr> arguments, int line,
                             int column)
    : typeName_(std::move(typeName)), arguments_(std::move(arguments)),
      line_(line), column_(column) {}

AddVarGroupExpression::AddVarGroupExpression(
    ExpressionPtr target, std::string type, std::string field,
    ExpressionPtr value, int line, int column)
    : target_(std::move(target)), type_(std::move(type)),
      field_(std::move(field)), value_(std::move(value)), line_(line),
      column_(column) {}

RemoveVarGroupExpression::RemoveVarGroupExpression(
    ExpressionPtr target, std::string field, int line, int column)
    : target_(std::move(target)), field_(std::move(field)), line_(line),
      column_(column) {}

Value TypeCoerceExpression::evaluate(Environment& environment) const {
    return Environment::convertForType(inner_->evaluate(environment), type_,
                                       0, 0);
}

Value DotAccessExpression::evaluate(Environment& environment) const {
    if (const auto* global =
            dynamic_cast<const VariableExpression*>(object_.get());
        global != nullptr && global->name() == "global") {
        // `global.name` is a variable when one exists, and otherwise names a
        // global function used as a value — which is how a callback is passed
        // to a built-in such as `nativeThreadStart`.
        if (!environment.hasVariable(field_) &&
            environment.findFunction(field_) != nullptr) {
            auto function = std::make_shared<CodeblockValue>();
            function->name = field_;
            return function;
        }
        return environment.get(field_, line_, column_);
    }
    // Enum namespace: identifier names a declared enum; the field is a
    // variant constructed without a payload.
    if (const auto* variable = dynamic_cast<const VariableExpression*>(object_.get())) {
        if (!environment.hasVariable(variable->name())) {
            if (const EnumDef* enumDef =
                    TypeRegistry::instance().findEnum(variable->name())) {
                for (const EnumVariant& variant : enumDef->variants) {
                    if (variant.name == field_) {
                        if (!variant.fields.empty()) {
                            throw SourceError(
                                "variant '" + field_ + "' of enum '" +
                                    variable->name() +
                                    "' requires payload values",
                                line_, column_);
                        }
                        return std::make_shared<EnumValue>(
                            EnumValue{variable->name(), field_, {}, {}});
                    }
                }
                throw SourceError("enum '" + variable->name() +
                                      "' has no variant '" + field_ + "'",
                                  line_, column_);
            }
        }
    }

    const Value object = object_->evaluate(environment);
    if (const auto* record = std::get_if<std::shared_ptr<RecordValue>>(&object)) {
        if (*record == nullptr) {
            throw SourceError("cannot access field of none", line_, column_);
        }
        const RecordField* field = findRecordField(**record, field_);
        if (field == nullptr) {
            noFieldError(**record, field_, line_, column_);
        }
        return field->value;
    }
    if (const auto* enumValue = std::get_if<std::shared_ptr<EnumValue>>(&object)) {
        if (*enumValue == nullptr) {
            throw SourceError("cannot access field of none", line_, column_);
        }
        for (std::size_t index = 0; index < (*enumValue)->fieldNames.size();
             ++index) {
            if ((*enumValue)->fieldNames[index] == field_) {
                return (*enumValue)->payload[index];
            }
        }
        throw SourceError("Enum variant '" + (*enumValue)->variantName +
                              "' has no payload '" + field_ + "'",
                          line_, column_);
    }
    throw SourceError("value of type '" + typeNameOf(object) +
                          "' has no field '" + field_ + "'",
                      line_, column_);
}

Value MethodCallExpression::evaluate(Environment& environment) const {
    // global.<builtin>(...) keeps its builtin-call meaning.
    if (const auto* variable = dynamic_cast<const VariableExpression*>(object_.get())) {
        if (variable->name() == "global") {
            if (isOwnershipBuiltin(method_)) {
                return callOwnershipBuiltin(
                    method_, ownershipArgumentNames(arguments_), environment,
                    line_, column_);
            }
            std::vector<Value> arguments;
            arguments.reserve(arguments_.size());
            for (const auto& argument : arguments_) {
                arguments.push_back(argument->evaluate(environment));
            }
            return callBuiltin(method_, arguments, environment, line_,
                               column_);
        }
        if (variable->name() == "embedPy") {
            throw SourceError(
                "Python bridging (embedPy) is not supported in Lynxer",
                line_, column_);
        }
        if (variable->name() == "async") {
            std::vector<Value> arguments;
            arguments.reserve(arguments_.size());
            for (const auto& argument : arguments_) {
                arguments.push_back(argument->evaluate(environment));
            }
            return environment.callUserFunction(method_, arguments, {}, line_,
                                                column_);
        }
        if (!environment.hasVariable(variable->name())) {
            if (const EnumDef* enumDef =
                    TypeRegistry::instance().findEnum(variable->name())) {
                for (const EnumVariant& variant : enumDef->variants) {
                    if (variant.name == method_) {
                        if (arguments_.size() != variant.fields.size()) {
                            throw SourceError(
                                "variant '" + method_ + "' of enum '" +
                                    variable->name() + "' expects " +
                                    std::to_string(variant.fields.size()) +
                                    " payload values, received " +
                                    std::to_string(arguments_.size()),
                                line_, column_);
                        }
                        std::vector<Value> payload;
                        std::vector<std::string> fieldNames;
                        for (std::size_t index = 0;
                             index < variant.fields.size(); ++index) {
                            payload.push_back(Environment::convertForType(
                                arguments_[index]->evaluate(environment),
                                variant.fields[index].type, line_, column_));
                            fieldNames.push_back(variant.fields[index].name);
                        }
                        return std::make_shared<EnumValue>(EnumValue{
                            variable->name(), method_, std::move(fieldNames),
                            std::move(payload)});
                    }
                }
                throw SourceError("enum '" + variable->name() +
                                      "' has no variant '" + method_ + "'",
                                  line_, column_);
            }
        }
    }

    const Value receiver = object_->evaluate(environment);
    const auto* record = std::get_if<std::shared_ptr<RecordValue>>(&receiver);
    if (record == nullptr || *record == nullptr) {
        throw SourceError("value of type '" + typeNameOf(receiver) +
                              "' has no method '" + method_ + "'",
                          line_, column_);
    }
    if ((*record)->kind != RecordKind::Class) {
        noFieldError(**record, method_, line_, column_);
    }
    const ClassDef* classDef =
        TypeRegistry::instance().findClass((*record)->typeName);
    if (classDef == nullptr) {
        throw SourceError("unknown class '" + (*record)->typeName + "'",
                          line_, column_);
    }
    for (const ClassMethod& method : classDef->methods) {
        if (method.name == method_) {
            std::vector<Value> arguments;
            arguments.reserve(arguments_.size());
            for (const auto& argument : arguments_) {
                arguments.push_back(argument->evaluate(environment));
            }
            return runClassMethod(*record, method, std::move(arguments),
                                  environment, line_, column_);
        }
    }
    noFieldError(**record, method_, line_, column_);
}

Value NewExpression::evaluate(Environment& environment) const {
    std::vector<Value> arguments;
    arguments.reserve(arguments_.size());
    for (const auto& argument : arguments_) {
        arguments.push_back(argument->evaluate(environment));
    }

    if (const StructDef* structDef =
            TypeRegistry::instance().findStruct(typeName_)) {
        if (arguments.size() != structDef->fields.size()) {
            throw SourceError("struct '" + typeName_ + "' expects " +
                                  std::to_string(structDef->fields.size()) +
                                  " arguments, received " +
                                  std::to_string(arguments.size()),
                              line_, column_);
        }
        auto record = std::make_shared<RecordValue>();
        record->typeName = typeName_;
        record->displayName = typeName_;
        record->kind = RecordKind::Struct;
        for (std::size_t index = 0; index < structDef->fields.size(); ++index) {
            RecordField field;
            field.type = structDef->fields[index].type;
            field.name = structDef->fields[index].name;
            field.value = Environment::convertForType(
                std::move(arguments[index]), field.type, line_, column_);
            record->fields.push_back(std::move(field));
        }
        return record;
    }

    if (const ClassDef* classDef =
            TypeRegistry::instance().findClass(typeName_)) {
        auto record = std::make_shared<RecordValue>();
        record->typeName = typeName_;
        record->displayName = typeName_;
        record->kind = RecordKind::Class;
        for (const ClassFieldDef& fieldDef : classDef->fields) {
            RecordField field;
            field.type = fieldDef.type;
            field.name = fieldDef.name;
            field.constant = fieldDef.constant;
            field.value = fieldDef.initializer
                              ? Environment::convertForType(
                                    fieldDef.initializer->evaluate(environment),
                                    fieldDef.type, line_, column_)
                              : Value{};
            record->fields.push_back(std::move(field));
        }
        for (const ClassMethod& method : classDef->methods) {
            if (method.name == "init") {
                runClassMethod(record, method, std::move(arguments), environment,
                               line_, column_);
                return record;
            }
        }
        if (!arguments.empty()) {
            throw SourceError("class '" + typeName_ +
                                  "' has no init() constructor but arguments "
                                  "were given",
                              line_, column_);
        }
        return record;
    }

    throw SourceError("unknown type '" + typeName_ + "'", line_, column_);
}

Value AddVarGroupExpression::evaluate(Environment& environment) const {
    Value target = target_->evaluate(environment);
    auto* record = std::get_if<std::shared_ptr<RecordValue>>(&target);
    if (record == nullptr || *record == nullptr ||
        (*record)->kind != RecordKind::VarGroup) {
        throw SourceError("addVarGroup() expects a vargroup", line_, column_);
    }
    for (const RecordField& field : (*record)->fields) {
        if (field.name == field_) {
            throw SourceError("Duplicate field \"" + field_ +
                                  "\" in vargroup '" + (*record)->displayName +
                                  "'",
                              line_, column_);
        }
    }
    RecordField field;
    field.type = type_;
    field.name = field_;
    field.value = Environment::convertForType(value_->evaluate(environment),
                                              type_, line_, column_);
    (*record)->fields.push_back(std::move(field));
    return Value{};
}

Value RemoveVarGroupExpression::evaluate(Environment& environment) const {
    Value target = target_->evaluate(environment);
    auto* record = std::get_if<std::shared_ptr<RecordValue>>(&target);
    if (record == nullptr || *record == nullptr ||
        (*record)->kind != RecordKind::VarGroup) {
        throw SourceError("removeVarGroup() expects a vargroup", line_,
                          column_);
    }
    for (std::size_t index = 0; index < (*record)->fields.size(); ++index) {
        if ((*record)->fields[index].name == field_) {
            (*record)->fields.erase((*record)->fields.begin() +
                                    static_cast<std::ptrdiff_t>(index));
            return Value{};
        }
    }
    throw SourceError("vargroup '" + (*record)->displayName +
                          "' has no field '" + field_ + "'",
                      line_, column_);
}

VarGroupLiteralExpression::VarGroupLiteralExpression(
    std::vector<VarGroupFieldInit> fields)
    : fields_(std::move(fields)) {}

Value VarGroupLiteralExpression::evaluate(Environment& environment) const {
    auto record = std::make_shared<RecordValue>();
    record->kind = RecordKind::VarGroup;
    record->displayName = "vargroup";
    for (const auto& definition : fields_) {
        RecordField field;
        field.type = definition.type;
        field.name = definition.name;
        field.constant = definition.constant;
        field.value = Environment::convertForType(
            definition.value->evaluate(environment), definition.type, 0, 0);
        record->fields.push_back(std::move(field));
    }
    return record;
}

LiteralExpression::LiteralExpression(Value value) : value_(std::move(value)) {}

Value LiteralExpression::evaluate(Environment&) const { return value_; }

VariableExpression::VariableExpression(std::string name, int line, int column)
    : name_(std::move(name)), line_(line), column_(column) {}

Value VariableExpression::evaluate(Environment& environment) const {
    // A moved variable may not be read until it is reinitialised. Unknown names
    // fall through so the usual "unknown variable" error is reported.
    if (environment.hasVariable(name_)) {
        const std::string error = environment.ownershipError(name_, "read");
        if (!error.empty()) {
            throw SourceError(error, line_, column_);
        }
    }
    return environment.get(name_, line_, column_);
}

UnaryExpression::UnaryExpression(std::string operation, ExpressionPtr operand,
                                 int line, int column)
    : operation_(std::move(operation)), operand_(std::move(operand)),
      line_(line), column_(column) {}

BinaryExpression::BinaryExpression(std::string operation, ExpressionPtr left,
                                   ExpressionPtr right, int line, int column)
    : operation_(std::move(operation)), left_(std::move(left)),
      right_(std::move(right)), line_(line), column_(column) {}

namespace {

// Symbolic spellings are kept working so existing sources still run, but the
// keyword form is the one to use. Each returns the replacement keyword, or
// nullptr when the spelling is already canonical.
const char* deprecatedBinaryReplacement(const std::string& operation) {
    if (operation == "==") return "is";
    if (operation == "!=") return "isnt";
    if (operation == "not is") return "isnt";
    if (operation == "&&") return "and";
    if (operation == "||") return "or";
    if (operation == "!&&") return "nand";
    if (operation == "!||") return "nor";
    if (operation == "&") return "bitand";
    if (operation == "|") return "bitor";
    if (operation == "^") return "bitxor";
    if (operation == "!&") return "bitnand";
    if (operation == "!|") return "bitnor";
    if (operation == "!^") return "bitxnor";
    if (operation == "<<") return "bitleft";
    if (operation == ">>") return "bitright";
    return nullptr;
}

const char* deprecatedUnaryReplacement(const std::string& operation) {
    if (operation == "!!") return "not";
    if (operation == "~") return "bitnot";
    return nullptr;
}

} // namespace

Value UnaryExpression::evaluate(Environment& environment) const {
    if (!warned_ && !environment.deprecationWarningSuppressed()) {
        if (const char* replacement = deprecatedUnaryReplacement(operation_)) {
            warned_ = true;
            std::cerr << "Warning: line " << line_ << ", column " << column_
                      << ": operator '" << operation_ << "' is deprecated; use '"
                      << replacement << "' instead.\n";
        }
    }
    return applyUnary(operation_, operand_->evaluate(environment), line_,
                      column_);
}

Value BinaryExpression::evaluate(Environment& environment) const {
    const Value left = left_->evaluate(environment);
    const bool isAnd = operation_ == "&&" || operation_ == "and";
    const bool isOr = operation_ == "||" || operation_ == "or";
    if (isAnd && !isTruthy(left)) {
        return false;
    }
    if (isOr && isTruthy(left)) {
        return true;
    }
    const Value right = right_->evaluate(environment);

    if (isAnd || isOr) {
        return isTruthy(right);
    }
    if (!warned_ && !environment.deprecationWarningSuppressed()) {
        if (const char* replacement =
                deprecatedBinaryReplacement(operation_)) {
            warned_ = true;
            std::cerr << "Warning: line " << line_ << ", column " << column_
                      << ": operator '" << operation_ << "' is deprecated; use '"
                      << replacement << "' instead.\n";
        }
    }
    return applyBinary(binOpFromString(operation_, line_, column_), left,
                       right, line_, column_);
}

CallExpression::CallExpression(std::string name,
                               std::vector<ExpressionPtr> arguments, int line,
                               int column)
    : name_(std::move(name)), arguments_(std::move(arguments)), line_(line),
      column_(column) {}

void CallExpression::addInlineCodeblock(StatementList body) {
    codeblocks_.push_back(CodeblockArgument{"", std::move(body)});
}

void CallExpression::addNamedCodeblock(std::string name) {
    codeblocks_.push_back(CodeblockArgument{std::move(name), {}});
}

Value CallExpression::evaluate(Environment& environment) const {
    if (isOwnershipBuiltin(name_)) {
        return callOwnershipBuiltin(name_, ownershipArgumentNames(arguments_),
                                    environment, line_, column_);
    }
    std::vector<Value> arguments;
    arguments.reserve(arguments_.size());
    for (const auto& argument : arguments_) {
        arguments.push_back(argument->evaluate(environment));
    }
    std::vector<std::shared_ptr<CodeblockValue>> blocks;
    blocks.reserve(codeblocks_.size());
    for (const auto& codeblock : codeblocks_) {
        if (!codeblock.name.empty()) {
            Value value = environment.get(codeblock.name, line_, column_);
            const auto* stored =
                std::get_if<std::shared_ptr<CodeblockValue>>(&value);
            if (stored == nullptr || *stored == nullptr) {
                throw SourceError("'" + codeblock.name + "' is not a codeblock",
                                  line_, column_);
            }
            blocks.push_back(*stored);
            continue;
        }
        auto block = std::make_shared<CodeblockValue>();
        for (const auto& statement : codeblock.body) {
            block->body.push_back(statement.get());
        }
        blocks.push_back(std::move(block));
    }
    try {
        // A variable holding a function value is called through the name that
        // value carries, so `f(1)` works for `codeblock f = global.worker`.
        if (environment.hasVariable(name_)) {
            const Value stored = environment.get(name_, line_, column_);
            const auto* function =
                std::get_if<std::shared_ptr<CodeblockValue>>(&stored);
            if (function != nullptr && *function != nullptr &&
                !(*function)->name.empty()) {
                return environment.callUserFunction((*function)->name, arguments,
                                                    blocks, line_, column_);
            }
        }
        return environment.callUserFunction(name_, arguments, blocks, line_,
                                            column_);
    } catch (const SourceError& error) {
        if (error.what() != std::string("unknown function '" + name_ + "'")) {
            throw;
        }
    }
    return callBuiltin(name_, arguments, environment, line_, column_);
}

ListLiteralExpression::ListLiteralExpression(
    std::vector<ExpressionPtr> elements)
    : elements_(std::move(elements)) {}

Value ListLiteralExpression::evaluate(Environment& environment) const {
    std::vector<Value> values;
    values.reserve(elements_.size());
    for (const auto& element : elements_) {
        values.push_back(element->evaluate(environment));
    }
    return std::make_shared<List>(List{std::move(values)});
}

TupleLiteralExpression::TupleLiteralExpression(
    std::vector<ExpressionPtr> elements)
    : elements_(std::move(elements)) {}

Value TupleLiteralExpression::evaluate(Environment& environment) const {
    std::vector<Value> values;
    values.reserve(elements_.size());
    for (const auto& element : elements_) {
        values.push_back(element->evaluate(environment));
    }
    return std::make_shared<Tuple>(Tuple{std::move(values)});
}

void InterpStringExpression::addLiteral(std::string text) {
    literals_.push_back(std::move(text));
}

void InterpStringExpression::addExpression(ExpressionPtr expression) {
    expressions_.push_back(std::move(expression));
}

Value InterpStringExpression::evaluate(Environment& environment) const {
    std::vector<Value> values;
    values.reserve(expressions_.size());
    for (const auto& expression : expressions_) {
        values.push_back(expression->evaluate(environment));
    }
    return assembleInterp(literals_, values);
}

DeclarationStatement::DeclarationStatement(std::string type, std::string name,
                                           ExpressionPtr value, int line,
                                           int column, bool constant)
    : type_(std::move(type)), name_(std::move(name)),
      value_(std::move(value)), line_(line), column_(column),
      constant_(constant) {}

void DeclarationStatement::execute(Environment& environment) const {
    // Redeclaring an existing name is a write to it, so the ownership rules
    // apply (a moved variable is reinitialised; a borrowed one is not writable).
    if (environment.hasVariable(name_)) {
        const std::string error = environment.ownershipError(name_, "write to");
        if (!error.empty()) {
            throw SourceError(error, line_, column_);
        }
    }
    Value value = Environment::convertForType(value_->evaluate(environment),
                                              type_, line_, column_);
    if (constant_) {
        environment.declareConstant(name_, type_, std::move(value), line_,
                                     column_);
    } else {
        environment.declare(name_, type_, std::move(value), line_, column_);
    }
}

AssignmentStatement::AssignmentStatement(std::string name, ExpressionPtr value,
                                         int line, int column)
    : name_(std::move(name)), value_(std::move(value)), line_(line),
      column_(column) {}

void AssignmentStatement::execute(Environment& environment) const {
    environment.assign(name_, value_->evaluate(environment), line_, column_);
}

LoopControlStatement::LoopControlStatement(LoopControlKind kind)
    : kind_(kind) {}

void LoopControlStatement::execute(Environment&) const {
    throw LoopControl(kind_);
}

ExpressionStatement::ExpressionStatement(ExpressionPtr expression)
    : expression_(std::move(expression)) {}

void ExpressionStatement::execute(Environment& environment) const {
    expression_->evaluate(environment);
}


namespace {

// Restores temporary bindings saved as (name, snapshot); an absent variable
// is represented by a default-constructed Variable.
void restoreSavedVariables(
    const std::vector<std::pair<std::string, Variable>>& saved,
    Environment& environment) {
    for (const auto& restored : saved) {
        if (restored.second.type.empty() &&
            std::holds_alternative<std::monostate>(restored.second.value)) {
            environment.removeVariable(restored.first);
        } else {
            environment.setVariableRaw(restored.first, restored.second);
        }
    }
}

} // namespace

bool matchPattern(const Expression& pattern, const Value& value,
                  std::vector<std::pair<std::string, Value>>& bindings,
                  Environment& environment, int line, int column) {
    (void)line;
    (void)column;
    if (const auto* variable = dynamic_cast<const VariableExpression*>(&pattern)) {
        if (variable->name() == "_") {
            return true;
        }
        bindings.emplace_back(variable->name(), value);
        return true;
    }
    if (const auto* coerce = dynamic_cast<const TypeCoerceExpression*>(&pattern)) {
        if (const auto* inner = dynamic_cast<const VariableExpression*>(&coerce->inner())) {
            if (inner->name() == "_") {
                return true;
            }
            bindings.emplace_back(inner->name(), value);
            return true;
        }
        return valuesEqual(coerce->evaluate(environment), value);
    }
    if (const auto* listPattern =
            dynamic_cast<const ListLiteralExpression*>(&pattern)) {
        const auto* list = std::get_if<std::shared_ptr<List>>(&value);
        if (list == nullptr || *list == nullptr ||
            (*list)->elements.size() != listPattern->elements().size()) {
            return false;
        }
        for (std::size_t index = 0; index < listPattern->elements().size();
             ++index) {
            if (!matchPattern(*listPattern->elements()[index],
                              (*list)->elements[index], bindings, environment,
                              line, column)) {
                return false;
            }
        }
        return true;
    }
    if (const auto* tuplePattern =
            dynamic_cast<const TupleLiteralExpression*>(&pattern)) {
        const auto* tuple = std::get_if<std::shared_ptr<Tuple>>(&value);
        if (tuple == nullptr || *tuple == nullptr ||
            (*tuple)->elements.size() != tuplePattern->elements().size()) {
            return false;
        }
        for (std::size_t index = 0; index < tuplePattern->elements().size();
             ++index) {
            if (!matchPattern(*tuplePattern->elements()[index],
                              (*tuple)->elements[index], bindings, environment,
                              line, column)) {
                return false;
            }
        }
        return true;
    }
    if (const auto* method = dynamic_cast<const MethodCallExpression*>(&pattern)) {
        const auto* receiver =
            dynamic_cast<const VariableExpression*>(&method->receiver());
        if (receiver == nullptr) {
            return valuesEqual(method->evaluate(environment), value);
        }
        const auto* enumValue = std::get_if<std::shared_ptr<EnumValue>>(&value);
        if (enumValue == nullptr || *enumValue == nullptr ||
            (*enumValue)->enumName != receiver->name() ||
            (*enumValue)->variantName != method->methodName() ||
            (*enumValue)->payload.size() != method->arguments().size()) {
            return false;
        }
        for (std::size_t index = 0; index < method->arguments().size();
             ++index) {
            if (!matchPattern(*method->arguments()[index],
                              (*enumValue)->payload[index], bindings,
                              environment, line, column)) {
                return false;
            }
        }
        return true;
    }
    if (const auto* dot = dynamic_cast<const DotAccessExpression*>(&pattern)) {
        const auto* receiver =
            dynamic_cast<const VariableExpression*>(&dot->object());
        if (receiver == nullptr) {
            return valuesEqual(dot->evaluate(environment), value);
        }
        const auto* enumValue = std::get_if<std::shared_ptr<EnumValue>>(&value);
        return enumValue != nullptr && *enumValue != nullptr &&
               (*enumValue)->enumName == receiver->name() &&
               (*enumValue)->variantName == dot->fieldName() &&
               (*enumValue)->payload.empty();
    }
    return valuesEqual(pattern.evaluate(environment), value);
}

DotAssignmentStatement::DotAssignmentStatement(std::vector<std::string> path,
                                               std::string type,
                                               ExpressionPtr value, int line,
                                               int column)
    : path_(std::move(path)), type_(std::move(type)),
      value_(std::move(value)), line_(line), column_(column) {}

void DotAssignmentStatement::execute(Environment& environment) const {
    Value value = value_->evaluate(environment);
    if (!type_.empty()) {
        value = Environment::convertForType(std::move(value), type_, line_,
                                            column_);
    }
    const Value& base = environment.get(path_[0], line_, column_);
    const auto* baseRecord = std::get_if<std::shared_ptr<RecordValue>>(&base);
    if (baseRecord == nullptr || *baseRecord == nullptr) {
        throw SourceError("value of type '" + typeNameOf(base) +
                              "' has no field '" + path_[1] + "'",
                          line_, column_);
    }
    RecordValue* record = baseRecord->get();
    if (record->kind == RecordKind::VarGroup && type_.empty()) {
        throw SourceError(
            "Vargroup and legacy class-field assignment requires an explicit "
            "type", line_, column_);
    }
    for (std::size_t index = 1; index + 1 < path_.size(); ++index) {
        const RecordField* field = findRecordField(*record, path_[index]);
        if (field == nullptr) {
            noFieldError(*record, path_[index], line_, column_);
        }
        const auto* nested =
            std::get_if<std::shared_ptr<RecordValue>>(&field->value);
        if (nested == nullptr || *nested == nullptr) {
            throw SourceError("value of type '" + typeNameOf(field->value) +
                                  "' has no field '" + path_[index + 1] + "'",
                              line_, column_);
        }
        record = nested->get();
    }
    setRecordField(*record, path_.back(), value, line_, column_);
}

SwitchStatement::SwitchStatement(ExpressionPtr value,
                                 std::vector<SwitchCase> cases, int line,
                                 int column)
    : value_(std::move(value)), cases_(std::move(cases)), line_(line),
      column_(column) {}

void SwitchStatement::execute(Environment& environment) const {
    const Value value = value_->evaluate(environment);
    std::vector<std::pair<std::string, Value>> bindings;
    const StatementList* defaultBody = nullptr;
    for (const SwitchCase& switchCase : cases_) {
        if (switchCase.pattern == nullptr) {
            defaultBody = &switchCase.body;
            continue;
        }
        bindings.clear();
        if (matchPattern(*switchCase.pattern, value, bindings, environment,
                         0, 0)) {
            std::vector<std::pair<std::string, Variable>> saved;
            for (const auto& binding : bindings) {
                saved.emplace_back(binding.first,
                                   environment.hasVariable(binding.first)
                                       ? environment.variableSnapshot(
                                             binding.first)
                                       : Variable{});
                environment.setVariableRawCurrent(
                    binding.first, Variable{"any", binding.second, false});
            }
            try {
                executeStatements(switchCase.body, environment);
            } catch (...) {
                restoreSavedVariables(saved, environment);
                throw;
            }
            restoreSavedVariables(saved, environment);
            return;
        }
    }
    if (defaultBody != nullptr) {
        executeStatements(*defaultBody, environment);
    }
}

TryCatchStatement::TryCatchStatement(StatementList tryStatements,
                                     std::string catchName,
                                     StatementList catchStatements, int line,
                                     int column)
    : tryStatements_(std::move(tryStatements)),
      catchName_(std::move(catchName)),
      catchStatements_(std::move(catchStatements)), line_(line),
      column_(column) {}

void TryCatchStatement::execute(Environment& environment) const {
    try {
        executeStatements(tryStatements_, environment);
    } catch (const SourceError& error) {
        if (!catchName_.empty()) {
            if (environment.hasVariable(catchName_)) {
                const Variable existing =
                    environment.variableSnapshot(catchName_);
                if (existing.constant) {
                    throw SourceError("Cannot bind catch variable '" +
                                          catchName_ +
                                          "': it is declared as const",
                                      line_, column_);
                }
                if (existing.type != "str" && existing.type != "any") {
                    throw SourceError(
                        "Cannot bind catch variable '" + catchName_ +
                            "' as 'str': '" + catchName_ +
                            "' is already declared as '" + existing.type + "'",
                        line_, column_);
                }
                environment.assign(catchName_, std::string(error.what()), line_,
                                   column_);
            } else {
                environment.declare(catchName_, "str",
                                     std::string(error.what()), line_, column_);
            }
        }
        executeStatements(catchStatements_, environment);
    }
}

ReturnStatement::ReturnStatement(ExpressionPtr value, int line, int column)
    : value_(std::move(value)), line_(line), column_(column) {}

void ReturnStatement::execute(Environment& environment) const {
    if (value_ != nullptr) {
        throw ReturnControl{value_->evaluate(environment), true};
    }
    throw ReturnControl{Value{}, false};
}

CodeblockDeclarationStatement::CodeblockDeclarationStatement(
    std::string name, std::vector<std::pair<std::string, std::string>> params,
    StatementList body, int line, int column)
    : name_(std::move(name)), params_(std::move(params)),
      body_(std::move(body)), line_(line), column_(column) {}

void CodeblockDeclarationStatement::execute(Environment& environment) const {
    auto block = std::make_shared<CodeblockValue>();
    block->name = name_;
    block->params = params_;
    block->body.clear();
    for (const auto& statement : body_) {
        block->body.push_back(statement.get());
    }
    environment.declare(name_, "codeblock", block, line_, column_);
}

ExecStatement::ExecStatement(std::vector<ExpressionPtr> arguments,
                             std::string blockName,
                             std::vector<std::pair<std::string, std::string>> params,
                             StatementList body, int line, int column)
    : arguments_(std::move(arguments)), blockName_(std::move(blockName)),
      params_(std::move(params)), body_(std::move(body)), line_(line),
      column_(column) {}

void ExecStatement::execute(Environment& environment) const {
    if (!blockName_.empty()) {
        // Named form: exec(args){{blockName}}
        Value blockValue = environment.get(blockName_, line_, column_);
        const auto* block =
            std::get_if<std::shared_ptr<CodeblockValue>>(&blockValue);
        if (block == nullptr || *block == nullptr) {
            throw SourceError("'" + blockName_ + "' is not a codeblock",
                              line_, column_);
        }
        std::vector<Value> arguments;
        arguments.reserve(arguments_.size());
        for (const auto& argument : arguments_) {
            arguments.push_back(argument->evaluate(environment));
        }
        std::vector<std::pair<std::string, std::string>> params = (*block)->params;
        if (params.empty()) {
            // Infer names from the user variables the body references.
            std::vector<std::string> ordered;
            collectNames((*block)->body, ordered);
            if (ordered.size() != arguments.size()) {
                throw SourceError(
                    "exec() expects " + std::to_string(ordered.size()) +
                        " values for codeblock '" + blockName_ +
                        "', received " + std::to_string(arguments.size()),
                    line_, column_);
            }
            for (const std::string& name : ordered) {
                params.emplace_back("any", name);
            }
        }
        if (params.size() != arguments.size()) {
            throw SourceError("exec() expects " +
                                  std::to_string(params.size()) +
                                  " values, received " +
                                  std::to_string(arguments.size()),
                              line_, column_);
        }
        std::vector<std::pair<std::string, Variable>> saved;
        for (std::size_t index = 0; index < params.size(); ++index) {
            Value converted = Environment::convertForType(
                std::move(arguments[index]), params[index].first, line_,
                column_);
            saved.emplace_back(params[index].second,
                               environment.hasVariable(params[index].second)
                                   ? environment.variableSnapshot(
                                         params[index].second)
                                   : Variable{});
            environment.setVariableRaw(
                params[index].second,
                Variable{params[index].first, std::move(converted), false});
        }
        const std::vector<const Statement*>& body = (*block)->body;
        try {
            for (const auto& statement : body) {
                statement->execute(environment);
            }
        } catch (...) {
            restoreSavedVariables(saved, environment);
            throw;
        }
        restoreSavedVariables(saved, environment);
        return;
    }

    // Inline form: exec(params){ body } — values come from the surrounding
    // scope by parameter name.
    std::vector<std::pair<std::string, Variable>> saved;
    for (const auto& param : params_) {
        Value value = environment.get(param.second, line_, column_);
        value = Environment::convertForType(std::move(value), param.first,
                                            line_, column_);
        saved.emplace_back(param.second,
                           environment.hasVariable(param.second)
                               ? environment.variableSnapshot(param.second)
                               : Variable{});
        environment.setVariableRaw(
            param.second, Variable{param.first, std::move(value), false});
    }
    try {
        executeStatements(body_, environment);
    } catch (...) {
        restoreSavedVariables(saved, environment);
        throw;
    }
    restoreSavedVariables(saved, environment);
}

void FunctionDeclarationStatement::execute(Environment& environment) const {
    environment.registerFunction(function_->name,
                                 std::shared_ptr<void>(function_));
}

Value invokeFunction(
    const Function& function, const std::vector<Value>& args,
    const std::vector<std::shared_ptr<CodeblockValue>>& blocks,
    Environment& environment, int line, int column) {
    if (args.size() > function.parameters.size()) {
        throw SourceError(
            "function '" + function.name + "' expects at most " +
                std::to_string(function.parameters.size()) + " arguments, received " +
                std::to_string(args.size()),
            line, column);
    }
    if (blocks.size() != function.codeblockParameters.size()) {
        throw SourceError(
            "function '" + function.name + "' expects exactly " +
                std::to_string(function.codeblockParameters.size()) +
                " code block(s), but got " + std::to_string(blocks.size()),
            line, column);
    }

    environment.pushScope();
    try {
        for (std::size_t index = 0; index < function.parameters.size(); ++index) {
            Value value;
            if (index < args.size()) {
                value = args[index];
            } else if (function.parameters[index].defaultValue != nullptr) {
                value = function.parameters[index].defaultValue->evaluate(environment);
            } else {
                throw SourceError(
                    "function '" + function.name + "' expects at least " +
                        std::to_string(index + 1) + " arguments, received " +
                        std::to_string(args.size()),
                    line, column);
            }
            value = Environment::convertForType(
                std::move(value), function.parameters[index].type, line, column);
            environment.setVariableRawCurrent(
                function.parameters[index].name,
                Variable{function.parameters[index].type, std::move(value), false});
        }
        for (std::size_t index = 0; index < blocks.size(); ++index) {
            const auto& block = blocks[index];
            environment.setVariableRawCurrent(
                function.codeblockParameters[index],
                Variable{"codeblock", block, false});
        }

        Value result;
        try {
            executeStatements(function.statements, environment);
        } catch (const ReturnControl& control) {
            result = control.hasValue ? control.value : Value{};
        }
        if (function.returnType != "any") {
            result = Environment::convertForType(std::move(result),
                                                  function.returnType, line,
                                                  column);
        }
        environment.popScope();
        return result;
    } catch (...) {
        environment.popScope();
        throw;
    }
}

// One lock guards every evaluation of Lynxer code. It is recursive because a
// nested evaluation (a callback into Lynxer from a native module, say) happens
// on the thread that already holds it.
namespace {

std::recursive_mutex& interpreterLock() {
    static std::recursive_mutex lock;
    return lock;
}

}  // namespace

void lockInterpreter() { interpreterLock().lock(); }

void unlockInterpreter() { interpreterLock().unlock(); }

void executeProgram(const std::unordered_map<std::string, Function>& functions,
                    Environment& environment) {
    // The interpreter is not re-entrant: exactly one thread evaluates Lynxer
    // code at a time. `nativeThread*` runs a callback on another thread, and it
    // releases this lock only while it is blocked waiting for that thread, so
    // the two never evaluate at once. See `nativeThreadJoin` in builtins.cpp.
    std::lock_guard<std::recursive_mutex> interpreterGuard(interpreterLock());
    // A program may leave a native thread running. Join it before the
    // environment it captured goes away — including when main throws.
    struct ThreadReaper {
        ~ThreadReaper() { joinNativeThreadsAtExit(); }
    } threadReaper;
    // Native modules are often imported from inside a source module (for
    // example `game.lynx` importing `game.so`), so the environment captured at
    // attach time belongs to that module and only knows its own functions.
    // Frame callbacks must resolve against the top-level program, so record it
    // here for `lynxerHostInvoke`.
    hostInvokeEnvironment = &environment;
    for (const auto& entry : functions) {
        const Function* function = &entry.second;
        environment.registerFunction(
            entry.first,
            std::shared_ptr<void>(const_cast<Function*>(function),
                                  [](void*) {}));
    }
    environment.setUserFunctionHandler(
        [&functions](const std::string& requested,
                     const std::vector<Value>& args,
                     const std::vector<std::shared_ptr<CodeblockValue>>& blocks,
                     Environment& env, int line, int column) -> Value {
            std::string name = requested;
            const std::size_t dot = name.rfind('.');
            if (dot != std::string::npos) {
                name = name.substr(dot + 1);
            }
            if (const auto local = env.findFunction(name)) {
                return invokeFunction(*std::static_pointer_cast<Function>(local),
                                      args, blocks, env, line, column);
            }
            const auto found = functions.find(name);
            if (found != functions.end()) {
                return invokeFunction(found->second, args, blocks, env, line,
                                      column);
            }
            return callBuiltin(requested, args, env, line, column);
        });

    const auto setup = functions.find("setup");
    if (setup != functions.end()) {
        environment.setSetupInProgress(true);
        try {
            for (const Parameter& parameter : setup->second.parameters) {
                if (parameter.defaultValue == nullptr) {
                    throw SourceError(
                        "global setup() parameters must have defaults", 0, 0);
                }
                Value value = parameter.defaultValue->evaluate(environment);
                value = Environment::convertForType(
                    std::move(value), parameter.type, 0, 0);
                environment.setVariableRawCurrent(
                    parameter.name,
                    Variable{parameter.type, std::move(value), false});
            }
            executeStatements(setup->second.statements, environment);
        } catch (const ReturnControl&) {
            // setup returns are ignored, matching the lifecycle entry point.
        }
    }
    environment.setSetupInProgress(false);

    const std::string entryName =
        environment.mainOverride().empty() ? "main" : environment.mainOverride();
    const auto entry = functions.find(entryName);
    if (entry == functions.end()) {
        throw SourceError("entry-point function '" + entryName + "' was not found",
                          0, 0);
    }
    invokeFunction(entry->second, {}, {}, environment, 0, 0);
}

void ImportStatement::execute(Environment& environment) const {
    const std::string module = moduleNameFromPath(path_);
    const std::string namespaceName = alias_.empty() ? module : alias_;
    const bool nativeImport =
        path_.size() >= 3 &&
        path_.compare(path_.size() - 3, 3, ".so") == 0;
    const std::string importKey = nativeImport ? path_ : module;
    if (module.empty()) {
        throw SourceError("module path has no name", line_, column_);
    }
    if (environment.hasImportedModule(importKey)) {
        if (!alias_.empty()) {
            const auto existing = environment.moduleNamespace(module);
            if (existing != nullptr) {
                environment.registerModuleNamespace(alias_, existing);
                environment.aliasModuleFunctions(module, alias_);
            }
        }
        return;
    }
    if (path_.size() >= 3 &&
        path_.compare(path_.size() - 3, 3, ".so") == 0) {
#if defined(__unix__) || defined(__APPLE__)
        const std::string nativePath =
            findSourceModule(environment, path_).empty()
                ? path_
                : findSourceModule(environment, path_);
        const std::string loadPath =
            std::filesystem::path(nativePath).is_relative()
                ? (std::filesystem::current_path() /
                   std::filesystem::path(nativePath))
                      .string()
                : nativePath;
        void* handle = dlopen(loadPath.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (handle == nullptr) {
            throw SourceError("could not load native module '" + path_ + "'",
                              line_, column_);
        }
        auto initializer =
            reinterpret_cast<int (*)(int (*)(const char*, const char*,
                                             const char*),
                                     int (*)(const char*, std::int64_t),
                                     int (*)(const char*, const char*))>(
                dlsym(handle, "lynxer_module_init_v1"));
        if (initializer == nullptr) {
            dlclose(handle);
            throw SourceError("native module lifecycle failure: missing "
                              "lynxer_module_init_v1 entry point",
                              line_, column_);
        }
        NativeRegistration registration;
        registration.handle = handle;
        activeNativeRegistration = &registration;
        const int status = initializer(nativeRegisterFunction,
                                        nativeRegisterConstant,
                                        nativeRegisterType);
        activeNativeRegistration = nullptr;
        if (status != 0 || !registration.error.empty()) {
            const std::string detail = registration.error.empty()
                                            ? "initializer returned non-zero"
                                            : registration.error;
            dlclose(handle);
            throw SourceError("native module lifecycle failure: " + detail,
                              line_, column_);
        }
        // Optional host API: lets a module call back into the interpreter
        // (frame callbacks) and query the interrupt flag.
        auto attach = reinterpret_cast<int (*)(const LynxerHostApi*)>(
            dlsym(handle, "lynxer_module_attach_v1"));
        if (attach != nullptr) {
            LynxerHostApi host{};
            host.version = 1;
            host.context = &environment;
            host.invoke = lynxerHostInvoke;
            host.interrupted = lynxerHostInterrupted;
            if (attach(&host) != 0) {
                dlclose(handle);
                throw SourceError("native module lifecycle failure: attach "
                                  "rejected the host API",
                                  line_, column_);
            }
        }
        environment.markImportedModule(importKey);
        environment.retainNativeModule(
            std::shared_ptr<void>(handle, [](void* value) { dlclose(value); }));
        auto namespaceValue = std::make_shared<RecordValue>();
        namespaceValue->typeName = "module";
        namespaceValue->displayName = namespaceName;
        namespaceValue->kind = RecordKind::VarGroup;
        for (const auto& entry : registration.constants) {
            namespaceValue->fields.push_back(
                RecordField{"int", entry.first, entry.second, true});
        }
        for (const auto& entry : registration.types) {
            namespaceValue->fields.push_back(
                RecordField{"str", entry.first, entry.second, true});
        }
        environment.registerModuleNamespace(namespaceName, namespaceValue);
        for (const auto& entry : registration.functions) {
            environment.registerModuleFunction(
                namespaceName + "." + entry.first,
                [address = entry.second.first, signature = entry.second.second](
                    const std::string&, const std::vector<Value>& args,
                    const std::vector<std::shared_ptr<CodeblockValue>>&,
                    Environment&, int line, int column) {
                    return callNative(address, signature, args, line, column);
                });
        }
        return;
#else
        throw SourceError("native modules are only supported on POSIX hosts",
                          line_, column_);
#endif
    }
    // A compiled executable carries its module sources in memory; otherwise the
    // module is read from the filesystem.
    std::string source;
    std::string resolved;
    if (const std::string* embedded = findEmbeddedSource(path_);
        embedded != nullptr) {
        resolved = path_;
        source = *embedded;
    } else {
        resolved = findSourceModule(environment, path_);
        if (resolved.empty()) {
            throw SourceError("module '" + path_ + "' was not found", line_,
                              column_);
        }
        std::ifstream input(resolved);
        std::ostringstream content;
        content << input.rdbuf();
        source = content.str();
    }
    Lexer lexer(source, resolved);
    Parser parser(lexer.scan());
    auto functions = std::make_shared<std::unordered_map<std::string, Function>>(
        parser.parseProgram());
    if (optimizerEnabled()) {
        optimizeProgram(*functions, optimizationStats());
    }

    auto moduleEnvironment = std::make_shared<Environment>();
    const std::filesystem::path resolvedPath(resolved);
    if (resolvedPath.has_parent_path()) {
        moduleEnvironment->setSourceDirectory(resolvedPath.parent_path().string());
    }
    for (const auto& entry : *functions) {
        moduleEnvironment->registerFunction(
            entry.first,
            std::shared_ptr<void>(const_cast<Function*>(&entry.second),
                                  [](void*) {}));
    }
    moduleEnvironment->setUserFunctionHandler(
        [functions](const std::string& requested,
                     const std::vector<Value>& args,
                     const std::vector<std::shared_ptr<CodeblockValue>>& blocks,
                     Environment& env, int line, int column) -> Value {
            const auto found = functions->find(requested);
            if (found != functions->end()) {
                return invokeFunction(found->second, args, blocks, env, line,
                                      column);
            }
            return callBuiltin(requested, args, env, line, column);
        });
    environment.markImportedModule(module);
    const auto setup = functions->find("setup");
    if (setup != functions->end()) {
        moduleEnvironment->setSetupInProgress(true);
        try {
            for (const auto& parameter : setup->second.parameters) {
                if (parameter.defaultValue == nullptr) {
                    throw SourceError(
                        "global setup() parameters must have defaults", line_,
                        column_);
                }
                moduleEnvironment->setVariableRawCurrent(
                    parameter.name,
                    Variable{parameter.type,
                             Environment::convertForType(
                                 parameter.defaultValue->evaluate(
                                     *moduleEnvironment),
                                 parameter.type, line_, column_),
                             false});
            }
            executeStatements(setup->second.statements, *moduleEnvironment);
        } catch (...) {
            moduleEnvironment->setSetupInProgress(false);
            throw;
        }
        moduleEnvironment->setSetupInProgress(false);
    }
    auto namespaceValue = std::make_shared<RecordValue>();
    namespaceValue->typeName = "module";
    namespaceValue->displayName = namespaceName;
    namespaceValue->kind = RecordKind::VarGroup;
    for (const auto& variable : moduleEnvironment->currentVariables()) {
        namespaceValue->fields.push_back(
            RecordField{variable.second.type, variable.first,
                        variable.second.value, variable.second.constant});
    }
    environment.registerModuleNamespace(namespaceName, namespaceValue);
    for (const auto& entry : *functions) {
        if (entry.first == "setup" || entry.first == "main") {
            continue;
        }
        environment.registerModuleFunction(
            namespaceName + "." + entry.first,
            [moduleEnvironment, functions, functionName = entry.first](
                const std::string&, const std::vector<Value>& args,
                const std::vector<std::shared_ptr<CodeblockValue>>& blocks,
                Environment&, int line, int column) {
                return invokeFunction(functions->at(functionName), args, blocks,
                                      *moduleEnvironment, line, column);
            });
    }
}

void executeStatements(const StatementList& statements,
                       Environment& environment) {
    // Lynxer uses a single flat scope per function: declarations inside
    // control-flow blocks are visible in the enclosing (function) scope and
    // re-declaring an existing name overwrites it, matching the Python
    // reference. Only function/method invocation pushes a fresh scope.
    for (const auto& statement : statements) {
        throwIfInterrupted();
        statement->execute(environment);
    }
}

IfStatement::IfStatement(ExpressionPtr condition, StatementList thenStatements,
                         StatementList elseStatements, bool hasElse)
    : condition_(std::move(condition)),
      thenStatements_(std::move(thenStatements)),
      elseStatements_(std::move(elseStatements)), hasElse_(hasElse) {}

void IfStatement::execute(Environment& environment) const {
    if (isTruthy(condition_->evaluate(environment))) {
        executeStatements(thenStatements_, environment);
    } else if (hasElse_) {
        executeStatements(elseStatements_, environment);
    }
}

WhileStatement::WhileStatement(ExpressionPtr condition, StatementList statements)
    : condition_(std::move(condition)), statements_(std::move(statements)) {}

void WhileStatement::execute(Environment& environment) const {
    while (isTruthy(condition_->evaluate(environment))) {
        throwIfInterrupted();
        try {
            executeStatements(statements_, environment);
        } catch (const LoopControl& control) {
            if (control.kind == LoopControlKind::Break) {
                break;
            }
            // Continue/restart jumps to the next condition check.
        }
    }
}

ForStatement::ForStatement(StatementPtr initializer, ExpressionPtr condition,
                           StatementPtr update, StatementList statements)
    : initializer_(std::move(initializer)),
      condition_(std::move(condition)), update_(std::move(update)),
      statements_(std::move(statements)) {}

void ForStatement::execute(Environment& environment) const {
    initializer_->execute(environment);
    while (isTruthy(condition_->evaluate(environment))) {
        throwIfInterrupted();
        bool shouldBreak = false;
        try {
            executeStatements(statements_, environment);
        } catch (const LoopControl& control) {
            shouldBreak = control.kind == LoopControlKind::Break;
        }
        if (shouldBreak) {
            break;
        }
        update_->execute(environment);
    }
}

DoWhileStatement::DoWhileStatement(ExpressionPtr condition,
                                   StatementList statements)
    : condition_(std::move(condition)), statements_(std::move(statements)) {}

void DoWhileStatement::execute(Environment& environment) const {
    for (;;) {
        throwIfInterrupted();
        try {
            executeStatements(statements_, environment);
        } catch (const LoopControl& control) {
            if (control.kind == LoopControlKind::Break) {
                break;
            }
            // Continue/restart proceeds to the condition check.
        }
        if (condition_ != nullptr &&
            !isTruthy(condition_->evaluate(environment))) {
            break;
        }
    }
}

IterateStatement::IterateStatement(ExpressionPtr count, StatementList statements,
                                   int line, int column)
    : count_(std::move(count)), statements_(std::move(statements)),
      line_(line), column_(column) {}

void IterateStatement::execute(Environment& environment) const {
    const Value countValue = count_->evaluate(environment);
    if (!std::holds_alternative<std::int64_t>(countValue)) {
        throw SourceError("iterate() count must be an integer", line_, column_);
    }
    const auto count = std::get<std::int64_t>(countValue);
    for (std::int64_t index = 0; index < count; ++index) {
        throwIfInterrupted();
        try {
            executeStatements(statements_, environment);
        } catch (const LoopControl& control) {
            if (control.kind == LoopControlKind::Break) {
                break;
            }
            // Continue/restart proceeds to the next fixed iteration.
        }
    }
}

ForeverStatement::ForeverStatement(StatementList statements, int line,
                                   int column)
    : statements_(std::move(statements)), line_(line), column_(column) {}

void ForeverStatement::execute(Environment& environment) const {
    if (!warned_ && !environment.foreverWarningSuppressed() &&
        !statementsContainBreak(statements_)) {
        warned_ = true;
        const std::string& message = Config::instance().get(
            "warning.forever_no_break",
            "forever() has no break; it will run until the process is "
            "stopped. Add break; or call suppressForeverWarning() in "
            "global setup(){}.");
        std::cerr << "Warning: " << message << '\n';
    }
    for (;;) {
        throwIfInterrupted();
        bool shouldBreak = false;
        try {
            executeStatements(statements_, environment);
        } catch (const LoopControl& control) {
            shouldBreak = control.kind == LoopControlKind::Break;
        }
        if (shouldBreak) {
            return;
        }
        const double seconds = environment.foreverDelay();
        if (seconds > 0.0) {
            const auto deadline = std::chrono::steady_clock::now() +
                                  std::chrono::duration<double>(seconds);
            while (std::chrono::steady_clock::now() < deadline) {
                throwIfInterrupted();
                const std::chrono::duration<double> remaining =
                    deadline - std::chrono::steady_clock::now();
                std::this_thread::sleep_for(
                    std::min(std::chrono::duration<double>(0.05), remaining));
            }
        }
    }
}

bool statementsContainBreak(const StatementList& statements) {
    for (const auto& statement : statements) {
        const LoopControlStatement* control =
            dynamic_cast<const LoopControlStatement*>(statement.get());
        if (control != nullptr) {
            return true;
        }
        if (const auto* ifStatement =
                dynamic_cast<const IfStatement*>(statement.get())) {
            if (statementsContainBreak(ifStatement->thenStatements()) ||
                statementsContainBreak(ifStatement->elseStatements())) {
                return true;
            }
        }
        if (const auto* whileStatement =
                dynamic_cast<const WhileStatement*>(statement.get())) {
            if (statementsContainBreak(whileStatement->statements())) {
                return true;
            }
        }
        if (const auto* forStatement =
                dynamic_cast<const ForStatement*>(statement.get())) {
            if (statementsContainBreak(forStatement->statements())) {
                return true;
            }
        }
        if (const auto* doWhileStatement =
                dynamic_cast<const DoWhileStatement*>(statement.get())) {
            if (statementsContainBreak(doWhileStatement->statements())) {
                return true;
            }
        }
        if (const auto* iterateStatement =
                dynamic_cast<const IterateStatement*>(statement.get())) {
            if (statementsContainBreak(iterateStatement->statements())) {
                return true;
            }
        }
        if (const auto* foreverStatement =
                dynamic_cast<const ForeverStatement*>(statement.get())) {
            if (statementsContainBreak(foreverStatement->statements())) {
                return true;
            }
        }
    }
    return false;
}

// --- AST optimization pass ---------------------------------------------------
//
// Runs once, after parsing and before execution, in interpreted and compiled
// runs alike. It must be semantics-preserving: a folded node computes exactly
// what the interpreter would have, and anything that could throw, warn, or
// otherwise behave differently is left alone so the runtime keeps the original
// behaviour at the original source location.

ExpressionPtr Expression::optimize(OptimizationStats&) { return nullptr; }

void Statement::optimizeChildren(OptimizationStats&) {}

bool Statement::rewrite(StatementList&, OptimizationStats&) { return false; }

namespace {

// A literal whose value is safe to fold into a replacement literal. UInt64 and
// container-like values are excluded: `applyUnary` mishandles UInt64, and
// caching a list/tuple/record/object/codeblock Value would alias one mutable
// object across evaluations or hold non-owning AST pointers.
bool foldableLiteral(const Expression* expression, Value& value) {
    const auto* literal = dynamic_cast<const LiteralExpression*>(expression);
    if (literal == nullptr) {
        return false;
    }
    if (std::holds_alternative<UInt64Value>(literal->value())) {
        return false;
    }
    value = literal->value();
    return true;
}

} // namespace

ExpressionPtr UnaryExpression::optimize(OptimizationStats& stats) {
    operand_ = optimizeExpression(std::move(operand_), stats);
    Value operand;
    if (!foldableLiteral(operand_.get(), operand)) {
        return nullptr;
    }
    // Deprecated spellings must reach the runtime so they still warn.
    if (deprecatedUnaryReplacement(operation_) != nullptr) {
        return nullptr;
    }
    // Negating INT64_MIN is signed overflow; leave it to the runtime.
    if (operation_ == "-") {
        if (const auto* integer = std::get_if<std::int64_t>(&operand)) {
            if (*integer == std::numeric_limits<std::int64_t>::min()) {
                return nullptr;
            }
        }
    }
    try {
        Value folded = applyUnary(operation_, operand, line_, column_);
        ++stats.constantFolds;
        return std::make_unique<LiteralExpression>(folded);
    } catch (const std::exception&) {
        // A literal-only operation that raises stays a runtime error; do not
        // fold it and do not count it.
        return nullptr;
    }
}

ExpressionPtr BinaryExpression::optimize(OptimizationStats& stats) {
    left_ = optimizeExpression(std::move(left_), stats);
    right_ = optimizeExpression(std::move(right_), stats);

    const bool isAnd = operation_ == "&&" || operation_ == "and";
    const bool isOr = operation_ == "||" || operation_ == "or";
    Value left;
    Value right;
    const bool leftLiteral = foldableLiteral(left_.get(), left);
    const bool rightLiteral = foldableLiteral(right_.get(), right);

    // When the left operand decides the result, the runtime never evaluates the
    // right side either, so dropping it is exact.
    if (isAnd && leftLiteral && !isTruthy(left)) {
        ++stats.shortCircuits;
        return std::make_unique<LiteralExpression>(false);
    }
    if (isOr && leftLiteral && isTruthy(left)) {
        ++stats.shortCircuits;
        return std::make_unique<LiteralExpression>(true);
    }
    if (isAnd || isOr) {
        // A non-deciding and/or returns isTruthy(right); both operands must be
        // literals for the fold to be free of side effects.
        if (leftLiteral && rightLiteral) {
            ++stats.constantFolds;
            return std::make_unique<LiteralExpression>(isTruthy(right));
        }
        return nullptr;
    }

    if (!leftLiteral || !rightLiteral) {
        return nullptr;
    }
    // Deprecated spellings must reach the runtime so they still warn.
    if (deprecatedBinaryReplacement(operation_) != nullptr) {
        return nullptr;
    }
    // Integer exponentiation can overflow (signed UB) or iterate for an
    // unbounded time; leave it to the runtime, which already defines it.
    if (operation_ == "**" && std::holds_alternative<std::int64_t>(left) &&
        std::holds_alternative<std::int64_t>(right)) {
        return nullptr;
    }
    try {
        Value folded = applyBinary(binOpFromString(operation_, line_, column_),
                                   left, right, line_, column_);
        ++stats.constantFolds;
        return std::make_unique<LiteralExpression>(folded);
    } catch (const std::exception&) {
        return nullptr;
    }
}

ExpressionPtr AwaitExpression::optimize(OptimizationStats& stats) {
    expression_ = optimizeExpression(std::move(expression_), stats);
    return nullptr;
}

ExpressionPtr CallExpression::optimize(OptimizationStats& stats) {
    for (auto& argument : arguments_) {
        argument = optimizeExpression(std::move(argument), stats);
    }
    for (auto& codeblock : codeblocks_) {
        optimizeStatementList(codeblock.body, stats);
    }
    return nullptr;
}

ExpressionPtr ListLiteralExpression::optimize(OptimizationStats& stats) {
    for (auto& element : elements_) {
        element = optimizeExpression(std::move(element), stats);
    }
    return nullptr;
}

ExpressionPtr TupleLiteralExpression::optimize(OptimizationStats& stats) {
    for (auto& element : elements_) {
        element = optimizeExpression(std::move(element), stats);
    }
    return nullptr;
}

ExpressionPtr TypeCoerceExpression::optimize(OptimizationStats& stats) {
    inner_ = optimizeExpression(std::move(inner_), stats);
    return nullptr;
}

ExpressionPtr DotAccessExpression::optimize(OptimizationStats& stats) {
    object_ = optimizeExpression(std::move(object_), stats);
    return nullptr;
}

ExpressionPtr MethodCallExpression::optimize(OptimizationStats& stats) {
    object_ = optimizeExpression(std::move(object_), stats);
    for (auto& argument : arguments_) {
        argument = optimizeExpression(std::move(argument), stats);
    }
    return nullptr;
}

ExpressionPtr NewExpression::optimize(OptimizationStats& stats) {
    for (auto& argument : arguments_) {
        argument = optimizeExpression(std::move(argument), stats);
    }
    return nullptr;
}

ExpressionPtr AddVarGroupExpression::optimize(OptimizationStats& stats) {
    target_ = optimizeExpression(std::move(target_), stats);
    value_ = optimizeExpression(std::move(value_), stats);
    return nullptr;
}

ExpressionPtr RemoveVarGroupExpression::optimize(OptimizationStats& stats) {
    target_ = optimizeExpression(std::move(target_), stats);
    return nullptr;
}

ExpressionPtr VarGroupLiteralExpression::optimize(OptimizationStats& stats) {
    for (auto& field : fields_) {
        field.value = optimizeExpression(std::move(field.value), stats);
    }
    return nullptr;
}

ExpressionPtr InterpStringExpression::optimize(OptimizationStats& stats) {
    for (auto& expression : expressions_) {
        expression = optimizeExpression(std::move(expression), stats);
    }
    return nullptr;
}

void DeclarationStatement::optimizeChildren(OptimizationStats& stats) {
    value_ = optimizeExpression(std::move(value_), stats);
}

void AssignmentStatement::optimizeChildren(OptimizationStats& stats) {
    value_ = optimizeExpression(std::move(value_), stats);
}

void DotAssignmentStatement::optimizeChildren(OptimizationStats& stats) {
    value_ = optimizeExpression(std::move(value_), stats);
}

void ReturnStatement::optimizeChildren(OptimizationStats& stats) {
    value_ = optimizeExpression(std::move(value_), stats);
}

void ExpressionStatement::optimizeChildren(OptimizationStats& stats) {
    expression_ = optimizeExpression(std::move(expression_), stats);
}

void IfStatement::optimizeChildren(OptimizationStats& stats) {
    condition_ = optimizeExpression(std::move(condition_), stats);
    optimizeStatementList(thenStatements_, stats);
    optimizeStatementList(elseStatements_, stats);
}

bool IfStatement::rewrite(StatementList& out, OptimizationStats& stats) {
    const auto* literal =
        dynamic_cast<const LiteralExpression*>(condition_.get());
    if (literal == nullptr) {
        return false;
    }
    ++stats.deadBranches;
    out = isTruthy(literal->value()) ? std::move(thenStatements_)
                                     : std::move(elseStatements_);
    return true;
}

void WhileStatement::optimizeChildren(OptimizationStats& stats) {
    condition_ = optimizeExpression(std::move(condition_), stats);
    optimizeStatementList(statements_, stats);
}

bool WhileStatement::rewrite(StatementList&, OptimizationStats& stats) {
    const auto* literal =
        dynamic_cast<const LiteralExpression*>(condition_.get());
    if (literal == nullptr || isTruthy(literal->value())) {
        return false;
    }
    ++stats.deadBranches;
    return true;
}

void ForStatement::optimizeChildren(OptimizationStats& stats) {
    if (initializer_) {
        initializer_->optimizeChildren(stats);
    }
    condition_ = optimizeExpression(std::move(condition_), stats);
    if (update_) {
        update_->optimizeChildren(stats);
    }
    optimizeStatementList(statements_, stats);
}

void DoWhileStatement::optimizeChildren(OptimizationStats& stats) {
    condition_ = optimizeExpression(std::move(condition_), stats);
    optimizeStatementList(statements_, stats);
}

void IterateStatement::optimizeChildren(OptimizationStats& stats) {
    count_ = optimizeExpression(std::move(count_), stats);
    optimizeStatementList(statements_, stats);
}

bool IterateStatement::rewrite(StatementList&, OptimizationStats& stats) {
    const auto* literal = dynamic_cast<const LiteralExpression*>(count_.get());
    if (literal == nullptr) {
        return false;
    }
    const auto* count = std::get_if<std::int64_t>(&literal->value());
    if (count == nullptr || *count > 0) {
        return false;
    }
    ++stats.deadBranches;
    return true;
}

void ForeverStatement::optimizeChildren(OptimizationStats& stats) {
    optimizeStatementList(statements_, stats);
}

void SwitchStatement::optimizeChildren(OptimizationStats& stats) {
    value_ = optimizeExpression(std::move(value_), stats);
    for (auto& switchCase : cases_) {
        // Patterns bind identifiers when they match, so they are left intact.
        optimizeStatementList(switchCase.body, stats);
    }
}

void TryCatchStatement::optimizeChildren(OptimizationStats& stats) {
    optimizeStatementList(tryStatements_, stats);
    optimizeStatementList(catchStatements_, stats);
}

void CodeblockDeclarationStatement::optimizeChildren(OptimizationStats& stats) {
    optimizeStatementList(body_, stats);
}

void ExecStatement::optimizeChildren(OptimizationStats& stats) {
    for (auto& argument : arguments_) {
        argument = optimizeExpression(std::move(argument), stats);
    }
    optimizeStatementList(body_, stats);
}

void FunctionDeclarationStatement::optimizeChildren(OptimizationStats& stats) {
    if (function_) {
        optimizeFunction(*function_, stats);
    }
}

// --- AST dump -----------------------------------------------------------------
//
// Renders the parsed program as a position-free, indented tree for `--ast`.
// The layout mirrors the Python reference's `_ast_lines`: a node prints its
// class name, then each field as `name:` one level deeper with the value two
// levels deeper, lists as `list[` ... `]`, and source positions are omitted.

namespace {

std::string reprString(const std::string& text) {
    std::string out = "'";
    for (char character : text) {
        switch (character) {
            case '\\': out += "\\\\"; break;
            case '\'': out += "\\'"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default: out += character;
        }
    }
    out += "'";
    return out;
}

std::string astValueRepr(const Value& value) {
    if (std::holds_alternative<std::monostate>(value)) {
        return "none";
    }
    if (const auto* text = std::get_if<std::string>(&value)) {
        return reprString(*text);
    }
    if (const auto* character = std::get_if<CharValue>(&value)) {
        return reprString(character->text);
    }
    return valueToString(value);
}

void writeIndent(std::ostream& out, int indent) {
    out << std::string(static_cast<std::size_t>(indent), ' ');
}

void dumpScalarLine(std::ostream& out, int indent, const std::string& text) {
    writeIndent(out, indent);
    out << text << '\n';
}

void dumpString(std::ostream& out, int indent, const std::string& text) {
    dumpScalarLine(out, indent, reprString(text));
}

void dumpValueLine(std::ostream& out, int indent, const Value& value) {
    dumpScalarLine(out, indent, astValueRepr(value));
}

void dumpNode(std::ostream& out, int indent, const Expression* expression) {
    if (expression == nullptr) {
        dumpScalarLine(out, indent, "none");
        return;
    }
    expression->dump(out, indent);
}

void dumpNode(std::ostream& out, int indent, const Statement* statement) {
    if (statement == nullptr) {
        dumpScalarLine(out, indent, "none");
        return;
    }
    statement->dump(out, indent);
}

// Field label at nodeIndent+2; its value follows at nodeIndent+4.
void dumpFieldLabel(std::ostream& out, int nodeIndent, const char* name) {
    writeIndent(out, nodeIndent + 2);
    out << name << ":\n";
}

void dumpExprField(std::ostream& out, int nodeIndent, const char* name,
                   const Expression* expression) {
    dumpFieldLabel(out, nodeIndent, name);
    dumpNode(out, nodeIndent + 4, expression);
}

void dumpStmtField(std::ostream& out, int nodeIndent, const char* name,
                   const Statement* statement) {
    dumpFieldLabel(out, nodeIndent, name);
    dumpNode(out, nodeIndent + 4, statement);
}

void dumpStringField(std::ostream& out, int nodeIndent, const char* name,
                     const std::string& text) {
    dumpFieldLabel(out, nodeIndent, name);
    dumpString(out, nodeIndent + 4, text);
}

void dumpValueField(std::ostream& out, int nodeIndent, const char* name,
                    const Value& value) {
    dumpFieldLabel(out, nodeIndent, name);
    dumpValueLine(out, nodeIndent + 4, value);
}

void dumpBoolField(std::ostream& out, int nodeIndent, const char* name,
                   bool value) {
    dumpFieldLabel(out, nodeIndent, name);
    dumpScalarLine(out, nodeIndent + 4, value ? "true" : "false");
}

void dumpExprList(std::ostream& out, int indent,
                  const std::vector<ExpressionPtr>& items) {
    if (items.empty()) {
        dumpScalarLine(out, indent, "list[]");
        return;
    }
    dumpScalarLine(out, indent, "list[");
    for (const auto& item : items) {
        dumpNode(out, indent + 2, item.get());
    }
    dumpScalarLine(out, indent, "]");
}

void dumpStmtList(std::ostream& out, int indent, const StatementList& items) {
    if (items.empty()) {
        dumpScalarLine(out, indent, "list[]");
        return;
    }
    dumpScalarLine(out, indent, "list[");
    for (const auto& item : items) {
        dumpNode(out, indent + 2, item.get());
    }
    dumpScalarLine(out, indent, "]");
}

void dumpStringList(std::ostream& out, int indent,
                    const std::vector<std::string>& items) {
    if (items.empty()) {
        dumpScalarLine(out, indent, "list[]");
        return;
    }
    dumpScalarLine(out, indent, "list[");
    for (const auto& item : items) {
        dumpString(out, indent + 2, item);
    }
    dumpScalarLine(out, indent, "]");
}

void dumpExprListField(std::ostream& out, int nodeIndent, const char* name,
                       const std::vector<ExpressionPtr>& items) {
    dumpFieldLabel(out, nodeIndent, name);
    dumpExprList(out, nodeIndent + 4, items);
}

void dumpStmtListField(std::ostream& out, int nodeIndent, const char* name,
                       const StatementList& items) {
    dumpFieldLabel(out, nodeIndent, name);
    dumpStmtList(out, nodeIndent + 4, items);
}

void dumpStringListField(std::ostream& out, int nodeIndent, const char* name,
                         const std::vector<std::string>& items) {
    dumpFieldLabel(out, nodeIndent, name);
    dumpStringList(out, nodeIndent + 4, items);
}

void dumpParamPairs(std::ostream& out, int indent,
                    const std::vector<std::pair<std::string, std::string>>& params) {
    if (params.empty()) {
        dumpScalarLine(out, indent, "list[]");
        return;
    }
    dumpScalarLine(out, indent, "list[");
    for (const auto& param : params) {
        dumpScalarLine(out, indent + 2, "Parameter");
        dumpStringField(out, indent + 2, "type", param.first);
        dumpStringField(out, indent + 2, "name", param.second);
    }
    dumpScalarLine(out, indent, "]");
}

void dumpParamsField(std::ostream& out, int nodeIndent, const char* name,
                     const std::vector<std::pair<std::string, std::string>>& params) {
    dumpFieldLabel(out, nodeIndent, name);
    dumpParamPairs(out, nodeIndent + 4, params);
}

void dumpFunction(std::ostream& out, const Function& function, int indent) {
    dumpScalarLine(out, indent, "Function");
    dumpStringField(out, indent, "name", function.name);
    dumpFieldLabel(out, indent, "parameters");
    if (function.parameters.empty()) {
        dumpScalarLine(out, indent + 4, "list[]");
    } else {
        dumpScalarLine(out, indent + 4, "list[");
        for (const Parameter& parameter : function.parameters) {
            dumpScalarLine(out, indent + 6, "Parameter");
            dumpStringField(out, indent + 6, "type", parameter.type);
            dumpStringField(out, indent + 6, "name", parameter.name);
            dumpExprField(out, indent + 6, "defaultValue",
                          parameter.defaultValue.get());
        }
        dumpScalarLine(out, indent + 4, "]");
    }
    dumpStringListField(out, indent, "codeblockParameters",
                        function.codeblockParameters);
    dumpStringField(out, indent, "returnType", function.returnType);
    dumpBoolField(out, indent, "isGlobal", function.isGlobal);
    dumpBoolField(out, indent, "isFileFunction", function.isFileFunction);
    dumpStmtListField(out, indent, "statements", function.statements);
}

} // namespace

void LiteralExpression::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "LiteralExpression");
    dumpValueField(out, indent, "value", value_);
}

void VariableExpression::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "VariableExpression");
    dumpStringField(out, indent, "name", name_);
}

void UnaryExpression::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "UnaryExpression");
    dumpStringField(out, indent, "operation", operation_);
    dumpExprField(out, indent, "operand", operand_.get());
}

void AwaitExpression::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "AwaitExpression");
    dumpExprField(out, indent, "expression", expression_.get());
}

void BinaryExpression::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "BinaryExpression");
    dumpStringField(out, indent, "operation", operation_);
    dumpExprField(out, indent, "left", left_.get());
    dumpExprField(out, indent, "right", right_.get());
}

void CallExpression::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "CallExpression");
    dumpStringField(out, indent, "name", name_);
    dumpExprListField(out, indent, "arguments", arguments_);
    dumpFieldLabel(out, indent, "codeblocks");
    if (codeblocks_.empty()) {
        dumpScalarLine(out, indent + 4, "list[]");
    } else {
        dumpScalarLine(out, indent + 4, "list[");
        for (const CodeblockArgument& codeblock : codeblocks_) {
            dumpScalarLine(out, indent + 6, "CodeblockArgument");
            dumpStringField(out, indent + 6, "name", codeblock.name);
            dumpStmtListField(out, indent + 6, "body", codeblock.body);
        }
        dumpScalarLine(out, indent + 4, "]");
    }
}

void ListLiteralExpression::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "ListLiteralExpression");
    dumpExprListField(out, indent, "elements", elements_);
}

void TupleLiteralExpression::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "TupleLiteralExpression");
    dumpExprListField(out, indent, "elements", elements_);
}

void TypeCoerceExpression::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "TypeCoerceExpression");
    dumpExprField(out, indent, "inner", inner_.get());
    dumpStringField(out, indent, "type", type_);
}

void DotAccessExpression::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "DotAccessExpression");
    dumpExprField(out, indent, "object", object_.get());
    dumpStringField(out, indent, "field", field_);
}

void MethodCallExpression::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "MethodCallExpression");
    dumpExprField(out, indent, "receiver", object_.get());
    dumpStringField(out, indent, "method", method_);
    dumpExprListField(out, indent, "arguments", arguments_);
}

void NewExpression::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "NewExpression");
    dumpStringField(out, indent, "typeName", typeName_);
    dumpExprListField(out, indent, "arguments", arguments_);
}

void AddVarGroupExpression::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "AddVarGroupExpression");
    dumpExprField(out, indent, "target", target_.get());
    dumpStringField(out, indent, "type", type_);
    dumpStringField(out, indent, "field", field_);
    dumpExprField(out, indent, "value", value_.get());
}

void RemoveVarGroupExpression::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "RemoveVarGroupExpression");
    dumpExprField(out, indent, "target", target_.get());
    dumpStringField(out, indent, "field", field_);
}

void VarGroupLiteralExpression::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "VarGroupLiteralExpression");
    dumpFieldLabel(out, indent, "fields");
    if (fields_.empty()) {
        dumpScalarLine(out, indent + 4, "list[]");
        return;
    }
    dumpScalarLine(out, indent + 4, "list[");
    for (const VarGroupFieldInit& field : fields_) {
        dumpScalarLine(out, indent + 6, "VarGroupFieldInit");
        dumpStringField(out, indent + 6, "type", field.type);
        dumpStringField(out, indent + 6, "name", field.name);
        dumpBoolField(out, indent + 6, "constant", field.constant);
        dumpExprField(out, indent + 6, "value", field.value.get());
    }
    dumpScalarLine(out, indent + 4, "]");
}

void InterpStringExpression::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "InterpStringExpression");
    dumpStringListField(out, indent, "literals", literals_);
    dumpExprListField(out, indent, "expressions", expressions_);
}

void DeclarationStatement::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "DeclarationStatement");
    dumpStringField(out, indent, "type", type_);
    dumpStringField(out, indent, "name", name_);
    dumpBoolField(out, indent, "constant", constant_);
    dumpExprField(out, indent, "value", value_.get());
}

void AssignmentStatement::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "AssignmentStatement");
    dumpStringField(out, indent, "name", name_);
    dumpExprField(out, indent, "value", value_.get());
}

void DotAssignmentStatement::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "DotAssignmentStatement");
    dumpStringListField(out, indent, "path", path_);
    dumpStringField(out, indent, "type", type_);
    dumpExprField(out, indent, "value", value_.get());
}

void SwitchStatement::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "SwitchStatement");
    dumpExprField(out, indent, "value", value_.get());
    dumpFieldLabel(out, indent, "cases");
    if (cases_.empty()) {
        dumpScalarLine(out, indent + 4, "list[]");
        return;
    }
    dumpScalarLine(out, indent + 4, "list[");
    for (const SwitchCase& switchCase : cases_) {
        dumpScalarLine(out, indent + 6, "SwitchCase");
        dumpExprField(out, indent + 6, "pattern", switchCase.pattern.get());
        dumpStmtListField(out, indent + 6, "body", switchCase.body);
    }
    dumpScalarLine(out, indent + 4, "]");
}

void TryCatchStatement::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "TryCatchStatement");
    dumpStringField(out, indent, "catchName", catchName_);
    dumpStmtListField(out, indent, "try", tryStatements_);
    dumpStmtListField(out, indent, "catch", catchStatements_);
}

void ReturnStatement::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "ReturnStatement");
    dumpExprField(out, indent, "value", value_.get());
}

void CodeblockDeclarationStatement::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "CodeblockDeclarationStatement");
    dumpStringField(out, indent, "name", name_);
    dumpParamsField(out, indent, "parameters", params_);
    dumpStmtListField(out, indent, "body", body_);
}

void ExecStatement::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "ExecStatement");
    dumpExprListField(out, indent, "arguments", arguments_);
    dumpStringField(out, indent, "blockName", blockName_);
    dumpParamsField(out, indent, "parameters", params_);
    dumpStmtListField(out, indent, "body", body_);
}

void FunctionDeclarationStatement::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "FunctionDeclarationStatement");
    dumpFieldLabel(out, indent, "function");
    if (function_ == nullptr) {
        dumpScalarLine(out, indent + 4, "none");
        return;
    }
    dumpFunction(out, *function_, indent + 4);
}

void LoopControlStatement::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "LoopControlStatement");
    dumpStringField(out, indent, "kind",
                    kind_ == LoopControlKind::Break ? "break" : "continue");
}

void ExpressionStatement::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "ExpressionStatement");
    dumpExprField(out, indent, "expression", expression_.get());
}

void ImportStatement::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "ImportStatement");
    dumpStringField(out, indent, "path", path_);
    dumpStringField(out, indent, "alias", alias_);
}

void IfStatement::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "IfStatement");
    dumpExprField(out, indent, "condition", condition_.get());
    dumpBoolField(out, indent, "hasElse", hasElse_);
    dumpStmtListField(out, indent, "then", thenStatements_);
    dumpStmtListField(out, indent, "else", elseStatements_);
}

void WhileStatement::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "WhileStatement");
    dumpExprField(out, indent, "condition", condition_.get());
    dumpStmtListField(out, indent, "body", statements_);
}

void ForStatement::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "ForStatement");
    dumpStmtField(out, indent, "initializer", initializer_.get());
    dumpExprField(out, indent, "condition", condition_.get());
    dumpStmtField(out, indent, "update", update_.get());
    dumpStmtListField(out, indent, "body", statements_);
}

void DoWhileStatement::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "DoWhileStatement");
    dumpExprField(out, indent, "condition", condition_.get());
    dumpStmtListField(out, indent, "body", statements_);
}

void IterateStatement::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "IterateStatement");
    dumpExprField(out, indent, "count", count_.get());
    dumpStmtListField(out, indent, "body", statements_);
}

void ForeverStatement::dump(std::ostream& out, int indent) const {
    dumpScalarLine(out, indent, "ForeverStatement");
    dumpStmtListField(out, indent, "body", statements_);
}

void dumpProgram(std::ostream& out, const std::vector<const Function*>& functions) {
    dumpScalarLine(out, 0, "Program");
    dumpFieldLabel(out, 0, "functions");
    if (functions.empty()) {
        dumpScalarLine(out, 4, "list[]");
        return;
    }
    dumpScalarLine(out, 4, "list[");
    for (const Function* function : functions) {
        dumpFunction(out, *function, 6);
    }
    dumpScalarLine(out, 4, "]");
}

} // namespace lynxer
