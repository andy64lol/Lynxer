#pragma once

#include "ast.hpp"
#include "error.hpp"
#include "lexer.hpp"

#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace clynxer {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    std::unordered_map<std::string, Function> parseProgram(
        bool requireEntryPoints = true);

    // Every import declared while parsing, in source order.
    const std::vector<ImportRecord>& imports() const { return imports_; }

    // Every top-level function declared while parsing, in source order.
    const std::vector<std::string>& programOrder() const {
        return programOrder_;
    }

private:
    void parseStructDefinition();
    void parseClassDefinition();
    void parseEnumDefinition();
    std::vector<std::pair<std::string, std::string>> parseParameters();
    std::vector<Parameter> parseFunctionParameters();
    Function parseFunction(const std::string& kind, bool topLevel);
    StatementPtr parseLocalFunction();
    bool looksLikeCodeblockSignature() const;
    std::vector<std::string> parseCodeblockSignature();
    StatementPtr parseCodeblockDeclaration();
    std::string parseTypeName(const std::string& message);
    bool isTypeName(const Token& token) const;
    ExpressionPtr parsePostfix(ExpressionPtr expression, const Token& start);
    ExpressionPtr parseTypedElement();
    StatementPtr parseReturn();
    StatementPtr parseSwitch();

    StatementPtr parseTryCatch();
    StatementPtr parseExec();
    StatementPtr parseVargroupDeclaration(bool constant);
    StatementPtr parseDeclaration(bool constant);
    StatementPtr parseTypedDotAssignment();
    StatementPtr parseAssignmentOrExpressionStatement(bool requireSemicolon);

    StatementPtr parseStatement();

    StatementPtr parseCallStatement();
    StatementPtr parseImport();
    StatementPtr parseImportAs();

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

    ExpressionPtr parseOrExpr();

    ExpressionPtr parseXorExpr();

    ExpressionPtr parseAndExpr();

    ExpressionPtr parseNotExpr();

    ExpressionPtr parseCompExpr();

    ExpressionPtr parseBitwiseOr();

    ExpressionPtr parseBitwiseXor();

    ExpressionPtr parseBitwiseAnd();

    ExpressionPtr parseShift();

    ExpressionPtr parseArith();

    ExpressionPtr parseTerm();

    ExpressionPtr parsePower();

    ExpressionPtr parseFactor();

    ExpressionPtr parsePrimary();

    bool check(TokenKind kind) const;

    bool checkText(const std::string& text) const;

    bool checkKeyword(const std::string& text) const;

    bool match(const std::string& text);

    bool matchKeyword(const std::string& text);

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
    std::unordered_set<std::string> codeblockNames_;
    std::vector<ImportRecord> imports_;
    std::vector<std::string> programOrder_;
};

} // namespace clynxer
