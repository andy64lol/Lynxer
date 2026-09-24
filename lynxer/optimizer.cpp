#include "optimizer.hpp"

#include "types.hpp"

#include <utility>

namespace lynxer {

namespace {

bool optimizerEnabledFlag = true;
OptimizationStats processStats;

} // namespace

bool optimizerEnabled() { return optimizerEnabledFlag; }

void setOptimizerEnabled(bool enabled) { optimizerEnabledFlag = enabled; }

OptimizationStats& optimizationStats() { return processStats; }

ExpressionPtr optimizeExpression(ExpressionPtr expression,
                                 OptimizationStats& stats) {
    if (!expression) {
        return expression;
    }
    if (ExpressionPtr replacement = expression->optimize(stats)) {
        return replacement;
    }
    return expression;
}

void optimizeStatementList(StatementList& statements, OptimizationStats& stats) {
    StatementList result;
    result.reserve(statements.size());
    for (auto& statement : statements) {
        if (!statement) {
            continue;
        }
        statement->optimizeChildren(stats);
        // `rewrite` lets one statement become zero or many: a constant `if`
        // splices its taken arm in, and `while (false)` / `iterate (0)` vanish.
        StatementList replacement;
        if (statement->rewrite(replacement, stats)) {
            for (auto& entry : replacement) {
                result.push_back(std::move(entry));
            }
        } else {
            result.push_back(std::move(statement));
        }
    }
    statements = std::move(result);
}

void optimizeFunction(Function& function, OptimizationStats& stats) {
    for (auto& parameter : function.parameters) {
        parameter.defaultValue =
            optimizeExpression(std::move(parameter.defaultValue), stats);
    }
    optimizeStatementList(function.statements, stats);
}

void optimizeProgram(std::unordered_map<std::string, Function>& functions,
                     OptimizationStats& stats) {
    for (auto& entry : functions) {
        optimizeFunction(entry.second, stats);
    }
    TypeRegistry::instance().forEachClass([&stats](ClassDef& definition) {
        for (auto& field : definition.fields) {
            field.initializer =
                optimizeExpression(std::move(field.initializer), stats);
        }
        for (auto& method : definition.methods) {
            optimizeStatementList(method.body, stats);
        }
    });
}

} // namespace lynxer
