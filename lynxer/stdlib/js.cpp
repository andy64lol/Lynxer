// Lynxer `js` stdlib backend: run JavaScript through a Node.js subprocess.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "subprocess.hpp"

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

static std::string shellQuote(const std::string& value) {
    std::string quoted = "'";
    for (const char character : value) {
        if (character == '\'') {
            quoted += "'\\''";
        } else {
            quoted += character;
        }
    }
    quoted += "'";
    return quoted;
}

static std::string trimTrailingNewlines(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}

static std::string captureCommand(const std::string& command, int& status) {
    auto result = lynxer_subprocess::run(command, 0);
    status = result.status;
    return std::move(result.output);
}

// How long a Node program may run before it is killed. Overridable per host with
// `LYNXER_JS_TIMEOUT` (seconds); defaults to 30.
static int jsTimeoutSeconds() {
    const char* configured = std::getenv("LYNXER_JS_TIMEOUT");
    if (configured != nullptr && *configured != '\0') {
        char* end = nullptr;
        const long parsed = std::strtol(configured, &end, 10);
        if (end != configured && parsed > 0) {
            return static_cast<int>(parsed);
        }
    }
    return 30;
}

// Runs `command` in its own process group and captures stdout and stderr.
static std::string captureWithTimeout(const std::string& command, int seconds,
                                      int& status) {
    auto result = lynxer_subprocess::run(command, seconds);
    status = result.status;
    if (result.timedOut) {
        status = 124;
    }
    return std::move(result.output);
}

// Runs a Node command with the configured timeout. Its stderr is part of the
// result, and a timeout answers an explanatory error.
static std::string runNodeCommand(const std::string& nodeCommand) {
    const int seconds = jsTimeoutSeconds();
    int status = 0;
    const std::string output = captureWithTimeout(nodeCommand, seconds, status);
    if (status == 124) {
        return "Error: node timed out after " + std::to_string(seconds) + "s";
    }
    if (status != 0 && output.empty()) {
        return "Error: node exited with status " + std::to_string(status);
    }
    return output;
}

static bool nodeAvailable() {
    int status = 0;
    captureCommand("node --version >/dev/null 2>&1", status);
    return status == 0;
}

static std::string runNodeSource(const std::string& source) {
    if (!nodeAvailable()) {
        return "Error: node not found on PATH";
    }
    return runNodeCommand("node -e " + shellQuote(source));
}

static std::string runNodeFile(const std::string& path) {
    if (!nodeAvailable()) {
        return "Error: node not found on PATH";
    }
    return runNodeCommand("node " + shellQuote(path));
}

extern "C" const char* js_runJS(const char* code) {
    return stable(runNodeSource(textOrEmpty(code)));
}

extern "C" const char* js_runJSFile(const char* path) {
    return stable(runNodeFile(textOrEmpty(path)));
}

extern "C" const char* js_evalJS(const char* expression) {
    const std::string source =
        "console.log(" + textOrEmpty(expression) + ");";
    return stable(trimTrailingNewlines(runNodeSource(source)));
}

extern "C" std::int64_t js_nodeExists() { return nodeAvailable() ? 1 : 0; }

extern "C" const char* js_nodeVersion() {
    if (!nodeAvailable()) {
        return stable("");
    }
    int status = 0;
    const std::string output =
        trimTrailingNewlines(captureCommand("node --version 2>/dev/null",
                                            status));
    return stable(status == 0 ? output : std::string());
}

extern "C" int lynxer_module_init_v1(RegisterFunction function,
                                     RegisterConstant, RegisterType) {
    return function("runJS", "js_runJS", "cdecl:cstring(cstring)") &&
                   function("runJSFile", "js_runJSFile",
                            "cdecl:cstring(cstring)") &&
                   function("evalJS", "js_evalJS", "cdecl:cstring(cstring)") &&
                   function("nodeVersion", "js_nodeVersion",
                            "cdecl:cstring()") &&
                   function("nodeExists", "js_nodeExists", "cdecl:int64()")
               ? 0
               : 1;
}
