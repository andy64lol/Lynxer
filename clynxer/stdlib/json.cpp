// Lynxer `json` stdlib backend backed by nlohmann/json.
//
// Structured values cross the native ABI as JSON strings; the `.lynx` wrapper
// only forwards, except for list-building helpers implemented with builtins.

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>

using RegisterFunction = int (*)(const char*, const char*, const char*);
using RegisterConstant = int (*)(const char*, std::int64_t);
using RegisterType = int (*)(const char*, const char*);
using Json = nlohmann::ordered_json;

static const char* stable(std::string value) {
    thread_local std::string result;
    result = std::move(value);
    return result.c_str();
}

static std::string textOrEmpty(const char* text) {
    return text == nullptr ? std::string() : std::string(text);
}

static bool parse(const char* text, Json& value) {
    try {
        value = Json::parse(textOrEmpty(text));
        return true;
    } catch (const Json::parse_error&) {
        return false;
    }
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

static bool truthy(const Json& value) {
    if (value.is_null()) {
        return false;
    }
    if (value.is_boolean()) {
        return value.get<bool>();
    }
    if (value.is_number()) {
        return value.get<double>() != 0.0;
    }
    if (value.is_string()) {
        return !value.get_ref<const std::string&>().empty();
    }
    return !value.empty();
}

// Keep the compact output format used by the original Clynxer JSON backend:
// separators outside strings have one following space.
static std::string compactDump(const Json& value) {
    const std::string raw = value.dump();
    std::string result;
    result.reserve(raw.size() + raw.size() / 8);
    bool inString = false;
    bool escaped = false;
    for (const char character : raw) {
        if (inString) {
            result += character;
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                inString = false;
            }
            continue;
        }
        if (character == '"') {
            inString = true;
            result += character;
        } else if (character == ':' || character == ',') {
            result += character;
            result += ' ';
        } else {
            result += character;
        }
    }
    return result;
}

static std::string scalarText(const Json& value) {
    if (value.is_null()) {
        return "";
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? "true" : "false";
    }
    if (value.is_string()) {
        return value.get_ref<const std::string&>();
    }
    return compactDump(value);
}

static const Json* findField(const Json& value, const std::string& key) {
    if (!value.is_object()) {
        return nullptr;
    }
    const auto found = value.find(key);
    return found == value.end() ? nullptr : &*found;
}

extern "C" std::int64_t json_valid(const char* text) {
    Json value;
    return parse(text, value) ? 1 : 0;
}

extern "C" const char* json_parse(const char* text) {
    Json value;
    return parse(text, value) ? stable(value.dump(2)) : stable("");
}

extern "C" const char* json_pretty(const char* text) {
    Json value;
    return parse(text, value) ? stable(value.dump(4)) : stable("");
}

extern "C" const char* json_get(const char* text, const char* key) {
    Json value;
    if (!parse(text, value)) {
        return stable("");
    }
    const Json* found = findField(value, textOrEmpty(key));
    return stable(found == nullptr ? std::string() : scalarText(*found));
}

extern "C" std::int64_t json_getInt(const char* text, const char* key) {
    Json value;
    const Json* found = nullptr;
    if (!parse(text, value) || (found = findField(value, textOrEmpty(key))) == nullptr) {
        return 0;
    }
    if (found->is_number_integer() || found->is_number_unsigned()) {
        return found->get<std::int64_t>();
    }
    if (found->is_number_float()) {
        return static_cast<std::int64_t>(found->get<double>());
    }
    if (found->is_boolean()) {
        return found->get<bool>() ? 1 : 0;
    }
    if (found->is_string()) {
        return parseInt(found->get_ref<const std::string&>());
    }
    return 0;
}

extern "C" double json_getFloat(const char* text, const char* key) {
    Json value;
    const Json* found = nullptr;
    if (!parse(text, value) || (found = findField(value, textOrEmpty(key))) == nullptr) {
        return 0.0;
    }
    if (found->is_number()) {
        return found->get<double>();
    }
    if (found->is_boolean()) {
        return found->get<bool>() ? 1.0 : 0.0;
    }
    if (found->is_string()) {
        return parseDouble(found->get_ref<const std::string&>());
    }
    return 0.0;
}

extern "C" std::int64_t json_getBool(const char* text, const char* key) {
    Json value;
    const Json* found = nullptr;
    if (!parse(text, value) || (found = findField(value, textOrEmpty(key))) == nullptr) {
        return 0;
    }
    return truthy(*found) ? 1 : 0;
}

extern "C" const char* json_keys(const char* text) {
    Json value;
    if (!parse(text, value) || !value.is_object()) {
        return stable("");
    }
    std::string result;
    for (const auto& item : value.items()) {
        if (!result.empty()) {
            result += ",";
        }
        result += item.key();
    }
    return stable(std::move(result));
}

extern "C" const char* json_stringify(const char* text) {
    return stable(Json(textOrEmpty(text)).dump());
}

extern "C" std::int64_t json_has(const char* text, const char* key) {
    Json value;
    if (!parse(text, value)) {
        return 0;
    }
    const std::string wanted = textOrEmpty(key);
    if (value.is_object()) {
        return value.contains(wanted) ? 1 : 0;
    }
    if (value.is_array()) {
        for (const auto& item : value) {
            if (item.is_string() && item.get_ref<const std::string&>() == wanted) {
                return 1;
            }
        }
    }
    return 0;
}

extern "C" std::int64_t json_length(const char* text) {
    Json value;
    if (!parse(text, value)) {
        return 0;
    }
    if (value.is_object() || value.is_array() || value.is_string()) {
        return static_cast<std::int64_t>(value.size());
    }
    return 0;
}

extern "C" const char* json_set(const char* text, const char* key,
                                const char* value) {
    Json object;
    if (!parse(text, object) || !object.is_object()) {
        return stable(textOrEmpty(text));
    }
    Json parsed;
    if (parse(value, parsed)) {
        object[textOrEmpty(key)] = std::move(parsed);
    } else {
        object[textOrEmpty(key)] = textOrEmpty(value);
    }
    return stable(compactDump(object));
}

extern "C" const char* json_setInt(const char* text, const char* key,
                                   std::int64_t value) {
    Json object;
    if (!parse(text, object) || !object.is_object()) {
        return stable(textOrEmpty(text));
    }
    object[textOrEmpty(key)] = value;
    return stable(compactDump(object));
}

extern "C" const char* json_delete(const char* text, const char* key) {
    Json object;
    if (!parse(text, object) || !object.is_object()) {
        return stable(textOrEmpty(text));
    }
    object.erase(textOrEmpty(key));
    return stable(compactDump(object));
}

extern "C" const char* json_merge(const char* first, const char* second) {
    Json left;
    Json right;
    if (!parse(first, left) || !parse(second, right) ||
        !left.is_object() || !right.is_object()) {
        return stable(textOrEmpty(first));
    }
    for (const auto& item : right.items()) {
        left[item.key()] = item.value();
    }
    return stable(compactDump(left));
}

extern "C" const char* json_type(const char* text, const char* key) {
    Json value;
    if (!parse(text, value) || !value.is_object()) {
        return stable("unknown");
    }
    const Json* found = findField(value, textOrEmpty(key));
    if (found == nullptr) {
        return stable("null");
    }
    if (found->is_null()) return stable("null");
    if (found->is_boolean()) return stable("bool");
    if (found->is_number_integer() || found->is_number_unsigned()) {
        return stable("int");
    }
    if (found->is_number_float()) return stable("float");
    if (found->is_string()) return stable("string");
    if (found->is_array()) return stable("array");
    if (found->is_object()) return stable("object");
    return stable("unknown");
}

extern "C" const char* json_build(const char* pairs) {
    Json object = Json::object();
    const std::string input = textOrEmpty(pairs);
    std::size_t start = 0;
    while (start <= input.size()) {
        const std::size_t separator = input.find('|', start);
        const std::string pair = trim(input.substr(
            start, separator == std::string::npos ? std::string::npos
                                                  : separator - start));
        const std::size_t equals = pair.find('=');
        if (equals != std::string::npos) {
            object[trim(pair.substr(0, equals))] =
                trim(pair.substr(equals + 1));
        }
        if (separator == std::string::npos) {
            break;
        }
        start = separator + 1;
    }
    return stable(compactDump(object));
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