#include "parser.hpp"

#include "error.hpp"

#include <cstdint>

namespace clynxer {

std::unordered_map<std::string, Function> Parser::parseProgram() {
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

StatementPtr Parser::parseStatement() {
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

StatementPtr Parser::parseSimpleStatement(bool requireSemicolon) {
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

StatementPtr Parser::parseIf() {
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

StatementPtr Parser::parseWhile() {
    expectText("while", "expected 'while'");
    expectText("(", "expected '(' after 'while'");
    ExpressionPtr condition = parseExpression();
    expectText(")", "expected ')' after condition");
    return std::make_unique<WhileStatement>(
        std::move(condition), parseLoopBlock("while body"));
}

StatementPtr Parser::parseFor() {
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

StatementPtr Parser::parseDoWhile() {
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

StatementPtr Parser::parseIterate() {
    const Token loop = expectText("iterate", "expected 'iterate'");
    expectText("(", "expected '(' after 'iterate'");
    ExpressionPtr count = parseExpression();
    expectText(")", "expected ')' after iterate count");
    return std::make_unique<IterateStatement>(
        std::move(count), parseLoopBlock("iterate body"), loop.line,
        loop.column);
}

StatementPtr Parser::parseForever() {
    const Token loop = expectText("forever", "expected 'forever'");
    expectText("(", "expected '(' after 'forever'");
    expectText(")", "forever() does not accept arguments");
    return std::make_unique<ForeverStatement>(
        parseLoopBlock("forever body"), loop.line, loop.column);
}

StatementPtr Parser::parseLoopControl() {
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

Token Parser::expectAssignmentOperator() {
    if (checkText("=") || checkText("+=") || checkText("-=") ||
        checkText("*=") || checkText("/=") || checkText("%=")) {
        return advance();
    }
    fail("expected assignment operator", current());
}

StatementList Parser::parseBlock(const std::string& description) {
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

StatementList Parser::parseLoopBlock(const std::string& description) {
    ++loopDepth_;
    StatementList statements = parseBlock(description);
    --loopDepth_;
    return statements;
}

ExpressionPtr Parser::parseExpression() { return parseLogicalOr(); }

ExpressionPtr Parser::parseLogicalOr() {
    ExpressionPtr expression = parseLogicalAnd();
    while (match("||")) {
        const Token operation = previous();
        expression = std::make_unique<BinaryExpression>(
            operation.text, std::move(expression), parseLogicalAnd(),
            operation.line, operation.column);
    }
    return expression;
}

ExpressionPtr Parser::parseLogicalAnd() {
    ExpressionPtr expression = parseEquality();
    while (match("&&")) {
        const Token operation = previous();
        expression = std::make_unique<BinaryExpression>(
            operation.text, std::move(expression), parseEquality(),
            operation.line, operation.column);
    }
    return expression;
}

ExpressionPtr Parser::parseEquality() {
    ExpressionPtr expression = parseComparison();
    while (match("==") || match("!=")) {
        const Token operation = previous();
        expression = std::make_unique<BinaryExpression>(
            operation.text, std::move(expression), parseComparison(),
            operation.line, operation.column);
    }
    return expression;
}

ExpressionPtr Parser::parseComparison() {
    ExpressionPtr expression = parseTerm();
    while (match("<") || match("<=") || match(">") || match(">=")) {
        const Token operation = previous();
        expression = std::make_unique<BinaryExpression>(
            operation.text, std::move(expression), parseTerm(), operation.line,
            operation.column);
    }
    return expression;
}

ExpressionPtr Parser::parseTerm() {
    ExpressionPtr expression = parseFactor();
    while (match("+") || match("-")) {
        const Token operation = previous();
        expression = std::make_unique<BinaryExpression>(
            operation.text, std::move(expression), parseFactor(), operation.line,
            operation.column);
    }
    return expression;
}

ExpressionPtr Parser::parseFactor() {
    ExpressionPtr expression = parseUnary();
    while (match("*") || match("/") || match("%")) {
        const Token operation = previous();
        expression = std::make_unique<BinaryExpression>(
            operation.text, std::move(expression), parseUnary(), operation.line,
            operation.column);
    }
    return expression;
}

ExpressionPtr Parser::parseUnary() {
    if (match("!") || match("-")) {
        const Token operation = previous();
        return std::make_unique<UnaryExpression>(
            operation.text, parseUnary(), operation.line, operation.column);
    }
    return parsePrimary();
}

ExpressionPtr Parser::parsePrimary() {
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

bool Parser::check(TokenKind kind) const { return current().kind == kind; }

bool Parser::checkText(const std::string& text) const {
    return current().text == text;
}

bool Parser::match(const std::string& text) {
    if (current().kind != TokenKind::Symbol || !checkText(text)) {
        return false;
    }
    advance();
    return true;
}

Token Parser::expect(TokenKind kind, const std::string& message) {
    if (!check(kind)) {
        fail(message, current());
    }
    return advance();
}

Token Parser::expectText(const std::string& text, const std::string& message) {
    if (!checkText(text)) {
        fail(message, current());
    }
    return advance();
}

const Token& Parser::current() const { return tokens_[index_]; }

const Token& Parser::previous() const { return tokens_[index_ - 1]; }

Token Parser::advance() {
    const Token token = current();
    if (!check(TokenKind::End)) {
        ++index_;
    }
    return token;
}

void Parser::fail(const std::string& message, const Token& token) const {
    throw SourceError(message, token.line, token.column);
}

} // namespace clynxer
