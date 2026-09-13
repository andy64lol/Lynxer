#include "ast.hpp"

#include "builtins.hpp"
#include "config.hpp"
#include "error.hpp"
#include "ops.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>
#include <variant>

namespace clynxer {

LiteralExpression::LiteralExpression(Value value) : value_(std::move(value)) {}

Value LiteralExpression::evaluate(Environment&) const { return value_; }

VariableExpression::VariableExpression(std::string name, int line, int column)
    : name_(std::move(name)), line_(line), column_(column) {}

Value VariableExpression::evaluate(Environment& environment) const {
    return environment.get(name_, line_, column_);
}

UnaryExpression::UnaryExpression(std::string operation, ExpressionPtr operand,
                                 int line, int column)
    : operation_(std::move(operation)), operand_(std::move(operand)),
      line_(line), column_(column) {}

Value UnaryExpression::evaluate(Environment& environment) const {
    return applyUnary(operation_, operand_->evaluate(environment), line_,
                      column_);
}

BinaryExpression::BinaryExpression(std::string operation, ExpressionPtr left,
                                   ExpressionPtr right, int line, int column)
    : operation_(std::move(operation)), left_(std::move(left)),
      right_(std::move(right)), line_(line), column_(column) {}

Value BinaryExpression::evaluate(Environment& environment) const {
    const Value left = left_->evaluate(environment);
    if (operation_ == "&&" && !isTruthy(left)) {
        return false;
    }
    if (operation_ == "||" && isTruthy(left)) {
        return true;
    }
    const Value right = right_->evaluate(environment);

    if (operation_ == "&&") {
        return isTruthy(right);
    }
    if (operation_ == "||") {
        return isTruthy(right);
    }
    return applyBinary(binOpFromString(operation_, line_, column_), left,
                       right, line_, column_);
}

CallExpression::CallExpression(std::string name,
                               std::vector<ExpressionPtr> arguments, int line,
                               int column)
    : name_(std::move(name)), arguments_(std::move(arguments)), line_(line),
      column_(column) {}

Value CallExpression::evaluate(Environment& environment) const {
    std::vector<Value> arguments;
    arguments.reserve(arguments_.size());
    for (const auto& argument : arguments_) {
        arguments.push_back(argument->evaluate(environment));
    }
    return callBuiltin(name_, arguments, environment, line_, column_);
}

ListLiteralExpression::ListLiteralExpression(
    std::vector<ExpressionPtr> elements)
    : elements_(std::move(elements)) {}

Value ListLiteralExpression::evaluate(Environment& environment) const {
    std::vector<Value> values;
    values.reserve(elements_.size());
    for (const auto& element : elements_) {
        values.push_back(element->evaluate(environment));
    }
    return std::make_shared<List>(List{std::move(values)});
}

TupleLiteralExpression::TupleLiteralExpression(
    std::vector<ExpressionPtr> elements)
    : elements_(std::move(elements)) {}

Value TupleLiteralExpression::evaluate(Environment& environment) const {
    std::vector<Value> values;
    values.reserve(elements_.size());
    for (const auto& element : elements_) {
        values.push_back(element->evaluate(environment));
    }
    return std::make_shared<Tuple>(Tuple{std::move(values)});
}

void InterpStringExpression::addLiteral(std::string text) {
    literals_.push_back(std::move(text));
}

void InterpStringExpression::addExpression(ExpressionPtr expression) {
    expressions_.push_back(std::move(expression));
}

Value InterpStringExpression::evaluate(Environment& environment) const {
    std::vector<Value> values;
    values.reserve(expressions_.size());
    for (const auto& expression : expressions_) {
        values.push_back(expression->evaluate(environment));
    }
    return assembleInterp(literals_, values);
}

DeclarationStatement::DeclarationStatement(std::string type, std::string name,
                                           ExpressionPtr value, int line,
                                           int column)
    : type_(std::move(type)), name_(std::move(name)),
      value_(std::move(value)), line_(line), column_(column) {}

void DeclarationStatement::execute(Environment& environment) const {
    environment.declare(name_, type_,
                        Environment::convertForType(
                            value_->evaluate(environment), type_, line_,
                            column_),
                        line_, column_);
}

AssignmentStatement::AssignmentStatement(std::string name, ExpressionPtr value,
                                         int line, int column)
    : name_(std::move(name)), value_(std::move(value)), line_(line),
      column_(column) {}

void AssignmentStatement::execute(Environment& environment) const {
    environment.assign(name_, value_->evaluate(environment), line_, column_);
}

LoopControlStatement::LoopControlStatement(LoopControlKind kind)
    : kind_(kind) {}

void LoopControlStatement::execute(Environment&) const {
    throw LoopControl(kind_);
}

ExpressionStatement::ExpressionStatement(ExpressionPtr expression)
    : expression_(std::move(expression)) {}

void ExpressionStatement::execute(Environment& environment) const {
    expression_->evaluate(environment);
}

void executeStatements(const StatementList& statements,
                       Environment& environment) {
    for (const auto& statement : statements) {
        statement->execute(environment);
    }
}

IfStatement::IfStatement(ExpressionPtr condition, StatementList thenStatements,
                         StatementList elseStatements, bool hasElse)
    : condition_(std::move(condition)),
      thenStatements_(std::move(thenStatements)),
      elseStatements_(std::move(elseStatements)), hasElse_(hasElse) {}

void IfStatement::execute(Environment& environment) const {
    if (isTruthy(condition_->evaluate(environment))) {
        executeStatements(thenStatements_, environment);
    } else if (hasElse_) {
        executeStatements(elseStatements_, environment);
    }
}

WhileStatement::WhileStatement(ExpressionPtr condition, StatementList statements)
    : condition_(std::move(condition)), statements_(std::move(statements)) {}

void WhileStatement::execute(Environment& environment) const {
    while (isTruthy(condition_->evaluate(environment))) {
        try {
            executeStatements(statements_, environment);
        } catch (const LoopControl& control) {
            if (control.kind == LoopControlKind::Break) {
                break;
            }
            // Continue/restart jumps to the next condition check.
        }
    }
}

ForStatement::ForStatement(StatementPtr initializer, ExpressionPtr condition,
                           StatementPtr update, StatementList statements)
    : initializer_(std::move(initializer)),
      condition_(std::move(condition)), update_(std::move(update)),
      statements_(std::move(statements)) {}

void ForStatement::execute(Environment& environment) const {
    initializer_->execute(environment);
    while (isTruthy(condition_->evaluate(environment))) {
        bool shouldBreak = false;
        try {
            executeStatements(statements_, environment);
        } catch (const LoopControl& control) {
            shouldBreak = control.kind == LoopControlKind::Break;
        }
        if (shouldBreak) {
            break;
        }
        update_->execute(environment);
    }
}

DoWhileStatement::DoWhileStatement(ExpressionPtr condition,
                                   StatementList statements)
    : condition_(std::move(condition)), statements_(std::move(statements)) {}

void DoWhileStatement::execute(Environment& environment) const {
    for (;;) {
        try {
            executeStatements(statements_, environment);
        } catch (const LoopControl& control) {
            if (control.kind == LoopControlKind::Break) {
                break;
            }
            // Continue/restart proceeds to the condition check.
        }
        if (condition_ != nullptr &&
            !isTruthy(condition_->evaluate(environment))) {
            break;
        }
    }
}

IterateStatement::IterateStatement(ExpressionPtr count, StatementList statements,
                                   int line, int column)
    : count_(std::move(count)), statements_(std::move(statements)),
      line_(line), column_(column) {}

void IterateStatement::execute(Environment& environment) const {
    const Value countValue = count_->evaluate(environment);
    if (!std::holds_alternative<std::int64_t>(countValue)) {
        throw SourceError("iterate() count must be an integer", line_, column_);
    }
    const auto count = std::get<std::int64_t>(countValue);
    for (std::int64_t index = 0; index < count; ++index) {
        try {
            executeStatements(statements_, environment);
        } catch (const LoopControl& control) {
            if (control.kind == LoopControlKind::Break) {
                break;
            }
            // Continue/restart proceeds to the next fixed iteration.
        }
    }
}

ForeverStatement::ForeverStatement(StatementList statements, int line,
                                   int column)
    : statements_(std::move(statements)), line_(line), column_(column) {}

void ForeverStatement::execute(Environment& environment) const {
    if (!warned_ && !environment.foreverWarningSuppressed() &&
        !statementsContainBreak(statements_)) {
        warned_ = true;
        const std::string& message = Config::instance().get(
            "warning.forever_no_break",
            "forever() has no break; it will run until the process is "
            "stopped. Add break; or call suppressForeverWarning() in "
            "global setup(){}.");
        std::cerr << "Warning: " << message << '\n';
    }
    for (;;) {
        bool shouldBreak = false;
        try {
            executeStatements(statements_, environment);
        } catch (const LoopControl& control) {
            shouldBreak = control.kind == LoopControlKind::Break;
        }
        if (shouldBreak) {
            return;
        }
        const double seconds = environment.foreverDelay();
        if (seconds > 0.0) {
            std::this_thread::sleep_for(
                std::chrono::duration<double>(seconds));
        }
    }
}

bool statementsContainBreak(const StatementList& statements) {
    for (const auto& statement : statements) {
        const LoopControlStatement* control =
            dynamic_cast<const LoopControlStatement*>(statement.get());
        if (control != nullptr) {
            return true;
        }
        if (const auto* ifStatement =
                dynamic_cast<const IfStatement*>(statement.get())) {
            if (statementsContainBreak(ifStatement->thenStatements()) ||
                statementsContainBreak(ifStatement->elseStatements())) {
                return true;
            }
        }
        if (const auto* whileStatement =
                dynamic_cast<const WhileStatement*>(statement.get())) {
            if (statementsContainBreak(whileStatement->statements())) {
                return true;
            }
        }
        if (const auto* forStatement =
                dynamic_cast<const ForStatement*>(statement.get())) {
            if (statementsContainBreak(forStatement->statements())) {
                return true;
            }
        }
        if (const auto* doWhileStatement =
                dynamic_cast<const DoWhileStatement*>(statement.get())) {
            if (statementsContainBreak(doWhileStatement->statements())) {
                return true;
            }
        }
        if (const auto* iterateStatement =
                dynamic_cast<const IterateStatement*>(statement.get())) {
            if (statementsContainBreak(iterateStatement->statements())) {
                return true;
            }
        }
        if (const auto* foreverStatement =
                dynamic_cast<const ForeverStatement*>(statement.get())) {
            if (statementsContainBreak(foreverStatement->statements())) {
                return true;
            }
        }
    }
    return false;
}

} // namespace clynxer
