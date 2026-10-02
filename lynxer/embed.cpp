// Embedding runtime: runs a bundled Lynxer program in-process and exposes its
// declared `export`s through the C ABI in `lynxer/lynxer.h`. Shared by every
// library produced by `lynxer --emit-library`.

#include "lynxer.h"

#include "ast.hpp"
#include "bundle.hpp"
#include "error.hpp"
#include "exports.hpp"
#include "interrupt.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "runtime.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lynxer {
namespace {

thread_local std::string lastError;
thread_local std::string cstringResult;
thread_local std::vector<std::uint8_t> bytesResult;

void clearLastError() { lastError.clear(); }

void setLastError(const std::string& message) { lastError = message; }

std::string describeError(const SourceError& error) {
    std::string text;
    if (!error.source.empty()) {
        text += error.source;
        text += ':';
    }
    text += std::to_string(error.line);
    text += ':';
    text += std::to_string(error.column);
    text += ": ";
    text += error.what();
    return text;
}

std::string baseName(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
               0;
}

// Decodes one tagged argument according to the exported C parameter type.
bool decodeArgument(const LynxerFfiArg& argument, ExportType expected,
                    Value& out, std::string& error) {
    switch (expected) {
        case ExportType::Int64:
            if (argument.tag == LYNXER_FFI_ARG_INT) {
                out = static_cast<std::int64_t>(argument.i);
                return true;
            }
            if (argument.tag == LYNXER_FFI_ARG_BOOL) {
                out = argument.i != 0;
                return true;
            }
            if (argument.tag == LYNXER_FFI_ARG_UINT64) {
                out = UInt64Value{static_cast<std::uint64_t>(argument.i)};
                return true;
            }
            error = "expected an integer argument";
            return false;
        case ExportType::Float64:
            if (argument.tag == LYNXER_FFI_ARG_FLOAT) {
                out = argument.f;
                return true;
            }
            if (argument.tag == LYNXER_FFI_ARG_INT ||
                argument.tag == LYNXER_FFI_ARG_UINT64) {
                out = static_cast<double>(argument.i);
                return true;
            }
            error = "expected a numeric argument";
            return false;
        case ExportType::CString:
            if (argument.tag == LYNXER_FFI_ARG_STRING) {
                out = std::string(argument.s == nullptr ? "" : argument.s);
                return true;
            }
            error = "expected a string argument";
            return false;
        case ExportType::Bytes: {
            if (argument.tag != LYNXER_FFI_ARG_BYTES) {
                error = "expected a bytes argument";
                return false;
            }
            if (argument.data_length < 0 ||
                (argument.data == nullptr && argument.data_length != 0)) {
                error = "invalid bytes argument";
                return false;
            }
            auto bytes = std::make_shared<BytesValue>();
            if (argument.data != nullptr && argument.data_length > 0) {
                bytes->data.assign(argument.data,
                                   argument.data + argument.data_length);
            }
            out = bytes;
            return true;
        }
        case ExportType::Void:
            error = "void is not a valid argument type";
            return false;
    }
    error = "unsupported argument type";
    return false;
}

// Encodes the returned Lynxer value according to the exported C return type.
bool encodeResult(const Value& value, ExportType type, LynxerFfiResult& out,
                  std::string& error) {
    out = {};
    switch (type) {
        case ExportType::Int64: {
            if (const auto* integer = std::get_if<std::int64_t>(&value)) {
                out.tag = LYNXER_FFI_INT64;
                out.i = *integer;
                return true;
            }
            if (const auto* flag = std::get_if<bool>(&value)) {
                out.tag = LYNXER_FFI_INT64;
                out.i = *flag ? 1 : 0;
                return true;
            }
            if (const auto* wide = std::get_if<UInt64Value>(&value)) {
                out.tag = LYNXER_FFI_INT64;
                out.i = static_cast<std::int64_t>(wide->value);
                return true;
            }
            error = "exported function did not return an integer";
            return false;
        }
        case ExportType::Float64: {
            if (const auto* number = std::get_if<double>(&value)) {
                out.tag = LYNXER_FFI_FLOAT64;
                out.f = *number;
                return true;
            }
            if (const auto* integer = std::get_if<std::int64_t>(&value)) {
                out.tag = LYNXER_FFI_FLOAT64;
                out.f = static_cast<double>(*integer);
                return true;
            }
            error = "exported function did not return a number";
            return false;
        }
        case ExportType::CString: {
            if (const auto* text = std::get_if<std::string>(&value)) {
                cstringResult = *text;
            } else if (std::holds_alternative<std::monostate>(value)) {
                cstringResult.clear();
            } else {
                cstringResult = valueToString(value);
            }
            out.tag = LYNXER_FFI_CSTRING;
            out.s = cstringResult.c_str();
            return true;
        }
        case ExportType::Bytes: {
            const auto* bytes =
                std::get_if<std::shared_ptr<BytesValue>>(&value);
            if (bytes == nullptr) {
                error = "exported function did not return bytes";
                return false;
            }
            bytesResult = (*bytes)->data;
            out.tag = LYNXER_FFI_BYTES;
            out.data = bytesResult.empty() ? nullptr : bytesResult.data();
            out.data_length = static_cast<std::int64_t>(bytesResult.size());
            return true;
        }
        case ExportType::Void:
            out.tag = LYNXER_FFI_VOID;
            return true;
    }
    error = "unsupported return type";
    return false;
}

} // namespace
} // namespace lynxer

// One embedded program per process: the interpreter keeps process-global
// module maps, the top-level host environment and a single global interpreter
// lock, so a second context is refused rather than cross-wired.
struct LynxerEmbedContext {
    std::unordered_map<std::string, lynxer::Function> functions;
    std::unordered_map<std::string, lynxer::ExportSignature> exports;
    std::unique_ptr<lynxer::Environment> environment;
};

extern "C" LynxerEmbedContext* lynxer_embed_init(const std::uint8_t* archive,
                                                 std::int64_t length) {
    lynxer::clearLastError();
    if (archive == nullptr || length <= 0) {
        lynxer::setLastError("lynxer_embed_init: the program archive is empty");
        return nullptr;
    }
    static std::atomic<bool> active{false};
    bool expected = false;
    if (!active.compare_exchange_strong(expected, true)) {
        lynxer::setLastError(
            "the Lynxer embedding runtime supports one program per process");
        return nullptr;
    }
    try {
        const std::vector<std::uint8_t> payload(archive, archive + length);
        lynxer::ProgramArchive program;
        if (!lynxer::decodeProgramArchive(payload, program)) {
            lynxer::setLastError("lynxer_embed_init: invalid program archive");
            return nullptr;
        }
        std::map<std::string, std::string> libraries;
        std::map<std::string, std::string> assets;
        std::string bundleError;
        if (!lynxer::materializeBundle(program.modules, libraries, assets,
                                       bundleError)) {
            lynxer::setLastError("lynxer_embed_init: " + bundleError);
            return nullptr;
        }
        lynxer::setBundledAssets(std::move(assets));

        std::map<std::string, std::string> sources;
        for (const auto& module : program.modules) {
            if (!module.library.empty()) {
                continue;
            }
            const std::string bare = lynxer::baseName(module.name);
            sources[module.name] = module.source;
            sources[bare] = module.source;
            if (lynxer::endsWith(bare, ".lynx")) {
                sources[bare.substr(0, bare.size() - 5)] = module.source;
            } else {
                sources[bare + ".lynx"] = module.source;
            }
        }
        lynxer::setEmbeddedModuleSources(std::move(sources));
        lynxer::setEmbeddedModuleLibraries(std::move(libraries));

        lynxer::Lexer lexer(program.mainSource, program.mainPath);
        lynxer::Parser parser(lexer.scan(), program.mainPath);
        auto functions = parser.parseProgram(/*requireEntryPoints=*/false);
        const std::vector<lynxer::ExportRecord> exports = parser.exports();
        if (exports.empty()) {
            lynxer::setLastError(
                "lynxer_embed_init: the program declares no exports");
            return nullptr;
        }

        auto context = std::make_unique<LynxerEmbedContext>();
        context->environment = std::make_unique<lynxer::Environment>();
        context->environment->setProgramArguments({program.mainPath});
        const std::size_t slash = program.mainPath.find_last_of('/');
        if (slash != std::string::npos) {
            context->environment->setSourceDirectory(
                program.mainPath.substr(0, slash));
        }
        context->functions = std::move(functions);
        for (const auto& record : exports) {
            lynxer::ExportSignature signature;
            std::string signatureError;
            if (!lynxer::parseExportSignature(record.signature, signature,
                                              signatureError)) {
                lynxer::setLastError("lynxer_embed_init: " + signatureError);
                return nullptr;
            }
            context->exports[record.name] = std::move(signature);
        }

        lynxer::lockInterpreter();
        try {
            lynxer::prepareProgram(context->functions,
                                   *context->environment);
            lynxer::runSetup(context->functions, *context->environment);
        } catch (...) {
            lynxer::unlockInterpreter();
            throw;
        }
        lynxer::unlockInterpreter();
        return context.release();
    } catch (const lynxer::SourceError& error) {
        lynxer::setLastError(
            "lynxer_embed_init: " + lynxer::describeError(error));
        return nullptr;
    } catch (const lynxer::InterruptError&) {
        lynxer::setLastError("lynxer_embed_init: interrupted");
        return nullptr;
    } catch (const lynxer::ExitControl& control) {
        lynxer::setLastError("lynxer_embed_init: the program requested exit (" +
                             std::to_string(control.code) + ")");
        return nullptr;
    } catch (const std::exception& error) {
        lynxer::setLastError(std::string("lynxer_embed_init: ") + error.what());
        return nullptr;
    } catch (...) {
        lynxer::setLastError("lynxer_embed_init: unknown interpreter error");
        return nullptr;
    }
}

extern "C" int lynxer_embed_call(LynxerEmbedContext* context, const char* name,
                                 const LynxerFfiArg* args, std::int64_t argCount,
                                 LynxerFfiResult* out) {
    lynxer::clearLastError();
    if (context == nullptr || name == nullptr || out == nullptr) {
        lynxer::setLastError("lynxer_embed_call: null argument");
        return 1;
    }
    if (argCount < 0 || (argCount > 0 && args == nullptr)) {
        lynxer::setLastError("lynxer_embed_call: invalid argument count");
        return 1;
    }
    const auto signature = context->exports.find(name);
    if (signature == context->exports.end()) {
        lynxer::setLastError(std::string("no exported function named '") + name +
                             "'");
        return 1;
    }
    if (static_cast<std::size_t>(argCount) !=
        signature->second.parameters.size()) {
        lynxer::setLastError(std::string("argument count does not match the "
                                         "exported signature of '") +
                             name + "'");
        return 1;
    }

    lynxer::lockInterpreter();
    struct Unlock {
        ~Unlock() { lynxer::unlockInterpreter(); }
    } unlock;
    lynxer::Environment* saved = lynxer::currentHostInvokeEnvironment();
    struct Restore {
        lynxer::Environment* saved;
        ~Restore() { lynxer::setHostInvokeEnvironment(saved); }
    } restore{saved};
    lynxer::setHostInvokeEnvironment(context->environment.get());

    try {
        std::vector<lynxer::Value> values;
        values.reserve(static_cast<std::size_t>(argCount));
        for (std::int64_t index = 0; index < argCount; ++index) {
            lynxer::Value value;
            std::string argumentError;
            if (!lynxer::decodeArgument(
                    args[index],
                    signature->second.parameters[static_cast<std::size_t>(
                        index)],
                    value, argumentError)) {
                lynxer::setLastError(std::string("exported call '") + name +
                                     "': " + argumentError);
                return 1;
            }
            values.push_back(std::move(value));
        }
        const lynxer::Value result = context->environment->callUserFunction(
            name, values,
            std::vector<std::shared_ptr<lynxer::CodeblockValue>>(), 0, 0);
        std::string resultError;
        if (!lynxer::encodeResult(result, signature->second.returnType, *out,
                                  resultError)) {
            lynxer::setLastError(std::string("exported call '") + name +
                                 "': " + resultError);
            return 1;
        }
        return 0;
    } catch (const lynxer::SourceError& error) {
        lynxer::setLastError(lynxer::describeError(error));
        return 1;
    } catch (const lynxer::InterruptError&) {
        lynxer::setLastError("interrupted");
        return 1;
    } catch (const lynxer::ExitControl& control) {
        lynxer::setLastError("the program requested exit with status " +
                             std::to_string(control.code));
        return 1;
    } catch (const std::exception& error) {
        lynxer::setLastError(error.what());
        return 1;
    } catch (...) {
        lynxer::setLastError("unknown interpreter error");
        return 1;
    }
}

extern "C" const char* lynxer_embed_last_error(void) {
    return lynxer::lastError.c_str();
}

extern "C" void lynxer_embed_reset_error(void) { lynxer::clearLastError(); }
