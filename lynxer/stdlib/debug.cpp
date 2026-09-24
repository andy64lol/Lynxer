// Lynxer `debug` stdlib backend: timers, timestamps, environment and memory.
//
// The assertion, logging and inspection helpers are written in pure Lynxer in
// the wrapper; only the nondeterministic/OS-dependent pieces live here.

#include "native_json.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <map>
#include <string>

#include <sys/resource.h>

using RegisterFunction = int (*)(const char*, const char*, const char*);
using RegisterConstant = int (*)(const char*, std::int64_t);
using RegisterType = int (*)(const char*, const char*);

extern char** environ;

static const char* stable(std::string value) {
    thread_local std::string result;
    result = std::move(value);
    return result.c_str();
}

static std::string textOrEmpty(const char* text) {
    return text == nullptr ? std::string() : std::string(text);
}

static double nowMilliseconds() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration<double, std::milli>(now).count();
}

static std::map<std::string, double>& timers() {
    static std::map<std::string, double> instance;
    return instance;
}

extern "C" const char* debug_timestamp() {
    const std::time_t raw = std::time(nullptr);
    std::tm local {};
    if (::localtime_r(&raw, &local) == nullptr) {
        return stable("");
    }
    char buffer[16];
    if (std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local) == 0) {
        return stable("");
    }
    return stable(std::string(buffer));
}

extern "C" double debug_clock() { return nowMilliseconds(); }

extern "C" double debug_getMemory() {
    struct rusage usage {};
    if (::getrusage(RUSAGE_SELF, &usage) != 0) {
        return -1.0;
    }
    return static_cast<double>(usage.ru_maxrss) / 1024.0;
}

extern "C" const char* debug_envGet(const char* key) {
    const char* value = key == nullptr ? nullptr : std::getenv(key);
    return stable(value == nullptr ? std::string() : std::string(value));
}

extern "C" const char* debug_envAll() {
    native_json::Value object = native_json::makeObject();
    if (environ != nullptr) {
        for (char** entry = environ; *entry != nullptr; ++entry) {
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
                            "cdecl:double(cstring)")
               ? 0
               : 1;
}
