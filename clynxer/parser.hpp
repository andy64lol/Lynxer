#pragma once

#include "ast.hpp"
#include "error.hpp"
#include "lexer.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace clynxer {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    std::unordered_map<std::string, Function> parseProgram();

private:
    StatementPtr parseStatement();

    StatementPtr parseCallStatement();

    ExpressionPtr parseCallExpression();

    std::vector<ExpressionPtr> parseArguments(const std::string& name);

    ExpressionPtr parseInterpString(const Token& token);

    StatementPtr parseSimpleStatement(bool requireSemicolon);

    StatementPtr parseIf();

    StatementPtr parseWhile();

    StatementPtr parseFor();

    StatementPtr parseDoWhile();

    StatementPtr parseIterate();

    StatementPtr parseForever();

    StatementPtr parseLoopControl();

    Token expectAssignmentOperator();

    StatementList parseBlock(const std::string& description);

    StatementList parseLoopBlock(const std::string& description);

    ExpressionPtr parseExpression();

    ExpressionPtr parseLogicalOr();

    ExpressionPtr parseLogicalAnd();

    ExpressionPtr parseEquality();

    ExpressionPtr parseComparison();

    ExpressionPtr parseTerm();

    ExpressionPtr parseFactor();

    ExpressionPtr parseUnary();

    ExpressionPtr parsePrimary();

    bool check(TokenKind kind) const;

    bool checkText(const std::string& text) const;

    bool match(const std::string& text);

    Token expect(TokenKind kind, const std::string& message);

    Token expectText(const std::string& text, const std::string& message);

    const Token& current() const;

    const Token& peekAt(std::size_t offset) const;

    const Token& previous() const;

    Token advance();

    [[noreturn]] void fail(const std::string& message, const Token& token) const;

    [[noreturn]] void fail(const std::string& message, int line, int column) const {
        throw SourceError(message, line, column);
    }

    std::vector<Token> tokens_;
    std::size_t index_ = 0;
    int loopDepth_ = 0;
};

} // namespace clynxer
