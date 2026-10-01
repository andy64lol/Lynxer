// C ABI between the Lynxer C++ interpreter and its Rust FFI engine.
//
// The interpreter performs every native call — `ffiCall` as well as calls into
// imported native modules — through this ABI. The engine lives in
// `lynxer/rust/ffi` (a Rust `staticlib` linked into the interpreter, NOT a
// stdlib module), and `cargo` is a required build tool.
//
// The Rust side mirrors these structs with `#[repr(C)]` in
// `lynxer/rust/ffi/src/lib.rs`; keep both definitions in sync. This header is
// deliberately outside `stdlib/` because the engine is not a Lynxer module.
#ifndef LYNXER_FFI_ABI_H
#define LYNXER_FFI_ABI_H

#include <cstdint>

// Result kinds, stored in `LynxerFfiResult::tag`.
enum LynxerFfiResultTag {
    LYNXER_FFI_VOID = 0,
    LYNXER_FFI_INT64 = 1,
    LYNXER_FFI_FLOAT64 = 2,
    LYNXER_FFI_CSTRING = 3,
    // `data`/`data_length` point at a byte buffer that stays valid until the
    // next `lynxer_ffi_call`.
    LYNXER_FFI_BYTES = 4,
    // `value` points at a recursively typed `LynxerFfiValue`.
    LYNXER_FFI_VALUE = 5,
};

// Argument kinds, stored in `LynxerFfiArg::tag`. The tag preserves the Lynxer
// value's type so the engine can enforce the same strict/promoting rules the
// interpreter always has: `int64` parameters accept only ints, `float64`
// parameters accept ints, floats and unsigned 64-bit values, and `cstring`
// parameters accept only strings.
enum LynxerFfiArgTag {
    LYNXER_FFI_ARG_INT = 0,
    LYNXER_FFI_ARG_FLOAT = 1,
    LYNXER_FFI_ARG_BOOL = 2,
    LYNXER_FFI_ARG_STRING = 3,
    LYNXER_FFI_ARG_UINT64 = 4,
    LYNXER_FFI_ARG_OTHER = 5,
    // A byte buffer, borrowed for the duration of the call.
    LYNXER_FFI_ARG_BYTES = 6,
    // A borrowed typed aggregate, used by `cdecl:v2:value(value)`.
    LYNXER_FFI_ARG_VALUE = 7,
};

enum LynxerFfiValueTag {
    LYNXER_FFI_VALUE_NULL = 0,
    LYNXER_FFI_VALUE_INT64 = 1,
    LYNXER_FFI_VALUE_FLOAT64 = 2,
    LYNXER_FFI_VALUE_BOOL = 3,
    LYNXER_FFI_VALUE_STRING = 4,
    LYNXER_FFI_VALUE_BYTES = 5,
    LYNXER_FFI_VALUE_ARRAY = 6,
    LYNXER_FFI_VALUE_RECORD = 7,
    LYNXER_FFI_VALUE_TUPLE = 8,
    LYNXER_FFI_VALUE_UINT64 = 9,
    LYNXER_FFI_VALUE_CHAR = 10,
    LYNXER_FFI_VALUE_ENUM = 11,
};

enum LynxerFfiRecordKind {
    LYNXER_FFI_RECORD_VARGROUP = 0,
    LYNXER_FFI_RECORD_STRUCT = 1,
    LYNXER_FFI_RECORD_CLASS = 2,
};

typedef struct LynxerFfiValue LynxerFfiValue;

typedef struct LynxerFfiValueField {
    const char* name;
    std::int64_t name_length;
    const char* type;
    std::int64_t type_length;
    std::uint32_t constant;
    const LynxerFfiValue* value;
} LynxerFfiValueField;

typedef struct LynxerFfiEnum {
    const char* enum_name;
    std::int64_t enum_name_length;
    const char* variant_name;
    std::int64_t variant_name_length;
    const LynxerFfiValueField* fields;
    std::int64_t count;
} LynxerFfiEnum;

typedef struct LynxerFfiValue {
    std::uint32_t tag;
    union {
        std::int64_t i;
        std::uint64_t u;
        double f;
        std::uint32_t boolean;
        struct {
            const std::uint8_t* data;
            std::int64_t length;
        } string;
        struct {
            const std::uint8_t* data;
            std::int64_t length;
        } bytes;
        struct {
            const LynxerFfiValue* items;
            std::int64_t count;
        } array;
        struct {
            const LynxerFfiValueField* fields;
            std::int64_t count;
            const char* type_name;
            std::int64_t type_name_length;
            const char* display_name;
            std::int64_t display_name_length;
            std::uint32_t kind;
        } record;
        LynxerFfiEnum enumeration;
    } value;
} LynxerFfiValue;

// One argument. `i` carries ints, bools and unsigned 64-bit values, `f` carries
// floats, `s` carries a NUL-terminated string, and `data`/`data_length` carry a
// byte buffer (all borrowed for the call). The `value` pointer carries a
// recursively typed value borrowed for the duration of `lynxer_ffi_call`.
typedef struct LynxerFfiArg {
    std::uint32_t tag;
    std::int64_t i;
    double f;
    const char* s;
    const std::uint8_t* data;
    std::int64_t data_length;
    const LynxerFfiValue* value;
} LynxerFfiArg;

// The call result. `i`, `f`, `s` and `data`/`data_length` are meaningful
// according to `tag`; a `cstring` or `bytes` result points into a buffer that
// stays valid until the next `lynxer_ffi_call`, which is exactly the
// interpreter's copy window. A `value` result points into module-owned storage
// that must remain valid until it is copied by the interpreter.
typedef struct LynxerFfiResult {
    std::uint32_t tag;
    std::int64_t i;
    double f;
    const char* s;
    const std::uint8_t* data;
    std::int64_t data_length;
    const LynxerFfiValue* value;
} LynxerFfiResult;

extern "C" {

// Invokes the native function at `address` with the `signature`
// (`cdecl:<ret>(<arg>,...)`; `cdecl:v2:` enables typed `value` arguments and
// results; `...` selects the packed four-scalar prototype).
// Returns 0 on success, non-zero on error. On error nothing is written to `out`
// and `lynxer_ffi_last_error()` holds a located-message the caller wraps in its
// own `SourceError`.
int lynxer_ffi_call(void* address, const char* signature,
                    const LynxerFfiArg* args, std::int64_t argument_count,
                    LynxerFfiResult* out);

// The message for the most recent failed `lynxer_ffi_call` on this thread, or a
// pointer to an empty string when the last call succeeded. Valid until the next
// call.
const char* lynxer_ffi_last_error();

}  // extern "C"

#endif  // LYNXER_FFI_ABI_H
