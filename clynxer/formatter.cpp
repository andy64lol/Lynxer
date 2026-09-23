#include "formatter.hpp"

#include "lexer.hpp"
#include "parser.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace clynxer {

namespace {

bool isWord(const Token& token) {
    switch (token.kind) {
    case TokenKind::Identifier:
    case TokenKind::Number:
    case TokenKind::String:
    case TokenKind::Char:
    case TokenKind::InterpString:
        return true;
    default:
        return false;
    }
}

bool isSymbol(const Token& token, const char* text) {
    return token.kind == TokenKind::Symbol && token.text == text;
}

// Any symbol that is not pure punctuation (a brace, bracket, comma, semicolon
// or member dot) reads as an operator for spacing purposes.
bool isOperator(const Token& token) {
    if (token.kind != TokenKind::Symbol) {
        return false;
    }
    const std::string& text = token.text;
    return text != "," && text != ";" && text != "(" && text != ")" &&
           text != "[" && text != "]" && text != "{" && text != "}" &&
           text != ".";
}

bool endsWith(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string trimCopy(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(begin, end - begin);
}

// Trailing whitespace only, so indentation survives.
std::string rstripCopy(const std::string& text) {
    std::size_t end = text.size();
    while (end > 0 &&
           (text[end - 1] == ' ' || text[end - 1] == '\t' ||
            text[end - 1] == '\r')) {
        --end;
    }
    return text.substr(0, end);
}

bool needsSpace(const Token* previous, const Token& current) {
    if (previous == nullptr) {
        return false;
    }
    if (isSymbol(current, ",") || isSymbol(current, ";") ||
        isSymbol(current, ")") || isSymbol(current, "]") ||
        isSymbol(current, "}") || isSymbol(current, ".")) {
        return false;
    }
    if (isSymbol(*previous, "(") || isSymbol(*previous, "[") ||
        isSymbol(*previous, ".")) {
        return false;
    }
    if (isSymbol(current, "(")) {
        return false;
    }
    if (isSymbol(current, "+") || isSymbol(current, "-") ||
        isSymbol(current, "~") || isSymbol(current, "!")) {
        return isWord(*previous) || isSymbol(*previous, ")") ||
               isSymbol(*previous, "]");
    }
    if (isSymbol(*previous, "+") || isSymbol(*previous, "-") ||
        isSymbol(*previous, "~") || isSymbol(*previous, "!")) {
        return isWord(current) || isSymbol(current, "(") || isSymbol(current, "[");
    }
    if (isOperator(*previous) || isOperator(current)) {
        return true;
    }
    if (isWord(*previous) && isWord(current)) {
        return true;
    }
    if ((isSymbol(*previous, ")") || isSymbol(*previous, "]") ||
         isSymbol(*previous, "}")) &&
        isWord(current)) {
        return true;
    }
    return false;
}

// Comment text found between two tokens. A `//` line is kept as written; a
// `///`/`////` block is kept verbatim, including its delimiters.
std::vector<std::string> commentsInGap(const std::string& gap) {
    std::vector<std::string> comments;
    std::vector<std::string> lines;
    std::string current;
    for (char character : gap) {
        if (character == '\n') {
            lines.push_back(current);
            current.clear();
        } else {
            current += character;
        }
    }
    lines.push_back(current);

    for (std::size_t index = 0; index < lines.size(); ++index) {
        const std::string stripped = trimCopy(lines[index]);
        if (stripped.empty()) {
            continue;
        }
        const std::string delimiter =
            stripped.rfind("////", 0) == 0 ? "////"
            : stripped.rfind("///", 0) == 0 ? "///"
                                            : "";
        if (!delimiter.empty()) {
            if (stripped.size() > delimiter.size() &&
                endsWith(stripped, delimiter)) {
                comments.push_back(stripped);
                continue;
            }
            std::string block = stripped;
            while (index + 1 < lines.size()) {
                ++index;
                block += "\n" + trimCopy(lines[index]);
                if (endsWith(trimCopy(lines[index]), delimiter)) {
                    break;
                }
            }
            comments.push_back(block);
            continue;
        }
        if (stripped.rfind("//", 0) == 0) {
            comments.push_back(stripped);
        }
    }
    return comments;
}

std::string onelineComment(const std::string& comment) {
    std::string collapsed = comment;
    for (char& character : collapsed) {
        if (character == '\n' || character == '\r') {
            character = ' ';
        }
    }
    if (collapsed.rfind("///", 0) == 0) {
        return collapsed;
    }
    // `// text` cannot survive on one line next to code, so use the delimited
    // form the language provides.
    return "///" + collapsed.substr(2) + " ///";
}

class Writer {
public:
    explicit Writer(bool oneline) : oneline_(oneline), lines_(1) {}

    int& indent() { return indent_; }

    bool currentBlank() const { return trimCopy(lines_.back()).empty(); }

    void append(const std::string& text, bool space = false) {
        if (text.empty()) {
            return;
        }
        std::string& line = lines_.back();
        if (space && !line.empty() && line.back() != ' ') {
            line += ' ';
        }
        line += text;
    }

    void newline(bool force = false) {
        if (oneline_) {
            std::string& line = lines_.back();
            if (!line.empty() && line.back() != ' ') {
                line += ' ';
            }
            return;
        }
        if (force || !currentBlank()) {
            lines_.push_back(std::string(static_cast<std::size_t>(4 * indent_), ' '));
        }
    }

    void alignCurrentIndent() {
        if (oneline_) {
            return;
        }
        if (currentBlank()) {
            lines_.back() = std::string(static_cast<std::size_t>(4 * indent_), ' ');
        }
    }

    std::string finish() const {
        if (oneline_) {
            std::string collapsed;
            bool pendingSpace = false;
            for (char character : lines_.back()) {
                if (character == ' ' || character == '\t' || character == '\n' ||
                    character == '\r') {
                    pendingSpace = !collapsed.empty();
                    continue;
                }
                if (pendingSpace) {
                    collapsed += ' ';
                    pendingSpace = false;
                }
                collapsed += character;
            }
            return collapsed;
        }
        std::vector<std::string> result = lines_;
        while (!result.empty() && trimCopy(result.back()).empty()) {
            result.pop_back();
        }
        std::string output;
        for (std::size_t index = 0; index < result.size(); ++index) {
            if (index != 0) {
                output += '\n';
            }
            output += rstripCopy(result[index]);
        }
        return trimCopy(output) + "\n";
    }

private:
    bool oneline_;
    int indent_ = 0;
    std::vector<std::string> lines_;
};

void emitComments(Writer& writer, const std::string& gap, bool oneline) {
    for (const std::string& comment : commentsInGap(gap)) {
        if (oneline) {
            writer.append(onelineComment(comment), !writer.currentBlank());
            writer.append(" ");
        } else {
            writer.newline();
            writer.append(comment);
            writer.newline();
        }
    }
}

} // namespace

std::string formatSource(const std::string& source, const std::string& display,
                         bool oneline) {
    Lexer lexer(source, display);
    std::vector<Token> tokens = lexer.scan();
    Parser parser(tokens);
    parser.parseProgram(false);   // validates; throws SourceError on failure

    Writer writer(oneline);
    const Token* previous = nullptr;
    std::size_t previousEnd = 0;
    int parenDepth = 0;

    for (std::size_t index = 0; index < tokens.size(); ++index) {
        const Token& token = tokens[index];
        if (token.kind == TokenKind::End) {
            break;
        }
        const Token* next = index + 1 < tokens.size() ? &tokens[index + 1] : nullptr;

        const std::string gap =
            source.substr(previousEnd, token.start - previousEnd);
        emitComments(writer, gap, oneline);

        const std::string raw = source.substr(token.start, token.end - token.start);
        if (isSymbol(token, "{")) {
            writer.append("{", !writer.currentBlank());
            writer.indent() += 1;
            writer.newline();
        } else if (isSymbol(token, "}")) {
            if (writer.indent() > 0) {
                writer.indent() -= 1;
            }
            writer.newline();
            writer.alignCurrentIndent();
            writer.append("}");
            const bool continues = next != nullptr &&
                (isSymbol(*next, ";") || isSymbol(*next, ",") ||
                 isSymbol(*next, ")") || isSymbol(*next, "]"));
            const bool keywordFollows = next != nullptr &&
                next->kind == TokenKind::Identifier &&
                (next->text == "else" || next->text == "elif" ||
                 next->text == "catch");
            if (!continues && !keywordFollows) {
                writer.newline();
            }
        } else if (isSymbol(token, ";")) {
            writer.append(";");
            if (parenDepth > 0) {
                writer.append(" ");
            } else {
                writer.newline();
            }
        } else if (isSymbol(token, ",")) {
            writer.append(",");
            writer.append(" ");
        } else {
            writer.append(raw, needsSpace(previous, token));
        }

        if (isSymbol(token, "(")) {
            ++parenDepth;
        } else if (isSymbol(token, ")")) {
            if (parenDepth > 0) {
                --parenDepth;
            }
        }
        previous = &token;
        previousEnd = token.end;
    }

    emitComments(writer, source.substr(previousEnd), oneline);
    return writer.finish();
}

} // namespace clynxer
