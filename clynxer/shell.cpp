#include "shell.hpp"

#include "bundle.hpp"
#include "compiler.hpp"
#include "config.hpp"
#include "error.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "runtime.hpp"
#include "vm.hpp"

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
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
    std::cout << "  clynxer <file.lynx>                    Run a Lynxer source file\n";
    std::cout << "  clynxer --compile <file.lynx>          Compile to bytecode (.lynxc)\n";
    std::cout << "  clynxer --compile --no-cache <file>    Recompile even when bytecode is current\n";
    std::cout << "  clynxer --compile --no-opt <file>      Compile without optimization passes\n";
    std::cout << "  clynxer --bundle <file.lynx> [name]     Build a standalone native executable\n";
    std::cout << "  clynxer <file.lynxc>                   Run a compiled bytecode file\n";
    std::cout << "  clynxer --view-bytecode <file.lynxc>   Inspect bytecode metadata and structure\n";
    std::cout << "  clynxer --ast <file.lynx>              Parse and print the abstract syntax tree\n";
    std::cout << "  clynxer --benchmark-compile <files...> Benchmark optimized and unoptimized compilation\n";
    std::cout << "  clynxer --format <file.lynx>           Format a Lynxer source file in place\n";
    std::cout << "  clynxer --format-oneline <file.lynx>   Compact a Lynxer source file to one line\n";
    std::cout << "  clynxer --lint <file.lynx>             Check Lynxer syntax without running it\n";
    std::cout << "  clynxer --validate-executeable         Run the comprehensive interpreter validator\n";
    std::cout << "  clynxer --version                      Print version\n";
    std::cout << "  clynxer --list-stdlibs                 List available Lynxer stdlib modules\n";
    std::cout << "  clynxer --install                      Install the compiled executable as /usr/bin/lynxer, may require sudo\n";
    std::cout << "  clynxer --uninstall                    Remove /usr/bin/lynxer, also may require sudo\n";
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

// Lexes and parses a source file into the function map (shared by the run
// and compile paths).
std::unordered_map<std::string, Function> parseSource(const std::string& display,
                                                       const std::string& source) {
    Lexer lexer(source, display);
    Parser parser(lexer.scan());
    return parser.parseProgram();
}

// Two-placeholder message helper: replaces {0} and {1}.
std::string message2(const char* key, const char* fallback,
                     const std::string& first, const std::string& second) {
    std::string value = Config::instance().get(key, fallback);
    std::size_t position = value.find("{0}");
    if (position != std::string::npos) {
        value.replace(position, 3, first);
    }
    position = value.find("{1}");
    if (position != std::string::npos) {
        value.replace(position, 3, second);
    }
    return value;
}

int compileFile(const std::string& display, const std::string& sourcePath,
                const std::string& source, bool optimize, bool useCache) {
    const std::string outputPath =
        sourcePath.substr(0, sourcePath.find_last_of('.')) + ".lynxc";

    const uint64_t hash = fnv1a64(source);
    if (useCache) {
        try {
            std::ifstream existing(outputPath, std::ios::binary);
            if (existing) {
                std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(existing)),
                                           std::istreambuf_iterator<char>());
                const CompiledProgram cached = loadProgram(bytes);
                if (cached.sourcePath == sourcePath && cached.sourceHash == hash &&
                    (cached.flags & BYTECODE_FLAG_OPTIMIZED) ==
                        (optimize ? BYTECODE_FLAG_OPTIMIZED : 0)) {
                    std::cout << Config::instance().format(
                                     "status.compile_skipped",
                                     "clynxer: bytecode is up to date: '{0}'",
                                     "{0}", outputPath)
                              << '\n';
                    return 0;
                }
            }
        } catch (const BytecodeError&) {
            // Stale or corrupt cache: recompile.
        }
    }

    try {
        const CompiledProgram program =
            compileProgram(parseSource(display, source), sourcePath, source,
                           optimize);
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        if (!output) {
            std::cerr << Config::instance().format(
                             "error.bytecode_write_failed",
                             "clynxer: could not write '{0}'", "{0}",
                             outputPath)
                      << '\n';
            return 1;
        }
        const std::vector<uint8_t> bytes = serializeProgram(program);
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        if (!output) {
            std::cerr << Config::instance().format(
                             "error.bytecode_write_failed",
                             "clynxer: could not write '{0}'", "{0}",
                             outputPath)
                      << '\n';
            return 1;
        }
    } catch (const SourceError& error) {
        std::cerr << "clynxer: " << display << ':' << error.line << ':'
                  << error.column << ": " << error.what() << '\n';
        return 1;
    }
    std::cout << Config::instance().format(
                     "status.compile_ok", "Compiled: {0}", "{0}", outputPath)
              << '\n';
    return 0;
}

// Loads, validates, and runs serialized bytecode; used for .lynxc files and
// for the payload embedded in a bundled executable.
int runBytecodeBytes(const std::vector<uint8_t>& bytes,
                     const std::string& display) {
    CompiledProgram program;
    try {
        program = loadProgram(bytes);
    } catch (const BytecodeError& error) {
        std::cerr << message2("error.bytecode_invalid",
                              "clynxer: invalid bytecode '{0}': {1}", display,
                              error.what())
                  << '\n';
        return 1;
    }
    try {
        Environment environment;
        runProgram(program, environment);
        return 0;
    } catch (const SourceError& error) {
        std::cerr << "clynxer: " << program.sourcePath << ':' << error.line
                  << ':' << error.column << ": " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << message2("error.interpreter_failure",
                              "clynxer: interpreter failure in '{0}': {1}",
                              program.sourcePath, error.what())
                  << '\n';
        return 1;
    }
}

int runBytecodeFile(const std::string& display, const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::cerr << Config::instance().format("error.file_not_found",
                                               "clynxer: file not found: '{0}'",
                                               "{0}", display)
                  << '\n';
        return 1;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    return runBytecodeBytes(bytes, display);
}

int bundleProgram(const std::vector<std::string>& arguments) {
    if (arguments.empty() || arguments.size() > 2) {
        std::cerr << Config::instance().get(
                             "error.bundle_usage",
                             "clynxer: --bundle requires a .lynx file and "
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
    std::vector<uint8_t> bytecode;
    try {
        const CompiledProgram program =
            compileProgram(parseSource(file, source), file, source, true);
        bytecode = serializeProgram(program);
    } catch (const SourceError& error) {
        std::cerr << "clynxer: " << file << ':' << error.line << ':'
                  << error.column << ": " << error.what() << '\n';
        return 1;
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

    std::string error;
    if (!writeBundledExecutable(outputPath, makeBundlePayload(bytecode),
                                error)) {
        std::cerr << Config::instance().format(
                         "error.bundle_failed", "clynxer: bundle failed: {0}",
                         "{0}", error)
                  << '\n';
        return 1;
    }
    std::cout << Config::instance().format("status.bundle_ok",
                                           "Bundled: {0}", "{0}", outputPath)
              << '\n';
    return 0;
}

int viewBytecodeFile(const std::string& display, const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::cerr << Config::instance().format("error.file_not_found",
                                               "clynxer: file not found: '{0}'",
                                               "{0}", display)
                  << '\n';
        return 1;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    try {
        std::cout << disassembleProgram(loadProgram(bytes));
        return 0;
    } catch (const BytecodeError& error) {
        std::cerr << message2("error.bytecode_invalid",
                              "clynxer: invalid bytecode '{0}': {1}", display,
                              error.what())
                  << '\n';
        return 1;
    }
}

} // namespace

int shellMain(int argc, char** argv) {
    std::signal(SIGINT, handleInterrupt);

    // A bundled executable runs its embedded program directly.
    std::vector<uint8_t> selfPayload;
    if (readSelfPayload(selfPayload)) {
        const int exitCode = runBytecodeBytes(selfPayload, "bundled program");
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
        return unsupportedFeature(args[0]);
    }
    if (args[0] == "--compile" || args[0] == "-c" || args[0] == "--c" ||
        args[0] == "-compile") {
        bool optimize = true;
        bool useCache = true;
        std::vector<std::string> files;
        for (std::size_t index = 1; index < args.size(); ++index) {
            if (args[index] == "--no-opt") {
                optimize = false;
            } else if (args[index] == "--no-cache") {
                useCache = false;
            } else {
                files.push_back(args[index]);
            }
        }
        if (files.size() != 1) {
            std::cerr << Config::instance().get(
                                 "error.compile_requires_file",
                                 "clynxer: --compile requires a file argument")
                      << '\n';
            return 1;
        }
        const std::string& file = files[0];
        const std::string& sourcePath = file;
        bool ok = false;
        const std::string source = readFile(sourcePath, file, ok);
        if (!ok) {
            return 1;
        }
        return compileFile(file, sourcePath, source, optimize, useCache);
    }
    if (args[0] == "--bundle" || args[0] == "-bundle") {
        return bundleProgram(std::vector<std::string>(args.begin() + 1,
                                                       args.end()));
    }
    if (args[0] == "--view-bytecode" || args[0] == "--inspect-bytecode" ||
        args[0] == "--disasm") {
        if (args.size() != 2) {
            std::cerr << Config::instance().get(
                                 "error.view_bytecode_usage",
                                 "clynxer: --view-bytecode requires a .lynxc "
                                 "file argument")
                      << '\n';
            return 1;
        }
        return viewBytecodeFile(args[1], args[1]);
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
        return runBytecodeFile(display, display);
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
