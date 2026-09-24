# Extending Lynxer

Lynxer grows in three ways:

| Extension | Written in | Use it when |
| --- | --- | --- |
| **Pure Clynxer module** | Clynxer, in `lynxer/stdlib/<name>.lynx` | The behaviour is expressible in Clynxer itself. `colorlib`, `text` and `typing` are examples; they ship no shared library. |
| **Native module** | C++ in `stdlib/<name>.cpp`, or Rust in `rust/<name>/` | The behaviour needs a system API, a file format, a device, or a third-party crate. |
| **Built-in** | C++ in `lynxer/builtins.cpp` | The operation is a language primitive that must be available without `import`. |

Almost everything belongs in the middle row. A built-in is not available to a
module author — it changes the language — and a pure Clynxer module is just a
module with no backend, so both are covered by the same wrapper rules.

Read [stdlib-contracts.md](stdlib-contracts.md) before you start: it states the
conventions your module has to follow, and
[native-module-abi.md](native-module-abi.md) is the ABI reference.

---

## 1. Choose C++ or Rust

| | C++ (`stdlib/<name>.cpp`) | Rust (`rust/<name>/`) |
| --- | --- | --- |
| Build | Built by the `stdlib/*.cpp` wildcard, no new Makefile entry | Add the crate to the workspace **and** its name to `LYNXER_RUST_MODULE_NAMES` |
| Dependencies | Standard library only | Any crate, but it must build offline after `Cargo.lock` is committed |
| Best for | POSIX calls, `<filesystem>`, `<chrono>`, small hand-written parsers | Anything with a real third-party crate: formats, protocols, GUI, audio, databases |
| Signature | Either a fixed shape or the packed `...` form | **Must** be the packed `...` form |

If a crate exists for the job, use Rust. Write C++ when the whole
implementation is a few calls into the standard library or POSIX.

---

## 2. Write the wrapper

The wrapper is what Clynxer programs see. It is always
`stdlib/<name>.lynx`, and it always has the same shape:

```lynx
////
Clynxer standard library: example.
One-line summary, then anything a user needs to know.

Extra paragraphs here are printed by `lynxer --list-stdlibs`, so keep them
useful and do not leave placeholder prose in a shipped module.
////

global setup(){ importAs("example.so", "nativeExample"); }

// Double the value. Returns the result, or -1 on failure.
global doubleIt(int value) -> int { return global.nativeExample.doubleIt(value); }

// Join two words. Returns the joined string.
global join(str left, str right) -> str { return global.nativeExample.join(left, right); }
```

Rules the wrapper must follow:

- **`setup()` is the only import.** Import the backend once, with
  `importAs("<name>.so", "native<Name>")`.
- **One `global` function per operation**, named as the user calls it.
- **Convert the backend's flat results here.** A backend returns `int64` `0`/`1`
  for booleans, so the wrapper writes `!= 0` and declares `-> bool`.
- **Never expose a backend type.** No crate names, no pointers, no structs.
- **Do not add logic.** If the wrapper starts computing, the calculation belongs
  in the backend where it can be tested and reused.
- Start the file with a `////` docstring. The first line is the module summary
  in `--list-stdlibs`; the rest is printed verbatim, so it is real documentation.

If the module needs no native code, write the functions directly and skip
`setup()` entirely — like `stdlib/text.lynx`.

---

## 3. Write the backend

### 3a. In Rust

Add `rust/<name>/Cargo.toml` with `crate-type = ["cdylib"]`, depend on
`lynxer_abi` by path, and write `rust/<name>/src/lib.rs`:

```rust
//! `example` stdlib backend: double a number, join two words.

use lynxer_abi::{export_int, export_string, clynxer_module};

export_int!(example_double_it, args, {
    let value = args.int(0);
    value * 2
});

export_string!(example_join, args, {
    let left = args.string(0);
    let right = args.string(1);
    format!("{left}{right}")
});

const OPS: &[(&str, &str, &str)] = &[
    ("doubleIt", "example_double_it", "cdecl:int64(...)"),
    ("join", "example_join", "cdecl:cstring(...)"),
];

clynxer_module!(OPS);
```

Three things to get right, all of which have caused real defects here:

1. **Every signature is packed** — `cdecl:int64(...)`, `cdecl:float64(...)` or
   `cdecl:cstring(...)`. The `export_*!` macros generate the four-scalar packed
   prototype, so registering a fixed shape (`cdecl:int64(int64)`) calls the
   symbol through the wrong C prototype and **segfaults on the first call**,
   with no build-time warning.
2. **Arguments are indexed per kind, not positionally.** `args.int(i)` reads the
   *i*-th number and `args.string(i)` the *i*-th string. For
   `save(handle, path, quality)` the reads are `args.int(0)`, `args.string(0)`,
   `args.int(1)`. Reading past the end of either list yields `0` / `""` **in
   silence**, so a wrong index produces a zero value rather than an error.
   `scripts/check_module_contracts.py` catches this; see §6.
3. **The macros guard panics for you.** `export_int!` and friends wrap the body
   in a panic guard that returns the module's sentinel, so a `panic!` or an
   `unwrap()` on `None` becomes a failure result instead of unwinding across the
   C boundary and killing the interpreter. Do not add your own `catch_unwind`.

Keep resources that must outlive a call in a registry you own — a
`Vec<Option<T>>` behind a `thread_local`, indexed by an integer handle. The
`image` and `sound` backends are the reference implementations.

### 3b. In C++

Write `stdlib/<name>.cpp` and export one entry point plus one
`extern "C"` function per operation. Read
[native-module-abi.md](native-module-abi.md) first; the worked example there is
complete. The short version:

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

extern "C" std::int64_t example_double_it(std::int64_t value) {
    return value * 2;
}

extern "C" const char* example_join(const char* left, const char* right) {
    return stable(std::string(left) + right);
}

extern "C" int clynxer_module_init_v1(RegisterFunction f, RegisterConstant,
                                    RegisterType) {
    return f("doubleIt", "example_double_it", "cdecl:int64(int64)") &&
                   f("join", "example_join", "cdecl:cstring(cstring,cstring)")
               ? 0
               : 1;
}
```

A returned `const char*` must stay valid until the interpreter copies it, which
happens immediately after the call — hence the `thread_local` buffer and the
"one live string result per call" rule. C++ modules may use either a fixed shape
or the packed form; fixed shapes are type-checked per argument at call time, so
a mistake fails with a located error instead of corrupting the stack.

---

## 4. Wire it into the build

**C++** needs nothing: `stdlib/*.cpp` is a wildcard, so the new file becomes
`stdlib/<name>.so` automatically. If the file needs extra link flags, that is
not yet supported — a module with system-library dependencies should be a Rust
crate instead.

**Rust** needs two edits:

1. add the crate to the workspace members in `rust/Cargo.toml`;
2. add its name to `LYNXER_RUST_MODULE_NAMES` in the root `Makefile`.

Rust modules are skipped with a warning when `cargo` is absent, so Lynxer still
builds without a Rust toolchain. Keep `rust/Cargo.lock` committed: the module
must build offline.

A crate is allowed to register **nothing**: `rust/ffi` is an intentional no-op
`cdylib` that keeps the workspace uniform. A C++ module that needs POSIX is
gated with `LYNXER_POSIX_BUILTINS`, as the managed
`filesystem*`/`process*`/`networking*` families are, and falls back to
`unsupportedTable()` otherwise.

---

## 5. Test it

Add `examples/stdlib_<name>.lynx` and a sibling
`examples/stdlib_<name>.expected`. `make test` runs every
`examples/stdlib_*.lynx` and diffs its output against the `.expected` file, so
the fixture is the specification for anything a user can observe.

Cover, as applicable:

| Case | What to assert |
| --- | --- |
| Success | The normal path, including the boundary of any range |
| Malformed input | A sentinel — never a crash or a panic |
| Invalid handles | `-1`, an out-of-range index, and a released handle |
| Missing files | `-1` or an error string, depending on the module's family |
| Cleanup | Releasing twice returns the failure sentinel; the registry count drops |
| Optional dependencies | The behaviour when the system binary/crate feature is absent |

Keep the fixture **hermetic**: no network, no wall-clock time, no host-specific
strings. Where a module needs a file, commit a small asset under
`examples/assets/` rather than generating one at test time. Where output would
be host-specific, assert a property instead (`returnLength(x) > 0`) rather than
the exact text.

If the module is Rust-backed and hermetic, add its fixture name to the
compiled/bundled parity loop in the root `Makefile` so it is also checked when
compiled into a standalone executable.

---

## 6. Let the checks catch what tests cannot

A fixture records what the code *does*, so it cannot detect a module that is
broken in exactly the way the fixture reproduces. Two bundled modules shipped
that way: `sqldb` passed a path through its wrapper while the backend read an
integer handle, and every call returned `ERROR: invalid handle` — which is what
the fixture asserted. `tui` read packed arguments positionally.

`scripts/check_module_contracts.py` runs first in `make test` and compares the
wrapper against the backend structurally:

- every `global.native<Alias>.<op>(...)` call names a registered op;
- a Rust backend's packed reads are in range for the arguments the wrapper
  passes;
- any registered op no wrapper calls is reported as a warning.

Run it on its own while iterating:

```bash
python3 lynxer/scripts/check_module_contracts.py --verbose
```

---

## 7. Document it

Every module is finished when all of these are true:

- [ ] `stdlib/<name>.lynx` with a real `////` docstring, visible in
      `--list-stdlibs`
- [ ] the backend under `stdlib/<name>.cpp` or `rust/<name>/`, registered in the
      Makefile
- [ ] `lynxer/docs/stdlib/<name>.md` — the operation table, argument by
      argument
- [ ] `examples/stdlib_<name>.lynx` + `.expected`, covering the failure paths
- [ ] the new name added to the module table in `lynxer/docs/README.md` and to
      `LYNXER_LIST_STDLIB_MODULES` in the root `Makefile`
- [ ] the identity model recorded in the table in
      [stdlib-contracts.md](stdlib-contracts.md)
- [ ] deliberate divergences from `clynxer/stdlib/<name>.lynx` written down in
      [limitations.md](limitations.md), with the reason
- [ ] `make test` green, including the contract check and the compiled/bundled
      parity run

Compare against `clynxer/stdlib/<name>.lynx` — the Python implementation is the
behaviour reference — and either match it or record why not. Byte-identical
output is achievable more often than it looks: `sqldb` and `sound` both produce
output identical to the reference, including their error strings.
