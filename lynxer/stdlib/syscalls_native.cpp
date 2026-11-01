#include "lynxer_native_abi.h"
#include "syscall_numbers.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#if defined(__linux__)
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#endif

using RegisterFunction = int (*)(const char*, const char*, const char*);

namespace {

#if defined(__aarch64__)
constexpr bool kArm64 = true;
constexpr bool kX86_64 = false;
#else
constexpr bool kArm64 = false;
constexpr bool kX86_64 = true;
#endif

thread_local std::string lastError;

void setError(const std::string& message) { lastError = message; }

} // namespace

extern "C" const char* syscalls_lastError() { return lastError.c_str(); }

extern "C" const char* syscalls_platform() {
#if defined(__linux__)
    return "linux";
#elif defined(_WIN32)
    return "win32";
#elif defined(__APPLE__)
    return "darwin";
#else
    return "unknown";
#endif
}

extern "C" const char* syscalls_architecture() {
#if defined(__x86_64__) || defined(_M_X64)
    return "amd64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#else
    return "";
#endif
}

extern "C" std::int64_t syscalls_call(const char* name, std::int64_t count,
                                       std::int64_t arg0, std::int64_t arg1,
                                       std::int64_t arg2, std::int64_t arg3,
                                       std::int64_t arg4, std::int64_t arg5) {
    lastError.clear();
    if (name == nullptr || count < 0 || count > 6) {
        setError("syscalls accept a name and at most six arguments");
        return -1;
    }
#if !defined(__linux__) || \
    !(defined(__x86_64__) || defined(__aarch64__))
    setError("named syscalls require a Linux amd64 or arm64 runtime");
    return -1;
#else
    const std::string function(name);
    if ((function == "syscallPollFileDescriptors" && count != 3) ||
        ((function == "syscallPpollFileDescriptors" ||
          function == "syscallWaitForEvents" ||
          function == "syscallWaitForEventsWithSignalMask") &&
         count != 5)) {
        setError(function + " received an invalid argument count");
        return -1;
    }

    bool known = false;
    const long number = syscallNumberFor(function, known);
    if (!known) {
        setError("syscall '" + function +
                 "' is not available on this architecture");
        return -1;
    }

    std::vector<long> words;
    words.reserve(6);
    const std::int64_t arguments[] = {arg0, arg1, arg2, arg3, arg4, arg5};
    for (std::int64_t index = 0; index < count; ++index) {
        words.push_back(static_cast<long>(arguments[index]));
    }

    struct ::timespec timeout {};
    if (function == "syscallPollFileDescriptors" && kArm64) {
        const std::int64_t timeoutMs = arg2;
        if (timeoutMs < 0) {
            words = {static_cast<long>(arg0), static_cast<long>(arg1), 0, 0,
                     static_cast<long>(sizeof(unsigned long))};
        } else {
            timeout.tv_sec = timeoutMs / 1000;
            timeout.tv_nsec = (timeoutMs % 1000) * 1000000L;
            words = {static_cast<long>(arg0), static_cast<long>(arg1),
                     static_cast<long>(
                         reinterpret_cast<std::uintptr_t>(&timeout)),
                     0, static_cast<long>(sizeof(unsigned long))};
        }
    } else if (function == "syscallWaitForEvents" && kX86_64) {
        words.resize(std::min<std::size_t>(words.size(), 4));
    } else if (function == "syscallWaitForEvents" ||
               function == "syscallWaitForEventsWithSignalMask") {
        words.push_back(static_cast<long>(sizeof(unsigned long)));
    }

    while (words.size() < 6) {
        words.push_back(0);
    }
    errno = 0;
    const long result =
        ::syscall(number, words[0], words[1], words[2], words[3], words[4],
                  words[5]);
    if (result == -1 && errno != 0) {
        setError("syscall '" + function + "' failed: " +
                 std::strerror(errno));
        return -1;
    }
    return static_cast<std::int64_t>(result);
#endif
}

extern "C" int lynxer_module_init_v1(RegisterFunction function,
                                      void (*)(const char*, std::int64_t),
                                      void (*)(const char*, const char*)) {
    return function("call", "syscalls_call",
                    "cdecl:int64(cstring,int64,int64,int64,int64,int64,int64,int64)") &&
                   function("lastError", "syscalls_lastError",
                            "cdecl:cstring()") &&
                   function("platform", "syscalls_platform", "cdecl:cstring()") &&
                   function("architecture", "syscalls_architecture",
                            "cdecl:cstring()")
               ? 0
               : 1;
}
