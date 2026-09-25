#pragma once

#include "runtime.hpp"

#include <iosfwd>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace lynxer {

// Counters for the AST optimization pass (defined in optimizer.hpp). Only a
// forward declaration is needed here; the pass itself lives in optimizer.cpp
// and each node's transformation is implemented in ast.cpp.
struct OptimizationStats;

class Statement;
using StatementPtr = std::unique_ptr<Statement>;
using StatementList = std::vector<StatementPtr>;

class Expression;
using ExpressionPtr = std::unique_ptr<Expression>;

class Expression {
public:
    virtual ~Expression() = default;
    virtual Value evaluate(Environment& environment) const = 0;

    // AST optimization hook. Recurses into owned children and returns a
    // replacement node, or nullptr to keep this node as it is.
    virtual ExpressionPtr optimize(OptimizationStats& stats);

    // Prints this node as an indented, position-free tree; see `dumpProgram`.
    virtual void dump(std::ostream& out, int indent) const = 0;
};

class LiteralExpression final : public Expression {
public:
    void dump(std::ostream& out, int indent) const override;
    explicit LiteralExpression(Value value);

    Value evaluate(Environment& environment) const override;


    const Value& value() const { return value_; }

private:
    Value value_;
};

class VariableExpression final : public Expression {
public:
    void dump(std::ostream& out, int indent) const override;
    VariableExpression(std::string name, int line, int column);

    Value evaluate(Environment& environment) const override;


    const std::string& name() const { return name_; }

private:
    int line() const { return line_; }

    int column() const { return column_; }

    std::string name_;
    int line_;
    int column_;
};

class UnaryExpression final : public Expression {
public:
    void dump(std::ostream& out, int indent) const override;
    UnaryExpression(std::string operation, ExpressionPtr operand, int line,
                    int column);

    Value evaluate(Environment& environment) const override;

    ExpressionPtr optimize(OptimizationStats& stats) override;

    const Expression& operandExpr() const { return *operand_; }

private:
    int line() const { return line_; }

    int column() const { return column_; }

    std::string operation_;
    ExpressionPtr operand_;
    int line_;
    int column_;
    // The deprecated-spelling warning is emitted once per source location, not
    // once per evaluation, so a loop body cannot flood stderr.
    mutable bool warned_ = false;
};

// Lynxer executes async functions cooperatively on the interpreter thread.
// Await is therefore an explicit expression node even though evaluating it
// currently resumes the already-synchronous operation immediately.
class AwaitExpression final : public Expression {
public:
    void dump(std::ostream& out, int indent) const override;
    explicit AwaitExpression(ExpressionPtr expression)
        : expression_(std::move(expression)) {}

    Value evaluate(Environment& environment) const override {
        return expression_->evaluate(environment);
    }

    ExpressionPtr optimize(OptimizationStats& stats) override;

private:
    ExpressionPtr expression_;
};

class BinaryExpression final : public Expression {
public:
    void dump(std::ostream& out, int indent) const override;
    BinaryExpression(std::string operation, ExpressionPtr left,
                     ExpressionPtr right, int line, int column);

    Value evaluate(Environment& environment) const override;

    ExpressionPtr optimize(OptimizationStats& stats) override;

    const Expression& leftExpr() const { return *left_; }
    const Expression& rightExpr() const { return *right_; }

private:
    int line() const { return line_; }

    int column() const { return column_; }

    std::string operation_;
    ExpressionPtr left_;
    ExpressionPtr right_;
    int line_;
    int column_;
    // See UnaryExpression::warned_.
    mutable bool warned_ = false;
};

class CallExpression final : public Expression {
public:
    void dump(std::ostream& out, int indent) const override;
    CallExpression(std::string name, std::vector<ExpressionPtr> arguments,
                   int line, int column);

    Value evaluate(Environment& environment) const override;

    ExpressionPtr optimize(OptimizationStats& stats) override;

    const std::vector<ExpressionPtr>& arguments() const { return arguments_; }
    const std::string& name() const { return name_; }
    void addInlineCodeblock(StatementList body);
    void addNamedCodeblock(std::string name);

private:
    int line() const { return line_; }

    int column() const { return column_; }

    std::string name_;
    std::vector<ExpressionPtr> arguments_;
    struct CodeblockArgument {
        std::string name;
        StatementList body;
    };
    std::vector<CodeblockArgument> codeblocks_;
    int line_;
    int column_;
};

class ListLiteralExpression final : public Expression {
public:
    void dump(std::ostream& out, int indent) const override;
    explicit ListLiteralExpression(std::vector<ExpressionPtr> elements);

    Value evaluate(Environment& environment) const override;

    ExpressionPtr optimize(OptimizationStats& stats) override;

    const std::vector<ExpressionPtr>& elements() const { return elements_; }

private:
    std::vector<ExpressionPtr> elements_;
};

class TupleLiteralExpression final : public Expression {
public:
    void dump(std::ostream& out, int indent) const override;
    explicit TupleLiteralExpression(std::vector<ExpressionPtr> elements);

    Value evaluate(Environment& environment) const override;

    ExpressionPtr optimize(OptimizationStats& stats) override;

    const std::vector<ExpressionPtr>& elements() const { return elements_; }

private:
    std::vector<ExpressionPtr> elements_;
};

// Coerces a value to a declared type at evaluation time; used by typed
// element literals such as [int 1, int 2].
class TypeCoerceExpression final : public Expression {
public:
    void dump(std::ostream& out, int indent) const override;
    TypeCoerceExpression(ExpressionPtr inner, std::string type);

    Value evaluate(Environment& environment) const override;

    ExpressionPtr optimize(OptimizationStats& stats) override;

    const Expression& inner() const { return *inner_; }

    const std::string& type() const { return type_; }

private:
    ExpressionPtr inner_;
    std::string type_;
};

// Field access on records (vargroup/struct/class), enum payloads, and
// paren-less enum variant construction (status.Ready).
class DotAccessExpression final : public Expression {
public:
    void dump(std::ostream& out, int indent) const override;
    DotAccessExpression(ExpressionPtr object, std::string field, int line,
                        int column);

    Value evaluate(Environment& environment) const override;

    ExpressionPtr optimize(OptimizationStats& stats) override;

    int line() const { return line_; }

    int column() const { return column_; }
    const Expression& object() const { return *object_; }
    const std::string& fieldName() const { return field_; }

private:
    ExpressionPtr object_;
    std::string field_;
    int line_;
    int column_;
};

// Method calls on class instances (instance.method(...)), enum variant
// construction with payloads (status.Failed("x")), and global.-prefixed
// builtin calls.
class MethodCallExpression final : public Expression {
public:
    void dump(std::ostream& out, int indent) const override;
    MethodCallExpression(ExpressionPtr object, std::string method,
                         std::vector<ExpressionPtr> arguments, int line,
                         int column);

    Value evaluate(Environment& environment) const override;

    ExpressionPtr optimize(OptimizationStats& stats) override;

    int line() const { return line_; }

    int column() const { return column_; }
    const Expression& receiver() const { return *object_; }
    const std::string& methodName() const { return method_; }
    const std::vector<ExpressionPtr>& arguments() const { return arguments_; }

private:
    ExpressionPtr object_;
    std::string method_;
    std::vector<ExpressionPtr> arguments_;
    int line_;
    int column_;
};

// new StructName(...) / new ClassName(...) construction.
class NewExpression final : public Expression {
public:
    void dump(std::ostream& out, int indent) const override;
    NewExpression(std::string typeName, std::vector<ExpressionPtr> arguments,
                  int line, int column);

    Value evaluate(Environment& environment) const override;

    ExpressionPtr optimize(OptimizationStats& stats) override;

    int line() const { return line_; }

    int column() const { return column_; }
    const std::vector<ExpressionPtr>& arguments() const { return arguments_; }

private:
    std::string typeName_;
    std::vector<ExpressionPtr> arguments_;
    int line_;
    int column_;
};

// addVarGroup(player, str title = "Warrior") — vargroup field addition.
class AddVarGroupExpression final : public Expression {
public:
    void dump(std::ostream& out, int indent) const override;
    AddVarGroupExpression(ExpressionPtr target, std::string type,
                          std::string field, ExpressionPtr value, int line,
                          int column);

    Value evaluate(Environment& environment) const override;

    ExpressionPtr optimize(OptimizationStats& stats) override;

private:
    ExpressionPtr target_;
    std::string type_;
    std::string field_;
    ExpressionPtr value_;
    int line_;
    int column_;
};

// removeVarGroup(player, title) — vargroup field removal by name.
class RemoveVarGroupExpression final : public Expression {
public:
    void dump(std::ostream& out, int indent) const override;
    RemoveVarGroupExpression(ExpressionPtr target, std::string field, int line,
                             int column);

    Value evaluate(Environment& environment) const override;

    ExpressionPtr optimize(OptimizationStats& stats) override;

private:
    ExpressionPtr target_;
    std::string field_;
    int line_;
    int column_;
};

struct VarGroupFieldInit {
    std::string type;
    std::string name;
    ExpressionPtr value;
    bool constant = false;
};

class VarGroupLiteralExpression final : public Expression {
public:
    void dump(std::ostream& out, int indent) const override;
    explicit VarGroupLiteralExpression(std::vector<VarGroupFieldInit> fields);

    Value evaluate(Environment& environment) const override;

    ExpressionPtr optimize(OptimizationStats& stats) override;

private:
    std::vector<VarGroupFieldInit> fields_;
};

// inter"..." — an interpolated string; literal parts alternate with
// expressions evaluated at runtime.
class InterpStringExpression final : public Expression {
public:
    void dump(std::ostream& out, int indent) const override;
    void addLiteral(std::string text);

    void addExpression(ExpressionPtr expression);

    Value evaluate(Environment& environment) const override;

    ExpressionPtr optimize(OptimizationStats& stats) override;

    const std::vector<ExpressionPtr>& expressions() const { return expressions_; }

private:
    std::vector<std::string> literals_;
    std::vector<ExpressionPtr> expressions_;
};

class Statement {
public:
    virtual ~Statement() = default;
    virtual void execute(Environment& environment) const = 0;

    // AST optimization hooks. `optimizeChildren` recurses into owned children;
    // `rewrite` may replace this statement with zero or more statements in
    // `out` and returns true when it did (used for dead-branch elimination).
    virtual void optimizeChildren(OptimizationStats& stats);
    virtual bool rewrite(StatementList& out, OptimizationStats& stats);

    // Prints this node as an indented, position-free tree; see `dumpProgram`.
    virtual void dump(std::ostream& out, int indent) const = 0;
};


// True when any statement in the list (at any nesting depth) is a break.
bool statementsContainBreak(const StatementList& statements);

// Matches a switch-case pattern against a runtime value, recording identifier
// bindings in source order. Wildcard "_" matches without binding.
bool matchPattern(const Expression& pattern, const Value& value,
                  std::vector<std::pair<std::string, Value>>& bindings,
                  Environment& environment, int line, int column);

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
    void dump(std::ostream& out, int indent) const override;
    DeclarationStatement(std::string type, std::string name, ExpressionPtr value,
                         int line, int column, bool constant = false);

    void execute(Environment& environment) const override;

    void optimizeChildren(OptimizationStats& stats) override;

    const Expression& valueExpr() const { return *value_; }
    bool isConstant() const { return constant_; }

private:
    int line() const { return line_; }

    int column() const { return column_; }

    std::string type_;
    std::string name_;
    ExpressionPtr value_;
    int line_;
    int column_;
    bool constant_;
};

// `shared <type> name = source;` — the legacy shared-variable declaration. It
// declares `name` and makes it a mutable alias of the existing variable
// `source`: writes through either name update the same storage until
// `unshare(name)` (or `varEndBorrow(name)`) detaches it.
class SharedDeclarationStatement final : public Statement {
public:
    void dump(std::ostream& out, int indent) const override;
    SharedDeclarationStatement(std::string type, std::string name,
                               std::string source, int line, int column);

    void execute(Environment& environment) const override;

private:
    std::string type_;
    std::string name_;
    std::string source_;
    int line_;
    int column_;
};

class AssignmentStatement final : public Statement {
public:
    void dump(std::ostream& out, int indent) const override;
    AssignmentStatement(std::string name, ExpressionPtr value, int line,
                        int column);

    void execute(Environment& environment) const override;

    void optimizeChildren(OptimizationStats& stats) override;

    const std::string& name() const { return name_; }
    const Expression& valueExpr() const { return *value_; }

private:
    int line() const { return line_; }

    int column() const { return column_; }

    std::string name_;
    ExpressionPtr value_;
    int line_;
    int column_;
};

// Assignment to a record field through a dotted path, optionally with a
// declared type: int player.coins = 500; or player.health = 90;
class DotAssignmentStatement final : public Statement {
public:
    void dump(std::ostream& out, int indent) const override;
    DotAssignmentStatement(std::vector<std::string> path, std::string type,
                           ExpressionPtr value, int line, int column);

    void execute(Environment& environment) const override;

    void optimizeChildren(OptimizationStats& stats) override;

private:
    std::vector<std::string> path_;
    std::string type_;
    ExpressionPtr value_;
    int line_;
    int column_;
};

struct SwitchCase {
    ExpressionPtr pattern;  // null for default
    StatementList body;
};

class SwitchStatement final : public Statement {
public:
    void dump(std::ostream& out, int indent) const override;
    SwitchStatement(ExpressionPtr value, std::vector<SwitchCase> cases,
                    int line, int column);

    void execute(Environment& environment) const override;

    void optimizeChildren(OptimizationStats& stats) override;

private:
    ExpressionPtr value_;
    std::vector<SwitchCase> cases_;
    int line_;
    int column_;
};

class TryCatchStatement final : public Statement {
public:
    void dump(std::ostream& out, int indent) const override;
    TryCatchStatement(StatementList tryStatements, std::string catchName,
                      StatementList catchStatements, int line, int column);

    void execute(Environment& environment) const override;

    void optimizeChildren(OptimizationStats& stats) override;

    const StatementList& tryStatements() const { return tryStatements_; }
    const StatementList& catchStatements() const { return catchStatements_; }
    const std::string& catchName() const { return catchName_; }

private:
    StatementList tryStatements_;
    std::string catchName_;
    StatementList catchStatements_;
    int line_;
    int column_;
};

struct ReturnControl {
    Value value;
    bool hasValue = false;
};

class ReturnStatement final : public Statement {
public:
    void dump(std::ostream& out, int indent) const override;
    explicit ReturnStatement(ExpressionPtr value, int line, int column);

    void execute(Environment& environment) const override;

    void optimizeChildren(OptimizationStats& stats) override;

private:
    ExpressionPtr value_;
    int line_;
    int column_;
};

class CodeblockDeclarationStatement final : public Statement {
public:
    void dump(std::ostream& out, int indent) const override;
    CodeblockDeclarationStatement(
        std::string name,
        std::vector<std::pair<std::string, std::string>> params,
        StatementList body, int line, int column);

    void execute(Environment& environment) const override;

    void optimizeChildren(OptimizationStats& stats) override;

private:
    std::string name_;
    std::vector<std::pair<std::string, std::string>> params_;
    StatementList body_;
    int line_;
    int column_;
};

// exec(...){...} inline form and exec(...){{name}} named form.
class ExecStatement final : public Statement {
public:
    void dump(std::ostream& out, int indent) const override;
    ExecStatement(std::vector<ExpressionPtr> arguments, std::string blockName,
                  std::vector<std::pair<std::string, std::string>> params,
                  StatementList body, int line, int column);

    void execute(Environment& environment) const override;

    void optimizeChildren(OptimizationStats& stats) override;

private:
    void runBody(const StatementList& body,
                 std::vector<std::pair<std::string, Variable>>& saved,
                 Environment& environment) const;

    std::vector<ExpressionPtr> arguments_;  // named form
    std::string blockName_;                 // named form
    std::vector<std::pair<std::string, std::string>> params_;  // inline form
    StatementList body_;                    // inline form
    int line_;
    int column_;
};

struct Parameter {
    std::string type = "any";
    std::string name;
    ExpressionPtr defaultValue;
};

struct Function {
    std::string name;
    std::vector<Parameter> parameters;
    std::vector<std::string> codeblockParameters;
    std::string returnType = "any";
    bool isGlobal = false;
    bool isFileFunction = false;
    StatementList statements;
};

class FunctionDeclarationStatement final : public Statement {
public:
    void dump(std::ostream& out, int indent) const override;
    explicit FunctionDeclarationStatement(std::shared_ptr<Function> function)
        : function_(std::move(function)) {}

    void execute(Environment& environment) const override;

    void optimizeChildren(OptimizationStats& stats) override;

private:
    std::shared_ptr<Function> function_;
};

class LoopControlStatement final : public Statement {
public:
    void dump(std::ostream& out, int indent) const override;
    explicit LoopControlStatement(LoopControlKind kind);

    void execute(Environment& environment) const override;


private:
    LoopControlKind kind_;
};

class ExpressionStatement final : public Statement {
public:
    void dump(std::ostream& out, int indent) const override;
    explicit ExpressionStatement(ExpressionPtr expression);

    void execute(Environment& environment) const override;

    void optimizeChildren(OptimizationStats& stats) override;

    const Expression& expression() const { return *expression_; }

private:
    ExpressionPtr expression_;
};

class ImportStatement final : public Statement {
public:
    void dump(std::ostream& out, int indent) const override;
    ImportStatement(std::string path, std::string alias, int line, int column)
        : path_(std::move(path)), alias_(std::move(alias)), line_(line),
          column_(column) {}

    void execute(Environment& environment) const override;

private:
    std::string path_;
    std::string alias_;
    int line_;
    int column_;
};

void executeStatements(const StatementList& statements, Environment& environment);

class IfStatement final : public Statement {
public:
    void dump(std::ostream& out, int indent) const override;
    IfStatement(ExpressionPtr condition, StatementList thenStatements,
                StatementList elseStatements, bool hasElse);

    void execute(Environment& environment) const override;

    void optimizeChildren(OptimizationStats& stats) override;
    bool rewrite(StatementList& out, OptimizationStats& stats) override;

    const StatementList& thenStatements() const { return thenStatements_; }

    const StatementList& elseStatements() const { return elseStatements_; }
    const Expression& condition() const { return *condition_; }

private:
    ExpressionPtr condition_;
    StatementList thenStatements_;
    StatementList elseStatements_;
    bool hasElse_;
};

class WhileStatement final : public Statement {
public:
    void dump(std::ostream& out, int indent) const override;
    WhileStatement(ExpressionPtr condition, StatementList statements);

    void execute(Environment& environment) const override;

    void optimizeChildren(OptimizationStats& stats) override;
    bool rewrite(StatementList& out, OptimizationStats& stats) override;

    const StatementList& statements() const { return statements_; }
    const Expression& condition() const { return *condition_; }

private:
    ExpressionPtr condition_;
    StatementList statements_;
};

class ForStatement final : public Statement {
public:
    void dump(std::ostream& out, int indent) const override;
    ForStatement(StatementPtr initializer, ExpressionPtr condition,
                 StatementPtr update, StatementList statements);

    void execute(Environment& environment) const override;

    void optimizeChildren(OptimizationStats& stats) override;

    const StatementList& statements() const { return statements_; }

private:
    StatementPtr initializer_;
    ExpressionPtr condition_;
    StatementPtr update_;
    StatementList statements_;
};

class DoWhileStatement final : public Statement {
public:
    void dump(std::ostream& out, int indent) const override;
    DoWhileStatement(ExpressionPtr condition, StatementList statements);

    void execute(Environment& environment) const override;

    void optimizeChildren(OptimizationStats& stats) override;

    const StatementList& statements() const { return statements_; }

private:
    ExpressionPtr condition_;
    StatementList statements_;
};

class IterateStatement final : public Statement {
public:
    void dump(std::ostream& out, int indent) const override;
    IterateStatement(ExpressionPtr count, StatementList statements, int line,
                     int column);

    void execute(Environment& environment) const override;

    void optimizeChildren(OptimizationStats& stats) override;
    bool rewrite(StatementList& out, OptimizationStats& stats) override;

    const StatementList& statements() const { return statements_; }
    const Expression& count() const { return *count_; }

private:
    int line() const { return line_; }

    int column() const { return column_; }

    ExpressionPtr count_;
    StatementList statements_;
    int line_;
    int column_;
};

class ForeverStatement final : public Statement {
public:
    void dump(std::ostream& out, int indent) const override;
    ForeverStatement(StatementList statements, int line, int column);

    void execute(Environment& environment) const override;

    void optimizeChildren(OptimizationStats& stats) override;

    const StatementList& statements() const { return statements_; }

private:
    int line() const { return line_; }

    int column() const { return column_; }

    bool containsBreak(const StatementList& statements) const;

    StatementList statements_;
    int line_;
    int column_;
    mutable bool warned_ = false;
};

Value invokeFunction(const Function& function, const std::vector<Value>& args,
                     const std::vector<std::shared_ptr<CodeblockValue>>& blocks,
                     Environment& environment, int line, int column);

void executeProgram(const std::unordered_map<std::string, Function>& functions,
                    Environment& environment);

// Prints the parsed program as the position-free tree used by `--ast`.
// `functions` is the order they were declared in (see Parser::programOrder).
void dumpProgram(std::ostream& out, const std::vector<const Function*>& functions);

// One `import`/`importAs` occurrence found while parsing.
struct ImportRecord {
    std::string path;
    std::string alias;
    int line = 0;
    int column = 0;
};

// Lexes and parses `source`, returning every import it declares in source order.
std::vector<ImportRecord> collectImports(const std::string& source,
                                         const std::string& display);

// Resolves a module reference the way the interpreter does, starting from
// `sourceDirectory`. Embedded modules take priority; returns "" when nothing
// matches.
std::string resolveModulePath(const std::string& sourceDirectory,
                              const std::string& requested);

// The interpreter evaluates Lynxer code on one thread at a time. A built-in
// that blocks on another thread holding this lock (see `nativeThreadJoin`)
// releases it while it waits.
void lockInterpreter();
void unlockInterpreter();

// Calls an operation on a bundled native module that a built-in family is
// implemented behind — `sound*` uses the `sound` module. The module is loaded
// on first use, so a program that never touches the family never needs it.
Value callBridgedModule(const std::string& module, const std::string& operation,
                        const std::vector<Value>& args, int line, int column);

// Calls a native function using the Lynxer signature grammar. The FFI
// built-ins reuse the same checked dispatcher as imported native modules.
Value callNative(void* address, const std::string& signature,
                 const std::vector<Value>& args, int line, int column);

// Installs the module sources and native library paths carried by a compiled
// executable.
void setEmbeddedModuleSources(std::map<std::string, std::string> sources);
void setEmbeddedModuleLibraries(std::map<std::string, std::string> libraries);

} // namespace lynxer
