#include "error.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "runtime.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace clynxer {

static std::string readFile(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open source file '" + path + "'");
    }
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

static void printUsage() {
    std::cout << "Usage: clynxer <file.lynx>\n"
                 "\n"
                 "Run a small, standalone C++ Lynxer program.\n";
}

} // namespace clynxer

int main(int argc, char** argv) {
    if (argc != 2) {
        clynxer::printUsage();
        return argc == 1 ? 0 : 2;
    }
    const std::string argument = argv[1];
    if (argument == "--help" || argument == "-h") {
        clynxer::printUsage();
        return 0;
    }
    if (argument == "--version") {
        std::cout << "clynxer 0.1.0\n";
        return 0;
    }

    try {
        const std::string source = clynxer::readFile(argument);
        clynxer::Lexer lexer(source, argument);
        clynxer::Parser parser(lexer.scan());
        auto functions = parser.parseProgram();
        clynxer::Environment environment;
        for (const std::string functionName : {"setup", "main"}) {
            auto found = functions.find(functionName);
            if (found == functions.end()) {
                continue;
            }
            for (const auto& statement : found->second.statements) {
                statement->execute(environment);
            }
        }
        return 0;
    } catch (const clynxer::SourceError& error) {
        std::cerr << "clynxer: " << argument << ':' << error.line << ':'
                  << error.column << ": " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "clynxer: " << error.what() << '\n';
        return 1;
    }
}
