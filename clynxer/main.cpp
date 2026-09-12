#include <cctype>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace clynxer {

struct SourceError : std::runtime_error {
    SourceError(std::string message, int line, int column)
        : std::runtime_error(std::move(message)), line(line), column(column) {}

    int line;
    int column;
};

enum class TokenKind {
    End,
    Identifier,
    Number,
    String,
    Symbol,
};

struct Token {
    TokenKind kind;
    std::string text;
    int line;
    int column;
};

class Lexer {
public:
    Lexer(const std::string& source, std::string filename)
        : source_(source), filename_(std::move(filename)) {}

    std::vector<Token> scan() {
        std::vector<Token> tokens;
        while (!atEnd()) {
            skipWhitespaceAndComments();
            if (atEnd()) {
                break;
            }

            const int line = line_;
            const int column = column_;
            const char current = advance();

            if (isIdentifierStart(current)) {
                std::string text(1, current);
                while (!atEnd() && isIdentifierPart(peek())) {
                    text += advance();
                }
                tokens.push_back({TokenKind::Identifier, text, line, column});
            } else if (std::isdigit(static_cast<unsigned char>(current)) ||
                       (current == '.' &&
                        std::isdigit(static_cast<unsigned char>(peek())))) {
                std::string text(1, current);
                bool hasDot = current == '.';
                while (!atEnd()) {
                    const char next = peek();
                    if (std::isdigit(static_cast<unsigned char>(next))) {
                        text += advance();
                    } else if (next == '.' && !hasDot) {
                        hasDot = true;
                        text += advance();
                    } else {
                        break;
                    }
                }
                tokens.push_back({TokenKind::Number, text, line, column});
            } else if (current == '"') {
                tokens.push_back(
                    {TokenKind::String, readString(line, column), line, column});
            } else {
                std::string symbol(1, current);
                const char next = peek();
                if ((current == '=' && next == '=') ||
                    (current == '!' && next == '=') ||
                    (current == '<' && next == '=') ||
                    (current == '>' && next == '=') ||
                    (current == '&' && next == '&') ||
                    (current == '|' && next == '|') ||
                    (current == '+' && next == '=') ||
                    (current == '-' && next == '=') ||
                    (current == '*' && next == '=') ||
                    (current == '/' && next == '=') ||
                    (current == '%' && next == '=')) {
                    symbol += advance();
                } else if (std::string("+-*/%<>=!;(),{}").find(current) ==
                           std::string::npos) {
                    fail("unexpected character '" + std::string(1, current),
                         line, column);
                }
                tokens.push_back({TokenKind::Symbol, symbol, line, column});
            }
        }
        tokens.push_back({TokenKind::End, "", line_, column_});
        return tokens;
    }

private:
    bool atEnd() const { return index_ >= source_.size(); }

    char peek() const { return atEnd() ? '\0' : source_[index_]; }

    char advance() {
        const char value = source_[index_++];
        if (value == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        return value;
    }

    void skipWhitespaceAndComments() {
        for (;;) {
            while (!atEnd() &&
                   std::isspace(static_cast<unsigned char>(peek()))) {
                advance();
            }
            if (startsWith("///")) {
                skipDelimitedComment();
                continue;
            }
            if (peek() != '/' || index_ + 1 >= source_.size() ||
                source_[index_ + 1] != '/') {
                return;
            }
            while (!atEnd() && advance() != '\n') {
            }
        }
    }

    bool startsWith(const std::string& text) const {
        return source_.compare(index_, text.size(), text) == 0;
    }

    void skipDelimitedComment() {
        const int startLine = line_;
        const int startColumn = column_;
        advance();
        advance();
        advance();

        while (!atEnd()) {
            if (startsWith("///")) {
                advance();
                advance();
                advance();
                return;
            }
            advance();
        }

        fail("unterminated multiline comment; expected closing '///'",
             startLine, startColumn);
    }

    std::string readString(int line, int column) {
        std::string value;
        while (!atEnd() && peek() != '"') {
            if (peek() == '\n') {
                fail("unterminated string", line, column);
            }
            char current = advance();
            if (current == '\\') {
                if (atEnd()) {
                    fail("unterminated string escape", line, column);
                }
                const char escaped = advance();
                switch (escaped) {
                case 'n':
                    value += '\n';
                    break;
                case 'r':
                    value += '\r';
                    break;
                case 't':
                    value += '\t';
                    break;
                case '\\':
                    value += '\\';
                    break;
                case '"':
                    value += '"';
                    break;
                default:
                    fail("unknown string escape \\" +
                             std::string(1, escaped),
                         line, column);
                }
            } else {
                value += current;
            }
        }
        if (atEnd()) {
            fail("unterminated string", line, column);
        }
        advance();
        return value;
    }

    static bool isIdentifierStart(char value) {
        return std::isalpha(static_cast<unsigned char>(value)) || value == '_';
    }

    static bool isIdentifierPart(char value) {
        return std::isalnum(static_cast<unsigned char>(value)) || value == '_';
    }

    [[noreturn]] void fail(const std::string& message, int line, int column) const {
        throw SourceError(message, line, column);
    }

    const std::string& source_;
    std::string filename_;
    std::size_t index_ = 0;
    int line_ = 1;
    int column_ = 1;
};

using Value = std::variant<std::monostate, std::int64_t, double, bool, std::string>;

struct Variable {
    std::string type;
    Value value;
};

class Environment {
public:
    void declare(const std::string& name, const std::string& type, Value value,
                 int line, int column) {
        if (variables_.find(name) != variables_.end()) {
            fail("variable '" + name + "' is already declared", line, column);
        }
        variables_.emplace(name, Variable{type, std::move(value)});
    }

    void assign(const std::string& name, Value value, int line, int column) {
        auto found = variables_.find(name);
        if (found == variables_.end()) {
            fail("unknown variable '" + name + "'", line, column);
        }
        found->second.value =
            convertForType(std::move(value), found->second.type, line, column);
    }

    const Value& get(const std::string& name, int line, int column) const {
        auto found = variables_.find(name);
        if (found == variables_.end()) {
            fail("unknown variable '" + name + "'", line, column);
        }
        return found->second.value;
    }

    void setForeverDelay(double seconds) { foreverDelaySeconds_ = seconds; }

    double foreverDelay() const { return foreverDelaySeconds_; }

    static Value convertForType(Value value, const std::string& type, int line,
                                int column) {
        if (type == "any") {
            return value;
        }
        if (type == "int") {
            if (std::holds_alternative<std::int64_t>(value)) {
                return value;
            }
            if (const auto* number = std::get_if<double>(&value);
                number != nullptr && *number == static_cast<std::int64_t>(*number)) {
                return static_cast<std::int64_t>(*number);
            }
        } else if (type == "float") {
            if (std::holds_alternative<std::int64_t>(value)) {
                return static_cast<double>(std::get<std::int64_t>(value));
            }
            if (std::holds_alternative<double>(value)) {
                return value;
            }
        } else if (type == "str" && std::holds_alternative<std::string>(value)) {
            return value;
        } else if (type == "bool" && std::holds_alternative<bool>(value)) {
            return value;
        }
        fail("value cannot be assigned to type '" + type + "'", line, column);
    }

private:
    [[noreturn]] static void fail(const std::string& message, int line,
                                  int column) {
        throw SourceError(message, line, column);
    }

    std::unordered_map<std::string, Variable> variables_;
    double foreverDelaySeconds_ = 0.02;
};

static bool isTruthy(const Value& value) {
    if (std::holds_alternative<std::monostate>(value)) {
        return false;
    }
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return *integer != 0;
    }
    if (const auto* number = std::get_if<double>(&value)) {
        return *number != 0.0;
    }
    if (const auto* boolean = std::get_if<bool>(&value)) {
        return *boolean;
    }
    return !std::get<std::string>(value).empty();
}

static std::string valueToString(const Value& value) {
    if (std::holds_alternative<std::monostate>(value)) {
        return "none";
    }
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return std::to_string(*integer);
    }
    if (const auto* number = std::get_if<double>(&value)) {
        std::ostringstream output;
        output << std::setprecision(15) << *number;
        return output.str();
    }
    if (const auto* boolean = std::get_if<bool>(&value)) {
        return *boolean ? "true" : "false";
    }
    return std::get<std::string>(value);
}

static bool isNumber(const Value& value) {
    return std::holds_alternative<std::int64_t>(value) ||
           std::holds_alternative<double>(value);
}

static double asNumber(const Value& value, int line, int column) {
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return static_cast<double>(*integer);
    }
    if (const auto* number = std::get_if<double>(&value)) {
        return *number;
    }
    throw SourceError("numeric value required", line, column);
}

class Expression {
public:
    virtual ~Expression() = default;
    virtual Value evaluate(Environment& environment) const = 0;
};

using ExpressionPtr = std::unique_ptr<Expression>;

class LiteralExpression final : public Expression {
public:
    explicit LiteralExpression(Value value) : value_(std::move(value)) {}

    Value evaluate(Environment&) const override { return value_; }

private:
    Value value_;
};

class VariableExpression final : public Expression {
public:
    VariableExpression(std::string name, int line, int column)
        : name_(std::move(name)), line_(line), column_(column) {}

    Value evaluate(Environment& environment) const override {
        return environment.get(name_, line_, column_);
    }

private:
    std::string name_;
    int line_;
    int column_;
};

class UnaryExpression final : public Expression {
public:
    UnaryExpression(std::string operation, ExpressionPtr operand, int line,
                    int column)
        : operation_(std::move(operation)), operand_(std::move(operand)),
          line_(line), column_(column) {}

    Value evaluate(Environment& environment) const override {
        const Value value = operand_->evaluate(environment);
        if (operation_ == "!") {
            return !isTruthy(value);
        }
        if (operation_ == "-") {
            if (!isNumber(value)) {
                throw SourceError("unary '-' requires a number", line_, column_);
            }
            if (std::holds_alternative<std::int64_t>(value)) {
                return -std::get<std::int64_t>(value);
            }
            return -std::get<double>(value);
        }
        throw SourceError("unsupported unary operator '" + operation_ + "'",
                          line_, column_);
    }

private:
    std::string operation_;
    ExpressionPtr operand_;
    int line_;
    int column_;
};

class BinaryExpression final : public Expression {
public:
    BinaryExpression(std::string operation, ExpressionPtr left,
                     ExpressionPtr right, int line, int column)
        : operation_(std::move(operation)), left_(std::move(left)),
          right_(std::move(right)), line_(line), column_(column) {}

    Value evaluate(Environment& environment) const override {
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
        if (operation_ == "+") {
            if (std::holds_alternative<std::string>(left) ||
                std::holds_alternative<std::string>(right)) {
                return valueToString(left) + valueToString(right);
            }
            requireNumbers(left, right);
            if (std::holds_alternative<std::int64_t>(left) &&
                std::holds_alternative<std::int64_t>(right)) {
                return std::get<std::int64_t>(left) +
                       std::get<std::int64_t>(right);
            }
            return asNumber(left, line_, column_) + asNumber(right, line_, column_);
        }
        if (operation_ == "-" || operation_ == "*" || operation_ == "/") {
            requireNumbers(left, right);
            if (operation_ == "/" && asNumber(right, line_, column_) == 0.0) {
                throw SourceError("division by zero", line_, column_);
            }
            if (std::holds_alternative<std::int64_t>(left) &&
                std::holds_alternative<std::int64_t>(right) && operation_ != "/") {
                const auto lhs = std::get<std::int64_t>(left);
                const auto rhs = std::get<std::int64_t>(right);
                return operation_ == "-" ? lhs - rhs : lhs * rhs;
            }
            const double lhs = asNumber(left, line_, column_);
            const double rhs = asNumber(right, line_, column_);
            if (operation_ == "-") {
                return lhs - rhs;
            }
            if (operation_ == "*") {
                return lhs * rhs;
            }
            return lhs / rhs;
        }
        if (operation_ == "%") {
            if (!std::holds_alternative<std::int64_t>(left) ||
                !std::holds_alternative<std::int64_t>(right)) {
                throw SourceError("'%' requires integer operands", line_, column_);
            }
            const auto rhs = std::get<std::int64_t>(right);
            if (rhs == 0) {
                throw SourceError("division by zero", line_, column_);
            }
            return std::get<std::int64_t>(left) % rhs;
        }
        if (operation_ == "==" || operation_ == "!=") {
            bool equal = false;
            if (isNumber(left) && isNumber(right)) {
                equal = asNumber(left, line_, column_) ==
                        asNumber(right, line_, column_);
            } else {
                equal = left == right;
            }
            return operation_ == "==" ? equal : !equal;
        }
        if (operation_ == "<" || operation_ == "<=" || operation_ == ">" ||
            operation_ == ">=") {
            if (std::holds_alternative<std::string>(left) &&
                std::holds_alternative<std::string>(right)) {
                return compareStrings(std::get<std::string>(left),
                                      std::get<std::string>(right));
            }
            requireNumbers(left, right);
            return compareNumbers(asNumber(left, line_, column_),
                                  asNumber(right, line_, column_));
        }
        throw SourceError("unsupported binary operator '" + operation_ + "'",
                          line_, column_);
    }

private:
    void requireNumbers(const Value& left, const Value& right) const {
        if (!isNumber(left) || !isNumber(right)) {
            throw SourceError("numeric operands required for '" + operation_ + "'",
                              line_, column_);
        }
    }

    bool compareNumbers(double left, double right) const {
        if (operation_ == "<") {
            return left < right;
        }
        if (operation_ == "<=") {
            return left <= right;
        }
        if (operation_ == ">") {
            return left > right;
        }
        return left >= right;
    }

    bool compareStrings(const std::string& left,
                        const std::string& right) const {
        if (operation_ == "<") {
            return left < right;
        }
        if (operation_ == "<=") {
            return left <= right;
        }
        if (operation_ == ">") {
            return left > right;
        }
        return left >= right;
    }

    std::string operation_;
    ExpressionPtr left_;
    ExpressionPtr right_;
    int line_;
    int column_;
};

class Statement {
public:
    virtual ~Statement() = default;
    virtual void execute(Environment& environment) const = 0;
};

using StatementPtr = std::unique_ptr<Statement>;

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
                         int line, int column)
        : type_(std::move(type)), name_(std::move(name)),
          value_(std::move(value)), line_(line), column_(column) {}

    void execute(Environment& environment) const override {
        environment.declare(name_, type_,
                            Environment::convertForType(
                                value_->evaluate(environment), type_, line_,
                                column_),
                            line_, column_);
    }

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
                        int column)
        : name_(std::move(name)), value_(std::move(value)), line_(line),
          column_(column) {}

    void execute(Environment& environment) const override {
        environment.assign(name_, value_->evaluate(environment), line_, column_);
    }

private:
    std::string name_;
    ExpressionPtr value_;
    int line_;
    int column_;
};

class PrintStatement final : public Statement {
public:
    PrintStatement(ExpressionPtr value, bool newline)
        : value_(std::move(value)), newline_(newline) {}

    void execute(Environment& environment) const override {
        std::cout << valueToString(value_->evaluate(environment));
        if (newline_) {
            std::cout << '\n';
        }
    }

private:
    ExpressionPtr value_;
    bool newline_;
};

class ForeverDelayStatement final : public Statement {
public:
    ForeverDelayStatement(ExpressionPtr value, int line, int column)
        : value_(std::move(value)), line_(line), column_(column) {}

    void execute(Environment& environment) const override {
        const Value value = value_->evaluate(environment);
        if (!isNumber(value)) {
            throw SourceError("foreverDelay() requires a number", line_, column_);
        }
        const double seconds = asNumber(value, line_, column_);
        if (seconds < 0.0) {
            throw SourceError("foreverDelay() requires a non-negative number",
                              line_, column_);
        }
        environment.setForeverDelay(seconds);
    }

private:
    ExpressionPtr value_;
    int line_;
    int column_;
};

class LoopControlStatement final : public Statement {
public:
    explicit LoopControlStatement(LoopControlKind kind) : kind_(kind) {}

    void execute(Environment&) const override { throw LoopControl(kind_); }

private:
    LoopControlKind kind_;
};

using StatementList = std::vector<StatementPtr>;

static void executeStatements(const StatementList& statements,
                              Environment& environment);

class IfStatement final : public Statement {
public:
    IfStatement(ExpressionPtr condition, StatementList thenStatements,
                StatementList elseStatements, bool hasElse)
        : condition_(std::move(condition)),
          thenStatements_(std::move(thenStatements)),
          elseStatements_(std::move(elseStatements)), hasElse_(hasElse) {}

    void execute(Environment& environment) const override {
        if (isTruthy(condition_->evaluate(environment))) {
            executeStatements(thenStatements_, environment);
        } else if (hasElse_) {
            executeStatements(elseStatements_, environment);
        }
    }

private:
    ExpressionPtr condition_;
    StatementList thenStatements_;
    StatementList elseStatements_;
    bool hasElse_;
};

class WhileStatement final : public Statement {
public:
    WhileStatement(ExpressionPtr condition, StatementList statements)
        : condition_(std::move(condition)), statements_(std::move(statements)) {}

    void execute(Environment& environment) const override {
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

private:
    ExpressionPtr condition_;
    StatementList statements_;
};

class ForStatement final : public Statement {
public:
    ForStatement(StatementPtr initializer, ExpressionPtr condition,
                 StatementPtr update, StatementList statements)
        : initializer_(std::move(initializer)),
          condition_(std::move(condition)), update_(std::move(update)),
          statements_(std::move(statements)) {}

    void execute(Environment& environment) const override {
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

private:
    StatementPtr initializer_;
    ExpressionPtr condition_;
    StatementPtr update_;
    StatementList statements_;
};

class DoWhileStatement final : public Statement {
public:
    DoWhileStatement(ExpressionPtr condition, StatementList statements)
        : condition_(std::move(condition)), statements_(std::move(statements)) {}

    void execute(Environment& environment) const override {
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

private:
    ExpressionPtr condition_;
    StatementList statements_;
};

class IterateStatement final : public Statement {
public:
    IterateStatement(ExpressionPtr count, StatementList statements, int line,
                     int column)
        : count_(std::move(count)), statements_(std::move(statements)),
          line_(line), column_(column) {}

    void execute(Environment& environment) const override {
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

private:
    ExpressionPtr count_;
    StatementList statements_;
    int line_;
    int column_;
};

class ForeverStatement final : public Statement {
public:
    ForeverStatement(StatementList statements, int line, int column)
        : statements_(std::move(statements)), line_(line), column_(column) {}

    void execute(Environment& environment) const override {
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

private:
    StatementList statements_;
    int line_;
    int column_;
};

struct Function {
    StatementList statements;
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    std::unordered_map<std::string, Function> parseProgram() {
        std::unordered_map<std::string, Function> functions;
        while (!check(TokenKind::End)) {
            expectText("global", "expected 'global' function declaration");
            const Token name = expect(TokenKind::Identifier, "expected function name");
            if (name.text != "setup" && name.text != "main") {
                fail("only global setup() and global main() are supported", name);
            }
            expectText("(", "expected '(' after function name");
            expectText(")", "clynxer functions do not take parameters");
            Function function;
            function.statements = parseBlock("function body");
            if (functions.find(name.text) != functions.end()) {
                fail("duplicate function '" + name.text + "'", name);
            }
            functions.emplace(name.text, std::move(function));
        }
        if (functions.find("setup") == functions.end()) {
            fail("program must define global setup()", current());
        }
        if (functions.find("main") == functions.end()) {
            fail("program must define global main()", current());
        }
        return functions;
    }

private:
    StatementPtr parseStatement() {
        if (checkText("if")) {
            return parseIf();
        }
        if (checkText("while")) {
            return parseWhile();
        }
        if (checkText("for")) {
            return parseFor();
        }
        if (checkText("doWhile")) {
            return parseDoWhile();
        }
        if (checkText("iterate")) {
            return parseIterate();
        }
        if (checkText("forever")) {
            return parseForever();
        }
        if (checkText("break") || checkText("continue") ||
            checkText("restart")) {
            return parseLoopControl();
        }
        return parseSimpleStatement(true);
    }

    StatementPtr parseSimpleStatement(bool requireSemicolon) {
        if (checkText("int") || checkText("float") || checkText("str") ||
            checkText("bool") || checkText("any")) {
            const Token type = advance();
            const Token name = expect(TokenKind::Identifier, "expected variable name");
            expectText("=", "expected '=' in declaration");
            ExpressionPtr value = parseExpression();
            if (requireSemicolon) {
                expectText(";", "expected ';' after declaration");
            }
            return std::make_unique<DeclarationStatement>(
                type.text, name.text, std::move(value), type.line, type.column);
        }

        if (checkText("print") || checkText("println")) {
            const bool newline = current().text == "println";
            advance();
            expectText("(", "expected '(' after print function");
            ExpressionPtr value = parseExpression();
            expectText(")", "expected ')' after print argument");
            if (requireSemicolon) {
                expectText(";", "expected ';' after print call");
            }
            return std::make_unique<PrintStatement>(std::move(value), newline);
        }

        if (checkText("foreverDelay")) {
            const Token call = advance();
            expectText("(", "expected '(' after foreverDelay");
            ExpressionPtr value = parseExpression();
            expectText(")", "expected ')' after foreverDelay argument");
            if (requireSemicolon) {
                expectText(";", "expected ';' after foreverDelay call");
            }
            return std::make_unique<ForeverDelayStatement>(
                std::move(value), call.line, call.column);
        }

        const Token name = expect(TokenKind::Identifier,
                                  "expected a declaration, assignment, or print call");
        const Token operation = expectAssignmentOperator();
        ExpressionPtr value = parseExpression();
        if (requireSemicolon) {
            expectText(";", "expected ';' after assignment");
        }
        if (operation.text != "=") {
            const std::string binaryOperation(1, operation.text[0]);
            value = std::make_unique<BinaryExpression>(
                binaryOperation,
                std::make_unique<VariableExpression>(name.text, name.line,
                                                      name.column),
                std::move(value), operation.line, operation.column);
        }
        return std::make_unique<AssignmentStatement>(
            name.text, std::move(value), name.line, name.column);
    }

    StatementPtr parseIf() {
        expectText("if", "expected 'if'");
        expectText("(", "expected '(' after 'if'");
        ExpressionPtr condition = parseExpression();
        expectText(")", "expected ')' after condition");
        StatementList thenStatements = parseBlock("if body");

        StatementList elseStatements;
        bool hasElse = false;
        if (checkText("else")) {
            advance();
            hasElse = true;
            if (checkText("if")) {
                elseStatements.push_back(parseIf());
            } else {
                elseStatements = parseBlock("else body");
            }
        }
        return std::make_unique<IfStatement>(
            std::move(condition), std::move(thenStatements),
            std::move(elseStatements), hasElse);
    }

    StatementPtr parseWhile() {
        expectText("while", "expected 'while'");
        expectText("(", "expected '(' after 'while'");
        ExpressionPtr condition = parseExpression();
        expectText(")", "expected ')' after condition");
        return std::make_unique<WhileStatement>(
            std::move(condition), parseLoopBlock("while body"));
    }

    StatementPtr parseFor() {
        expectText("for", "expected 'for'");
        expectText("(", "expected '(' after 'for'");
        const Token initStart = current();
        std::string initName;
        if (checkText("int") || checkText("float") || checkText("str") ||
            checkText("bool") || checkText("any")) {
            if (index_ + 1 < tokens_.size()) {
                initName = tokens_[index_ + 1].text;
            }
        } else if (check(TokenKind::Identifier)) {
            initName = current().text;
        }
        if (initName.empty()) {
            fail("expected for-loop initializer", initStart);
        }
        StatementPtr initializer = parseSimpleStatement(true);
        ExpressionPtr condition = parseExpression();
        StatementPtr update;
        if (checkText(";")) {
            advance();
            if (!checkText(")")) {
                update = parseSimpleStatement(false);
            }
        } else if (!checkText(")")) {
            fail("expected ';' or ')' after for condition", current());
        }
        if (update == nullptr) {
            update = std::make_unique<AssignmentStatement>(
                initName,
                std::make_unique<BinaryExpression>(
                    "+",
                    std::make_unique<VariableExpression>(
                        initName, initStart.line, initStart.column),
                    std::make_unique<LiteralExpression>(
                        static_cast<std::int64_t>(1)),
                    initStart.line, initStart.column),
                initStart.line, initStart.column);
        }
        expectText(")", "expected ')' after for clauses");
        return std::make_unique<ForStatement>(
            std::move(initializer), std::move(condition), std::move(update),
            parseLoopBlock("for body"));
    }

    StatementPtr parseDoWhile() {
        const Token loop = expectText("doWhile", "expected 'doWhile'");
        expectText("(", "expected '(' after 'doWhile'");
        ExpressionPtr condition;
        if (!checkText(")")) {
            condition = parseExpression();
        }
        expectText(")", "expected ')' after doWhile condition");
        return std::make_unique<DoWhileStatement>(
            std::move(condition), parseLoopBlock("doWhile body"));
    }

    StatementPtr parseIterate() {
        const Token loop = expectText("iterate", "expected 'iterate'");
        expectText("(", "expected '(' after 'iterate'");
        ExpressionPtr count = parseExpression();
        expectText(")", "expected ')' after iterate count");
        return std::make_unique<IterateStatement>(
            std::move(count), parseLoopBlock("iterate body"), loop.line,
            loop.column);
    }

    StatementPtr parseForever() {
        const Token loop = expectText("forever", "expected 'forever'");
        expectText("(", "expected '(' after 'forever'");
        expectText(")", "forever() does not accept arguments");
        return std::make_unique<ForeverStatement>(
            parseLoopBlock("forever body"), loop.line, loop.column);
    }

    StatementPtr parseLoopControl() {
        const Token control = advance();
        if (loopDepth_ == 0) {
            fail("'" + control.text + "' is only valid inside a loop", control);
        }
        expectText(";", "expected ';' after '" + control.text + "'");
        const LoopControlKind kind =
            control.text == "break" ? LoopControlKind::Break
                                     : LoopControlKind::Continue;
        return std::make_unique<LoopControlStatement>(kind);
    }

    Token expectAssignmentOperator() {
        if (checkText("=") || checkText("+=") || checkText("-=") ||
            checkText("*=") || checkText("/=") || checkText("%=")) {
            return advance();
        }
        fail("expected assignment operator", current());
    }

    StatementList parseBlock(const std::string& description) {
        expectText("{", "expected '{' before " + description);
        StatementList statements;
        while (!checkText("}")) {
            if (check(TokenKind::End)) {
                fail("unterminated " + description, current());
            }
            statements.push_back(parseStatement());
        }
        expectText("}", "expected '}' after " + description);
        return statements;
    }

    StatementList parseLoopBlock(const std::string& description) {
        ++loopDepth_;
        StatementList statements = parseBlock(description);
        --loopDepth_;
        return statements;
    }

    ExpressionPtr parseExpression() { return parseLogicalOr(); }

    ExpressionPtr parseLogicalOr() {
        ExpressionPtr expression = parseLogicalAnd();
        while (match("||")) {
            const Token operation = previous();
            expression = std::make_unique<BinaryExpression>(
                operation.text, std::move(expression), parseLogicalAnd(),
                operation.line, operation.column);
        }
        return expression;
    }

    ExpressionPtr parseLogicalAnd() {
        ExpressionPtr expression = parseEquality();
        while (match("&&")) {
            const Token operation = previous();
            expression = std::make_unique<BinaryExpression>(
                operation.text, std::move(expression), parseEquality(),
                operation.line, operation.column);
        }
        return expression;
    }

    ExpressionPtr parseEquality() {
        ExpressionPtr expression = parseComparison();
        while (match("==") || match("!=")) {
            const Token operation = previous();
            expression = std::make_unique<BinaryExpression>(
                operation.text, std::move(expression), parseComparison(),
                operation.line, operation.column);
        }
        return expression;
    }

    ExpressionPtr parseComparison() {
        ExpressionPtr expression = parseTerm();
        while (match("<") || match("<=") || match(">") || match(">=")) {
            const Token operation = previous();
            expression = std::make_unique<BinaryExpression>(
                operation.text, std::move(expression), parseTerm(), operation.line,
                operation.column);
        }
        return expression;
    }

    ExpressionPtr parseTerm() {
        ExpressionPtr expression = parseFactor();
        while (match("+") || match("-")) {
            const Token operation = previous();
            expression = std::make_unique<BinaryExpression>(
                operation.text, std::move(expression), parseFactor(), operation.line,
                operation.column);
        }
        return expression;
    }

    ExpressionPtr parseFactor() {
        ExpressionPtr expression = parseUnary();
        while (match("*") || match("/") || match("%")) {
            const Token operation = previous();
            expression = std::make_unique<BinaryExpression>(
                operation.text, std::move(expression), parseUnary(), operation.line,
                operation.column);
        }
        return expression;
    }

    ExpressionPtr parseUnary() {
        if (match("!") || match("-")) {
            const Token operation = previous();
            return std::make_unique<UnaryExpression>(
                operation.text, parseUnary(), operation.line, operation.column);
        }
        return parsePrimary();
    }

    ExpressionPtr parsePrimary() {
        const Token token = advance();
        if (token.kind == TokenKind::Number) {
            if (token.text.find('.') != std::string::npos) {
                return std::make_unique<LiteralExpression>(
                    std::stod(token.text));
            }
            return std::make_unique<LiteralExpression>(
                static_cast<std::int64_t>(std::stoll(token.text)));
        }
        if (token.kind == TokenKind::String) {
            return std::make_unique<LiteralExpression>(token.text);
        }
        if (token.kind == TokenKind::Identifier) {
            if (token.text == "true") {
                return std::make_unique<LiteralExpression>(true);
            }
            if (token.text == "false") {
                return std::make_unique<LiteralExpression>(false);
            }
            if (token.text == "none") {
                return std::make_unique<LiteralExpression>(Value{});
            }
            return std::make_unique<VariableExpression>(
                token.text, token.line, token.column);
        }
        if (token.text == "(") {
            ExpressionPtr expression = parseExpression();
            expectText(")", "expected ')' after expression");
            return expression;
        }
        fail("expected an expression", token);
    }

    bool check(TokenKind kind) const { return current().kind == kind; }

    bool checkText(const std::string& text) const {
        return current().text == text;
    }

    bool match(const std::string& text) {
        if (current().kind != TokenKind::Symbol || !checkText(text)) {
            return false;
        }
        advance();
        return true;
    }

    Token expect(TokenKind kind, const std::string& message) {
        if (!check(kind)) {
            fail(message, current());
        }
        return advance();
    }

    Token expectText(const std::string& text, const std::string& message) {
        if (!checkText(text)) {
            fail(message, current());
        }
        return advance();
    }

    const Token& current() const { return tokens_[index_]; }

    const Token& previous() const { return tokens_[index_ - 1]; }

    Token advance() {
        const Token token = current();
        if (!check(TokenKind::End)) {
            ++index_;
        }
        return token;
    }

    [[noreturn]] void fail(const std::string& message, const Token& token) const {
        throw SourceError(message, token.line, token.column);
    }

    std::vector<Token> tokens_;
    std::size_t index_ = 0;
    int loopDepth_ = 0;
};

static void executeStatements(const StatementList& statements,
                              Environment& environment) {
    for (const auto& statement : statements) {
        statement->execute(environment);
    }
}

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