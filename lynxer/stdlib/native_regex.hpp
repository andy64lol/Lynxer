#ifndef LYNXER_NATIVE_REGEX_HPP
#define LYNXER_NATIVE_REGEX_HPP

// Python-flavoured regular-expression helpers built on std::regex.
//
// std::regex (libstdc++) implements the ECMAScript grammar, which differs from
// Python's `re`. The translations performed here are:
//   * named groups  (?P<name>...)  -> (...) with the name/index recorded, and
//     (?P=name) resolved to a numeric backreference;
//   * DOTALL rewrites an unescaped `.` outside a character class to [\s\S];
//   * inline (?i) / (?m) / (?s) set the corresponding whole-pattern flags.
//
// Unsupported by the underlying engine and therefore reported as "unsupported":
// lookbehind (?<=...)/(?<!...), atomic groups (?>...), inline flags applied to
// only part of a pattern, possessive quantifiers in Python-only forms, and
// Unicode property escapes such as \p{L}.
//
// Invalid or unsupported patterns are reported in band (the native ABI has no
// error channel): boolean predicates return 0, strings return "", and index
// helpers return -1.

#include "native_json.hpp"

#include <cstdint>
#include <regex>
#include <string>
#include <utility>
#include <vector>

namespace native_regex {

enum Flag : int {
    NoFlags = 0,
    IgnoreCase = 1,
    Multiline = 2,
    DotAll = 4,
};

struct Compiled {
    std::regex expression;
    std::vector<std::pair<std::string, int>> names;
};

struct Translation {
    bool ok = true;
    std::string error;
    std::string pattern;
    std::vector<std::pair<std::string, int>> names;
    int flags = 0;
};

inline bool isEscapableDigit(char character) {
    return character >= '0' && character <= '9';
}

inline Translation translatePattern(const std::string& raw,
                                    int initialFlags = 0) {
    Translation result;
    result.flags = initialFlags;
    std::string output;
    int groupIndex = 0;
    bool inClass = false;
    std::size_t index = 0;
    while (index < raw.size()) {
        const char character = raw[index];
        if (character == '\\') {
            output += character;
            if (index + 1 < raw.size()) {
                output += raw[index + 1];
                index += 2;
            } else {
                ++index;
            }
            continue;
        }
        if (inClass) {
            output += character;
            if (character == ']') {
                inClass = false;
            }
            ++index;
            continue;
        }
        if (character == '[') {
            inClass = true;
            output += character;
            ++index;
            continue;
        }
        if (character == '.' && (result.flags & DotAll) != 0) {
            output += "[\\s\\S]";
            ++index;
            continue;
        }
        if (character == '(') {
            if (raw.compare(index, 4, "(?P<") == 0) {
                const std::size_t close = raw.find('>', index + 4);
                if (close == std::string::npos) {
                    result.ok = false;
                    result.error = "unterminated named group";
                    return result;
                }
                const std::string name =
                    raw.substr(index + 4, close - index - 4);
                ++groupIndex;
                result.names.emplace_back(name, groupIndex);
                output += '(';
                index = close + 1;
                continue;
            }
            if (raw.compare(index, 4, "(?P=") == 0) {
                const std::size_t close = raw.find(')', index + 4);
                if (close == std::string::npos) {
                    result.ok = false;
                    result.error = "unterminated named backreference";
                    return result;
                }
                const std::string name =
                    raw.substr(index + 4, close - index - 4);
                int target = -1;
                for (const auto& entry : result.names) {
                    if (entry.first == name) {
                        target = entry.second;
                    }
                }
                if (target < 0) {
                    result.ok = false;
                    result.error = "unknown group name '" + name + "'";
                    return result;
                }
                output += "\\" + std::to_string(target);
                index = close + 1;
                continue;
            }
            if (raw.compare(index, 3, "(?<=") == 0 ||
                raw.compare(index, 3, "(?<!") == 0) {
                result.ok = false;
                result.error = "lookbehind is not supported by std::regex";
                return result;
            }
            if (raw.compare(index, 3, "(?>") == 0) {
                result.ok = false;
                result.error = "atomic groups are not supported by std::regex";
                return result;
            }
            if (raw.compare(index, 2, "(?") == 0 &&
                index + 2 < raw.size()) {
                // Inline flag group such as (?i), (?im), (?s:...).
                std::size_t cursor = index + 2;
                while (cursor < raw.size()) {
                    const char flag = raw[cursor];
                    if (flag == 'i') {
                        result.flags |= IgnoreCase;
                        ++cursor;
                        continue;
                    }
                    if (flag == 'm') {
                        result.flags |= Multiline;
                        ++cursor;
                        continue;
                    }
                    if (flag == 's') {
                        result.flags |= DotAll;
                        ++cursor;
                        continue;
                    }
                    if (flag == 'a' || flag == 'L' || flag == 'u' ||
                        flag == 'x') {
                        ++cursor;
                        continue;
                    }
                    break;
                }
                if (cursor < raw.size() && raw[cursor] == ')') {
                    index = cursor + 1;
                    continue;
                }
                if (cursor < raw.size() && raw[cursor] == ':') {
                    // Scoped flags: approximate by applying globally.
                    output += "(?:";
                    index = cursor + 1;
                    continue;
                }
            }
            if (raw.compare(index, 2, "(?") == 0) {
                // Non-capturing construct: pass through verbatim.
                output += '(';
                ++index;
                continue;
            }
            ++groupIndex;
            output += '(';
            ++index;
            continue;
        }
        output += character;
        ++index;
    }
    result.pattern = output;
    return result;
}

inline bool compilePattern(const std::string& raw, int flags, Compiled& out,
                           std::string& error) {
    Translation translation = translatePattern(raw, flags);
    if (!translation.ok) {
        error = translation.error;
        return false;
    }
    flags = translation.flags;
    auto options = std::regex::ECMAScript;
    if ((flags & IgnoreCase) != 0) {
        options |= std::regex::icase;
    }
    if ((flags & Multiline) != 0) {
        options |= std::regex::multiline;
    }
    try {
        out.expression = std::regex(translation.pattern, options);
    } catch (const std::regex_error& exception) {
        error = exception.what();
        return false;
    }
    out.names = std::move(translation.names);
    return true;
}

inline std::string translateReplacement(
    const std::string& replacement,
    const std::vector<std::pair<std::string, int>>& names) {
    std::string output;
    std::size_t index = 0;
    while (index < replacement.size()) {
        const char character = replacement[index];
        if (character == '$') {
            output += "$$";
            ++index;
            continue;
        }
        if (character != '\\') {
            output += character;
            ++index;
            continue;
        }
        if (index + 1 >= replacement.size()) {
            output += '\\';
            ++index;
            continue;
        }
        const char next = replacement[index + 1];
        if (isEscapableDigit(next)) {
            output += '$';
            ++index;
            while (index < replacement.size() &&
                   isEscapableDigit(replacement[index])) {
                output += replacement[index];
                ++index;
            }
            continue;
        }
        if (next == 'g' && index + 2 < replacement.size() &&
            replacement[index + 2] == '<') {
            const std::size_t close = replacement.find('>', index + 3);
            if (close != std::string::npos) {
                const std::string name =
                    replacement.substr(index + 3, close - index - 3);
                int target = -1;
                for (const auto& entry : names) {
                    if (entry.first == name) {
                        target = entry.second;
                    }
                }
                if (target >= 0) {
                    output += '$' + std::to_string(target);
                } else {
                    output += '$' + name;
                }
                index = close + 1;
                continue;
            }
        }
        switch (next) {
            case 'n': output += '\n'; break;
            case 't': output += '\t'; break;
            case 'r': output += '\r'; break;
            case '\\': output += '\\'; break;
            default: output += next; break;
        }
        index += 2;
    }
    return output;
}

inline std::string groupText(const std::smatch& match, std::size_t index) {
    if (index >= match.size() || !match[index].matched) {
        return "";
    }
    return match[index].str();
}

struct PreparedMatch {
    bool ok = false;
    Compiled compiled;
    std::smatch match;
};

inline bool searchFirst(const std::string& raw, int flags,
                        const std::string& subject, PreparedMatch& out) {
    std::string error;
    if (!compilePattern(raw, flags, out.compiled, error)) {
        return false;
    }
    out.ok = std::regex_search(subject, out.match, out.compiled.expression);
    return true;
}

}  // namespace native_regex

#endif  // LYNXER_NATIVE_REGEX_HPP
