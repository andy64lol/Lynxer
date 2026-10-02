// Export signature parsing and compatibility validation. The token grammar and
// alias normalization intentionally mirror the native-call engine in
// `lynxer/rust/ffi/src/lib.rs` (`parse_signature` / `normalize_token`); keep the
// two in sync when the grammar changes.

#include "exports.hpp"

#include <cstddef>
#include <unordered_set>

namespace lynxer {

namespace {

struct RawSignature {
    bool version2 = false;
    std::string result;
    std::vector<std::string> arguments;
};

std::string trim(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

bool splitSignature(const std::string& original, RawSignature& out,
                    std::string& error) {
    std::string normalized;
    if (original.rfind("cdecl:v2:", 0) == 0) {
        out.version2 = true;
        normalized = original.substr(9);
    } else if (original.rfind("v2:", 0) == 0) {
        out.version2 = true;
        normalized = original.substr(3);
    } else if (original.rfind("cdecl:", 0) == 0) {
        normalized = original.substr(6);
    } else {
        normalized = original;
    }
    const std::size_t open = normalized.find('(');
    const std::size_t close = normalized.rfind(')');
    if (open == std::string::npos || close == std::string::npos || open >= close) {
        error = "invalid native function signature '" + original + "'";
        return false;
    }
    out.result = normalized.substr(0, open);
    const std::string text = normalized.substr(open + 1, close - open - 1);
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t comma = text.find(',', start);
        if (comma == std::string::npos) {
            out.arguments.push_back(trim(text.substr(start)));
            break;
        }
        out.arguments.push_back(trim(text.substr(start, comma - start)));
        start = comma + 1;
    }
    return true;
}

// Mirrors `normalize_token` in the Rust engine: integer widths and `uintptr`
// collapse to int64, `double` to float64.
bool normalizeToken(const std::string& token, ExportType& out, bool allowVoid) {
    if (token == "void") {
        if (!allowVoid) {
            return false;
        }
        out = ExportType::Void;
        return true;
    }
    static const std::unordered_set<std::string> integers = {
        "int64", "uintptr", "uint64", "int32", "uint32",
        "int16", "uint16",   "int8",   "uint8"};
    if (integers.count(token) != 0) {
        out = ExportType::Int64;
        return true;
    }
    if (token == "float64" || token == "double") {
        out = ExportType::Float64;
        return true;
    }
    if (token == "cstring") {
        out = ExportType::CString;
        return true;
    }
    if (token == "bytes") {
        out = ExportType::Bytes;
        return true;
    }
    return false;
}

std::string unsupportedReason(const std::string& token) {
    if (token == "value") {
        return "exported functions do not support the 'value' type";
    }
    if (token == "...") {
        return "exported functions do not support packed signatures";
    }
    return "unsupported C type '" + token + "'";
}

bool typeMatchesExport(ExportType type, const std::string& lynxerType) {
    static const std::unordered_set<std::string> integers = {
        "int",   "int8",  "int16",  "int32",  "int64",
        "uint8", "uint16", "uint32", "uint64", "byte",
        "bit",   "bool",  "numBool"};
    static const std::unordered_set<std::string> floating = {
        "float", "float32", "float64", "num"};
    switch (type) {
        case ExportType::Int64:
            return integers.count(lynxerType) != 0;
        case ExportType::Float64:
            return floating.count(lynxerType) != 0;
        case ExportType::CString:
            return lynxerType == "str";
        case ExportType::Bytes:
            return lynxerType == "bytes";
        case ExportType::Void:
            return lynxerType == "any" || lynxerType == "none" ||
                   lynxerType == "void";
    }
    return false;
}

} // namespace

const char* exportCType(ExportType type) {
    switch (type) {
        case ExportType::Int64: return "int64_t";
        case ExportType::Float64: return "double";
        case ExportType::CString: return "const char*";
        case ExportType::Bytes: return "const uint8_t*";
        case ExportType::Void: return "void";
    }
    return "void";
}

bool parseExportSignature(const std::string& signature, ExportSignature& out,
                          std::string& error) {
    RawSignature tokens;
    if (!splitSignature(signature, tokens, error)) {
        return false;
    }
    if (tokens.version2) {
        error = "exported functions do not support the v2 signature prefix";
        return false;
    }
    if (!normalizeToken(tokens.result, out.returnType, /*allowVoid=*/true)) {
        error = unsupportedReason(tokens.result) + " as a return type in '" +
                signature + "'";
        return false;
    }
    out.parameters.clear();
    for (const std::string& token : tokens.arguments) {
        ExportType type;
        if (!normalizeToken(token, type, /*allowVoid=*/false)) {
            error = unsupportedReason(token) + " as an argument in '" +
                    signature + "'";
            return false;
        }
        out.parameters.push_back(type);
    }
    return true;
}

bool checkExportCompatibility(const Function& function,
                              const ExportSignature& signature,
                              std::string& error) {
    if (!function.codeblockParameters.empty()) {
        error = "exported function '" + function.name +
                "' cannot declare codeblock parameters";
        return false;
    }
    if (signature.parameters.size() != function.parameters.size()) {
        error = "exported function '" + function.name + "' has " +
                std::to_string(function.parameters.size()) +
                " parameter(s), but the C signature declares " +
                std::to_string(signature.parameters.size());
        return false;
    }
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        const Parameter& parameter = function.parameters[index];
        if (parameter.defaultValue != nullptr) {
            error = "exported function '" + function.name + "' parameter '" +
                    parameter.name + "' cannot have a default value";
            return false;
        }
        if (!typeMatchesExport(signature.parameters[index], parameter.type)) {
            error = "exported function '" + function.name + "' parameter '" +
                    parameter.name + "' has Lynxer type '" + parameter.type +
                    "', which does not match C type '" +
                    exportCType(signature.parameters[index]) + "'";
            return false;
        }
    }
    if (!typeMatchesExport(signature.returnType, function.returnType)) {
        error = "exported function '" + function.name + "' returns Lynxer type '" +
                function.returnType + "', which does not match C return type '" +
                exportCType(signature.returnType) + "'";
        return false;
    }
    return true;
}

} // namespace lynxer
