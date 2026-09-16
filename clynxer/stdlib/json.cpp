// Lynxer `json` stdlib backend: a dependency-free JSON encoder/decoder.
//
// Structured values cross the native ABI as JSON strings; the `.lynx` wrapper
// only forwards, except for list-building helpers implemented with builtins.

#include "native_json.hpp"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>

using RegisterFunction = int (*)(const char*, const char*, const char*);
using RegisterConstant = int (*)(const char*, std::int64_t);
using RegisterType = int (*)(const char*, const char*);

using native_json::Type;
using native_json::Value;

static const char* stable(std::string value) {
    thread_local std::string result;
    result = std::move(value);
    return result.c_str();
}

static std::string textOrEmpty(const char* text) {
    return text == nullptr ? std::string() : std::string(text);
}

static bool isSpace(char character) {
    return character == ' ' || character == '\t' || character == '\n' ||
           character == '\r' || character == '\f' || character == '\v';
}

static std::string trim(const std::string& value) {
    std::size_t start = 0;
    std::size_t end = value.size();
    while (start < end && isSpace(value[start])) {
        ++start;
    }
    while (end > start && isSpace(value[end - 1])) {
        --end;
    }
    return value.substr(start, end - start);
}

static std::int64_t parseInt(const std::string& text) {
    const char* begin = text.c_str();
    char* end = nullptr;
    const long long value = std::strtoll(begin, &end, 10);
    if (end == begin) {
        return 0;
    }
    while (*end != '\0' && isSpace(*end)) {
        ++end;
    }
    return *end == '\0' ? static_cast<std::int64_t>(value) : 0;
}

static double parseDouble(const std::string& text) {
    const char* begin = text.c_str();
    char* end = nullptr;
    const double value = std::strtod(begin, &end);
    if (end == begin) {
        return 0.0;
    }
    while (*end != '\0' && isSpace(*end)) {
        ++end;
    }
    return *end == '\0' ? value : 0.0;
}

static std::string numberString(double value) {
    return native_json::numberToString(value);
}

static bool truthy(const Value& value) {
    switch (value.type) {
        case Type::Null: return false;
        case Type::Bool: return value.boolean;
        case Type::Integer: return value.integer != 0;
        case Type::Number: return value.number != 0.0;
        case Type::String: return !value.text.empty();
        case Type::Array: return !value.items.empty();
        case Type::Object: return !value.fields.empty();
    }
    return false;
}

static std::string scalarText(const Value& value) {
    switch (value.type) {
        case Type::Null: return "";
        case Type::Bool: return value.boolean ? "true" : "false";
        case Type::Integer: return std::to_string(value.integer);
        case Type::Number: return numberString(value.number);
        case Type::String: return value.text;
        case Type::Array:
        case Type::Object: return native_json::dump(value, false);
    }
    return "";
}

extern "C" std::int64_t json_valid(const char* text) {
    Value value;
    return native_json::parse(textOrEmpty(text), value) ? 1 : 0;
}

extern "C" const char* json_parse(const char* text) {
    Value value;
    if (!native_json::parse(textOrEmpty(text), value)) {
        return stable("");
    }
    return stable(native_json::dump(value, true, 2));
}

extern "C" const char* json_pretty(const char* text) {
    Value value;
    if (!native_json::parse(textOrEmpty(text), value)) {
        return stable("");
    }
    return stable(native_json::dump(value, true, 4));
}

extern "C" const char* json_get(const char* text, const char* key) {
    Value value;
    if (!native_json::parse(textOrEmpty(text), value)) {
        return stable("");
    }
    const Value* found = native_json::findField(value, textOrEmpty(key));
    return stable(found == nullptr ? std::string() : scalarText(*found));
}

extern "C" std::int64_t json_getInt(const char* text, const char* key) {
    Value value;
    if (!native_json::parse(textOrEmpty(text), value)) {
        return 0;
    }
    const Value* found = native_json::findField(value, textOrEmpty(key));
    if (found == nullptr) {
        return 0;
    }
    if (native_json::isNumber(*found)) {
        return native_json::asInteger(*found);
    }
    if (found->type == Type::Bool) {
        return found->boolean ? 1 : 0;
    }
    if (found->type == Type::String) {
        return parseInt(found->text);
    }
    return 0;
}

extern "C" double json_getFloat(const char* text, const char* key) {
    Value value;
    if (!native_json::parse(textOrEmpty(text), value)) {
        return 0.0;
    }
    const Value* found = native_json::findField(value, textOrEmpty(key));
    if (found == nullptr) {
        return 0.0;
    }
    if (native_json::isNumber(*found)) {
        return native_json::asDouble(*found);
    }
    if (found->type == Type::Bool) {
        return found->boolean ? 1.0 : 0.0;
    }
    if (found->type == Type::String) {
        return parseDouble(found->text);
    }
    return 0.0;
}

extern "C" std::int64_t json_getBool(const char* text, const char* key) {
    Value value;
    if (!native_json::parse(textOrEmpty(text), value)) {
        return 0;
    }
    const Value* found = native_json::findField(value, textOrEmpty(key));
    if (found == nullptr) {
        return 0;
    }
    return truthy(*found) ? 1 : 0;
}

extern "C" const char* json_keys(const char* text) {
    Value value;
    if (!native_json::parse(textOrEmpty(text), value) ||
        value.type != Type::Object) {
        return stable("");
    }
    std::string result;
    for (const auto& field : value.fields) {
        if (!result.empty()) {
            result += ",";
        }
        result += field.first;
    }
    return stable(std::move(result));
}

extern "C" const char* json_stringify(const char* text) {
    return stable(
        native_json::dump(native_json::makeString(textOrEmpty(text)), false));
}

extern "C" std::int64_t json_has(const char* text, const char* key) {
    Value value;
    if (!native_json::parse(textOrEmpty(text), value)) {
        return 0;
    }
    const std::string wanted = textOrEmpty(key);
    if (value.type == Type::Object) {
        return native_json::findField(value, wanted) == nullptr ? 0 : 1;
    }
    if (value.type == Type::Array) {
        for (const auto& item : value.items) {
            if (item.type == Type::String && item.text == wanted) {
                return 1;
            }
        }
    }
    return 0;
}

extern "C" std::int64_t json_length(const char* text) {
    Value value;
    if (!native_json::parse(textOrEmpty(text), value)) {
        return 0;
    }
    switch (value.type) {
        case Type::Object:
            return static_cast<std::int64_t>(value.fields.size());
        case Type::Array:
            return static_cast<std::int64_t>(value.items.size());
        case Type::String:
            return static_cast<std::int64_t>(value.text.size());
        default: return 0;
    }
}

extern "C" const char* json_set(const char* text, const char* key,
                                const char* value) {
    Value object;
    if (!native_json::parse(textOrEmpty(text), object) ||
        object.type != Type::Object) {
        return stable(textOrEmpty(text));
    }
    Value stored;
    Value parsed;
    if (native_json::parse(textOrEmpty(value), parsed)) {
        stored = std::move(parsed);
    } else {
        stored = native_json::makeString(textOrEmpty(value));
    }
    native_json::setField(object, textOrEmpty(key), std::move(stored));
    return stable(native_json::dump(object, false));
}

extern "C" const char* json_setInt(const char* text, const char* key,
                                   std::int64_t value) {
    Value object;
    if (!native_json::parse(textOrEmpty(text), object) ||
        object.type != Type::Object) {
        return stable(textOrEmpty(text));
    }
    native_json::setField(object, textOrEmpty(key),
                          native_json::makeInteger(value));
    return stable(native_json::dump(object, false));
}

extern "C" const char* json_delete(const char* text, const char* key) {
    Value object;
    if (!native_json::parse(textOrEmpty(text), object) ||
        object.type != Type::Object) {
        return stable(textOrEmpty(text));
    }
    native_json::removeField(object, textOrEmpty(key));
    return stable(native_json::dump(object, false));
}

extern "C" const char* json_merge(const char* first, const char* second) {
    Value left;
    Value right;
    if (!native_json::parse(textOrEmpty(first), left) ||
        !native_json::parse(textOrEmpty(second), right) ||
        left.type != Type::Object || right.type != Type::Object) {
        return stable(textOrEmpty(first));
    }
    for (auto& field : right.fields) {
        native_json::setField(left, field.first, field.second);
    }
    return stable(native_json::dump(left, false));
}

extern "C" const char* json_type(const char* text, const char* key) {
    Value value;
    if (!native_json::parse(textOrEmpty(text), value) ||
        value.type != Type::Object) {
        return stable("unknown");
    }
    const Value* found = native_json::findField(value, textOrEmpty(key));
    if (found == nullptr) {
        return stable("null");
    }
    switch (found->type) {
        case Type::Null: return stable("null");
        case Type::Bool: return stable("bool");
        case Type::Integer: return stable("int");
        case Type::Number: return stable("float");
        case Type::String: return stable("string");
        case Type::Array: return stable("array");
        case Type::Object: return stable("object");
    }
    return stable("unknown");
}

extern "C" const char* json_build(const char* pairs) {
    Value object = native_json::makeObject();
    const std::string input = textOrEmpty(pairs);
    std::size_t start = 0;
    while (start <= input.size()) {
        const std::size_t separator = input.find('|', start);
        const std::string pair = trim(input.substr(
            start, separator == std::string::npos ? std::string::npos
                                                  : separator - start));
        const std::size_t equals = pair.find('=');
        if (equals != std::string::npos) {
            native_json::setField(object, trim(pair.substr(0, equals)),
                                  native_json::makeString(
                                      trim(pair.substr(equals + 1))));
        }
        if (separator == std::string::npos) {
            break;
        }
        start = separator + 1;
    }
    return stable(native_json::dump(object, false));
}

extern "C" int lynxer_module_init_v1(RegisterFunction function,
                                     RegisterConstant, RegisterType) {
    return function("valid", "json_valid", "cdecl:int64(cstring)") &&
                   function("parse", "json_parse", "cdecl:cstring(cstring)") &&
                   function("pretty", "json_pretty", "cdecl:cstring(cstring)") &&
                   function("get", "json_get",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("getInt", "json_getInt",
                            "cdecl:int64(cstring,cstring)") &&
                   function("getFloat", "json_getFloat",
                            "cdecl:double(cstring,cstring)") &&
                   function("getBool", "json_getBool",
                            "cdecl:int64(cstring,cstring)") &&
                   function("keys", "json_keys", "cdecl:cstring(cstring)") &&
                   function("stringify", "json_stringify",
                            "cdecl:cstring(cstring)") &&
                   function("has", "json_has",
                            "cdecl:int64(cstring,cstring)") &&
                   function("length", "json_length", "cdecl:int64(cstring)") &&
                   function("set", "json_set",
                            "cdecl:cstring(cstring,cstring,cstring)") &&
                   function("setInt", "json_setInt",
                            "cdecl:cstring(cstring,cstring,int64)") &&
                   function("delete", "json_delete",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("merge", "json_merge",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("type", "json_type",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("build", "json_build", "cdecl:cstring(cstring)")
               ? 0
               : 1;
}
