// Shared ABI header for native modules that need the packed-argument calling
// convention or a callback into the interpreter.
//
// The classic `cdecl:<ret>(<args>)` signature grammar is limited to four
// arguments of `int64` / `double` / `cstring`, which is too small for APIs such
// as the `game` module. A module can instead register a function with the
// `...` parameter token, e.g. `cdecl:int64(...)`. The interpreter then calls
//
//     <ret> function(const double* nums, int64_t num_count,
//                    const char* const* strs, int64_t str_count);
//
// Numbers (Clynxer `int`, `float` and `bool` as 0/1) arrive in `nums`, strings
// in `strs`, in their original argument order within each group. Either pointer
// may be null when its count is zero.
//
// A module may also export `clynxer_module_attach_v1`, which the interpreter
// looks up after `clynxer_module_init_v1` succeeds. It hands the module a
// versioned `ClynxerHostApi` so the module can invoke a Clynxer function by name
// (used for frame callbacks) and query the interrupt flag.
//
// Both extensions are additive; modules that do not use them are unaffected.

#ifndef LYNXER_NATIVE_ABI_H
#define LYNXER_NATIVE_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Internal interpreter view of the packed arguments. It is not passed to the
// module as a struct; the four fields are passed to the function as four
// scalars in the order `nums`, `num_count`, `strs`, `str_count`.
typedef struct ClynxerArgs {
    int64_t num_count;
    const double* nums;
    int64_t str_count;
    const char* const* strs;
} ClynxerArgs;

// Host services available to a module through `clynxer_module_attach_v1`.
//
// `invoke` runs the Clynxer function `name` with either no argument or one
// numeric argument and returns 0 on success, non-zero when the callback failed.
// `interrupted` returns non-zero once the process has received SIGINT.
typedef struct ClynxerHostApi {
    int version; // 1
    void* context;
    int (*invoke)(void* context, const char* name, int has_arg, double arg);
    int (*interrupted)(void* context);
} ClynxerHostApi;

#ifdef __cplusplus
}
#endif

#endif // LYNXER_NATIVE_ABI_H
