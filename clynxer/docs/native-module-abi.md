# Native module ABI

Clynxer's standard libraries are shared libraries (`.so` on Linux, `.dylib` on
macOS) loaded at runtime with `dlopen`. This document is the contract a native
module must implement. It applies to the bundled `stdlib/*.so` modules exactly
as it applies to third-party modules.

## Entry point

Every module exports one C symbol:

```c
int lynxer_module_init_v1(
    int (*register_function)(const char *name, const char *symbol,
                             const char *signature),
    int (*register_constant)(const char *name, int64_t value),
    int (*register_type)(const char *name, const char *layout)
);
```

Return `0` after registering everything. A non-zero return, an invalid
identifier, a duplicate name, or a `symbol` that `dlsym` cannot resolve rejects
the module with a *native module lifecycle failure* error naming the symbol.

- `register_function(name, symbol, signature)` — `name` is the Lynxer-facing
  name under the module namespace; `symbol` is the exported C name; `signature`
  uses the grammar below.
- `register_constant(name, value)` — exposed as a read-only integer field on the
  module namespace.
- `register_type(name, layout)` — exposed as a string field holding a native
  memory layout (used with the `memory*` builtins).

Loading happens in `clynxer/ast.cpp` (`ImportStatement::execute`), where
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

Type tokens are `int64` (Lynxer `int`), `double` (Lynxer `float`) and `cstring`
(Lynxer `str`). `double` and `float64` are interchangeable — the dispatcher
normalizes `double` to `float64`. Argument lists may be empty.

`cdecl:` is optional; the bare `<return>(<args>)` form is accepted too.

## Supported signature shapes

The dispatcher in `clynxer/ast.cpp` builds the normalized shape string and looks
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
matching C++ prototype; keep `clynxer/docs/stdlib/` in sync when you do.

Numeric arguments are read leniently where the shape is numeric
(`asNumber` accepts an `int` or a `float`); integer and string arguments are
strict and raise a located `SourceError` on a type mismatch.

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

extern "C" int lynxer_module_init_v1(RegisterFunction function,
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

## Platform support

Native modules require a POSIX host. On other platforms every `.so` import
fails with `native modules are only supported on POSIX hosts`.
