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
};

// One argument. `i` carries ints, bools and unsigned 64-bit values, `f` carries
// floats and `s` carries a NUL-terminated string (borrowed for the call).
typedef struct LynxerFfiArg {
    std::uint32_t tag;
    std::int64_t i;
    double f;
    const char* s;
} LynxerFfiArg;

// The call result. `i`, `f` and `s` are meaningful according to `tag`; a
// `cstring` result points into a thread-local buffer that stays valid until the
// next `lynxer_ffi_call`, which is exactly the interpreter's copy window.
typedef struct LynxerFfiResult {
    std::uint32_t tag;
    std::int64_t i;
    double f;
    const char* s;
} LynxerFfiResult;

extern "C" {

// Invokes the native function at `address` with the `signature`
// (`cdecl:<ret>(<arg>,...)`; `...` selects the packed four-scalar prototype).
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
