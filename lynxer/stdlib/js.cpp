// Lynxer `js` stdlib backend: run JavaScript through a Node.js subprocess.

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

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

// Runs a shell command, capturing stdout and the exit status. stderr is left on
// the caller's stderr so failures remain visible.
static std::string captureCommand(const std::string& command, int& status) {
    std::string output;
    std::array<char, 256> buffer {};
    FILE* pipe = ::popen(command.c_str(), "r");
    if (pipe == nullptr) {
        status = -1;
        return output;
    }
    while (::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) !=
           nullptr) {
        output += buffer.data();
    }
    const int raw = ::pclose(pipe);
    status = raw;
    return output;
}

static bool nodeAvailable() {
    int status = 0;
    captureCommand("command -v node >/dev/null 2>&1", status);
    return status == 0;
}

static std::string runNodeSource(const std::string& source) {
    if (!nodeAvailable()) {
        return "Error: node not found on PATH";
    }
    char pattern[] = "/tmp/clynxerXXXXXX.js";
    const int descriptor = ::mkstemps(pattern, 3);
    if (descriptor < 0) {
        return "Error: could not create temporary file";
    }
    {
        std::ofstream output(pattern, std::ios::binary | std::ios::trunc);
        output << source;
    }
    ::close(descriptor);
    int status = 0;
    const std::string stdoutText =
        captureCommand("node " + shellQuote(pattern), status);
    ::unlink(pattern);
    if (status != 0 && stdoutText.empty()) {
        return "Error: node exited with status " + std::to_string(status);
    }
    return stdoutText;
}

static std::string runNodeFile(const std::string& path) {
    if (!nodeAvailable()) {
        return "Error: node not found on PATH";
    }
    int status = 0;
    const std::string stdoutText =
        captureCommand("node " + shellQuote(path), status);
    if (status != 0 && stdoutText.empty()) {
        return "Error: node exited with status " + std::to_string(status);
    }
    return stdoutText;
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
