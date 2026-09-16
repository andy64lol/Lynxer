#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <utility>
#include <vector>
#include <unistd.h>
#if defined(__linux__)
#include <sys/utsname.h>
#endif

#include "native_json.hpp"

using RegisterFunction = int (*)(const char*, const char*, const char*);
using RegisterConstant = int (*)(const char*, std::int64_t);
using RegisterType = int (*)(const char*, const char*);

// Keep in sync with the default `version` in clynxer/clynxer.config.
#define CLYNXER_VERSION_TEXT "CLynxer 0.1.8"
#define CLYNXER_VERSION_MAJOR 0
#define CLYNXER_VERSION_MINOR 1
#define CLYNXER_VERSION_MICRO 8

static const char* stable(std::string value) { thread_local std::string r; r=std::move(value); return r.c_str(); }

static std::string executablePath() {
    static thread_local char path[4096];
#if defined(__linux__)
    const auto length = ::readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (length > 0) { path[length] = '\0'; return path; }
#endif
    return "";
}

static std::vector<std::string> processArguments() {
    std::vector<std::string> arguments;
#if defined(__linux__)
    std::ifstream input("/proc/self/cmdline", std::ios::binary);
    if (!input) return arguments;
    std::string content((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>());
    std::string current;
    for (const char character : content) {
        if (character == '\0') { arguments.push_back(current); current.clear(); }
        else { current += character; }
    }
    if (!current.empty()) arguments.push_back(current);
#endif
    return arguments;
}

extern "C" const char* sys_platform() {
#if defined(__linux__)
    return "linux";
#elif defined(__APPLE__)
    return "darwin";
#elif defined(_WIN32)
    return "win32";
#else
    return "unknown";
#endif
}
// The Python reference returns the interpreter's version string; Clynxer has no
// Python runtime, so this reports the CLynxer version instead.
extern "C" const char* sys_version() { return CLYNXER_VERSION_TEXT; }
extern "C" const char* sys_versionInfo() {
    native_json::Value object = native_json::makeObject();
    native_json::setField(object, "major", native_json::makeInteger(CLYNXER_VERSION_MAJOR));
    native_json::setField(object, "minor", native_json::makeInteger(CLYNXER_VERSION_MINOR));
    native_json::setField(object, "micro", native_json::makeInteger(CLYNXER_VERSION_MICRO));
    native_json::setField(object, "releaselevel", native_json::makeString("final"));
    native_json::setField(object, "serial", native_json::makeInteger(0));
    return stable(native_json::dump(object, false));
}
extern "C" const char* sys_implementation() { return "CLynxer"; }
extern "C" const char* sys_apiVersion() { return "0.1"; }
extern "C" std::int64_t sys_isFrozen() { return 0; }
extern "C" std::int64_t sys_getpid() { return static_cast<std::int64_t>(getpid()); }
extern "C" std::int64_t sys_getMaxSize() { return static_cast<std::int64_t>(SIZE_MAX); }
extern "C" const char* sys_getByteOrder() { const std::uint16_t value=1; return *reinterpret_cast<const std::uint8_t*>(&value) ? "little" : "big"; }
extern "C" const char* sys_getDefaultEncoding() { return "utf-8"; }
extern "C" const char* sys_getFilesystemEncoding() { return "utf-8"; }
extern "C" std::int64_t sys_isatty() { return ::isatty(STDOUT_FILENO) ? 1 : 0; }
extern "C" const char* sys_stdinName() { return "<stdin>"; }
extern "C" const char* sys_stdoutName() { return "<stdout>"; }
extern "C" const char* sys_executable() { return stable(executablePath()); }
extern "C" const char* sys_prefix() {
    const std::string path = executablePath();
    const std::size_t slash = path.find_last_of('/');
    return stable(slash == std::string::npos ? std::string() : path.substr(0, slash));
}
extern "C" const char* sys_execPrefix() { return sys_prefix(); }

// Process arguments, not script arguments: Clynxer does not forward extra
// arguments to the program, so argv[0] is the clynxer executable.
extern "C" const char* sys_argv() {
    native_json::Value array = native_json::makeArray();
    for (const auto& argument : processArguments()) {
        array.items.push_back(native_json::makeString(argument));
    }
    return stable(native_json::dump(array, false));
}
extern "C" std::int64_t sys_argCount() {
    return static_cast<std::int64_t>(processArguments().size());
}
extern "C" const char* sys_getArg(std::int64_t index) {
    const std::vector<std::string> arguments = processArguments();
    if (index < 0 || static_cast<std::size_t>(index) >= arguments.size()) {
        return stable("");
    }
    return stable(arguments[static_cast<std::size_t>(index)]);
}
// Exits the process immediately; interpreter cleanup does not run.
extern "C" std::int64_t sys_exit(std::int64_t code) {
    std::exit(static_cast<int>(code));
    return 0;
}

extern "C" int lynxer_module_init_v1(RegisterFunction f, RegisterConstant, RegisterType) {
    return f("platform","sys_platform","cdecl:cstring()") &&
           f("version","sys_version","cdecl:cstring()") &&
           f("versionInfo","sys_versionInfo","cdecl:cstring()") &&
           f("implementation","sys_implementation","cdecl:cstring()") &&
           f("apiVersion","sys_apiVersion","cdecl:cstring()") &&
           f("isFrozen","sys_isFrozen","cdecl:int64()") &&
           f("getpid","sys_getpid","cdecl:int64()") &&
           f("getMaxSize","sys_getMaxSize","cdecl:int64()") &&
           f("getByteOrder","sys_getByteOrder","cdecl:cstring()") &&
           f("getDefaultEncoding","sys_getDefaultEncoding","cdecl:cstring()") &&
           f("getFilesystemEncoding","sys_getFilesystemEncoding","cdecl:cstring()") &&
           f("isatty","sys_isatty","cdecl:int64()") &&
           f("stdinName","sys_stdinName","cdecl:cstring()") &&
           f("stdoutName","sys_stdoutName","cdecl:cstring()") &&
           f("executable","sys_executable","cdecl:cstring()") &&
           f("prefix","sys_prefix","cdecl:cstring()") &&
           f("execPrefix","sys_execPrefix","cdecl:cstring()") &&
           f("argv","sys_argv","cdecl:cstring()") &&
           f("argCount","sys_argCount","cdecl:int64()") &&
           f("getArg","sys_getArg","cdecl:cstring(int64)") &&
           f("exit","sys_exit","cdecl:int64(int64)") ? 0 : 1;
}
