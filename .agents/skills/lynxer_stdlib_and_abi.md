---
name: lynxer-stdlib-and-abi
description: Add or change Lynxer standard-library modules across pure Lynxer wrappers, C++ and Rust backends, the native-module ABI, contract checker, Makefile wiring, docs and fixtures.
---

# Lynxer standard library and native ABI

Use this skill for any change under `lynxer/stdlib/`, `lynxer/rust/`, the
native-module ABI, or a module's documentation and fixture. The canonical
references are [`docs/extending.md`](../../docs/extending.md),
[`docs/stdlib-contracts.md`](../../docs/stdlib-contracts.md), and
[`docs/native-module-abi.md`](../../docs/native-module-abi.md).

## Choose the extension type

| Extension | Written in | Use when |
| --- | --- | --- |
| Pure Lynxer module | `lynxer/stdlib/<name>.lynx` | The behavior is expressible in Lynxer; no `.so`. |
| Native C++ module | `lynxer/stdlib/<name>.cpp` | POSIX, `<filesystem>`, `<chrono>`, or a small hand-written parser. |
| Native Rust module | `lynxer/rust/<name>/src/lib.rs` | A real third-party crate is needed. |
| Builtin | `lynxer/builtins.cpp` | The operation is a language primitive available without `import`. |

Most additions belong in the native rows. A builtin changes the language and is
not available to module authors.

## Module pair contract

A native module is always two files with the same stem:

- `lynxer/stdlib/<name>.lynx` — the public wrapper.
- `lynxer/stdlib/<name>.so` — the backend, built from C++ or Rust.

The wrapper is the contract; the backend is an implementation detail. Never
expose a crate, library, pointer, or native struct to Lynxer.

## Wrapper rules

- Start the file with a `////` docstring. The first line is the `--list-stdlibs`
  summary; the rest is printed verbatim, so write real documentation.
- `setup()` is the only place that imports, and it imports the backend once:
  `importAs("<name>.so", "native<Name>")`.
- Define one `global` function per operation, using lowerCamelCase names.
- Convert flat backend results in the wrapper: `int64` `0`/`1` becomes `bool`
  with `!= 0`; a newline-separated backend list becomes a Lynxer `list`.
- Do not add computation to the wrapper; put it in the backend.
- Pure Lynxer modules skip `setup()` entirely.

## Rust backend rules

- Declare the `cdylib` example target in `lynxer/rust/Cargo.toml`:
  ```toml
  [[example]]
  name = "lynxer_<name>"
  path = "<name>/src/lib.rs"
  crate-type = ["cdylib"]
  test = true
  ```
- `export_int!`, `export_string!`, and friends require the **packed** signature
  `cdecl:int64(...)`, `cdecl:float64(...)`, or `cdecl:cstring(...)`. Registering
  a packed macro under a fixed signature can segfault on the first call.
- Packed arguments are indexed per kind, not by position: `args.int(i)` is the
  *i*-th number, `args.string(i)` the *i*-th string. Out-of-range reads silently
  return `0`/`""`, so a wrong index is a correctness bug, not an error.
- The macros already guard panics. Do not add `catch_unwind`.
- Handles are integer indices into a backend-owned registry, never pointers.
  Keep the registry in a `thread_local` or equivalent and never reuse a live
  index.
- Returned strings must stay valid until the interpreter copies them; keep at
  most one live result buffer per call.
- Keep `Cargo.lock` committed so the build works offline.

## C++ backend rules

- Export one `lynxer_module_init_v1` entry point and one `extern "C"` function
  per operation.
- Fixed signatures are type-checked per argument by the interpreter; packed
  signatures follow the same per-kind rule as Rust.
- Returned `const char*` values use a `thread_local` stable buffer.
- C++ modules are built automatically by the `lynxer/stdlib/*.cpp` wildcard. If a
  module needs extra link flags, prefer a Rust backend instead.
- Gate POSIX-only code with the existing platform macros; Windows builds skip
  modules listed in `LYNXER_WINDOWS_SKIP_MODULES` and filter `winreg`/`winpipe`
  on non-Windows hosts.

## Build wiring checklist

For any new module, add its name to `LYNXER_LIST_STDLIB_MODULES` in the root
`Makefile` so the `--list-stdlibs` gate covers it, and to `STDLIB_MODULES` in
`site/build.py` so its site page and sidebar entry are generated.

For a Rust module, also edit the root `Makefile`:

1. Add the name to `LYNXER_RUST_MODULE_NAMES`.
2. Add `stdlib_<name>` to `LYNXER_PARITY_FIXTURES` if the fixture is hermetic.

For a C++ module, no Makefile source entry is needed; the `stdlib/*.cpp`
wildcard builds it.

## Contract checker

Run this before the full suite:

```bash
python3 lynxer/scripts/check_module_contracts.py --verbose
```

It catches:

- wrapper calls to operations the backend never registers;
- Rust packed argument indices beyond the arguments the wrapper passes;
- backend operations no wrapper calls.

It cannot validate runtime behavior; fixtures and the ABI tests do that.

## Tests and fixtures

- Add `lynxer/examples/stdlib_<name>.lynx` and a sibling `.expected`.
- Cover success, malformed input, invalid or released handles, missing files, and
  cleanup where applicable.
- Keep fixtures hermetic: no network, no wall-clock dependence, no host-specific
  strings. Commit small assets under `lynxer/examples/assets/`.
- Add the fixture to `LYNXER_PARITY_FIXTURES` when it must also pass compiled.
- For C++ or ABI changes, also run the relevant builtin fixture and
  `make testLynxerEmit` if the export path is touched.
- Run the module fixture directly, then `make testLynxer`.

## Documentation checklist

- `docs/stdlib/<name>.md` with an operation table and an example.
- A row in the module table in `docs/README.md` and the module count.
- A row in `docs/stdlib-contracts.md` with the error-sentinel family.
- Any deliberate divergence in `docs/limitations.md`.
- The `////` docstring in the wrapper, which `--list-stdlibs` prints.
- `site/build.py` `STDLIB_MODULES` entry for Rust modules.

## Error and resource conventions

- Failures are in-band sentinels, not exceptions across the ABI. Pick one family
  and apply it consistently: scalar sentinel, status string, error string, or a
  catchable runtime error in the wrapper.
- Never panic or abort across the C boundary.
- Long-running operations should poll the host interrupt flag and return their
  sentinel after SIGINT.
- Programs release anything they open, load, or create with the matching close
  or release operation.

## Definition of done

- Wrapper, backend, Makefile wiring, contract check, fixture, expected output,
  docs, and site list are all updated.
- `python3 lynxer/scripts/check_module_contracts.py --verbose` passes.
- `make testLynxer` passes, including parity when applicable.
- `--list-stdlibs` shows the module and its docstring.
- The final report states which platform-specific checks were skipped.
