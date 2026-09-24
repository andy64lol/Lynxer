#pragma once

#include "ast.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>

namespace lynxer {

// Counters for the AST optimization pass. The pass runs after parsing and
// before execution, in both interpreted and compiled runs. It is
// semantics-preserving, so the counters exist only for the opt-in report that
// LYNXER_OPT_REPORT prints; they never influence program output.
struct OptimizationStats {
    std::size_t constantFolds = 0;
    std::size_t shortCircuits = 0;
    std::size_t deadBranches = 0;
};

// Rewrites `expression` in place where possible. Returns the (possibly new)
// node; a node that cannot be simplified is returned unchanged.
//
// The pass only folds expressions built entirely from literals, simplifies a
// constant and/or, and removes branches whose condition is a constant. It
// deliberately does NOT apply algebraic identities such as `x + 0 -> x` or
// `x * 1 -> x`: Clynxer is dynamically typed, so `+` may concatenate strings and
// the arithmetic operators raise on a non-numeric operand, meaning dropping an
// operand could change a result or swallow a runtime type error.
ExpressionPtr optimizeExpression(ExpressionPtr expression,
                                 OptimizationStats& stats);

// Optimizes a statement list, including deleting dead branches and splicing
// the taken arm of a constant `if` into the surrounding list.
void optimizeStatementList(StatementList& statements, OptimizationStats& stats);

// Optimizes a function body and its default parameter values.
void optimizeFunction(Function& function, OptimizationStats& stats);

// Optimizes every function, and every class field initializer and method body
// registered in the TypeRegistry.
void optimizeProgram(std::unordered_map<std::string, Function>& functions,
                     OptimizationStats& stats);

// Process-wide counters. The main program and any imported module parsed at
// run time add to these, so one report covers the whole run.
OptimizationStats& optimizationStats();

// The pass is on by default; `lynxer --no-opt` turns it off for the run.
bool optimizerEnabled();
void setOptimizerEnabled(bool enabled);

} // namespace lynxer
