// Lynxer `debug` stdlib backend: timers, timestamps, environment and memory.
//
// The assertion, logging and inspection helpers are written in pure Lynxer in
// the wrapper; only the nondeterministic/OS-dependent pieces live here.

#include "native_json.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define PSAPI_VERSION 2
#include <psapi.h>
#else
#include <sys/resource.h>
extern char** environ;
#endif

using RegisterFunction = int (*)(const char*, const char*, const char*);
using RegisterConstant = int (*)(const char*, std::int64_t);
using RegisterType = int (*)(const char*, const char*);

static const char* stable(std::string value) {
    thread_local std::string result;
    result = std::move(value);
    return result.c_str();
}

static std::string textOrEmpty(const char* text) {
    return text == nullptr ? std::string() : std::string(text);
}

static double nowMilliseconds() {
    const char* configured = std::getenv("LYNXER_DEBUG_TEST_CLOCK_MS");
    if (configured != nullptr && *configured != '\0') {
        char* end = nullptr;
        const double value = std::strtod(configured, &end);
        if (end != configured && *end == '\0') {
            return value;
        }
    }
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration<double, std::milli>(now).count();
}

static std::map<std::string, double>& timers() {
    static std::map<std::string, double> instance;
    return instance;
}

extern "C" const char* debug_timestamp() {
    std::time_t raw = std::time(nullptr);
    const char* configured = std::getenv("LYNXER_DEBUG_TEST_EPOCH");
    if (configured != nullptr && *configured != '\0') {
        char* end = nullptr;
        const long long value = std::strtoll(configured, &end, 10);
        if (end != configured && *end == '\0') {
            raw = static_cast<std::time_t>(value);
        }
    }
    std::tm local {};
#if defined(_WIN32)
    const std::tm* converted = (configured != nullptr && *configured != '\0')
        ? (::gmtime_s(&local, &raw) == 0 ? &local : nullptr)
        : (::localtime_s(&local, &raw) == 0 ? &local : nullptr);
#else
    const std::tm* converted = configured != nullptr && *configured != '\0'
                                   ? ::gmtime_r(&raw, &local)
                                   : ::localtime_r(&raw, &local);
#endif
    if (converted == nullptr) {
        return stable("");
    }
    char buffer[16];
    if (std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local) == 0) {
        return stable("");
    }
    return stable(std::string(buffer));
}

extern "C" double debug_clock() { return nowMilliseconds(); }

static std::filesystem::path crashLogPath() {
    const char* override = std::getenv("LYNXER_CRASH_LOG");
    if (override != nullptr && *override != '\0') {
        return std::filesystem::path(override);
    }
#if defined(_WIN32)
    const char* local = std::getenv("LOCALAPPDATA");
    if (local != nullptr && *local != '\0') {
        return std::filesystem::path(local) / "Lynxer" / "crash.log";
    }
    const char* temp = std::getenv("TEMP");
    if (temp != nullptr && *temp != '\0') {
        return std::filesystem::path(temp) / "lynxer-crash.log";
    }
    return std::filesystem::path("lynxer-crash.log");
#else
    const char* state = std::getenv("XDG_STATE_HOME");
    if (state != nullptr && *state != '\0') {
        return std::filesystem::path(state) / "lynxer" / "crash.log";
    }
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".local" / "state" / "lynxer" /
               "crash.log";
    }
    return std::filesystem::path("/tmp/lynxer-crash.log");
#endif
}

extern "C" const char* debug_crashLogPath() {
    return stable(crashLogPath().string());
}

extern "C" std::int64_t debug_recordCrash(const char* message) {
    const std::filesystem::path path = crashLogPath();
    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
    }
    std::ofstream output(path, std::ios::app);
    if (!output) {
        return 0;
    }
    std::time_t raw = std::time(nullptr);
    std::tm local {};
#if defined(_WIN32)
    if (::localtime_s(&local, &raw) != 0) {
        return 0;
    }
#else
    if (::localtime_r(&raw, &local) == nullptr) {
        return 0;
    }
#endif
    char stamp[32];
    if (std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local) ==
        0) {
        return 0;
    }
    output << "[" << stamp << "] " << textOrEmpty(message) << "\n";
    return output.good() ? 1 : 0;
}

extern "C" double debug_getMemory() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (!::GetProcessMemoryInfo(::GetCurrentProcess(), &counters, sizeof(counters))) return -1.0;
    return static_cast<double>(counters.PeakWorkingSetSize) / (1024.0 * 1024.0);
#else
    struct rusage usage {};
    if (::getrusage(RUSAGE_SELF, &usage) != 0) {
        return -1.0;
    }
    return static_cast<double>(usage.ru_maxrss) / 1024.0;
#endif
}

extern "C" const char* debug_envGet(const char* key) {
    const char* value = key == nullptr ? nullptr : std::getenv(key);
    return stable(value == nullptr ? std::string() : std::string(value));
}

extern "C" const char* debug_envAll() {
    native_json::Value object = native_json::makeObject();
#if defined(_WIN32)
    char** environment = _environ;
#else
    char** environment = environ;
#endif
    if (environment != nullptr) {
        for (char** entry = environment; *entry != nullptr; ++entry) {
            const std::string pair(*entry);
            const std::size_t equals = pair.find('=');
            if (equals == std::string::npos) {
                continue;
            }
            native_json::setField(
                object, pair.substr(0, equals),
                native_json::makeString(pair.substr(equals + 1)));
        }
    }
    return stable(native_json::dump(object, false));
}

extern "C" std::int64_t debug_startTimer(const char* label) {
    timers()[textOrEmpty(label)] = nowMilliseconds();
    return 0;
}

extern "C" double debug_stopTimer(const char* label) {
    const std::string key = textOrEmpty(label);
    const auto found = timers().find(key);
    if (found == timers().end()) {
        return -1.0;
    }
    const double elapsed = nowMilliseconds() - found->second;
    timers().erase(found);
    return elapsed;
}

extern "C" double debug_elapsed(const char* label) {
    const auto found = timers().find(textOrEmpty(label));
    if (found == timers().end()) {
        return -1.0;
    }
    return nowMilliseconds() - found->second;
}

extern "C" int lynxer_module_init_v1(RegisterFunction function,
                                     RegisterConstant, RegisterType) {
    return function("timestamp", "debug_timestamp", "cdecl:cstring()") &&
                   function("clock", "debug_clock", "cdecl:double()") &&
                   function("getMemory", "debug_getMemory",
                            "cdecl:double()") &&
                   function("envGet", "debug_envGet",
                            "cdecl:cstring(cstring)") &&
                   function("envAll", "debug_envAll", "cdecl:cstring()") &&
                   function("startTimer", "debug_startTimer",
                            "cdecl:int64(cstring)") &&
                   function("stopTimer", "debug_stopTimer",
                            "cdecl:double(cstring)") &&
                   function("elapsed", "debug_elapsed",
                            "cdecl:double(cstring)") &&
                   function("crashLogPath", "debug_crashLogPath",
                            "cdecl:cstring()") &&
                   function("recordCrash", "debug_recordCrash",
                            "cdecl:int64(cstring)")
               ? 0
               : 1;
}
