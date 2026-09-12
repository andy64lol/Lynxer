#include "config.hpp"

#include <fstream>
#include <sstream>

#ifdef __linux__
#include <limits.h>
#include <unistd.h>
#endif

namespace clynxer {

namespace {

std::string trim(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::string unescape(const std::string& text) {
    std::string output;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '\\' && index + 1 < text.size()) {
            const char next = text[++index];
            switch (next) {
            case 'n':
                output += '\n';
                break;
            case 't':
                output += '\t';
                break;
            case '\\':
                output += '\\';
                break;
            default:
                output += '\\';
                output += next;
                break;
            }
        } else {
            output += text[index];
        }
    }
    return output;
}

std::string executableDirectoryImpl() {
#ifdef __linux__
    char path[PATH_MAX];
    const ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (length > 0) {
        path[length] = '\0';
        const std::string full(path);
        const std::size_t slash = full.find_last_of('/');
        if (slash != std::string::npos) {
            return full.substr(0, slash);
        }
    }
#endif
    return ".";
}

bool loadConfigFile(const std::string& path,
                    std::unordered_map<std::string, std::string>& values) {
    std::ifstream input(path);
    if (!input) {
        return false;
    }
    std::string line;
    while (std::getline(input, line)) {
        const std::string stripped = trim(line);
        if (stripped.empty() || stripped[0] == '#') {
            continue;
        }
        const std::size_t equals = stripped.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        values[trim(stripped.substr(0, equals))] =
            unescape(trim(stripped.substr(equals + 1)));
    }
    return true;
}

} // namespace

std::string executableDirectory() { return executableDirectoryImpl(); }

const Config& Config::instance() {
    static const Config config;
    return config;
}

Config::Config() {
    // Compiled-in copy of clynxer.config; the external file overrides these.
    setDefault("name", "CLynxer");
    setDefault("version", "0.1.8");
    setDefault("version.line", "CLynxer {0}");
    setDefault("error.file_not_found", "clynxer: file not found: '{0}'");
    setDefault("error.could_not_read", "clynxer: could not read '{0}': {1}");
    setDefault("error.requires_one_file",
               "clynxer: {0} requires exactly one file argument");
    setDefault("error.compile_requires_file",
               "clynxer: --compile requires a file argument");
    setDefault("error.bundle_usage",
               "clynxer: --bundle requires a .lynx file and optional output name");
    setDefault("error.bundle_failed", "clynxer: bundle failed: {0}");
    setDefault("error.view_bytecode_usage",
               "clynxer: --view-bytecode requires a .lynxc file argument");
    setDefault("error.benchmark_usage",
               "clynxer: --benchmark-compile requires at least one .lynx file");
    setDefault("error.could_not_run_bytecode",
               "clynxer: could not run bytecode '{0}': {1}");
    setDefault("error.interpreter_failure",
               "clynxer: interpreter failure in '{0}': {1}");
    setDefault("error.unsupported",
               "clynxer: '{0}' is not available in CLynxer yet");
    setDefault("error.bytecode_unsupported",
               "clynxer: running compiled .lynxc bytecode is not supported in CLynxer yet");
    setDefault("error.validator_missing",
               "clynxer: comprehensive validator is not available");
    setDefault("status.lint_ok", "Lint OK: {0}");
    setDefault("warning.forever_no_break",
               "forever() has no break; it will run until the process is "
               "stopped. Add break; or call suppressForeverWarning() in "
               "global setup(){}.");

    const std::string directory = executableDirectoryImpl();
    if (loadConfigFile(directory + "/clynxer.config", values_)) {
        return;
    }
    loadConfigFile("clynxer.config", values_);
}

void Config::setDefault(const std::string& key, const std::string& value) {
    values_.emplace(key, value);
}

std::string Config::get(const std::string& key,
                        const std::string& fallback) const {
    const auto found = values_.find(key);
    if (found == values_.end()) {
        return fallback;
    }
    return found->second;
}

std::string Config::format(const std::string& key,
                           const std::string& fallback,
                           const std::string& search,
                           const std::string& replacement) const {
    std::string value = get(key, fallback);
    std::size_t position = value.find(search);
    while (position != std::string::npos) {
        value.replace(position, search.size(), replacement);
        position = value.find(search, position + replacement.size());
    }
    return value;
}

} // namespace clynxer
