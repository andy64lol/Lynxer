# Clynxer Investigation Report

**Date and Time:** 2026-09-20 10:53 CEST (2026-09-20 08:53 UTC)
**Author:** investigation session (automated)
**Scope:** Clynxer source tree under `clynxer/`, the `sound`/`sqldb`/`tui` stdlib
modules, the native-module ABI, the build and test system, and the state of
`todo.md` and the Clynxer documentation.
**Result:** `make buildCLynxer` and `make testCLynxer` both pass. Four classes of
defect were found and fixed in the three new stdlib modules; ten documentation
inconsistencies were catalogued.

> **This revision replaces the earlier version of this report.** The earlier
> version was dated `2026-09-20 14:30 UTC` (a future timestamp, and therefore
> not a real measurement), and several of its claims were wrong — most notably
> that Milestone 9 was "not yet implemented" and that "no standalone compiler
> for Clynxer" exists. Both are contradicted by `todo.md` and by the shipped
> `--compile`/`--bundle` backend. See §9 for the corrections.

---

## Table of Contents

1. [What is Clynxer, and how does it differ from Lynxer?](#1-what-is-clynxer-and-how-does-it-differ-from-lynxer)
2. [Repository map — the important directories](#2-repository-map--the-important-directories)
3. [Architecture](#3-architecture)
4. [Build system](#4-build-system)
5. [Test system](#5-test-system)
6. [Standard library inventory](#6-standard-library-inventory)
7. [Milestone status](#7-milestone-status)
8. [Findings from the 2026-09-20 session](#8-findings-from-the-2026-09-20-session)
9. [Documentation inconsistencies](#9-documentation-inconsistencies)
10. [Open items and known bugs](#10-open-items-and-known-bugs)
11. [Recommendations](#11-recommendations)
12. [Appendix A — file and line index](#appendix-a--file-and-line-index)

---

## 1. What is Clynxer, and how does it differ from Lynxer?

### 1.1 Lynxer — the original (Python)

Lynxer is **a statically-flavoured, C-style scripting language** that runs on
Python. Source files use the `.lynx` extension (`README.md:6-7`).

- Implemented in Python under `lynxer/`. The interpreter pipeline
  (lexer + parser + interpreter) lives in `lynxer/lynxer.py`, builtins in
  `lynxer/builtins.py`, and the CLI in `lynxer/shell.py`.
- Ships a **C++ Python extension** for the native-memory built-ins
  (`lynxer/cpp.cpp`) and a **bytecode stack machine** (`lynxer/bytecode_vm.cpp`)
  compiled by `lynxer/setup.py`.
- **Has bytecode.** `lynxer --compile foo.lynx` writes `foo.lynxc`, which can be
  executed directly; see `docs/bytecode.md`. This is the Python `.lynxc`
  container and is **not** related to anything in Clynxer.
- Ships `rawPy(){ ... }` / `rawPyx(){ ... }` blocks that embed **inline Python
  and Cython** directly in a `.lynx` program (`docs/rawpy.md`). Most Python
  stdlib wrappers use `rawPy` internally.
- Linux only, x86-64 (`amd64`) and ARM64 (`aarch64`) (`README.md:9-14`).
- Distributed as a PyInstaller one-file binary (`make build`, `make buildLite`).

### 1.2 Clynxer — the rebuild (C++)

Clynxer is **the standalone C++ implementation being rebuilt beside the original
Python Lynxer** (`todo.md:3-5`, `clynxer/README.md:1-9`).

- C++17 only. **No Python runtime and no third-party C++ dependencies** for the
  interpreter itself.
- The same `.lynx` language and the same `global setup(){}` / `global main(){}`
  entry points.
- **There is no bytecode backend** (`clynxer/docs/limitations.md:24-28`).
  `--compile` writes a **standalone ELF executable** (the former `--bundle`)
  that embeds the program source, every transitively imported `.lynx` module
  source, and the bytes of every imported native `.so`. Native libraries are
  materialised into a temporary directory at startup so `dlopen` can load them.
  Passing a `.lynxc` file reports that bytecode is no longer supported — and
  `clynxer/Makefile:223-227` asserts exactly that.
- **No `rawPy`.** Because there is no Python runtime, every Python-backed
  stdlib module has been replaced by a native `.so` backend — initially C++,
  increasingly Rust.
- Reports version `CLynxer 0.1.8` (`clynxer --version`) and reports itself as
  `"CLynxer"` from `os.getPythonImplementation()` / `sys.implementation()`.

### 1.3 Side-by-side

| Dimension | Lynxer (`lynxer/`) | Clynxer (`clynxer/`) |
| --- | --- | --- |
| Language | Python 3 | C++17 |
| Role | the original implementation | the rebuild |
| Status | **frozen — the behaviour reference** | under active development |
| Runtime deps | Python, pip packages, native ext | libc only (+ Rust for some stdlib) |
| Executable | PyInstaller `dist/lynxer` | `clynxer/clynxer` (ELF) |
| `--compile` output | `.lynxc` bytecode | standalone ELF executable |
| Bytecode / VM | yes (`bytecode.py`, `bytecode_vm.cpp`) | **removed** (superseded) |
| Inline Python | `rawPy(){}` / `rawPyx(){}` | not available |
| Stdlib backend | `.lynx` + `rawPy`, Python packages | `.lynx` wrapper + `.so` native backend |
| Stdlib modules | 33 | 27 (§6) |
| Version | — | `CLynxer 0.1.8` |

### 1.4 Which one is authoritative?

**The Python implementation is the behaviour reference, but it is not a runtime
dependency, and its internals are not copied into Clynxer** (`todo.md:3-5`).
Where Clynxer deliberately diverges, the divergence is supposed to be written
down in `clynxer/docs/limitations.md`. That file is therefore the contract for
"what Clynxer does differently", and §9 shows it has drifted out of date.

Consequence for the current work: when porting a Python stdlib module to a Rust
backend, `lynxer/stdlib/<name>.lynx` (the Python reference) defines the intended
semantics — but the C++ `clynxer/stdlib/<name>.lynx` wrapper defines the
**names and signatures that actually exist**. The two disagreed for `tui` (§8.5).

---

## 2. Repository map — the important directories

### 2.1 Top level

| Path | What it is |
| --- | --- |
| `lynxer/` | **Python implementation.** The frozen behaviour reference. Also holds the C++ extension sources (`cpp.cpp`, `bytecode_vm.cpp`) and its own `stdlib/`. |
| `clynxer/` | **C++ implementation.** The whole active project. |
| `docs/` | Documentation **for the Python Lynxer** (`language.md`, `bytecode.md`, `native-modules.md`, `stdlib/`, …). |
| `clynxer/docs/` | Documentation **for Clynxer** (`language.md`, `native-module-abi.md`, `limitations.md`, `stdlib/`, …). |
| `test/` | The 55 Python test fixtures `test*.lynx`, the runners `validate.py` / `remaining.py`, and `test/golden/` (the Stage-1 golden corpus + `manifest.json`). |
| `scripts/` | `golden_corpus.py`, `benchmark_pipeline.py`, `select_pure_stdlib.py`, `testARM64Syscall.py`, `post-merge.sh`. |
| `assets/` | Logo and images. |
| `syntax.lynx` | Full syntax showcase; also linted by `make check`. |
| `todo.md` | **The Clynxer roadmap and source of truth for milestone status.** |
| `Makefile` | Root build/test orchestration for **both** implementations. |
| `.github/workflows/` | `buildCLynxer.yml` (builds + runs `make testCLynxer` on push/PR, ubuntu-latest) and `buildArmLinux.yml`. |
| `.agents/memory/` | Where this report lives. |
| `venv/` | Python virtualenv for the Python Lynxer toolchain. |
| `.clangd`, `.ruff_cache/` | Editor/linter caches. |

### 2.2 `clynxer/` internals

The interpreter is **12 translation units**, ~10.5k lines of `.cpp`/`.hpp`:

| File | Responsibility |
| --- | --- |
| `main.cpp` | Entry point |
| `shell.cpp` | CLI surface: argument parsing, subcommand dispatch, `--help` |
| `lexer.cpp` | Tokeniser |
| `parser.cpp` | Parser |
| `ast.cpp` | AST nodes **and the native-module loader/dispatcher** (§3.3) |
| `runtime.cpp` | `Environment`, scopes, value storage, variable declaration |
| `types.cpp` | Value model, type names, number formatting |
| `builtins.cpp` | Language built-ins |
| `ops.cpp` | Operator evaluation |
| `config.cpp` / `clynxer.config` | Version strings and user-facing message templates |
| `bundle.cpp` | `--compile` / `--bundle`: builds the self-contained ELF payload |
| `interrupt.cpp` | SIGINT handling |

Supporting directories:

| Path | What it is |
| --- | --- |
| `clynxer/stdlib/*.lynx` | **27** Lynxer-facing stdlib wrappers. Each `setup()` does `importAs("<name>.so", "native<Name>")` and each public function forwards to `global.native<Name>.<fn>()`. |
| `clynxer/stdlib/*.cpp` | **15** C++ native backends, compiled to the sibling `.so` by a wildcard rule. |
| `clynxer/stdlib/*.so` | Built native modules (24 total = 15 C++ + 9 Rust). |
| `clynxer/rust/` | **Cargo workspace with 10 crates** (~6.5k lines of Rust): `abi`, `game`, `image`, `json`, `lua`, `network`, `server`, `sound`, `sqldb`, `tui`. |
| `clynxer/examples/` | 55 `.lynx` fixtures, 25 of them `stdlib_*.lynx` with a sibling `.expected`. |
| `clynxer/docs/` | Clynxer docs; `native-module-abi.md` is the ABI contract. |
| `clynxer/build/rust/` | Cargo `--target-dir` output (intermediate). |
| `clynxer/Makefile` | Builds the binary, the C++ modules, the Rust modules, and runs the suite. |

### 2.3 `lynxer/` (Python) internals — for reference only

`lynxer.py` (lexer + parser + interpreter + bytecode compiler), `builtins.py`,
`values.py`, `type_registry.py`, `builtin_registry.py`, `runtime.py`,
`runtime_state.py`, `lexer.py`, `parser.py`, `lynxerAst.py`, `bytecode.py`,
`execution.py`, `formatting.py`, `shell.py`, `bundle.py`, `install.py`,
`validate.py`, `syscalls.py`, `error.py`, `_mp_workers.py`, `cpp.cpp`,
`bytecode_vm.cpp`, `setup.py`, and `stdlib/` (33 `.lynx` modules).

### 2.4 "Where do I look for X?"

| I want to… | Look at |
| --- | --- |
| add a new stdlib module | `clynxer/docs/README.md:91-110` (4-step recipe), then `rust/<name>/` |
| understand the native ABI | `clynxer/docs/native-module-abi.md` |
| know why Clynxer differs from Python | `clynxer/docs/limitations.md` |
| know what's done / not done | `todo.md` |
| see the CLI surface | `clynxer --help`, `clynxer/shell.cpp` |
| add a test | `clynxer/examples/stdlib_<name>.lynx` + `.expected` |
| see what CI enforces | `.github/workflows/buildCLynxer.yml`, `clynxer/Makefile:101-346` |

---

## 3. Architecture

### 3.1 Interpreter pipeline

```
.lynx source
   → lexer.cpp            tokens
   → parser.cpp           AST
   → ast.cpp              tree-walking execution
   → runtime.cpp/types.cpp/ops.cpp/builtins.cpp   values, scopes, operators
```

There is **no VM and no bytecode stage**: `ast.cpp` executes the tree directly.
`--compile` does not compile to an intermediate form either — `bundle.cpp` packs
the *source text* plus imported module sources and `.so` bytes into an ELF
payload, and the produced executable re-runs the same tree-walking interpreter at
startup. That is why compiled and interpreted runs are asserted to be identical
(`clynxer/Makefile:178-206`).

### 3.2 Standard library: wrapper + native backend

Every stdlib module is a **pair**:

1. `clynxer/stdlib/<name>.lynx` — the Lynxer-facing module. Its `setup()`
   imports the shared library under a namespace, and each public function
   forwards to it:

   ```lynx
   global setup(){ importAs("sound.so", "nativeSound"); }
   global playSound(int soundIdx) -> bool { return global.nativeSound.play(soundIdx) != 0; }
   ```

   This layer exists so the Lynxer-visible API never depends on native types:
   booleans come back as `int64` `0`/`1` and are converted with `!= 0`.

2. `clynxer/stdlib/<name>.so` — the native backend, built either from
   `clynxer/stdlib/<name>.cpp` **or** from a Rust crate under `clynxer/rust/<name>/`.

Three modules are **pure Lynxer** (no `.so`): `colorlib`, `text`, `typing`.

### 3.3 The native module ABI

Defined in `clynxer/docs/native-module-abi.md`; implemented in `clynxer/ast.cpp`.

**Entry point.** Every module exports exactly one C symbol:

```c
int lynxer_module_init_v1(
    int (*register_function)(const char *name, const char *symbol, const char *signature),
    int (*register_constant)(const char *name, int64_t value),
    int (*register_type)(const char *name, const char *layout));
```

`ast.cpp` `dlopen`s with `RTLD_NOW | RTLD_LOCAL` and invokes the initialiser with
those three callbacks. A module may optionally also export
`lynxer_module_attach_v1(const LynxerHostApi *)` to call back into Lynxer (used by
`game` for frame callbacks).

**Signature grammar.** `cdecl:<return>(<arg>,<arg>,...)`, with type tokens
`int64`, `double`/`float64`, `cstring`; `cdecl:` is optional.

**Two dispatch families.** This is the single most important thing to understand,
and it is the cause of the worst bug in §8.

| Family | Signature form | How the symbol is called |
| --- | --- | --- |
| **Fixed shape** | e.g. `cdecl:int64(int64)` | Through `nativeCallTable()` in `ast.cpp` using an exact C prototype (`int64_t(*)(int64_t)`). Only the **32 enumerated shapes** in `native-module-abi.md:82-115` are callable. |
| **Packed** | `cdecl:int64(...)`, `cdecl:float64(...)`, `cdecl:cstring(...)` | Through the packed shapes at `ast.cpp:481/494/505`; **all** arguments arrive as four scalars. |

The packed form is:

```c
int64_t     op(const double *nums, int64_t num_count,
               const char *const *strs, int64_t str_count);
double      op(const double *nums, int64_t num_count,
               const char *const *strs, int64_t str_count);
const char* op(const double *nums, int64_t num_count,
               const char *const *strs, int64_t str_count);
```

Numbers (Lynxer `int`, `float`, and `bool` as `0`/`1`) arrive in `nums` in
original order; strings in `strs`. Either pointer may be null when its count is
zero; at most 64 of each are accepted (`native-module-abi.md:124-150`).

The family is chosen at `ast.cpp:566`:

```cpp
const bool packed = types.size() == 1 && types[0] == "...";
```

Everything else is looked up in the fixed-shape table, and an unknown shape
raises `unsupported native signature '<sig>'`.

**The `clynxer_abi` crate** (`clynxer/rust/abi`) removes the boilerplate for Rust
modules: the packed-argument `Args` view, panic guards (`guard_int` /
`guard_float` / `guard_string`, so a Rust panic never unwinds across the C ABI),
the single `thread_local` string-result buffer, the `LynxerHostApi`, and the
`export_int!` / `export_float!` / `export_string!` / `lynxer_module!` macros.

### 3.4 Rust backends

Nine of the 24 native modules are Rust `cdylib`s, declared in
`clynxer/Makefile:15` (`RUST_MODULE_NAMES := game image json lua network server
sound sqldb tui`). Each exports `lynxer_module_init_v1` and its ops directly —
**there is no C++ shim**.

| Crate | Key dependencies | Replaces (Python) |
| --- | --- | --- |
| `game` | `macroquad` (also pulls `rodio`/`cpal`/`symphonia`) | Arcade |
| `image` | `image` 0.25 | Pillow |
| `json` | `serde_json` with `preserve_order` | `json` |
| `lua` | `mlua` with vendored Lua 5.4 | `lupa`/Lua |
| `network` | `ureq` + `rustls` + `tungstenite` | `requests` + `websocket-client` |
| `server` | `axum` + `tokio` | Flask-style servers |
| `sound` | `rodio` + `cpal` + `symphonia` | Arcade audio |
| `sqldb` | `rusqlite` (bundled SQLite) + `serde_json` + `base64` | `sqlite3` |
| `tui` | `ratatui` 0.24 + `crossterm` 0.27 | Rich |

Rust modules are gated on `cargo`; when it is missing they are skipped with a
warning and the rest of Clynxer still builds (`clynxer/Makefile:19-25`).

---

## 4. Build system

### 4.1 Root `Makefile` targets

| Target | Effect |
| --- | --- |
| `make buildCLynxer` | **The main target.** Builds `clynxer/clynxer`, then `make -C clynxer rust` and `make -C clynxer all` (`Makefile:153-156`). |
| `make testCLynxer` | `buildCLynxer`, then `make -C clynxer test` (`Makefile:70-71`). |
| `make test` | `buildCpp` + `testCLynxer` + the Python suites (`test/validate.py`, `test/remaining.py`). |
| `make build` / `buildAll` | Full Python Lynxer PyInstaller build (`dist/lynxer`). |
| `make buildLite` | Python Lynxer "lite" build — pure-stdlib modules only. |
| `make buildCpp` | The Python Lynxer C++ extension (`lynxer/setup.py build_ext --inplace`). |
| `make cargo` | Just the Rust backends. |
| `make venv` / `deps` / `liteDeps` / `platform-check` | Python toolchain setup. |
| `make validate` / `golden` / `check` | Python-side validation, the golden corpus, and linting. |
| `make cleanAll` | Everything: `clean` + `cleanC` + `cleanLynxc` + `cleanCLynxer`. |

`make cleanAll` is what the session started from. It deletes `__pycache__`,
`*.pyc`, non-stdlib `*.lynxc`, `build/`, `dist/`, `lynxer/*.so`,
`clynxer/clynxer`, the `clynxer/*.o` objects, **all `clynxer/stdlib/*.so`** and
the whole Cargo target directory (`clynxer/build/rust`) — which is why the next
build recompiles every Rust crate from scratch. That is the dominant cost of a
clean build and the reason a one-line Rust fix is expensive to verify after a
`cleanAll`.

### 4.2 `clynxer/Makefile`

- `all: $(TARGET) $(NATIVE_BUILT)` — the binary plus every native module.
- `rust: $(RUST_MODULES)` — only the nine Rust modules.
- The binary links 12 `.o` files; the C++ stdlib modules are built by the
  wildcard rule `stdlib/%.so: stdlib/%.cpp`; the Rust modules by
  `build/rust/release/libclynxer_%.so` → `cp` to `stdlib/%.so`
  (`clynxer/Makefile:87-96`).
- Rust builds use `RUSTFLAGS="-C relocation-model=pic"`, which is required for
  the `.so` to be `dlopen`-able into a PIE executable.

### 4.3 Toolchain requirements

`c++` (C++17), `make`, `cargo` + a Rust toolchain, `python3` for the Python side,
`node` on `PATH` for the `js` module only.

---

## 5. Test system

`make testCLynxer` → `clynxer/Makefile:101-346` runs, in order:

1. **Smoke** — `examples/hello.lynx` must print `Hello, Clynxer!` (stdin `Clynxer`).
2. **Hand-checked fixtures** — `conditions`, `comments`, `loops`, `inputln`,
   `missing_setup` (exit 1 + exact error text), `milestone4`,
   `milestone4_patterns`, `control_flow_error`, `builtins` (a ~65-line expected
   transcript), `milestone3`, `m3_scalars`, `m3_scope`, `m3_errors`, `m3_const`,
   `m3_vargroup`, `milestone5`, `milestone5_codeblocks`, `milestone6`,
   `milestone6_math_native`, `native_stdlibs`, `native_signatures`.
3. **Compiled-vs-interpreted parity** — for `hello conditions loops comments
   builtins milestone4 milestone5 milestone5_codeblocks` and separately for the
   import-using `native_stdlibs milestone6_module milestone6_math_native
   stdlib_json stdlib_re stdlib_path`, each fixture is run both ways and
   **stdout, stderr and exit status must all match** (`clynxer/Makefile:178-206`).
4. **Bundling** — `--bundle` alias, multi-file `--compile` with extra module
   inputs, and `--include` of a data file read via `bundledFile()`
   (`clynxer/Makefile:207-222`).
5. **Bytecode rejection** — a fake `CLYXC` file must be rejected with an error
   containing `no longer supported` (`clynxer/Makefile:223-227`).
6. **`--list-stdlibs`** — run from `/tmp` (so it must not depend on the cwd) and
   every module in `LIST_STDLIB_MODULES` must be listed
   (`clynxer/Makefile:318-322`).
7. **Stdlib fixtures** — every `examples/stdlib_*.lynx` (25) is run with
   `CLYNXER_GAME_HEADLESS=1` and `diff -u`'d against its sibling `.expected`
   (`clynxer/Makefile:323-336`).
8. **Consolidated stdlib test** — `examples/stdlibTestAll.lynx` exercises all
   modules in one process.
9. **`examples/game_clicker.lynx`** — must run headless without error.

On the Python side, `make test` runs `test/validate.py` and
`test/remaining.py` against the 55 `test/test*.lynx` fixtures. `make golden` runs
the Stage-1 corpus in `test/golden/` via `scripts/golden_corpus.py`.

**Cross-implementation parity is not part of `make testCLynxer`.** The
"15 of 55 pass" baseline quoted in `todo.md:260` (dated 2026-09-13) is a manual
measurement against the Python fixtures and was **not re-verified** in this
session. Note also that a naive old-vs-new diff is unreliable: several fixtures
are nondeterministic, `PYTHONPATH` shadowing can make the Python side import the
wrong tree, and `_here`-relative paths resolve differently between the two
runners.

---

## 6. Standard library inventory

**Clynxer ships 27 modules; the Python reference has 33.**

### 6.1 Clynxer modules by backend

| Backend | Count | Modules |
| --- | --- | --- |
| C++ (`stdlib/*.cpp`) | 15 | `cli`, `csv`, `debug`, `fileIO`, `js`, `math`, `multiprocessing`, `os`, `path`, `random`, `re`, `regex`, `shell`, `sys`, `time` |
| Rust (`rust/<name>/`) | 9 | `game`, `image`, `json`, `lua`, `network`, `server`, `sound`, `sqldb`, `tui` |
| Pure Lynxer | 3 | `colorlib`, `text`, `typing` |
| **Total** | **27** | |

### 6.2 Python modules with no Clynxer equivalent

| Python module | Disposition |
| --- | --- |
| `http`, `net` | **Superseded** by `network` + `server`. |
| `mathPlus` | **Merged into `math`** (`todo.md:40-45`). |
| `venv` | **Not implemented by design** — a Python concept with no C++ equivalent (`limitations.md:56-59`). |
| `tkinter`, `tkinterPlus` | Deferred. `todo.md:199` states the plan changed: no they will not be ported; a **`graphics.lynx` module using Rust `iced`** will replace tkinter. |
| `turtle` | Deferred/abandoned — `todo.md:199` notes the Rust `turtle` crate was last updated in 2019. |

Clynxer adds `network`, which does not exist in the Python tree.

---

## 7. Milestone status

Corrected against `todo.md` and verified against the source tree. The earlier
version of this report got several of these wrong (§9.1).

| Milestone | Status | Evidence |
| --- | --- | --- |
| **0 — small executable** | **Complete** | `clynxer/clynxer` builds; `--help` / `--version` work. |
| **1 — language core** | **Complete** | Lexer/parser/`setup`+`main` enforcement/typed vars/print all present and covered by fixtures. |
| **2 — control flow** | **Complete** | All loop forms, `break`/`continue`/`restart`; `examples/loops.lynx`. |
| **3 — runtime model** | **Complete** | Extended value model, lists/tuples, `inter"..."`, full scalar type set, `const`, structs/classes/enums/vargroups, pattern matching. |
| **4 — operators, statements, errors** | **Complete** | Bitwise/word operators, `switch`, `try`/`catch`; `examples/milestone4.lynx`. |
| **5 — functions and code blocks** | **Complete** | `func`/`global`/`local`, codeblocks, `exec(){{name}}`, `overrideMain`, class methods. |
| **6 — module system and stdlib** | **Complete for the ported set** | 27 modules; Rust migration done (`todo.md:11-26`). Remaining sub-items are about freezing contracts and adding failure-path fixtures (`todo.md:204-215`), not about missing modules. |
| **7 — native APIs** | **Complete for what is ported** | Built-ins with explicit unsupported-feature errors, native-memory family, Linux syscalls. Managed filesystem/process/async/sound/FFI/native-thread APIs remain (`todo.md:226-227`). |
| **8 — compiler, bytecode, CLI surface** | **Superseded, then complete** | CLI parity done. The bytecode/`CLYXC`/VM stack was **removed** (`todo.md:50-55`); `--compile` now emits a standalone ELF with fully-working imports (`todo.md:56-63`), including multi-file and `--include` bundling. Only "add an optimization pass" is open (`todo.md:256`). |
| **9 — compatibility gates** | **Partially done** | Baseline comparison exists (15/55 at 2026-09-13, `todo.md:260`). Lexer/parser/runtime comparison, golden output tests, and full-suite-per-milestone are open (`todo.md:263-267`). |

**Bottom line on the earlier report's claim:** the "compiler pivot" the old report
described as missing is present. `--compile`/`--bundle` are implemented and
covered by tests (§5 items 3-5). The old report's recommendation to "implement a
Clynxer compiler" was based on a misreading.

---

## 8. Findings from the 2026-09-20 session

### 8.1 Summary

The session began with `make cleanAll && make buildCLynxer` failing. Four
independent classes of defect were found, **all of them in the three stdlib
modules added most recently — `sound`, `sqldb` and `tui`**:

| # | Class | Impact | Modules affected |
| --- | --- | --- | --- |
| A | API misuse against new crate versions | **build fails** | sound, sqldb, tui |
| B | Wrong native-module signature family | **segfault at first call** | sound, sqldb, tui (94 registrations) |
| C | Dropped `Sink` → no audio | silent wrong behaviour | sound |
| D | Fixture/expectation drift vs the wrapper contract | test fails | tui |

The pre-existing 24 modules were unaffected, which is what made B diagnosable:
they all use the packed signature family.

### 8.2 Finding A — the new modules did not compile

The build stops at the first failing crate, so the three modules surfaced one at
a time across separate `make` invocations. **20 + 4 + 5 compile errors.**

**`clynxer/rust/sound`**
- `Sink::try_new(&state._stream)` — rodio 0.17's `Sink::try_new` takes an
  `&OutputStreamHandle`, but `_stream` is the `OutputStream`. `OutputStream::try_default()`
  returns **both**; only the handle was being kept. Fixed by storing the handle
  alongside the stream.
- `source.loop_infinitely()` does not exist in rodio 0.17. The trait method is
  **`repeat_infinite()`** (verified in `rodio-0.17.3/src/source/mod.rs:179`).
- `export_int!(sound_pause, args, { 1 })` and `sound_resume` were missing the
  trailing `;`. These macros expand to an `extern "C" fn` **item**, and Rust
  requires an item-position macro invocation to be braced or semicolon-terminated.

**`clynxer/rust/sqldb`** (all rusqlite 0.29 API drift)
- `row.get_value(i)` does not exist → `row.get::<usize, rusqlite::types::Value>(i)`.
- `map.insert(col.clone(), value)` where `col: &String` resolved to
  `<&String as Clone>::clone`, i.e. it cloned the **reference**, not the string
  → `col.to_string()`.
- That fix exposed a borrow error: `stmt.column_names()` borrows `stmt`
  immutably and the returned `Vec<&str>` kept the borrow alive across the
  subsequent `stmt.query_map(...)` mutable borrow. Fixed by collecting the
  column names into an owned `Vec<String>`.

**`clynxer/rust/tui`**
- Missing `export_float` import (used by `tui_ask_float`).
- **10** macro invocations missing the trailing `;`.
- `println!("─".repeat(40))` — **this is invalid Rust, not a version issue.** The
  first argument to `println!` is the format string, and the built-in format
  macro cannot parse a method call in that position. It fails with
  `expected ',', found '.'` plus `argument never used` and
  `cannot find function 'repeat'`. Fixed to `println!("{}", "─".repeat(40))`.
- `tui_enter` contained a three-level `unwrap_or_else` fallback chain
  referencing items that do not exist — `crossterm::terminal::TakeAlternativeScreen`,
  `ratatui::backend::TermionBackend`, and `crossterm::backend::CrosstermBackend`.
  It also could not type-check even if the names existed, because the arms return
  different `Terminal<B>` types. Replaced with `state.terminal = Terminal::new(backend).ok();`
  keeping `raw_mode = true` so `tui_exit` still restores the screen.

### 8.3 Finding B — packed-ABI signature mismatch (the most serious)

**Symptom.** After fixing the compile errors, `make buildCLynxer` succeeded, but
running the `sound` fixture exited **139 (SIGSEGV)** after printing only the two
`loadSound` results. Isolating the ops showed that **every** sound op crashed —
including `soundCount()`, which touches no player and no file:

```
playSound(-1)      -> exit=139
stopSound(-1)      -> exit=139
getSoundLength(-1) -> exit=139
soundCount()       -> exit=139
setSoundVolume(-1,0.5) -> exit=1  "unsupported native signature 'cdecl:int64(int64,double)'"
```

**Root cause.** All three new modules declared **per-argument** signatures in
their OPS tables:

```
sound:  cdecl:int64(cstring), cdecl:int64(int64), cdecl:float64(int64), cdecl:int64(int64,double) ...
sqldb:  cdecl:cstring(int64,cstring), cdecl:int64(int64,cstring) ...
tui:    cdecl:int64(cstring,cstring,cstring), cdecl:float64(cstring) ...  (71 entries)
```

`callNative` selects the packed path **only** when the parameter list is exactly
`...` (`clynxer/ast.cpp:566`). Any other shape is looked up in
`nativeCallTable()`, which calls the symbol through an exact C prototype —
`int64_t(*)(int64_t)`, `const char*(*)(const char*)`, and so on. The Rust export
is actually

```rust
pub unsafe extern "C" fn op(nums: *const f64, nnums: i64,
                            strs: *const *const c_char, nstrs: i64) -> i64
```

so the call lands on a **mismatched calling convention** and the process crashes
on the first invocation.

**Why it was hard to see.** The build is clean, `dlopen` succeeds, registration
succeeds, and the two `loadSound` calls appear to work — they return `-1` because
the fixture passes a non-existent file, which looks like correct error handling.
Only the ops that were actually invoked afterwards crashed. The single clean
error message came from `setVolume`, whose `int64(int64,float64)` shape is not in
the table at all, so it failed fast with
`unsupported native signature 'cdecl:int64(int64,double)'` instead of crashing.

**Scope.** 94 registrations were wrong: 12 in `sound`, 11 in `sqldb`, 71 in
`tui`. Every pre-existing Rust module (`game`, `image`, `json`, `lua`, `network`,
`server`) already used `cdecl:<ret>(...)` correctly — the three new modules were
the outliers.

**Fix.** All signatures normalised to `cdecl:{int64,float64,cstring}(...)`.

**Documentation gap.** `clynxer/docs/native-module-abi.md:124-134` presents the
packed form as an *option* — "An API with long or variadic argument lists **can**
use the wildcard parameter token `...`" — which reads as a convenience rather
than a requirement. For a Rust `cdylib` behind `clynxer_abi` it is effectively
**mandatory**, because `clynxer_abi`'s `export_*!` macros generate exactly the
four-scalar packed prototype. The doc should say so, and should warn that using
a fixed shape with such a module compiles cleanly and segfaults at call time.

### 8.4 Finding C — `sound` never retained its player

**Symptom.** `sound_play` / `sound_loop` returned `1` and reported success, but
playback stopped immediately.

**Root cause.** Both ops created a `Sink`, appended the decoded source, called
`sink.play()`, and then let the sink drop at the end of the closure. rodio's
`Drop for Sink` sets `stopped = true` unless the sink was detached
(`rodio-0.17.3/src/sink.rs:235-243`), so the audio was silenced the moment the
call returned. The module's registry stored only `{path, streaming}` — there was
nowhere for the player to live.

**Fix.** `SoundEntry` now holds `sink: Option<Sink>` and `volume: f32`, and the
whole op set was rewritten to use it, matching the Python reference
(`lynxer/stdlib/sound.lynx`):

| Op | Behaviour now |
| --- | --- |
| `play` / `loop` | Stop and replace any existing player, apply the stored volume, append (`repeat_infinite()` for loop), `play()`, **store** the sink. |
| `stop` | Stop the sink, clear it; `true` when the handle exists (even with no player), matching the reference. |
| `pause` / `resume` | `Sink::pause()` / `play()`; `false` only when no player is active — the reference requires a live player. |
| `setVolume` | Clamp to `[0,1]`, store it, apply to a live sink. |
| `isPlaying` | `!empty() && !is_paused()` instead of the previous always-true stub. |
| `release` | Stop the sink, clear the slot; a **second** release now returns `false` (the reference only sets `result = true` when the entry was non-null). |
| `count` | Counts **live** slots; previously returned `entries.len()`, so released handles still counted. |

Two further corrections were made while here:

- The write-only `streaming` field was removed. Under rodio, `load` and
  `load_streaming` are genuinely the same operation (both decode from the file
  handle), so the two ABI entry points now differ only in name, with a comment
  saying so. This also removed a `dead_code` warning.
- **Device initialisation no longer panics.** `SoundState::new()` used
  `OutputStream::try_default().unwrap()`. On a host with no audio device that
  panics, and because every op runs inside `guard_int`, the panic is converted
  into the **`-1` sentinel** — which breaks the stated headless-safe contract in
  the fixture header and would make e.g. `soundCount()` print `-1` instead of
  `0`. It is now `OutputStream::try_default().ok()` stored as an `Option`, and
  playback ops report "did not start" (`0`) when there is no device.

### 8.5 Finding D — `tui` fixture and expectation drift

- `clynxer/examples/stdlib_tui.lynx` called `global.tui.exists()` and
  `global.tui.version()`, but **neither the C++ wrapper nor the Python
  reference defines those names** — both define `tuiExists()` / `tuiVersion()`
  (`clynxer/stdlib/tui.lynx:9,12`; `lynxer/stdlib/tui.lynx:9,22`). The fixture
  was the outlier; it was corrected. The failure mode was
  `unknown function 'global.tui.exists'`.
- `clynxer/examples/stdlib_tui.expected` listed `1` / `1` for the two
  `styleValid()` calls, but the wrapper declares `-> bool`
  (`clynxer/stdlib/tui.lynx:81`) and the Python reference returns a `bool`. It
  therefore prints `true` / `true`. The expected file was corrected.
- **Caveat that should not be lost:** the Rust `styleValid` is still a stub that
  answers "valid" unconditionally (its own comment says *"Assume valid for
  simplicity"*). The corrected `.expected` therefore records the stub's
  behaviour, not Rich's — `styleValid("not-a-style")` is not genuinely tested.

### 8.6 Verification performed

| Check | Result |
| --- | --- |
| `make buildCLynxer` | passes — binary + 24 native modules |
| `make testCLynxer` | passes — all 9 stages of §5 |
| All 25 `examples/stdlib_*.lynx` fixtures | pass, diffed against `.expected` |
| `examples/stdlib_sound.lynx` | passes (previously SIGSEGV) |
| `examples/stdlib_sqldb.lynx` | passes |
| `examples/stdlib_tui.lynx` | passes (previously two distinct failures) |
| Real playback lifecycle | verified end-to-end against a generated 10 s 440 Hz WAV: `load`→ok, `count`=1, `length`=10, `isPlaying`=false, `play`→true, `isPlaying`=**true**, `pause`→true, `isPlaying`=false, `resume`→true, `isPlaying`=true, `setVolume`→true, `loop`→true, `isPlaying`=true, `stop`→true, `isPlaying`=false, `release`→true, `release` again→false, `count`=0. |

The `isPlaying == true` immediately after `play` is the specific assertion that
could not have held before Finding C was fixed.

---

## 9. Documentation inconsistencies

Ten were catalogued. Items 1-6 and 9 are **stale documentation**, i.e. the code
is right and the prose is wrong. Items 7-8 and 10 are ambiguities or internal
contradictions that should be resolved deliberately.

| # | Location | Problem |
| --- | --- | --- |
| 1 | `clynxer/docs/limitations.md:61-68` | Says "`tui` remains intentionally unimplemented because it needs a full-screen terminal library" and that the Python `sound` and `sqldb` modules are "out of scope". **All three are now implemented, built and tested.** |
| 2 | `clynxer/docs/README.md:88` | "`tui` is not implemented yet." Now false. |
| 3 | `clynxer/docs/README.md:55-80` | The module table lists 25 modules and **omits `sound`, `sqldb` and `tui`** entirely. |
| 4 | `clynxer/docs/README.md:72` | Lists `random` as "*pure* — deterministic LCG in Lynxer", but `random` is **native**: `clynxer/stdlib/random.cpp` exists, `stdlib/random.so` is built, and `clynxer/stdlib/random.lynx:9` does `importAs("random.so", "nativeRandom")`. |
| 5 | `clynxer/docs/README.md:21` | `make` is documented as "wipe and re-fetch third-party headers, then build". There is no `third_party/` and no CMake staging any more (`todo.md:19-20`). |
| 6 | `clynxer/docs/README.md:26-31, 82-86` | Repeats "the `game`, `json`, `network` and `server` modules are Rust crates". There are **nine** Rust modules. |
| 7 | `clynxer/docs/native-module-abi.md:8-11` | Same thing — names only `game`, `json`, `network`, `server` as Rust backends. |
| 8 | `todo.md:298` vs `todo.md:186-195` | Internal contradiction: the "Current boundary" section says "the remaining Python reference modules not yet ported are `sound`, `sqldb`, `tui`, `tkinter`, `tkinterPlus`, and `turtle`", while the milestone list marks `sound`, `sqldb` and `tui` as **`[x]` complete**. |
| 9 | `clynxer/docs/limitations.md:49` | "At most four arguments per native signature" contradicts the packed section of `native-module-abi.md:124-150`, where a packed call carries up to 64 numbers and 64 strings. The "four" refers to the four C parameters of the packed prototype, not to argument count. |
| 10 | `clynxer/docs/native-module-abi.md:124-134` | Presents the packed `...` form as optional. For Rust `cdylib` modules using `clynxer_abi` it is effectively mandatory, and getting it wrong segfaults at call time (§8.3). This gap directly caused Finding B. |

### 9.1 Corrections to the previous version of this report

| Previous claim | Reality |
| --- | --- |
| "Milestone 9 — compatibility gates: **not yet implemented**; no evidence of compatibility gates" | A baseline comparison exists (`todo.md:260`, 15/55 at 2026-09-13), and there **are** compatibility gates in the suite: compiled-vs-interpreted parity (`clynxer/Makefile:178-206`), `.lynxc` rejection (`:223-227`), and 25 `.expected` diffs (`:323-336`). |
| "No standalone compiler for Clynxer exists" | `--compile` / `--bundle` produce standalone ELF executables with working module imports, multi-file bundling and `--include` (`todo.md:50-76`; verified via `clynxer --help` and stage 3-4 of the suite). |
| Milestone 4 evidence was "ABI macros (`export_int!`…)" | Those are native-module registration macros, unrelated to Milestone 4 (operators/statements/errors). The milestone's real evidence is `examples/milestone4.lynx` and its expected transcript. |
| "The `cli` files in `clynxer/stdlib` suggest CLI integration" | `cli` is the **user-facing command-line stdlib module**, entirely unrelated to Clynxer's own `--help`/subcommand surface in `shell.cpp`. |
| Timestamp `2026-09-20 14:30 UTC` | A future time; not a real measurement. |

---

## 10. Open items and known bugs

### 10.1 Known parity bugs (`todo.md:269-276`)

- **test25** — a double `memoryFree()` aborts with a glibc double-free instead of
  raising a source-located error. Python raises *"address refers to freed
  memory"*.
- **test26** — reading an invalid address segfaults instead of raising
  *"invalid native memory address"*.
- **test22** — the zero-size allocation path crashes with a `stoll` interpreter
  failure instead of a clean error.

These three are the same class of problem this session found in `sound`: a
process-level crash where a sentinel or located error is expected. They are
worth attacking together.

### 10.2 Open roadmap items

- Whether `tkinter` / `tkinterPlus` / `turtle` get Rust backends — `todo.md:196-199`
  now says no, with a `graphics.lynx` module on Rust `iced` planned instead.
- Freeze each module's operation names, signatures, handle ownership, string
  lifetime, error sentinels, callbacks, interruption behaviour and cleanup before
  a second backend is introduced (`todo.md:204-208`). **Finding B is direct
  evidence for why this matters.**
- Rust-backend failure-path fixtures: success, malformed input, invalid handles,
  missing files, timeouts, cleanup, optional-dependency failures, and the
  compiled/bundled path (`todo.md:209-212`). The current `sound` and `sqldb`
  fixtures only exercise invalid handles.
- Managed filesystem / process / networking / async / sound / FFI / native-thread
  APIs (Milestone 7, `todo.md:226-227`).
- An optimization pass beyond constant folding (Milestone 8, `todo.md:256`).
- Lexer/parser/runtime comparison against Python, golden output tests, and
  running the full suite every milestone (Milestone 9, `todo.md:263-267`).

### 10.3 Known limitations intentionally kept

`venv` is deliberately absent; `re`/`regex` run on `std::regex` (ECMAScript
grammar) and report lookbehind, atomic groups and `\p{...}` as errors;
`cli`'s Click/Typer builders are hard "unknown function" errors; `js` requires
`node` and applies no timeout. Full list: `clynxer/docs/limitations.md`.

---

## 11. Recommendations

1. **Update the documentation before writing more modules.** Items 1-7 and 9-10
   in §9 are stale-but-shipped documentation that will mislead the next port.
   The single highest-value edit is `native-module-abi.md`: state that a Rust
   `cdylib` must use the packed `...` form, and record the segfault failure mode.
2. **Resolve the `todo.md` contradiction (#8).** `sound`, `sqldb` and `tui` are
   done; the "Current boundary" paragraph still lists them as unported.
3. **Document the two-family dispatch rule prominently.** "Fixed shape → exact C
   prototype; `...` → packed four-scalar prototype; pick by language, not by
   argument count" deserves to be the first thing in the ABI doc, because the
   failure is a silent SIGSEGV rather than a build error.
4. **Add failure-path fixtures for the three new modules** (`todo.md:209-212`).
   The current fixtures only cover invalid handles, so Findings C and D were
   invisible to the suite — a working playback test would have caught C.
5. **Replace the `tui.styleValid` stub** with real style validation, or record in
   `.expected` that the "not-a-style" case is a known stub. As it stands the
   fixture asserts a value that is true only because nothing is implemented.
6. **Consider a build-time guard against Finding B.** Since the interpreter
   already knows which shapes are packed, a one-line check at registration time
   could reject a fixed-shape signature for a module that exports the packed
   prototype — turning a SIGSEGV into a located error. This is the same
   "prefer explicit errors over crashes" principle as the known parity bugs.
7. **Attack the three crash-vs-error parity bugs (§10.1) as one unit.** They are
   mechanically similar and all three are the only remaining places where a
   user action can abort the process.

---

## Appendix A — file and line index

Everything cited in this report, for fast navigation.

**Clynxer interpreter**

| Reference | What it is |
| --- | --- |
| `clynxer/ast.cpp:224-520` | `nativeCallTable()` — the 32 fixed call shapes |
| `clynxer/ast.cpp:481,494,505` | The three packed (`...`) call shapes |
| `clynxer/ast.cpp:522-587` | `callNative()` — signature normalisation and dispatch |
| `clynxer/ast.cpp:560-563` | Shape lookup + `unsupported native signature` |
| `clynxer/ast.cpp:566` | `const bool packed = types.size() == 1 && types[0] == "..."` — **the packed/fixed selector** |
| `clynxer/rust/abi/src/lib.rs:199-269` | `export_int!` / `export_float!` / `export_string!` / `lynxer_module!` |

**Newly fixed modules**

| Reference | What it is |
| --- | --- |
| `clynxer/rust/sound/src/lib.rs` | `SoundEntry { path, volume, sink }`, `SoundState { entries, output }`, `start_playback()`, all 12 ops, `OPS` table |
| `clynxer/rust/sqldb/src/lib.rs` | `value_to_json()`, owned column-name collection, `OPS` table |
| `clynxer/rust/tui/src/lib.rs` | `tui_enter`, `println!("{}", "─".repeat(40))`, `OPS` table (71 entries) |
| `clynxer/examples/stdlib_tui.lynx`, `.expected` | Corrected `tuiExists`/`tuiVersion` calls and `true`/`true` |
| `clynxer/stdlib/{sound,sqldb,tui}.lynx` | The Lynxer-facing wrappers (unchanged) |

**Contract and reference**

| Reference | What it is |
| --- | --- |
| `clynxer/docs/native-module-abi.md` | The ABI contract; `:82-115` fixed shapes, `:124-150` packed |
| `clynxer/docs/limitations.md:24-33` | No bytecode backend; `--compile` embeds modules |
| `clynxer/docs/limitations.md:61-68` | **Stale** — claims sound/sqldb/tui out of scope |
| `lynxer/stdlib/sound.lynx` | The Python reference semantics for `sound` |
| `lynxer/stdlib/tui.lynx:9,22` | `tuiExists` / `tuiVersion` — the canonical names |
| `clynxer/stdlib/tui.lynx:81` | `styleValid(...) -> bool` |

**Build and test**

| Reference | What it is |
| --- | --- |
| `Makefile:153-156` | `buildCLynxer` |
| `Makefile:70-71`, `:65-68` | `testCLynxer`, `test` |
| `Makefile:195-197` | `cleanAll` composition |
| `clynxer/Makefile:15` | `RUST_MODULE_NAMES` |
| `clynxer/Makefile:87-96` | Rust crate → `stdlib/<name>.so` rule |
| `clynxer/Makefile:101-346` | The whole test suite |
| `clynxer/Makefile:178-206` | Compiled-vs-interpreted parity gates |
| `clynxer/Makefile:223-227` | `.lynxc` rejection gate |
| `clynxer/Makefile:318-322` | `--list-stdlibs` gate |
| `clynxer/Makefile:323-336` | Stdlib fixture `.expected` diffs |
| `.github/workflows/buildCLynxer.yml` | CI: build + `make testCLynxer` on push/PR |

**Roadmap and docs**

| Reference | What it is |
| --- | --- |
| `todo.md:3-5` | What Clynxer is |
| `todo.md:11-76` | Compiler pivot / stdlib consolidation (all `[x]`) |
| `todo.md:186-195` | `sound`, `sqldb`, `tui` marked complete |
| `todo.md:196-199` | The tkinter/`graphics.lynx`+`iced` decision |
| `todo.md:204-215` | Open: freeze contracts, failure-path fixtures |
| `todo.md:256` | Open: optimization pass |
| `todo.md:260-267` | Milestone 9 baseline and open gates |
| `todo.md:269-276` | The three known parity bugs |
| `todo.md:298` | **Stale** — lists sound/sqldb/tui as unported |
| `clynxer/docs/README.md:55-80` | Module table (missing sound/sqldb/tui) |
| `clynxer/docs/README.md:91-110` | "Adding a stdlib module" recipe |
| `README.md:6-14` | What Lynxer is; Linux-only, amd64/aarch64 |
| `clynxer/README.md:1-9` | What Clynxer is |

---

**End of Report**
