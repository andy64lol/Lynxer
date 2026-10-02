#pragma once

#include "ast.hpp"

#include <string>
#include <vector>

namespace lynxer {

// The C types an exported Lynxer function may use (v1). `void` is only valid as
// a return type.
enum class ExportType { Int64, Float64, CString, Bytes, Void };

// A parsed `cdecl:<ret>(<args>)` signature, normalized to the v1 export types.
struct ExportSignature {
    ExportType returnType = ExportType::Void;
    std::vector<ExportType> parameters;
};

// Parses `signature` with the same grammar and alias rules the native-call
// engine uses (`lynxer/rust/ffi/src/lib.rs`), restricted to the export types.
// Rejects `value`, packed (`...`) and v2 signatures with a reason in `error`.
bool parseExportSignature(const std::string& signature, ExportSignature& out,
                          std::string& error);

// Checks that `function` can implement `signature`: matching arity, no default
// values, no codeblock parameters, and compatible parameter/return types.
bool checkExportCompatibility(const Function& function,
                              const ExportSignature& signature,
                              std::string& error);

// The single-token C spelling of an export type (`bytes` is `const uint8_t*`;
// the two-argument form is added by the shim generator).
const char* exportCType(ExportType type);

} // namespace lynxer
