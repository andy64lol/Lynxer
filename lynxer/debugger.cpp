#include "debugger.hpp"

#include "runtime.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace lynxer {
namespace {

struct Frame {
    std::string name;
    Environment* environment = nullptr;
    std::size_t scopeDepth = 0;
    int line = 0;
    std::string path;
};

struct Breakpoint {
    std::string path;
    int line = 0;

    bool operator<(const Breakpoint& other) const {
        return path == other.path ? line < other.line : path < other.path;
    }
};

bool active = false;
bool stepMode = true;
std::vector<Frame> frames;
std::set<Breakpoint> breakpoints;

std::string normalizePath(const std::string& path) {
    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    return normalized;
}

bool hasBreakpoint(const std::string& path, int line) {
    for (const Breakpoint& breakpoint : breakpoints) {
        if (breakpoint.line == line &&
            (breakpoint.path.empty() ||
             normalizePath(path).find(breakpoint.path) != std::string::npos)) {
            return true;
        }
    }
    return false;
}

void printSourceLine(const std::string& path, int requestedLine) {
    if (path.empty() || requestedLine <= 0) return;
    std::ifstream input(path);
    if (!input) return;
    std::string line;
    for (int number = 1; number <= requestedLine && std::getline(input, line);
         ++number) {
        if (number == requestedLine) {
            std::cout << "  " << number << " | " << line << '\n';
        }
    }
}

bool parseBreakpoint(const std::string& input, Breakpoint& result) {
    const std::size_t separator = input.rfind(':');
    std::string lineText = input;
    if (separator != std::string::npos) {
        result.path = normalizePath(input.substr(0, separator));
        lineText = input.substr(separator + 1);
    } else {
        result.path.clear();
    }
    try {
        std::size_t consumed = 0;
        result.line = std::stoi(lineText, &consumed);
        return consumed == lineText.size() && result.line > 0;
    } catch (...) {
        return false;
    }
}

std::vector<Frame> fallbackFrame(Environment& environment) {
    return {Frame{"<program>", &environment, environment.scopeDepth(), 0, ""}};
}

void printStack(const std::vector<Frame>& stack) {
    for (std::size_t index = 0; index < stack.size(); ++index) {
        const Frame& frame = stack[stack.size() - index - 1];
        std::cout << '#' << index << ' ' << frame.name;
        if (frame.line > 0) {
            std::cout << " at " << frame.path << ':' << frame.line;
        }
        std::cout << '\n';
    }
}

void printLocals(const Frame& frame) {
    if (frame.environment == nullptr) return;
    const auto variables = frame.environment->variablesAtScope(frame.scopeDepth);
    if (variables.empty()) {
        std::cout << "(no visible variables)\n";
        return;
    }
    for (const auto& entry : variables) {
        std::cout << entry.second.type << ' ' << entry.first << " = "
                  << valueToString(entry.second.value) << '\n';
    }
}

bool runPrompt(const Statement& statement, Environment& environment) {
    std::vector<Frame> stack = frames.empty() ? fallbackFrame(environment) : frames;
    if (!stack.empty()) {
        stack.back().line = statement.sourceLine();
        stack.back().path = statement.sourcePath();
    }
    std::cout << "stopped at "
              << (statement.sourcePath().empty() ? "<program>"
                                                 : statement.sourcePath())
              << ':' << statement.sourceLine() << '\n';
    printSourceLine(statement.sourcePath(), statement.sourceLine());
    std::size_t selectedFrame = 0;
    std::string input;
    while (true) {
        std::cout << "(lynxer) " << std::flush;
        if (!std::getline(std::cin, input)) {
            active = false;
            return false;
        }
        std::istringstream command(input);
        std::string operation;
        command >> operation;
        if (operation.empty()) continue;
        if (operation == "s" || operation == "step") {
            stepMode = true;
            return false;
        }
        if (operation == "c" || operation == "continue") {
            stepMode = false;
            return false;
        }
        if (operation == "b" || operation == "break") {
            std::string location;
            Breakpoint breakpoint;
            if (!(command >> location) || !parseBreakpoint(location, breakpoint)) {
                std::cout << "usage: break [file:]line\n";
            } else {
                breakpoints.insert(breakpoint);
                std::cout << "breakpoint set at ";
                if (!breakpoint.path.empty()) std::cout << breakpoint.path << ':';
                std::cout << breakpoint.line << '\n';
            }
            continue;
        }
        if (operation == "delete" || operation == "clear") {
            std::string location;
            if (!(command >> location)) {
                breakpoints.clear();
                std::cout << "all breakpoints cleared\n";
            } else {
                Breakpoint breakpoint;
                if (parseBreakpoint(location, breakpoint) &&
                    breakpoints.erase(breakpoint) != 0) {
                    std::cout << "breakpoint cleared\n";
                } else {
                    std::cout << "no matching breakpoint\n";
                }
            }
            continue;
        }
        if (operation == "bt" || operation == "backtrace") {
            printStack(stack);
            continue;
        }
        if (operation == "frame") {
            std::size_t index = 0;
            if (!(command >> index) || index >= stack.size()) {
                std::cout << "frame index must be between 0 and "
                          << (stack.empty() ? 0 : stack.size() - 1) << '\n';
            } else {
                selectedFrame = index;
                const Frame& frame = stack[stack.size() - index - 1];
                std::cout << "selected " << frame.name << '\n';
            }
            continue;
        }
        const Frame& selected =
            stack[stack.size() - selectedFrame - 1];
        if (operation == "locals" || operation == "vars") {
            printLocals(selected);
            continue;
        }
        if (operation == "p" || operation == "print") {
            std::string name;
            if (!(command >> name) || selected.environment == nullptr) {
                std::cout << "usage: print variable\n";
                continue;
            }
            try {
                const auto variables = selected.environment->visibleVariablesAtDepth(
                    selected.scopeDepth);
                const auto value = variables.find(name);
                if (value == variables.end()) {
                    std::cout << "unknown variable '" << name << "'\n";
                } else {
                    std::cout << value->second.type << ' ' << name << " = "
                              << valueToString(value->second.value) << '\n';
                }
            } catch (const std::exception& error) {
                std::cout << error.what() << '\n';
            }
            continue;
        }
        if (operation == "q" || operation == "quit" || operation == "detach") {
            active = false;
            std::cout << "debugger detached; program continues\n";
            return false;
        }
        if (operation == "h" || operation == "help") {
            std::cout << "step | continue | break [file:]line | delete [file:]line\n"
                         "locals | print name | backtrace | frame n | detach\n";
            continue;
        }
        std::cout << "unknown command; type help\n";
    }
}

} // namespace

void setDebuggerEnabled(bool enabled) {
    active = enabled;
    stepMode = true;
    frames.clear();
    breakpoints.clear();
}

bool debuggerEnabled() { return active; }

void debugBeforeStatement(const Statement& statement,
                          Environment& environment) {
    if (!active || statement.sourceLine() <= 0) return;
    if (!frames.empty()) {
        frames.back().line = statement.sourceLine();
        frames.back().path = statement.sourcePath();
    }
    if (stepMode || hasBreakpoint(statement.sourcePath(), statement.sourceLine())) {
        (void)runPrompt(statement, environment);
    }
}

void debugPushFrame(const std::string& name, Environment& environment) {
    if (!active) return;
    frames.push_back(
        Frame{name, &environment, environment.scopeDepth(), 0, ""});
}

void debugPopFrame() {
    if (active && !frames.empty()) frames.pop_back();
}

} // namespace lynxer
