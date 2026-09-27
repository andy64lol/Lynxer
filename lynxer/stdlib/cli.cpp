// Lynxer `cli` stdlib backend: argument, environment, terminal and process
// helpers for command-line programs, plus the Click/Typer-style command
// builders.
//
// The builders keep their definitions in an integer-handle registry (the same
// model as `multiprocessing`); the builders themselves are reached through a
// single JSON descriptor argument so no native signature shape beyond the
// existing ones is needed.

#include "native_json.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
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

// --- command builders (Click/Typer style) ------------------------------------
//
// Every builder op takes a single JSON *array* descriptor; the `.lynx` wrapper
// presents the original positional API and builds the array. Command
// definitions live in an integer-handle registry.

namespace {

struct BuilderOption {
    std::vector<std::string> names;
    std::string help;
    std::string defaultValue;
    bool flag = false;
    bool required = false;
    bool multiple = false;
    std::string type = "text";
    std::string envvar;

    // The parameter name: the first declaration without its leading dashes, so
    // "--shout,-s" renders as `{shout}` in a shell template.
    std::string parameter() const {
        std::string name = names.empty() ? std::string() : names.front();
        while (!name.empty() && name.front() == '-') {
            name.erase(name.begin());
        }
        return name;
    }
};

struct BuilderArgument {
    std::string name;
    bool required = true;
    std::int64_t nargs = 1;
    std::string help;
};

struct BuilderNode {
    std::string name;
    std::string help;
    bool group = false;
    bool noArgsHelp = false;
    std::string shell;
    std::vector<BuilderArgument> arguments;
    std::vector<BuilderOption> options;
    std::vector<std::int64_t> children;
};

std::map<std::int64_t, BuilderNode>& builderNodes() {
    static std::map<std::int64_t, BuilderNode> nodes;
    return nodes;
}

std::mutex& builderMutex() {
    static std::mutex instance;
    return instance;
}

std::int64_t nextBuilderHandle() {
    static std::int64_t next = 0;
    return ++next;
}

std::string& lastParams() {
    static std::string value;
    return value;
}

bool parseDescriptor(const char* json, native_json::Value& out,
                     std::string& error) {
    if (!native_json::parse(textOrEmpty(json), out, error)) {
        return false;
    }
    if (out.type != native_json::Type::Array) {
        error = "builder descriptor must be a JSON array";
        return false;
    }
    return true;
}

std::string itemString(const native_json::Value& array, std::size_t index) {
    if (index >= array.items.size()) {
        return "";
    }
    const native_json::Value& item = array.items[index];
    return item.type == native_json::Type::String ? item.text : "";
}

std::int64_t itemInt(const native_json::Value& array, std::size_t index) {
    return index < array.items.size() ? native_json::asInteger(array.items[index])
                                      : 0;
}

bool itemBool(const native_json::Value& array, std::size_t index) {
    return index < array.items.size() &&
           native_json::asInteger(array.items[index]) != 0;
}

std::string scalarText(const native_json::Value& value) {
    switch (value.type) {
        case native_json::Type::String: return value.text;
        case native_json::Type::Integer: return std::to_string(value.integer);
        case native_json::Type::Number:
            return native_json::numberToString(value.number);
        case native_json::Type::Bool: return value.boolean ? "true" : "false";
        default: return "";
    }
}

// Converts one raw value to its declared type; ok=false and `error` on failure.
native_json::Value typedValue(const std::string& raw, const std::string& type,
                              bool& ok, std::string& error) {
    ok = true;
    if (type == "int") {
        std::int64_t number = 0;
        const char* begin = raw.data();
        const char* end = begin + raw.size();
        const auto result = std::from_chars(begin, end, number);
        if (result.ec != std::errc() || result.ptr != end) {
            ok = false;
            error = "'" + raw + "' is not a valid int";
            return native_json::makeNull();
        }
        return native_json::makeInteger(number);
    }
    if (type == "float") {
        try {
            std::size_t used = 0;
            const double number = std::stod(raw, &used);
            if (used != raw.size()) {
                throw std::invalid_argument("trailing");
            }
            return native_json::makeNumber(number);
        } catch (const std::exception&) {
            ok = false;
            error = "'" + raw + "' is not a valid float";
            return native_json::makeNull();
        }
    }
    if (type == "bool") {
        if (raw == "true" || raw == "1" || raw == "yes" || raw == "on") {
            return native_json::makeBool(true);
        }
        if (raw.empty() || raw == "false" || raw == "0" || raw == "no" ||
            raw == "off") {
            return native_json::makeBool(false);
        }
        ok = false;
        error = "'" + raw + "' is not a valid bool";
        return native_json::makeNull();
    }
    return native_json::makeString(raw);   // text and path
}

struct ParseResult {
    bool ok = true;
    std::string error;
    native_json::Value params = native_json::makeObject();
};

ParseResult parseForNode(const BuilderNode& node,
                         const std::vector<std::string>& tokens) {
    ParseResult result;
    std::map<std::string, native_json::Value> values;
    std::set<std::string> presentOptions;
    std::vector<std::string> positional;

    for (std::size_t index = 0; index < tokens.size(); ++index) {
        const std::string& token = tokens[index];
        const bool looksLikeOption = token.size() >= 2 && token[0] == '-';
        if (!looksLikeOption) {
            positional.push_back(token);
            continue;
        }
        std::string name = token;
        std::string inlineValue;
        bool hasInline = false;
        if (const std::size_t equals = token.find('=');
            equals != std::string::npos) {
            name = token.substr(0, equals);
            inlineValue = token.substr(equals + 1);
            hasInline = true;
        }
        const BuilderOption* option = nullptr;
        for (const auto& candidate : node.options) {
            for (const auto& declared : candidate.names) {
                if (declared == name) {
                    option = &candidate;
                }
            }
        }
        // `--no-<flag>` negates a declared flag, e.g. --no-shout for --shout.
        bool negated = false;
        if (option == nullptr && name.rfind("--no-", 0) == 0) {
            const std::string base = name.substr(5);
            for (const auto& candidate : node.options) {
                if (candidate.flag && candidate.parameter() == base) {
                    option = &candidate;
                    negated = true;
                }
            }
        }
        if (option == nullptr) {
            result.ok = false;
            result.error = "unknown option '" + name + "'";
            return result;
        }
        std::string raw;
        if (negated) {
            raw = "false";
        } else if (option->flag) {
            raw = hasInline ? inlineValue : "true";
        } else if (hasInline) {
            raw = inlineValue;
        } else {
            if (index + 1 >= tokens.size()) {
                result.ok = false;
                result.error = "option '" + name + "' requires a value";
                return result;
            }
            raw = tokens[++index];
        }
        bool ok = true;
        std::string error;
        const native_json::Value value =
            typedValue(raw, option->flag ? "bool" : option->type, ok, error);
        if (!ok) {
            result.ok = false;
            result.error = error;
            return result;
        }
        const std::string key = option->parameter();
        if (option->multiple) {
            // Repeated options collect into a JSON array.
            const auto existing = values.find(key);
            if (existing == values.end() ||
                existing->second.type != native_json::Type::Array) {
                native_json::Value array = native_json::makeArray();
                array.items.push_back(value);
                values[key] = array;
            } else {
                existing->second.items.push_back(value);
            }
        } else {
            values[key] = value;
        }
        presentOptions.insert(key);
    }

    std::size_t consumed = 0;
    std::set<std::string> presentArguments;
    for (const auto& argument : node.arguments) {
        if (argument.nargs == 1) {
            if (consumed < positional.size()) {
                values[argument.name] =
                    native_json::makeString(positional[consumed++]);
                presentArguments.insert(argument.name);
            }
        } else if (argument.nargs < 0) {
            native_json::Value array = native_json::makeArray();
            while (consumed < positional.size()) {
                array.items.push_back(
                    native_json::makeString(positional[consumed++]));
            }
            values[argument.name] = array;
            presentArguments.insert(argument.name);
        } else {
            native_json::Value array = native_json::makeArray();
            for (std::int64_t count = 0;
                 count < argument.nargs && consumed < positional.size();
                 ++count) {
                array.items.push_back(
                    native_json::makeString(positional[consumed++]));
            }
            values[argument.name] = array;
            presentArguments.insert(argument.name);
        }
    }
    if (consumed < positional.size()) {
        result.ok = false;
        result.error = "unexpected argument '" + positional[consumed] + "'";
        return result;
    }
    for (const auto& argument : node.arguments) {
        if (argument.required && argument.nargs == 1 &&
            presentArguments.find(argument.name) == presentArguments.end()) {
            result.ok = false;
            result.error = "missing required argument '" + argument.name + "'";
            return result;
        }
    }
    for (const auto& option : node.options) {
        if (presentOptions.find(option.parameter()) != presentOptions.end()) {
            continue;
        }
        if (option.required) {
            result.ok = false;
            result.error =
                "missing required option '" + option.names.front() + "'";
            return result;
        }
        // Priority: explicit argument, then the environment variable, then the
        // declared default.
        std::string fallback = option.defaultValue;
        if (!option.envvar.empty()) {
            if (const char* fromEnvironment = std::getenv(option.envvar.c_str());
                fromEnvironment != nullptr) {
                fallback = fromEnvironment;
            }
        }
        if (!fallback.empty()) {
            bool ok = true;
            std::string error;
            const native_json::Value value = typedValue(
                fallback, option.flag ? "bool" : option.type, ok, error);
            if (ok) {
                values[option.parameter()] = value;
            }
        }
    }

    for (const auto& argument : node.arguments) {
        if (values.count(argument.name) != 0) {
            native_json::setField(result.params, argument.name,
                                  values[argument.name]);
        }
    }
    for (const auto& option : node.options) {
        if (values.count(option.parameter()) != 0) {
            native_json::setField(result.params, option.parameter(),
                                  values[option.parameter()]);
        }
    }
    return result;
}

std::string substituteTemplate(const std::string& text,
                               const native_json::Value& params) {
    std::string out;
    for (std::size_t index = 0; index < text.size();) {
        if (text[index] != '{') {
            out += text[index++];
            continue;
        }
        const std::size_t close = text.find('}', index);
        if (close == std::string::npos) {
            out += text[index++];
            continue;
        }
        const std::string key = text.substr(index + 1, close - index - 1);
        const native_json::Value* field = native_json::findField(params, key);
        if (field == nullptr) {
            // Unknown placeholder: substitute nothing.
        } else if (field->type == native_json::Type::Array) {
            // A repeated option renders as its values joined by spaces.
            std::string joined;
            for (const auto& item : field->items) {
                if (!joined.empty()) {
                    joined += " ";
                }
                joined += scalarText(item);
            }
            out += joined;
        } else {
            out += scalarText(*field);
        }
        index = close + 1;
    }
    return out;
}

bool wantsHelp(const std::vector<std::string>& tokens) {
    for (const auto& token : tokens) {
        if (token == "-h" || token == "--help") {
            return true;
        }
    }
    return false;
}

// A usage/help block for `node`, listing its arguments, options and (for a
// group) its subcommands. Stored help text is only surfaced here.
std::string renderHelp(const BuilderNode& node) {
    std::string usage = "Usage: " + node.name;
    if (node.group) {
        usage += " COMMAND";
    } else {
        if (!node.options.empty()) {
            usage += " [OPTIONS]";
        }
        for (const auto& argument : node.arguments) {
            usage += argument.required ? " <" + argument.name + ">"
                                       : " [" + argument.name + "]";
        }
    }
    std::string text = usage + "\n";
    if (!node.help.empty()) {
        text += "\n" + node.help + "\n";
    }
    if (!node.arguments.empty()) {
        text += "\nArguments:\n";
        for (const auto& argument : node.arguments) {
            text += "  " + argument.name;
            if (!argument.help.empty()) {
                text += "  " + argument.help;
            }
            text += "\n";
        }
    }
    text += "\nOptions:\n";
    for (const auto& option : node.options) {
        std::string names;
        for (const auto& declared : option.names) {
            if (!names.empty()) {
                names += ", ";
            }
            names += declared;
        }
        text += "  " + names;
        if (!option.help.empty()) {
            text += "  " + option.help;
        }
        text += "\n";
    }
    text += "  --help, -h  Show this message and exit\n";
    if (node.group && !node.children.empty()) {
        text += "\nCommands:\n";
        for (const std::int64_t child : node.children) {
            const auto found = builderNodes().find(child);
            if (found != builderNodes().end()) {
                text += "  " + found->second.name;
                if (!found->second.help.empty()) {
                    text += "  " + found->second.help;
                }
                text += "\n";
            }
        }
    }
    return text;
}

std::string helpResult(const BuilderNode& node) {
    native_json::Value result = native_json::makeObject();
    native_json::setField(result, "params", native_json::makeObject());
    native_json::setField(result, "help", native_json::makeString(renderHelp(node)));
    return native_json::dump(result, false);
}

// Resolves a handle and runs it. On failure `error` is set and "" is returned.
std::string invokeNode(std::int64_t handle,
                       const std::vector<std::string>& tokens,
                       std::string& error) {
    const auto found = builderNodes().find(handle);
    if (found == builderNodes().end()) {
        error = "unknown command handle " + std::to_string(handle);
        return "";
    }
    const BuilderNode& node = found->second;
    if (node.group) {
        // A group with one command may omit the command name (Typer's model).
        if (node.children.size() == 1) {
            const auto only = builderNodes().find(node.children.front());
            if (only != builderNodes().end() &&
                (tokens.empty() || tokens.front() != only->second.name)) {
                if (tokens.empty() && node.noArgsHelp) {
                    return helpResult(node);
                }
                return invokeNode(only->first, tokens, error);
            }
        }
        if (wantsHelp(tokens)) {
            return helpResult(node);
        }
        if (tokens.empty()) {
            if (node.noArgsHelp) {
                return helpResult(node);
            }
            error = "command group '" + node.name + "' requires a subcommand";
            return "";
        }
        for (const std::int64_t child : node.children) {
            const auto candidate = builderNodes().find(child);
            if (candidate != builderNodes().end() &&
                candidate->second.name == tokens.front()) {
                return invokeNode(child,
                                  std::vector<std::string>(tokens.begin() + 1,
                                                           tokens.end()),
                                  error);
            }
        }
        error = "unknown command '" + tokens.front() + "'";
        return "";
    }

    // --help/-h is answered before required arguments are enforced.
    if (wantsHelp(tokens)) {
        return helpResult(node);
    }
    const ParseResult parsed = parseForNode(node, tokens);
    if (!parsed.ok) {
        error = parsed.error;
        return "";
    }
    native_json::Value result = native_json::makeObject();
    native_json::setField(result, "params", parsed.params);
    if (!node.shell.empty()) {
        const std::string command = substituteTemplate(node.shell, parsed.params);
        std::int64_t code = 0;
        const std::string output = captureCommand(command, code);
        native_json::setField(result, "output", native_json::makeString(output));
        native_json::setField(result, "exitCode", native_json::makeInteger(code));
    }
    lastParams() = native_json::dump(parsed.params, false);
    return native_json::dump(result, false);
}

std::vector<std::string> jsonStringList(const native_json::Value& array) {
    std::vector<std::string> tokens;
    for (const auto& item : array.items) {
        tokens.push_back(scalarText(item));
    }
    return tokens;
}

std::string invokeFromDescriptor(const char* json) {
    native_json::Value descriptor;
    std::string error;
    if (!parseDescriptor(json, descriptor, error)) {
        return native_json::dump(
            [&] {
                native_json::Value object = native_json::makeObject();
                native_json::setField(object, "error",
                                      native_json::makeString(error));
                return object;
            }(),
            false);
    }
    const std::int64_t handle = itemInt(descriptor, 0);
    std::vector<std::string> tokens;
    if (descriptor.items.size() > 1) {
        // The arguments arrive as a JSON array, either inline or (from the
        // wrapper) as a JSON string that itself holds an array.
        const native_json::Value& arguments = descriptor.items[1];
        if (arguments.type == native_json::Type::Array) {
            tokens = jsonStringList(arguments);
        } else if (arguments.type == native_json::Type::String) {
            native_json::Value parsed;
            std::string parseError;
            if (!native_json::parse(arguments.text, parsed, parseError) ||
                parsed.type != native_json::Type::Array) {
                native_json::Value object = native_json::makeObject();
                native_json::setField(
                    object, "error",
                    native_json::makeString(
                        "invoke arguments must be a JSON array"));
                return native_json::dump(object, false);
            }
            tokens = jsonStringList(parsed);
        }
    }
    const std::string result = invokeNode(handle, tokens, error);
    if (!error.empty()) {
        native_json::Value object = native_json::makeObject();
        native_json::setField(object, "error", native_json::makeString(error));
        return native_json::dump(object, false);
    }
    return result;
}

} // namespace

extern "C" std::int64_t cli_clickInit() {
    std::lock_guard<std::mutex> lock(builderMutex());
    builderNodes().clear();
    lastParams().clear();
    return 0;
}

extern "C" std::int64_t cli_clickCommandCreate(const char* json) {
    native_json::Value descriptor;
    std::string error;
    if (!parseDescriptor(json, descriptor, error)) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(builderMutex());
    BuilderNode node;
    node.name = itemString(descriptor, 0);
    node.help = itemString(descriptor, 1);
    const std::int64_t handle = nextBuilderHandle();
    builderNodes()[handle] = std::move(node);
    return handle;
}

extern "C" std::int64_t cli_clickGroupCreate(const char* json) {
    native_json::Value descriptor;
    std::string error;
    if (!parseDescriptor(json, descriptor, error)) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(builderMutex());
    BuilderNode node;
    node.name = itemString(descriptor, 0);
    node.help = itemString(descriptor, 1);
    node.group = true;
    node.noArgsHelp = itemBool(descriptor, 2);
    const std::int64_t handle = nextBuilderHandle();
    builderNodes()[handle] = std::move(node);
    return handle;
}

extern "C" std::int64_t cli_clickGroupAddCommand(const char* json) {
    native_json::Value descriptor;
    std::string error;
    if (!parseDescriptor(json, descriptor, error)) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(builderMutex());
    const std::int64_t group = itemInt(descriptor, 0);
    const std::int64_t command = itemInt(descriptor, 1);
    const auto found = builderNodes().find(group);
    if (found == builderNodes().end() ||
        builderNodes().find(command) == builderNodes().end()) {
        return 0;
    }
    found->second.children.push_back(command);
    return 1;
}

extern "C" std::int64_t cli_clickAddArgument(const char* json) {
    native_json::Value descriptor;
    std::string error;
    if (!parseDescriptor(json, descriptor, error)) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(builderMutex());
    const auto found = builderNodes().find(itemInt(descriptor, 0));
    if (found == builderNodes().end()) {
        return 0;
    }
    BuilderArgument argument;
    argument.name = itemString(descriptor, 1);
    argument.required = itemBool(descriptor, 2);
    argument.nargs = itemInt(descriptor, 3);
    argument.help = itemString(descriptor, 4);
    if (argument.nargs == 0) {
        argument.nargs = 1;
    }
    found->second.arguments.push_back(std::move(argument));
    return 1;
}

extern "C" std::int64_t cli_clickAddOption(const char* json) {
    native_json::Value descriptor;
    std::string error;
    if (!parseDescriptor(json, descriptor, error)) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(builderMutex());
    const auto found = builderNodes().find(itemInt(descriptor, 0));
    if (found == builderNodes().end()) {
        return 0;
    }
    BuilderOption option;
    const std::string declarations = itemString(descriptor, 1);
    std::string current;
    for (const char character : declarations) {
        if (character == ',') {
            if (!current.empty()) {
                option.names.push_back(current);
            }
            current.clear();
        } else if (character != ' ') {
            current += character;
        }
    }
    if (!current.empty()) {
        option.names.push_back(current);
    }
    option.help = itemString(descriptor, 2);
    option.defaultValue = itemString(descriptor, 3);
    option.flag = itemBool(descriptor, 4);
    option.required = itemBool(descriptor, 5);
    const std::string type = itemString(descriptor, 6);
    option.type = type.empty() ? "text" : type;
    option.envvar = itemString(descriptor, 7);
    option.multiple = itemBool(descriptor, 8);
    found->second.options.push_back(std::move(option));
    return 1;
}

extern "C" std::int64_t cli_clickCommandSetShell(const char* json) {
    native_json::Value descriptor;
    std::string error;
    if (!parseDescriptor(json, descriptor, error)) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(builderMutex());
    const auto found = builderNodes().find(itemInt(descriptor, 0));
    if (found == builderNodes().end()) {
        return 0;
    }
    found->second.shell = itemString(descriptor, 1);
    return 1;
}

extern "C" const char* cli_clickInvoke(const char* json) {
    std::lock_guard<std::mutex> lock(builderMutex());
    return stable(invokeFromDescriptor(json));
}

extern "C" const char* cli_clickGroupInvoke(const char* json) {
    std::lock_guard<std::mutex> lock(builderMutex());
    return stable(invokeFromDescriptor(json));
}

extern "C" const char* cli_clickRun(const char* json) {
    native_json::Value descriptor;
    std::string error;
    if (!parseDescriptor(json, descriptor, error)) {
        native_json::Value object = native_json::makeObject();
        native_json::setField(object, "error", native_json::makeString(error));
        return stable(native_json::dump(object, false));
    }
    std::vector<std::string> tokens;
    std::vector<std::string> arguments = processArguments();
    if (arguments.size() > 1) {
        tokens.assign(arguments.begin() + 1, arguments.end());
    }
    std::lock_guard<std::mutex> lock(builderMutex());
    const std::string result = invokeNode(itemInt(descriptor, 0), tokens, error);
    if (!error.empty()) {
        native_json::Value object = native_json::makeObject();
        native_json::setField(object, "error", native_json::makeString(error));
        return stable(native_json::dump(object, false));
    }
    return stable(result);
}

extern "C" const char* cli_clickLastParams() {
    std::lock_guard<std::mutex> lock(builderMutex());
    return stable(lastParams().empty() ? std::string("{}") : lastParams());
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
                   function("runCode", "cli_runCode", "cdecl:int64(cstring)") &&
                   function("clickInit", "cli_clickInit", "cdecl:int64()") &&
                   function("clickCommandCreate", "cli_clickCommandCreate",
                            "cdecl:int64(cstring)") &&
                   function("clickGroupCreate", "cli_clickGroupCreate",
                            "cdecl:int64(cstring)") &&
                   function("clickGroupAddCommand", "cli_clickGroupAddCommand",
                            "cdecl:int64(cstring)") &&
                   function("clickAddArgument", "cli_clickAddArgument",
                            "cdecl:int64(cstring)") &&
                   function("clickAddOption", "cli_clickAddOption",
                            "cdecl:int64(cstring)") &&
                   function("clickCommandSetShell", "cli_clickCommandSetShell",
                            "cdecl:int64(cstring)") &&
                   function("clickInvoke", "cli_clickInvoke",
                            "cdecl:cstring(cstring)") &&
                   function("clickGroupInvoke", "cli_clickGroupInvoke",
                            "cdecl:cstring(cstring)") &&
                   function("clickRun", "cli_clickRun",
                            "cdecl:cstring(cstring)") &&
                   function("clickLastParams", "cli_clickLastParams",
                            "cdecl:cstring()") ? 0
               : 1;
}
