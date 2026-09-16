// Lynxer `cli` stdlib backend: argument, environment, terminal and process
// helpers for command-line programs.
//
// The Python reference also exposes Click/Typer command builders; those wrap
// Python packages and have no Clynxer equivalent, so they are intentionally
// absent (see clynxer/docs/limitations.md).

#include "native_json.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

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

static std::vector<std::string> processArguments() {
    std::vector<std::string> arguments;
    std::ifstream input("/proc/self/cmdline", std::ios::binary);
    if (!input) {
        return arguments;
    }
    std::string content((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>());
    std::string current;
    for (const char character : content) {
        if (character == '\0') {
            arguments.push_back(current);
            current.clear();
        } else {
            current += character;
        }
    }
    if (!current.empty()) {
        arguments.push_back(current);
    }
    return arguments;
}

extern "C" const char* cli_argv() {
    native_json::Value array = native_json::makeArray();
    for (const auto& argument : processArguments()) {
        array.items.push_back(native_json::makeString(argument));
    }
    return stable(native_json::dump(array, false));
}

extern "C" std::int64_t cli_argCount() {
    return static_cast<std::int64_t>(processArguments().size());
}

extern "C" const char* cli_getArg(std::int64_t index) {
    const std::vector<std::string> arguments = processArguments();
    if (index < 0 || static_cast<std::size_t>(index) >= arguments.size()) {
        return stable("");
    }
    return stable(arguments[static_cast<std::size_t>(index)]);
}

extern "C" const char* cli_envGet(const char* name) {
    const char* value = name == nullptr ? nullptr : std::getenv(name);
    return stable(value == nullptr ? std::string() : std::string(value));
}

extern "C" std::int64_t cli_envHas(const char* name) {
    return name != nullptr && std::getenv(name) != nullptr ? 1 : 0;
}

extern "C" const char* cli_envAll() {
    native_json::Value object = native_json::makeObject();
    for (char** entry = environ; entry != nullptr && *entry != nullptr;
         ++entry) {
        const std::string pair(*entry);
        const std::size_t equals = pair.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        native_json::setField(object, pair.substr(0, equals),
                              native_json::makeString(pair.substr(equals + 1)));
    }
    return stable(native_json::dump(object, false));
}

extern "C" const char* cli_cwd() {
    std::array<char, 4096> buffer {};
    return ::getcwd(buffer.data(), buffer.size()) == nullptr
               ? stable("")
               : stable(std::string(buffer.data()));
}

extern "C" std::int64_t cli_chdir(const char* path) {
    return ::chdir(textOrEmpty(path).c_str()) == 0 ? 1 : 0;
}

extern "C" std::int64_t cli_stdinIsTty() {
    return ::isatty(STDIN_FILENO) ? 1 : 0;
}

extern "C" std::int64_t cli_stdoutIsTty() {
    return ::isatty(STDOUT_FILENO) ? 1 : 0;
}

extern "C" const char* cli_readStdin() {
    std::string content((std::istreambuf_iterator<char>(std::cin)),
                        std::istreambuf_iterator<char>());
    return stable(std::move(content));
}

extern "C" std::int64_t cli_writeStdout(const char* text) {
    std::cout << textOrEmpty(text);
    std::cout.flush();
    return 0;
}

extern "C" std::int64_t cli_writeStderr(const char* text) {
    std::cerr << textOrEmpty(text);
    std::cerr.flush();
    return 0;
}

extern "C" const char* cli_terminalSize() {
    struct winsize size {};
    int columns = 0;
    int lines = 0;
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0) {
        columns = size.ws_col;
        lines = size.ws_row;
    }
    native_json::Value object = native_json::makeObject();
    native_json::setField(object, "columns",
                          native_json::makeInteger(columns));
    native_json::setField(object, "lines", native_json::makeInteger(lines));
    return stable(native_json::dump(object, false));
}

extern "C" std::int64_t cli_exit(std::int64_t code) {
    std::exit(static_cast<int>(code));
    return 0;
}

extern "C" std::int64_t cli_pathExists(const char* path) {
    return ::access(textOrEmpty(path).c_str(), F_OK) == 0 ? 1 : 0;
}

extern "C" std::int64_t cli_isFile(const char* path) {
    struct stat info {};
    return ::stat(textOrEmpty(path).c_str(), &info) == 0 &&
                   S_ISREG(info.st_mode)
               ? 1
               : 0;
}

extern "C" std::int64_t cli_isDirectory(const char* path) {
    struct stat info {};
    return ::stat(textOrEmpty(path).c_str(), &info) == 0 &&
                   S_ISDIR(info.st_mode)
               ? 1
               : 0;
}

extern "C" const char* cli_which(const char* executable) {
    std::string output;
    std::array<char, 512> buffer {};
    const std::string query =
        "command -v " + textOrEmpty(executable) + " 2>/dev/null";
    FILE* pipe = ::popen(query.c_str(), "r");
    if (pipe == nullptr) {
        return stable("");
    }
    if (::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) !=
        nullptr) {
        output = buffer.data();
    }
    ::pclose(pipe);
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }
    return stable(std::move(output));
}

static std::string captureCommand(const std::string& command,
                                  std::int64_t& exitCode) {
    std::string output;
    std::array<char, 512> buffer {};
    FILE* pipe = ::popen(command.c_str(), "r");
    if (pipe == nullptr) {
        exitCode = -1;
        return output;
    }
    while (::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) !=
           nullptr) {
        output += buffer.data();
    }
    const int raw = ::pclose(pipe);
    if (raw == -1) {
        exitCode = -1;
    } else if (WIFEXITED(raw)) {
        exitCode = WEXITSTATUS(raw);
    } else {
        exitCode = -1;
    }
    return output;
}

// With capture=1 returns the command's stdout; otherwise runs it silently and
// returns the exit code as a string.
extern "C" const char* cli_run(const char* command, std::int64_t capture) {
    const std::string text = textOrEmpty(command);
    if (capture != 0) {
        std::int64_t code = 0;
        return stable(captureCommand(text, code));
    }
    std::int64_t code = 0;
    captureCommand(text + " >/dev/null 2>&1", code);
    return stable(std::to_string(code));
}

extern "C" std::int64_t cli_runCode(const char* command) {
    std::int64_t code = 0;
    captureCommand(textOrEmpty(command) + " >/dev/null 2>&1", code);
    return code;
}

extern "C" int lynxer_module_init_v1(RegisterFunction function,
                                     RegisterConstant, RegisterType) {
    return function("argv", "cli_argv", "cdecl:cstring()") &&
                   function("argCount", "cli_argCount", "cdecl:int64()") &&
                   function("getArg", "cli_getArg", "cdecl:cstring(int64)") &&
                   function("envGet", "cli_envGet", "cdecl:cstring(cstring)") &&
                   function("envHas", "cli_envHas", "cdecl:int64(cstring)") &&
                   function("envAll", "cli_envAll", "cdecl:cstring()") &&
                   function("cwd", "cli_cwd", "cdecl:cstring()") &&
                   function("chdir", "cli_chdir", "cdecl:int64(cstring)") &&
                   function("stdinIsTty", "cli_stdinIsTty", "cdecl:int64()") &&
                   function("stdoutIsTty", "cli_stdoutIsTty",
                            "cdecl:int64()") &&
                   function("readStdin", "cli_readStdin", "cdecl:cstring()") &&
                   function("writeStdout", "cli_writeStdout",
                            "cdecl:int64(cstring)") &&
                   function("writeStderr", "cli_writeStderr",
                            "cdecl:int64(cstring)") &&
                   function("terminalSize", "cli_terminalSize",
                            "cdecl:cstring()") &&
                   function("exit", "cli_exit", "cdecl:int64(int64)") &&
                   function("pathExists", "cli_pathExists",
                            "cdecl:int64(cstring)") &&
                   function("isFile", "cli_isFile", "cdecl:int64(cstring)") &&
                   function("isDirectory", "cli_isDirectory",
                            "cdecl:int64(cstring)") &&
                   function("which", "cli_which", "cdecl:cstring(cstring)") &&
                   function("run", "cli_run", "cdecl:cstring(cstring,int64)") &&
                   function("runCode", "cli_runCode", "cdecl:int64(cstring)")
               ? 0
               : 1;
}
