#ifndef CLYNXER_NATIVE_JSON_HPP
#define CLYNXER_NATIVE_JSON_HPP

// Minimal, dependency-free JSON value / parser / serializer used by clynxer's
// native stdlib modules. This header is included privately by each .so so that
// no module depends on another module's symbols.
//
// Divergences from Python's `json`:
//   * non-finite numbers (NaN/Infinity) serialize as `null`, never as the
//     Python-specific `NaN`/`Infinity` tokens, so output is always valid JSON;
//   * object key order is preserved (insertion order), matching Python 3.7+.

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace native_json {

enum class Type { Null, Bool, Integer, Number, String, Array, Object };

struct Value {
    Type type = Type::Null;
    bool boolean = false;
    std::int64_t integer = 0;
    double number = 0.0;
    std::string text;
    std::vector<Value> items;
    std::vector<std::pair<std::string, Value>> fields;
};

inline Value makeNull() { return Value(); }

inline Value makeBool(bool value) {
    Value result;
    result.type = Type::Bool;
    result.boolean = value;
    return result;
}

inline Value makeInteger(std::int64_t value) {
    Value result;
    result.type = Type::Integer;
    result.integer = value;
    return result;
}

inline Value makeNumber(double value) {
    Value result;
    result.type = Type::Number;
    result.number = value;
    return result;
}

inline Value makeString(const std::string& value) {
    Value result;
    result.type = Type::String;
    result.text = value;
    return result;
}

inline Value makeArray() {
    Value result;
    result.type = Type::Array;
    return result;
}

inline Value makeObject() {
    Value result;
    result.type = Type::Object;
    return result;
}

inline const Value* findField(const Value& object, const std::string& key) {
    if (object.type != Type::Object) {
        return nullptr;
    }
    for (const auto& field : object.fields) {
        if (field.first == key) {
            return &field.second;
        }
    }
    return nullptr;
}

inline Value* findField(Value& object, const std::string& key) {
    if (object.type != Type::Object) {
        return nullptr;
    }
    for (auto& field : object.fields) {
        if (field.first == key) {
            return &field.second;
        }
    }
    return nullptr;
}

inline void setField(Value& object, const std::string& key, Value value) {
    if (Value* existing = findField(object, key); existing != nullptr) {
        *existing = std::move(value);
        return;
    }
    object.fields.emplace_back(key, std::move(value));
}

inline bool removeField(Value& object, const std::string& key) {
    if (object.type != Type::Object) {
        return false;
    }
    for (std::size_t index = 0; index < object.fields.size(); ++index) {
        if (object.fields[index].first == key) {
            object.fields.erase(object.fields.begin() +
                                static_cast<std::ptrdiff_t>(index));
            return true;
        }
    }
    return false;
}

inline bool isNumber(const Value& value) {
    return value.type == Type::Integer || value.type == Type::Number;
}

inline double asDouble(const Value& value) {
    if (value.type == Type::Integer) {
        return static_cast<double>(value.integer);
    }
    if (value.type == Type::Number) {
        return value.number;
    }
    return 0.0;
}

inline std::int64_t asInteger(const Value& value) {
    if (value.type == Type::Integer) {
        return value.integer;
    }
    if (value.type == Type::Number) {
        return static_cast<std::int64_t>(value.number);
    }
    if (value.type == Type::Bool) {
        return value.boolean ? 1 : 0;
    }
    return 0;
}

inline const char* typeName(const Value& value) {
    switch (value.type) {
        case Type::Null: return "null";
        case Type::Bool: return "boolean";
        case Type::Integer: return "int";
        case Type::Number: return "float";
        case Type::String: return "str";
        case Type::Array: return "list";
        case Type::Object: return "dict";
    }
    return "unknown";
}

inline void appendUtf8(std::string& output, unsigned int code) {
    if (code <= 0x7F) {
        output += static_cast<char>(code);
    } else if (code <= 0x7FF) {
        output += static_cast<char>(0xC0 | (code >> 6));
        output += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code <= 0xFFFF) {
        output += static_cast<char>(0xE0 | (code >> 12));
        output += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        output += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        output += static_cast<char>(0xF0 | (code >> 18));
        output += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        output += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        output += static_cast<char>(0x80 | (code & 0x3F));
    }
}

class Parser {
public:
    explicit Parser(const std::string& input) : input_(input) {}

    bool parse(Value& output) {
        skipWhitespace();
        if (!parseValue(output)) {
            return false;
        }
        skipWhitespace();
        if (position_ != input_.size()) {
            error_ = "unexpected trailing content";
            return false;
        }
        return true;
    }

    const std::string& error() const { return error_; }

private:
    bool atEnd() const { return position_ >= input_.size(); }
    char peek() const { return input_[position_]; }

    void skipWhitespace() {
        while (!atEnd()) {
            const char value = peek();
            if (value == ' ' || value == '\t' || value == '\n' ||
                value == '\r') {
                ++position_;
            } else {
                break;
            }
        }
    }

    bool literal(const char* word) {
        const std::size_t length = std::char_traits<char>::length(word);
        if (input_.compare(position_, length, word) != 0) {
            return false;
        }
        position_ += length;
        return true;
    }

    bool parseValue(Value& output) {
        if (atEnd()) {
            error_ = "unexpected end of input";
            return false;
        }
        switch (peek()) {
            case '{': return parseObject(output);
            case '[': return parseArray(output);
            case '"': {
                std::string text;
                if (!parseString(text)) {
                    return false;
                }
                output = makeString(text);
                return true;
            }
            case 't':
                if (literal("true")) { output = makeBool(true); return true; }
                error_ = "invalid literal";
                return false;
            case 'f':
                if (literal("false")) { output = makeBool(false); return true; }
                error_ = "invalid literal";
                return false;
            case 'n':
                if (literal("null")) { output = makeNull(); return true; }
                error_ = "invalid literal";
                return false;
            default: return parseNumber(output);
        }
    }

    bool parseObject(Value& output) {
        ++position_;
        output = makeObject();
        skipWhitespace();
        if (!atEnd() && peek() == '}') {
            ++position_;
            return true;
        }
        while (true) {
            skipWhitespace();
            if (atEnd() || peek() != '"') {
                error_ = "expected object key";
                return false;
            }
            std::string key;
            if (!parseString(key)) {
                return false;
            }
            skipWhitespace();
            if (atEnd() || peek() != ':') {
                error_ = "expected ':' after object key";
                return false;
            }
            ++position_;
            skipWhitespace();
            Value value;
            if (!parseValue(value)) {
                return false;
            }
            setField(output, key, std::move(value));
            skipWhitespace();
            if (atEnd()) {
                error_ = "unterminated object";
                return false;
            }
            if (peek() == ',') {
                ++position_;
                continue;
            }
            if (peek() == '}') {
                ++position_;
                return true;
            }
            error_ = "expected ',' or '}' in object";
            return false;
        }
    }

    bool parseArray(Value& output) {
        ++position_;
        output = makeArray();
        skipWhitespace();
        if (!atEnd() && peek() == ']') {
            ++position_;
            return true;
        }
        while (true) {
            skipWhitespace();
            Value value;
            if (!parseValue(value)) {
                return false;
            }
            output.items.push_back(std::move(value));
            skipWhitespace();
            if (atEnd()) {
                error_ = "unterminated array";
                return false;
            }
            if (peek() == ',') {
                ++position_;
                continue;
            }
            if (peek() == ']') {
                ++position_;
                return true;
            }
            error_ = "expected ',' or ']' in array";
            return false;
        }
    }

    bool parseHex4(unsigned int& value) {
        if (position_ + 4 > input_.size()) {
            error_ = "truncated \\u escape";
            return false;
        }
        value = 0;
        for (int index = 0; index < 4; ++index) {
            const char digit = input_[position_++];
            value <<= 4;
            if (digit >= '0' && digit <= '9') {
                value |= static_cast<unsigned int>(digit - '0');
            } else if (digit >= 'a' && digit <= 'f') {
                value |= static_cast<unsigned int>(digit - 'a' + 10);
            } else if (digit >= 'A' && digit <= 'F') {
                value |= static_cast<unsigned int>(digit - 'A' + 10);
            } else {
                error_ = "invalid \\u escape";
                return false;
            }
        }
        return true;
    }

    bool parseString(std::string& output) {
        ++position_;
        output.clear();
        while (true) {
            if (atEnd()) {
                error_ = "unterminated string";
                return false;
            }
            const char value = input_[position_++];
            if (value == '"') {
                return true;
            }
            if (value != '\\') {
                output += value;
                continue;
            }
            if (atEnd()) {
                error_ = "unterminated escape";
                return false;
            }
            const char escape = input_[position_++];
            switch (escape) {
                case '"': output += '"'; break;
                case '\\': output += '\\'; break;
                case '/': output += '/'; break;
                case 'b': output += '\b'; break;
                case 'f': output += '\f'; break;
                case 'n': output += '\n'; break;
                case 'r': output += '\r'; break;
                case 't': output += '\t'; break;
                case 'u': {
                    unsigned int code = 0;
                    if (!parseHex4(code)) {
                        return false;
                    }
                    if (code >= 0xD800 && code <= 0xDBFF) {
                        if (position_ + 1 < input_.size() &&
                            input_[position_] == '\\' &&
                            input_[position_ + 1] == 'u') {
                            position_ += 2;
                            unsigned int low = 0;
                            if (!parseHex4(low)) {
                                return false;
                            }
                            code = 0x10000 + ((code - 0xD800) << 10) +
                                   (low - 0xDC00);
                        }
                    }
                    appendUtf8(output, code);
                    break;
                }
                default:
                    error_ = "invalid escape sequence";
                    return false;
            }
        }
    }

    bool parseNumber(Value& output) {
        const std::size_t start = position_;
        if (!atEnd() && peek() == '-') {
            ++position_;
        }
        bool isInteger = true;
        while (!atEnd() && peek() >= '0' && peek() <= '9') {
            ++position_;
        }
        if (!atEnd() && peek() == '.') {
            isInteger = false;
            ++position_;
            while (!atEnd() && peek() >= '0' && peek() <= '9') {
                ++position_;
            }
        }
        if (!atEnd() && (peek() == 'e' || peek() == 'E')) {
            isInteger = false;
            ++position_;
            if (!atEnd() && (peek() == '+' || peek() == '-')) {
                ++position_;
            }
            while (!atEnd() && peek() >= '0' && peek() <= '9') {
                ++position_;
            }
        }
        if (position_ == start) {
            error_ = "invalid value";
            return false;
        }
        const std::string token = input_.substr(start, position_ - start);
        if (isInteger) {
            std::int64_t integer = 0;
            const auto result =
                std::from_chars(token.data(), token.data() + token.size(),
                                integer);
            if (result.ec == std::errc() &&
                result.ptr == token.data() + token.size()) {
                output = makeInteger(integer);
                return true;
            }
        }
        char* end = nullptr;
        const double number = std::strtod(token.c_str(), &end);
        if (end != token.c_str() + token.size()) {
            error_ = "invalid number";
            return false;
        }
        output = makeNumber(number);
        return true;
    }

    const std::string& input_;
    std::size_t position_ = 0;
    std::string error_ = "invalid JSON";
};

inline bool parse(const std::string& input, Value& output,
                  std::string& error) {
    Parser parser(input);
    if (parser.parse(output)) {
        return true;
    }
    error = parser.error();
    return false;
}

inline bool parse(const std::string& input, Value& output) {
    std::string error;
    return parse(input, output, error);
}

inline void escapeString(const std::string& value, std::string& output) {
    output += '"';
    for (const unsigned char character : value) {
        switch (character) {
            case '"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (character < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", character);
                    output += buffer;
                } else {
                    output += static_cast<char>(character);
                }
        }
    }
    output += '"';
}

inline std::string numberToString(double value) {
    if (!std::isfinite(value)) {
        return "null";
    }
    char buffer[64];
    const auto result =
        std::to_chars(buffer, buffer + sizeof(buffer), value);
    std::string text(buffer, result.ptr);
    if (text.find_first_of(".eE") == std::string::npos) {
        text += ".0";
    }
    return text;
}

inline void serialize(const Value& value, std::string& output, bool pretty,
                      int width, int depth) {
    const auto indent = [&](int level) {
        if (pretty) {
            output += '\n';
            output.append(static_cast<std::size_t>(level * width), ' ');
        }
    };
    switch (value.type) {
        case Type::Null: output += "null"; break;
        case Type::Bool: output += value.boolean ? "true" : "false"; break;
        case Type::Integer: output += std::to_string(value.integer); break;
        case Type::Number: output += numberToString(value.number); break;
        case Type::String: escapeString(value.text, output); break;
        case Type::Array:
            if (value.items.empty()) {
                output += "[]";
                break;
            }
            output += '[';
            for (std::size_t index = 0; index < value.items.size(); ++index) {
                if (index > 0) {
                    output += pretty ? "," : ", ";
                }
                indent(depth + 1);
                serialize(value.items[index], output, pretty, width, depth + 1);
            }
            indent(depth);
            output += ']';
            break;
        case Type::Object:
            if (value.fields.empty()) {
                output += "{}";
                break;
            }
            output += '{';
            for (std::size_t index = 0; index < value.fields.size(); ++index) {
                if (index > 0) {
                    output += pretty ? "," : ", ";
                }
                indent(depth + 1);
                escapeString(value.fields[index].first, output);
                output += ": ";
                serialize(value.fields[index].second, output, pretty, width,
                          depth + 1);
            }
            indent(depth);
            output += '}';
            break;
    }
}

inline std::string dump(const Value& value, bool pretty, int width) {
    std::string output;
    serialize(value, output, pretty, width, 0);
    return output;
}

inline std::string dump(const Value& value, bool pretty) {
    return dump(value, pretty, 2);
}

inline std::string dump(const Value& value) { return dump(value, false, 2); }

}  // namespace native_json

#endif  // CLYNXER_NATIVE_JSON_HPP
