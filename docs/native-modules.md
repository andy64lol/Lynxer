# Native modules

Native modules are shared libraries that export a stable registration entry
point. On Linux the normal extension is `.so`; `.dylib` and `.dll` are also
accepted on their respective platforms.

## Registration ABI

Each module must export this C symbol:

```c
int clynxer_module_init_v1(
    int (*register_function)(const char *name, const char *symbol,
                             const char *signature),
    int (*register_constant)(const char *name, int64_t value),
    int (*register_type)(const char *name, const char *layout)
);
```

Return zero after all registrations succeed. Returning a non-zero value, using
an invalid identifier, registering duplicates, or naming a missing symbol
rejects the module. Names must be valid Clynxer identifiers. Function signatures
use the existing `cdecl:` native-call grammar.

Example:

```cpp
#include <cstdint>

using RegisterFunction = int (*)(const char *, const char *, const char *);
using RegisterConstant = int (*)(const char *, std::int64_t);
using RegisterType = int (*)(const char *, const char *);

extern "C" std::int64_t add(std::int64_t left, std::int64_t right) {
    return left + right;
}

extern "C" int clynxer_module_init_v1(RegisterFunction function,
                                      RegisterConstant constant,
                                      RegisterType type) {
    if (!function("add", "add", "cdecl:int64(int64,int64)")) return 1;
    if (!constant("version", 1)) return 2;
    if (!type("pair", "int64 left, int64 right")) return 3;
    return 0;
}
```

## Importing

Import a shared library from `setup()` just like a Clynxer module:

```lynx
global setup() {
    import("mylib.so");
}

global main() {
    println(global.mylib.version);
    println(global.mylib.add(2, 3));
}
```

Registered functions are callable directly, constants become integers, and
registered type layouts become strings suitable for the native memory APIs.
Native modules are initialized once per import and remain loaded for the
lifetime of their namespace. This prevents function pointers from becoming
invalid while a module is still in use.

The loader, registration callbacks, symbol lookup, and library lifetime are
implemented by Clynxer's C++ extension. The Python runtime does not use
`ctypes`, which means the same low-level path is used by source, bytecode, and
PyInstaller-bundled programs.

Native dependencies compiled into bytecode are recorded in its dependency
manifest. `--bundle` copies each declared library into the executable's
extraction directory. Missing libraries, missing `clynxer_module_init_v1`
symbols, missing registered symbols, duplicate registrations, and non-zero
initializer returns are reported as explicit **native module lifecycle
failure** errors with the relevant dependency or symbol.

## Bundled C++ stdlibs

The root `Makefile` treats every `lynxer/stdlib/*.cpp` file as a
native standard-library backend and builds its sibling `.so` inside the same
`lynxer/stdlib/` directory during `make`.
The corresponding `.lynx` file is the public wrapper when a Clynxer-compatible
API or return-type conversion is useful. This keeps the ABI boundary uniform:
adding a new dependency-free stdlib means adding its C++ implementation and
wrapper, without adding Python packages.

## Explicit handle API

For code that needs dynamic discovery, use:

| Function | Description |
| --- | --- |
| `nativeModuleLoad(path)` | Load and initialize a module; returns a handle |
| `nativeModuleName(handle)` | Return the filename-derived module name |
| `nativeModuleFunction(handle, name)` | Return a `functionAddress` |
| `nativeModuleConstant(handle, name)` | Return a registered integer |
| `nativeModuleType(handle, name)` | Return a registered native layout string |
| `nativeModuleError(handle)` | Return the module-local lifecycle error, or `""` |
| `nativeModuleDependencies(handle)` | Return discovered shared-library dependencies |
| `nativeModuleClose(handle)` | Release an explicitly loaded module |

An explicit module handle can be combined with `ffiCall` and the registered
function address. Closing a handle invalidates it and releases its registration
callbacks. Retained function addresses from that module fail cleanly after
close instead of calling unmapped code. Imported modules cannot be explicitly
closed; their namespace owns their lifetime. Invalid handles and failed
registrations produce normal Clynxer runtime errors. The low-level FFI entry
points are `ffiLoadLibrary`,
`ffiLookup`, `ffiCall`, `ffiCallback`, `ffiFreeCallback`, and
`ffiCloseLibrary`; unsupported callback signatures fail clearly. Dependency inspection is informational and
returns an empty list when host linker tooling cannot inspect a loaded module.
Native calls use the host ABI through `ctypes`/libffi rather than casting raw
addresses to unrelated C++ function-pointer types. Each callback has its own
managed trampoline, and callback `cstring` returns are rejected because their
storage cannot safely outlive the callback invocation.