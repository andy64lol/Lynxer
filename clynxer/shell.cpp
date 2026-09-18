#include "shell.hpp"

#include "bundle.hpp"
#include "config.hpp"
#include "error.hpp"
#include "interrupt.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "runtime.hpp"

#include <algorithm>
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
    std::cout << "  clynxer <file.lynx>                          Run a Lynxer source file\n";
    std::cout << "  clynxer --compile <a.lynx> [options] [name]  Compile input files into one executable\n";
    std::cout << "  clynxer --bundle <a.lynx> [options] [name]   Alias of --compile\n";
    std::cout << "      --include <file>                         Embed a module, native library or data file\n";
    std::cout << "      -o, --output <name>                      Name the output executable\n";
    std::cout << "  clynxer --ast <file.lynx>                    Parse and print the abstract syntax tree\n";
    std::cout << "  clynxer --format <file.lynx>                 Format a Lynxer source file in place\n";
    std::cout << "  clynxer --format-oneline <file.lynx>         Compact a Lynxer source file to one line\n";
    std::cout << "  clynxer --lint <file.lynx>                   Check Lynxer syntax without running it\n";
    std::cout << "  clynxer --validate-executeable               Run the comprehensive interpreter validator\n";
    std::cout << "  clynxer --version                            Print version\n";
    std::cout << "  clynxer --list-stdlibs                       List available Lynxer stdlib modules\n";
    std::cout << "  clynxer --install                            Install the compiled executable as /usr/bin/lynxer, may require sudo\n";
    std::cout << "  clynxer --uninstall                          Remove /usr/bin/lynxer, also may require sudo\n";
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
    } catch (const InterruptError&) {
        return 130;
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

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() > suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
               0;
}

// Resolves a file named directly on the command line: the path as written when
// it exists, otherwise the same search `import` uses.
std::string resolveExplicitPath(const std::string& given) {
    std::error_code error;
    if (std::filesystem::is_regular_file(given, error)) {
        return given;
    }
    return resolveModulePath("", given);
}

std::string baseName(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}

// Recursively collects `mainSource` and every module it imports into `archive`,
// then embeds each file named explicitly on the command line. Source modules are
// embedded as text; native modules are embedded as library bytes so the compiled
// executable does not need the build tree at run time.
bool collectArchive(const std::string& mainPath, const std::string& mainSource,
                    const std::vector<std::string>& extraInputs,
                    ProgramArchive& archive, std::string& error) {
    std::map<std::string, bool> collectedSources;
    std::map<std::string, bool> collectedLibraries;
    std::map<std::string, bool> collectedAssets;

    struct Pending {
        std::string path;
        std::string source;
    };
    std::vector<Pending> queue;
    queue.push_back(Pending{mainPath, mainSource});
    collectedSources[mainPath] = true;

    // Explicit inputs are embedded first so they take part in the same
    // de-duplication as imported modules, so a source input has its own imports
    // walked below, and so `import` can resolve them by name even when they live
    // outside the normal module search path. Inputs that are neither `.lynx` nor
    // `.so` are embedded as data files.
    std::map<std::string, bool> extraKeys;
    const auto recordExtraKeys = [&extraKeys](const std::string& given) {
        const std::string bare = baseName(given);
        extraKeys[given] = true;
        extraKeys[bare] = true;
        if (endsWith(bare, ".lynx")) {
            extraKeys[bare.substr(0, bare.size() - 5)] = true;
        }
    };

    for (const auto& given : extraInputs) {
        const bool native = endsWith(given, ".so");
        const bool sourceModule = endsWith(given, ".lynx");
        const std::string resolved = resolveExplicitPath(given);
        if (resolved.empty()) {
            error = "input file '" + given + "' was not found";
            return false;
        }
        if (native || sourceModule) {
            recordExtraKeys(given);
            recordExtraKeys(resolved);
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
            module.name = given;
            module.library.assign(std::istreambuf_iterator<char>(input),
                                  std::istreambuf_iterator<char>());
            collectedLibraries[resolved] = true;
            archive.modules.push_back(std::move(module));
            continue;
        }
        if (!sourceModule) {
            if (collectedAssets.count(resolved) != 0) {
                continue;
            }
            std::ifstream input(resolved, std::ios::binary);
            if (!input) {
                error = "cannot read included file '" + resolved + "'";
                return false;
            }
            ArchiveModule module;
            module.name = given;
            module.asset.assign(std::istreambuf_iterator<char>(input),
                                std::istreambuf_iterator<char>());
            collectedAssets[resolved] = true;
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
        module.name = given;
        module.source = content.str();
        collectedSources[resolved] = true;
        queue.push_back(Pending{resolved, module.source});
        archive.modules.push_back(std::move(module));
    }

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
            const bool native = endsWith(import.path, ".so");
            // An explicitly supplied input already satisfies the import.
            if (extraKeys.count(import.path) != 0) {
                continue;
            }
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

// Builds a standalone ELF executable carrying one or more input files and every
// module they import.
int compileProgramToExecutable(const std::vector<std::string>& arguments) {
    std::vector<std::string> inputs;
    std::string outputName;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string& argument = arguments[index];
        if (argument == "-o" || argument == "--output" ||
            argument == "-name" || argument == "--name") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "clynxer: " << argument << " requires a name\n";
                return 1;
            }
            if (!outputName.empty()) {
                std::cerr << "clynxer: the output name was given twice\n";
                return 1;
            }
            outputName = arguments[++index];
            continue;
        }
        // --include takes any file: a .lynx module, a native .so, or a data
        // file that the program reads with bundledFile().
        if (argument == "--include" || argument == "-i") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "clynxer: " << argument << " requires a file\n";
                return 1;
            }
            inputs.push_back(arguments[++index]);
            continue;
        }
        // Anything that looks like a source or library file is an input; a
        // bare name is the output.
        if (endsWith(argument, ".lynx") || endsWith(argument, ".so")) {
            inputs.push_back(argument);
            continue;
        }
        if (!outputName.empty()) {
            std::cerr << "clynxer: unexpected extra argument '" << argument
                      << "'\n";
            return 1;
        }
        outputName = argument;
    }

    if (inputs.empty()) {
        std::cerr << Config::instance().get(
                         "error.compile_usage",
                         "clynxer: --compile requires a .lynx file, optionally "
                         "followed by --include <file> inputs and an output "
                         "name")
                  << '\n';
        return 1;
    }
    if (!endsWith(inputs.front(), ".lynx")) {
        std::cerr << "clynxer: the first input must be a .lynx program file\n";
        return 1;
    }

    const std::string& file = inputs.front();
    bool ok = false;
    const std::string source = readFile(file, file, ok);
    if (!ok) {
        return 1;
    }

    ProgramArchive archive;
    archive.mainPath = file;
    archive.mainSource = source;
    const std::vector<std::string> extras(inputs.begin() + 1, inputs.end());
    std::string error;
    if (!collectArchive(file, source, extras, archive, error)) {
        // An empty message means the failure was already reported with a source
        // location.
        if (error.empty()) {
            return 1;
        }
        return failWith("error.compile_failed", "clynxer: compile failed: {0}",
                        error);
    }

    std::string outputPath = outputName;
    if (outputPath.empty()) {
        outputPath = baseName(file);
        if (endsWith(outputPath, ".lynx")) {
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
    std::map<std::string, std::string> assets;
    std::string error;
    if (!materializeBundle(archive.modules, libraries, assets, error)) {
        return failWith("error.compile_failed", "clynxer: compile failed: {0}",
                        error);
    }
    setBundledAssets(std::move(assets));

    std::map<std::string, std::string> sources;
    for (const auto& module : archive.modules) {
        if (!module.library.empty()) {
            continue;
        }
        const std::string bare = baseName(module.name);
        sources[module.name] = module.source;
        sources[bare] = module.source;
        // Register both the bare module name and its `.lynx` file name so
        // `import("name")` resolves whether or not the extension was written.
        if (endsWith(bare, ".lynx")) {
            sources[bare.substr(0, bare.size() - 5)] = module.source;
        } else {
            sources[bare + ".lynx"] = module.source;
        }
    }
    setEmbeddedModuleSources(std::move(sources));
    setEmbeddedModuleLibraries(std::move(libraries));
    return runProgram(archive.mainPath, archive.mainSource);
}

} // namespace

int shellMain(int argc, char** argv) {
    installInterruptHandler();

    // A compiled executable runs its embedded program directly.
    std::vector<uint8_t> selfPayload;
    if (readSelfPayload(selfPayload)) {
        const int exitCode = runCompiledPayload(selfPayload);
        if (interruptRequested()) {
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
    if (interruptRequested()) {
        std::cout << '\n';
        return 130;
    }
    return exitCode;
}

} // namespace clynxer
