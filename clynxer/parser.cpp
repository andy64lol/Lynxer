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
    if ((check(TokenKind::Identifier) && peekAt(1).text == "(") ||
        (checkText("global") && peekAt(1).text == ".")) {
        return parseCallStatement();
    }
    return parseSimpleStatement(true);
}

StatementPtr Parser::parseCallStatement() {
    ExpressionPtr call = parseCallExpression();
    expectText(";", "expected ';' after call");
    return std::make_unique<ExpressionStatement>(std::move(call));
}

ExpressionPtr Parser::parseCallExpression() {
    const Token start = advance();
    std::string name = start.text;
    if (name == "global") {
        expectText(".", "expected '.' after 'global'");
        name = expect(TokenKind::Identifier,
                      "expected function name after 'global.'")
                   .text;
    }
    while (match(".")) {
        const Token part =
            expect(TokenKind::Identifier, "expected name after '.'");
        name += "." + part.text;
    }
    expectText("(", "expected '(' after function name");
    std::vector<ExpressionPtr> arguments = parseArguments(name);
    return std::make_unique<CallExpression>(std::move(name),
                                             std::move(arguments), start.line,
                                             start.column);
}

std::vector<ExpressionPtr> Parser::parseArguments(const std::string& name) {
    std::string resolved = name;
    if (resolved.rfind("global.", 0) == 0) {
        resolved = resolved.substr(7);
    }
    const bool allowInter = resolved == "print" || resolved == "println" ||
                            resolved == "input" || resolved == "inputln";
    std::vector<ExpressionPtr> arguments;
    if (!checkText(")")) {
        for (;;) {
            if (allowInter && check(TokenKind::InterpString)) {
                const Token token = advance();
                arguments.push_back(parseInterpString(token));
            } else {
                arguments.push_back(parseExpression());
            }
            if (!match(",")) {
                break;
            }
        }
    }
    expectText(")", "expected ')' after arguments");
    return arguments;
}

ExpressionPtr Parser::parseInterpString(const Token& token) {
    auto expression = std::make_unique<InterpStringExpression>();
    const std::string& raw = token.text;
    std::string literal;
    std::size_t index = 0;
    while (index < raw.size()) {
        const char current = raw[index];
        if (current == '\\' && index + 1 < raw.size()) {
            const char escaped = raw[index + 1];
            index += 2;
            switch (escaped) {
            case 'n':
                literal += '\n';
                break;
            case 'r':
                literal += '\r';
                break;
            case 't':
                literal += '\t';
                break;
            case '\\':
                literal += '\\';
                break;
            case '"':
                literal += '"';
                break;
            case '{':
                literal += '{';
                break;
            case '}':
                literal += '}';
                break;
            default:
                fail("unknown string escape \\" + std::string(1, escaped),
                     token.line, token.column);
            }
            continue;
        }
        if (current == '{') {
            std::size_t cursor = index + 1;
            bool inString = false;
            std::size_t end = std::string::npos;
            while (cursor < raw.size()) {
                const char inner = raw[cursor];
                if (inString) {
                    if (inner == '\\') {
                        cursor += 2;
                        continue;
                    }
                    if (inner == '"') {
                        inString = false;
                    }
                } else if (inner == '"') {
                    inString = true;
                } else if (inner == '}') {
                    end = cursor;
                    break;
                }
                ++cursor;
            }
            if (end == std::string::npos) {
                fail("missing '}' in interpolated string", token.line,
                     token.column);
            }
            const std::string body = raw.substr(index + 1, end - index - 1);
            if (body.find_first_not_of(" \t") == std::string::npos) {
                fail("empty '{}' interpolation", token.line, token.column);
            }
            expression->addLiteral(literal);
            literal.clear();
            try {
                Lexer lexer(body, "");
                Parser parser(lexer.scan());
                ExpressionPtr inner = parser.parseExpression();
                if (!parser.check(TokenKind::End)) {
                    fail("invalid expression in interpolated string",
                         token.line, token.column);
                }
                expression->addExpression(std::move(inner));
            } catch (const SourceError& error) {
                fail(error.what(), token.line, token.column);
            }
            index = end + 1;
            continue;
        }
        if (current == '}') {
            fail("stray '}' in interpolated string", token.line, token.column);
        }
        literal += current;
        ++index;
    }
    expression->addLiteral(literal);
    return expression;
}

StatementPtr Parser::parseSimpleStatement(bool requireSemicolon) {
    if (checkText("int") || checkText("float") || checkText("str") ||
        checkText("bool") || checkText("any") || checkText("list") ||
        checkText("tuple")) {
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

    if ((checkText("sentinel") || checkText("object")) &&
        peekAt(1).kind == TokenKind::Identifier && peekAt(2).text == "=") {
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

    if (check(TokenKind::Identifier) && peekAt(1).text == "(") {
        ExpressionPtr call = parseCallExpression();
        if (requireSemicolon) {
            expectText(";", "expected ';' after call");
        }
        return std::make_unique<ExpressionStatement>(std::move(call));
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
        checkText("bool") || checkText("any") || checkText("list") ||
        checkText("tuple")) {
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
    if (token.kind == TokenKind::InterpString) {
        fail("interpolated strings are only allowed as arguments of print, "
             "println, input, and inputln",
             token);
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
        if ((token.text == "global" && checkText(".")) || checkText("(") ||
            checkText(".")) {
            // A bare or dotted call such as print(...), global.print(...),
            // or embedPy.len(...). Dotted names without a call are rejected
            // by parseCallExpression with a clear error.
            --index_;
            return parseCallExpression();
        }
        return std::make_unique<VariableExpression>(
            token.text, token.line, token.column);
    }
    if (token.text == "(") {
        if (checkText(")")) {
            advance();
            return std::make_unique<TupleLiteralExpression>(
                std::vector<ExpressionPtr>{});
        }
        ExpressionPtr first = parseExpression();
        if (match(",")) {
            std::vector<ExpressionPtr> elements;
            elements.push_back(std::move(first));
            while (!checkText(")")) {
                elements.push_back(parseExpression());
                if (!match(",")) {
                    break;
                }
            }
            expectText(")", "expected ')' after tuple");
            return std::make_unique<TupleLiteralExpression>(
                std::move(elements));
        }
        expectText(")", "expected ')' after expression");
        return first;
    }
    if (token.text == "[") {
        std::vector<ExpressionPtr> elements;
        if (!checkText("]")) {
            for (;;) {
                elements.push_back(parseExpression());
                if (!match(",")) {
                    break;
                }
            }
        }
        expectText("]", "expected ']' after list literal");
        return std::make_unique<ListLiteralExpression>(std::move(elements));
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

const Token& Parser::peekAt(std::size_t offset) const {
    const std::size_t position = index_ + offset;
    if (position >= tokens_.size()) {
        return tokens_.back();
    }
    return tokens_[position];
}

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
