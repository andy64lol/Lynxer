# Native module ABI

Lynxer's standard libraries are shared libraries (`.so` on Linux, `.dylib` on
macOS) loaded at runtime with `dlopen`. This document is the contract a native
module must implement. It applies to the bundled `stdlib/*.so` modules exactly
as it applies to third-party modules.

A module may be written in C++ (`stdlib/<name>.cpp`) or in Rust. The Rust
backends — `game`, `image`, `json`, `lua`, `network`, `server`, `sound`,
`sqldb` and `tui` — live under `rust/` and are all `cdylib`s that export
`clynxer_module_init_v1`, their ops, and (for `game`)
`clynxer_module_attach_v1` directly; there is no C++ shim. The `lynxer_abi`
crate provides the shared FFI plumbing (packed-argument view, panic guards,
string result buffer, host API, and the registration helper).

The workspace has one more member, `rust/ffi`, which is an intentional **no-op**
`cdylib`: its `clynxer_module_init_v1` registers nothing and returns `0`. The
`ffi*` builtins are implemented in C++, not by that crate, so the crate exists
only to keep the workspace uniform and should not be read as dead code.

## Entry point

Every module exports one C symbol:

```c
int clynxer_module_init_v1(
    int (*register_function)(const char *name, const char *symbol,
                             const char *signature),
    int (*register_constant)(const char *name, int64_t value),
    int (*register_type)(const char *name, const char *layout)
);
```

Return `0` after registering everything. A non-zero return, an invalid
identifier, a duplicate name, or a `symbol` that `dlsym` cannot resolve rejects
the whole module: the load fails with a *native module lifecycle failure*, and
the interpreter reports the short reason it recorded — `registered symbol not
found` for an unresolved symbol, `duplicate native registration` for a repeated
name — not the offending name.

- `register_function(name, symbol, signature)` — `name` is the Lynxer-facing
  name under the module namespace; `symbol` is the exported C name; `signature`
  uses the grammar below.
- `register_constant(name, value)` — exposed as a read-only integer field on the
  module namespace.
- `register_type(name, layout)` — exposed as a string field holding a native
  memory layout (used with the `memory*` builtins).

Loading happens in `lynxer/ast.cpp` (`ImportStatement::execute`), where
`dlopen` is called with `RTLD_NOW | RTLD_LOCAL` and the initializer is invoked
with the three callbacks. Modules are kept loaded for the lifetime of their
namespace so registered function pointers cannot dangle.

## Importing

Import the library from `setup()` with any of the `import` forms:

```lynx
global setup(){
    importAs("math.so", "nativeMath");
}

global main(){
    println(global.nativeMath.abs(-3));
}
```

A path ending in `.so` is treated as a native module; anything else is compiled
as Lynxer source. The namespace is the import alias, or the file stem when no
alias is given.

## Signature grammar

```
cdecl:<return>(<arg>,<arg>,...)
```

Type tokens are `int64` (Lynxer `int`), `double`/`float64` (Lynxer `float`) and
`cstring` (Lynxer `str`). Argument lists may be empty.

Before looking a signature up, the dispatcher **normalizes** the tokens: every
integer width is rewritten to `int64` (`int8`, `int16`, `int32`, `uint8`,
`uint16`, `uint32`, `uint64` and `uintptr`), and `double` is rewritten to
`float64`. So `cdecl:int32(int32)` and `cdecl:int64(int64)` resolve to the same
shipped shape. The supported table is the *normalized* set; the tokens above are
accepted spellings, not distinct shapes.

`cdecl:` is optional; the bare `<return>(<args>)` form is accepted too. The
packed form is selected when the argument list is exactly the single token `...`
(`types.size() == 1 && types[0] == "..."`).

## Supported signature shapes

The dispatcher in `lynxer/ast.cpp` builds the normalized shape string and looks
it up in a table. Only the shapes below are callable — anything else raises
`unsupported native signature '<sig>'`, and a shape/argument-count mismatch
raises `native call argument count does not match signature '<sig>'`.

| # | Return | Arguments |
| --- | --- | --- |
| 1 | `int64` | *(none)* |
| 2 | `float64` | *(none)* |
| 3 | `cstring` | *(none)* |
| 4 | `int64` | `int64, int64` |
| 5 | `int64` | `int64` |
| 6 | `cstring` | `cstring` |
| 7 | `int64` | `cstring` |
| 8 | `int64` | `cstring, cstring` |
| 9 | `cstring` | `cstring, int64` |
| 10 | `int64` | `int64, int64, int64` |
| 11 | `float64` | `float64` |
| 12 | `float64` | `float64, float64` |
| 13 | `float64` | `float64, float64, float64` |
| 14 | `int64` | `float64` |
| 15 | `float64` | `float64, int64` |
| 16 | `cstring` | `float64` |
| 17 | `float64` | `cstring` |
| 18 | `cstring` | `cstring, cstring` |
| 19 | `float64` | `cstring, cstring` |
| 20 | `cstring` | `cstring, cstring, cstring` |
| 21 | `cstring` | `cstring, cstring, int64` |
| 22 | `cstring` | `cstring, cstring, cstring, int64` |
| 23 | `cstring` | `cstring, cstring, cstring, cstring` |
| 24 | `int64` | `cstring, cstring, cstring` |
| 25 | `int64` | `cstring, int64` |
| 26 | `cstring` | `int64` |
| 27 | `cstring` | `int64, int64` |
| 28 | `float64` | `int64` |
| 29 | `int64` | `cstring, int64, int64` |
| 30 | `float64` | `cstring, float64` |
| 31 | `cstring` | `float64, float64, int64` |
| 32 | `cstring` | `cstring, float64, float64` |

Adding a shape is a one-line table entry plus a `reinterpret_cast` with the
matching C++ prototype; keep `lynxer/docs/stdlib/` in sync when you do.

Numeric arguments are read leniently where the shape is numeric
(`asNumber` accepts an `int` or a `float`); integer and string arguments are
strict and raise a located `SourceError` on a type mismatch.

### Packed arguments (`...`)

An API with long or variadic argument lists can use the wildcard parameter
token `...` instead of a fixed shape:

```
cdecl:int64(...)
cdecl:float64(...)
cdecl:cstring(...)
```

Only these three shapes are packed; the return type selects which one. This is
the form the `lynxer_abi` macros generate.

> **A Rust `cdylib` must use a packed signature.** The `export_int!`,
> `export_float!` and `export_string!` macros expand to the four-scalar
> prototype below, so registering such an op with a *fixed* shape is a hard
> error that is **not** caught at build time: the interpreter calls the symbol
> through the fixed shape's C prototype (`int64_t(*)(int64_t)`,
> `const char*(*)(const char*)`, …), which does not match the real function, and
> the process **segfaults on the first call** to that op. `dlopen` and
> registration both succeed, so the mistake only shows up at run time. (The one
> benign exception is a shape that is not in the fixed-shape table at all, which
> fails fast with `unsupported native signature '<sig>'`.)
>
> Because the packed form accepts any number of arguments, a wrong signature is
> never an argument-count error — it is a call through the wrong ABI.

The function receives every argument as four scalars:

```c
int64_t function(const double* nums, int64_t num_count,
                 const char* const* strs, int64_t str_count);
double  function(const double* nums, int64_t num_count,
                 const char* const* strs, int64_t str_count);
const char* function(const double* nums, int64_t num_count,
                     const char* const* strs, int64_t str_count);
```

Numbers (Lynxer `int`, `float`, and `bool` as `0`/`1`) arrive in `nums` in their
original order; strings arrive in `strs` in theirs. Either pointer is null when
its count is zero, and at most 64 arguments **in total** are accepted across
both lists (`kMaxPackedArgs` in `lynxer/ast.cpp`); a call with more raises a
located `SourceError`. Any other argument type raises a located `SourceError`.
`lynxer/stdlib/clynxer_native_abi.h` documents the convention for module
authors.

**Arguments are indexed per type, not by position.** `nums` and `strs` are
separate lists, so the *n*-th string argument is `strs[n]` and the *n*-th
numeric argument is `nums[n]`, regardless of how they were interleaved at the
call site. The `lynxer_abi` `Args` view enforces this: `args.int(i)` /
`args.float(i)` read `nums[i]` and `args.string(i)` reads `strs[i]`.
For `save(handle, path, quality)` the correct reads are `args.int(0)` for the
handle, `args.string(0)` for the path and `args.int(1)` for the quality —
**not** `args.int(0)`, `args.string(1)`, `args.int(2)`. Reading past the end of
either list yields `0` / `""` instead of failing, so a wrong index silently
produces a zero value rather than an error.

Because that failure is silent, `make test` runs
`lynxer/scripts/check_module_contracts.py`, which reads each
`stdlib/<name>.lynx` wrapper and fails if a packed op reads an argument that the
wrapper never passes — or if a wrapper calls an op the backend does not
register. Write the reads the way the wrapper passes them, and the check stays
quiet.

## Calling back into Lynxer

A module may additionally export

```c
int clynxer_module_attach_v1(const LynxerHostApi *host);
```

and the interpreter calls it (via `dlsym`) right after
`clynxer_module_init_v1` succeeds. Returning non-zero rejects the module:

```c
typedef struct LynxerHostApi {
    int version; // 1
    void *context;
    int (*invoke)(void *context, const char *name, int has_arg, double arg);
    int (*interrupted)(void *context);
} LynxerHostApi;
```

- `invoke` runs the Lynxer function `name` with no argument or one numeric
  argument, and returns `0` on success.
- `interrupted` returns non-zero once the process has received SIGINT.

The `context` pointer is opaque to the module. `invoke` resolves against the
top-level program, so frame callbacks registered by a module imported from a
source wrapper still reach the program's own functions. This entry point is
optional; modules that do not export it behave as before.

## Data conventions

The ABI has no aggregate types, so modules follow these conventions.

**Booleans** are `int64` `0`/`1`; the `.lynx` wrapper converts with `!= 0`.

**Errors** are in-band sentinels, because there is no error channel:
`-1` / `-1.0` for indices and sizes, `""` for strings, `"ERROR: ..."` for
`csv`, and `"Error: ..."` for `js`.

**Stateless structured data** crosses as a JSON string. The native side encodes
with `stdlib/native_json.hpp` (a header-only value/parser/serializer embedded
into each `.so`, so no module depends on another module's symbols), and the
wrapper either forwards the string or post-processes it with `listJson*` /
`splitStr`.

**List results** use whichever encoding the data allows:

- values that cannot contain the separator: a joined `cstring` that the wrapper
  splits with `splitStr` (newline for paths, tab for numeric vectors);
- arbitrary strings (command output): an `int64` handle into a module-local
  registry, read back with `resultCount`/`resultAt`/`codeAt` and released with
  `release` — see `stdlib/multiprocessing.cpp`.

**Cstring lifetime.** Returned `const char*` values must stay valid until the
interpreter copies them, which happens immediately after the call. Modules
return a `thread_local std::string` from a `stable()` helper and therefore
support exactly one live string result per call.

## Worked example

```cpp
#include <cstdint>
#include <string>

using RegisterFunction = int (*)(const char*, const char*, const char*);
using RegisterConstant = int (*)(const char*, std::int64_t);
using RegisterType = int (*)(const char*, const char*);

static const char* stable(std::string value) {
    thread_local std::string result;
    result = std::move(value);
    return result.c_str();
}

extern "C" std::int64_t pair_sum(std::int64_t left, std::int64_t right) {
    return left + right;
}

extern "C" const char* pair_label(const char* name, std::int64_t count) {
    return stable(std::string(name) + ":" + std::to_string(count));
}

extern "C" int clynxer_module_init_v1(RegisterFunction function,
                                     RegisterConstant,
                                     RegisterType) {
    return function("sum", "pair_sum", "cdecl:int64(int64,int64)") &&
                   function("label", "pair_label", "cdecl:cstring(cstring,int64)")
               ? 0
               : 1;
}
```

```lynx
global setup(){ importAs("pair.so", "pair"); }
global main(){
    println(global.pair.sum(2, 3));
    println(global.pair.label("items", 4));
}
```

## Extending the ABI

The ABI is deliberately small and changes only when a real module cannot be
expressed with the conventions above. Before adding anything, a module must
first try to fit one of them:

| Convention | Use it for |
| --- | --- |
| Numbers (`int64`, `float64`, `bool` as `0`/`1`) | countable, measurable or flag-like values |
| Strings (`cstring`) | text, and paths |
| Integer handles | anything the module owns and must outlive a call |
| JSON strings | stateless structured data — lists, tuples and records |
| Named callbacks (`clynxer_module_attach_v1`) | the module driving Lynxer code |

An additive ABI change is only merged with all four of:

1. **a C example** — `lynxer/examples/native_signatures.cpp` is the pattern,
   and `make test` compiles and runs it;
2. **a Rust example** — through `lynxer_abi`, because that is how the bundled
   backends are written;
3. **compatibility coverage** — the shape added to `nativeCallTable()` plus a
   fixture that exercises it, so a regression fails `make test`;
4. **documentation** — this page, and
   [stdlib-contracts.md](stdlib-contracts.md) when the change alters what a
   module may rely on.

An additive change must not alter how existing shapes or signatures behave.

## Platform support

Native modules require a POSIX host. On other platforms every `.so` import
fails with `native modules are only supported on POSIX hosts`.
