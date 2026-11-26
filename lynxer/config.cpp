#include "config.hpp"

#include "platform.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace lynxer {

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

std::string formatValue(std::string value, const std::string& search,
                        const std::string& replacement) {
    if (search.empty()) {
        return value;
    }
    std::size_t position = value.find(search);
    while (position != std::string::npos) {
        value.replace(position, search.size(), replacement);
        position = value.find(search, position + replacement.size());
    }
    return value;
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
    const std::string path = platform::executablePath();
    if (path.empty()) {
        return ".";
    }
    const std::filesystem::path parent =
        std::filesystem::path(path).parent_path();
    return parent.empty() ? std::string(".") : parent.string();
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

std::string stdlibDirectory() {
    if (const char* overridePath = std::getenv("LYNXER_STDLIB");
        overridePath != nullptr && overridePath[0] != '\0') {
        const std::filesystem::path overrideDirectory(overridePath);
        std::error_code overrideError;
        if (std::filesystem::is_directory(overrideDirectory, overrideError)) {
            return overrideDirectory.string();
        }
    }

    const std::filesystem::path beside(executableDirectoryImpl());
    std::error_code error;
    const std::filesystem::path primary = beside / "stdlib";
    if (std::filesystem::is_directory(primary, error)) {
        return primary.string();
    }
    // `--install` keeps the stdlib beside the real binary under
    // `<prefix>/lib/lynxer`. On POSIX the `$PREFIX/bin/lynxer` symlink resolves
    // to that binary, so the check above already succeeds; on Windows the
    // launcher is a copy, so the executable sits in `<prefix>/bin` and this
    // fallback finds the installed stdlib.
    const std::filesystem::path installed =
        beside / ".." / "lib" / "lynxer" / "stdlib";
    if (std::filesystem::is_directory(installed, error)) {
        return installed.lexically_normal().string();
    }
    return primary.string();
}

std::string executablePath() { return platform::executablePath(); }

const Config& Config::instance() {
    static const Config config;
    return config;
}

Config::Config() {
    // Compiled-in copy of lynxer.config; the external file overrides these.
    setDefault("name", "Lynxer");
    setDefault("version", "0.1.9b3");
    setDefault("version.line", "Lynxer {0}");
    setDefault("error.file_not_found", "lynxer: file not found: '{0}'");
    setDefault("error.could_not_read", "lynxer: could not read '{0}': {1}");
    setDefault("error.requires_one_file",
               "lynxer: {0} requires exactly one file argument");
    setDefault("error.compile_usage",
               "lynxer: --compile requires a .lynx file, optionally followed "
               "by --include <file> inputs and an output name");
    setDefault("error.compile_failed", "lynxer: compile failed: {0}");
    setDefault("error.payload_invalid",
               "lynxer: the embedded program payload is invalid");
    setDefault("error.bytecode_removed",
               "lynxer: bytecode files are no longer supported; compile the "
               ".lynx source with --compile instead");
    setDefault("error.interpreter_failure",
               "lynxer: interpreter failure in '{0}': {1}");
    setDefault("error.validator_missing",
               "lynxer: comprehensive validator is not available");

    // CLI messages below were previously hard-coded in shell.cpp. The
    // compiled-in defaults keep a config-less binary byte-identical.
    setDefault("error.emit_library_failed", "lynxer: emit-library failed: {0}");
    setDefault("error.removed_flag",
               "lynxer: '{0}' was removed with the bytecode backend");
    setDefault("status.use_instead", "; use {0} instead");
    setDefault("error.requires_name", "lynxer: {0} requires a name");
    setDefault("error.output_given_twice",
               "lynxer: the output name was given twice");
    setDefault("error.requires_file", "lynxer: {0} requires a file");
    setDefault("error.unexpected_extra_argument",
               "lynxer: unexpected extra argument '{0}'");
    setDefault("error.first_input_not_lynx",
               "lynxer: the first input must be a .lynx program file");
    setDefault("error.could_not_write", "lynxer: could not write '{0}'");
    setDefault("error.invalid_project_name",
               "lynxer: invalid project name '{0}'");
    setDefault("error.could_not_create_dir",
               "lynxer: cannot create {0}/: {1}");
    setDefault("error.project_no_entry",
               "lynxer: lynxer.config has no 'entry' key (expected e.g. "
               "'entry = src/main.lynx')");
    setDefault("error.project_entry_not_found",
               "lynxer: project entry not found: '{0}'");
    setDefault("error.could_not_run_output", "lynxer: could not run '{0}'");
    setDefault("error.nothing_to_uninstall",
               "lynxer: nothing to uninstall under {0}");
    setDefault("error.retry_permission",
               "lynxer: re-run with permission to write {0}");
    setDefault("error.install_failed", "lynxer: install failed: {0}");
    setDefault("error.could_not_remove", "lynxer: could not remove {0}: {1}");
    setDefault("error.requires_source_file",
               "lynxer: {0} requires a source file");
    setDefault("error.runtime_requires_path",
               "lynxer: --runtime requires a library path");
    setDefault("error.cc_requires_command",
               "lynxer: --cc requires a compiler command");

    setDefault("status.lint_ok", "Lint OK: {0}");
    setDefault("status.compile_ok", "Compiled: {0}");
    setDefault("warning.forever_no_break",
               "forever() has no break; it will run until the process is "
               "stopped. Add break; or call suppressForeverWarning() in "
               "fn setup(){}.");

    const std::string directory = executableDirectoryImpl();
    if (loadConfigFile(directory + "/lynxer.config", values_)) {
        return;
    }
    // A Windows install copies the launcher into `<prefix>/bin`, so the config
    // file lives under `<prefix>/lib/lynxer`.
    if (loadConfigFile(directory + "/../lib/lynxer/lynxer.config", values_)) {
        return;
    }
    loadConfigFile("lynxer.config", values_);
}

std::string Config::render(
    const std::string& key, const std::string& fallback,
    const std::vector<std::pair<std::string, std::string>>& values) const {
    std::string result = get(key, fallback);
    for (const auto& item : values) {
        result = formatValue(result, item.first, item.second);
    }
    return result;
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
    return formatValue(get(key, fallback), search, replacement);
}

} // namespace lynxer
