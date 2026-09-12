#include "lexer.hpp"

#include "error.hpp"

#include <cctype>

namespace clynxer {

std::vector<Token> Lexer::scan() {
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
            if (text == "inter" && peek() == '"') {
                // inter"..." — keep the raw content; the parser splits it
                // into literal and interpolated parts.
                advance();
                tokens.push_back(
                    {TokenKind::InterpString, readRawString(line, column),
                     line, column});
            } else {
                tokens.push_back({TokenKind::Identifier, text, line, column});
            }
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
            } else if (std::string("+-*/%<>=!;(),{}.[]").find(current) ==
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

bool Lexer::atEnd() const { return index_ >= source_.size(); }

char Lexer::peek() const { return atEnd() ? '\0' : source_[index_]; }

char Lexer::advance() {
    const char value = source_[index_++];
    if (value == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
    return value;
}

void Lexer::skipWhitespaceAndComments() {
    for (;;) {
        while (!atEnd() && std::isspace(static_cast<unsigned char>(peek()))) {
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

bool Lexer::startsWith(const std::string& text) const {
    return source_.compare(index_, text.size(), text) == 0;
}

void Lexer::skipDelimitedComment() {
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

std::string Lexer::readString(int line, int column) {
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
                fail("unknown string escape \\" + std::string(1, escaped),
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

std::string Lexer::readRawString(int line, int column) {
    std::string value;
    while (!atEnd() && peek() != '"') {
        if (peek() == '\n') {
            fail("unterminated string", line, column);
        }
        value += advance();
    }
    if (atEnd()) {
        fail("unterminated string", line, column);
    }
    advance();
    return value;
}

bool Lexer::isIdentifierStart(char value) {
    return std::isalpha(static_cast<unsigned char>(value)) || value == '_';
}

bool Lexer::isIdentifierPart(char value) {
    return std::isalnum(static_cast<unsigned char>(value)) || value == '_';
}

void Lexer::fail(const std::string& message, int line, int column) const {
    throw SourceError(message, line, column);
}

} // namespace clynxer
