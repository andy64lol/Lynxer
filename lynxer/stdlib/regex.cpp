// Lynxer `regex` stdlib backend: extended helpers with a named compiled-pattern
// cache. Behaves like the Python reference's `re` fallback path (the third
// party `regex` package is not available, so Unicode `\p{...}` helpers fall
// back to ASCII classes and overlapping matching is emulated).

#include "native_json.hpp"
#include "native_regex.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <regex>
#include <string>
#include <vector>

using RegisterFunction = int (*)(const char*, const char*, const char*);
using RegisterConstant = int (*)(const char*, std::int64_t);
using RegisterType = int (*)(const char*, const char*);

using native_json::Value;
using native_regex::Compiled;

static const char* stable(std::string value) {
    thread_local std::string result;
    result = std::move(value);
    return result.c_str();
}

static std::string textOrEmpty(const char* text) {
    return text == nullptr ? std::string() : std::string(text);
}

static bool compile(const std::string& pattern, int flags, Compiled& out) {
    std::string error;
    return native_regex::compilePattern(pattern, flags, out, error);
}

static int flagsFromText(const std::string& flags) {
    int result = native_regex::NoFlags;
    for (const char character : flags) {
        switch (character) {
            case 'I': case 'i': result |= native_regex::IgnoreCase; break;
            case 'M': case 'm': result |= native_regex::Multiline; break;
            case 'S': case 's': result |= native_regex::DotAll; break;
            default: break;
        }
    }
    return result;
}

static std::map<std::string, Compiled>& cache() {
    static std::map<std::string, Compiled> instance;
    return instance;
}

static std::mutex& cacheMutex() {
    static std::mutex instance;
    return instance;
}

static bool findCached(const std::string& name, Compiled& out) {
    std::lock_guard<std::mutex> guard(cacheMutex());
    const auto found = cache().find(name);
    if (found == cache().end()) {
        return false;
    }
    out = found->second;
    return true;
}

static std::string opExtract(const std::string& pattern,
                             const std::string& subject) {
    Compiled compiled;
    if (!compile(pattern, native_regex::NoFlags, compiled)) {
        return "{}";
    }
    std::smatch match;
    if (!std::regex_search(subject, match, compiled.expression)) {
        return "{}";
    }
    Value object = native_json::makeObject();
    for (const auto& entry : compiled.names) {
        const std::size_t index = static_cast<std::size_t>(entry.second);
        if (index < match.size() && match[index].matched) {
            native_json::setField(object, entry.first,
                                  native_json::makeString(match[index].str()));
        } else {
            native_json::setField(object, entry.first,
                                  native_json::makeNull());
        }
    }
    return native_json::dump(object, false);
}

static std::string opExtractAll(const std::string& pattern,
                                const std::string& subject) {
    Compiled compiled;
    if (!compile(pattern, native_regex::NoFlags, compiled)) {
        return "[]";
    }
    Value rows = native_json::makeArray();
    for (auto iterator =
             std::sregex_iterator(subject.begin(), subject.end(),
                                  compiled.expression);
         iterator != std::sregex_iterator(); ++iterator) {
        const std::smatch& match = *iterator;
        Value row = native_json::makeObject();
        for (const auto& entry : compiled.names) {
            const std::size_t index = static_cast<std::size_t>(entry.second);
            if (index < match.size() && match[index].matched) {
                native_json::setField(
                    row, entry.first,
                    native_json::makeString(match[index].str()));
            } else {
                native_json::setField(row, entry.first,
                                      native_json::makeNull());
            }
        }
        rows.items.push_back(std::move(row));
    }
    return native_json::dump(rows, false);
}

static std::string opUnique(const std::string& pattern,
                            const std::string& subject) {
    Compiled compiled;
    if (!compile(pattern, native_regex::NoFlags, compiled)) {
        return "[]";
    }
    std::vector<std::string> seen;
    for (auto iterator =
             std::sregex_iterator(subject.begin(), subject.end(),
                                  compiled.expression);
         iterator != std::sregex_iterator(); ++iterator) {
        const std::string matched = iterator->str();
        if (std::find(seen.begin(), seen.end(), matched) == seen.end()) {
            seen.push_back(matched);
        }
    }
    Value array = native_json::makeArray();
    for (const auto& entry : seen) {
        array.items.push_back(native_json::makeString(entry));
    }
    return native_json::dump(array, false);
}

static std::string opLastMatch(const std::string& pattern,
                               const std::string& subject) {
    Compiled compiled;
    if (!compile(pattern, native_regex::NoFlags, compiled)) {
        return "";
    }
    std::string result;
    for (auto iterator =
             std::sregex_iterator(subject.begin(), subject.end(),
                                  compiled.expression);
         iterator != std::sregex_iterator(); ++iterator) {
        result = iterator->str();
    }
    return result;
}

static std::string opFindAllOverlapping(const std::string& pattern,
                                        const std::string& subject) {
    Compiled compiled;
    if (!compile(pattern, native_regex::NoFlags, compiled)) {
        return "[]";
    }
    Value array = native_json::makeArray();
    auto start = subject.cbegin();
    const auto end = subject.cend();
    while (start <= end) {
        auto options = std::regex_constants::match_default;
        if (start != subject.cbegin()) {
            options |= std::regex_constants::match_prev_avail;
        }
        std::smatch match;
        if (!std::regex_search(start, end, match, compiled.expression,
                               options)) {
            break;
        }
        array.items.push_back(native_json::makeString(match.str()));
        if (match[0].first == end) {
            break;
        }
        start = match[0].first + 1;
    }
    return native_json::dump(array, false);
}

// Shared manual substitution loop used by replaceNth, replaceAllLiteral and
// highlight. Highlight keeps the matched text between `before` and `after`;
// Replace substitutes `replacement` verbatim (no backreference expansion).
enum class ManualMode { Replace, Highlight };

static std::string replaceManual(const std::string& subject,
                                 const std::regex& expression, int nth,
                                 const std::string& before,
                                 const std::string& replacement,
                                 const std::string& after,
                                 ManualMode mode) {
    std::string output;
    auto begin = subject.cbegin();
    const auto end = subject.cend();
    std::smatch match;
    int index = 0;
    while (true) {
        auto options = std::regex_constants::match_default;
        if (begin != subject.cbegin()) {
            options |= std::regex_constants::match_prev_avail;
        }
        if (!std::regex_search(begin, end, match, expression, options)) {
            break;
        }
        output.append(begin, match[0].first);
        ++index;
        if (nth < 0 || index == nth) {
            output += before;
            output += mode == ManualMode::Highlight ? match.str()
                                                    : replacement;
            output += after;
        } else {
            output.append(match[0].first, match[0].second);
        }
        begin = match[0].second;
        if (match.length(0) == 0) {
            if (begin == end) {
                break;
            }
            output += *begin;
            ++begin;
        }
    }
    output.append(begin, end);
    return output;
}

static std::string opReplaceNth(const std::string& pattern,
                                const std::string& replacement,
                                const std::string& subject, int nth) {
    Compiled compiled;
    if (!compile(pattern, native_regex::NoFlags, compiled)) {
        return subject;
    }
    return replaceManual(subject, compiled.expression, nth, "", replacement, "",
                         ManualMode::Replace);
}

static std::string opReplaceAllLiteral(const std::string& pattern,
                                       const std::string& replacement,
                                       const std::string& subject) {
    Compiled compiled;
    if (!compile(pattern, native_regex::NoFlags, compiled)) {
        return subject;
    }
    return replaceManual(subject, compiled.expression, -1, "", replacement, "",
                         ManualMode::Replace);
}

static std::string opHighlight(const std::string& pattern,
                               const std::string& before,
                               const std::string& after,
                               const std::string& subject) {
    Compiled compiled;
    if (!compile(pattern, native_regex::NoFlags, compiled)) {
        return subject;
    }
    return replaceManual(subject, compiled.expression, -1, before, "", after,
                         ManualMode::Highlight);
}

static std::string escapeLiteral(char character) {
    static const char* kSpecial = "()[]{}?*+-|^$\\.&~# \t\n\r\v\f";
    std::string output;
    if (std::strchr(kSpecial, character) != nullptr) {
        output += '\\';
    }
    output += character;
    return output;
}

static std::string opGlobToRegex(const std::string& glob) {
    std::string escaped;
    std::size_t index = 0;
    while (index < glob.size()) {
        const char character = glob[index];
        if (character == '*') {
            escaped += ".*";
        } else if (character == '?') {
            escaped += ".";
        } else if (character == '[') {
            const std::size_t close = glob.find(']', index);
            if (close != std::string::npos) {
                escaped += glob.substr(index, close - index + 1);
                index = close + 1;
                continue;
            }
            escaped += escapeLiteral(character);
        } else {
            escaped += escapeLiteral(character);
        }
        ++index;
    }
    return "^" + escaped + "$";
}

static std::string opTruncateMatch(const std::string& pattern,
                                   const std::string& subject, int maxLen) {
    Compiled compiled;
    if (!compile(pattern, native_regex::NoFlags, compiled)) {
        return subject.substr(0, static_cast<std::size_t>(
                                     std::max(0, maxLen)));
    }
    std::smatch match;
    if (!std::regex_search(subject, match, compiled.expression)) {
        return subject.substr(
            0, static_cast<std::size_t>(std::max(0, maxLen)));
    }
    const int half = maxLen / 2;
    const int start =
        std::max(0, static_cast<int>(match.position(0)) - half);
    const int end = std::min(static_cast<int>(subject.size()),
                             start + std::max(0, maxLen));
    if (end <= start) {
        return "";
    }
    return subject.substr(static_cast<std::size_t>(start),
                          static_cast<std::size_t>(end - start));
}

extern "C" std::int64_t regex_compile(const char* name, const char* pattern,
                                      const char* flags) {
    Compiled compiled;
    if (!compile(textOrEmpty(pattern), flagsFromText(textOrEmpty(flags)),
                 compiled)) {
        return 0;
    }
    std::lock_guard<std::mutex> guard(cacheMutex());
    cache()[textOrEmpty(name)] = std::move(compiled);
    return 1;
}

extern "C" std::int64_t regex_testCompiled(const char* name,
                                           const char* subject) {
    Compiled compiled;
    if (!findCached(textOrEmpty(name), compiled)) {
        return 0;
    }
    const std::string input = textOrEmpty(subject);
    return std::regex_search(input, compiled.expression) ? 1 : 0;
}

extern "C" const char* regex_matchCompiled(const char* name,
                                           const char* subject) {
    Compiled compiled;
    if (!findCached(textOrEmpty(name), compiled)) {
        return stable("");
    }
    const std::string input = textOrEmpty(subject);
    std::smatch match;
    if (!std::regex_search(input, match, compiled.expression)) {
        return stable("");
    }
    return stable(match.str());
}

extern "C" const char* regex_findallCompiled(const char* name,
                                             const char* subject) {
    Compiled compiled;
    if (!findCached(textOrEmpty(name), compiled)) {
        return stable("[]");
    }
    const std::string input = textOrEmpty(subject);
    Value array = native_json::makeArray();
    const std::size_t groups = compiled.expression.mark_count();
    for (auto iterator =
             std::sregex_iterator(input.begin(), input.end(),
                                  compiled.expression);
         iterator != std::sregex_iterator(); ++iterator) {
        const std::smatch& match = *iterator;
        if (groups == 0) {
            array.items.push_back(native_json::makeString(match.str()));
        } else if (groups == 1) {
            array.items.push_back(
                native_json::makeString(native_regex::groupText(match, 1)));
        } else {
            Value row = native_json::makeArray();
            for (std::size_t index = 1; index <= groups; ++index) {
                row.items.push_back(native_json::makeString(
                    native_regex::groupText(match, index)));
            }
            array.items.push_back(std::move(row));
        }
    }
    return stable(native_json::dump(array, false));
}

extern "C" const char* regex_subCompiled(const char* name,
                                         const char* replacement,
                                         const char* subject) {
    Compiled compiled;
    const std::string input = textOrEmpty(subject);
    if (!findCached(textOrEmpty(name), compiled)) {
        return stable(input);
    }
    const std::string format = native_regex::translateReplacement(
        textOrEmpty(replacement), compiled.names);
    std::string output;
    auto begin = input.cbegin();
    const auto end = input.cend();
    std::smatch match;
    while (true) {
        auto options = std::regex_constants::match_default;
        if (begin != input.cbegin()) {
            options |= std::regex_constants::match_prev_avail;
        }
        if (!std::regex_search(begin, end, match, compiled.expression,
                               options)) {
            break;
        }
        output.append(begin, match[0].first);
        output.append(match.format(format));
        begin = match[0].second;
        if (match.length(0) == 0) {
            if (begin == end) {
                break;
            }
            output += *begin;
            ++begin;
        }
    }
    output.append(begin, end);
    return stable(std::move(output));
}

extern "C" std::int64_t regex_clearCache() {
    std::lock_guard<std::mutex> guard(cacheMutex());
    cache().clear();
    return 0;
}

extern "C" const char* regex_cacheKeys() {
    Value array = native_json::makeArray();
    {
        std::lock_guard<std::mutex> guard(cacheMutex());
        for (const auto& entry : cache()) {
            array.items.push_back(native_json::makeString(entry.first));
        }
    }
    return stable(native_json::dump(array, false));
}

extern "C" std::int64_t regex_isValid(const char* pattern) {
    Compiled compiled;
    return compile(textOrEmpty(pattern), native_regex::NoFlags, compiled) ? 1
                                                                          : 0;
}

extern "C" const char* regex_extract(const char* pattern,
                                     const char* subject) {
    return stable(opExtract(textOrEmpty(pattern), textOrEmpty(subject)));
}

extern "C" const char* regex_extractAll(const char* pattern,
                                        const char* subject) {
    return stable(opExtractAll(textOrEmpty(pattern), textOrEmpty(subject)));
}

extern "C" const char* regex_unique(const char* pattern,
                                    const char* subject) {
    return stable(opUnique(textOrEmpty(pattern), textOrEmpty(subject)));
}

extern "C" const char* regex_lastMatch(const char* pattern,
                                       const char* subject) {
    return stable(opLastMatch(textOrEmpty(pattern), textOrEmpty(subject)));
}

extern "C" const char* regex_findallOverlapping(const char* pattern,
                                                const char* subject) {
    return stable(
        opFindAllOverlapping(textOrEmpty(pattern), textOrEmpty(subject)));
}

extern "C" const char* regex_replaceNth(const char* pattern,
                                        const char* replacement,
                                        const char* subject,
                                        std::int64_t nth) {
    return stable(opReplaceNth(textOrEmpty(pattern), textOrEmpty(replacement),
                               textOrEmpty(subject),
                               static_cast<int>(nth)));
}

extern "C" const char* regex_replaceAllLiteral(const char* pattern,
                                               const char* replacement,
                                               const char* subject) {
    return stable(opReplaceAllLiteral(textOrEmpty(pattern),
                                      textOrEmpty(replacement),
                                      textOrEmpty(subject)));
}

extern "C" const char* regex_highlight(const char* pattern, const char* before,
                                       const char* after,
                                       const char* subject) {
    return stable(opHighlight(textOrEmpty(pattern), textOrEmpty(before),
                              textOrEmpty(after), textOrEmpty(subject)));
}

extern "C" const char* regex_splitKeep(const char* pattern,
                                       const char* subject) {
    Compiled compiled;
    if (!compile("(" + textOrEmpty(pattern) + ")", native_regex::NoFlags,
                 compiled)) {
        return stable("[]");
    }
    const std::string input = textOrEmpty(subject);
    Value array = native_json::makeArray();
    auto searchFrom = input.cbegin();
    const auto end = input.cend();
    auto segmentStart = input.cbegin();
    std::smatch match;
    while (true) {
        auto options = std::regex_constants::match_default;
        if (searchFrom != input.cbegin()) {
            options |= std::regex_constants::match_prev_avail;
        }
        if (!std::regex_search(searchFrom, end, match, compiled.expression,
                               options)) {
            break;
        }
        array.items.push_back(native_json::makeString(
            std::string(segmentStart, match[0].first)));
        for (std::size_t index = 1; index < match.size(); ++index) {
            array.items.push_back(native_json::makeString(
                native_regex::groupText(match, index)));
        }
        segmentStart = match[0].second;
        searchFrom = match[0].second;
        if (match.length(0) == 0) {
            if (searchFrom == end) {
                break;
            }
            ++searchFrom;
        }
    }
    array.items.push_back(
        native_json::makeString(std::string(segmentStart, end)));
    return stable(native_json::dump(array, false));
}

static std::string findallPattern(const std::string& pattern,
                                  const std::string& subject) {
    Compiled compiled;
    if (!compile(pattern, native_regex::NoFlags, compiled)) {
        return "[]";
    }
    Value array = native_json::makeArray();
    for (auto iterator =
             std::sregex_iterator(subject.begin(), subject.end(),
                                  compiled.expression);
         iterator != std::sregex_iterator(); ++iterator) {
        array.items.push_back(
            native_json::makeString(iterator->str()));
    }
    return native_json::dump(array, false);
}

extern "C" const char* regex_findLetters(const char* subject) {
    return stable(findallPattern("[a-zA-Z]+", textOrEmpty(subject)));
}

extern "C" const char* regex_findDigits(const char* subject) {
    return stable(findallPattern("\\d+", textOrEmpty(subject)));
}

extern "C" const char* regex_globToRegex(const char* glob) {
    return stable(opGlobToRegex(textOrEmpty(glob)));
}

extern "C" std::int64_t regex_countMatches(const char* pattern,
                                           const char* subject) {
    Compiled compiled;
    if (!compile(textOrEmpty(pattern), native_regex::NoFlags, compiled)) {
        return 0;
    }
    const std::string input = textOrEmpty(subject);
    std::int64_t total = 0;
    for (auto iterator = std::sregex_iterator(input.begin(), input.end(),
                                              compiled.expression);
         iterator != std::sregex_iterator(); ++iterator) {
        ++total;
    }
    return total;
}

extern "C" std::int64_t regex_firstMatchPos(const char* pattern,
                                            const char* subject) {
    Compiled compiled;
    if (!compile(textOrEmpty(pattern), native_regex::NoFlags, compiled)) {
        return -1;
    }
    const std::string input = textOrEmpty(subject);
    std::smatch match;
    if (!std::regex_search(input, match, compiled.expression)) {
        return -1;
    }
    return static_cast<std::int64_t>(match.position(0));
}

extern "C" const char* regex_truncateMatch(const char* pattern,
                                           const char* subject,
                                           std::int64_t maxLen) {
    return stable(opTruncateMatch(textOrEmpty(pattern), textOrEmpty(subject),
                                  static_cast<int>(maxLen)));
}

extern "C" int clynxer_module_init_v1(RegisterFunction function,
                                     RegisterConstant, RegisterType) {
    return function("compile", "regex_compile",
                            "cdecl:int64(cstring,cstring,cstring)") &&
                   function("testCompiled", "regex_testCompiled",
                            "cdecl:int64(cstring,cstring)") &&
                   function("matchCompiled", "regex_matchCompiled",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("findallCompiled", "regex_findallCompiled",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("subCompiled", "regex_subCompiled",
                            "cdecl:cstring(cstring,cstring,cstring)") &&
                   function("clearCache", "regex_clearCache", "cdecl:int64()") &&
                   function("cacheKeys", "regex_cacheKeys",
                            "cdecl:cstring()") &&
                   function("isValid", "regex_isValid",
                            "cdecl:int64(cstring)") &&
                   function("extract", "regex_extract",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("extractAll", "regex_extractAll",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("unique", "regex_unique",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("lastMatch", "regex_lastMatch",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("findallOverlapping", "regex_findallOverlapping",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("replaceNth", "regex_replaceNth",
                            "cdecl:cstring(cstring,cstring,cstring,int64)") &&
                   function("replaceAllLiteral", "regex_replaceAllLiteral",
                            "cdecl:cstring(cstring,cstring,cstring)") &&
                   function("highlight", "regex_highlight",
                            "cdecl:cstring(cstring,cstring,cstring,cstring)") &&
                   function("splitKeep", "regex_splitKeep",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("findLetters", "regex_findLetters",
                            "cdecl:cstring(cstring)") &&
                   function("findDigits", "regex_findDigits",
                            "cdecl:cstring(cstring)") &&
                   function("globToRegex", "regex_globToRegex",
                            "cdecl:cstring(cstring)") &&
                   function("countMatches", "regex_countMatches",
                            "cdecl:int64(cstring,cstring)") &&
                   function("firstMatchPos", "regex_firstMatchPos",
                            "cdecl:int64(cstring,cstring)") &&
                   function("truncateMatch", "regex_truncateMatch",
                            "cdecl:cstring(cstring,cstring,int64)")
               ? 0
               : 1;
}
