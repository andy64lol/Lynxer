// Lynxer `re` stdlib backend: Python-flavoured regular expressions on top of
// std::regex. See native_regex.hpp for the translation rules and divergences.

#include "native_json.hpp"
#include "native_regex.hpp"

#include <cstdint>
#include <cstring>
#include <regex>
#include <string>

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

static std::int64_t opTest(const std::string& pattern,
                           const std::string& subject, int flags) {
    Compiled compiled;
    if (!compile(pattern, flags, compiled)) {
        return 0;
    }
    return std::regex_search(subject, compiled.expression) ? 1 : 0;
}

static std::string opSearch(const std::string& pattern,
                            const std::string& subject, int flags) {
    Compiled compiled;
    if (!compile(pattern, flags, compiled)) {
        return "";
    }
    std::smatch match;
    if (!std::regex_search(subject, match, compiled.expression)) {
        return "";
    }
    return match.str();
}

static std::string opMatch(const std::string& pattern,
                           const std::string& subject, int flags) {
    Compiled compiled;
    if (!compile(pattern, flags, compiled)) {
        return "";
    }
    std::smatch match;
    if (!std::regex_search(subject, match, compiled.expression,
                           std::regex_constants::match_continuous)) {
        return "";
    }
    return match.str();
}

static std::string opMatchFull(const std::string& pattern,
                               const std::string& subject, int flags) {
    Compiled compiled;
    if (!compile(pattern, flags, compiled)) {
        return "";
    }
    std::smatch match;
    if (!std::regex_match(subject, match, compiled.expression)) {
        return "";
    }
    return match.str();
}

static std::string opFindAll(const std::string& pattern,
                             const std::string& subject, int flags) {
    Compiled compiled;
    if (!compile(pattern, flags, compiled)) {
        return "[]";
    }
    Value array = native_json::makeArray();
    const std::size_t groups = compiled.expression.mark_count();
    for (auto iterator =
             std::sregex_iterator(subject.begin(), subject.end(),
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
    return native_json::dump(array, false);
}

static std::int64_t opCount(const std::string& pattern,
                            const std::string& subject, int flags) {
    Compiled compiled;
    if (!compile(pattern, flags, compiled)) {
        return 0;
    }
    std::int64_t total = 0;
    for (auto iterator =
             std::sregex_iterator(subject.begin(), subject.end(),
                                  compiled.expression);
         iterator != std::sregex_iterator(); ++iterator) {
        ++total;
    }
    return total;
}

static std::string opGroups(const std::string& pattern,
                            const std::string& subject, int flags) {
    Compiled compiled;
    if (!compile(pattern, flags, compiled)) {
        return "[]";
    }
    std::smatch match;
    if (!std::regex_search(subject, match, compiled.expression)) {
        return "[]";
    }
    Value array = native_json::makeArray();
    for (std::size_t index = 1; index < match.size(); ++index) {
        array.items.push_back(
            native_json::makeString(native_regex::groupText(match, index)));
    }
    return native_json::dump(array, false);
}

static std::string opGroupsAll(const std::string& pattern,
                               const std::string& subject, int flags) {
    Compiled compiled;
    if (!compile(pattern, flags, compiled)) {
        return "[]";
    }
    Value rows = native_json::makeArray();
    for (auto iterator =
             std::sregex_iterator(subject.begin(), subject.end(),
                                  compiled.expression);
         iterator != std::sregex_iterator(); ++iterator) {
        const std::smatch& match = *iterator;
        Value row = native_json::makeArray();
        for (std::size_t index = 1; index < match.size(); ++index) {
            row.items.push_back(native_json::makeString(
                native_regex::groupText(match, index)));
        }
        rows.items.push_back(std::move(row));
    }
    return native_json::dump(rows, false);
}

static std::string opNamed(const std::string& pattern,
                           const std::string& subject, int flags) {
    Compiled compiled;
    if (!compile(pattern, flags, compiled)) {
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

static std::string replaceLimited(const std::string& subject,
                                  const std::regex& expression,
                                  const std::string& format, int limit,
                                  std::int64_t& count) {
    std::string output;
    auto begin = subject.cbegin();
    const auto end = subject.cend();
    std::smatch match;
    count = 0;
    while (limit < 0 || count < limit) {
        // match_prev_avail keeps `^` anchored to real line starts instead of
        // treating the continuation offset as the beginning of the input.
        auto options = std::regex_constants::match_default;
        if (begin != subject.cbegin()) {
            options |= std::regex_constants::match_prev_avail;
        }
        if (!std::regex_search(begin, end, match, expression, options)) {
            break;
        }
        output.append(begin, match[0].first);
        output.append(match.format(format));
        ++count;
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

static std::string opSub(const std::string& pattern,
                         const std::string& replacement,
                         const std::string& subject, int flags, int limit) {
    Compiled compiled;
    if (!compile(pattern, flags, compiled)) {
        return "";
    }
    const std::string format =
        native_regex::translateReplacement(replacement, compiled.names);
    std::int64_t count = 0;
    return replaceLimited(subject, compiled.expression, format, limit, count);
}

static std::string opSubn(const std::string& pattern,
                          const std::string& replacement,
                          const std::string& subject, int flags) {
    Compiled compiled;
    if (!compile(pattern, flags, compiled)) {
        return "{\"result\": \"\", \"count\": 0}";
    }
    const std::string format =
        native_regex::translateReplacement(replacement, compiled.names);
    std::int64_t count = 0;
    const std::string result =
        replaceLimited(subject, compiled.expression, format, -1, count);
    Value object = native_json::makeObject();
    native_json::setField(object, "result", native_json::makeString(result));
    native_json::setField(object, "count", native_json::makeInteger(count));
    return native_json::dump(object, false);
}

static std::string opSplit(const std::string& pattern,
                           const std::string& subject, int flags,
                           int maxSplit) {
    Compiled compiled;
    if (!compile(pattern, flags, compiled)) {
        return "[]";
    }
    Value array = native_json::makeArray();
    auto searchFrom = subject.cbegin();
    const auto end = subject.cend();
    auto segmentStart = subject.cbegin();
    std::smatch match;
    int splits = 0;
    while (maxSplit < 0 || splits < maxSplit) {
        auto options = std::regex_constants::match_default;
        if (searchFrom != subject.cbegin()) {
            options |= std::regex_constants::match_prev_avail;
        }
        if (!std::regex_search(searchFrom, end, match, compiled.expression,
                               options)) {
            break;
        }
        array.items.push_back(
            native_json::makeString(std::string(segmentStart, match[0].first)));
        for (std::size_t index = 1; index < match.size(); ++index) {
            array.items.push_back(native_json::makeString(
                native_regex::groupText(match, index)));
        }
        ++splits;
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
    return native_json::dump(array, false);
}

static std::int64_t opMatchStart(const std::string& pattern,
                                 const std::string& subject, int flags) {
    Compiled compiled;
    if (!compile(pattern, flags, compiled)) {
        return -1;
    }
    std::smatch match;
    if (!std::regex_search(subject, match, compiled.expression)) {
        return -1;
    }
    return static_cast<std::int64_t>(match.position(0));
}

static std::int64_t opMatchEnd(const std::string& pattern,
                               const std::string& subject, int flags) {
    Compiled compiled;
    if (!compile(pattern, flags, compiled)) {
        return -1;
    }
    std::smatch match;
    if (!std::regex_search(subject, match, compiled.expression)) {
        return -1;
    }
    return static_cast<std::int64_t>(match.position(0) + match.length(0));
}

static std::string opFindSpans(const std::string& pattern,
                               const std::string& subject, int flags) {
    Compiled compiled;
    if (!compile(pattern, flags, compiled)) {
        return "[]";
    }
    Value spans = native_json::makeArray();
    for (auto iterator =
             std::sregex_iterator(subject.begin(), subject.end(),
                                  compiled.expression);
         iterator != std::sregex_iterator(); ++iterator) {
        const std::smatch& match = *iterator;
        Value span = native_json::makeObject();
        native_json::setField(
            span, "start",
            native_json::makeInteger(
                static_cast<std::int64_t>(match.position(0))));
        native_json::setField(
            span, "end",
            native_json::makeInteger(static_cast<std::int64_t>(
                match.position(0) + match.length(0))));
        native_json::setField(span, "match",
                              native_json::makeString(match.str()));
        spans.items.push_back(std::move(span));
    }
    return native_json::dump(spans, false);
}

static std::string opEscape(const std::string& value) {
    static const char* kSpecial = "()[]{}?*+-|^$\\.&~# \t\n\r\v\f";
    std::string output;
    for (const char character : value) {
        if (std::strchr(kSpecial, character) != nullptr) {
            output += '\\';
        }
        output += character;
    }
    return output;
}

extern "C" std::int64_t re_test(const char* pattern, const char* subject) {
    return opTest(textOrEmpty(pattern), textOrEmpty(subject),
                  native_regex::NoFlags);
}

extern "C" const char* re_match(const char* pattern, const char* subject) {
    return stable(
        opMatch(textOrEmpty(pattern), textOrEmpty(subject),
                native_regex::NoFlags));
}

extern "C" const char* re_matchFull(const char* pattern, const char* subject) {
    return stable(
        opMatchFull(textOrEmpty(pattern), textOrEmpty(subject),
                    native_regex::NoFlags));
}

extern "C" const char* re_search(const char* pattern, const char* subject) {
    return stable(
        opSearch(textOrEmpty(pattern), textOrEmpty(subject),
                 native_regex::NoFlags));
}

extern "C" const char* re_findall(const char* pattern, const char* subject) {
    return stable(
        opFindAll(textOrEmpty(pattern), textOrEmpty(subject),
                  native_regex::NoFlags));
}

extern "C" std::int64_t re_count(const char* pattern, const char* subject) {
    return opCount(textOrEmpty(pattern), textOrEmpty(subject),
                   native_regex::NoFlags);
}

extern "C" const char* re_groups(const char* pattern, const char* subject) {
    return stable(
        opGroups(textOrEmpty(pattern), textOrEmpty(subject),
                 native_regex::NoFlags));
}

extern "C" const char* re_groupsAll(const char* pattern, const char* subject) {
    return stable(
        opGroupsAll(textOrEmpty(pattern), textOrEmpty(subject),
                    native_regex::NoFlags));
}

extern "C" const char* re_named(const char* pattern, const char* subject) {
    return stable(
        opNamed(textOrEmpty(pattern), textOrEmpty(subject),
                native_regex::NoFlags));
}

extern "C" const char* re_sub(const char* pattern, const char* replacement,
                              const char* subject) {
    return stable(opSub(textOrEmpty(pattern), textOrEmpty(replacement),
                        textOrEmpty(subject), native_regex::NoFlags, -1));
}

extern "C" const char* re_subN(const char* pattern, const char* replacement,
                               const char* subject, std::int64_t limit) {
    return stable(opSub(textOrEmpty(pattern), textOrEmpty(replacement),
                        textOrEmpty(subject), native_regex::NoFlags,
                        static_cast<int>(limit)));
}

extern "C" const char* re_subn(const char* pattern, const char* replacement,
                               const char* subject) {
    return stable(opSubn(textOrEmpty(pattern), textOrEmpty(replacement),
                         textOrEmpty(subject), native_regex::NoFlags));
}

extern "C" const char* re_split(const char* pattern, const char* subject) {
    return stable(
        opSplit(textOrEmpty(pattern), textOrEmpty(subject),
                native_regex::NoFlags, -1));
}

extern "C" const char* re_splitN(const char* pattern, const char* subject,
                                 std::int64_t maxSplit) {
    return stable(opSplit(textOrEmpty(pattern), textOrEmpty(subject),
                          native_regex::NoFlags,
                          static_cast<int>(maxSplit)));
}

extern "C" const char* re_escape(const char* subject) {
    return stable(opEscape(textOrEmpty(subject)));
}

extern "C" std::int64_t re_matchStart(const char* pattern,
                                      const char* subject) {
    return opMatchStart(textOrEmpty(pattern), textOrEmpty(subject),
                        native_regex::NoFlags);
}

extern "C" std::int64_t re_matchEnd(const char* pattern, const char* subject) {
    return opMatchEnd(textOrEmpty(pattern), textOrEmpty(subject),
                      native_regex::NoFlags);
}

extern "C" const char* re_findSpans(const char* pattern, const char* subject) {
    return stable(
        opFindSpans(textOrEmpty(pattern), textOrEmpty(subject),
                    native_regex::NoFlags));
}

extern "C" std::int64_t re_testIgnoreCase(const char* pattern,
                                          const char* subject) {
    return opTest(textOrEmpty(pattern), textOrEmpty(subject),
                  native_regex::IgnoreCase);
}

extern "C" const char* re_matchIgnoreCase(const char* pattern,
                                          const char* subject) {
    return stable(opMatch(textOrEmpty(pattern), textOrEmpty(subject),
                          native_regex::IgnoreCase));
}

extern "C" const char* re_searchIgnoreCase(const char* pattern,
                                           const char* subject) {
    return stable(opSearch(textOrEmpty(pattern), textOrEmpty(subject),
                           native_regex::IgnoreCase));
}

extern "C" const char* re_findallIgnoreCase(const char* pattern,
                                            const char* subject) {
    return stable(opFindAll(textOrEmpty(pattern), textOrEmpty(subject),
                            native_regex::IgnoreCase));
}

extern "C" const char* re_subIgnoreCase(const char* pattern,
                                        const char* replacement,
                                        const char* subject) {
    return stable(opSub(textOrEmpty(pattern), textOrEmpty(replacement),
                        textOrEmpty(subject), native_regex::IgnoreCase, -1));
}

extern "C" const char* re_findallMultiline(const char* pattern,
                                           const char* subject) {
    return stable(opFindAll(textOrEmpty(pattern), textOrEmpty(subject),
                            native_regex::Multiline));
}

extern "C" const char* re_subMultiline(const char* pattern,
                                       const char* replacement,
                                       const char* subject) {
    return stable(opSub(textOrEmpty(pattern), textOrEmpty(replacement),
                        textOrEmpty(subject), native_regex::Multiline, -1));
}

extern "C" const char* re_searchDotall(const char* pattern,
                                       const char* subject) {
    return stable(opSearch(textOrEmpty(pattern), textOrEmpty(subject),
                           native_regex::DotAll));
}

extern "C" int lynxer_module_init_v1(RegisterFunction function,
                                     RegisterConstant, RegisterType) {
    return function("test", "re_test", "cdecl:int64(cstring,cstring)") &&
                   function("match", "re_match",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("matchFull", "re_matchFull",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("search", "re_search",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("findall", "re_findall",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("count", "re_count",
                            "cdecl:int64(cstring,cstring)") &&
                   function("groups", "re_groups",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("groupsAll", "re_groupsAll",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("named", "re_named",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("sub", "re_sub",
                            "cdecl:cstring(cstring,cstring,cstring)") &&
                   function("subN", "re_subN",
                            "cdecl:cstring(cstring,cstring,cstring,int64)") &&
                   function("subn", "re_subn",
                            "cdecl:cstring(cstring,cstring,cstring)") &&
                   function("split", "re_split",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("splitN", "re_splitN",
                            "cdecl:cstring(cstring,cstring,int64)") &&
                   function("escape", "re_escape", "cdecl:cstring(cstring)") &&
                   function("matchStart", "re_matchStart",
                            "cdecl:int64(cstring,cstring)") &&
                   function("matchEnd", "re_matchEnd",
                            "cdecl:int64(cstring,cstring)") &&
                   function("findSpans", "re_findSpans",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("testIgnoreCase", "re_testIgnoreCase",
                            "cdecl:int64(cstring,cstring)") &&
                   function("matchIgnoreCase", "re_matchIgnoreCase",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("searchIgnoreCase", "re_searchIgnoreCase",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("findallIgnoreCase", "re_findallIgnoreCase",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("subIgnoreCase", "re_subIgnoreCase",
                            "cdecl:cstring(cstring,cstring,cstring)") &&
                   function("findallMultiline", "re_findallMultiline",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("subMultiline", "re_subMultiline",
                            "cdecl:cstring(cstring,cstring,cstring)") &&
                   function("searchDotall", "re_searchDotall",
                            "cdecl:cstring(cstring,cstring)")
               ? 0
               : 1;
}
