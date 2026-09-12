#include "shell.hpp"

#include "config.hpp"
#include "error.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "runtime.hpp"

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
        for (const std::string functionName : {"setup", "main"}) {
            auto found = functions.find(functionName);
            if (found == functions.end()) {
                continue;
            }
            environment.setSetupInProgress(functionName == "setup");
            for (const auto& statement : found->second.statements) {
                statement->execute(environment);
            }
        }
        environment.setSetupInProgress(false);
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

} // namespace

int shellMain(int argc, char** argv) {
    std::signal(SIGINT, handleInterrupt);

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
        return unsupportedFeature(args[0]);
    }
    if (args[0] == "--bundle" || args[0] == "-bundle") {
        return unsupportedFeature(args[0]);
    }
    if (args[0] == "--view-bytecode" || args[0] == "--inspect-bytecode" ||
        args[0] == "--disasm") {
        return unsupportedFeature(args[0]);
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
                         "error.bytecode_unsupported",
                         "clynxer: running compiled .lynxc bytecode is not "
                         "supported in CLynxer yet")
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
