// Shared ABI header for native modules that need a byte buffer, the
// packed-argument calling convention, or a callback into the interpreter.
//
// The `cdecl:<ret>(<args>)` grammar names each parameter with a type token:
// `int64` (any integer width), `double`/`float64`, `cstring`, or `bytes`. A
// `bytes` parameter is passed as two C arguments — a pointer and a length:
//
//     <ret> function(const uint8_t* data, int64_t length);
//
// A `bytes` return is a pointer to a buffer laid out as `[int64 length]`
// followed by the payload bytes (length counts bytes). The interpreter reads
// the little-endian prefix and copies that many bytes, so the module must keep
// the buffer alive until its next call — use `lynxerBytes` below.
//
// A module can instead register a function with the `...` parameter token,
// e.g. `cdecl:int64(...)`. The interpreter then calls
//
//     <ret> function(const double* nums, int64_t num_count,
//                    const char* const* strs, int64_t str_count);
//
// Numbers (Lynxer `int`, `float` and `bool` as 0/1) arrive in `nums`, strings
// in `strs`, in their original argument order within each group. Either pointer
// may be null when its count is zero.
//
// A module may also export `lynxer_module_attach_v1`, which the interpreter
// looks up after `lynxer_module_init_v1` succeeds. It hands the module a
// versioned `LynxerHostApi` so the module can invoke a Lynxer function by name
// (used for frame callbacks) and query the interrupt flag.
//
// Both extensions are additive; modules that do not use them are unaffected.

#ifndef LYNXER_NATIVE_ABI_H
#define LYNXER_NATIVE_ABI_H

#include <stdint.h>

#ifdef __cplusplus
#include <string>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Internal interpreter view of the packed arguments. It is not passed to the
// module as a struct; the four fields are passed to the function as four
// scalars in the order `nums`, `num_count`, `strs`, `str_count`.
typedef struct LynxerArgs {
    int64_t num_count;
    const double* nums;
    int64_t str_count;
    const char* const* strs;
} LynxerArgs;

// Host services available to a module through `lynxer_module_attach_v1`.
//
// `invoke` runs the Lynxer function `name` with either no argument or one
// numeric argument and returns 0 on success, non-zero when the callback failed.
// `interrupted` returns non-zero once the process has received SIGINT.
typedef struct LynxerHostApi {
    int version; // 1
    void* context;
    int (*invoke)(void* context, const char* name, int has_arg, double arg);
    int (*interrupted)(void* context);
} LynxerHostApi;

#ifdef __cplusplus
}

// C++ helper for a `bytes` return: builds `[int64 little-endian length]` + the
// payload in a thread-local buffer and returns its address. The buffer stays
// valid until the next call on this thread, which is the ABI's copy window.
inline const uint8_t* lynxerBytes(const void* payload, int64_t length) {
    static thread_local std::string buffer;
    const int64_t stored = length < 0 ? 0 : length;
    buffer.assign(8 + static_cast<std::size_t>(stored), '\0');
    for (int index = 0; index < 8; ++index) {
        buffer[static_cast<std::size_t>(index)] = static_cast<char>(
            (static_cast<uint64_t>(stored) >> (8 * index)) & 0xFF);
    }
    if (stored > 0 && payload != nullptr) {
        buffer.replace(8, static_cast<std::size_t>(stored),
                       static_cast<const char*>(payload),
                       static_cast<std::size_t>(stored));
    }
    return reinterpret_cast<const uint8_t*>(buffer.data());
}

#endif

#endif // LYNXER_NATIVE_ABI_H
