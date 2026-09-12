#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace clynxer {

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

    std::vector<Token> scan();

private:
    bool atEnd() const;

    char peek() const;

    char advance();

    void skipWhitespaceAndComments();

    bool startsWith(const std::string& text) const;

    void skipDelimitedComment();

    std::string readString(int line, int column);

    static bool isIdentifierStart(char value);

    static bool isIdentifierPart(char value);

    [[noreturn]] void fail(const std::string& message, int line, int column) const;

    const std::string& source_;
    std::string filename_;
    std::size_t index_ = 0;
    int line_ = 1;
    int column_ = 1;
};

} // namespace clynxer
