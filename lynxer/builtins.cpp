#include "builtins.hpp"

#include "ast.hpp"
#include "error.hpp"
#include "interrupt.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <dlfcn.h>
#include <unordered_map>
#include <unordered_set>

#ifdef __linux__
#include <sys/syscall.h>
#include <unistd.h>
#endif

// The managed `filesystem*` built-ins follow the Python reference, which is
// built on POSIX `open`/`read`/`stat`/`dirent` calls. On a non-POSIX host the
// names stay in `unsupportedTable()` instead.
#if defined(__unix__) || defined(__APPLE__)
#define LYNXER_POSIX_BUILTINS 1
#include <arpa/inet.h>
#include <csignal>
#include <dirent.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#define LYNXER_POSIX_BUILTINS 0
#endif

namespace lynxer {

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
    if (const auto* wide = std::get_if<UInt64Value>(&value)) {
        return static_cast<std::int64_t>(wide->value);
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
    return std::holds_alternative<std::int64_t>(value) ||
           std::holds_alternative<UInt64Value>(value);
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
        // A signal handler without SA_RESTART makes the read fail with EINTR
        // when Ctrl-C arrives, so surface it as an interrupt, not an error.
        if (interruptRequested()) {
            throw InterruptError();
        }
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
        std::cout << valueToString(args[0]) << '\n';
        std::cout.flush();
    }
    return readLine(line, column);
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

// Returns the filesystem path of a file included in the running compiled
// executable, or "" when the program is not compiled or the file is absent.
Value builtinBundledFile(const std::vector<Value>& args, Environment&, int line,
                         int column) {
    if (args.size() != 1 ||
        !std::holds_alternative<std::string>(args[0])) {
        fail("bundledFile(name) expects a string file name", line, column);
    }
    return bundledAssetPath(std::get<std::string>(args[0]));
}

// Returns the names of every file included in the running compiled executable.
Value builtinBundledFiles(const std::vector<Value>& args, Environment&, int line,
                          int column) {
    requireArity(args, 0, "bundledFiles() takes no arguments", line, column);
    std::vector<Value> names;
    for (auto& name : bundledAssetNames()) {
        names.push_back(std::move(name));
    }
    return makeList(std::move(names));
}

Value builtinCharAt(const std::vector<Value>& args, Environment&, int line,
                    int column) {
    if (args.size() != 2 || !std::holds_alternative<std::string>(args[0]) ||
        !std::holds_alternative<std::int64_t>(args[1])) {
        fail("charAt(str, int) expects a string and an index", line, column);
    }
    const auto& text = std::get<std::string>(args[0]);
    const auto index = std::get<std::int64_t>(args[1]);
    if (index < 0 || static_cast<std::size_t>(index) >= text.size()) {
        fail("charAt() index is out of range", line, column);
    }
    return std::string(1, text[static_cast<std::size_t>(index)]);
}

// Byte value of a char or the first byte of a string, or -1 for an empty
// string. Lynxer strings are byte strings — `charAt` and `returnLength` count
// bytes — so the code point is the first byte rather than a Unicode scalar.
Value builtinCharCode(const std::vector<Value>& args, Environment&, int line,
                      int column) {
    requireArity(args, 1, "charCode() takes exactly 1 argument", line, column);
    const std::string* text = nullptr;
    if (const auto* character = std::get_if<CharValue>(&args[0])) {
        text = &character->text;
    } else if (const auto* string = std::get_if<std::string>(&args[0])) {
        text = string;
    }
    if (text == nullptr) {
        fail("charCode() expects a char or a string", line, column);
    }
    if (text->empty()) {
        return std::int64_t{-1};
    }
    return static_cast<std::int64_t>(
        static_cast<unsigned char>((*text)[0]));
}

// One-byte char for a code in 0..255. The inverse of charCode().
Value builtinCharOf(const std::vector<Value>& args, Environment&, int line,
                    int column) {
    requireArity(args, 1, "charOf() takes exactly 1 argument", line, column);
    if (!std::holds_alternative<std::int64_t>(args[0])) {
        fail("charOf() expects an int code", line, column);
    }
    const auto code = std::get<std::int64_t>(args[0]);
    if (code < 0 || code > 255) {
        fail("charOf() code must be in 0..255", line, column);
    }
    return CharValue{std::string(1, static_cast<char>(code))};
}

Value builtinSubstring(const std::vector<Value>& args, Environment&, int line,
                       int column) {
    if (args.size() != 3 || !std::holds_alternative<std::string>(args[0]) ||
        !std::holds_alternative<std::int64_t>(args[1]) ||
        !std::holds_alternative<std::int64_t>(args[2])) {
        fail("substring(str, start, end) expects a string and two indexes",
             line, column);
    }
    const auto& text = std::get<std::string>(args[0]);
    const auto start = std::get<std::int64_t>(args[1]);
    const auto end = std::get<std::int64_t>(args[2]);
    if (start < 0 || end < start ||
        static_cast<std::size_t>(end) > text.size()) {
        fail("substring() range is out of bounds", line, column);
    }
    return text.substr(static_cast<std::size_t>(start),
                       static_cast<std::size_t>(end - start));
}

Value builtinTrim(const std::vector<Value>& args, Environment&, int line,
                  int column) {
    requireArity(args, 1, "trim() takes exactly 1 argument", line, column);
    if (!std::holds_alternative<std::string>(args[0])) {
        fail("trim() expects a string", line, column);
    }
    const auto& text = std::get<std::string>(args[0]);
    const auto first = std::find_if_not(text.begin(), text.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
    const auto last = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }).base();
    if (first >= last) {
        return std::string{};
    }
    return std::string(first, last);
}

Value builtinUpper(const std::vector<Value>& args, Environment&, int line,
                   int column) {
    requireArity(args, 1, "upper() takes exactly 1 argument", line, column);
    if (!std::holds_alternative<std::string>(args[0])) {
        fail("upper() expects a string", line, column);
    }
    auto result = std::get<std::string>(args[0]);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return result;
}

Value builtinLower(const std::vector<Value>& args, Environment&, int line,
                   int column) {
    requireArity(args, 1, "lower() takes exactly 1 argument", line, column);
    if (!std::holds_alternative<std::string>(args[0])) {
        fail("lower() expects a string", line, column);
    }
    auto result = std::get<std::string>(args[0]);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

Value builtinReplace(const std::vector<Value>& args, Environment&, int line,
                     int column) {
    requireArity(args, 3, "replace() takes exactly 3 arguments", line, column);
    if (!std::holds_alternative<std::string>(args[0]) ||
        !std::holds_alternative<std::string>(args[1]) ||
        !std::holds_alternative<std::string>(args[2])) {
        fail("replace() expects three strings", line, column);
    }
    auto result = std::get<std::string>(args[0]);
    const auto& oldValue = std::get<std::string>(args[1]);
    const auto& newValue = std::get<std::string>(args[2]);
    if (oldValue.empty()) {
        return result;
    }
    std::size_t position = 0;
    while ((position = result.find(oldValue, position)) != std::string::npos) {
        result.replace(position, oldValue.size(), newValue);
        position += newValue.size();
    }
    return result;
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
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        throwIfInterrupted();
        const std::chrono::duration<double> remaining =
            deadline - std::chrono::steady_clock::now();
        std::this_thread::sleep_for(
            std::min(std::chrono::duration<double>(0.05), remaining));
    }
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
    env.setDeprecationWarningSuppressed();
    return none();
}

Value builtinOverrideMain(const std::vector<Value>& args, Environment& env,
                          int line, int column) {
    if (!env.setupInProgress()) {
        fail("overrideMain() may only be called inside global setup(){}", line,
             column);
    }
    if (args.size() != 1 || !std::holds_alternative<std::string>(args[0])) {
        fail("overrideMain() expects exactly one string argument — the name "
             "of the global function to use as the program entry point",
             line, column);
    }
    env.setMainOverride(std::get<std::string>(args[0]));
    return none();
}

} // namespace

// --- native memory ---------------------------------------------------------------

namespace {

// Native-memory registry. Every allocation is tracked with its size so that a
// double free, a stale pointer, or an out-of-bounds access becomes a
// source-located Lynxer error instead of corrupting the process (the Python
// reference behaves the same way; docs: builtins.md).
std::recursive_mutex& memoryRegistryMutex() {
    static std::recursive_mutex mutex;
    return mutex;
}

std::unordered_map<void*, std::size_t>& liveAllocations() {
    static std::unordered_map<void*, std::size_t> allocations;
    return allocations;
}

std::unordered_set<void*>& freedAllocations() {
    static std::unordered_set<void*> freed;
    return freed;
}

void trackAllocation(void* pointer, std::size_t size) {
    std::lock_guard<std::recursive_mutex> guard(memoryRegistryMutex());
    if (pointer != nullptr) {
        liveAllocations()[pointer] = size;
        freedAllocations().erase(pointer);
    }
}

void validateMemory(void* pointer, std::size_t offset, std::size_t bytes,
                    int line, int column) {
    std::lock_guard<std::recursive_mutex> guard(memoryRegistryMutex());
    if (freedAllocations().find(pointer) != freedAllocations().end()) {
        fail("address refers to freed memory", line, column);
    }
    const auto allocation = liveAllocations().find(pointer);
    if (allocation == liveAllocations().end()) {
        fail("invalid native memory address", line, column);
    }
    if (offset > allocation->second || bytes > allocation->second - offset) {
        fail("memory access is out of bounds", line, column);
    }
}

std::uint8_t* memoryPointer(const std::vector<Value>& args,
                            std::size_t addressIndex, std::size_t offsetIndex,
                            const char* who, int line, int column,
                            std::size_t bytes = 0) {
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
    void* base = reinterpret_cast<void*>(static_cast<std::uintptr_t>(address));
    validateMemory(base, static_cast<std::size_t>(offset), bytes, line, column);
    std::uint8_t* pointer = reinterpret_cast<std::uint8_t*>(
        static_cast<std::uintptr_t>(address) + static_cast<std::size_t>(offset));
    return pointer;
}

std::int64_t allocationArg(const std::vector<Value>& args, std::size_t index,
                           const char* who, int line, int column) {
    if (index >= args.size() || !isIntegerValue(args[index])) {
        fail(std::string(who) + " expects integer arguments", line, column);
    }
    return toInt(args[index], line, column);
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
    trackAllocation(pointer, static_cast<std::size_t>(size));
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
    trackAllocation(pointer, static_cast<std::size_t>(count * size));
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
    void* base = reinterpret_cast<void*>(static_cast<std::uintptr_t>(address));
    if (address != 0) {
        validateMemory(base, 0, 0, line, column);
    }
    void* pointer =
        std::realloc(base, static_cast<std::size_t>(size));
    if (pointer == nullptr && size != 0) {
        fail("memoryReallocate() failed: out of memory", line, column);
    }
    if (address != 0) {
        std::lock_guard<std::recursive_mutex> guard(memoryRegistryMutex());
        liveAllocations().erase(base);
        if (pointer != base) {
            freedAllocations().insert(base);
        }
    }
    trackAllocation(pointer, static_cast<std::size_t>(size));
    return static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(pointer));
}

Value builtinMemoryFree(const std::vector<Value>& args, Environment&, int line,
                        int column) {
    const std::int64_t address =
        allocationArg(args, 0, "memoryFree(address)", line, column);
    if (address < 0) {
        fail("memoryFree() address cannot be negative", line, column);
    }
    void* base = reinterpret_cast<void*>(static_cast<std::uintptr_t>(address));
    validateMemory(base, 0, 0, line, column);
    std::free(base);
    {
        std::lock_guard<std::recursive_mutex> guard(memoryRegistryMutex());
        liveAllocations().erase(base);
        freedAllocations().insert(base);
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
        memoryPointer(args, 0, args.size(), "memorySet()", line, column,
                      static_cast<std::size_t>(size));
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
        memoryPointer(args, 0, args.size(), "memoryCopy()", line, column,
                      static_cast<std::size_t>(size));
    std::uint8_t* source =
        memoryPointer(args, 1, args.size(), "memoryCopy()", line, column,
                      static_cast<std::size_t>(size));
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
        memoryPointer(args, 0, 1, "memoryRead", line, column, kind.size);
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
        return UInt64Value{value};
    }
    }
}

// A typed 64-bit write must not round-trip through a double: INT64_MAX becomes
// 2^63 as a double and the cast back is out of range. Pull the payload from the
// integer alternatives directly, so signed and unsigned 8-byte writes preserve
// the full range, the way the reference's exact integers do.
std::int64_t signedMemoryPayload(const Value& value) {
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return *integer;
    }
    if (const auto* wide = std::get_if<UInt64Value>(&value)) {
        return static_cast<std::int64_t>(wide->value);
    }
    return static_cast<std::int64_t>(asNumber(value, 0, 0));
}

std::uint64_t unsignedMemoryPayload(const Value& value) {
    if (const auto* wide = std::get_if<UInt64Value>(&value)) {
        return wide->value;
    }
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return static_cast<std::uint64_t>(*integer);
    }
    return static_cast<std::uint64_t>(asNumber(value, 0, 0));
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
        memoryPointer(args, 0, 1, "memoryWrite", line, column, kind.size);
    if (kind.isFloat) {
        const double raw = asNumber(args[2], line, column);
        if (kind.size == 4) {
            const float value = static_cast<float>(raw);
            std::memcpy(pointer, &value, sizeof(value));
        } else {
            const double value = raw;
            std::memcpy(pointer, &value, sizeof(value));
        }
        return none();
    }
    if (kind.size == 8) {
        if (kind.isSigned) {
            const std::int64_t value = signedMemoryPayload(args[2]);
            std::memcpy(pointer, &value, sizeof(value));
        } else {
            const std::uint64_t value = unsignedMemoryPayload(args[2]);
            std::memcpy(pointer, &value, sizeof(value));
        }
        return none();
    }
    const auto integer = signedMemoryPayload(args[2]);
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
        memoryPointer(args, 0, 1, "memoryReadEndian()", line, column, kind.size);
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
        if (kind.size == 8) {
            std::uint64_t wide = 0;
            std::memcpy(&wide, buffer, sizeof(wide));
            return UInt64Value{wide};
        }
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
        memoryPointer(args, 0, 1, "memoryWriteEndian()", line, column, kind.size);
    std::uint8_t buffer[8];
    if (kind.isFloat) {
        const double raw = asNumber(args[4], line, column);
        if (kind.size == 4) {
            const float value = static_cast<float>(raw);
            std::memcpy(buffer, &value, sizeof(value));
        } else {
            const double value = raw;
            std::memcpy(buffer, &value, sizeof(value));
        }
    } else if (kind.size == 8) {
        if (kind.isSigned) {
            const std::int64_t value = signedMemoryPayload(args[4]);
            std::memcpy(buffer, &value, sizeof(value));
        } else {
            const std::uint64_t value = unsignedMemoryPayload(args[4]);
            std::memcpy(buffer, &value, sizeof(value));
        }
    } else {
        const std::int64_t integer = signedMemoryPayload(args[4]);
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

// --- Managed filesystem built-ins -------------------------------------------
//
// `filesystem*` is a small handle-based API that preserves errno in its
// messages, mirroring the Python reference. Open descriptors live in a registry
// keyed by a non-negative handle; anything a program leaves open is closed by
// the operating system when the process exits.

#if LYNXER_POSIX_BUILTINS

std::unordered_map<std::int64_t, int>& openFiles() {
    static std::unordered_map<std::int64_t, int> files;
    return files;
}

std::int64_t nextFileHandle() {
    static std::int64_t next = 1;
    return next++;
}

// Reports the failure of `name`, preserving the errno of the call that failed.
[[noreturn]] void failErrno(const std::string& name, int line, int column) {
    const int code = errno;
    fail(name + "() failed: [" + std::to_string(code) + "] " +
             std::strerror(code),
         line, column);
}

bool asBool(const Value& value) {
    if (const auto* flag = std::get_if<bool>(&value)) {
        return *flag;
    }
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return *integer != 0;
    }
    return false;
}

bool isNonNegativeInt(const Value& value) {
    const auto* integer = std::get_if<std::int64_t>(&value);
    return integer != nullptr && *integer >= 0;
}

// Resolves a handle argument to a live descriptor, or fails with the
// reference's wording.
int requireOpenFile(const Value& value, const char* name, int line,
                    int column) {
    const auto* handle = std::get_if<std::int64_t>(&value);
    if (handle == nullptr || *handle < 0) {
        fail(std::string(name) + "() expects a file handle", line, column);
    }
    const auto found = openFiles().find(*handle);
    if (found == openFiles().end()) {
        fail(std::string(name) + "() received an unknown or closed file handle",
             line, column);
    }
    return found->second;
}

// Replaces bytes that are not valid UTF-8 with U+FFFD, like the reference's
// `bytes.decode("utf-8", errors="replace")`.
std::string utf8Replace(const std::string& bytes) {
    std::string out;
    out.reserve(bytes.size());
    std::size_t index = 0;
    while (index < bytes.size()) {
        const auto lead = static_cast<unsigned char>(bytes[index]);
        std::size_t length = 0;
        if (lead < 0x80) {
            length = 1;
        } else if ((lead & 0xE0) == 0xC0) {
            length = 2;
        } else if ((lead & 0xF0) == 0xE0) {
            length = 3;
        } else if ((lead & 0xF8) == 0xF0) {
            length = 4;
        }
        bool valid = length > 0 && index + length <= bytes.size();
        for (std::size_t offset = 1; valid && offset < length; ++offset) {
            const auto next =
                static_cast<unsigned char>(bytes[index + offset]);
            if ((next & 0xC0) != 0x80) {
                valid = false;
            }
        }
        if (!valid) {
            out += "\xEF\xBF\xBD";
            ++index;
            continue;
        }
        out.append(bytes, index, length);
        index += length;
    }
    return out;
}

std::string requirePathArg(const std::vector<Value>& args, std::size_t index,
                           const char* usage, int line, int column) {
    if (!std::holds_alternative<std::string>(args[index])) {
        fail(usage, line, column);
    }
    return std::get<std::string>(args[index]);
}

double statSeconds(const timespec& time) {
    return static_cast<double>(time.tv_sec) +
           static_cast<double>(time.tv_nsec) / 1e9;
}

Value builtinFilesystemOpen(const std::vector<Value>& args, Environment&,
                            int line, int column) {
    const char* usage =
        "filesystemOpen(path, mode, permissions?) expects strings and an "
        "optional integer";
    if (args.size() != 2 && args.size() != 3) {
        fail(usage, line, column);
    }
    if (!std::holds_alternative<std::string>(args[0]) ||
        !std::holds_alternative<std::string>(args[1]) ||
        (args.size() == 3 && !isNonNegativeInt(args[2]))) {
        fail(usage, line, column);
    }
    const std::string& mode = std::get<std::string>(args[1]);
    int flags = 0;
    if (mode == "r") {
        flags = O_RDONLY;
    } else if (mode == "w") {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    } else if (mode == "a") {
        flags = O_WRONLY | O_CREAT | O_APPEND;
    } else if (mode == "r+") {
        flags = O_RDWR;
    } else if (mode == "w+") {
        flags = O_RDWR | O_CREAT | O_TRUNC;
    } else if (mode == "a+") {
        flags = O_RDWR | O_CREAT | O_APPEND;
    } else {
        fail("filesystemOpen() mode must be r, w, a, r+, w+, or a+", line,
             column);
    }
    const mode_t permissions =
        args.size() == 3 ? static_cast<mode_t>(std::get<std::int64_t>(args[2]))
                         : 0666;
    const std::string path = std::get<std::string>(args[0]);
    const int descriptor = ::open(path.c_str(), flags, permissions);
    if (descriptor < 0) {
        failErrno("filesystemOpen", line, column);
    }
    const std::int64_t handle = nextFileHandle();
    openFiles()[handle] = descriptor;
    return handle;
}

Value builtinFilesystemRead(const std::vector<Value>& args, Environment&,
                            int line, int column) {
    if (args.size() != 2 || !isNonNegativeInt(args[1])) {
        fail("filesystemRead(handle, maxBytes) expects a non-negative byte "
             "count",
             line, column);
    }
    const int descriptor =
        requireOpenFile(args[0], "filesystemRead", line, column);
    const auto wanted = static_cast<std::size_t>(std::get<std::int64_t>(args[1]));
    std::string buffer(wanted, '\0');
    const ssize_t count = ::read(descriptor, buffer.data(), wanted);
    if (count < 0) {
        failErrno("filesystemRead", line, column);
    }
    buffer.resize(static_cast<std::size_t>(count));
    return utf8Replace(buffer);
}

Value builtinFilesystemWrite(const std::vector<Value>& args, Environment&,
                             int line, int column) {
    if (args.size() != 2 || !std::holds_alternative<std::string>(args[1])) {
        fail("filesystemWrite(handle, data) expects a file handle and string",
             line, column);
    }
    const int descriptor =
        requireOpenFile(args[0], "filesystemWrite", line, column);
    const std::string& data = std::get<std::string>(args[1]);
    const ssize_t count = ::write(descriptor, data.data(), data.size());
    if (count < 0) {
        failErrno("filesystemWrite", line, column);
    }
    return static_cast<std::int64_t>(count);
}

Value builtinFilesystemClose(const std::vector<Value>& args, Environment&,
                             int line, int column) {
    if (args.size() != 1) {
        fail("filesystemClose(handle) expects a file handle", line, column);
    }
    const int descriptor =
        requireOpenFile(args[0], "filesystemClose", line, column);
    if (::close(descriptor) != 0) {
        failErrno("filesystemClose", line, column);
    }
    openFiles().erase(std::get<std::int64_t>(args[0]));
    return std::int64_t{0};
}

Value builtinFilesystemStat(const std::vector<Value>& args, Environment&,
                            int line, int column) {
    const std::string path = requirePathArg(
        args, 0, "filesystemStat(path) expects a path string", line, column);
    struct stat info {};
    if (::lstat(path.c_str(), &info) != 0) {
        failErrno("filesystemStat", line, column);
    }
    const char* kind = "other";
    if (S_ISLNK(info.st_mode)) {
        kind = "symlink";
    } else if (S_ISREG(info.st_mode)) {
        kind = "file";
    } else if (S_ISDIR(info.st_mode)) {
        kind = "dir";
    }
    return std::string("{\"type\":") + jsonString(kind) +
           ",\"size\":" + std::to_string(info.st_size) +
           ",\"mode\":" + std::to_string(info.st_mode & 07777) +
           ",\"modifiedTime\":" + jsonDouble(statSeconds(info.st_mtim)) +
           ",\"accessTime\":" + jsonDouble(statSeconds(info.st_atim)) +
           ",\"changeTime\":" + jsonDouble(statSeconds(info.st_ctim)) + "}";
}

Value builtinFilesystemList(const std::vector<Value>& args, Environment&,
                            int line, int column) {
    const std::string path =
        requirePathArg(args, 0,
                       "filesystemList(path) expects a directory path string",
                       line, column);
    DIR* directory = ::opendir(path.c_str());
    if (directory == nullptr) {
        failErrno("filesystemList", line, column);
    }
    std::vector<std::string> names;
    while (const dirent* entry = ::readdir(directory)) {
        const std::string name = entry->d_name;
        if (name != "." && name != "..") {
            names.push_back(name);
        }
    }
    ::closedir(directory);
    std::sort(names.begin(), names.end());
    std::vector<Value> elements;
    elements.reserve(names.size());
    for (const auto& name : names) {
        elements.emplace_back(name);
    }
    return makeList(std::move(elements));
}

Value builtinFilesystemMkdir(const std::vector<Value>& args, Environment&,
                             int line, int column) {
    const char* usage =
        "filesystemMkdir(path, parents?) expects a path and optional boolean";
    if ((args.size() != 1 && args.size() != 2) ||
        !std::holds_alternative<std::string>(args[0]) ||
        (args.size() == 2 && !std::holds_alternative<bool>(args[1]) &&
         !std::holds_alternative<std::int64_t>(args[1]))) {
        fail(usage, line, column);
    }
    const std::string path = std::get<std::string>(args[0]);
    if (args.size() == 2 && asBool(args[1])) {
        // Create missing parents, tolerating a directory that already exists.
        std::string partial;
        std::size_t index = 0;
        while (index <= path.size()) {
            const std::size_t slash = path.find('/', index);
            const std::size_t end =
                slash == std::string::npos ? path.size() : slash;
            partial = path.substr(0, end);
            if (!partial.empty() && ::mkdir(partial.c_str(), 0777) != 0 &&
                errno != EEXIST) {
                failErrno("filesystemMkdir", line, column);
            }
            if (slash == std::string::npos) {
                break;
            }
            index = slash + 1;
        }
    } else if (::mkdir(path.c_str(), 0777) != 0) {
        failErrno("filesystemMkdir", line, column);
    }
    return true;
}

Value builtinFilesystemRemove(const std::vector<Value>& args, Environment&,
                              int line, int column) {
    const std::string path = requirePathArg(
        args, 0, "filesystemRemove(path) expects a path string", line, column);
    struct stat info {};
    const bool isDirectory =
        ::lstat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode) &&
        !S_ISLNK(info.st_mode);
    const int result = isDirectory ? ::rmdir(path.c_str()) : ::unlink(path.c_str());
    if (result != 0) {
        failErrno("filesystemRemove", line, column);
    }
    return std::int64_t{0};
}

Value builtinFilesystemRename(const std::vector<Value>& args, Environment&,
                              int line, int column) {
    if (args.size() != 2 ||
        !std::holds_alternative<std::string>(args[0]) ||
        !std::holds_alternative<std::string>(args[1])) {
        fail("filesystemRename(source, target) expects two path strings", line,
             column);
    }
    if (::rename(std::get<std::string>(args[0]).c_str(),
                 std::get<std::string>(args[1]).c_str()) != 0) {
        failErrno("filesystemRename", line, column);
    }
    return std::int64_t{0};
}

Value builtinFilesystemLink(const std::vector<Value>& args, Environment&,
                            int line, int column) {
    const char* usage =
        "filesystemLink(source, target, symbolic?) expects paths and an "
        "optional boolean";
    if ((args.size() != 2 && args.size() != 3) ||
        !std::holds_alternative<std::string>(args[0]) ||
        !std::holds_alternative<std::string>(args[1]) ||
        (args.size() == 3 && !std::holds_alternative<bool>(args[2]) &&
         !std::holds_alternative<std::int64_t>(args[2]))) {
        fail(usage, line, column);
    }
    const std::string source = std::get<std::string>(args[0]);
    const std::string target = std::get<std::string>(args[1]);
    const bool symbolic = args.size() == 3 && asBool(args[2]);
    const int result = symbolic ? ::symlink(source.c_str(), target.c_str())
                                : ::link(source.c_str(), target.c_str());
    if (result != 0) {
        failErrno("filesystemLink", line, column);
    }
    return std::int64_t{0};
}

Value builtinFilesystemReadLink(const std::vector<Value>& args, Environment&,
                                int line, int column) {
    const std::string path = requirePathArg(
        args, 0, "filesystemReadLink(path) expects a path string", line,
        column);
    std::vector<char> buffer(256);
    for (;;) {
        const ssize_t count =
            ::readlink(path.c_str(), buffer.data(), buffer.size());
        if (count < 0) {
            failErrno("filesystemReadLink", line, column);
        }
        if (static_cast<std::size_t>(count) < buffer.size()) {
            return std::string(buffer.data(), static_cast<std::size_t>(count));
        }
        buffer.resize(buffer.size() * 2);
    }
}

Value builtinFilesystemChmod(const std::vector<Value>& args, Environment&,
                             int line, int column) {
    const char* usage =
        "filesystemChmod(path, mode) expects a path and non-negative integer "
        "mode";
    if (args.size() != 2 || !std::holds_alternative<std::string>(args[0]) ||
        !isNonNegativeInt(args[1])) {
        fail(usage, line, column);
    }
    if (::chmod(std::get<std::string>(args[0]).c_str(),
                static_cast<mode_t>(std::get<std::int64_t>(args[1]))) != 0) {
        failErrno("filesystemChmod", line, column);
    }
    return std::int64_t{0};
}

// --- Managed process built-ins ----------------------------------------------
//
// `process*` spawns a child with one pipe per standard stream and keeps it in a
// registry keyed by a handle. Commands are never shell-parsed: the caller names
// an executable and passes its argv. This mirrors the Python reference, whose
// messages are reproduced verbatim.

struct ChildProcess {
    pid_t pid = -1;
    int input = -1;
    int output = -1;
    int error = -1;
    bool reaped = false;
    int status = 0;
};

std::unordered_map<std::int64_t, ChildProcess>& childProcesses() {
    static std::unordered_map<std::int64_t, ChildProcess> processes;
    return processes;
}

std::int64_t nextProcessHandle() {
    static std::int64_t next = 1;
    return next++;
}

// Python ignores SIGPIPE at startup; without the same here, writing to a pipe
// whose reader has gone would kill the interpreter instead of reporting EPIPE.
void ignoreSigpipeOnce() {
    static const bool once = [] {
        ::signal(SIGPIPE, SIG_IGN);
        return true;
    }();
    (void)once;
}

ChildProcess* requireProcess(const Value& value, const char* name, int line,
                             int column) {
    const auto* handle = std::get_if<std::int64_t>(&value);
    if (handle == nullptr || *handle < 0) {
        fail(std::string(name) + "() expects a process handle", line, column);
    }
    const auto found = childProcesses().find(*handle);
    if (found == childProcesses().end()) {
        fail(std::string(name) + "() received an unknown process handle", line,
             column);
    }
    return &found->second;
}

// `waitpid` reports a signal death as a negative status, like Python's
// `returncode`.
int decodeStatus(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return -WTERMSIG(status);
    }
    return 0;
}

// Returns the exit status, or -1 while the child is still running. The child is
// reaped at most once and the status cached.
int pollProcess(ChildProcess& child) {
    if (child.reaped) {
        return child.status;
    }
    int status = 0;
    const pid_t result = ::waitpid(child.pid, &status, WNOHANG);
    if (result == child.pid) {
        child.reaped = true;
        child.status = decodeStatus(status);
        return child.status;
    }
    return -1;
}

// Reads up to `wanted` bytes, looping the way Python's buffered `read(n)` does,
// and returns what arrived before end of file.
std::string readStream(int descriptor, std::size_t wanted) {
    std::string out;
    out.reserve(wanted);
    while (out.size() < wanted) {
        char buffer[4096];
        const std::size_t remaining = wanted - out.size();
        const std::size_t chunk = std::min(remaining, sizeof(buffer));
        const ssize_t count = ::read(descriptor, buffer, chunk);
        if (count <= 0) {
            break;
        }
        out.append(buffer, static_cast<std::size_t>(count));
    }
    return out;
}

Value builtinProcessSpawn(const std::vector<Value>& args, Environment&, int line,
                          int column) {
    ignoreSigpipeOnce();
    const char* usage =
        "processSpawn(command, arguments, environment?) expects a command and a "
        "list of string arguments";
    if (args.size() < 2 || args.size() > 3 ||
        !std::holds_alternative<std::string>(args[0])) {
        fail(usage, line, column);
    }
    const auto* argumentList = asList(args[1]);
    if (argumentList == nullptr) {
        fail(usage, line, column);
    }
    std::vector<std::string> arguments;
    for (const auto& element : (*argumentList)->elements) {
        if (!std::holds_alternative<std::string>(element)) {
            fail(usage, line, column);
        }
        arguments.push_back(std::get<std::string>(element));
    }

    std::vector<std::string> environment;
    if (args.size() == 3) {
        const auto* overrideList = asList(args[2]);
        if (overrideList == nullptr) {
            fail("processSpawn environment must be a list of KEY=VALUE strings",
                 line, column);
        }
        std::unordered_map<std::string, std::string> merged;
        for (char** entry = ::environ; entry != nullptr && *entry != nullptr;
             ++entry) {
            const std::string item = *entry;
            const auto equals = item.find('=');
            merged[item.substr(0, equals)] = item.substr(equals + 1);
        }
        for (const auto& element : (*overrideList)->elements) {
            if (!std::holds_alternative<std::string>(element)) {
                fail(
                    "processSpawn environment must be a list of KEY=VALUE "
                    "strings",
                    line, column);
            }
            const std::string& item = std::get<std::string>(element);
            const auto equals = item.find('=');
            if (equals == std::string::npos) {
                fail(
                    "processSpawn environment must be a list of KEY=VALUE "
                    "strings",
                    line, column);
            }
            if (equals == 0) {
                fail("processSpawn environment keys must not be empty", line,
                     column);
            }
            merged[item.substr(0, equals)] = item.substr(equals + 1);
        }
        for (const auto& pair : merged) {
            environment.push_back(pair.first + "=" + pair.second);
        }
    }

    const std::string& command = std::get<std::string>(args[0]);
    int input[2] = {-1, -1};
    int output[2] = {-1, -1};
    int error[2] = {-1, -1};
    int report[2] = {-1, -1};
    if (::pipe2(input, 0) != 0 || ::pipe2(output, 0) != 0 ||
        ::pipe2(error, 0) != 0 || ::pipe2(report, O_CLOEXEC) != 0) {
        const int code = errno;
        for (int descriptor : {input[0], input[1], output[0], output[1],
                               error[0], error[1], report[0], report[1]}) {
            if (descriptor >= 0) {
                ::close(descriptor);
            }
        }
        fail("processSpawn() failed: [Errno " + std::to_string(code) + "] " +
                 std::strerror(code),
             line, column);
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        const int code = errno;
        for (int descriptor : {input[0], input[1], output[0], output[1],
                               error[0], error[1], report[0], report[1]}) {
            ::close(descriptor);
        }
        fail("processSpawn() failed: [Errno " + std::to_string(code) + "] " +
                 std::strerror(code),
             line, column);
    }

    if (pid == 0) {
        // Child. Report any failure through `report` so the parent can format
        // the same message Python's subprocess module does.
        ::dup2(input[0], STDIN_FILENO);
        ::dup2(output[1], STDOUT_FILENO);
        ::dup2(error[1], STDERR_FILENO);
        ::close(input[0]);
        ::close(input[1]);
        ::close(output[0]);
        ::close(output[1]);
        ::close(error[0]);
        ::close(error[1]);
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(command.c_str()));
        for (auto& argument : arguments) {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);
        if (environment.empty()) {
            ::execvp(command.c_str(), argv.data());
        } else {
            std::vector<char*> envp;
            for (auto& item : environment) {
                envp.push_back(const_cast<char*>(item.c_str()));
            }
            envp.push_back(nullptr);
            ::execvpe(command.c_str(), argv.data(), envp.data());
        }
        const int code = errno;
        const auto ignored = ::write(report[1], &code, sizeof(code));
        (void)ignored;
        ::_exit(127);
    }

    ::close(input[0]);
    ::close(output[1]);
    ::close(error[1]);
    ::close(report[1]);
    int childError = 0;
    const ssize_t reported = ::read(report[0], &childError, sizeof(childError));
    ::close(report[0]);
    if (reported == static_cast<ssize_t>(sizeof(childError))) {
        ::close(input[1]);
        ::close(output[0]);
        ::close(error[0]);
        int status = 0;
        ::waitpid(pid, &status, 0);
        fail("processSpawn() failed: [Errno " + std::to_string(childError) +
                 "] " + std::strerror(childError) + ": '" + command + "'",
             line, column);
    }

    ChildProcess child;
    child.pid = pid;
    child.input = input[1];
    child.output = output[0];
    child.error = error[0];
    const std::int64_t handle = nextProcessHandle();
    childProcesses()[handle] = child;
    return handle;
}

Value builtinProcessWrite(const std::vector<Value>& args, Environment&, int line,
                          int column) {
    if (args.size() != 2 || !std::holds_alternative<std::string>(args[1])) {
        fail("processWrite(handle, data) expects a handle and string", line,
             column);
    }
    ChildProcess* child = requireProcess(args[0], "processWrite", line, column);
    if (child->input < 0) {
        // Python's stream object reports a closed file with this text.
        fail("processWrite() failed: write to closed file", line, column);
    }
    const std::string& data = std::get<std::string>(args[1]);
    std::size_t written = 0;
    while (written < data.size()) {
        const ssize_t count =
            ::write(child->input, data.data() + written, data.size() - written);
        if (count < 0) {
            const int code = errno;
            fail("processWrite() failed: [Errno " + std::to_string(code) +
                     "] " + std::strerror(code),
                 line, column);
        }
        written += static_cast<std::size_t>(count);
    }
    return static_cast<std::int64_t>(written);
}

Value builtinProcessCloseInput(const std::vector<Value>& args, Environment&,
                               int line, int column) {
    if (args.size() != 1) {
        fail("processCloseInput(handle) expects a process handle", line, column);
    }
    ChildProcess* child =
        requireProcess(args[0], "processCloseInput", line, column);
    if (child->input >= 0) {
        if (::close(child->input) != 0) {
            failErrno("processCloseInput", line, column);
        }
        child->input = -1;
    }
    return std::int64_t{0};
}

Value builtinProcessRead(const std::vector<Value>& args, Environment&, int line,
                         int column) {
    const char* usage =
        "processRead(handle, stream, maxBytes) expects stdout/stderr and a "
        "non-negative byte count";
    if (args.size() != 3 || !std::holds_alternative<std::string>(args[1]) ||
        !isNonNegativeInt(args[2])) {
        fail(usage, line, column);
    }
    const std::string& stream = std::get<std::string>(args[1]);
    if (stream != "stdout" && stream != "stderr") {
        fail(usage, line, column);
    }
    ChildProcess* child = requireProcess(args[0], "processRead", line, column);
    const int descriptor = stream == "stdout" ? child->output : child->error;
    if (descriptor < 0) {
        fail("processRead() stream is closed", line, column);
    }
    const auto wanted = static_cast<std::size_t>(std::get<std::int64_t>(args[2]));
    const std::string bytes = readStream(descriptor, wanted);
    return utf8Replace(bytes);
}

Value builtinProcessPoll(const std::vector<Value>& args, Environment&, int line,
                         int column) {
    if (args.size() != 1) {
        fail("processPoll(handle) expects a process handle", line, column);
    }
    ChildProcess* child = requireProcess(args[0], "processPoll", line, column);
    return static_cast<std::int64_t>(pollProcess(*child));
}

Value builtinProcessWait(const std::vector<Value>& args, Environment&, int line,
                         int column) {
    const char* usage =
        "processWait(handle, timeoutSeconds) expects a non-negative timeout";
    if (args.size() != 2 || std::holds_alternative<bool>(args[1])) {
        fail(usage, line, column);
    }
    double timeout = 0.0;
    if (const auto* integer = std::get_if<std::int64_t>(&args[1])) {
        if (*integer < 0) {
            fail(usage, line, column);
        }
        timeout = static_cast<double>(*integer);
    } else if (const auto* number = std::get_if<double>(&args[1])) {
        if (*number < 0) {
            fail(usage, line, column);
        }
        timeout = *number;
    } else {
        fail(usage, line, column);
    }
    ChildProcess* child = requireProcess(args[0], "processWait", line, column);
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                              std::chrono::duration<double>(timeout));
    for (;;) {
        const int status = pollProcess(*child);
        if (status != -1 || std::chrono::steady_clock::now() >= deadline) {
            return static_cast<std::int64_t>(status);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

Value builtinProcessSendSignal(const std::vector<Value>& args, Environment&,
                               int line, int column) {
    if (args.size() != 2 || !isNonNegativeInt(args[1])) {
        fail("processSendSignal(handle, signal) expects a signal number", line,
             column);
    }
    ChildProcess* child =
        requireProcess(args[0], "processSendSignal", line, column);
    if (pollProcess(*child) != -1) {
        fail("processSendSignal() process has already exited", line, column);
    }
    if (::kill(child->pid, static_cast<int>(std::get<std::int64_t>(args[1]))) !=
        0) {
        const int code = errno;
        fail("processSendSignal() failed: [Errno " + std::to_string(code) +
                 "] " + std::strerror(code),
             line, column);
    }
    return std::int64_t{0};
}

Value builtinProcessClose(const std::vector<Value>& args, Environment&, int line,
                          int column) {
    if (args.size() != 1) {
        fail("processClose(handle) expects a process handle", line, column);
    }
    ChildProcess* child = requireProcess(args[0], "processClose", line, column);
    for (int* descriptor : {&child->input, &child->output, &child->error}) {
        if (*descriptor >= 0) {
            ::close(*descriptor);
            *descriptor = -1;
        }
    }
    if (pollProcess(*child) == -1) {
        ::kill(child->pid, SIGTERM);
        for (int attempt = 0; attempt < 200 && pollProcess(*child) == -1;
             ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (pollProcess(*child) == -1) {
            ::kill(child->pid, SIGKILL);
            ::waitpid(child->pid, nullptr, 0);
            child->reaped = true;
        }
    }
    childProcesses().erase(std::get<std::int64_t>(args[0]));
    return std::int64_t{0};
}

// --- Managed networking built-ins -------------------------------------------
//
// `networking*` hands out managed TCP, UDP and Unix-domain sockets. Addresses
// stay as host/path strings plus an integer port so the API never exposes a
// native address structure. Messages match the Python reference verbatim.

struct ManagedSocket {
    int descriptor = -1;
    int family = AF_INET;
};

std::unordered_map<std::int64_t, ManagedSocket>& openSockets() {
    static std::unordered_map<std::int64_t, ManagedSocket> sockets;
    return sockets;
}

std::int64_t nextSocketHandle() {
    static std::int64_t next = 1;
    return next++;
}

ManagedSocket* requireSocket(const Value& value, const char* name, int line,
                             int column) {
    const auto* handle = std::get_if<std::int64_t>(&value);
    if (handle == nullptr || *handle < 0) {
        fail(std::string(name) + "() expects a socket handle", line, column);
    }
    const auto found = openSockets().find(*handle);
    if (found == openSockets().end()) {
        fail(std::string(name) + "() received an unknown or closed socket handle",
             line, column);
    }
    return &found->second;
}

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return text;
}

// Fills an IPv4 address from a host string and port, resolving host names.
bool resolveIpv4(const std::string& host, std::int64_t port, int socketType,
                 sockaddr_in& out) {
    addrinfo hints {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = socketType;
    addrinfo* results = nullptr;
    const std::string service = std::to_string(port);
    if (::getaddrinfo(host.empty() ? nullptr : host.c_str(), service.c_str(),
                      &hints, &results) != 0 ||
        results == nullptr) {
        return false;
    }
    std::memcpy(&out, results->ai_addr, sizeof(sockaddr_in));
    ::freeaddrinfo(results);
    return true;
}

// `networkingBind`/`networkingConnect` accept either a bare address (a Unix
// socket path) or a host and port.
bool socketAddress(const std::vector<Value>& args, sockaddr_storage& storage,
                   socklen_t& length) {
    if (args.size() == 2 && std::holds_alternative<std::string>(args[1])) {
        sockaddr_un address {};
        address.sun_family = AF_UNIX;
        const std::string& path = std::get<std::string>(args[1]);
        if (path.size() >= sizeof(address.sun_path)) {
            return false;
        }
        std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
        std::memcpy(&storage, &address, sizeof(address));
        length = sizeof(address);
        return true;
    }
    if (args.size() == 3 && std::holds_alternative<std::string>(args[1]) &&
        isNonNegativeInt(args[2])) {
        sockaddr_in address {};
        if (!resolveIpv4(std::get<std::string>(args[1]),
                         std::get<std::int64_t>(args[2]), 0, address)) {
            return false;
        }
        std::memcpy(&storage, &address, sizeof(address));
        length = sizeof(address);
        return true;
    }
    return false;
}

Value builtinNetworkingOpen(const std::vector<Value>& args, Environment&, int line,
                            int column) {
    if (args.size() != 1 || !std::holds_alternative<std::string>(args[0])) {
        fail("networkingOpen(kind) expects tcp, udp, or unix", line, column);
    }
    const std::string kind = lowercase(std::get<std::string>(args[0]));
    int family = AF_INET;
    int socketType = SOCK_STREAM;
    if (kind == "tcp") {
        socketType = SOCK_STREAM;
    } else if (kind == "udp") {
        socketType = SOCK_DGRAM;
    } else if (kind == "unix") {
        family = AF_UNIX;
        socketType = SOCK_STREAM;
    } else {
        fail("networkingOpen() kind must be tcp, udp, or unix", line, column);
    }
    const int descriptor = ::socket(family, socketType, 0);
    if (descriptor < 0) {
        failErrno("networkingOpen", line, column);
    }
    const std::int64_t handle = nextSocketHandle();
    openSockets()[handle] = ManagedSocket{descriptor, family};
    return handle;
}

Value builtinNetworkingBind(const std::vector<Value>& args, Environment&, int line,
                            int column) {
    if (args.empty()) {
        fail("networkingBind(handle, address, port?) expects a socket handle",
             line, column);
    }
    ManagedSocket* socket = requireSocket(args[0], "networkingBind", line, column);
    sockaddr_storage storage {};
    socklen_t length = 0;
    if (!socketAddress(args, storage, length)) {
        fail("networkingBind(handle, address, port?) expects an address and "
             "optional non-negative port",
             line, column);
    }
    if (::bind(socket->descriptor, reinterpret_cast<sockaddr*>(&storage),
               length) != 0) {
        failErrno("networkingBind", line, column);
    }
    return std::int64_t{0};
}

Value builtinNetworkingListen(const std::vector<Value>& args, Environment&,
                              int line, int column) {
    if ((args.size() != 1 && args.size() != 2) ||
        (args.size() == 2 && !isNonNegativeInt(args[1]))) {
        fail("networkingListen(handle, backlog?) expects a socket handle and "
             "optional integer",
             line, column);
    }
    ManagedSocket* socket =
        requireSocket(args[0], "networkingListen", line, column);
    const int backlog =
        args.size() == 2 ? static_cast<int>(std::get<std::int64_t>(args[1])) : 128;
    if (::listen(socket->descriptor, backlog) != 0) {
        failErrno("networkingListen", line, column);
    }
    return std::int64_t{0};
}

Value builtinNetworkingAccept(const std::vector<Value>& args, Environment&,
                              int line, int column) {
    if (args.size() != 1) {
        fail("networkingAccept(handle) expects a socket handle", line, column);
    }
    ManagedSocket* socket =
        requireSocket(args[0], "networkingAccept", line, column);
    const int accepted =
        ::accept(socket->descriptor, nullptr, nullptr);
    if (accepted < 0) {
        failErrno("networkingAccept", line, column);
    }
    const std::int64_t handle = nextSocketHandle();
    openSockets()[handle] = ManagedSocket{accepted, socket->family};
    return handle;
}

Value builtinNetworkingConnect(const std::vector<Value>& args, Environment&,
                               int line, int column) {
    if (args.empty()) {
        fail("networkingConnect(handle, address, port?) expects a socket handle",
             line, column);
    }
    ManagedSocket* socket =
        requireSocket(args[0], "networkingConnect", line, column);
    sockaddr_storage storage {};
    socklen_t length = 0;
    if (!socketAddress(args, storage, length)) {
        fail("networkingConnect(handle, address, port?) expects an address and "
             "optional non-negative port",
             line, column);
    }
    if (::connect(socket->descriptor, reinterpret_cast<sockaddr*>(&storage),
                  length) != 0) {
        failErrno("networkingConnect", line, column);
    }
    return std::int64_t{0};
}

Value builtinNetworkingSend(const std::vector<Value>& args, Environment&, int line,
                            int column) {
    if (args.size() != 2 || !std::holds_alternative<std::string>(args[1])) {
        fail("networkingSend(handle, data) expects a socket handle and string",
             line, column);
    }
    ManagedSocket* socket = requireSocket(args[0], "networkingSend", line, column);
    const std::string& data = std::get<std::string>(args[1]);
    const ssize_t count = ::send(socket->descriptor, data.data(), data.size(), 0);
    if (count < 0) {
        failErrno("networkingSend", line, column);
    }
    return static_cast<std::int64_t>(count);
}

Value builtinNetworkingReceive(const std::vector<Value>& args, Environment&,
                               int line, int column) {
    if (args.size() != 2 || !isNonNegativeInt(args[1])) {
        fail("networkingReceive(handle, maxBytes) expects a non-negative byte "
             "count",
             line, column);
    }
    ManagedSocket* socket =
        requireSocket(args[0], "networkingReceive", line, column);
    const auto wanted = static_cast<std::size_t>(std::get<std::int64_t>(args[1]));
    std::string buffer(wanted, '\0');
    const ssize_t count = ::recv(socket->descriptor, buffer.data(), wanted, 0);
    if (count < 0) {
        failErrno("networkingReceive", line, column);
    }
    buffer.resize(static_cast<std::size_t>(count));
    return utf8Replace(buffer);
}

Value builtinNetworkingClose(const std::vector<Value>& args, Environment&,
                             int line, int column) {
    if (args.size() != 1) {
        fail("networkingClose(handle) expects a socket handle", line, column);
    }
    ManagedSocket* socket =
        requireSocket(args[0], "networkingClose", line, column);
    if (::close(socket->descriptor) != 0) {
        failErrno("networkingClose", line, column);
    }
    openSockets().erase(std::get<std::int64_t>(args[0]));
    return std::int64_t{0};
}

Value builtinNetworkingShutdown(const std::vector<Value>& args, Environment&,
                                int line, int column) {
    if (args.size() != 2 || !std::holds_alternative<std::string>(args[1])) {
        fail("networkingShutdown(handle, how) expects read, write, or both", line,
             column);
    }
    ManagedSocket* socket =
        requireSocket(args[0], "networkingShutdown", line, column);
    const std::string& how = std::get<std::string>(args[1]);
    int mode = 0;
    if (how == "read") {
        mode = SHUT_RD;
    } else if (how == "write") {
        mode = SHUT_WR;
    } else if (how == "both") {
        mode = SHUT_RDWR;
    } else {
        fail("networkingShutdown(handle, how) expects read, write, or both", line,
             column);
    }
    if (::shutdown(socket->descriptor, mode) != 0) {
        failErrno("networkingShutdown", line, column);
    }
    return std::int64_t{0};
}

Value builtinNetworkingBlocking(const std::vector<Value>& args, Environment&,
                                int line, int column) {
    if (args.size() != 2 || std::holds_alternative<std::monostate>(args[1]) ||
        std::holds_alternative<std::string>(args[1]) ||
        asList(args[1]) != nullptr ||
        std::holds_alternative<std::shared_ptr<Tuple>>(args[1])) {
        fail("networkingBlocking(handle, enabled) expects a socket handle and "
             "boolean",
             line, column);
    }
    ManagedSocket* socket =
        requireSocket(args[0], "networkingBlocking", line, column);
    const bool enabled = asBool(args[1]);
    int flags = ::fcntl(socket->descriptor, F_GETFL, 0);
    if (flags < 0) {
        failErrno("networkingBlocking", line, column);
    }
    flags = enabled ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
    if (::fcntl(socket->descriptor, F_SETFL, flags) != 0) {
        failErrno("networkingBlocking", line, column);
    }
    return std::int64_t{0};
}

Value builtinNetworkingOption(const std::vector<Value>& args, Environment&,
                              int line, int column) {
    if (args.size() != 3 || !std::holds_alternative<std::string>(args[1]) ||
        std::holds_alternative<std::monostate>(args[2]) ||
        std::holds_alternative<std::string>(args[2])) {
        fail("networkingOption(handle, name, value) expects a socket handle, "
             "name, and integer",
             line, column);
    }
    const std::string& name = std::get<std::string>(args[1]);
    int option = 0;
    if (name == "reuseAddr") {
        option = SO_REUSEADDR;
    } else if (name == "keepAlive") {
        option = SO_KEEPALIVE;
    } else if (name == "broadcast") {
        option = SO_BROADCAST;
    } else {
        fail("networkingOption() supports reuseAddr, keepAlive, and broadcast",
             line, column);
    }
    ManagedSocket* socket =
        requireSocket(args[0], "networkingOption", line, column);
    const int value = asBool(args[2]) ? 1 : static_cast<int>(toInt(args[2], line, column));
    if (::setsockopt(socket->descriptor, SOL_SOCKET, option, &value,
                     sizeof(value)) != 0) {
        failErrno("networkingOption", line, column);
    }
    return std::int64_t{0};
}

Value builtinNetworkingResolve(const std::vector<Value>& args, Environment&,
                               int line, int column) {
    if (args.size() != 2 || !std::holds_alternative<std::string>(args[0]) ||
        !isNonNegativeInt(args[1])) {
        fail("networkingResolve(host, port) expects a host and non-negative port",
             line, column);
    }
    addrinfo hints {};
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* results = nullptr;
    const std::string service = std::to_string(std::get<std::int64_t>(args[1]));
    const int status = ::getaddrinfo(std::get<std::string>(args[0]).c_str(),
                                     service.c_str(), &hints, &results);
    if (status != 0) {
        fail("networkingResolve() failed: [" + std::to_string(status) + "] " +
                 ::gai_strerror(status),
             line, column);
    }
    std::vector<std::string> addresses;
    for (addrinfo* entry = results; entry != nullptr; entry = entry->ai_next) {
        char text[INET6_ADDRSTRLEN] = {0};
        const void* source =
            entry->ai_family == AF_INET
                ? static_cast<const void*>(
                      &reinterpret_cast<sockaddr_in*>(entry->ai_addr)->sin_addr)
                : static_cast<const void*>(
                      &reinterpret_cast<sockaddr_in6*>(entry->ai_addr)->sin6_addr);
        if (::inet_ntop(entry->ai_family, source, text, sizeof(text)) != nullptr) {
            addresses.push_back(text);
        }
    }
    ::freeaddrinfo(results);
    std::sort(addresses.begin(), addresses.end());
    addresses.erase(std::unique(addresses.begin(), addresses.end()),
                    addresses.end());
    std::vector<Value> elements;
    elements.reserve(addresses.size());
    for (auto& address : addresses) {
        elements.emplace_back(address);
    }
    return makeList(std::move(elements));
}

Value builtinNetworkingAddress(const std::vector<Value>& args, Environment&,
                               int line, int column) {
    if (args.size() != 1) {
        fail("networkingAddress(handle) expects a socket handle", line, column);
    }
    ManagedSocket* socket =
        requireSocket(args[0], "networkingAddress", line, column);
    sockaddr_storage storage {};
    socklen_t length = sizeof(storage);
    if (::getsockname(socket->descriptor,
                      reinterpret_cast<sockaddr*>(&storage), &length) != 0) {
        failErrno("networkingAddress", line, column);
    }
    if (storage.ss_family == AF_UNIX) {
        const auto* address = reinterpret_cast<const sockaddr_un*>(&storage);
        return jsonString(address->sun_path);
    }
    const auto* address = reinterpret_cast<const sockaddr_in*>(&storage);
    char text[INET_ADDRSTRLEN] = {0};
    ::inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text));
    return std::string("[") + jsonString(text) + "," +
           std::to_string(::ntohs(address->sin_port)) + "]";
}

// --- Managed sound built-ins ------------------------------------------------
//
// `sound*` is implemented by the bundled Rust `sound` stdlib module, which owns
// the audio backend (rodio/cpal/symphonia). Keeping one implementation avoids
// duplicating an audio stack in C++ and keeps the audio dependency out of the
// interpreter binary. The built-in layer keeps the reference's validation
// messages and its own registry — a handle is valid only if this layer loaded
// it — and defers the audio work to the module.

std::unordered_set<std::int64_t>& liveSounds() {
    static std::unordered_set<std::int64_t> sounds;
    return sounds;
}

Value callSoundModule(const std::string& operation,
                      const std::vector<Value>& args, int line, int column) {
    return callBridgedModule("sound.so", operation, args, line, column);
}

bool isNumberValue(const Value& value) {
    return std::holds_alternative<std::int64_t>(value) ||
           std::holds_alternative<double>(value) ||
           std::holds_alternative<bool>(value);
}

double numberOf(const Value& value) {
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return static_cast<double>(*integer);
    }
    if (const auto* number = std::get_if<double>(&value)) {
        return *number;
    }
    if (const auto* flag = std::get_if<bool>(&value)) {
        return *flag ? 1.0 : 0.0;
    }
    return 0.0;
}

// True when the module reported success for a `int64` 0/1 result.
bool moduleSucceeded(const Value& result) {
    const auto* flag = std::get_if<std::int64_t>(&result);
    return flag != nullptr && *flag != 0;
}

std::int64_t requireSoundHandle(const std::vector<Value>& args, const char* name,
                                int line, int column) {
    if (args.size() != 1 || std::holds_alternative<bool>(args[0])) {
        fail(std::string(name) + "(handle) expects one handle", line, column);
    }
    const auto* handle = std::get_if<std::int64_t>(&args[0]);
    if (handle == nullptr) {
        fail(std::string(name) + "(handle) expects one handle", line, column);
    }
    if (liveSounds().find(*handle) == liveSounds().end()) {
        fail(std::string(name) + " received an invalid sound handle", line,
             column);
    }
    return *handle;
}

Value builtinSoundLoad(const std::vector<Value>& args, Environment&, int line,
                       int column) {
    if (args.size() != 1 || !std::holds_alternative<std::string>(args[0])) {
        fail("soundLoad(path) expects one string path", line, column);
    }
    const std::string& path = std::get<std::string>(args[0]);
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        fail("sound file does not exist: '" + path + "'", line, column);
    }
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    if (extension != ".wav" && extension != ".ogg" && extension != ".mp3" &&
        extension != ".flac") {
        fail("unsupported sound format; use WAV, OGG, MP3, or FLAC", line,
             column);
    }
    const Value loaded = callSoundModule("load", {Value(path)}, line, column);
    const auto* handle = std::get_if<std::int64_t>(&loaded);
    if (handle == nullptr || *handle < 0) {
        // The reference surfaces the backend's own exception text here; this
        // path reports what the module's sentinel means instead.
        fail("audio backend failed to load sound: '" + path + "'", line, column);
    }
    liveSounds().insert(*handle);
    return *handle;
}

Value builtinSoundPlay(const std::vector<Value>& args, Environment&, int line,
                       int column) {
    const std::int64_t handle =
        requireSoundHandle(args, "soundPlay", line, column);
    if (!moduleSucceeded(callSoundModule("play", {Value(handle)}, line, column))) {
        fail("audio backend failed to play sound: playback did not start", line,
             column);
    }
    return std::int64_t{0};
}

Value builtinSoundLoop(const std::vector<Value>& args, Environment&, int line,
                       int column) {
    const std::int64_t handle =
        requireSoundHandle(args, "soundLoop", line, column);
    if (!moduleSucceeded(callSoundModule("loop", {Value(handle)}, line, column))) {
        fail("audio backend failed to loop sound: playback did not start", line,
             column);
    }
    return std::int64_t{0};
}

Value builtinSoundStop(const std::vector<Value>& args, Environment&, int line,
                       int column) {
    const std::int64_t handle =
        requireSoundHandle(args, "soundStop", line, column);
    callSoundModule("stop", {Value(handle)}, line, column);
    return std::int64_t{0};
}

// The reference fails here: "audio backend does not support portable
// pause/resume", because Arcade has no portable pause. The Rust module does, so
// these two are a deliberate improvement over the reference. Recorded in
// docs/limitations.md.
Value builtinSoundPause(const std::vector<Value>& args, Environment&, int line,
                        int column) {
    const std::int64_t handle =
        requireSoundHandle(args, "soundPause", line, column);
    if (!moduleSucceeded(callSoundModule("pause", {Value(handle)}, line, column))) {
        fail("audio backend failed to pause sound: no player is active", line,
             column);
    }
    return std::int64_t{0};
}

Value builtinSoundResume(const std::vector<Value>& args, Environment&, int line,
                         int column) {
    const std::int64_t handle =
        requireSoundHandle(args, "soundResume", line, column);
    if (!moduleSucceeded(
            callSoundModule("resume", {Value(handle)}, line, column))) {
        fail("audio backend failed to resume sound: no player is active", line,
             column);
    }
    return std::int64_t{0};
}

Value builtinSoundSetVolume(const std::vector<Value>& args, Environment&, int line,
                            int column) {
    if (args.size() != 2 || !isNumberValue(args[0]) || !isNumberValue(args[1])) {
        fail("soundSetVolume(handle, volume) expects numbers", line, column);
    }
    const std::int64_t handle = static_cast<std::int64_t>(numberOf(args[0]));
    if (liveSounds().find(handle) == liveSounds().end()) {
        fail("soundSetVolume received an invalid sound handle", line, column);
    }
    const double volume = numberOf(args[1]);
    if (!(volume >= 0.0 && volume <= 1.0)) {
        fail("sound volume must be between 0.0 and 1.0", line, column);
    }
    callSoundModule("setVolume", {Value(handle), Value(volume)}, line, column);
    return std::int64_t{0};
}

Value builtinSoundIsPlaying(const std::vector<Value>& args, Environment&, int line,
                            int column) {
    const std::int64_t handle =
        requireSoundHandle(args, "soundIsPlaying", line, column);
    return moduleSucceeded(
        callSoundModule("isPlaying", {Value(handle)}, line, column));
}

Value builtinSoundRelease(const std::vector<Value>& args, Environment&, int line,
                          int column) {
    if (args.size() != 1 || !isNumberValue(args[0])) {
        fail("soundRelease(handle) expects one handle", line, column);
    }
    const std::int64_t handle = static_cast<std::int64_t>(numberOf(args[0]));
    if (liveSounds().erase(handle) == 0) {
        fail("soundRelease received an invalid sound handle", line, column);
    }
    callSoundModule("release", {Value(handle)}, line, column);
    return std::int64_t{0};
}

// --- FFI Built-ins -------------------------------------------------------------



// --- Managed native-thread built-ins ----------------------------------------
//
// `nativeThreadStart` runs a Lynxer function on a `std::thread`. The interpreter
// evaluates Lynxer code under one lock (see `executeProgram`), and a worker
// takes that lock before calling back in, so two threads never evaluate at
// once. A worker therefore runs while the thread that started it is blocked in
// `nativeThreadJoin`/`nativeThreadJoinAll`, which release the lock before
// waiting: cooperative threads, safe by construction rather than by careful
// locking.

struct NativeThreadEntry {
    std::thread worker;
    std::mutex statusMutex;
    std::string status = "running";
    bool alive = false;
    bool detached = false;
    std::int64_t handle = 0;
};

std::unordered_map<std::int64_t, std::shared_ptr<NativeThreadEntry>>&
nativeThreads() {
    static std::unordered_map<std::int64_t, std::shared_ptr<NativeThreadEntry>>
        threads;
    return threads;
}

std::mutex& nativeThreadsMutex() {
    static std::mutex mutex;
    return mutex;
}

std::int64_t nextThreadHandle() {
    static std::int64_t next = 1;
    return next++;
}

std::shared_ptr<NativeThreadEntry> requireThread(const std::vector<Value>& args,
                                                 const char* name, int line,
                                                 int column) {
    if (args.size() != 1 || !isNonNegativeInt(args[0])) {
        fail(std::string(name) + "(handle) expects a thread handle", line,
             column);
    }
    const std::int64_t handle = std::get<std::int64_t>(args[0]);
    if (handle == 0) {
        fail("invalid native thread handle", line, column);
    }
    std::lock_guard<std::mutex> guard(nativeThreadsMutex());
    const auto found = nativeThreads().find(handle);
    if (found == nativeThreads().end()) {
        fail("unknown or already released native thread", line, column);
    }
    return found->second;
}

// Joins `entry`, releasing the interpreter lock while it waits so the worker
// can take it. Erases the handle, so a second join is an unknown handle.
std::string joinThread(std::int64_t handle,
                       const std::shared_ptr<NativeThreadEntry>& entry,
                       int line, int column) {
    if (entry->detached) {
        fail("cannot join a detached native thread", line, column);
    }
    unlockInterpreter();
    if (entry->worker.joinable()) {
        entry->worker.join();
    }
    lockInterpreter();
    std::string status;
    {
        std::lock_guard<std::mutex> guard(entry->statusMutex);
        status = entry->status;
    }
    {
        std::lock_guard<std::mutex> guard(nativeThreadsMutex());
        nativeThreads().erase(handle);
    }
    return status;
}

Value builtinNativeThreadStart(const std::vector<Value>& args, Environment& environment,
                               int line, int column) {
    if (args.size() != 2 || !std::holds_alternative<std::shared_ptr<List>>(args[1])) {
        fail("nativeThreadStart(function, arguments) expects a function and list",
             line, column);
    }
    const auto* function =
        std::get_if<std::shared_ptr<CodeblockValue>>(&args[0]);
    if (function == nullptr || *function == nullptr ||
        (*function)->name.empty()) {
        fail("nativeThreadStart(function, arguments) expects a function and list",
             line, column);
    }
    std::vector<Value> arguments = (*std::get_if<std::shared_ptr<List>>(&args[1]))
                                       ->elements;
    const std::string name = (*function)->name;

    auto entry = std::make_shared<NativeThreadEntry>();
    entry->alive = true;
    std::weak_ptr<NativeThreadEntry> weak = entry;
    entry->worker = std::thread([weak, &environment, name,
                                 arguments = std::move(arguments)]() {
        lockInterpreter();
        std::string status;
        try {
            environment.callUserFunction(name, arguments, {}, 0, 0);
            status = "completed";
        } catch (const std::exception& error) {
            status = error.what();
        } catch (...) {
            status = "native thread callback failed";
        }
        // Release before touching the registry so the two locks are never held
        // in opposite orders.
        unlockInterpreter();
        if (auto entry = weak.lock()) {
            {
                std::lock_guard<std::mutex> guard(entry->statusMutex);
                entry->status = status;
                entry->alive = false;
            }
            // A detached thread owns itself: it leaves the registry once it is
            // done, so nothing has to join it.
            if (entry->detached) {
                std::lock_guard<std::mutex> guard(nativeThreadsMutex());
                nativeThreads().erase(entry->handle);
            }
        }
    });

    const std::int64_t handle = nextThreadHandle();
    entry->handle = handle;
    {
        std::lock_guard<std::mutex> guard(nativeThreadsMutex());
        nativeThreads()[handle] = entry;
    }
    return handle;
}

Value builtinNativeThreadJoin(const std::vector<Value>& args, Environment&,
                              int line, int column) {
    const std::shared_ptr<NativeThreadEntry> entry =
        requireThread(args, "nativeThreadJoin", line, column);
    return joinThread(std::get<std::int64_t>(args[0]), entry, line,
                      column);
}

Value builtinNativeThreadJoinAll(const std::vector<Value>& args, Environment&,
                                 int line, int column) {
    if (!args.empty()) {
        fail("nativeThreadJoinAll() expects no arguments", line, column);
    }
    std::vector<std::pair<std::int64_t, std::shared_ptr<NativeThreadEntry>>>
        pending;
    {
        std::lock_guard<std::mutex> guard(nativeThreadsMutex());
        for (const auto& pair : nativeThreads()) {
            if (!pair.second->detached) {
                pending.push_back(pair);
            }
        }
    }
    for (const auto& pair : pending) {
        joinThread(pair.first, pair.second, line, column);
    }
    return std::int64_t{0};
}

Value builtinNativeThreadIsAlive(const std::vector<Value>& args, Environment&,
                                 int line, int column) {
    const std::shared_ptr<NativeThreadEntry> entry =
        requireThread(args, "nativeThreadIsAlive", line, column);
    std::lock_guard<std::mutex> guard(entry->statusMutex);
    return entry->alive;
}

Value builtinNativeThreadStatus(const std::vector<Value>& args, Environment&,
                                int line, int column) {
    const std::shared_ptr<NativeThreadEntry> entry =
        requireThread(args, "nativeThreadStatus", line, column);
    std::lock_guard<std::mutex> guard(entry->statusMutex);
    return entry->status;
}

Value builtinNativeThreadDetach(const std::vector<Value>& args, Environment&,
                                int line, int column) {
    const std::shared_ptr<NativeThreadEntry> entry =
        requireThread(args, "nativeThreadDetach", line, column);
    {
        std::lock_guard<std::mutex> guard(nativeThreadsMutex());
        if (entry->detached) {
            fail("native thread is already detached", line, column);
        }
        entry->detached = true;
    }
    if (entry->worker.joinable()) {
        entry->worker.detach();
    }
    return std::int64_t{0};
}

// --- Native FFI --------------------------------------------------------------

struct FfiCallbackRecord {
    std::string signature;
    std::string function;
};

std::unordered_map<void*, void*>& ffiLibraries() {
    static std::unordered_map<void*, void*> libraries;
    return libraries;
}

std::unordered_map<void*, void*>& ffiFunctions() {
    static std::unordered_map<void*, void*> functions;
    return functions;
}

std::unordered_map<std::int64_t, FfiCallbackRecord>& ffiCallbacks() {
    static std::unordered_map<std::int64_t, FfiCallbackRecord> callbacks;
    return callbacks;
}

std::int64_t nextFfiCallbackHandle() {
    static std::int64_t next = -1;
    return next--;
}

Value builtinFfiLoadLibrary(const std::vector<Value>& args, Environment&, int line,
                            int column) {
    if (args.size() != 1 || !std::holds_alternative<std::string>(args[0])) {
        fail("ffiLoadLibrary(path) expects a library path", line, column);
    }
    void* handle = ::dlopen(std::get<std::string>(args[0]).c_str(),
                            RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        fail("ffiLoadLibrary() failed: " +
                 std::string(::dlerror() == nullptr ? "unknown error"
                                                     : ::dlerror()),
             line, column);
    }
    ffiLibraries()[handle] = handle;
    return static_cast<std::int64_t>(reinterpret_cast<std::intptr_t>(handle));
}

void* ffiAddress(const Value& value, const char* name, int line, int column) {
    const auto* integer = std::get_if<std::int64_t>(&value);
    if (integer == nullptr || *integer == 0) {
        fail(std::string(name) + "() expects a non-zero native address", line,
             column);
    }
    return reinterpret_cast<void*>(static_cast<std::intptr_t>(*integer));
}

Value builtinFfiLookup(const std::vector<Value>& args, Environment&, int line,
                       int column) {
    if (args.size() != 2 || !std::holds_alternative<std::string>(args[1])) {
        fail("ffiLookup(library, symbol) expects a library handle and symbol",
             line, column);
    }
    void* library = ffiAddress(args[0], "ffiLookup", line, column);
    if (ffiLibraries().find(library) == ffiLibraries().end()) {
        fail("ffiLookup() received an unknown library handle", line, column);
    }
    void* symbol = ::dlsym(library, std::get<std::string>(args[1]).c_str());
    if (symbol == nullptr) {
        const char* error = ::dlerror();
        fail("ffiLookup() failed: " +
                 std::string(error == nullptr ? "symbol not found" : error),
             line, column);
    }
    ffiFunctions()[symbol] = library;
    return static_cast<std::int64_t>(
        reinterpret_cast<std::intptr_t>(symbol));
}

Value builtinFfiCloseLibrary(const std::vector<Value>& args, Environment&,
                             int line, int column) {
    if (args.size() != 1) {
        fail("ffiCloseLibrary(library) expects a library handle", line, column);
    }
    void* library = ffiAddress(args[0], "ffiCloseLibrary", line, column);
    const auto found = ffiLibraries().find(library);
    if (found == ffiLibraries().end()) {
        fail("ffiCloseLibrary() received an unknown library handle", line,
             column);
    }
    ::dlclose(library);
    ffiLibraries().erase(found);
    return std::int64_t{0};
}

Value builtinFfiCall(const std::vector<Value>& args, Environment& environment,
                    int line, int column) {
    if (args.size() != 3 || !std::holds_alternative<std::string>(args[1]) ||
        asList(args[2]) == nullptr) {
        fail("ffiCall(address, signature, arguments) expects an address, "
             "signature, and list",
             line, column);
    }
    const auto* address = std::get_if<std::int64_t>(&args[0]);
    if (address == nullptr || *address == 0) {
        fail("ffiCall() expects a non-zero function address", line, column);
    }
    const auto callback = ffiCallbacks().find(*address);
    const auto& values = listElements(args[2]);
    if (callback != ffiCallbacks().end()) {
        if (values.size() != 2) {
            fail("ffiCall() callback expects two arguments", line, column);
        }
        return environment.callUserFunction(callback->second.function, values,
                                            {}, line, column);
    }
    const void* nativeAddress =
        reinterpret_cast<void*>(static_cast<std::intptr_t>(*address));
    const auto owner = ffiFunctions().find(const_cast<void*>(nativeAddress));
    if (owner != ffiFunctions().end() &&
        ffiLibraries().find(owner->second) == ffiLibraries().end()) {
        fail("ffiCall() function address belongs to a closed library", line,
             column);
    }
    return callNative(const_cast<void*>(nativeAddress), std::get<std::string>(args[1]),
                      values, line, column);
}

Value builtinFfiCallback(const std::vector<Value>& args, Environment&, int line,
                         int column) {
    if (args.size() != 2 || !std::holds_alternative<std::string>(args[0])) {
        fail("ffiCallback(signature, function) expects a signature and Lynxer "
             "function",
             line, column);
    }
    const auto* function = std::get_if<std::shared_ptr<CodeblockValue>>(&args[1]);
    if (function == nullptr || *function == nullptr || (*function)->name.empty()) {
        fail("ffiCallback() expects a function", line, column);
    }
    const std::int64_t handle = nextFfiCallbackHandle();
    ffiCallbacks()[handle] =
        FfiCallbackRecord{std::get<std::string>(args[0]), (*function)->name};
    return handle;
}

Value builtinFfiFreeCallback(const std::vector<Value>& args, Environment&,
                             int line, int column) {
    if (args.size() != 1) {
        fail("ffiFreeCallback(callback) expects a function address", line,
             column);
    }
    const auto* handle = std::get_if<std::int64_t>(&args[0]);
    if (handle == nullptr || ffiCallbacks().erase(*handle) == 0) {
        fail("ffiFreeCallback() received an unknown callback", line, column);
    }
    return std::int64_t{0};
}

// --- Cooperative async helpers ----------------------------------------------
//
// Lynxer has one interpreter thread, so async functions are represented by
// ordinary local functions and `await` evaluates their operation immediately.
// The resource side of the API is still real: poll(2), monotonic timers, and
// pipe-backed wakeups are managed here and use the same file/socket handles as
// the other native APIs.

struct AsyncRegistration {
    std::string token;
    std::string events;
    short mask = 0;
};

struct AsyncTimer {
    std::int64_t poll = 0;
    std::chrono::steady_clock::time_point deadline;
    std::chrono::milliseconds repeat{0};
    std::string token;
};

struct AsyncWakeup {
    std::int64_t poll = 0;
    int read = -1;
    int write = -1;
    std::string token;
};

struct AsyncPoll {
    std::unordered_map<int, AsyncRegistration> registrations;
    std::unordered_map<std::int64_t, AsyncTimer> timers;
    std::vector<std::int64_t> wakeups;
    bool closed = false;
};

std::unordered_map<std::int64_t, AsyncPoll>& asyncPolls() {
    static std::unordered_map<std::int64_t, AsyncPoll> values;
    return values;
}

std::unordered_map<std::int64_t, AsyncTimer>& asyncTimers() {
    static std::unordered_map<std::int64_t, AsyncTimer> values;
    return values;
}

std::unordered_map<std::int64_t, AsyncWakeup>& asyncWakeups() {
    static std::unordered_map<std::int64_t, AsyncWakeup> values;
    return values;
}

std::int64_t nextAsyncHandle() {
    static std::int64_t next = 1;
    return next++;
}

AsyncPoll& requireAsyncPoll(const Value& value, const char* name, int line,
                            int column) {
    const auto* handle = std::get_if<std::int64_t>(&value);
    if (handle == nullptr || *handle < 0) {
        fail(std::string(name) + "(poll) expects a poll handle", line, column);
    }
    const auto found = asyncPolls().find(*handle);
    if (found == asyncPolls().end() || found->second.closed) {
        fail(std::string(name) + "() received an unknown or closed poll handle",
             line, column);
    }
    return found->second;
}

int asyncResourceFd(const Value& value, const char* name, int line,
                    int column) {
    const auto* handle = std::get_if<std::int64_t>(&value);
    if (handle == nullptr || *handle < 0) {
        fail(std::string(name) + "() expects a resource handle", line, column);
    }
    const auto file = openFiles().find(*handle);
    if (file != openFiles().end()) {
        return file->second;
    }
    const auto socket = openSockets().find(*handle);
    if (socket != openSockets().end()) {
        return socket->second.descriptor;
    }
    const auto process = childProcesses().find(*handle);
    if (process != childProcesses().end()) {
        if (process->second.output >= 0) {
            return process->second.output;
        }
        if (process->second.error >= 0) {
            return process->second.error;
        }
    }
    fail(std::string(name) + "() received an unknown resource handle", line,
         column);
}

short asyncMask(const Value& value, const char* name, std::string& normalized,
                int line, int column) {
    if (!std::holds_alternative<std::string>(value)) {
        fail(std::string(name) + "() events must be read, write, or readwrite",
             line, column);
    }
    normalized = lowercase(std::get<std::string>(value));
    if (normalized == "read") {
        return POLLIN;
    }
    if (normalized == "write") {
        return POLLOUT;
    }
    if (normalized == "readwrite") {
        return POLLIN | POLLOUT;
    }
    fail(std::string(name) + "() events must be read, write, or readwrite",
         line, column);
}

std::string asyncEventJson(const std::string& kind, const std::string& token,
                           int fd, const std::string& events) {
    std::string result = "{\"kind\":\"" + kind + "\",\"token\":\"";
    for (char character : token) {
        if (character == '"' || character == '\\') {
            result += '\\';
        }
        result += character;
    }
    result += "\"";
    if (fd >= 0) {
        result += ",\"fd\":" + std::to_string(fd);
    }
    if (!events.empty()) {
        result += ",\"events\":[";
        bool first = true;
        if (events == "read" || events == "readwrite") {
            result += "\"read\"";
            first = false;
        }
        if (events == "write" || events == "readwrite") {
            if (!first) {
                result += ",";
            }
            result += "\"write\"";
        }
        result += "]";
    }
    return result + "}";
}

Value builtinAsyncPollCreate(const std::vector<Value>& args, Environment&,
                             int line, int column) {
    if (!args.empty()) {
        fail("asyncPollCreate() expects no arguments", line, column);
    }
    const std::int64_t handle = nextAsyncHandle();
    asyncPolls().emplace(handle, AsyncPoll{});
    return handle;
}

Value builtinAsyncPollRegister(const std::vector<Value>& args, Environment&,
                               int line, int column) {
    if (args.size() != 4 || !std::holds_alternative<std::string>(args[3])) {
        fail("asyncPollRegister(poll, resource, events, token) expects four "
             "arguments",
             line, column);
    }
    const auto* pollHandle = std::get_if<std::int64_t>(&args[0]);
    if (pollHandle == nullptr) {
        fail("asyncPollRegister() expects a poll handle", line, column);
    }
    AsyncPoll& poll = requireAsyncPoll(args[0], "asyncPollRegister", line,
                                       column);
    const int fd = asyncResourceFd(args[1], "asyncPollRegister", line, column);
    std::string events;
    const short mask =
        asyncMask(args[2], "asyncPollRegister", events, line, column);
    if (poll.registrations.count(fd) != 0) {
        fail("asyncPollRegister() resource is already registered", line,
             column);
    }
    poll.registrations.emplace(
        fd, AsyncRegistration{std::get<std::string>(args[3]), events, mask});
    return std::int64_t{0};
}

Value builtinAsyncPollModify(const std::vector<Value>& args, Environment&,
                             int line, int column) {
    if (args.size() != 4 || !std::holds_alternative<std::string>(args[3])) {
        fail("asyncPollModify(poll, resource, events, token) expects four "
             "arguments",
             line, column);
    }
    AsyncPoll& poll = requireAsyncPoll(args[0], "asyncPollModify", line,
                                       column);
    const int fd = asyncResourceFd(args[1], "asyncPollModify", line, column);
    std::string events;
    const short mask =
        asyncMask(args[2], "asyncPollModify", events, line, column);
    const auto found = poll.registrations.find(fd);
    if (found == poll.registrations.end()) {
        fail("asyncPollModify() resource is not registered", line, column);
    }
    found->second = AsyncRegistration{std::get<std::string>(args[3]), events,
                                      mask};
    return std::int64_t{0};
}

Value builtinAsyncPollRemove(const std::vector<Value>& args, Environment&,
                             int line, int column) {
    if (args.size() != 2) {
        fail("asyncPollRemove(poll, resource) expects two arguments", line,
             column);
    }
    AsyncPoll& poll = requireAsyncPoll(args[0], "asyncPollRemove", line,
                                       column);
    const int fd = asyncResourceFd(args[1], "asyncPollRemove", line, column);
    if (poll.registrations.erase(fd) == 0) {
        fail("asyncPollRemove() resource is not registered", line, column);
    }
    return std::int64_t{0};
}

Value asyncPollWaitValues(AsyncPoll& poll, std::int64_t timeout,
                          std::int64_t maximum, int line, int column) {
    if (maximum <= 0) {
        fail("asyncPollWait max_events must be positive", line, column);
    }
    std::vector<struct pollfd> descriptors;
    std::vector<int> fds;
    for (const auto& entry : poll.registrations) {
        descriptors.push_back(pollfd{entry.first, entry.second.mask, 0});
        fds.push_back(entry.first);
    }
    for (const auto handle : poll.wakeups) {
        const auto found = asyncWakeups().find(handle);
        if (found != asyncWakeups().end()) {
            descriptors.push_back(pollfd{found->second.read, POLLIN, 0});
            fds.push_back(found->second.read);
        }
    }
    int waitMs = timeout < 0 ? -1 : static_cast<int>(std::min<std::int64_t>(
                                              timeout, 2147483647));
    const auto now = std::chrono::steady_clock::now();
    for (const auto& entry : poll.timers) {
        const auto duration = entry.second.deadline - now;
        const auto remaining = std::chrono::duration_cast<
            std::chrono::milliseconds>(duration).count();
        // Round up so a sub-millisecond remainder does not turn into a
        // zero-time poll immediately before a timer is due.
        const std::int64_t rounded =
            duration <= std::chrono::steady_clock::duration::zero()
                ? 0
                : remaining + 1;
        waitMs = waitMs < 0
                     ? static_cast<int>(std::min<std::int64_t>(
                           2147483647, rounded))
                     : std::min(waitMs, static_cast<int>(std::min<std::int64_t>(
                                             2147483647, rounded)));
    }
    const int status = ::poll(descriptors.data(), descriptors.size(), waitMs);
    if (status < 0) {
        if (errno == EINTR) {
            return {};
        }
        failErrno("asyncPollWait", line, column);
    }
    std::vector<Value> events;
    for (std::size_t index = 0; index < descriptors.size() &&
                                events.size() < static_cast<std::size_t>(maximum);
         ++index) {
        if (descriptors[index].revents == 0) {
            continue;
        }
        bool isWakeup = false;
        for (const auto wakeHandle : poll.wakeups) {
            const auto found = asyncWakeups().find(wakeHandle);
            if (found != asyncWakeups().end() &&
                found->second.read == fds[index]) {
                char buffer[64];
                (void)::read(found->second.read, buffer, sizeof(buffer));
                events.push_back(asyncEventJson("wakeup", found->second.token,
                                                -1, ""));
                isWakeup = true;
                break;
            }
        }
        if (isWakeup) {
            continue;
        }
        const auto registration = poll.registrations.find(fds[index]);
        if (registration != poll.registrations.end()) {
            std::string ready;
            if (descriptors[index].revents & POLLIN) {
                ready = "read";
            }
            if (descriptors[index].revents & POLLOUT) {
                ready = ready.empty() ? "write" : "readwrite";
            }
            events.push_back(asyncEventJson("io", registration->second.token,
                                            fds[index], ready));
        }
    }
    const auto after = std::chrono::steady_clock::now();
    for (auto& entry : poll.timers) {
        if (events.size() >= static_cast<std::size_t>(maximum) ||
            after < entry.second.deadline) {
            continue;
        }
        events.push_back(
            asyncEventJson("timer", entry.second.token, -1, ""));
        if (entry.second.repeat.count() > 0) {
            entry.second.deadline = after + entry.second.repeat;
        } else {
            entry.second.deadline = after + std::chrono::hours(24 * 365);
        }
    }
    return makeList(std::move(events));
}

Value builtinAsyncPollWait(const std::vector<Value>& args, Environment&,
                           int line, int column) {
    if (args.size() < 1 || args.size() > 3) {
        fail("asyncPollWait(poll, timeout_ms?, max_events?) expects one to "
             "three arguments",
             line, column);
    }
    std::int64_t timeout = args.size() > 1 ? toInt(args[1], line, column) : -1;
    std::int64_t maximum = args.size() > 2 ? toInt(args[2], line, column) : 64;
    return asyncPollWaitValues(
        requireAsyncPoll(args[0], "asyncPollWait", line, column), timeout,
        maximum, line, column);
}

Value builtinAsyncPollDispatch(const std::vector<Value>& args,
                               Environment& environment, int line, int column) {
    if (args.size() < 2 || args.size() > 4) {
        fail("asyncPollDispatch(poll, callback, timeout_ms?, max_events?) "
             "expects two to four arguments",
             line, column);
    }
    const auto* callback = std::get_if<std::shared_ptr<CodeblockValue>>(&args[1]);
    if (callback == nullptr || *callback == nullptr || (*callback)->name.empty()) {
        fail("asyncPollDispatch() callback must be a function", line, column);
    }
    const std::int64_t timeout = args.size() > 2 ? toInt(args[2], line, column)
                                                 : -1;
    const std::int64_t maximum = args.size() > 3 ? toInt(args[3], line, column)
                                                  : 64;
    const Value values = asyncPollWaitValues(
        requireAsyncPoll(args[0], "asyncPollDispatch", line, column), timeout,
        maximum, line, column);
    const auto* list = asList(values);
    for (const Value& event : (*list)->elements) {
        environment.callUserFunction((*callback)->name, {event}, {}, line,
                                     column);
    }
    return static_cast<std::int64_t>((*list)->elements.size());
}

Value builtinAsyncPollClose(const std::vector<Value>& args, Environment&,
                            int line, int column) {
    if (args.size() != 1) {
        fail("asyncPollClose(poll) expects one poll handle", line, column);
    }
    const auto* handle = std::get_if<std::int64_t>(&args[0]);
    AsyncPoll& poll = requireAsyncPoll(args[0], "asyncPollClose", line, column);
    for (const auto wakeHandle : poll.wakeups) {
        const auto found = asyncWakeups().find(wakeHandle);
        if (found != asyncWakeups().end()) {
            ::close(found->second.read);
            ::close(found->second.write);
            asyncWakeups().erase(found);
        }
    }
    for (const auto& timer : poll.timers) {
        asyncTimers().erase(timer.first);
    }
    poll.closed = true;
    asyncPolls().erase(*handle);
    return std::int64_t{0};
}

Value builtinAsyncTimerCreate(const std::vector<Value>& args, Environment&,
                              int line, int column) {
    if (args.size() != 3 && args.size() != 4) {
        fail("asyncTimerCreate(poll, milliseconds, token, repeat_ms?) expects "
             "three or four arguments",
             line, column);
    }
    if (!std::holds_alternative<std::string>(args[2])) {
        fail("asyncTimerCreate token must be a string", line, column);
    }
    const std::int64_t delay = toInt(args[1], line, column);
    const std::int64_t repeat = args.size() == 4 ? toInt(args[3], line, column) : 0;
    if (delay < 0 || repeat < 0) {
        fail("asyncTimerCreate milliseconds must be nonnegative", line, column);
    }
    const auto* pollHandle = std::get_if<std::int64_t>(&args[0]);
    if (pollHandle == nullptr) {
        fail("asyncTimerCreate() expects a poll handle", line, column);
    }
    AsyncPoll& poll = requireAsyncPoll(args[0], "asyncTimerCreate", line, column);
    const std::int64_t handle = nextAsyncHandle();
    AsyncTimer timer{*pollHandle, std::chrono::steady_clock::now() +
                                      std::chrono::milliseconds(delay),
                     std::chrono::milliseconds(repeat),
                     std::get<std::string>(args[2])};
    poll.timers.emplace(handle, timer);
    asyncTimers().emplace(handle, timer);
    return handle;
}

Value builtinAsyncTimerCancel(const std::vector<Value>& args, Environment&,
                              int line, int column) {
    if (args.size() != 1) {
        fail("asyncTimerCancel(timer) expects a valid timer handle", line,
             column);
    }
    const auto* handle = std::get_if<std::int64_t>(&args[0]);
    const auto found = asyncTimers().find(handle == nullptr ? -1 : *handle);
    if (found == asyncTimers().end()) {
        fail("asyncTimerCancel() received an unknown or cancelled timer", line,
             column);
    }
    const auto poll = asyncPolls().find(found->second.poll);
    if (poll != asyncPolls().end()) {
        poll->second.timers.erase(found->first);
    }
    asyncTimers().erase(found);
    return std::int64_t{0};
}

Value builtinAsyncWakeupCreate(const std::vector<Value>& args, Environment&,
                               int line, int column) {
    if (args.size() != 2 || !std::holds_alternative<std::string>(args[1])) {
        fail("asyncWakeupCreate(poll, token) expects a poll and string token",
             line, column);
    }
    const auto* pollHandle = std::get_if<std::int64_t>(&args[0]);
    AsyncPoll& poll = requireAsyncPoll(args[0], "asyncWakeupCreate", line,
                                       column);
    int descriptors[2];
    if (::pipe(descriptors) != 0) {
        failErrno("asyncWakeupCreate", line, column);
    }
    const std::int64_t handle = nextAsyncHandle();
    asyncWakeups().emplace(
        handle, AsyncWakeup{*pollHandle, descriptors[0], descriptors[1],
                            std::get<std::string>(args[1])});
    poll.wakeups.push_back(handle);
    return handle;
}

Value builtinAsyncWakeupSignal(const std::vector<Value>& args, Environment&,
                               int line, int column) {
    if (args.size() != 1) {
        fail("asyncWakeupSignal(wakeup) expects one wakeup handle", line,
             column);
    }
    const auto* handle = std::get_if<std::int64_t>(&args[0]);
    const auto found = asyncWakeups().find(handle == nullptr ? -1 : *handle);
    if (found == asyncWakeups().end()) {
        fail("asyncWakeupSignal() received an unknown wakeup handle", line,
             column);
    }
    const char value = 1;
    if (::write(found->second.write, &value, 1) < 0 && errno != EAGAIN) {
        failErrno("asyncWakeupSignal", line, column);
    }
    return std::int64_t{0};
}

Value builtinAsyncWakeupClose(const std::vector<Value>& args, Environment&,
                              int line, int column) {
    if (args.size() != 1) {
        fail("asyncWakeupClose(wakeup) expects one wakeup handle", line,
             column);
    }
    const auto* handle = std::get_if<std::int64_t>(&args[0]);
    const auto found = asyncWakeups().find(handle == nullptr ? -1 : *handle);
    if (found == asyncWakeups().end()) {
        fail("asyncWakeupClose() received an unknown wakeup handle", line,
             column);
    }
    auto poll = asyncPolls().find(found->second.poll);
    if (poll != asyncPolls().end()) {
        poll->second.wakeups.erase(
            std::remove(poll->second.wakeups.begin(), poll->second.wakeups.end(),
                        *handle),
            poll->second.wakeups.end());
    }
    ::close(found->second.read);
    ::close(found->second.write);
    asyncWakeups().erase(found);
    return std::int64_t{0};
}

Value builtinAsyncSleep(const std::vector<Value>& args, Environment&, int line,
                        int column) {
    if (args.size() != 1 || !isNumber(args[0])) {
        fail("asyncSleep(seconds) expects a single numeric argument", line,
             column);
    }
    const double seconds = asNumber(args[0], line, column);
    if (seconds < 0) {
        fail("asyncSleep(seconds) expects a non-negative duration", line,
             column);
    }
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    return Value{};
}

Value builtinAsyncRun(const std::vector<Value>& args, Environment& environment,
                      int line, int column) {
    if (args.empty()) {
        fail("asyncRun(function, arguments?) expects a function", line, column);
    }
    const auto* function = std::get_if<std::shared_ptr<CodeblockValue>>(&args[0]);
    if (function == nullptr || *function == nullptr || (*function)->name.empty()) {
        fail("asyncRun() expects a function", line, column);
    }
    std::vector<Value> arguments;
    if (args.size() == 2) {
        const auto* list = asList(args[1]);
        if (list == nullptr) {
            fail("asyncRun() arguments must be a list", line, column);
        }
        arguments = (*list)->elements;
    } else if (args.size() > 2) {
        fail("asyncRun(function, arguments?) expects one or two arguments", line,
             column);
    }
    return environment.callUserFunction((*function)->name, arguments, {}, line,
                                        column);
}

Value builtinAsyncGather(const std::vector<Value>& args, Environment&,
                         int, int) {
    return makeList(args);
}

void joinNativeThreadsAtExitInternal() {
#if LYNXER_POSIX_BUILTINS
    // A program may start a thread and never join it. Such a worker calls back
    // into the interpreter, so it has to finish before the environment it
    // captured goes away.
    std::vector<std::pair<std::int64_t, std::shared_ptr<NativeThreadEntry>>>
        pending;
    {
        std::lock_guard<std::mutex> guard(nativeThreadsMutex());
        for (const auto& pair : nativeThreads()) {
            if (!pair.second->detached) {
                pending.push_back(pair);
            }
        }
    }
    for (const auto& pair : pending) {
        if (pair.second->detached) {
            continue;
        }
        unlockInterpreter();
        if (pair.second->worker.joinable()) {
            pair.second->worker.join();
        }
        lockInterpreter();
        std::lock_guard<std::mutex> guard(nativeThreadsMutex());
        nativeThreads().erase(pair.first);
    }
#endif
}

#endif  // LYNXER_POSIX_BUILTINS

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
        {"bundledFile", builtinBundledFile},
        {"bundledFiles", builtinBundledFiles},
        {"charAt", builtinCharAt},
        {"charCode", builtinCharCode},
        {"charOf", builtinCharOf},
        {"substring", builtinSubstring},
        {"trim", builtinTrim},
        {"upper", builtinUpper},
        {"lower", builtinLower},
        {"replace", builtinReplace},
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
        {"overrideMain", builtinOverrideMain},
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
#if LYNXER_POSIX_BUILTINS
        // Managed filesystem API. Kept in `unsupportedTable()` on a host
        // without POSIX `open`/`stat`/`dirent`.
        {"filesystemOpen", builtinFilesystemOpen},
        {"filesystemRead", builtinFilesystemRead},
        {"filesystemWrite", builtinFilesystemWrite},
        {"filesystemClose", builtinFilesystemClose},
        {"filesystemStat", builtinFilesystemStat},
        {"filesystemList", builtinFilesystemList},
        {"filesystemMkdir", builtinFilesystemMkdir},
        {"filesystemRemove", builtinFilesystemRemove},
        {"filesystemRename", builtinFilesystemRename},
        {"filesystemLink", builtinFilesystemLink},
        {"filesystemReadLink", builtinFilesystemReadLink},
        {"filesystemChmod", builtinFilesystemChmod},
        // Managed process API.
        {"processSpawn", builtinProcessSpawn},
        {"processWrite", builtinProcessWrite},
        {"processCloseInput", builtinProcessCloseInput},
        {"processRead", builtinProcessRead},
        {"processPoll", builtinProcessPoll},
        {"processWait", builtinProcessWait},
        {"processSendSignal", builtinProcessSendSignal},
        {"processClose", builtinProcessClose},
        // Managed networking API.
        {"networkingOpen", builtinNetworkingOpen},
        {"networkingBind", builtinNetworkingBind},
        {"networkingListen", builtinNetworkingListen},
        {"networkingAccept", builtinNetworkingAccept},
        {"networkingConnect", builtinNetworkingConnect},
        {"networkingSend", builtinNetworkingSend},
        {"networkingReceive", builtinNetworkingReceive},
        {"networkingClose", builtinNetworkingClose},
        {"networkingShutdown", builtinNetworkingShutdown},
        {"networkingBlocking", builtinNetworkingBlocking},
        {"networkingOption", builtinNetworkingOption},
        {"networkingResolve", builtinNetworkingResolve},
        {"networkingAddress", builtinNetworkingAddress},
        // Managed sound API, bridged to the Rust `sound` stdlib module.
        {"soundLoad", builtinSoundLoad},
        {"soundPlay", builtinSoundPlay},
        {"soundLoop", builtinSoundLoop},
        {"soundStop", builtinSoundStop},
        {"soundPause", builtinSoundPause},
        {"soundResume", builtinSoundResume},
        {"soundSetVolume", builtinSoundSetVolume},
        {"soundIsPlaying", builtinSoundIsPlaying},
        {"soundRelease", builtinSoundRelease},
        // Managed native threads.
        {"nativeThreadStart", builtinNativeThreadStart},
        {"nativeThreadJoin", builtinNativeThreadJoin},
        {"nativeThreadJoinAll", builtinNativeThreadJoinAll},
        {"nativeThreadIsAlive", builtinNativeThreadIsAlive},
        {"nativeThreadStatus", builtinNativeThreadStatus},
        {"nativeThreadDetach", builtinNativeThreadDetach},
        // Cooperative async API.
        {"asyncRun", builtinAsyncRun},
        {"asyncGather", builtinAsyncGather},
        {"asyncPollCreate", builtinAsyncPollCreate},
        {"asyncPollRegister", builtinAsyncPollRegister},
        {"asyncPollModify", builtinAsyncPollModify},
        {"asyncPollRemove", builtinAsyncPollRemove},
        {"asyncPollWait", builtinAsyncPollWait},
        {"asyncPollDispatch", builtinAsyncPollDispatch},
        {"asyncPollClose", builtinAsyncPollClose},
        {"asyncTimerCreate", builtinAsyncTimerCreate},
        {"asyncTimerCancel", builtinAsyncTimerCancel},
        {"asyncWakeupCreate", builtinAsyncWakeupCreate},
        {"asyncWakeupSignal", builtinAsyncWakeupSignal},
        {"asyncWakeupClose", builtinAsyncWakeupClose},
        {"asyncSleep", builtinAsyncSleep},
        // Dynamic libraries and typed native calls.
        {"ffiLoadLibrary", builtinFfiLoadLibrary},
        {"ffiLookup", builtinFfiLookup},
        {"ffiCloseLibrary", builtinFfiCloseLibrary},
        {"ffiCall", builtinFfiCall},
        {"ffiCallback", builtinFfiCallback},
        {"ffiFreeCallback", builtinFfiFreeCallback},
#endif
    };
    return handlers;
}

const std::unordered_set<std::string>& unsupportedTable() {
    static const std::unordered_set<std::string> unsupported = {
        "rawPy", "rawPyx", "cleanRawPyxCache",
#if !LYNXER_POSIX_BUILTINS
        "asyncRun", "asyncGather", "asyncPollCreate", "asyncPollRegister",
        "asyncPollModify", "asyncPollRemove", "asyncPollWait",
        "asyncPollDispatch", "asyncPollClose", "asyncTimerCreate",
        "asyncTimerCancel", "asyncWakeupCreate", "asyncWakeupSignal",
        "asyncWakeupClose", "asyncSleep",
#endif
        // These names are explicit unsupported features on non-POSIX hosts.
#if !LYNXER_POSIX_BUILTINS
        "ffiLoadLibrary", "ffiLookup", "ffiCloseLibrary", "ffiCall",
        "ffiCallback", "ffiFreeCallback",
#endif
#if !LYNXER_POSIX_BUILTINS
        "soundLoad", "soundPlay", "soundLoop", "soundStop", "soundPause",
        "soundResume", "soundSetVolume", "soundIsPlaying", "soundRelease",
#endif
        "unshare",
        "getAddress", "modifyAddressValue", "getAddressValue", "functionAddress",
        "nativeFunctionAddress", "nativeCall",

        "nativeModuleLoad", "nativeModuleName", "nativeModuleFunction",
        "nativeModuleConstant", "nativeModuleType", "nativeModuleError",
        "nativeModuleDependencies", "nativeModuleClose",
#if !LYNXER_POSIX_BUILTINS
        "nativeThreadStart", "nativeThreadJoin", "nativeThreadJoinAll",
        "nativeThreadIsAlive", "nativeThreadStatus", "nativeThreadDetach",
#endif
        "nativeMutexCreate", "nativeMutexLock", "nativeMutexTryLock",
        "nativeMutexUnlock", "nativeMutexClose",
        "nativeConditionCreate", "nativeConditionWait", "nativeConditionNotify",
        "nativeConditionNotifyAll", "nativeConditionClose",
        "nativeSemaphoreCreate", "nativeSemaphoreWait", "nativeSemaphoreTryWait",
        "nativeSemaphorePost", "nativeSemaphoreClose",
        "nativeHandleAllocate", "nativeHandleAddress", "nativeHandleFree",
        "nativeHandleIsAlive",
#if !LYNXER_POSIX_BUILTINS
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
#endif
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

void joinNativeThreadsAtExit() {
#if LYNXER_POSIX_BUILTINS
    joinNativeThreadsAtExitInternal();
#endif
}

namespace {

// Ownership built-ins take variable names, not values, so they are dispatched
// from the call site rather than through the value-based handler table.
bool isOwnershipName(const std::string& name) {
    return name == "varTransfer" || name == "varTransferMutate" ||
           name == "varBorrow" || name == "varBorrowMutate" ||
           name == "varSwapAll" || name == "varSwapVal" ||
           name == "varEndBorrow" || name == "borrowing" ||
           name == "beingBorrowed";
}

std::string stripGlobalPrefix(const std::string& name) {
    const std::string prefix = "global.";
    if (name.rfind(prefix, 0) == 0) {
        return name.substr(prefix.size());
    }
    return name;
}

} // namespace

bool isOwnershipBuiltin(const std::string& name) {
    return isOwnershipName(stripGlobalPrefix(name));
}

Value callOwnershipBuiltin(const std::string& name,
                           const std::vector<std::string>& names,
                           Environment& environment, int line, int column) {
    const std::string resolved = stripGlobalPrefix(name);
    const bool unary = resolved == "varEndBorrow" || resolved == "borrowing" ||
                       resolved == "beingBorrowed";
    const std::size_t expected = unary ? 1 : 2;
    if (names.size() != expected) {
        fail(resolved + "() expects exactly " + std::to_string(expected) +
                 " variable name(s)",
             line, column);
    }
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (names[index].empty()) {
            fail(resolved + "() argument " + std::to_string(index) +
                     " must be a variable name",
                 line, column);
        }
    }
    if (resolved == "borrowing") {
        return environment.isBorrowing(names[0]);
    }
    if (resolved == "beingBorrowed") {
        return environment.isBeingBorrowed(names[0]);
    }

    std::string error;
    if (resolved == "varTransfer") {
        error = environment.transfer(names[0], names[1]);
    } else if (resolved == "varTransferMutate") {
        error = environment.transferMutate(names[0], names[1]);
    } else if (resolved == "varBorrow") {
        error = environment.borrow(names[0], names[1]);
    } else if (resolved == "varBorrowMutate") {
        error = environment.borrowMutate(names[0], names[1]);
    } else if (resolved == "varSwapAll") {
        error = environment.swapAll(names[0], names[1]);
    } else if (resolved == "varSwapVal") {
        error = environment.swapValue(names[0], names[1]);
    } else if (resolved == "varEndBorrow") {
        error = environment.endBorrow(names[0]);
    }
    if (!error.empty()) {
        fail(error, line, column);
    }
    return none();
}

bool isBuiltinName(const std::string& name) {
    if (isOwnershipBuiltin(name)) {
        return true;
    }
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
        fail("Python bridging (embedPy) is not supported in Lynxer", line,
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
        fail(resolved + "() is not supported in Lynxer yet", line, column);
    }
    fail("unknown function '" + name + "'", line, column);
}

} // namespace lynxer
