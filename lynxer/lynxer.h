// Public C ABI for embedding a Lynxer program as a shared library.
//
// A library built by `lynxer --emit-library` links against `liblynxer.so` and
// calls `lynxer_embed_*`. C consumers get the argument/result structs and tags
// from here; C++ consumers additionally see the full `ffi_abi.h` definitions.
//
// The exported `LynxerFfiArg`/`LynxerFfiResult` types are the same wire types
// the native-call engine uses (`lynxer/ffi_abi.h`), so a value's shape is
// identical in both directions.
#ifndef LYNXER_H
#define LYNXER_H

#include <stdint.h>

#if defined(__cplusplus)
#include "ffi_abi.h"
extern "C" {
#else
// Plain-C mirrors of `LynxerFfiValue`'s argument/result carriers. Only the
// pointer is ever used opaquely; `value` arguments carry a recursively typed
// tree the C caller does not need to interpret for the v1 scalar exports.
typedef struct LynxerFfiValue LynxerFfiValue;

typedef struct LynxerFfiArg {
    uint32_t tag;
    int64_t i;
    double f;
    const char* s;
    const uint8_t* data;
    int64_t data_length;
    const LynxerFfiValue* value;
} LynxerFfiArg;

typedef struct LynxerFfiResult {
    uint32_t tag;
    int64_t i;
    double f;
    const char* s;
    const uint8_t* data;
    int64_t data_length;
    const LynxerFfiValue* value;
} LynxerFfiResult;

// Argument tags: match `LynxerFfiArgTag` in `lynxer/ffi_abi.h`.
enum {
    LYNXER_FFI_ARG_INT = 0,
    LYNXER_FFI_ARG_FLOAT = 1,
    LYNXER_FFI_ARG_BOOL = 2,
    LYNXER_FFI_ARG_STRING = 3,
    LYNXER_FFI_ARG_UINT64 = 4,
    LYNXER_FFI_ARG_OTHER = 5,
    LYNXER_FFI_ARG_BYTES = 6,
    LYNXER_FFI_ARG_VALUE = 7
};

// Result tags: match `LynxerFfiResultTag` in `lynxer/ffi_abi.h`.
enum {
    LYNXER_FFI_VOID = 0,
    LYNXER_FFI_INT64 = 1,
    LYNXER_FFI_FLOAT64 = 2,
    LYNXER_FFI_CSTRING = 3,
    LYNXER_FFI_BYTES = 4,
    LYNXER_FFI_VALUE = 5
};
#endif

typedef struct LynxerEmbedContext LynxerEmbedContext;

// Initializes the embedded program from an archive produced by the export
// builder. Runs `global setup()` if present; never runs `main()`. Returns null
// on failure, with the reason in `lynxer_embed_last_error()`. The runtime
// supports one program per process: a second call fails.
LynxerEmbedContext* lynxer_embed_init(const uint8_t* archive, int64_t length);

// Calls the exported function `name` with `argCount` tagged arguments. Returns
// 0 on success and fills `out`; on failure returns non-zero and leaves `out`
// unspecified. String and byte results stay valid until the next call on the
// same thread.
int lynxer_embed_call(LynxerEmbedContext* context, const char* name,
                      const LynxerFfiArg* args, int64_t argCount,
                      LynxerFfiResult* out);

// The message for the most recent failure on this thread, or "" if none.
const char* lynxer_embed_last_error(void);

// Clears the current thread's last error.
void lynxer_embed_reset_error(void);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // LYNXER_H
