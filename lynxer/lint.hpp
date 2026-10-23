#pragma once

#include "ast.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace lynxer {

struct LintDiagnostic {
    enum class Severity { Warning, Error };

    Severity severity = Severity::Warning;
    int line = 0;
    int column = 0;
    std::string message;
};

// Runs conservative, whole-program checks on the parsed AST. Unknown types
// and external functions are left to the runtime rather than guessed at.
std::vector<LintDiagnostic> analyzeLint(
    const std::unordered_map<std::string, Function>& functions);

} // namespace lynxer
