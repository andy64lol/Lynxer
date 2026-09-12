#include "builtins.hpp"

#include "error.hpp"

#include <algorithm>
#include <charconv>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#ifdef __linux__
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace clynxer {

namespace {

#if defined(__x86_64__)
constexpr bool kX86_64 = true;
constexpr bool kArm64 = false;
#elif defined(__aarch64__)
constexpr bool kX86_64 = false;
constexpr bool kArm64 = true;
#else
constexpr bool kX86_64 = false;
constexpr bool kArm64 = false;
#endif

[[noreturn]] void fail(const std::string& message, int line, int column) {
    throw SourceError(message, line, column);
}

using Handler = Value (*)(const std::vector<Value>& args, Environment&,
                          int line, int column);

Value none() { return Value{}; }

Value makeList(std::vector<Value> elements) {
    return std::make_shared<List>(List{std::move(elements)});
}

Value makeTuple(std::vector<Value> elements) {
    return std::make_shared<Tuple>(Tuple{std::move(elements)});
}

const std::shared_ptr<List>* asList(const Value& value) {
    return std::get_if<std::shared_ptr<List>>(&value);
}

const std::shared_ptr<Tuple>* asTuple(const Value& value) {
    return std::get_if<std::shared_ptr<Tuple>>(&value);
}

const std::vector<Value>& listElements(const Value& value) {
    return (*asList(value))->elements;
}

const std::vector<Value>& tupleElements(const Value& value) {
    return (*asTuple(value))->elements;
}

std::int64_t toInt(const Value& value, int line, int column) {
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return *integer;
    }
    if (const auto* number = std::get_if<double>(&value)) {
        return static_cast<std::int64_t>(*number);
    }
    fail("an integer value is required", line, column);
}

void requireArity(const std::vector<Value>& args, std::size_t count,
                  const char* usage, int line, int column) {
    if (args.size() != count) {
        fail(usage, line, column);
    }
}

void requireList(const std::vector<Value>& args, const char* usage, int line,
                 int column) {
    if (args.size() != 1 || asList(args[0]) == nullptr) {
        fail(usage, line, column);
    }
}

void requireTuple(const std::vector<Value>& args, const char* usage, int line,
                  int column) {
    if (args.size() != 1 || asTuple(args[0]) == nullptr) {
        fail(usage, line, column);
    }
}

bool isIntegerValue(const Value& value) {
    return std::holds_alternative<std::int64_t>(value);
}

// Python-style slicing with negative indices and clamping.
std::vector<Value> pythonSlice(const std::vector<Value>& elements,
                               std::int64_t start, std::int64_t stop) {
    const std::int64_t size = static_cast<std::int64_t>(elements.size());
    std::int64_t from = start < 0 ? std::max<std::int64_t>(0, size + start)
                                   : std::min<std::int64_t>(start, size);
    std::int64_t to = stop < 0 ? std::max<std::int64_t>(0, size + stop)
                                : std::min<std::int64_t>(stop, size);
    if (from >= to) {
        return {};
    }
    return std::vector<Value>(elements.begin() + from, elements.begin() + to);
}

// --- sorting -----------------------------------------------------------------

// 0 = number, 1 = string, 2 = anything else (compared by string form).
int sortCategory(const Value& value) {
    if (isNumber(value)) {
        return 0;
    }
    if (std::holds_alternative<std::string>(value)) {
        return 1;
    }
    return 2;
}

bool sortLess(const Value& left, const Value& right, int line, int column) {
    const int leftCategory = sortCategory(left);
    const int rightCategory = sortCategory(right);
    if (leftCategory != rightCategory) {
        fail("sortList() failed: '<' not supported between values of type '" +
                 typeNameOf(left) + "' and '" + typeNameOf(right) + "'",
             line, column);
    }
    if (leftCategory == 0) {
        return asNumber(left, line, column) < asNumber(right, line, column);
    }
    return valueToString(left) < valueToString(right);
}

// --- JSON --------------------------------------------------------------------

std::string jsonEscape(const std::string& text) {
    std::string output;
    for (const char character : text) {
        switch (character) {
        case '"':
            output += "\\\"";
            break;
        case '\\':
            output += "\\\\";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(character) < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", character);
                output += buffer;
            } else {
                output += character;
            }
            break;
        }
    }
    return output;
}

std::string jsonDouble(double number) {
    if (std::isnan(number)) {
        return "NaN";
    }
    if (std::isinf(number)) {
        return number > 0 ? "Infinity" : "-Infinity";
    }
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), number);
    std::string text(buffer, result.ptr);
    if (text.find('.') == std::string::npos &&
        text.find('e') == std::string::npos) {
        text += ".0";
    }
    return text;
}

std::string jsonString(const std::string& text) {
    return "\"" + jsonEscape(text) + "\"";
}

std::string jsonValue(const Value& value) {
    if (std::holds_alternative<std::monostate>(value)) {
        return "null";
    }
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return std::to_string(*integer);
    }
    if (const auto* number = std::get_if<double>(&value)) {
        return jsonDouble(*number);
    }
    if (const auto* boolean = std::get_if<bool>(&value)) {
        return *boolean ? "true" : "false";
    }
    if (const auto* text = std::get_if<std::string>(&value)) {
        return jsonString(*text);
    }
    if (asList(value) != nullptr) {
        std::string output = "[";
        const auto& elements = listElements(value);
        for (std::size_t index = 0; index < elements.size(); ++index) {
            if (index != 0) {
                output += ", ";
            }
            output += jsonValue(elements[index]);
        }
        return output + "]";
    }
    if (asTuple(value) != nullptr) {
        std::string output = "[";
        const auto& elements = tupleElements(value);
        for (std::size_t index = 0; index < elements.size(); ++index) {
            if (index != 0) {
                output += ", ";
            }
            output += jsonValue(elements[index]);
        }
        return output + "]";
    }
    return jsonString(valueToString(value));
}

// Python str() of a JSON-compatible value, used for object keys.
std::string jsonKeyString(const Value& value) {
    if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value) ? "True" : "False";
    }
    if (std::holds_alternative<std::monostate>(value)) {
        return "None";
    }
    return valueToString(value);
}

std::string jsonPairObject(const Value& left, const Value& right) {
    return "{\"a\": " + jsonValue(left) + ", \"b\": " + jsonValue(right) + "}";
}

// --- I/O ---------------------------------------------------------------------

Value builtinPrint(const std::vector<Value>& args, Environment&, int, int) {
    std::string output;
    for (const auto& argument : args) {
        output += valueToString(argument);
    }
    std::cout << output;
    std::cout.flush();
    return none();
}

Value builtinPrintln(const std::vector<Value>& args, Environment&, int, int) {
    std::string output;
    for (const auto& argument : args) {
        output += valueToString(argument);
    }
    std::cout << output << '\n';
    std::cout.flush();
    return none();
}

std::string readLine(int line, int column) {
    std::string text;
    if (!std::getline(std::cin, text)) {
        if (std::cin.eof()) {
            fail("input(): end of input while reading stdin", line, column);
        }
        fail("input(): unable to read from stdin", line, column);
    }
    return text;
}

Value builtinInput(const std::vector<Value>& args, Environment&, int line,
                   int column) {
    if (args.size() > 1) {
        fail("input() takes 0 or 1 arguments", line, column);
    }
    if (!args.empty()) {
        std::cout << valueToString(args[0]);
        std::cout.flush();
    }
    return readLine(line, column);
}

Value builtinInputln(const std::vector<Value>& args, Environment&, int line,
                     int column) {
    if (args.size() > 1) {
        fail("inputln() takes 0 or 1 arguments", line, column);
    }
    if (!args.empty()) {
        std::cout << valueToString(args[0]);
        std::cout.flush();
    }
    return readLine(line, column) + "\n";
}

// --- conversion and introspection --------------------------------------------

double parseLooseDouble(const std::string& text, bool& ok) {
    ok = false;
    const char* begin = text.c_str();
    char* end = nullptr;
    errno = 0;
    const double value = std::strtod(begin, &end);
    while (end != nullptr && *end != '\0' &&
           std::isspace(static_cast<unsigned char>(*end))) {
        ++end;
    }
    if (end == begin || (end != nullptr && *end != '\0')) {
        return 0.0;
    }
    ok = true;
    return value;
}

Value builtinStrOf(const std::vector<Value>& args, Environment&, int line,
                   int column) {
    requireArity(args, 1, "strOf() takes exactly 1 argument", line, column);
    return valueToString(args[0]);
}

Value builtinIntOf(const std::vector<Value>& args, Environment&, int line,
                   int column) {
    requireArity(args, 1, "intOf() takes exactly 1 argument", line, column);
    bool ok = false;
    double parsed = 0.0;
    if (const auto* integer = std::get_if<std::int64_t>(&args[0])) {
        return *integer;
    }
    if (const auto* number = std::get_if<double>(&args[0])) {
        return static_cast<std::int64_t>(*number);
    }
    if (const auto* boolean = std::get_if<bool>(&args[0])) {
        return *boolean ? std::int64_t{1} : std::int64_t{0};
    }
    if (const auto* text = std::get_if<std::string>(&args[0])) {
        parsed = parseLooseDouble(*text, ok);
    }
    if (!ok) {
        fail("Cannot convert '" + valueToString(args[0]) + "' to int", line,
             column);
    }
    return static_cast<std::int64_t>(parsed);
}

Value builtinFloatOf(const std::vector<Value>& args, Environment&, int line,
                     int column) {
    requireArity(args, 1, "floatOf() takes exactly 1 argument", line, column);
    bool ok = false;
    double parsed = 0.0;
    if (const auto* integer = std::get_if<std::int64_t>(&args[0])) {
        return static_cast<double>(*integer);
    }
    if (const auto* number = std::get_if<double>(&args[0])) {
        return *number;
    }
    if (const auto* boolean = std::get_if<bool>(&args[0])) {
        return *boolean ? 1.0 : 0.0;
    }
    if (const auto* text = std::get_if<std::string>(&args[0])) {
        parsed = parseLooseDouble(*text, ok);
    }
    if (!ok) {
        fail("Cannot convert '" + valueToString(args[0]) + "' to float", line,
             column);
    }
    return parsed;
}

Value builtinSentinel(const std::vector<Value>& args, Environment&, int line,
                      int column) {
    if (args.size() > 1 || (!args.empty() &&
                            !std::holds_alternative<std::string>(args[0]))) {
        fail("sentinel() expects zero or one string argument — "
             "sentinel(\"NAME\")",
             line, column);
    }
    const std::string name = args.empty() ? "" : std::get<std::string>(args[0]);
    return std::make_shared<SentinelValue>(SentinelValue{name});
}

Value builtinObject(const std::vector<Value>& args, Environment&, int line,
                    int column) {
    if (!args.empty()) {
        fail("object() takes no arguments", line, column);
    }
    return std::make_shared<ObjectValue>(ObjectValue{});
}

Value builtinReturnType(const std::vector<Value>& args, Environment&, int line,
                        int column) {
    requireArity(args, 1, "returnType() takes exactly 1 argument", line,
                 column);
    return typeNameOf(args[0]);
}

Value builtinReturnLength(const std::vector<Value>& args, Environment&, int line,
                          int column) {
    requireArity(args, 1, "returnLength() takes exactly 1 argument", line,
                 column);
    if (const auto* text = std::get_if<std::string>(&args[0])) {
        return static_cast<std::int64_t>(text->size());
    }
    if (asList(args[0]) != nullptr) {
        return static_cast<std::int64_t>(listElements(args[0]).size());
    }
    if (asTuple(args[0]) != nullptr) {
        return static_cast<std::int64_t>(tupleElements(args[0]).size());
    }
    fail("returnLength() does not support values of type '" +
             typeNameOf(args[0]) + "'",
         line, column);
}

// --- sequences ----------------------------------------------------------------

Value builtinRange(const std::vector<Value>& args, Environment&, int line,
                   int column) {
    if (args.empty() || args.size() > 3) {
        fail("range() expects 1, 2, or 3 integer arguments: range(stop), "
             "range(start, stop), or range(start, stop, step)",
             line, column);
    }
    for (const auto& argument : args) {
        if (!isNumber(argument)) {
            fail("range() expects 1, 2, or 3 integer arguments: range(stop), "
                 "range(start, stop), or range(start, stop, step)",
                 line, column);
        }
    }
    std::int64_t start = 0;
    std::int64_t stop = 0;
    std::int64_t step = 1;
    if (args.size() == 1) {
        stop = toInt(args[0], line, column);
    } else if (args.size() == 2) {
        start = toInt(args[0], line, column);
        stop = toInt(args[1], line, column);
    } else {
        start = toInt(args[0], line, column);
        stop = toInt(args[1], line, column);
        step = toInt(args[2], line, column);
    }
    if (step == 0) {
        fail("range() step cannot be 0", line, column);
    }
    std::vector<Value> elements;
    for (std::int64_t value = start;
         step > 0 ? value < stop : value > stop; value += step) {
        elements.push_back(value);
    }
    return makeList(std::move(elements));
}

Value builtinSeqFromTo(const std::vector<Value>& args, Environment&, int line,
                       int column) {
    if (args.size() != 3 || !isNumber(args[0]) || !isNumber(args[1]) ||
        !isNumber(args[2])) {
        fail("seqFromTo() expects exactly 3 numeric arguments — "
             "seqFromTo(start, stop, step)",
             line, column);
    }
    const std::int64_t start = toInt(args[0], line, column);
    const std::int64_t stop = toInt(args[1], line, column);
    const std::int64_t step = toInt(args[2], line, column);
    if (step == 0) {
        fail("seqFromTo() step cannot be 0", line, column);
    }
    std::vector<Value> elements;
    for (std::int64_t value = start;
         step > 0 ? value < stop : value > stop; value += step) {
        elements.push_back(value);
    }
    return makeList(std::move(elements));
}

// --- list operations -----------------------------------------------------------

Value builtinListJsonArray(const std::vector<Value>& args, Environment&, int line,
                           int column) {
    if (args.size() != 1 || asList(args[0]) == nullptr) {
        fail("listJsonArray(list) expects a list", line, column);
    }
    return jsonValue(args[0]);
}

Value builtinListJsonObject(const std::vector<Value>& args, Environment&, int line,
                            int column) {
    if (args.size() != 1 || asList(args[0]) == nullptr) {
        fail("listJsonObject(list) expects a flat key/value list", line, column);
    }
    const auto& elements = listElements(args[0]);
    if (elements.size() % 2 != 0) {
        fail("listJsonObject() requires an even-length list "
             "(key, value, key, value, ...)",
             line, column);
    }
    std::string output = "{";
    for (std::size_t index = 0; index < elements.size(); index += 2) {
        if (index != 0) {
            output += ", ";
        }
        output += jsonString(jsonKeyString(elements[index])) + ": " +
                  jsonValue(elements[index + 1]);
    }
    return output + "}";
}

Value builtinSplitStr(const std::vector<Value>& args, Environment&, int line,
                      int column) {
    if (args.size() != 2 || !std::holds_alternative<std::string>(args[0]) ||
        !std::holds_alternative<std::string>(args[1])) {
        fail("splitStr(str, sep) expects two string arguments", line, column);
    }
    const std::string& text = std::get<std::string>(args[0]);
    const std::string& separator = std::get<std::string>(args[1]);
    if (separator.empty()) {
        fail("splitStr() separator cannot be empty", line, column);
    }
    std::vector<Value> parts;
    std::size_t begin = 0;
    for (;;) {
        const std::size_t found = text.find(separator, begin);
        if (found == std::string::npos) {
            parts.push_back(text.substr(begin));
            break;
        }
        parts.push_back(text.substr(begin, found - begin));
        begin = found + separator.size();
    }
    return makeList(std::move(parts));
}

Value builtinListFlatten(const std::vector<Value>& args, Environment&, int line,
                         int column) {
    requireList(args, "listFlatten(list) expects a list", line, column);
    std::vector<Value> flat;
    for (const auto& element : listElements(args[0])) {
        if (asList(element) != nullptr) {
            const auto& inner = listElements(element);
            flat.insert(flat.end(), inner.begin(), inner.end());
        } else {
            flat.push_back(element);
        }
    }
    return makeList(std::move(flat));
}

Value builtinListUnique(const std::vector<Value>& args, Environment&, int line,
                        int column) {
    requireList(args, "listUnique(list) expects a list", line, column);
    std::vector<std::string> seen;
    std::vector<Value> unique;
    for (const auto& element : listElements(args[0])) {
        const std::string key = valueToString(element);
        if (std::find(seen.begin(), seen.end(), key) == seen.end()) {
            seen.push_back(key);
            unique.push_back(element);
        }
    }
    return makeList(std::move(unique));
}

Value builtinListPush(const std::vector<Value>& args, Environment&, int line,
                      int column) {
    if (args.size() != 2 || asList(args[0]) == nullptr) {
        fail("listPush(list, item) expects a list and a value", line, column);
    }
    std::vector<Value> elements = listElements(args[0]);
    elements.push_back(args[1]);
    return makeList(std::move(elements));
}

Value builtinListPop(const std::vector<Value>& args, Environment&, int line,
                     int column) {
    requireList(args, "listPop(list) expects a list", line, column);
    const auto& elements = listElements(args[0]);
    if (elements.empty()) {
        fail("listPop() called on an empty list", line, column);
    }
    return elements.back();
}

std::int64_t resolveIndex(std::int64_t index, std::size_t size,
                          const char* who, int line, int column) {
    const std::int64_t count = static_cast<std::int64_t>(size);
    if (index < -count || index >= count) {
        fail(std::string(who) + " index " + std::to_string(index) +
                 " out of range for list of length " + std::to_string(count),
             line, column);
    }
    return index >= 0 ? index : index + count;
}

Value builtinListGet(const std::vector<Value>& args, Environment&, int line,
                     int column) {
    if (args.size() != 2 || asList(args[0]) == nullptr || !isNumber(args[1])) {
        fail("listGet(list, idx) expects a list and an integer index", line,
             column);
    }
    const auto& elements = listElements(args[0]);
    const std::int64_t index =
        resolveIndex(toInt(args[1], line, column), elements.size(), "listGet()",
                     line, column);
    return elements[static_cast<std::size_t>(index)];
}

Value builtinListSet(const std::vector<Value>& args, Environment&, int line,
                     int column) {
    if (args.size() != 3 || asList(args[0]) == nullptr || !isNumber(args[1])) {
        fail("listSet(list, idx, val) expects a list, an integer index, and a "
             "value",
             line, column);
    }
    std::vector<Value> elements = listElements(args[0]);
    const std::int64_t index =
        resolveIndex(toInt(args[1], line, column), elements.size(), "listSet()",
                     line, column);
    elements[static_cast<std::size_t>(index)] = args[2];
    return makeList(std::move(elements));
}

Value builtinListSlice(const std::vector<Value>& args, Environment&, int line,
                       int column) {
    if (args.size() != 3 || asList(args[0]) == nullptr || !isNumber(args[1]) ||
        !isNumber(args[2])) {
        fail("listSlice(list, start, stop) expects a list and two integer "
             "indices",
             line, column);
    }
    return makeList(pythonSlice(listElements(args[0]),
                                toInt(args[1], line, column),
                                toInt(args[2], line, column)));
}

Value membership(const std::vector<Value>& args, const char* usage, int line,
                 int column) {
    if (args.size() != 2 ||
        (asList(args[0]) == nullptr && asTuple(args[0]) == nullptr)) {
        fail(usage, line, column);
    }
    const std::string target = valueToString(args[1]);
    const auto& elements =
        asList(args[0]) != nullptr ? listElements(args[0])
                                   : tupleElements(args[0]);
    for (const auto& element : elements) {
        if (valueToString(element) == target) {
            return true;
        }
    }
    return false;
}

Value builtinListContains(const std::vector<Value>& args, Environment&, int line,
                          int column) {
    if (args.size() != 2 || asList(args[0]) == nullptr) {
        fail("listContains(list, item) expects a list and a value", line,
             column);
    }
    return membership(args, "listContains(list, item) expects a list and a value",
                      line, column);
}

Value builtinContains(const std::vector<Value>& args, Environment&, int line,
                      int column) {
    return membership(args,
                      "contains(list_or_tuple, value) expects a list or tuple "
                      "and a value",
                      line, column);
}

Value builtinListJoin(const std::vector<Value>& args, Environment&, int line,
                      int column) {
    if (args.size() != 2 || asList(args[0]) == nullptr ||
        !std::holds_alternative<std::string>(args[1])) {
        fail("listJoin(list, sep) expects a list and a string separator", line,
             column);
    }
    const std::string& separator = std::get<std::string>(args[1]);
    std::string output;
    const auto& elements = listElements(args[0]);
    for (std::size_t index = 0; index < elements.size(); ++index) {
        if (index != 0) {
            output += separator;
        }
        output += valueToString(elements[index]);
    }
    return output;
}

Value builtinListIndex(const std::vector<Value>& args, Environment&, int line,
                       int column) {
    if (args.size() != 2 || asList(args[0]) == nullptr) {
        fail("listIndex(list, item) expects a list and a value", line, column);
    }
    const std::string target = valueToString(args[1]);
    const auto& elements = listElements(args[0]);
    for (std::size_t index = 0; index < elements.size(); ++index) {
        if (valueToString(elements[index]) == target) {
            return static_cast<std::int64_t>(index);
        }
    }
    return std::int64_t{-1};
}

Value builtinListRemove(const std::vector<Value>& args, Environment&, int line,
                        int column) {
    if (args.size() != 2 || asList(args[0]) == nullptr || !isNumber(args[1])) {
        fail("listRemove(list, idx) expects a list and an integer index", line,
             column);
    }
    std::vector<Value> elements = listElements(args[0]);
    const std::int64_t index =
        resolveIndex(toInt(args[1], line, column), elements.size(),
                     "listRemove()", line, column);
    elements.erase(elements.begin() + index);
    return makeList(std::move(elements));
}

Value builtinAnyOf(const std::vector<Value>& args, Environment&, int line,
                   int column) {
    requireList(args, "anyOf(list) expects a list", line, column);
    for (const auto& element : listElements(args[0])) {
        if (isTruthy(element)) {
            return true;
        }
    }
    return false;
}

Value builtinAllOf(const std::vector<Value>& args, Environment&, int line,
                   int column) {
    requireList(args, "allOf(list) expects a list", line, column);
    for (const auto& element : listElements(args[0])) {
        if (!isTruthy(element)) {
            return false;
        }
    }
    return true;
}

bool numericSum(const std::vector<Value>& elements, Value& result) {
    bool anyFloat = false;
    double total = 0.0;
    std::int64_t integerTotal = 0;
    for (const auto& element : elements) {
        if (const auto* integer = std::get_if<std::int64_t>(&element)) {
            integerTotal += *integer;
        } else if (const auto* number = std::get_if<double>(&element)) {
            anyFloat = true;
            total += *number;
        }
    }
    if (anyFloat) {
        result = static_cast<double>(integerTotal) + total;
    } else {
        result = integerTotal;
    }
    return true;
}

Value builtinSumOf(const std::vector<Value>& args, Environment&, int line,
                   int column) {
    requireList(args, "sumOf(list) expects a list", line, column);
    Value total;
    numericSum(listElements(args[0]), total);
    return total;
}

Value builtinSortList(const std::vector<Value>& args, Environment&, int line,
                      int column) {
    if (args.empty() || args.size() > 2 || asList(args[0]) == nullptr) {
        fail("sortList(list) or sortList(list, reverse) expects a list", line,
             column);
    }
    bool reverse = false;
    if (args.size() == 2) {
        if (!std::holds_alternative<bool>(args[1])) {
            fail("sortList(list) or sortList(list, reverse) expects a list",
                 line, column);
        }
        reverse = std::get<bool>(args[1]);
    }
    std::vector<Value> elements = listElements(args[0]);
    const auto less = [line, column](const Value& left, const Value& right) {
        return sortLess(left, right, line, column);
    };
    if (reverse) {
        std::sort(elements.begin(), elements.end(),
                  [less](const Value& left, const Value& right) {
                      return less(right, left);
                  });
    } else {
        std::sort(elements.begin(), elements.end(), less);
    }
    return makeList(std::move(elements));
}

Value builtinReverseList(const std::vector<Value>& args, Environment&, int line,
                         int column) {
    requireList(args, "reverseList(list) expects a list", line, column);
    std::vector<Value> elements = listElements(args[0]);
    std::reverse(elements.begin(), elements.end());
    return makeList(std::move(elements));
}

Value extremeOf(const std::vector<Value>& elements, bool smallest, int line,
                int column) {
    if (elements.empty()) {
        fail(smallest ? "min() arg is an empty sequence"
                      : "max() arg is an empty sequence",
             line, column);
    }
    const Value* best = &elements[0];
    for (std::size_t index = 1; index < elements.size(); ++index) {
        const bool better =
            smallest ? sortLess(elements[index], *best, line, column)
                     : sortLess(*best, elements[index], line, column);
        if (better) {
            best = &elements[index];
        }
    }
    return *best;
}

Value builtinListMin(const std::vector<Value>& args, Environment&, int line,
                     int column) {
    requireList(args, "listMin(list) expects a list", line, column);
    return extremeOf(listElements(args[0]), true, line, column);
}

Value builtinListMax(const std::vector<Value>& args, Environment&, int line,
                     int column) {
    requireList(args, "listMax(list) expects a list", line, column);
    return extremeOf(listElements(args[0]), false, line, column);
}

Value firstOf(const std::vector<Value>& elements, const char* who, int line,
              int column) {
    if (elements.empty()) {
        fail(std::string(who) + " called on an empty sequence", line, column);
    }
    return elements.front();
}

Value lastOf(const std::vector<Value>& elements, const char* who, int line,
             int column) {
    if (elements.empty()) {
        fail(std::string(who) + " called on an empty sequence", line, column);
    }
    return elements.back();
}

Value builtinListFirst(const std::vector<Value>& args, Environment&, int line,
                       int column) {
    requireList(args, "listFirst(list) expects a list", line, column);
    return firstOf(listElements(args[0]), "listFirst()", line, column);
}

Value builtinListLast(const std::vector<Value>& args, Environment&, int line,
                      int column) {
    requireList(args, "listLast(list) expects a list", line, column);
    return lastOf(listElements(args[0]), "listLast()", line, column);
}

Value builtinListHead(const std::vector<Value>& args, Environment&, int line,
                      int column) {
    if (args.size() != 2 || asList(args[0]) == nullptr || !isNumber(args[1])) {
        fail("listHead(list, count) expects a list and an integer count", line,
             column);
    }
    const std::int64_t count = toInt(args[1], line, column);
    if (count < 0) {
        fail("listHead() count cannot be negative", line, column);
    }
    const auto& elements = listElements(args[0]);
    const std::size_t take =
        std::min<std::int64_t>(count, static_cast<std::int64_t>(elements.size()));
    return makeList(std::vector<Value>(elements.begin(),
                                        elements.begin() + take));
}

Value builtinListTail(const std::vector<Value>& args, Environment&, int line,
                      int column) {
    if (args.size() != 2 || asList(args[0]) == nullptr || !isNumber(args[1])) {
        fail("listTail(list, count) expects a list and an integer count", line,
             column);
    }
    const std::int64_t count = toInt(args[1], line, column);
    if (count < 0) {
        fail("listTail() count cannot be negative", line, column);
    }
    const auto& elements = listElements(args[0]);
    const std::size_t take =
        std::min<std::int64_t>(count, static_cast<std::int64_t>(elements.size()));
    return makeList(std::vector<Value>(elements.end() - take, elements.end()));
}

Value builtinListCount(const std::vector<Value>& args, Environment&, int line,
                       int column) {
    if (args.size() != 2 || asList(args[0]) == nullptr) {
        fail("listCount(list, value) expects a list and a value", line, column);
    }
    const std::string target = valueToString(args[1]);
    std::int64_t count = 0;
    for (const auto& element : listElements(args[0])) {
        if (valueToString(element) == target) {
            ++count;
        }
    }
    return count;
}

Value builtinListExtend(const std::vector<Value>& args, Environment&, int line,
                        int column) {
    if (args.size() != 2 || asList(args[0]) == nullptr ||
        asList(args[1]) == nullptr) {
        fail("listExtend(list1, list2) expects two lists", line, column);
    }
    std::vector<Value> elements = listElements(args[0]);
    const auto& other = listElements(args[1]);
    elements.insert(elements.end(), other.begin(), other.end());
    return makeList(std::move(elements));
}

Value builtinListInsert(const std::vector<Value>& args, Environment&, int line,
                        int column) {
    if (args.size() != 3 || asList(args[0]) == nullptr || !isNumber(args[1])) {
        fail("listInsert(list, index, value) expects a list, integer index, "
             "and value",
             line, column);
    }
    std::vector<Value> elements = listElements(args[0]);
    std::int64_t index = toInt(args[1], line, column);
    const std::int64_t size = static_cast<std::int64_t>(elements.size());
    if (index < 0) {
        index = std::max<std::int64_t>(0, size + index);
    }
    index = std::min<std::int64_t>(index, size);
    elements.insert(elements.begin() + index, args[2]);
    return makeList(std::move(elements));
}

Value builtinListClear(const std::vector<Value>& args, Environment&, int line,
                       int column) {
    requireList(args, "listClear(list) expects a list", line, column);
    return makeList({});
}

Value builtinListRepeat(const std::vector<Value>& args, Environment&, int line,
                        int column) {
    if (args.size() != 2 || !isNumber(args[1])) {
        fail("listRepeat(value, count) expects a value and integer count", line,
             column);
    }
    const std::int64_t count = toInt(args[1], line, column);
    if (count < 0) {
        fail("listRepeat() count cannot be negative", line, column);
    }
    return makeList(std::vector<Value>(static_cast<std::size_t>(count), args[0]));
}

Value builtinListAvg(const std::vector<Value>& args, Environment&, int line,
                     int column) {
    requireList(args, "listAvg(list) expects a list", line, column);
    double total = 0.0;
    std::size_t count = 0;
    for (const auto& element : listElements(args[0])) {
        if (isNumber(element)) {
            total += asNumber(element, line, column);
            ++count;
        }
    }
    if (count == 0) {
        return 0.0;
    }
    return total / static_cast<double>(count);
}

Value builtinListZip(const std::vector<Value>& args, Environment&, int line,
                     int column) {
    if (args.size() != 2 || asList(args[0]) == nullptr ||
        asList(args[1]) == nullptr) {
        fail("listZip(list1, list2) expects two lists", line, column);
    }
    const auto& left = listElements(args[0]);
    const auto& right = listElements(args[1]);
    std::vector<Value> pairs;
    const std::size_t count = std::min(left.size(), right.size());
    for (std::size_t index = 0; index < count; ++index) {
        pairs.push_back(jsonPairObject(left[index], right[index]));
    }
    return makeList(std::move(pairs));
}

// --- tuple operations -----------------------------------------------------------

Value builtinTupleCreate(const std::vector<Value>& args, Environment&, int,
                         int) {
    return makeTuple(std::vector<Value>(args.begin(), args.end()));
}

Value builtinTupleGet(const std::vector<Value>& args, Environment&, int line,
                      int column) {
    if (args.size() != 2 || asTuple(args[0]) == nullptr || !isNumber(args[1])) {
        fail("tupleGet(tuple, idx) expects a tuple and an integer index", line,
             column);
    }
    const auto& elements = tupleElements(args[0]);
    const std::int64_t index =
        resolveIndex(toInt(args[1], line, column), elements.size(),
                     "tupleGet()", line, column);
    return elements[static_cast<std::size_t>(index)];
}

Value builtinTupleLen(const std::vector<Value>& args, Environment&, int line,
                      int column) {
    requireTuple(args, "tupleLen(tuple) expects a tuple", line, column);
    return static_cast<std::int64_t>(tupleElements(args[0]).size());
}

Value builtinTupleContains(const std::vector<Value>& args, Environment&, int line,
                           int column) {
    if (args.size() != 2 || asTuple(args[0]) == nullptr) {
        fail("tupleContains(tuple, item) expects a tuple and a value", line,
             column);
    }
    return membership(args,
                      "tupleContains(tuple, item) expects a tuple and a value",
                      line, column);
}

Value builtinTupleIndex(const std::vector<Value>& args, Environment&, int line,
                        int column) {
    if (args.size() != 2 || asTuple(args[0]) == nullptr) {
        fail("tupleIndex(tuple, item) expects a tuple and a value", line,
             column);
    }
    const std::string target = valueToString(args[1]);
    const auto& elements = tupleElements(args[0]);
    for (std::size_t index = 0; index < elements.size(); ++index) {
        if (valueToString(elements[index]) == target) {
            return static_cast<std::int64_t>(index);
        }
    }
    return std::int64_t{-1};
}

Value builtinTupleSlice(const std::vector<Value>& args, Environment&, int line,
                        int column) {
    if (args.size() != 3 || asTuple(args[0]) == nullptr || !isNumber(args[1]) ||
        !isNumber(args[2])) {
        fail("tupleSlice(tuple, start, stop) expects a tuple and two integer "
             "indices",
             line, column);
    }
    return makeTuple(pythonSlice(tupleElements(args[0]),
                                 toInt(args[1], line, column),
                                 toInt(args[2], line, column)));
}

Value builtinTupleToList(const std::vector<Value>& args, Environment&, int line,
                         int column) {
    requireTuple(args, "tupleToList(tuple) expects a tuple", line, column);
    return makeList(tupleElements(args[0]));
}

Value builtinListToTuple(const std::vector<Value>& args, Environment&, int line,
                         int column) {
    requireList(args, "listToTuple(list) expects a list", line, column);
    return makeTuple(listElements(args[0]));
}

Value builtinTupleConcat(const std::vector<Value>& args, Environment&, int line,
                         int column) {
    if (args.size() != 2 || asTuple(args[0]) == nullptr ||
        asTuple(args[1]) == nullptr) {
        fail("tupleConcat(tuple1, tuple2) expects two tuples", line, column);
    }
    std::vector<Value> elements = tupleElements(args[0]);
    const auto& other = tupleElements(args[1]);
    elements.insert(elements.end(), other.begin(), other.end());
    return makeTuple(std::move(elements));
}

Value builtinTupleCount(const std::vector<Value>& args, Environment&, int line,
                        int column) {
    if (args.size() != 2 || asTuple(args[0]) == nullptr) {
        fail("tupleCount(tuple, value) expects a tuple and a value", line,
             column);
    }
    const std::string target = valueToString(args[1]);
    std::int64_t count = 0;
    for (const auto& element : tupleElements(args[0])) {
        if (valueToString(element) == target) {
            ++count;
        }
    }
    return count;
}

Value builtinTupleFirst(const std::vector<Value>& args, Environment&, int line,
                        int column) {
    requireTuple(args, "tupleFirst(tuple) expects a tuple", line, column);
    return firstOf(tupleElements(args[0]), "tupleFirst()", line, column);
}

Value builtinTupleLast(const std::vector<Value>& args, Environment&, int line,
                       int column) {
    requireTuple(args, "tupleLast(tuple) expects a tuple", line, column);
    return lastOf(tupleElements(args[0]), "tupleLast()", line, column);
}

Value builtinTupleJsonArray(const std::vector<Value>& args, Environment&, int line,
                            int column) {
    requireTuple(args, "tupleJsonArray(tuple) expects a tuple", line, column);
    return jsonValue(args[0]);
}

Value builtinTupleReverse(const std::vector<Value>& args, Environment&, int line,
                          int column) {
    requireTuple(args, "tupleReverse(tuple) expects a tuple", line, column);
    std::vector<Value> elements = tupleElements(args[0]);
    std::reverse(elements.begin(), elements.end());
    return makeTuple(std::move(elements));
}

Value builtinTupleSort(const std::vector<Value>& args, Environment&, int line,
                       int column) {
    requireTuple(args, "tupleSort(tuple) expects a tuple", line, column);
    std::vector<Value> elements = tupleElements(args[0]);
    std::sort(elements.begin(), elements.end(),
              [line, column](const Value& left, const Value& right) {
                  return sortLess(left, right, line, column);
              });
    return makeTuple(std::move(elements));
}

Value builtinTupleSortDesc(const std::vector<Value>& args, Environment&, int line,
                           int column) {
    requireTuple(args, "tupleSortDesc(tuple) expects a tuple", line, column);
    std::vector<Value> elements = tupleElements(args[0]);
    std::sort(elements.begin(), elements.end(),
              [line, column](const Value& left, const Value& right) {
                  return sortLess(right, left, line, column);
              });
    return makeTuple(std::move(elements));
}

Value builtinTupleMin(const std::vector<Value>& args, Environment&, int line,
                      int column) {
    requireTuple(args, "tupleMin(tuple) expects a tuple", line, column);
    return extremeOf(tupleElements(args[0]), true, line, column);
}

Value builtinTupleMax(const std::vector<Value>& args, Environment&, int line,
                      int column) {
    requireTuple(args, "tupleMax(tuple) expects a tuple", line, column);
    return extremeOf(tupleElements(args[0]), false, line, column);
}

Value builtinTupleSum(const std::vector<Value>& args, Environment&, int line,
                      int column) {
    requireTuple(args, "tupleSum(tuple) expects a tuple", line, column);
    Value total;
    numericSum(tupleElements(args[0]), total);
    return total;
}

Value builtinTupleAny(const std::vector<Value>& args, Environment&, int line,
                      int column) {
    requireTuple(args, "tupleAny(tuple) expects a tuple", line, column);
    for (const auto& element : tupleElements(args[0])) {
        if (isTruthy(element)) {
            return true;
        }
    }
    return false;
}

Value builtinTupleAll(const std::vector<Value>& args, Environment&, int line,
                      int column) {
    requireTuple(args, "tupleAll(tuple) expects a tuple", line, column);
    for (const auto& element : tupleElements(args[0])) {
        if (!isTruthy(element)) {
            return false;
        }
    }
    return true;
}

Value builtinTupleUnique(const std::vector<Value>& args, Environment&, int line,
                         int column) {
    requireTuple(args, "tupleUnique(tuple) expects a tuple", line, column);
    std::vector<std::string> seen;
    std::vector<Value> unique;
    for (const auto& element : tupleElements(args[0])) {
        const std::string key = valueToString(element);
        if (std::find(seen.begin(), seen.end(), key) == seen.end()) {
            seen.push_back(key);
            unique.push_back(element);
        }
    }
    return makeTuple(std::move(unique));
}

Value builtinTupleMean(const std::vector<Value>& args, Environment&, int line,
                       int column) {
    requireTuple(args, "tupleMean(tuple) expects a tuple", line, column);
    double total = 0.0;
    std::size_t count = 0;
    for (const auto& element : tupleElements(args[0])) {
        if (isNumber(element)) {
            total += asNumber(element, line, column);
            ++count;
        }
    }
    if (count == 0) {
        return 0.0;
    }
    return total / static_cast<double>(count);
}

Value builtinTupleFlatten(const std::vector<Value>& args, Environment&, int line,
                          int column) {
    requireTuple(args, "tupleFlatten(tuple) expects a tuple", line, column);
    std::vector<Value> flat;
    for (const auto& element : tupleElements(args[0])) {
        if (asTuple(element) != nullptr) {
            const auto& inner = tupleElements(element);
            flat.insert(flat.end(), inner.begin(), inner.end());
        } else if (asList(element) != nullptr) {
            const auto& inner = listElements(element);
            flat.insert(flat.end(), inner.begin(), inner.end());
        } else {
            flat.push_back(element);
        }
    }
    return makeTuple(std::move(flat));
}

Value builtinTupleZip(const std::vector<Value>& args, Environment&, int line,
                      int column) {
    if (args.size() != 2 || asTuple(args[0]) == nullptr ||
        asTuple(args[1]) == nullptr) {
        fail("tupleZip(tuple1, tuple2) expects two tuples", line, column);
    }
    const auto& left = tupleElements(args[0]);
    const auto& right = tupleElements(args[1]);
    std::vector<Value> pairs;
    const std::size_t count = std::min(left.size(), right.size());
    for (std::size_t index = 0; index < count; ++index) {
        pairs.push_back(jsonPairObject(left[index], right[index]));
    }
    return makeTuple(std::move(pairs));
}

Value builtinTupleJoin(const std::vector<Value>& args, Environment&, int line,
                       int column) {
    if (args.size() != 2 || asTuple(args[0]) == nullptr ||
        !std::holds_alternative<std::string>(args[1])) {
        fail("tupleJoin(tuple, sep) expects a tuple and a string separator",
             line, column);
    }
    const std::string& separator = std::get<std::string>(args[1]);
    std::string output;
    const auto& elements = tupleElements(args[0]);
    for (std::size_t index = 0; index < elements.size(); ++index) {
        if (index != 0) {
            output += separator;
        }
        output += valueToString(elements[index]);
    }
    return output;
}

// --- assertions, timing, and forever helpers -----------------------------------

Value builtinAssert(const std::vector<Value>& args, Environment&, int line,
                    int column) {
    if (args.empty() || args.size() > 2 ||
        (!std::holds_alternative<bool>(args[0]) && !isNumber(args[0]))) {
        fail("assert(condition[, message]) expects a boolean or number "
             "condition and an optional string message",
             line, column);
    }
    if (args.size() == 2 && !std::holds_alternative<std::string>(args[1])) {
        fail("assert(condition[, message]) expects message to be a string",
             line, column);
    }
    const bool condition =
        std::holds_alternative<bool>(args[0])
            ? std::get<bool>(args[0])
            : asNumber(args[0], line, column) != 0.0;
    if (!condition) {
        const std::string message =
            args.size() == 2 ? std::get<std::string>(args[1])
                             : "Assertion failed";
        fail(message, line, column);
    }
    return none();
}

Value builtinSleep(const std::vector<Value>& args, Environment&, int line,
                   int column) {
    if (args.size() != 1 || !isNumber(args[0])) {
        fail("sleep(seconds) expects exactly one number", line, column);
    }
    const double seconds = asNumber(args[0], line, column);
    if (seconds < 0.0) {
        fail("sleep() duration cannot be negative", line, column);
    }
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    return none();
}

Value builtinForeverDelay(const std::vector<Value>& args, Environment& env,
                          int line, int column) {
    if (!env.setupInProgress()) {
        fail("foreverDelay() may only be called inside global setup(){}", line,
             column);
    }
    if (args.size() != 1 || !isNumber(args[0])) {
        fail("foreverDelay(seconds) expects exactly one number", line, column);
    }
    const double seconds = asNumber(args[0], line, column);
    if (seconds < 0.0) {
        fail("foreverDelay(seconds) cannot be negative", line, column);
    }
    env.setForeverDelay(seconds);
    return none();
}

Value builtinSuppressForeverWarning(const std::vector<Value>& args,
                                    Environment& env, int line, int column) {
    if (!env.setupInProgress()) {
        fail("suppressForeverWarning() may only be called inside global "
             "setup(){}",
             line, column);
    }
    if (!args.empty()) {
        fail("suppressForeverWarning() takes no arguments", line, column);
    }
    env.setForeverWarningSuppressed();
    return none();
}

Value builtinSuppressDeprecationWarning(const std::vector<Value>& args,
                                        Environment& env, int line,
                                        int column) {
    if (!env.setupInProgress()) {
        fail("suppressDeprecationWarning() may only be called inside global "
             "setup(){}",
             line, column);
    }
    if (!args.empty()) {
        fail("suppressDeprecationWarning() takes no arguments", line, column);
    }
    // CLynxer has no legacy syntax deprecation warnings yet; accept and ignore.
    return none();
}

} // namespace

// --- native memory ---------------------------------------------------------------

namespace {

std::uint8_t* memoryPointer(const std::vector<Value>& args,
                            std::size_t addressIndex, std::size_t offsetIndex,
                            const char* who, int line, int column) {
    const std::int64_t address = toInt(args[addressIndex], line, column);
    if (address < 0) {
        fail(std::string(who) + " address cannot be negative", line, column);
    }
    std::int64_t offset = 0;
    if (offsetIndex < args.size()) {
        offset = toInt(args[offsetIndex], line, column);
    }
    if (offset < 0) {
        fail(std::string(who) + " offset cannot be negative", line, column);
    }
    std::uint8_t* pointer = reinterpret_cast<std::uint8_t*>(
        static_cast<std::uintptr_t>(address) + static_cast<std::size_t>(offset));
    return pointer;
}

std::int64_t allocationArg(const std::vector<Value>& args, std::size_t index,
                           const char* who, int line, int column) {
    if (index >= args.size() || !isIntegerValue(args[index])) {
        fail(std::string(who) + " expects integer arguments", line, column);
    }
    return std::get<std::int64_t>(args[index]);
}

Value builtinMemoryAllocate(const std::vector<Value>& args, Environment&,
                            int line, int column) {
    const std::int64_t size =
        allocationArg(args, 0, "memoryAllocate(size)", line, column);
    if (size < 0) {
        fail("memoryAllocate() size cannot be negative", line, column);
    }
    void* pointer = std::malloc(static_cast<std::size_t>(size));
    if (pointer == nullptr && size != 0) {
        fail("memoryAllocate() failed: out of memory", line, column);
    }
    return static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(pointer));
}

Value builtinMemoryAllocateZeroed(const std::vector<Value>& args, Environment&,
                                  int line, int column) {
    const std::int64_t count =
        allocationArg(args, 0, "memoryAllocateZeroed(count, size)", line, column);
    const std::int64_t size =
        allocationArg(args, 1, "memoryAllocateZeroed(count, size)", line, column);
    if (count < 0 || size < 0) {
        fail("memoryAllocateZeroed() arguments cannot be negative", line, column);
    }
    void* pointer = std::calloc(static_cast<std::size_t>(count),
                                static_cast<std::size_t>(size));
    if (pointer == nullptr && count * size != 0) {
        fail("memoryAllocateZeroed() failed: out of memory", line, column);
    }
    return static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(pointer));
}

Value builtinMemoryReallocate(const std::vector<Value>& args, Environment&,
                              int line, int column) {
    const std::int64_t address =
        allocationArg(args, 0, "memoryReallocate(address, size)", line, column);
    const std::int64_t size =
        allocationArg(args, 1, "memoryReallocate(address, size)", line, column);
    if (address < 0 || size < 0) {
        fail("memoryReallocate() arguments cannot be negative", line, column);
    }
    void* pointer = std::realloc(
        reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)),
        static_cast<std::size_t>(size));
    if (pointer == nullptr && size != 0) {
        fail("memoryReallocate() failed: out of memory", line, column);
    }
    return static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(pointer));
}

Value builtinMemoryFree(const std::vector<Value>& args, Environment&, int line,
                        int column) {
    const std::int64_t address =
        allocationArg(args, 0, "memoryFree(address)", line, column);
    if (address < 0) {
        fail("memoryFree() address cannot be negative", line, column);
    }
    if (address != 0) {
        std::free(reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)));
    }
    return none();
}

Value builtinMemorySet(const std::vector<Value>& args, Environment&, int line,
                       int column) {
    if (args.size() != 3) {
        fail("memorySet(address, value, size) expects three integer arguments",
             line, column);
    }
    const std::int64_t value =
        allocationArg(args, 1, "memorySet(address, value, size)", line, column);
    const std::int64_t size =
        allocationArg(args, 2, "memorySet(address, value, size)", line, column);
    if (size < 0) {
        fail("memorySet() size cannot be negative", line, column);
    }
    std::uint8_t* pointer =
        memoryPointer(args, 0, args.size(),
                      "memorySet()", line, column);
    std::memset(pointer, static_cast<int>(value & 0xFF),
                static_cast<std::size_t>(size));
    return none();
}

Value builtinMemoryCopy(const std::vector<Value>& args, Environment&, int line,
                        int column) {
    if (args.size() != 3) {
        fail("memoryCopy(destination, source, size) expects three integer "
             "arguments",
             line, column);
    }
    const std::int64_t size = allocationArg(
        args, 2, "memoryCopy(destination, source, size)", line, column);
    if (size < 0) {
        fail("memoryCopy() size cannot be negative", line, column);
    }
    std::uint8_t* destination =
        memoryPointer(args, 0, args.size(),
                      "memoryCopy()", line, column);
    std::uint8_t* source =
        memoryPointer(args, 1, args.size(),
                      "memoryCopy()", line, column);
    std::memcpy(destination, source, static_cast<std::size_t>(size));
    return none();
}

struct TypedKind {
    std::size_t size;
    bool isFloat;
    bool isSigned;
};

const std::unordered_map<std::string, TypedKind>& typedKinds() {
    static const std::unordered_map<std::string, TypedKind> kinds = {
        {"byte", {1, false, false}},   {"int8", {1, false, true}},
        {"uint8", {1, false, false}},  {"int16", {2, false, true}},
        {"uint16", {2, false, false}}, {"int32", {4, false, true}},
        {"uint32", {4, false, false}}, {"int64", {8, false, true}},
        {"uint64", {8, false, false}}, {"float32", {4, true, true}},
        {"float64", {8, true, true}},
    };
    return kinds;
}

Value typedRead(const std::string& type, const std::vector<Value>& args,
                int line, int column) {
    const TypedKind& kind = typedKinds().at(type);
    std::uint8_t* pointer =
        memoryPointer(args, 0, 1, "memoryRead", line, column);
    if (kind.isFloat) {
        if (kind.size == 4) {
            float value = 0.0f;
            std::memcpy(&value, pointer, sizeof(value));
            return static_cast<double>(value);
        }
        double value = 0.0;
        std::memcpy(&value, pointer, sizeof(value));
        return value;
    }
    if (kind.isSigned) {
        switch (kind.size) {
        case 1: {
            std::int8_t value = 0;
            std::memcpy(&value, pointer, sizeof(value));
            return static_cast<std::int64_t>(value);
        }
        case 2: {
            std::int16_t value = 0;
            std::memcpy(&value, pointer, sizeof(value));
            return static_cast<std::int64_t>(value);
        }
        case 4: {
            std::int32_t value = 0;
            std::memcpy(&value, pointer, sizeof(value));
            return static_cast<std::int64_t>(value);
        }
        default: {
            std::int64_t value = 0;
            std::memcpy(&value, pointer, sizeof(value));
            return value;
        }
        }
    }
    switch (kind.size) {
    case 1: {
        std::uint8_t value = 0;
        std::memcpy(&value, pointer, sizeof(value));
        return static_cast<std::int64_t>(value);
    }
    case 2: {
        std::uint16_t value = 0;
        std::memcpy(&value, pointer, sizeof(value));
        return static_cast<std::int64_t>(value);
    }
    case 4: {
        std::uint32_t value = 0;
        std::memcpy(&value, pointer, sizeof(value));
        return static_cast<std::int64_t>(value);
    }
    default: {
        std::uint64_t value = 0;
        std::memcpy(&value, pointer, sizeof(value));
        return static_cast<std::int64_t>(value);
    }
    }
}

Value typedWrite(const std::string& type, const std::vector<Value>& args,
                 int line, int column) {
    const TypedKind& kind = typedKinds().at(type);
    if (args.size() != 3 || !isNumber(args[2])) {
        fail("memoryWrite" + type + "(address, offset, value) expects an "
             "address, an offset, and a value",
             line, column);
    }
    std::uint8_t* pointer =
        memoryPointer(args, 0, 1, "memoryWrite", line, column);
    const double raw = asNumber(args[2], line, column);
    if (kind.isFloat) {
        if (kind.size == 4) {
            const float value = static_cast<float>(raw);
            std::memcpy(pointer, &value, sizeof(value));
        } else {
            const double value = raw;
            std::memcpy(pointer, &value, sizeof(value));
        }
        return none();
    }
    const auto integer = static_cast<std::int64_t>(raw);
    switch (kind.size) {
    case 1: {
        const std::int8_t value = static_cast<std::int8_t>(integer);
        std::memcpy(pointer, &value, sizeof(value));
        break;
    }
    case 2: {
        const std::int16_t value = static_cast<std::int16_t>(integer);
        std::memcpy(pointer, &value, sizeof(value));
        break;
    }
    case 4: {
        const std::int32_t value = static_cast<std::int32_t>(integer);
        std::memcpy(pointer, &value, sizeof(value));
        break;
    }
    default: {
        const std::int64_t value = integer;
        std::memcpy(pointer, &value, sizeof(value));
        break;
    }
    }
    return none();
}

} // namespace

namespace {

template <bool IsWrite>
Value typedAccessor(const char* type, const std::vector<Value>& args,
                    Environment&, int line, int column) {
    if (IsWrite) {
        return typedWrite(type, args, line, column);
    }
    if (args.size() != 2) {
        fail("memoryRead" + std::string(type) +
                 "(address, offset) expects an address and an offset",
             line, column);
    }
    return typedRead(type, args, line, column);
}

#define TYPED_HANDLER(NAME, TYPE, IS_WRITE)                              \
    [](const std::vector<Value>& args, Environment& env, int line,        \
       int column) { return typedAccessor<IS_WRITE>(TYPE, args, env, line, column); }

Value builtinMemoryReadEndian(const std::vector<Value>& args, Environment&,
                              int line, int column) {
    if (args.size() != 4 || !std::holds_alternative<std::string>(args[2]) ||
        !std::holds_alternative<std::string>(args[3])) {
        fail("memoryReadEndian(address, offset, type, order) expects an "
             "address, an offset, a type, and a byte order",
             line, column);
    }
    const std::string& type = std::get<std::string>(args[2]);
    const std::string& order = std::get<std::string>(args[3]);
    const auto kindFound = typedKinds().find(type);
    if (kindFound == typedKinds().end()) {
        fail("unknown memory type '" + type + "'", line, column);
    }
    const bool bigEndian =
        order == "big" || order == "be";
    if (!bigEndian && order != "little" && order != "le") {
        fail("memoryReadEndian() order must be 'little', 'le', 'big', or 'be'",
             line, column);
    }
    const TypedKind& kind = kindFound->second;
    std::uint8_t* pointer =
        memoryPointer(args, 0, 1, "memoryReadEndian()", line, column);
    std::uint8_t buffer[8];
    if (bigEndian) {
        for (std::size_t index = 0; index < kind.size; ++index) {
            buffer[index] = pointer[kind.size - 1 - index];
        }
    } else {
        std::memcpy(buffer, pointer, kind.size);
    }
    if (kind.isFloat) {
        if (kind.size == 4) {
            float value = 0.0f;
            std::memcpy(&value, buffer, sizeof(value));
            return static_cast<double>(value);
        }
        double value = 0.0;
        std::memcpy(&value, buffer, sizeof(value));
        return value;
    }
    std::int64_t unsignedValue = 0;
    std::memcpy(&unsignedValue, buffer, kind.size);
    if (!kind.isSigned) {
        return unsignedValue;
    }
    switch (kind.size) {
    case 1:
        return static_cast<std::int8_t>(unsignedValue);
    case 2:
        return static_cast<std::int16_t>(unsignedValue);
    case 4:
        return static_cast<std::int32_t>(unsignedValue);
    default:
        return unsignedValue;
    }
}

Value builtinMemoryWriteEndian(const std::vector<Value>& args, Environment&,
                               int line, int column) {
    if (args.size() != 5 || !std::holds_alternative<std::string>(args[2]) ||
        !std::holds_alternative<std::string>(args[3]) || !isNumber(args[4])) {
        fail("memoryWriteEndian(address, offset, type, order, value) expects "
             "an address, an offset, a type, a byte order, and a value",
             line, column);
    }
    const std::string& type = std::get<std::string>(args[2]);
    const std::string& order = std::get<std::string>(args[3]);
    const auto kindFound = typedKinds().find(type);
    if (kindFound == typedKinds().end()) {
        fail("unknown memory type '" + type + "'", line, column);
    }
    const bool bigEndian = order == "big" || order == "be";
    if (!bigEndian && order != "little" && order != "le") {
        fail("memoryWriteEndian() order must be 'little', 'le', 'big', or 'be'",
             line, column);
    }
    const TypedKind& kind = kindFound->second;
    std::uint8_t* pointer =
        memoryPointer(args, 0, 1, "memoryWriteEndian()", line, column);
    std::uint8_t buffer[8];
    const double raw = asNumber(args[4], line, column);
    if (kind.isFloat) {
        if (kind.size == 4) {
            const float value = static_cast<float>(raw);
            std::memcpy(buffer, &value, sizeof(value));
        } else {
            const double value = raw;
            std::memcpy(buffer, &value, sizeof(value));
        }
    } else {
        const auto integer = static_cast<std::int64_t>(raw);
        std::memcpy(buffer, &integer, kind.size);
    }
    if (bigEndian) {
        for (std::size_t index = 0; index < kind.size; ++index) {
            pointer[index] = buffer[kind.size - 1 - index];
        }
    } else {
        std::memcpy(pointer, buffer, kind.size);
    }
    return none();
}

Value builtinMemoryTypeSize(const std::vector<Value>& args, Environment&, int line,
                            int column) {
    if (args.size() != 1 || !std::holds_alternative<std::string>(args[0])) {
        fail("memoryTypeSize(type) expects one string", line, column);
    }
    const auto found = typedKinds().find(std::get<std::string>(args[0]));
    if (found == typedKinds().end()) {
        fail("unknown memory type '" + std::get<std::string>(args[0]) + "'",
             line, column);
    }
    return static_cast<std::int64_t>(found->second.size);
}

Value builtinMemoryTypeAlignment(const std::vector<Value>& args, Environment&,
                                 int line, int column) {
    if (args.size() != 1 || !std::holds_alternative<std::string>(args[0])) {
        fail("memoryTypeAlignment(type) expects one string", line, column);
    }
    const auto found = typedKinds().find(std::get<std::string>(args[0]));
    if (found == typedKinds().end()) {
        fail("unknown memory type '" + std::get<std::string>(args[0]) + "'",
             line, column);
    }
    const TypedKind& kind = found->second;
    return static_cast<std::int64_t>(kind.isFloat && kind.size == 4
                                          ? alignof(float)
                                          : kind.isFloat
                                                ? alignof(double)
                                                : kind.size);
}

Value builtinSizeOf(const std::vector<Value>& args, Environment&, int line,
                    int column) {
    if (args.size() != 1 || !std::holds_alternative<std::string>(args[0])) {
        fail("sizeOf(typeName) expects one string", line, column);
    }
    const std::string& type = std::get<std::string>(args[0]);
    std::size_t size = 0;
    bool known = true;
    if (type == "char") size = sizeof(char);
    else if (type == "short") size = sizeof(short);
    else if (type == "int") size = sizeof(int);
    else if (type == "long") size = sizeof(long);
    else if (type == "long long") size = sizeof(long long);
    else if (type == "float") size = sizeof(float);
    else if (type == "double") size = sizeof(double);
    else if (type == "void*") size = sizeof(void*);
    else if (type == "size_t") size = sizeof(std::size_t);
    else if (type == "uintptr_t") size = sizeof(std::uintptr_t);
    else if (type == "int8") size = sizeof(std::int8_t);
    else if (type == "int16") size = sizeof(std::int16_t);
    else if (type == "int32") size = sizeof(std::int32_t);
    else if (type == "int64") size = sizeof(std::int64_t);
    else if (type == "uint8") size = sizeof(std::uint8_t);
    else if (type == "uint16") size = sizeof(std::uint16_t);
    else if (type == "uint32") size = sizeof(std::uint32_t);
    else if (type == "uint64") size = sizeof(std::uint64_t);
    else known = false;
    if (!known) {
        fail("unknown C type '" + type + "'", line, column);
    }
    return static_cast<std::int64_t>(size);
}

// --- named Linux syscalls --------------------------------------------------------

bool requiresArm64(const std::string& name) {
    return name == "syscallPpollFileDescriptors" ||
           name == "syscallWaitForEventsWithSignalMask";
}

long syscallNumberFor(const std::string& name, bool& known) {
    known = true;
#if defined(__x86_64__) || defined(__aarch64__)
    if (name == "syscallGetCurrentDirectory") return SYS_getcwd;
    if (name == "syscallChangeDirectory") return SYS_chdir;
    if (name == "syscallControlInputOutput") return SYS_ioctl;
    if (name == "syscallRead") return SYS_read;
    if (name == "syscallWrite") return SYS_write;
#ifdef SYS_pread64
    if (name == "syscallPositionedRead64") return SYS_pread64;
    if (name == "syscallPositionedWrite64") return SYS_pwrite64;
#endif
    if (name == "syscallOpenAt") return SYS_openat;
    if (name == "syscallClose") return SYS_close;
#ifdef SYS_readv
    if (name == "syscallReadVector") return SYS_readv;
    if (name == "syscallWriteVector") return SYS_writev;
#endif
    if (name == "syscallSeekFile") return SYS_lseek;
    if (name == "syscallGetFileStatus") return SYS_fstat;
    if (name == "syscallGetFileStatusAt") return SYS_newfstatat;
    if (name == "syscallTruncateFile") return SYS_ftruncate;
    if (name == "syscallCheckFileAccessAt") return SYS_faccessat;
    if (name == "syscallSynchronizeFile") return SYS_fsync;
    if (name == "syscallSynchronizeFileData") return SYS_fdatasync;
    if (name == "syscallDuplicateFileDescriptor") return SYS_dup;
    if (name == "syscallDuplicateFileDescriptorAt") return SYS_dup3;
    if (name == "syscallCreatePipe") return SYS_pipe2;
    if (name == "syscallControlFileDescriptor") return SYS_fcntl;
#ifdef SYS_getdents64
    if (name == "syscallGetDirectoryEntries") return SYS_getdents64;
#endif
    if (name == "syscallReadSymbolicLink") return SYS_readlinkat;
    if (name == "syscallCreateDirectoryAt") return SYS_mkdirat;
    if (name == "syscallRemoveFileAt") return SYS_unlinkat;
    if (name == "syscallRenameFileAt") return SYS_renameat;
    if (name == "syscallCreateHardLinkAt") return SYS_linkat;
    if (name == "syscallCreateSymbolicLinkAt") return SYS_symlinkat;
    if (name == "syscallChangeFilePermissions") return SYS_fchmodat;
    if (name == "syscallChangeFileDescriptorPermissions") return SYS_fchmod;
    if (name == "syscallChangeFileOwner") return SYS_fchownat;
    if (name == "syscallChangeFileDescriptorOwner") return SYS_fchown;
    if (name == "syscallMemoryMap") return SYS_mmap;
    if (name == "syscallMemoryUnmap") return SYS_munmap;
    if (name == "syscallMemoryProtect") return SYS_mprotect;
    if (name == "syscallMemoryAdvise") return SYS_madvise;
#ifdef SYS_mremap
    if (name == "syscallMemoryRemap") return SYS_mremap;
#endif
    if (name == "syscallAdjustProgramBreak") return SYS_brk;
    if (name == "syscallExecuteProgram") return SYS_execve;
    if (name == "syscallExecuteProgramAt") return SYS_execveat;
    if (name == "syscallExitProcess") return SYS_exit;
    if (name == "syscallExitAllThreads") return SYS_exit_group;
    if (name == "syscallWaitForProcess") return SYS_wait4;
    if (name == "syscallGetProcessId") return SYS_getpid;
    if (name == "syscallGetParentProcessId") return SYS_getppid;
    if (name == "syscallSendSignal") return SYS_kill;
    if (name == "syscallCreateThread") return SYS_clone;
    if (name == "syscallGetThreadId") return SYS_gettid;
    if (name == "syscallWaitOnMemory") return SYS_futex;
    if (name == "syscallSetThreadIdAddress") return SYS_set_tid_address;
    if (name == "syscallSetRobustThreadList") return SYS_set_robust_list;
    if (name == "syscallGetRobustThreadList") return SYS_get_robust_list;
    if (name == "syscallYieldProcessor") return SYS_sched_yield;
    if (name == "syscallGetClockTime") return SYS_clock_gettime;
    if (name == "syscallGetClockResolution") return SYS_clock_getres;
    if (name == "syscallSleep") return SYS_nanosleep;
    if (name == "syscallGetRandomBytes") return SYS_getrandom;
    if (name == "syscallCreateSocket") return SYS_socket;
    if (name == "syscallCreateSocketPair") return SYS_socketpair;
    if (name == "syscallBindSocket") return SYS_bind;
    if (name == "syscallListenSocket") return SYS_listen;
    if (name == "syscallAcceptConnection") return SYS_accept;
    if (name == "syscallConnectSocket") return SYS_connect;
    if (name == "syscallSendData") return SYS_sendto;
    if (name == "syscallReceiveData") return SYS_recvfrom;
    if (name == "syscallSendMessage") return SYS_sendmsg;
    if (name == "syscallReceiveMessage") return SYS_recvmsg;
    if (name == "syscallShutdownSocket") return SYS_shutdown;
    if (name == "syscallGetSocketAddress") return SYS_getsockname;
    if (name == "syscallGetPeerAddress") return SYS_getpeername;
    if (name == "syscallSetSocketOption") return SYS_setsockopt;
    if (name == "syscallGetSocketOption") return SYS_getsockopt;
    if (name == "syscallCreateEventPoll") return SYS_epoll_create1;
    if (name == "syscallControlEventPoll") return SYS_epoll_ctl;
    if (name == "syscallInitializeInodeNotifications") return SYS_inotify_init1;
    if (name == "syscallAddInodeNotificationWatch") return SYS_inotify_add_watch;
    if (name == "syscallRemoveInodeNotificationWatch") return SYS_inotify_rm_watch;
    if (name == "syscallGetSystemInformation") return SYS_sysinfo;
    if (name == "syscallGetUnixSystemName") return SYS_uname;
    if (name == "syscallGetExtendedFileStatus") return SYS_statx;
    if (name == "syscallGetResourceUsage") return SYS_getrusage;
    if (name == "syscallGetResourceLimit") return SYS_getrlimit;
    if (name == "syscallSetResourceLimit") return SYS_setrlimit;
    if (name == "syscallControlProcess") return SYS_prctl;
#if defined(__x86_64__)
    if (name == "syscallPollFileDescriptors") return SYS_poll;
    if (name == "syscallWaitForEvents") return SYS_epoll_wait;
#elif defined(__aarch64__)
    if (name == "syscallPollFileDescriptors") return SYS_ppoll;
    if (name == "syscallWaitForEvents") return SYS_epoll_pwait;
    if (name == "syscallPpollFileDescriptors") return SYS_ppoll;
    if (name == "syscallWaitForEventsWithSignalMask") return SYS_epoll_pwait;
#endif
    known = false;
    return -1;
#else
    (void)name;
    known = false;
    return -1;
#endif
}

bool syscallArgumentsValid(const std::vector<Value>& args, int line, int column) {
    for (const auto& argument : args) {
        if (!isIntegerValue(argument)) {
            fail("syscall arguments must be integers", line, column);
        }
    }
    return true;
}

Value builtinSyscall(const std::string& name, const std::vector<Value>& args,
                     int line, int column) {
#if !defined(__linux__)
    (void)args;
    fail("named syscalls require a Linux runtime", line, column);
#else
    if (args.size() > 6) {
        fail("syscalls accept at most six arguments", line, column);
    }
    if (requiresArm64(name) && !kArm64) {
        fail("syscall built-in '" + name + "' is only available on arm64", line,
             column);
    }
    syscallArgumentsValid(args, line, column);
    std::size_t expected = 0;
    if (name == "syscallPollFileDescriptors") {
        expected = 3;
    } else if (name == "syscallPpollFileDescriptors" ||
               name == "syscallWaitForEvents" ||
               name == "syscallWaitForEventsWithSignalMask") {
        expected = 5;
    }
    if (expected != 0 && args.size() != expected) {
        fail(name + " expects exactly " + std::to_string(expected) +
                 " arguments, received " + std::to_string(args.size()),
             line, column);
    }

    bool known = false;
    long number = syscallNumberFor(name, known);
    if (!known) {
        fail("syscall '" + name + "' is not available on this architecture",
             line, column);
    }

    std::vector<long> words;
    for (const auto& argument : args) {
        words.push_back(static_cast<long>(std::get<std::int64_t>(argument)));
    }

    struct ::timespec timeout {};
    if (name == "syscallPollFileDescriptors" && kArm64) {
        // ARM64 lacks poll(2); ppoll(2) takes a timespec instead of
        // milliseconds, mirroring the Python implementation.
        const std::int64_t timeoutMs = std::get<std::int64_t>(args[2]);
        if (timeoutMs < 0) {
            words = {words[0], words[1], 0, 0,
                     static_cast<long>(sizeof(unsigned long))};
        } else {
            timeout.tv_sec = timeoutMs / 1000;
            timeout.tv_nsec = (timeoutMs % 1000) * 1000000L;
            words = {words[0], words[1],
                     static_cast<long>(reinterpret_cast<std::uintptr_t>(&timeout)),
                     0, static_cast<long>(sizeof(unsigned long))};
        }
    } else if (name == "syscallWaitForEvents" && kX86_64) {
        // epoll_wait(2) takes four words; the portable API's fifth slot is
        // ignored on x86-64.
        words.resize(std::min<std::size_t>(words.size(), 4));
    } else if (name == "syscallWaitForEvents" ||
               name == "syscallWaitForEventsWithSignalMask") {
        // epoll_pwait(2) ends with a sigsetsize word.
        words.push_back(static_cast<long>(sizeof(unsigned long)));
    }

    while (words.size() < 6) {
        words.push_back(0);
    }
    errno = 0;
    const long result =
        ::syscall(number, words[0], words[1], words[2], words[3], words[4],
                  words[5]);
    if (result == -1 && errno != 0) {
        fail("syscall '" + name + "' failed: " + std::strerror(errno), line,
             column);
    }
    return static_cast<std::int64_t>(result);
#endif
}

// --- registry ---------------------------------------------------------------------

const std::unordered_map<std::string, Handler>& handlerTable() {
    static const std::unordered_map<std::string, Handler> handlers = {
        {"print", builtinPrint},
        {"println", builtinPrintln},
        {"input", builtinInput},
        {"inputln", builtinInputln},
        {"strOf", builtinStrOf},
        {"intOf", builtinIntOf},
        {"floatOf", builtinFloatOf},
        {"sentinel", builtinSentinel},
        {"object", builtinObject},
        {"returnType", builtinReturnType},
        {"returnLength", builtinReturnLength},
        {"range", builtinRange},
        {"seqFromTo", builtinSeqFromTo},
        {"listJsonArray", builtinListJsonArray},
        {"listJsonObject", builtinListJsonObject},
        {"splitStr", builtinSplitStr},
        {"listFlatten", builtinListFlatten},
        {"listUnique", builtinListUnique},
        {"listPush", builtinListPush},
        {"listPop", builtinListPop},
        {"listGet", builtinListGet},
        {"listSet", builtinListSet},
        {"listSlice", builtinListSlice},
        {"listContains", builtinListContains},
        {"contains", builtinContains},
        {"listJoin", builtinListJoin},
        {"listIndex", builtinListIndex},
        {"listRemove", builtinListRemove},
        {"anyOf", builtinAnyOf},
        {"allOf", builtinAllOf},
        {"sumOf", builtinSumOf},
        {"sortList", builtinSortList},
        {"reverseList", builtinReverseList},
        {"listMin", builtinListMin},
        {"listMax", builtinListMax},
        {"listFirst", builtinListFirst},
        {"listLast", builtinListLast},
        {"listHead", builtinListHead},
        {"listTail", builtinListTail},
        {"listCount", builtinListCount},
        {"listExtend", builtinListExtend},
        {"listInsert", builtinListInsert},
        {"listClear", builtinListClear},
        {"listRepeat", builtinListRepeat},
        {"listAvg", builtinListAvg},
        {"listZip", builtinListZip},
        {"tupleCreate", builtinTupleCreate},
        {"tupleGet", builtinTupleGet},
        {"tupleLen", builtinTupleLen},
        {"tupleContains", builtinTupleContains},
        {"tupleIndex", builtinTupleIndex},
        {"tupleSlice", builtinTupleSlice},
        {"tupleToList", builtinTupleToList},
        {"listToTuple", builtinListToTuple},
        {"tupleConcat", builtinTupleConcat},
        {"tupleCount", builtinTupleCount},
        {"tupleFirst", builtinTupleFirst},
        {"tupleLast", builtinTupleLast},
        {"tupleJsonArray", builtinTupleJsonArray},
        {"tupleReverse", builtinTupleReverse},
        {"tupleSort", builtinTupleSort},
        {"tupleSortDesc", builtinTupleSortDesc},
        {"tupleMin", builtinTupleMin},
        {"tupleMax", builtinTupleMax},
        {"tupleSum", builtinTupleSum},
        {"tupleAny", builtinTupleAny},
        {"tupleAll", builtinTupleAll},
        {"tupleUnique", builtinTupleUnique},
        {"tupleMean", builtinTupleMean},
        {"tupleFlatten", builtinTupleFlatten},
        {"tupleZip", builtinTupleZip},
        {"tupleJoin", builtinTupleJoin},
        {"assert", builtinAssert},
        {"sleep", builtinSleep},
        {"foreverDelay", builtinForeverDelay},
        {"suppressForeverWarning", builtinSuppressForeverWarning},
        {"suppressDeprecationWarning", builtinSuppressDeprecationWarning},
        {"memoryAllocate", builtinMemoryAllocate},
        {"memoryAllocateZeroed", builtinMemoryAllocateZeroed},
        {"memoryReallocate", builtinMemoryReallocate},
        {"memoryFree", builtinMemoryFree},
        {"memorySet", builtinMemorySet},
        {"memoryCopy", builtinMemoryCopy},
        {"memoryReadByte", TYPED_HANDLER("byte", "byte", false)},
        {"memoryWriteByte", TYPED_HANDLER("byte", "byte", true)},
        {"memoryReadInt8", TYPED_HANDLER("Int8", "int8", false)},
        {"memoryWriteInt8", TYPED_HANDLER("Int8", "int8", true)},
        {"memoryReadInt16", TYPED_HANDLER("Int16", "int16", false)},
        {"memoryWriteInt16", TYPED_HANDLER("Int16", "int16", true)},
        {"memoryReadInt32", TYPED_HANDLER("Int32", "int32", false)},
        {"memoryWriteInt32", TYPED_HANDLER("Int32", "int32", true)},
        {"memoryReadInt64", TYPED_HANDLER("Int64", "int64", false)},
        {"memoryWriteInt64", TYPED_HANDLER("Int64", "int64", true)},
        {"memoryReadUInt8", TYPED_HANDLER("UInt8", "uint8", false)},
        {"memoryWriteUInt8", TYPED_HANDLER("UInt8", "uint8", true)},
        {"memoryReadUInt16", TYPED_HANDLER("UInt16", "uint16", false)},
        {"memoryWriteUInt16", TYPED_HANDLER("UInt16", "uint16", true)},
        {"memoryReadUInt32", TYPED_HANDLER("UInt32", "uint32", false)},
        {"memoryWriteUInt32", TYPED_HANDLER("UInt32", "uint32", true)},
        {"memoryReadUInt64", TYPED_HANDLER("UInt64", "uint64", false)},
        {"memoryWriteUInt64", TYPED_HANDLER("UInt64", "uint64", true)},
        {"memoryReadFloat32", TYPED_HANDLER("Float32", "float32", false)},
        {"memoryWriteFloat32", TYPED_HANDLER("Float32", "float32", true)},
        {"memoryReadFloat64", TYPED_HANDLER("Float64", "float64", false)},
        {"memoryWriteFloat64", TYPED_HANDLER("Float64", "float64", true)},
        {"memoryReadEndian", builtinMemoryReadEndian},
        {"memoryWriteEndian", builtinMemoryWriteEndian},
        {"memoryTypeSize", builtinMemoryTypeSize},
        {"memoryTypeAlignment", builtinMemoryTypeAlignment},
        {"sizeOf", builtinSizeOf},
    };
    return handlers;
}

const std::unordered_set<std::string>& unsupportedTable() {
    static const std::unordered_set<std::string> unsupported = {
        "rawPy", "rawPyx", "cleanRawPyxCache",
        "asyncRun", "asyncGather", "asyncPollCreate", "asyncPollRegister",
        "asyncPollModify", "asyncPollRemove", "asyncPollWait",
        "asyncPollDispatch", "asyncPollClose", "asyncTimerCreate",
        "asyncTimerCancel", "asyncWakeupCreate", "asyncWakeupSignal",
        "asyncWakeupClose", "asyncSleep",
        "soundLoad", "soundPlay", "soundLoop", "soundStop", "soundPause",
        "soundResume", "soundSetVolume", "soundIsPlaying", "soundRelease",
        "overrideMain", "unshare",
        "varTransfer", "varTransferMutate", "varBorrow", "varBorrowMutate",
        "varSwapAll", "varSwapVal", "varEndBorrow", "borrowing", "beingBorrowed",
        "getAddress", "modifyAddressValue", "getAddressValue", "functionAddress",
        "nativeFunctionAddress", "nativeCall",
        "ffiLoadLibrary", "ffiLookup", "ffiCloseLibrary", "ffiCall",
        "ffiCallback", "ffiFreeCallback",
        "nativeModuleLoad", "nativeModuleName", "nativeModuleFunction",
        "nativeModuleConstant", "nativeModuleType", "nativeModuleError",
        "nativeModuleDependencies", "nativeModuleClose",
        "nativeThreadStart", "nativeThreadJoin", "nativeThreadJoinAll",
        "nativeThreadIsAlive", "nativeThreadStatus", "nativeThreadDetach",
        "nativeMutexCreate", "nativeMutexLock", "nativeMutexTryLock",
        "nativeMutexUnlock", "nativeMutexClose",
        "nativeConditionCreate", "nativeConditionWait", "nativeConditionNotify",
        "nativeConditionNotifyAll", "nativeConditionClose",
        "nativeSemaphoreCreate", "nativeSemaphoreWait", "nativeSemaphoreTryWait",
        "nativeSemaphorePost", "nativeSemaphoreClose",
        "nativeHandleAllocate", "nativeHandleAddress", "nativeHandleFree",
        "nativeHandleIsAlive",
        "processSpawn", "processWrite", "processCloseInput", "processRead",
        "processPoll", "processWait", "processSendSignal", "processClose",
        "filesystemOpen", "filesystemRead", "filesystemWrite", "filesystemClose",
        "filesystemStat", "filesystemList", "filesystemMkdir", "filesystemRemove",
        "filesystemRename", "filesystemLink", "filesystemReadLink",
        "filesystemChmod",
        "networkingOpen", "networkingBind", "networkingListen",
        "networkingAccept", "networkingConnect", "networkingSend",
        "networkingReceive", "networkingClose", "networkingShutdown",
        "networkingBlocking", "networkingOption", "networkingResolve",
        "networkingAddress",
        "atomicLoad", "atomicStore", "atomicAdd", "volatileRead", "volatileWrite",
        "memoryProtect",
        "memoryBlockAllocate", "memoryBlockView", "memoryBlockGet",
        "memoryBlockSet", "memoryBlockLength",
        "memoryArrayAllocate", "memoryArrayView", "memoryArrayGet",
        "memoryArraySet", "memoryArrayLength",
        "memoryViewGet", "memoryViewSet", "memoryViewLength",
        "memoryStructSize", "memoryStructFieldOffset", "memoryStructFieldSize",
        "memoryStructAlignment", "memoryStructFieldCount",
        "memoryStructFieldType", "memoryStructAllocate", "memoryStructGet",
        "memoryStructSet",
        "nativeStructSize", "nativeStructAllocate", "nativeStructFieldOffset",
        "nativeStructFieldSize", "nativeTypeAlignment", "nativeStructAlignment",
        "nativeStructFieldCount", "nativeStructFieldType", "nativeStructGet",
        "nativeStructSet",
        // Named syscalls that are dispatched generically below.
        "syscallGetCurrentDirectory", "syscallChangeDirectory",
        "syscallControlInputOutput", "syscallRead", "syscallWrite",
        "syscallPositionedRead64", "syscallPositionedWrite64", "syscallOpenAt",
        "syscallClose", "syscallReadVector", "syscallWriteVector",
        "syscallSeekFile", "syscallGetFileStatus", "syscallGetFileStatusAt",
        "syscallTruncateFile", "syscallCheckFileAccessAt",
        "syscallSynchronizeFile", "syscallSynchronizeFileData",
        "syscallDuplicateFileDescriptor", "syscallDuplicateFileDescriptorAt",
        "syscallCreatePipe", "syscallControlFileDescriptor",
        "syscallGetDirectoryEntries", "syscallReadSymbolicLink",
        "syscallCreateDirectoryAt", "syscallRemoveFileAt", "syscallRenameFileAt",
        "syscallCreateHardLinkAt", "syscallCreateSymbolicLinkAt",
        "syscallChangeFilePermissions", "syscallChangeFileDescriptorPermissions",
        "syscallChangeFileOwner", "syscallChangeFileDescriptorOwner",
        "syscallMemoryMap", "syscallMemoryUnmap", "syscallMemoryProtect",
        "syscallMemoryAdvise", "syscallMemoryRemap", "syscallAdjustProgramBreak",
        "syscallExecuteProgram", "syscallExecuteProgramAt",
        "syscallExitProcess", "syscallExitAllThreads", "syscallWaitForProcess",
        "syscallGetProcessId", "syscallGetParentProcessId", "syscallSendSignal",
        "syscallCreateThread", "syscallGetThreadId", "syscallWaitOnMemory",
        "syscallSetThreadIdAddress", "syscallSetRobustThreadList",
        "syscallGetRobustThreadList", "syscallYieldProcessor",
        "syscallGetClockTime", "syscallGetClockResolution", "syscallSleep",
        "syscallGetRandomBytes", "syscallCreateSocket", "syscallCreateSocketPair",
        "syscallBindSocket", "syscallListenSocket", "syscallAcceptConnection",
        "syscallConnectSocket", "syscallSendData", "syscallReceiveData",
        "syscallSendMessage", "syscallReceiveMessage", "syscallShutdownSocket",
        "syscallGetSocketAddress", "syscallGetPeerAddress",
        "syscallSetSocketOption", "syscallGetSocketOption",
        "syscallPollFileDescriptors", "syscallPpollFileDescriptors",
        "syscallCreateEventPoll", "syscallControlEventPoll",
        "syscallWaitForEvents", "syscallWaitForEventsWithSignalMask",
        "syscallInitializeInodeNotifications",
        "syscallAddInodeNotificationWatch",
        "syscallRemoveInodeNotificationWatch", "syscallGetSystemInformation",
        "syscallGetUnixSystemName", "syscallGetExtendedFileStatus",
        "syscallGetResourceUsage", "syscallGetResourceLimit",
        "syscallSetResourceLimit", "syscallControlProcess",
    };
    return unsupported;
}

} // namespace

bool isBuiltinName(const std::string& name) {
    const auto& handlers = handlerTable();
    const auto& unsupported = unsupportedTable();
    if (handlers.find(name) != handlers.end()) {
        return true;
    }
    if (name.rfind("syscall", 0) == 0 &&
        unsupported.find(name) != unsupported.end()) {
        return true;
    }
    return unsupported.find(name) != unsupported.end();
}

Value callBuiltin(const std::string& name, const std::vector<Value>& args,
                  Environment& environment, int line, int column) {
    std::string resolved = name;
    if (resolved.rfind("global.", 0) == 0) {
        resolved = resolved.substr(7);
    }
    if (resolved == "embedPy" || resolved.rfind("embedPy.", 0) == 0) {
        fail("Python bridging (embedPy) is not supported in CLynxer", line,
             column);
    }
    const auto& handlers = handlerTable();
    const auto handler = handlers.find(resolved);
    if (handler != handlers.end()) {
        return handler->second(args, environment, line, column);
    }
    if (resolved.rfind("syscall", 0) == 0) {
        const auto& unsupported = unsupportedTable();
        if (unsupported.find(resolved) != unsupported.end()) {
            return builtinSyscall(resolved, args, line, column);
        }
    }
    if (unsupportedTable().find(resolved) != unsupportedTable().end()) {
        fail(resolved + "() is not supported in CLynxer yet", line, column);
    }
    fail("unknown function '" + name + "'", line, column);
}

} // namespace clynxer
