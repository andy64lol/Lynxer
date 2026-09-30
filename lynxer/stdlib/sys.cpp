#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <string>
#include <utility>
#include <vector>
#include <unistd.h>
#if defined(__linux__)
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#endif
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__DragonFly__)
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/types.h>
#endif
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include "lynxer_native_abi.h"
#include "native_json.hpp"

using RegisterFunction = int (*)(const char*, const char*, const char*);
using RegisterConstant = int (*)(const char*, std::int64_t);
using RegisterType = int (*)(const char*, const char*);

// Keep in sync with the default `version` in lynxer/lynxer.config.
#define LYNXER_VERSION_TEXT "Lynxer 0.1.8.1"
#define LYNXER_VERSION_MAJOR 0
#define LYNXER_VERSION_MINOR 1
#define LYNXER_VERSION_MICRO 8
#define LYNXER_VERSION_PATCH 1

static const char* stable(std::string value) { thread_local std::string r; r=std::move(value); return r.c_str(); }

static std::string executablePath() {
    static thread_local char path[4096];
#if defined(__linux__)
    const auto length = ::readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (length > 0) { path[length] = '\0'; return path; }
#elif defined(__APPLE__)
    std::uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        path[sizeof(path) - 1] = '\0';
        return path;
    }
#endif
    return "";
}

// The interpreter's own command line, used only when the module is loaded
// without a host (a missing attach), so `argv` still has an answer.
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

// Host services handed over by the interpreter; `program_args` reports the
// program's own command line rather than the interpreter's.
static LynxerHostApi hostApi{};
static bool hostAttached = false;

extern "C" int lynxer_module_attach_v1(const LynxerHostApi* host) {
    if (host == nullptr || host->version != 1) {
        return 1;
    }
    hostApi = *host;
    hostAttached = true;
    return 0;
}

// A minimal parser for the JSON array of strings the host produces. It decodes
// the escapes that encoder emits (`\"`, `\\`, `\n`, `\r`, `\t`, `\u00XX`).
static std::vector<std::string> parseStringArray(const char* text) {
    std::vector<std::string> values;
    if (text == nullptr) return values;
    const char* cursor = text;
    while (*cursor != '\0' && *cursor != '[') ++cursor;
    if (*cursor != '[') return values;
    ++cursor;
    while (*cursor != '\0') {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' ||
               *cursor == '\r' || *cursor == ',') {
            ++cursor;
        }
        if (*cursor == ']' || *cursor == '\0') break;
        if (*cursor != '"') break;
        ++cursor;
        std::string value;
        while (*cursor != '\0' && *cursor != '"') {
            if (*cursor == '\\' && cursor[1] != '\0') {
                ++cursor;
                switch (*cursor) {
                    case 'n': value += '\n'; break;
                    case 'r': value += '\r'; break;
                    case 't': value += '\t'; break;
                    case '"': value += '"'; break;
                    case '\\': value += '\\'; break;
                    case '/': value += '/'; break;
                    case 'u': {
                        unsigned code = 0;
                        int digits = 0;
                        for (int i = 1; i <= 4 && cursor[i] != '\0'; ++i) {
                            const char digit = cursor[i];
                            int number = -1;
                            if (digit >= '0' && digit <= '9') number = digit - '0';
                            else if (digit >= 'a' && digit <= 'f') number = digit - 'a' + 10;
                            else if (digit >= 'A' && digit <= 'F') number = digit - 'A' + 10;
                            else break;
                            code = code * 16 + static_cast<unsigned>(number);
                            digits += 1;
                        }
                        if (digits == 4) {
                            value += static_cast<char>(code & 0xFF);
                            cursor += 4;
                        }
                        break;
                    }
                    default: value += *cursor; break;
                }
                ++cursor;
            } else {
                value += *cursor;
                ++cursor;
            }
        }
        if (*cursor == '"') ++cursor;
        values.push_back(value);
    }
    return values;
}

// The program's own arguments: the script path (or compiled executable) and
// everything after it. Falls back to the process command line when the module
// was loaded without a host.
static std::vector<std::string> programArguments() {
    if (hostAttached && hostApi.program_args != nullptr) {
        const char* json = hostApi.program_args(hostApi.context);
        if (json != nullptr) {
            return parseStringArray(json);
        }
    }
    return processArguments();
}

extern "C" const char* sys_platform() {
#if defined(__linux__)
    return "linux";
#elif defined(__APPLE__)
    return "darwin";
#elif defined(__FreeBSD__)
    return "freebsd";
#elif defined(__NetBSD__)
    return "netbsd";
#elif defined(__OpenBSD__)
    return "openbsd";
#elif defined(__DragonFly__)
    return "dragonfly";
#elif defined(_WIN32)
    return "win32";
#else
    return "unknown";
#endif
}
// The canonical syscall architecture of this build — the same word syscalls()
// accepts: amd64 on x86-64, arm64 on aarch64.
extern "C" const char* sys_architecture() {
#if defined(__x86_64__)
    return "amd64";
#elif defined(__aarch64__)
    return "arm64";
#else
    return "";
#endif
}

// Online processor count, or 0 when unavailable. `sysconf` is POSIX, so this
// answers on Linux, macOS and the BSDs alike.
extern "C" std::int64_t sys_cpuCount() {
#if defined(_SC_NPROCESSORS_ONLN)
    const long count = ::sysconf(_SC_NPROCESSORS_ONLN);
    return count > 0 ? static_cast<std::int64_t>(count) : 0;
#else
    return 0;
#endif
}

// Memory page size in bytes, or 0 when unavailable.
extern "C" std::int64_t sys_pageSize() {
#if defined(_SC_PAGESIZE)
    const long size = ::sysconf(_SC_PAGESIZE);
    return size > 0 ? static_cast<std::int64_t>(size) : 0;
#else
    return 0;
#endif
}

#if defined(__linux__)
static bool readSysinfo(struct sysinfo& info) {
    return ::sysinfo(&info) == 0;
}
#endif

// macOS and FreeBSD/DragonFly expose `sysctlbyname`; the other BSDs do not, and
// fall back to the 0/[] sentinels.
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__DragonFly__)
#define LYNXER_BSD_SYSCTL 1
#endif

#if defined(LYNXER_BSD_SYSCTL)
static bool sysctlByName(const char* name, void* value, std::size_t size) {
    std::size_t length = size;
    return ::sysctlbyname(name, value, &length, nullptr, 0) == 0;
}

static bool sysctlU64(const char* name, std::uint64_t& value) {
    std::uint64_t result = 0;
    if (!sysctlByName(name, &result, sizeof(result))) {
        return false;
    }
    value = result;
    return true;
}

static bool readBootTime(struct timeval& boot) {
    return sysctlByName("kern.boottime", &boot, sizeof(boot));
}
#endif

// Total physical memory in bytes, or 0 when unavailable.
extern "C" std::int64_t sys_memoryTotal() {
#if defined(__linux__)
    struct sysinfo info {};
    return readSysinfo(info)
               ? static_cast<std::int64_t>(info.totalram) * info.mem_unit
               : 0;
#elif defined(LYNXER_BSD_SYSCTL)
    std::uint64_t bytes = 0;
    if (sysctlU64("hw.memsize", bytes) || sysctlU64("hw.physmem", bytes)) {
        return static_cast<std::int64_t>(bytes);
    }
    return 0;
#else
    return 0;
#endif
}

// Available (free) physical memory in bytes, or 0 when unavailable.
extern "C" std::int64_t sys_memoryAvailable() {
#if defined(__linux__)
    struct sysinfo info {};
    return readSysinfo(info)
               ? static_cast<std::int64_t>(info.freeram) * info.mem_unit
               : 0;
#elif defined(LYNXER_BSD_SYSCTL)
    std::uint64_t pages = 0;
    if (!sysctlU64("vm.page_free_count", pages) &&
        !sysctlU64("vm.stats.vm.v_free_count", pages)) {
        return 0;
    }
    return static_cast<std::int64_t>(pages) * sys_pageSize();
#else
    return 0;
#endif
}

// Seconds since boot, or 0 when unavailable.
extern "C" std::int64_t sys_uptime() {
#if defined(__linux__)
    struct sysinfo info {};
    return readSysinfo(info) ? static_cast<std::int64_t>(info.uptime) : 0;
#elif defined(LYNXER_BSD_SYSCTL)
    struct timeval boot {};
    if (!readBootTime(boot) || boot.tv_sec == 0) {
        return 0;
    }
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    return now > static_cast<std::int64_t>(boot.tv_sec)
               ? now - static_cast<std::int64_t>(boot.tv_sec)
               : 0;
#else
    return 0;
#endif
}

// Approximate boot time as a Unix timestamp, or 0 when unavailable.
extern "C" std::int64_t sys_bootTime() {
#if defined(__linux__)
    struct sysinfo info {};
    if (!readSysinfo(info)) {
        return 0;
    }
    return static_cast<std::int64_t>(std::time(nullptr)) -
           static_cast<std::int64_t>(info.uptime);
#elif defined(LYNXER_BSD_SYSCTL)
    struct timeval boot {};
    return readBootTime(boot) ? static_cast<std::int64_t>(boot.tv_sec) : 0;
#else
    return 0;
#endif
}

// Load averages over 1, 5 and 15 minutes as a JSON array. `getloadavg` is
// available on Linux, macOS and the BSDs.
extern "C" const char* sys_loadAverage() {
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) || \
    defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
    double loads[3] = {0.0, 0.0, 0.0};
    if (::getloadavg(loads, 3) < 0) {
        return stable("[]");
    }
    native_json::Value array = native_json::makeArray();
    array.items.push_back(native_json::makeNumber(loads[0]));
    array.items.push_back(native_json::makeNumber(loads[1]));
    array.items.push_back(native_json::makeNumber(loads[2]));
    return stable(native_json::dump(array, false));
#else
    return stable("[]");
#endif
}
// The Python reference returns the interpreter's version string; Lynxer has no
// Python runtime, so this reports the Lynxer version instead.
extern "C" const char* sys_version() { return LYNXER_VERSION_TEXT; }
extern "C" const char* sys_versionInfo() {
    native_json::Value object = native_json::makeObject();
    native_json::setField(object, "major", native_json::makeInteger(LYNXER_VERSION_MAJOR));
    native_json::setField(object, "minor", native_json::makeInteger(LYNXER_VERSION_MINOR));
    native_json::setField(object, "micro", native_json::makeInteger(LYNXER_VERSION_MICRO));
    native_json::setField(object, "patch", native_json::makeInteger(LYNXER_VERSION_PATCH));
    native_json::setField(object, "releaselevel", native_json::makeString("final"));
    native_json::setField(object, "serial", native_json::makeInteger(0));
    return stable(native_json::dump(object, false));
}
extern "C" const char* sys_implementation() { return "Lynxer"; }
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

// The program's own arguments: entry 0 is the script path (or the compiled
// executable), followed by the arguments passed after it.
extern "C" const char* sys_argv() {
    native_json::Value array = native_json::makeArray();
    for (const auto& argument : programArguments()) {
        array.items.push_back(native_json::makeString(argument));
    }
    return stable(native_json::dump(array, false));
}
extern "C" std::int64_t sys_argCount() {
    return static_cast<std::int64_t>(programArguments().size());
}
extern "C" const char* sys_getArg(std::int64_t index) {
    const std::vector<std::string> arguments = programArguments();
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
           f("architecture","sys_architecture","cdecl:cstring()") &&
           f("cpuCount","sys_cpuCount","cdecl:int64()") &&
           f("pageSize","sys_pageSize","cdecl:int64()") &&
           f("memoryTotal","sys_memoryTotal","cdecl:int64()") &&
           f("memoryAvailable","sys_memoryAvailable","cdecl:int64()") &&
           f("uptime","sys_uptime","cdecl:int64()") &&
           f("bootTime","sys_bootTime","cdecl:int64()") &&
           f("loadAverage","sys_loadAverage","cdecl:cstring()") &&
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
