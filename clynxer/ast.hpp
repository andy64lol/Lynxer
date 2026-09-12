#pragma once

#include "runtime.hpp"

#include <memory>
#include <vector>

namespace clynxer {

class Expression {
public:
    virtual ~Expression() = default;
    virtual Value evaluate(Environment& environment) const = 0;
};

using ExpressionPtr = std::unique_ptr<Expression>;

class LiteralExpression final : public Expression {
public:
    explicit LiteralExpression(Value value);

    Value evaluate(Environment& environment) const override;

private:
    Value value_;
};

class VariableExpression final : public Expression {
public:
    VariableExpression(std::string name, int line, int column);

    Value evaluate(Environment& environment) const override;

private:
    std::string name_;
    int line_;
    int column_;
};

class UnaryExpression final : public Expression {
public:
    UnaryExpression(std::string operation, ExpressionPtr operand, int line,
                    int column);

    Value evaluate(Environment& environment) const override;

private:
    std::string operation_;
    ExpressionPtr operand_;
    int line_;
    int column_;
};

class BinaryExpression final : public Expression {
public:
    BinaryExpression(std::string operation, ExpressionPtr left,
                     ExpressionPtr right, int line, int column);

    Value evaluate(Environment& environment) const override;

private:
    void requireNumbers(const Value& left, const Value& right) const;

    bool compareNumbers(double left, double right) const;

    bool compareStrings(const std::string& left, const std::string& right) const;

    std::string operation_;
    ExpressionPtr left_;
    ExpressionPtr right_;
    int line_;
    int column_;
};

class CallExpression final : public Expression {
public:
    CallExpression(std::string name, std::vector<ExpressionPtr> arguments,
                   int line, int column);

    Value evaluate(Environment& environment) const override;

private:
    std::string name_;
    std::vector<ExpressionPtr> arguments_;
    int line_;
    int column_;
};

class ListLiteralExpression final : public Expression {
public:
    explicit ListLiteralExpression(std::vector<ExpressionPtr> elements);

    Value evaluate(Environment& environment) const override;

private:
    std::vector<ExpressionPtr> elements_;
};

class TupleLiteralExpression final : public Expression {
public:
    explicit TupleLiteralExpression(std::vector<ExpressionPtr> elements);

    Value evaluate(Environment& environment) const override;

private:
    std::vector<ExpressionPtr> elements_;
};

// inter"..." — an interpolated string; literal parts alternate with
// expressions evaluated at runtime.
class InterpStringExpression final : public Expression {
public:
    void addLiteral(std::string text);

    void addExpression(ExpressionPtr expression);

    Value evaluate(Environment& environment) const override;

private:
    std::vector<std::string> literals_;
    std::vector<ExpressionPtr> expressions_;
};

class Statement {
public:
    virtual ~Statement() = default;
    virtual void execute(Environment& environment) const = 0;
};

using StatementPtr = std::unique_ptr<Statement>;
using StatementList = std::vector<StatementPtr>;

enum class LoopControlKind {
    Break,
    Continue,
};

struct LoopControl {
    explicit LoopControl(LoopControlKind kind) : kind(kind) {}

    LoopControlKind kind;
};

class DeclarationStatement final : public Statement {
public:
    DeclarationStatement(std::string type, std::string name, ExpressionPtr value,
                         int line, int column);

    void execute(Environment& environment) const override;

private:
    std::string type_;
    std::string name_;
    ExpressionPtr value_;
    int line_;
    int column_;
};

class AssignmentStatement final : public Statement {
public:
    AssignmentStatement(std::string name, ExpressionPtr value, int line,
                        int column);

    void execute(Environment& environment) const override;

private:
    std::string name_;
    ExpressionPtr value_;
    int line_;
    int column_;
};

class LoopControlStatement final : public Statement {
public:
    explicit LoopControlStatement(LoopControlKind kind);

    void execute(Environment& environment) const override;

private:
    LoopControlKind kind_;
};

class ExpressionStatement final : public Statement {
public:
    explicit ExpressionStatement(ExpressionPtr expression);

    void execute(Environment& environment) const override;

private:
    ExpressionPtr expression_;
};

void executeStatements(const StatementList& statements, Environment& environment);

class IfStatement final : public Statement {
public:
    IfStatement(ExpressionPtr condition, StatementList thenStatements,
                StatementList elseStatements, bool hasElse);

    void execute(Environment& environment) const override;

    const StatementList& thenStatements() const { return thenStatements_; }

    const StatementList& elseStatements() const { return elseStatements_; }

private:
    ExpressionPtr condition_;
    StatementList thenStatements_;
    StatementList elseStatements_;
    bool hasElse_;
};

class WhileStatement final : public Statement {
public:
    WhileStatement(ExpressionPtr condition, StatementList statements);

    void execute(Environment& environment) const override;

    const StatementList& statements() const { return statements_; }

private:
    ExpressionPtr condition_;
    StatementList statements_;
};

class ForStatement final : public Statement {
public:
    ForStatement(StatementPtr initializer, ExpressionPtr condition,
                 StatementPtr update, StatementList statements);

    void execute(Environment& environment) const override;

    const StatementList& statements() const { return statements_; }

private:
    StatementPtr initializer_;
    ExpressionPtr condition_;
    StatementPtr update_;
    StatementList statements_;
};

class DoWhileStatement final : public Statement {
public:
    DoWhileStatement(ExpressionPtr condition, StatementList statements);

    void execute(Environment& environment) const override;

    const StatementList& statements() const { return statements_; }

private:
    ExpressionPtr condition_;
    StatementList statements_;
};

class IterateStatement final : public Statement {
public:
    IterateStatement(ExpressionPtr count, StatementList statements, int line,
                     int column);

    void execute(Environment& environment) const override;

    const StatementList& statements() const { return statements_; }

private:
    ExpressionPtr count_;
    StatementList statements_;
    int line_;
    int column_;
};

class ForeverStatement final : public Statement {
public:
    ForeverStatement(StatementList statements, int line, int column);

    void execute(Environment& environment) const override;

    const StatementList& statements() const { return statements_; }

private:
    bool containsBreak(const StatementList& statements) const;

    StatementList statements_;
    int line_;
    int column_;
    mutable bool warned_ = false;
};

struct Function {
    StatementList statements;
};

} // namespace clynxer
