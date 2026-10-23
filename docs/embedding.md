# Embedding and exporting to a C ABI

Lynxer can build a shared library that exposes a program's functions as **real
C symbols**, so C, C++, Python `ctypes`, and other native consumers can call
Lynxer code. This is the reverse of the
[native-module ABI](native-module-abi.md): there, Lynxer calls C; here C calls
Lynxer.

## Declaring exports

An exported function is declared at the top level with a quoted C signature:

```lynx
global setup(){ /* optional; runs at library init */ }

export "cdecl:int64(int64,int64)" add(int a, int b) -> int { return a + b; }
export "cdecl:double(double)"     scale(float x)  -> float { return x * 2.0; }
export "cdecl:cstring(cstring)"   greet(str who)   -> str   { return "hi " + who; }
export "cdecl:bytes(bytes)"       echo(bytes b)    -> bytes { return b; }
export "cdecl:void(int64)"        sink(int n)      -> none  { println(n); }
```

- `export "<signature>" name(params) -> type { ... }` at the top level; `main()`
  is neither required nor run (a library is not a program).
- `setup()` is optional. If present it must stay the first declaration and runs
  once, before the first exported call; it may call exports by name.
- The exported C symbol is exactly the Lynxer function name. Avoid names that
  collide with libc or with another loaded library (`read`, `open`, `main`, …).
- The signature string uses the same `cdecl:` grammar as native modules, with
  the same aliases: `int8/16/32/64`, `uint8/16/32/64`, `uintptr` are all
  `int64`; `double` is `float64`.

## The C ABI

| Signature token | C type |
| --- | --- |
| `int64` | `int64_t` |
| `float64` | `double` |
| `cstring` | `const char*` |
| `bytes` argument | `const uint8_t* data, int64_t length` |
| `bytes` return | `const uint8_t*` to `[int64 little-endian length][payload]` |
| `void` return | `void` |

`cstring` and `bytes` results are valid only until the next exported call on the
same thread; copy them if you need to keep them. On failure the wrapper returns
`0` / `0.0` / `""` / `nullptr`; the reason is available from
`lynxer_embed_last_error()`.

The Lynxer parameter and return types must be compatible with the signature:
`int64`↔`int`-family, `float64`↔`float`/`num`, `cstring`↔`str`,
`bytes`↔`bytes`, `void`↔omitted/`none`. Rejected at build time: defaults,
codeblock parameters, `value`, packed (`...`) signatures, `v2:` prefixes, and
arity or type mismatches.

## Building the library

On Linux, the output is an ELF `.so`. On Windows, use the MSYS2 MinGW-w64
toolchain used by Lynxer's Windows builds; the output is a `.dll` plus a MinGW
`.dll.a` import library. macOS shared-runtime builds are not available yet.

```bash
# Linux
lynxer --emit-library app.lynx -o libapp.so

# Windows (MSYS2 MinGW)
lynxer --emit-library app.lynx -o app.dll
```

| Flag | Effect |
| --- | --- |
| `--emit-library <a.lynx>` | build a platform shared library exporting the program's `export`s |
| `--include <file>` | embed an extra module, native library or data file |
| `-o <path>` | name the output library (default: the input stem + `.so` or `.dll`) |
| `--runtime <path>` | use an explicit embedding runtime (`liblynxer.so` or `lynxer.dll`) |
| `--cc <compiler>` | C++ compiler used to build the library |

`--emit-library` needs a C++ compiler and the matching embedding runtime. On
Linux it links `liblynxer.so` and records an `rpath` to the runtime and
`$ORIGIN`. On Windows it links the MinGW import library `lynxer.dll.a`; the
Windows DLL loader must be able to find `lynxer.dll` when the emitted library
is loaded, so put the runtime beside the host executable or add its directory
to `PATH`.

Alongside the library it writes a header with the exported prototypes:
`libapp.so` produces `libapp.h`, a self-contained header declaring each
exported function in `extern "C"` (it includes only `<stdint.h>`; include
`lynxer.h` too if you need the embedding wire types). Consumers include that
one header instead of declaring the prototypes by hand:

```bash
lynxer --emit-library app.lynx -o libapp.so
# libapp.so and libapp.h
c++ -I. -I<dir-with-lynxer.h> client.cpp libapp.so -o client
```

On Windows, `app.dll`, `app.dll.a` and `app.h` are emitted. Link the import
library with MinGW (`g++ -I. client.cpp app.dll.a -o client.exe`) and make
`lynxer.dll` available through the Windows DLL search path.

The library initializes the embedded program lazily, on the first exported
call. A program can be embedded once per process; a second initialization is
refused because the interpreter keeps process-global state.

## Staging the SDK

`make sdk` (also run by `make buildLynxer`) stages the pieces a consumer needs
under `lynxer/build/sdk/`:

```
lynxer/build/sdk/include/lynxer.h     the public C ABI
lynxer/build/sdk/include/ffi_abi.h    the wire types
lynxer/build/sdk/lib/liblynxer.so     Linux runtime
lynxer/build/sdk/lib/lynxer.dll.a     Windows MinGW import library
lynxer/build/sdk/bin/lynxer.dll       Windows runtime
```

## Lynxer must be built

`export` is a build feature: it only takes effect through a library produced by
`--emit-library`, not when the file is run directly. Running `lynxer app.lynx`
executes `main()` and never exposes the exports.

Building the library requires the **compiled** interpreter and its runtime:

```bash
make buildLynxer      # builds the interpreter and this host's embedding runtime
```

If the embedding runtime or its Windows import library is missing,
`--emit-library` reports that `make buildLynxer` is required rather than
passing a missing library to the compiler.

The emitted library is **self-contained**: it embeds the program archive (the
source and every module it imports), so at run time it needs neither the
original `.lynx` file nor the `lynxer` executable — only the host's Lynxer
runtime, which carries the compiled interpreter. The fixture builds from a temporary copy
of the source, deletes it, and then runs the C++ and Python consumers from
`/tmp` to prove this.

## Calling from C

```c
#include "libapp.h"   /* generated next to libapp.so */
/* bytes return: read the 8-byte little-endian length prefix first */
```

## Calling from Python

```python
import ctypes
lib = ctypes.CDLL("./libapp.so")
lib.add.restype = ctypes.c_int64
lib.add.argtypes = [ctypes.c_int64, ctypes.c_int64]
print(lib.add(2, 3))  # 5
```

See `lynxer/examples/export_basic.lynx`, `export_test.cpp` and
`export_test.py` for a complete, tested example.

## Notes and limits

- Linux and Windows MinGW builds are supported; macOS runtime builds remain
  unavailable.
- One embedded program per process (the shared runtime is the interpreter core).
- A library does not run `main()`; put initialization in `setup()`.
- `value`/v2 aggregate exports, packed exports, `as <cname>` renaming, and
  self-contained (statically linked) libraries are not supported yet.
