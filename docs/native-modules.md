# Native modules

A native module is a shared library that exports one stable registration entry
point. On Linux the extension is `.so`; the loader is C++ (`dlopen`/`dlsym`) —
there is no Python and **no `libffi`**: calls dispatch through a hand-written
fixed-shape table (see [native-module-abi.md](native-module-abi.md)).

## Registration ABI

```c
int lynxer_module_init_v1(
    int (*register_function)(const char *name, const char *symbol,
                             const char *signature),
    int (*register_constant)(const char *name, int64_t value),
    int (*register_type)(const char *name, const char *layout)
);
```

Return `0` after all registrations succeed. A non-zero return, an invalid
identifier, a duplicate registration, or a missing symbol rejects the module.
Function signatures use the `cdecl:<ret>(<args>)` grammar.

```cpp
#include <cstdint>

using RegisterFunction = int (*)(const char *, const char *, const char *);
using RegisterConstant = int (*)(const char *, std::int64_t);
using RegisterType = int (*)(const char *, const char *);

extern "C" std::int64_t add(std::int64_t left, std::int64_t right) {
    return left + right;
}

extern "C" int lynxer_module_init_v1(RegisterFunction function,
                                     RegisterConstant constant,
                                     RegisterType type) {
    if (!function("add", "add", "cdecl:int64(int64,int64)")) return 1;
    if (!constant("version", 1)) return 2;
    if (!type("pair", "int64 left, int64 right")) return 3;
    return 0;
}
```

## Importing

Import a shared library from `setup()` like any other module:

```lynx
global setup() {
    import("mylib.so");
}

global main() {
    println(global.mylib.version);   // a registered constant
    println(global.mylib.add(2, 3)); // a registered function
}
```

Registered functions are callable, constants become integers, and registered
type layouts become strings usable with the native-memory APIs. A module is
initialised once per import and stays loaded for the lifetime of its namespace,
so function pointers cannot become invalid while it is still in use.

## Explicit handle API

| Function | Notes |
| --- | --- |
| `nativeModuleLoad(path)` | Loads and initialises a module; returns a handle. |
| `nativeModuleName(handle)` | The filename-derived module name. |
| `nativeModuleFunction(handle, name)` | Returns a `functionAddress`. |
| `nativeModuleConstant(handle, name)` | Returns a registered integer. |
| `nativeModuleType(handle, name)` | Returns a registered layout string. |
| `nativeModuleError(handle)` | The module-local lifecycle error, or `""`. |
| `nativeModuleDependencies(handle)` | Discovered shared-library dependencies. |
| `nativeModuleClose(handle)` | Releases an explicitly loaded module. |

An explicit handle can be combined with `ffiCall` and a returned
`functionAddress`. Closing a handle invalidates it and releases its registration
callbacks; retained function addresses then fail cleanly instead of calling
unmapped code. Imported modules cannot be explicitly closed — their namespace
owns their lifetime. Invalid handles and failed registrations raise normal
runtime errors. `nativeModuleDependencies` is informational and returns an empty
list when the host linker tooling cannot inspect a module.

## FFI entry points

`ffiLoadLibrary`, `ffiLookup`, `ffiCall`, `ffiCallback`, `ffiFreeCallback` and
`ffiCloseLibrary`. `ffiCall` describes and performs a native call through the
same fixed-shape table as `nativeCall` (at most four typed parameters, or the
packed `ret(...)` form).

`ffiCallback(signature, function)` does **not** produce a real native callable:
it stores the pair under a handle, and `ffiCall` recognises that handle and
re-invokes the Lynxer function. There is no trampoline, and the stored signature
is not validated. The `ffi*` calling convention is still an
[open decision](../todo.md) for arbitrary signatures.

## Building C++ stdlib backends

Every `lynxer/stdlib/*.cpp` file is built into its sibling `.so` by the
`Makefile`; the matching `.lynx` file is the public wrapper where a
Lynxer-friendly API or return-type conversion is useful. Adding a
dependency-free stdlib module means adding a C++ backend and a wrapper — no
Python packages.

## See also

- [native-module-abi.md](native-module-abi.md) — the frozen ABI, signature
  shapes and data conventions.
- [extending.md](extending.md) — adding a stdlib module end to end.
