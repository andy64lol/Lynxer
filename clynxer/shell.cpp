#include "shell.hpp"

#include "bundle.hpp"
#include "config.hpp"
#include "error.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "runtime.hpp"

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <dirent.h>

namespace clynxer {

namespace {

volatile std::sig_atomic_t interrupted = 0;

void handleInterrupt(int) { interrupted = 1; }

std::string readFile(const std::string& path, const std::string& display,
                     bool& ok) {
    std::ifstream input(path);
    if (!input) {
        std::cerr << Config::instance().format("error.file_not_found",
                                               "clynxer: file not found: '{0}'",
                                               "{0}", display)
                  << '\n';
        ok = false;
        return "";
    }
    std::ostringstream content;
    content << input.rdbuf();
    ok = true;
    return content.str();
}

void printUsage() {
    std::cout << "\n";
    std::cout << "Usage:\n";
    std::cout << "  clynxer <file.lynx>                   Run a Lynxer source file\n";
    std::cout << "  clynxer --compile <file.lynx> [name]  Compile to a standalone executable\n";
    std::cout << "  clynxer --bundle <file.lynx> [name]   Alias of --compile\n";
    std::cout << "  clynxer --ast <file.lynx>             Parse and print the abstract syntax tree\n";
    std::cout << "  clynxer --format <file.lynx>          Format a Lynxer source file in place\n";
    std::cout << "  clynxer --format-oneline <file.lynx>  Compact a Lynxer source file to one line\n";
    std::cout << "  clynxer --lint <file.lynx>            Check Lynxer syntax without running it\n";
    std::cout << "  clynxer --validate-executeable        Run the comprehensive interpreter validator\n";
    std::cout << "  clynxer --version                     Print version\n";
    std::cout << "  clynxer --list-stdlibs                List available Lynxer stdlib modules\n";
    std::cout << "  clynxer --install                     Install the compiled executable as /usr/bin/lynxer, may require sudo\n";
    std::cout << "  clynxer --uninstall                   Remove /usr/bin/lynxer, also may require sudo\n";
    std::cout << "\n";
    std::cout << "  BTW, please run the install and uninstall with the executeable, not shell.cpp nor anything else.\n";
    std::cout << "  If you are running from source, use the compiled executable instead located in GitHub Releases.\n";
    std::cout << "\n";
}

void printVersion() {
    const Config& config = Config::instance();
    std::cout << config.format("version.line", "CLynxer {0}", "{0}",
                               config.get("version", "0.1.8"))
              << '\n';
}

void printEasterEgg() {
    std::cout << "Easter Egg found!\n";
    std::cout
        << "Wanna do sudo rm -rf / --no-preserve-root? Just kidding, don't do that.\n";
    std::cout << "But seriously, don't do that. It's a bad idea.\n";
    std::cout << "That will erase the entire linux OS and all your files. You will lose everything.\n";
}

std::string extractDocstring(const std::string& path) {
    std::vector<std::string> lines;
    bool inside = false;
    std::ifstream input(path);
    if (!input) {
        std::cout << "Error: could not read '" << path << "'";
        return "";
    }
    std::string line;
    while (std::getline(input, line)) {
        const std::size_t first = line.find_first_not_of(" \t\r");
        const std::string stripped =
            first == std::string::npos ? "" : line.substr(first);
        if (!inside) {
            if (stripped == "////") {
                inside = true;
            }
            continue;
        }
        if (stripped == "////") {
            break;
        }
        while (!line.empty() &&
               (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        lines.push_back(line);
    }
    std::string text;
    for (const auto& entry : lines) {
        if (!text.empty()) {
            text += "\n";
        }
        text += entry;
    }
    while (!text.empty() && text.back() == '\n') {
        text.pop_back();
    }
    return text;
}

int listStdlibs() {
    const std::string stdlibPath = executableDirectory() + "/stdlib";
    std::vector<std::string> files;
    if (DIR* directory = opendir(stdlibPath.c_str())) {
        while (const dirent* entry = readdir(directory)) {
            const std::string name = entry->d_name;
            if (name.size() > 5 &&
                name.compare(name.size() - 5, 5, ".lynx") == 0) {
                files.push_back(name);
            }
        }
        closedir(directory);
    } else {
        std::cout << "No stdlib directory found.\n";
        return 1;
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        std::cout << "No Lynxer stdlib modules found.\n";
        return 0;
    }
    std::cout << "Available Lynxer stdlib modules:\n\n";
    for (const auto& file : files) {
        const std::string path = stdlibPath + "/" + file;
        const std::string name = file.substr(0, file.size() - 5);
        const std::string docstring = extractDocstring(path);
        if (!docstring.empty()) {
            std::cout << "  " << name << "\n";
            std::istringstream doc(docstring);
            std::string docLine;
            while (std::getline(doc, docLine)) {
                if (!docLine.empty() &&
                    !(docLine.size() == 1 && docLine[0] == '\r')) {
                    std::cout << "    " << docLine << "\n";
                } else {
                    std::cout << "\n";
                }
            }
            std::cout << "\n";
        } else {
            std::cout << "  " << name << "\n\n";
        }
    }
    return 0;
}

int lintFile(const std::string& display, const std::string& source) {
    Lexer lexer(source, display);
    Parser parser(lexer.scan());
    try {
        parser.parseProgram();
    } catch (const SourceError& error) {
        std::cerr << "clynxer: " << display << ':' << error.line << ':'
                  << error.column << ": " << error.what() << '\n';
        return 1;
    }
    std::cout << Config::instance().format("status.lint_ok", "Lint OK: {0}",
                                           "{0}", display)
              << '\n';
    return 0;
}

int runProgram(const std::string& display, const std::string& source) {
    try {
        Lexer lexer(source, display);
        Parser parser(lexer.scan());
        auto functions = parser.parseProgram();
        Environment environment;
        const std::size_t slash = display.find_last_of('/');
        if (slash != std::string::npos) {
            environment.setSourceDirectory(display.substr(0, slash));
        }
        executeProgram(functions, environment);
        return 0;
    } catch (const SourceError& error) {
        std::cerr << "clynxer: " << display << ':' << error.line << ':'
                  << error.column << ": " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << Config::instance().format(
                         "error.interpreter_failure",
                         "clynxer: interpreter failure in '{0}': {1}", "{0}",
                         display)
                  << ": " << error.what() << '\n';
        return 1;
    }
}

int unsupportedFeature(const std::string& flag) {
    std::cerr << Config::instance().format("error.unsupported",
                                           "clynxer: '{0}' is not available in CLynxer yet",
                                           "{0}", flag)
              << '\n';
    return 1;
}

// Recursively collects `source` and every module it imports into `archive`.
// Source modules are embedded as text; native modules are embedded as library
// bytes so the compiled executable does not need the build tree at run time.
bool collectArchive(const std::string& displayPath, const std::string& source,
                    ProgramArchive& archive, std::string& error) {
    std::map<std::string, bool> collectedSources;
    std::map<std::string, bool> collectedLibraries;

    struct Pending {
        std::string path;
        std::string source;
    };
    std::vector<Pending> queue;
    queue.push_back(Pending{displayPath, source});
    collectedSources[displayPath] = true;

    while (!queue.empty()) {
        const Pending current = queue.back();
        queue.pop_back();
        std::string directory;
        const std::filesystem::path currentPath(current.path);
        if (currentPath.has_parent_path()) {
            directory = currentPath.parent_path().string();
        }

        std::vector<ImportRecord> imports;
        try {
            imports = collectImports(current.source, current.path);
        } catch (const SourceError& importError) {
            std::cerr << "clynxer: " << current.path << ':' << importError.line
                      << ':' << importError.column << ": " << importError.what()
                      << '\n';
            return false;
        }

        for (const auto& import : imports) {
            const bool native =
                import.path.size() >= 3 &&
                import.path.compare(import.path.size() - 3, 3, ".so") == 0;
            const std::string resolved =
                resolveModulePath(directory, import.path);
            if (resolved.empty()) {
                error = "module '" + import.path + "' was not found";
                return false;
            }
            if (native) {
                if (collectedLibraries.count(resolved) != 0) {
                    continue;
                }
                std::ifstream input(resolved, std::ios::binary);
                if (!input) {
                    error = "cannot read native module '" + resolved + "'";
                    return false;
                }
                ArchiveModule module;
                module.name = import.path;
                module.library.assign(
                    std::istreambuf_iterator<char>(input),
                    std::istreambuf_iterator<char>());
                collectedLibraries[resolved] = true;
                archive.modules.push_back(std::move(module));
                continue;
            }
            if (collectedSources.count(resolved) != 0) {
                continue;
            }
            std::ifstream input(resolved);
            if (!input) {
                error = "cannot read module '" + resolved + "'";
                return false;
            }
            std::ostringstream content;
            content << input.rdbuf();
            ArchiveModule module;
            module.name = import.path;
            module.source = content.str();
            collectedSources[resolved] = true;
            queue.push_back(Pending{resolved, module.source});
            archive.modules.push_back(std::move(module));
        }
    }
    return true;
}

int failWith(const char* key, const char* fallback, const std::string& value) {
    std::cerr << Config::instance().format(key, fallback, "{0}", value) << '\n';
    return 1;
}

int removedFlag(const std::string& flag, const std::string& replacement) {
    std::cerr << "clynxer: '" << flag
              << "' was removed with the bytecode backend";
    if (!replacement.empty()) {
        std::cerr << "; use " << replacement << " instead";
    }
    std::cerr << '\n';
    return 1;
}

// Builds a standalone ELF executable carrying the program and its modules.
int compileProgramToExecutable(const std::vector<std::string>& arguments) {
    if (arguments.empty() || arguments.size() > 2) {
        std::cerr << Config::instance().get(
                         "error.compile_usage",
                         "clynxer: --compile requires a .lynx file and an "
                         "optional output name")
                  << '\n';
        return 1;
    }
    const std::string& file = arguments[0];
    bool ok = false;
    const std::string source = readFile(file, file, ok);
    if (!ok) {
        return 1;
    }

    ProgramArchive archive;
    archive.mainPath = file;
    archive.mainSource = source;
    std::string error;
    if (!collectArchive(file, source, archive, error)) {
        // An empty message means the failure was already reported with a source
        // location.
        if (error.empty()) {
            return 1;
        }
        return failWith("error.compile_failed", "clynxer: compile failed: {0}",
                        error);
    }

    std::string outputPath;
    if (arguments.size() == 2) {
        outputPath = arguments[1];
    } else {
        outputPath = file;
        const std::size_t slash = outputPath.find_last_of('/');
        if (slash != std::string::npos) {
            outputPath = outputPath.substr(slash + 1);
        }
        if (outputPath.size() > 5 &&
            outputPath.compare(outputPath.size() - 5, 5, ".lynx") == 0) {
            outputPath.resize(outputPath.size() - 5);
        }
    }

    if (!writeBundledExecutable(outputPath, makeBundlePayload(archive),
                                error)) {
        return failWith("error.compile_failed", "clynxer: compile failed: {0}",
                        error);
    }
    std::cout << Config::instance().format("status.compile_ok",
                                           "Compiled: {0}", "{0}", outputPath)
              << '\n';
    return 0;
}

// Runs the program carried by a compiled executable.
int runCompiledPayload(const std::vector<uint8_t>& payload) {
    ProgramArchive archive;
    if (!decodeProgramArchive(payload, archive)) {
        std::cerr << Config::instance().get(
                         "error.payload_invalid",
                         "clynxer: the embedded program payload is invalid")
                  << '\n';
        return 1;
    }

    std::map<std::string, std::string> libraries;
    std::string error;
    if (!materializeLibraries(archive.modules, libraries, error)) {
        return failWith("error.compile_failed", "clynxer: compile failed: {0}",
                        error);
    }

    std::map<std::string, std::string> sources;
    for (const auto& module : archive.modules) {
        if (!module.library.empty()) {
            continue;
        }
        const std::string bare =
            std::filesystem::path(module.name).filename().string();
        sources[module.name] = module.source;
        sources[bare] = module.source;
        if (!std::filesystem::path(module.name).has_extension()) {
            sources[module.name + ".lynx"] = module.source;
        }
    }
    setEmbeddedModuleSources(std::move(sources));
    setEmbeddedModuleLibraries(std::move(libraries));
    return runProgram(archive.mainPath, archive.mainSource);
}

} // namespace

int shellMain(int argc, char** argv) {
    std::signal(SIGINT, handleInterrupt);

    // A compiled executable runs its embedded program directly.
    std::vector<uint8_t> selfPayload;
    if (readSelfPayload(selfPayload)) {
        const int exitCode = runCompiledPayload(selfPayload);
        if (interrupted != 0) {
            std::cout << '\n';
            return 130;
        }
        return exitCode;
    }

    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "-h" || args[0] == "--help") {
        printUsage();
        return 0;
    }
    if (args[0] == "-v" || args[0] == "--version" || args[0] == "-version" ||
        args[0] == "--v") {
        printVersion();
        return 0;
    }
    if (args[0] == "-easterEgg" || args[0] == "--easterEgg" ||
        args[0] == "--idklmao" || args[0] == "-wnwnerbcyunwrbygnubeuyxnqybxun") {
        printEasterEgg();
        return 0;
    }
    if (args[0] == "--list-stdlibs" || args[0] == "--stdlibs" ||
        args[0] == "-stdlibs" || args[0] == "-list-stdlibs") {
        return listStdlibs();
    }
    if (args[0] == "--install" || args[0] == "--uninstall") {
        return unsupportedFeature(args[0]);
    }
    if (args[0] == "--validate-executeable" ||
        args[0] == "--validate-executable") {
        return unsupportedFeature(args[0]);
    }
    if (args[0] == "--benchmark-compile" || args[0] == "--bench-compile") {
        return removedFlag(args[0], "clynxer --compile");
    }
    if (args[0] == "--compile" || args[0] == "-c" || args[0] == "--c" ||
        args[0] == "-compile" || args[0] == "--bundle" ||
        args[0] == "-bundle") {
        return compileProgramToExecutable(
            std::vector<std::string>(args.begin() + 1, args.end()));
    }
    if (args[0] == "--view-bytecode" || args[0] == "--inspect-bytecode" ||
        args[0] == "--disasm") {
        return removedFlag(args[0], "clynxer --compile");
    }
    if (args[0] == "--no-cache" || args[0] == "--no-opt") {
        return removedFlag(args[0], "clynxer --compile");
    }
    if (args[0] == "--ast" || args[0] == "--format" ||
        args[0] == "--format-oneline") {
        return unsupportedFeature(args[0]);
    }
    if (args[0] == "--lint") {
        if (args.size() != 2) {
            std::cerr << Config::instance().format(
                                 "error.requires_one_file",
                                 "clynxer: {0} requires exactly one file argument",
                                 "{0}", args[0])
                      << '\n';
            return 1;
        }
        bool ok = false;
        const std::string source = readFile(args[1], args[1], ok);
        if (!ok) {
            return 1;
        }
        return lintFile(args[1], source);
    }

    const std::string& display = args[0];
    if (display.size() > 6 &&
        display.compare(display.size() - 6, 6, ".lynxc") == 0) {
        std::cerr << Config::instance().get(
                         "error.bytecode_removed",
                         "clynxer: bytecode files are no longer supported; "
                         "compile the .lynx source with --compile instead")
                  << '\n';
        return 1;
    }

    bool ok = false;
    const std::string source = readFile(display, display, ok);
    if (!ok) {
        return 1;
    }
    const int exitCode = runProgram(display, source);
    if (interrupted != 0) {
        std::cout << '\n';
        return 130;
    }
    return exitCode;
}

} // namespace clynxer
