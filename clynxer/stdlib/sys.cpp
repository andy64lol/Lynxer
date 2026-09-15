#include <cstdint>
#include <cstddef>
#include <string>
#include <utility>
#include <unistd.h>
#include <cstdio>
#if defined(__linux__)
#include <sys/utsname.h>
#endif
using RegisterFunction = int (*)(const char*, const char*, const char*);
using RegisterConstant = int (*)(const char*, std::int64_t);
using RegisterType = int (*)(const char*, const char*);
static const char* stable(std::string value) { thread_local std::string r; r=std::move(value); return r.c_str(); }
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
extern "C" const char* sys_version() {
#if defined(__linux__)
    struct utsname info{}; if (uname(&info) == 0) return stable(info.sysname + std::string(" ") + info.release);
#endif
    return stable("");
}
extern "C" std::int64_t sys_getpid() { return static_cast<std::int64_t>(getpid()); }
extern "C" std::int64_t sys_getMaxSize() { return static_cast<std::int64_t>(SIZE_MAX); }
extern "C" const char* sys_getByteOrder() { const std::uint16_t value=1; return *reinterpret_cast<const std::uint8_t*>(&value) ? "little" : "big"; }
extern "C" const char* sys_getDefaultEncoding() { return "utf-8"; }
extern "C" const char* sys_getFilesystemEncoding() { return "utf-8"; }
extern "C" std::int64_t sys_isatty() { return ::isatty(STDOUT_FILENO) ? 1 : 0; }
extern "C" const char* sys_stdinName() { return "<stdin>"; }
extern "C" const char* sys_stdoutName() { return "<stdout>"; }
extern "C" const char* sys_executable() {
    static thread_local char path[4096];
#if defined(__linux__)
    const auto length = ::readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (length > 0) { path[length] = '\0'; return path; }
#endif
    return "";
}
extern "C" int lynxer_module_init_v1(RegisterFunction f, RegisterConstant, RegisterType) {
    return f("platform","sys_platform","cdecl:cstring()") &&
           f("version","sys_version","cdecl:cstring()") &&
           f("getpid","sys_getpid","cdecl:int64()") &&
           f("getMaxSize","sys_getMaxSize","cdecl:int64()") &&
           f("getByteOrder","sys_getByteOrder","cdecl:cstring()") &&
           f("getDefaultEncoding","sys_getDefaultEncoding","cdecl:cstring()") &&
           f("getFilesystemEncoding","sys_getFilesystemEncoding","cdecl:cstring()") &&
           f("isatty","sys_isatty","cdecl:int64()") &&
           f("stdinName","sys_stdinName","cdecl:cstring()") &&
           f("stdoutName","sys_stdoutName","cdecl:cstring()") &&
           f("executable","sys_executable","cdecl:cstring()") ? 0 : 1;
}
