# Clynxer Investigation Report

**Date and Time:** 2026-09-20 20:26 CEST (2026-09-20 18:26 UTC)
**Author:** investigation session (automated)
**Scope:** Clynxer source tree under `clynxer/`, the `sound`/`sqldb`/`tui` stdlib
modules, the native-module ABI, the build and test system, and the state of
`todo.md` and the Clynxer documentation.

> **Build-system note (2026-09-22):** `clynxer/Makefile` has since been merged
> into the root `Makefile`, so every `clynxer/Makefile:NNN` citation below is a
> historical reference to a file that no longer exists. The Clynxer target
> names are unchanged (`make buildCLynxer`, `make testCLynxer`, `make cargo`),
> and the suite now runs from the repo root.
**Result:** `make buildCLynxer` and `make testCLynxer` both pass. **Six classes of
defect** were found in the three newest stdlib modules — **all six fixed**.
Eleven documentation inconsistencies were catalogued and corrected. A new
automated check (`clynxer/scripts/check_module_contracts.py`) guards the two
defect classes the fixture suite could not see. **Milestone 6 is complete** —
see §12.

> **This revision replaces the earlier version of this report.** The earlier
> version was dated `2026-09-20 14:30 UTC` (a future timestamp, and therefore
> not a real measurement), and several of its claims were wrong — most notably
> that Milestone 9 was "not yet implemented" and that "no standalone compiler
> for Clynxer" exists. Both are contradicted by `todo.md` and by the shipped
> `--compile`/`--bundle` backend. See §9.1 for the corrections.
>
> **Revision 3 (11:15 CEST)** added Findings E and F, recorded the documentation
> fixes in §9, and confirmed byte-identical `sqldb` output against the Python
> reference.
>
> **Revision 4 (11:35 CEST)** fixed Finding F and added
> `check_module_contracts.py`, now wired into `make test`. It found an **18th**
> bad index that manual inspection had missed.
>
> **Revision 5 (20:26 CEST)** completes Milestone 6: the frozen contracts
> document, failure-path fixtures for every Rust backend with compiled/bundled
> parity, the ABI extension policy, and `extending.md`. See §12.
>
> **Revision 6 (21:06 CEST)** starts Milestone 7: the managed `filesystem*`
> family is ported, byte-identical to the Python reference.
>
> **Revision 7 (21:38 CEST)** adds `process*` and `networking*` the same way —
> 33 of 69 built-ins now ported and byte-identical to the reference. The four
> remaining families each need a decision before they can be written; see
> §13.4.
>
> **Revision 8 (22:16 CEST)** adds `sound*` (9), bridged to the Rust `sound`
> stdlib module — 42 of 69. Three remaining families are still blocked on
> decisions. See §13.6.
>
> **Revision 9 (22:35 CEST)** finds that `nativeThread*` needs a **missing
> language feature** (named global functions as values) on top of the
> threading work, so it was not started. See §13.8.
>
> **Revision 10 (2026-09-23)** records everything since 2026-09-22, after the
> compiler pivot: bytecode, `.lynxc`, `--view-bytecode`, `--benchmark-compile`
> and `--no-cache` are gone and `--compile` writes an ELF that embeds source; an
> AST optimizer (`clynxer/optimizer.cpp`, `--no-opt`, `CLYNXER_OPT_REPORT`)
> landed; the CLI/diagnostic golden gate (`clynxer/scripts/check_golden.py`) and
> the low-level amd64/arm64 fixtures (`examples/lowlevel_memory.lynx`,
> `examples/lowlevel_syscalls.lynx`) run in `make testCLynxer`, which both
> Clynxer CI workflows now run with `CLYNXER_SKIP_DISPLAY=1` (the display and
> audio tests are skipped on a runner). **Milestones 8 and 9 are complete**;
> every entry in §10.1 is resolved; `clynxer/docs/parity.md` is the parity
> scope. The repo-root `Makefile` is authoritative — the `clynxer/Makefile`
> citations below are historical (see the build-system note).
>
> **Revision 11 (2026-09-23, later)** records that **Clynxer has surpassed the
> Python implementation for real use** and is the primary implementation.
> Evidence: 27 natively backed stdlib modules (the Python set of 33 includes
> seven that are Python-only or superseded — `http`, `net`, `mathPlus`,
> `tkinter`, `tkinterPlus`, `turtle`, `venv`), a standalone ELF `--compile`, an
> AST optimizer, a frozen native-module ABI with a Rust `cdylib` target,
> cooperative threads plus an async family, ~14k lines of C++ and ~9.8k of Rust
> against ~20.8k of Python, and 73 example fixtures (38 with pinned output) run
> through the contract/golden/parity gates on amd64 **and** arm64 in CI. The
> remaining Python-only surface is documented as "will not be done" in
> `docs/limitations.md`. The Python package is flagged as the frozen behaviour
> reference in `README.md`, `lynxer/__init__.py` and `lynxer/shell.py`. Two
> language bugs found while verifying the Clynxer docs were fixed: compound
> assignment on a field, and `-> none` return annotations
> (`examples/language_fields.lynx`).

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
- Distributed as a PyInstaller one-file binary (`make buildLynxer`, `make buildLynxerLite`).

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
| `make build` / `buildAll` | Everything: `buildLynxer` + `buildLynxerLite` + `buildCLynxer`. |
| `make buildLynxer` | Full Python Lynxer PyInstaller build (`dist/lynxer`). |
| `make buildLynxerLite` | Python Lynxer "lite" build — pure-stdlib modules only (`dist/lynxer-lite`). |
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
| **6 — module system and stdlib** | **Complete** | 27 modules, all with a wrapper, a backend, a doc page and a fixture. See §12 for what closed it out (`clynxer/docs/stdlib-contracts.md`, `clynxer/docs/extending.md`, failure-path fixtures, ABI policy). |
| **7 — native APIs** | **Complete for what is ported** | Built-ins with explicit unsupported-feature errors, native-memory family, Linux syscalls. Managed filesystem/process/async/sound/FFI/native-thread APIs remain (`todo.md:226-227`). |
| **8 — compiler, bytecode, CLI surface** | **Complete** | The bytecode/`CLYXC`/VM stack was **removed**; `--compile` emits a standalone ELF with fully-working imports, multi-file and `--include` bundling. The post-pivot optimization item is now done as the AST optimizer (`clynxer/optimizer.cpp`). |
| **9 — compatibility gates** | **Complete** | Baseline comparison (15/55 at 2026-09-13) plus the divergence-aware gates: lexical-divergence fixtures, `clynxer/scripts/check_golden.py` for the CLI/diagnostic text, low-level amd64/arm64 fixtures, `make testCLynxer` in CI, and `clynxer/docs/parity.md` as the parity scope. |

**Bottom line on the earlier report's claim:** the "compiler pivot" the old report
described as missing is present. `--compile`/`--bundle` are implemented and
covered by tests (§5 items 3-5). The old report's recommendation to "implement a
Clynxer compiler" was based on a misreading.

---

## 8. Findings from the 2026-09-20 session

### 8.1 Summary

The session began with `make cleanAll && make buildCLynxer` failing. Six
independent classes of defect were found, **all of them in the three stdlib
modules added most recently — `sound`, `sqldb` and `tui`**:

| # | Class | Impact | Modules affected |
| --- | --- | --- | --- |
| A | API misuse against new crate versions | **build fails** | sound, sqldb, tui |
| B | Wrong native-module signature family | **segfault at first call** | sound, sqldb, tui (94 registrations) |
| C | Dropped `Sink` → no audio | silent wrong behaviour | sound |
| D | Fixture/expectation drift vs the wrapper contract | test fails | tui |
| E | Wrapper/backend contract mismatch (path vs handle) | **every call fails** | sqldb |
| F | Per-type argument indices used positionally | latent wrong reads (stubbed) | tui (18 ops) |

The pre-existing 24 modules were unaffected, which is what made B diagnosable:
they all use the packed signature family.

**All six are fixed.** A and B were found by building and running the fixtures.
C and D were found by reading the code against the reference. E and F were found
only because a *different* question was asked — whether the module satisfied the
contract its own wrapper advertises. That is the general lesson: this suite's
fixtures cannot detect a module that is broken in the same way on both sides of
the comparison, because they assert what the code does rather than what the
contract says. `clynxer/scripts/check_module_contracts.py` (§8.9) now tests the
contract directly.

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

### 8.6 Finding E — `sqldb` was entirely non-functional

**Symptom.** Every `sqldb` function returned `"ERROR: invalid handle"` for any
input, valid or not. The module's own fixture "passed" because its expected file
recorded exactly that: ten lines of `ERROR: invalid handle`.

**Root cause.** A wrapper/backend contract mismatch over what argument 0 means.

| Layer | Says argument 0 is |
| --- | --- |
| Python reference `lynxer/stdlib/sqldb.lynx` | a database **path** |
| C++ wrapper `clynxer/stdlib/sqldb.lynx` | a database **path** |
| `clynxer/docs/stdlib/sqldb.md` | a database **path** |
| Fixture `examples/stdlib_sqldb.lynx` | a database **path** |
| **Rust backend** `rust/sqldb/src/lib.rs` | an integer **handle index** |

The backend implemented a handle registry (`SqlDbState { connections }`, with a
`sqldb_open` op returning an index) while every other layer passed a path. Each
op did `let idx = args.int(0) as usize;` — but argument 0 is a *string*, so it
lands in `strs`, not `nums`, and `args.int(0)` reads `nums[0]` of an empty list
and returns **`0`**. `connections.get(0)` was therefore always `None` (nothing
had ever called `open`, which the wrapper does not expose), and every call
returned `"ERROR: invalid handle"`.

Calling the native module directly confirmed the backend was also broken on its
own terms: `open()` returned handle `0`, after which `execute(0, "CREATE ...")`
returned `"ERROR: not an error"` — the SQL was read from `args.string(1)`, i.e.
the *second* string, which does not exist in a two-argument call.

**Fix.** Rewrote the backend to match the contract the other four layers already
agreed on: every op takes a path, opens a connection for the duration of the
call, and closes it — which is exactly what the Python reference does
(`_conn = _sqlite3.connect(path) … _conn.close()`). The handle registry,
`SqlDbState`, `with_state`, the `STATE` thread-local and the `sqldb_open` op are
gone; `with_conn(path, f)` replaced them. Because the API is now string-in, the
per-type indices line up naturally (`args.string(0)` = path, `args.string(1)` =
SQL).

Three further corrections were needed to reach parity:

- **`scalar` read the value as `String`**, so `SELECT COUNT(*)` (an integer)
  failed to convert and returned `""`. The reference does `str(_row[0])`, so the
  column is now read as `rusqlite::types::Value` and rendered with a
  `value_to_scalar()` helper. `SELECT COUNT(*)` now returns `2` where it
  previously returned `""`.
- **`scalar` returned `""` on every error**, including a failure to open the
  database. The reference returns `"ERROR: <message>"` for a genuine exception
  and `""` only for a missing row or NULL. An absent row is now matched
  explicitly via `rusqlite::Error::QueryReturnedNoRows`.
- **JSON separators did not match.** `query`/`queryArgs`/`tables` used
  `serde_json::to_string` (compact: `{"id":1}`) while the reference emits
  Python `json.dumps` defaults (`{"id": 1}`). `rust/json` already solves this
  with a `compact()` helper, so `sqldb` now carries an equivalent
  `json_dumps()`. The helper is duplicated rather than shared because the
  workspace's `.so`s are intentionally self-contained (the same reason the C++
  side embeds `native_json.hpp` into each module) and `clynxer_abi` is
  deliberately dependency-light.

**Result.** The fixture was rewritten to exercise real behaviour against a
repo-local scratch database (following the `fileIO` fixture's convention) and
its output is now **byte-identical to the Python reference** — verified by
running the same fixture through both implementations and diffing.

**Why this matters beyond `sqldb`.** `todo.md:186-195` marks `sqldb` complete and
`clynxer/docs/README.md` listed it as a working module. The only thing standing
between "documented as working" and "returns an error for every call" was a
fixture that asserted the bug. This is the same failure mode as Finding B: the
test suite compared the implementation against itself, not against the contract.

### 8.7 Finding F — `tui` reads packed arguments by position instead of by type

**Symptom.** None today — which is the point.

**Root cause.** Packed arguments are delivered as **two separate lists**, so
argument *n* of a given type must be read at index *n* **within that type**:
`args.int(i)` reads `nums[i]`, `args.string(i)` reads `strs[i]`. `tui` reads most
of its arguments as if a single positional list existed, e.g.
`tableAddColumn(idx, header, style)` does:

```rust
let _idx = args.int(0);        // correct (nums[0])
let _header = args.string(1);  // WRONG — strs[1] is the *style*
let _style = args.string(2);   // WRONG — out of bounds → ""
```

The established modules use the correct convention — `image_save(handle, path)`
reads `args.int(0)` **and** `args.string(0)`, and
`image_save_quality(handle, path, quality)` reads `args.int(0)`,
`args.string(0)`, `args.int(1)`.

**Eighteen of the 70 `tui` ops** were affected:

| Op | Wrong read | Correct read |
| --- | --- | --- |
| `printSyntax(code, lexer, lineNumbers)` | `int(2)` | `int(0)` |
| `printColumns(itemsJson, equal, expand)` | `int(1)`, `int(2)` | `int(0)`, `int(1)` |
| `printAligned(text, align, pad)` | `int(2)` | `int(0)` |
| `printPadded(text, top, right, bottom, left)` | `int(1)..int(4)` | `int(0)..int(3)` |
| `tableAddColumn(idx, header, style)` | `string(1)`, `string(2)` | `string(0)`, `string(1)` |
| `tableAddRow(idx, valuesJson)` | `string(1)` | `string(0)` |
| `tableSetCaption(idx, caption)` | `string(1)` | `string(0)` |
| `tableSetBox(idx, boxName)` | `string(1)` | `string(0)` |
| `treeAdd(parentIdx, label)` | `string(1)` | `string(0)` |
| `layoutSplitRows(idx, namesJson)` | `string(1)` | `string(0)` |
| `layoutSplitColumns(idx, namesJson)` | `string(1)` | `string(0)` |
| `layoutUpdate(idx, name, text)` | `string(1)`, `string(2)` | `string(0)`, `string(1)` |
| `layoutPanel(idx, name, text, title)` | `string(1)..string(3)` | `string(0)..string(2)` |
| `progressAddTask(idx, description, total)` | `string(1)`, `float(2)` | `string(0)`, `float(1)` |
| `statusUpdate(idx, text)` | `string(1)` | `string(0)` |
| `liveUpdate(idx, text)` | `string(1)` | `string(0)` |
| `livePanel(idx, text, title)` | `string(1)`, `string(2)` | `string(0)`, `string(1)` |
| `confirm(prompt)` | `int(0)` | *(none — see below)* |

The ops whose arguments are all strings or all numbers are correct — most of the
module, including `printStyled`, `panel`, `panelStyled`, `ruleStyled`, `table`,
`progressAdvance`, `progressUpdate` and the `tableSet*` numeric setters.

**Why it was invisible.** Almost all of them are placeholders that bind their
arguments to `_`-prefixed locals and return `0`, so the values are discarded.
The reads also fail *silently*: `Args` returns `0` / `""` past the end of a list
rather than raising, so there is no error to notice.

**Two of them were worse than the others.** `tableAddColumn` and
`layoutUpdate`/`layoutPanel` read a **shifted but in-bounds** value — they would
bind the style where the header belongs, and the title where the text belongs.
Those would have produced wrong data, not empty data, the moment the ops were
implemented.

**`confirm` was the 18th, and manual inspection had missed it.** Its read is at
index **0**, so a grep for "non-zero index reads" — the method used to build the
original 17-item list — could not see it. `tui_confirm_default` was registered
under *two* Lynxer names with different arities (`confirm(prompt)` and
`confirmDefault(prompt, defaultValue)`) while reading `int(0)` for the default;
`confirm(prompt)` passes no number at all. The fix splits the registration:
`confirm` now has its own op reading only the prompt, and `confirmDefault` keeps
the two-argument form. This is exactly the kind of thing the automated check
exists to catch, and it did so on its first run.

**Fixed.** All 18 sites corrected, `confirm` split, and
`clynxer/scripts/check_module_contracts.py` now enforces the convention (§8.9).

### 8.9 The new contract check

`clynxer/scripts/check_module_contracts.py` tests, for every
`stdlib/<name>.lynx` that imports `stdlib/<name>.so`:

1. **Op coverage** — every `global.native<Alias>.<op>(...)` call names an op the
   backend registers. *(failure)*
2. **Packed argument bounds** — for a Rust backend, where every op uses the
   packed signature, the highest index an op reads for a given kind must be
   lower than the number of arguments of that kind the wrapper passes. Numbers
   and strings are counted separately, which is how `clynxer_abi` delivers them.
   *(failure)*
3. **Unused registrations** — an op no wrapper function calls. *(warning)*

C++ backends register a fixed shape, which the interpreter already type-checks
per argument when the op is called (`native call argument count does not match
signature`), so only rule 1 applies to them.

A wrapper function whose argument kinds cannot be inferred (an expression, a
nested call, an untyped parameter) is reported as *skipped* rather than guessed
at, so the check never fails on something it cannot read. In practice nothing is
skipped: the run reports **24 backends, 686 op calls checked, 0 skipped**.

It is wired into the first line of the `test` recipe in `clynxer/Makefile`, so
`make testCLynxer` and CI both run it, and it fails with a clear message if
`python3` is absent rather than silently skipping.

**Validated by injecting both historical bugs back in:**

| Injected fault | Detected |
| --- | --- |
| `tui::printAligned` reads `args.int(2)` instead of `args.int(0)` | yes — `'printAligned' reads args.int(2), but the wrapper tui.lynx passes 1 number argument(s)` |
| `sqldb::execute` reads `args.int(0)` for the path (the Finding E bug) | yes — `'execute' reads args.int(0), but the wrapper sqldb.lynx passes 0 number argument(s)` |

Both were reverted and the check returned to 0 errors. A check that cannot fail
is worthless, so this mattered more than the clean run.

**What it does not catch.** The two *shifted but in-bounds* reads
(`tableAddColumn`, `layoutUpdate`/`layoutPanel`) are within range, so the bounds
rule alone cannot see them — it only caught them because
`tableAddColumn`/`layoutPanel` also had an out-of-range read alongside. A
general "every argument is read exactly once" rule would catch all cases but
would false-positive on ops that deliberately ignore an argument (the `tui`
placeholders do exactly that). The bounds rule is the sweet spot: it catches
every case where a wrong index reads past the end, with no false positives.

### 8.10 Verification performed

| Check | Result |
| --- | --- |
| `make buildCLynxer` | passes — binary + 24 native modules |
| `make testCLynxer` | passes — all 9 stages of §5, including the new contract check |
| **Contract check** (§8.9) | **24 backends, 686 op calls checked, 0 skipped, 0 errors, 0 warnings** |
| Contract check negative tests | both historical bugs (Finding E, Finding F) re-injected and detected, then reverted |
| All 25 `examples/stdlib_*.lynx` fixtures | pass, diffed against `.expected` |
| `examples/stdlib_sound.lynx` | passes (previously SIGSEGV) |
| `examples/stdlib_sqldb.lynx` | passes, and **byte-identical to the Python reference** |
| `examples/stdlib_tui.lynx` | passes (previously two distinct failures) |
| Real playback lifecycle (sound) | verified end-to-end against a generated 10 s 440 Hz WAV: `load`→ok, `count`=1, `length`=10, `isPlaying`=false, `play`→true, `isPlaying`=**true**, `pause`→true, `isPlaying`=false, `resume`→true, `isPlaying`=true, `setVolume`→true, `loop`→true, `isPlaying`=true, `stop`→true, `isPlaying`=false, `release`→true, `release` again→false, `count`=0. |
| Real database lifecycle (sqldb) | `execute`, `executeArgs`, `query`, `queryArgs`, `scalar`, `tables`, `tableExists`, `lastInsertId`, `script` and the error path all behave correctly against a real SQLite file |
| Cross-implementation diff | the rewritten `sqldb` fixture produces output **identical** to `venv/bin/python lynxer/shell.py` on the same file — the strongest available evidence of parity |
| Workspace left clean | no scratch database or journal file remains after the run (`git status` clean apart from intended edits) |

The `isPlaying == true` immediately after `play` is the specific assertion that
could not have held before Finding C was fixed. The `sqldb` diff is the
assertion that could not have passed before Finding E.

**A note on the parity method.** Comparing Clynxer against the Python reference
worked here because the program is deterministic and self-contained. That is not
generally true — several fixtures in `test/` are nondeterministic, `PYTHONPATH`
shadowing can make the Python side import the wrong tree, and `_here`-relative
paths resolve differently between the two runners. The `sqldb` comparison was
run with `PYTHONPATH=` cleared and an explicit database path for that reason.

---

## 9. Documentation inconsistencies

Eleven were catalogued and **all eleven have been corrected** (see the status
column). Items 1-7 and 9-11 were **stale or incomplete documentation**, i.e. the
code is right and the prose was wrong or missing. Item 8 was an internal
contradiction in `todo.md`.

| # | Location | Problem | Status |
| --- | --- | --- | --- |
| 1 | `clynxer/docs/limitations.md:64-71` | Said "`tui` remains intentionally unimplemented because it needs a full-screen terminal library" and that the Python `sound` and `sqldb` modules were "out of scope". All three are implemented, built and tested. | **Fixed** — replaced with a "Modules that are not ported" section naming only `tkinter`/`tkinterPlus`/`turtle`, plus new `sound`, `sqldb` and `tui` divergence sections. |
| 2 | `clynxer/docs/README.md:88` | "`tui` is not implemented yet." | **Fixed** — sentence removed. |
| 3 | `clynxer/docs/README.md:55-80` | Module table listed 25 modules and omitted `sound`, `sqldb` and `tui`. | **Fixed** — three rows added; all 27 modules now have a row and a doc page. |
| 4 | `clynxer/docs/README.md:72` | Listed `random` as "*pure* — deterministic LCG in Lynxer", but it is native (`stdlib/random.cpp`, `stdlib/random.so`, `importAs` at `stdlib/random.lynx:9`). | **Fixed** — now "native \| seeded linear congruential generator in C++". |
| 5 | `clynxer/docs/README.md:21` | `make` documented as "wipe and re-fetch third-party headers, then build". There is no `third_party/` or CMake staging any more. | **Fixed** — now "build the interpreter and every native stdlib module". |
| 6 | `clynxer/docs/README.md:26-31, 82-86, 106-112` | Repeated "the `game`, `json`, `network` and `server` modules are Rust crates"; there are **nine**. Also referenced a non-existent `stdlib/libs.mk`. | **Fixed** — all three places list the nine modules; the `libs.mk` sentence removed. |
| 7 | `clynxer/docs/native-module-abi.md:8-11` | Same thing — named only four Rust backends. | **Fixed** — now lists all nine. |
| 8 | `todo.md:24-25`, `todo.md:294-299` vs `todo.md:186-195` | Internal contradiction: "Current boundary" listed `sound`, `sqldb`, `tui` as "not yet ported" while the milestone list marked all three `[x]`; and an earlier bullet still said "`tui` remains intentionally unsupported". | **Fixed** — the stale bullet now lists sound/sqldb/tui as Rust backends, and the boundary paragraph lists only `tkinter`/`tkinterPlus`/`turtle` as unported. |
| 9 | `clynxer/docs/limitations.md:49` | "At most four arguments per native signature" contradicted the packed section, where a call carries at most 64 arguments. | **Fixed** — restated as "at most four arguments" for fixed shapes, "at most 64 arguments in total" for packed. |
| 10 | `clynxer/docs/native-module-abi.md:124-134` | Presented the packed `...` form as **optional**. For a Rust `cdylib` using `clynxer_abi` it is mandatory, and using a fixed shape compiles cleanly and segfaults at call time. **This gap directly caused Finding B.** | **Fixed** — a callout now states the requirement and the failure mode, the "Adding a stdlib module" recipe in `docs/README.md` repeats it, and `limitations.md` points at it. |
| 11 | `clynxer/docs/native-module-abi.md:124-150` | The packed section did not document that arguments are indexed **per type**, and wrongly implied "at most 64 of each" (the real limit is 64 arguments in total, `kMaxPackedArgs`). Without this, Finding F is easy to reproduce. | **Fixed** — added a per-type indexing subsection with a worked `save(handle, path, quality)` example and a warning that out-of-range reads yield `0`/`""` silently. |

**Files touched by the documentation pass:** `clynxer/docs/README.md`,
`clynxer/docs/limitations.md`, `clynxer/docs/native-module-abi.md`,
`clynxer/README.md`, `todo.md`.

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

### 10.1 Known parity bugs (`todo.md:269-276`) — all resolved (2026-09-23)

- **test25** — **fixed.** A double `memoryFree()` now raises
  `address refers to freed memory` with a source location.
- **test26** — **fixed.** Reading an invalid address now raises
  `invalid native memory address` with a source location.
- **test22** — **fixed.** The crash is gone, and the typed 8-byte write path no
  longer coerces through a `double`, so `memoryWriteInt64` /
  `memoryWriteEndian(..., "int64", ...)` round-trip INT64_MAX and any value above
  2^53 a double cannot represent (`clynxer/builtins.cpp`, `signedMemoryPayload` /
  `unsignedMemoryPayload`). Direction confirmed as *lynxer → clynxer*: the
  reference (`lynxer/builtins.py:83`) range-checks and stores the exact integer.

The first two were the same class of problem this session found in `sound`: a
process-level crash where a sentinel or located error is expected, fixed with the
allocation registry in `builtins.cpp` (`validateMemory`). Regression coverage is
`examples/lowlevel_memory.lynx`, which asserts the round-trips and the three
memory error messages.

### 10.2 Open roadmap items

- Whether `tkinter` / `tkinterPlus` / `turtle` get Rust backends — `todo.md:196-199`
  now says no, with a `graphics.lynx` module on Rust `iced` planned instead.
- Freeze each module's operation names, signatures, handle ownership, string
  lifetime, error sentinels, callbacks, interruption behaviour and cleanup before
  a second backend is introduced (`todo.md:204-208`). **Findings B and E are both
  direct evidence for why this matters** — one is a signature contract violated
  three ways, the other a wrapper/backend contract violated once. Had the
  contract been frozen and written down per module, neither could have survived
  a single review.
- Rust-backend failure-path fixtures: success, malformed input, invalid handles,
  missing files, timeouts, cleanup, optional-dependency failures, and the
  compiled/bundled path (`todo.md:209-212`). `sqldb` now has real success-path
  and error-path coverage; `sound` and `tui` still only exercise invalid handles
  and would not notice a regression in their main code paths.
- Managed filesystem / process / networking / async / sound / FFI / native-thread
  APIs (Milestone 7, `todo.md:226-227`).
- ~~An optimization pass beyond constant folding (Milestone 8).~~ **Done** —
  `clynxer/optimizer.cpp`.
- ~~Lexer/parser/runtime comparison against Python, golden output tests, and
  running the full suite every milestone (Milestone 9).~~ **Done** — see the
  Revision 10 note and `clynxer/docs/parity.md`.

### 10.3 Known limitations intentionally kept

`venv` is deliberately absent; `re`/`regex` run on `std::regex` (ECMAScript
grammar) and report lookbehind, atomic groups and `\p{...}` as errors;
`cli`'s Click/Typer builders are hard "unknown function" errors; `js` requires
`node` and applies no timeout. The `tui` rendering/prompt/handle families are
placeholders. Full list: `clynxer/docs/limitations.md`, which now documents the
`sound`, `sqldb` and `tui` divergences as well.

---

## 11. Recommendations

1. ~~Update the documentation before writing more modules.~~ **Done** — §9 lists
   the eleven items and their fixes. The highest-value change was documenting the
   packed-signature requirement and the per-type argument indexing in
   `native-module-abi.md`.
2. ~~Fix Finding F (the `tui` argument indices).~~ **Done** — all 18 sites
   corrected, and `confirm` given its own registration. The automated check
   guarantees it stays fixed.
3. **Make the contract explicit per module before adding a second backend.** The
   freeze item at `todo.md:204-208` is the one that would have prevented both B
   and E. For a new module, write down — for each op — the Lynxer-facing name,
   the argument types *and order*, which argument is the handle/path, the error
   sentinel, and who owns cleanup. The wrapper already encodes most of this, and
   §8.9's check now enforces the mechanical half of it; the semantic half (which
   argument *means* what) still needs human agreement when a second backend
   appears.
4. ~~Add a "does the module satisfy its own wrapper?" check to the suite.~~
   **Done** — `clynxer/scripts/check_module_contracts.py`, wired into the first
   line of `clynxer/Makefile`'s `test` recipe. It found an 18th `tui` defect that
   manual inspection had missed, on its first run.
5. **Add failure-path fixtures for `sound` and `tui`** (`todo.md:209-212`). A
   playback test would have caught Finding C on day one; `sqldb`'s rewritten
   fixture shows what that coverage looks like. `sound` and `tui` still only
   exercise invalid handles and would not notice a regression in their main code
   paths.
6. **Replace the `tui.styleValid` stub** with real style validation, or record in
   `.expected` that the "not-a-style" case is a known stub. As it stands the
   fixture asserts a value that is true only because nothing is implemented.
7. **Consider a build-time guard against Finding B.** The interpreter already
   knows which shapes are packed, so a registration-time check could reject a
   fixed-shape signature — turning a SIGSEGV into a located error. Same
   "prefer explicit errors over crashes" principle as the parity bugs. (The
   static check covers this for the bundled modules; a runtime guard would also
   cover third-party `.so` files, which the script cannot see.)
8. **Attack the three crash-vs-error parity bugs (§10.1) as one unit.** They are
   mechanically similar and all three are the only remaining places where a user
   action can abort the process.
9. **Extend the contract check if the placeholder families get implemented.** The
   bounds rule has one blind spot: a shifted but in-bounds read is invisible
   unless the op also reads past the end (§8.9). Before `tui`'s table/tree/layout
   ops become real, either tighten the rule to "every argument is read exactly
   once" for the ops that opt in, or review those 18 sites by hand.

---

## 12. Milestone 6 closed (2026-09-20 20:26 CEST)

Milestone 6 had five open items. All five are done, plus four further defects
that closing them surfaced.

### 12.1 The five items

| Item (`todo.md`) | Delivered |
| --- | --- |
| The `tkinter`/`tkinterPlus`/`turtle` decision | Recorded as decided: not ported. `tkinter` is replaced by a planned `graphics` module on Rust `iced`; `turtle` is dropped. Reflected in `limitations.md` and `todo.md`. |
| Freeze the per-module contract | **`clynxer/docs/stdlib-contracts.md`** — the frozen contract at CLynxer 0.1.8. States the conventions that apply to all modules (operation naming, argument order and types, handles, string lifetime, error sentinels, callbacks, interruption, cleanup) and a per-module table of the dimensions that vary: identity model and cleanup owner for all 27 modules. Also defines what changing a contract requires. |
| Rust-backend failure-path fixtures | `sound`, `image`, `lua` and `tui` fixtures rewritten; `sqldb` and `json` already covered. All hermetic Rust fixtures added to the compiled/bundled parity loop. |
| ABI extension policy | `native-module-abi.md` § "Extending the ABI" — the five conventions a module must try first, and the four requirements for any additive change (C example, Rust example, compatibility coverage, documentation). No shape has been added since the packed form. |
| `extending.md` | **`clynxer/docs/extending.md`** — choosing C++ or Rust, the wrapper/backend pair, the packed ABI and per-kind indexing, build wiring, what a fixture must cover, the contract check, and a completion checklist. |

### 12.2 Four further defects found while closing it

**a. `evalLua` leaked a Rust source path into user output.**
`eval_source` called `lua.load(...)` without `set_name`, so mlua named the chunk
after the call site and a syntax error read
`Error: syntax error: lua/src/lib.rs:51:1: unexpected symbol near ')'` — an
implementation detail in a user-facing message, and one that would shift
whenever the file was edited. `run_source` already passed `clynxer.lua`; `eval`
now does too.

**b. `image.fromBase64` forgot the image format.**
It stored `None` as the format, so `getFormat` returned `""` and `info` reported
`"format":""` for an image whose bytes plainly say `PNG`. The comparison against
the Python reference — where Pillow reports `PNG` — is what exposed it. Now uses
`image::guess_format` on the decoded bytes. This changed one assertion in
`examples/stdlibTestAll.lynx`, which the consolidated test caught immediately.

**c. `tui.clear()`'s output could be emitted out of order.**
The `tui` backend writes its fallback text straight to stdout rather than
through the interpreter, so its output interleaves with the interpreter's only
because both are line-buffered. `clear()` writes its ANSI sequence with **no**
trailing newline, so it sat in Rust's buffer and appeared *after* whatever the
interpreter printed next — `setWidth(80)` came out before the clear sequence
that preceded it. It now flushes explicitly. Verified stable across three
consecutive runs.

**d. The compiled/interpreted parity loop did not set the headless flag.**
Adding `stdlib_game` to it hung the suite: the loop ran without
`CLYNXER_GAME_HEADLESS=1`, so `game` tried to open a window. The fixture loop
below it already set the variable; the parity loop did not. Both commands in the
loop now set it.

### 12.3 Fixture coverage added

| Fixture | Before | After |
| --- | --- | --- |
| `sound` | 2 invalid-handle calls | Missing file, a real committed asset (`examples/assets/tone.wav`, 0.25 s, 44.1 kHz PCM), duration, handle bookkeeping, all four invalid-handle playback ops, volume clamping, playback-then-stop, double release, count after cleanup |
| `image` | Success path only | Plus missing file, a file that is not an image, malformed base64, and six invalid-handle reads |
| `lua` | Success path only | Plus two syntax errors, a missing script file, and a runtime error |
| `tui` | 6 calls | 60 calls: the full fallback path, all configuration ops, every stateful placeholder family, and the prompt variants |
| `sqldb` | (rewritten earlier) | Real round-trip, error path, cleanup |

`enter()`/`exit()` are deliberately excluded from the `tui` fixture: they put the
controlling terminal into raw mode, which a fixture must not do. That is stated
in the fixture.

**Every fixture stays runnable from any directory.** The first draft of the
`sound` and `image` fixtures referenced `examples/assets/...`, which works under
`make test` (cwd is `clynxer/`) but breaks a by-hand run from the repository
root — a rule this project had already learned once and recorded. Both were
reworked: absent files now use scratch names under the cwd, the "not an image"
file is created and deleted with `fileIO`, and the one genuine binary asset is
resolved by probing both layouts so the output is identical either way. All Rust
backend fixtures were then verified from both directories.

### 12.4 Reference comparison

The item asked for the wrapper's behaviour to be compared with the Python
reference where the API is intended to remain compatible. That was done by
running each fixture through `venv/bin/python lynxer/shell.py` from the same
working directory and diffing:

| Module | Result |
| --- | --- |
| `sound` | **Byte-identical**, including the duration and the cleanup counts |
| `sqldb` | **Byte-identical** |
| `lua` | Diverges: `luaExists()` returns `bool` vs the reference's `1`; `luaVersion()` reports the vendored 5.4 vs the system's; error strings differ in prefix and chunk name |
| `image` | Diverges: pixel getters return a bracketed list vs a comma-joined string; mutating ops return `bool` vs `0`; `grayscale` keeps alpha (`LA` vs `L`); `info` emits compact JSON |

All four divergences are documented in `limitations.md` under `image` and `lua`.
Two of them — the pixel formatting and the `bool` returns — are pre-existing
shipped API choices, not defects, so they are documented rather than changed.

This is also where a methodology point landed: the Python comparison initially
failed for `sound` because the fixture was run from the repository root while
the fixture's relative asset path assumes `clynxer/`. Re-run from the matching
directory it was byte-identical. Cross-implementation diffs need a matching
working directory — the same hazard already noted for the `test/` parity work.

### 12.5 Verification

| Check | Result |
| --- | --- |
| `make buildCLynxer` | passes |
| `make testCLynxer` | passes |
| Contract check | 24 backends, 686 op calls, 0 skipped, 0 errors, 0 warnings |
| Stdlib fixtures | all 27 pass, byte-diffed |
| Compiled/bundled parity | 12 fixtures now, including `game`, `image`, `lua`, `sound`, `sqldb`, `tui` |
| Consolidated `stdlibTestAll` | passes (one assertion updated for the corrected `image.info`) |
| Determinism | the `tui` fixture is byte-identical across three consecutive runs |

### 12.6 What Milestone 6 does not claim

- `tui`'s rendering, prompt and stateful families are still placeholders; the
  contract is frozen, the implementation is not finished. That is recorded in
  `limitations.md` and is a Milestone 6 *contract* statement, not a completion
  claim.
- The contract check covers names and argument bounds. The semantic half — which
  argument means what — is documented in `stdlib-contracts.md` for a human
  reviewer; it cannot be checked mechanically.
- `sound`'s playback itself is device-dependent and therefore not asserted by
  the fixture. It was verified by hand against a generated WAV (§8.10).

---

## 13. Milestone 7 — first family ported (2026-09-20 21:05 CEST)

### 13.1 What the item actually covers

`todo.md`'s one open Milestone 7 item names seven API families. Measured against
the Python reference, that is **69 built-ins and ~1,110 lines of Python**:

| Family | Built-ins | Reference lines |
| --- | --- | --- |
| `async*` | 15 | 370 |
| `networking*` | 13 | 177 |
| `process*` | 8 | 171 |
| `filesystem*` | 12 | 149 |
| `sound*` | 9 | 119 |
| `ffi*` | 6 | 82 |
| `nativeThread*` | 6 | 46 |

All 69 are already *registered* in Clynxer: `builtins.cpp` has a
`handlerTable()` of implemented built-ins and an `unsupportedTable()` whose
entries fail with `<name>() is not supported in CLynxer yet`. Porting a family
means implementing it and moving its names from the second table to the first.
The direction is therefore unambiguous — no design decision was needed about
built-ins versus stdlib modules, even though Clynxer already provides
filesystem/process/networking/sound functionality through modules.

### 13.2 Method

The project's own rule (`todo.md`, "Rebuild rules") is to build one small
vertical slice at a time with a fixture and a passing `make test`. That also
matches what this session has learned the hard way: the three stdlib modules
written in one pass were broken in four ways. So the families are being ported
one at a time, each verified against the Python reference by running the same
program through both implementations.

### 13.3 `filesystem*` — done

12 built-ins: `Open`, `Read`, `Write`, `Close`, `Stat`, `List`, `Mkdir`,
`Remove`, `Rename`, `Link`, `ReadLink`, `Chmod`. 408 lines added to
`builtins.cpp`, guarded by a `CLYNXER_POSIX_BUILTINS` macro so a host without
POSIX `open`/`stat`/`dirent` keeps them in the unsupported set.

- Handles are non-negative integers in a registry (`openFiles()`); unknown and
  already-closed handles are runtime errors, not silent failures.
- Every failure preserves the operation and errno:
  `filesystemOpen() failed: [2] No such file or directory`.
- `filesystemStat` uses `lstat`, so a symlink reports `type: "symlink"` rather
  than being followed.
- `filesystemRead` replaces bytes that are not valid UTF-8 with U+FFFD, matching
  the reference's `errors="replace"`.
- **Output is byte-identical to the Python reference**, including all eleven
  error strings, over a program that exercises every function plus twelve
  failure paths.

One finding worth recording: the reference's `Number.null` is `Number(0)`
(`lynxer/values.py:535`), so the value-less operations (`Close`, `Remove`,
`Rename`, `Link`, `Chmod`) must return the integer `0`, not Clynxer's `none`.
Returning `none` produced the only difference in the first comparison run.

Fixture: `examples/builtin_filesystem.lynx` + `.expected`, cwd-independent, run
by a new `builtin_*` fixture glob in `clynxer/Makefile` that diffs against a
sibling `.expected` exactly like the `stdlib_*` loop.

### 13.4 Remaining families need decisions, not effort

`sound*` (9), `nativeThread*` (6), `ffi*` (6) and `async*` (15) were the four not
yet ported as of revision 7. `sound*` was then resolved by bridging it to the
Rust module (§13.6). The other three are still blocked, because none is a
straight POSIX port:

| Family | The question |
| --- | --- |
| `nativeThread*` | Needs a thread registry and a way for a spawned thread to call back into Lynxer. The reference relies on CPython's GIL — `lynxer/cpp.cpp` calls `PyGILState_Ensure` then invokes the function object. Clynxer has **no interpreter lock**: `Environment` (`runtime.hpp:180-191`) is unsynchronised state and the evaluator is a tree-walking interpreter with no re-entrancy, so running a Lynxer function on a second thread today would be a data race. |
| `ffi*` | Needs a calling-convention layer. `libffi` is a new build dependency for the interpreter; hand-rolling covers only a few signatures. Largest security surface of the three. |
| `async*` | ~370 reference lines and an event loop, for a language Clynxer does not currently run asynchronously. Worth deciding whether the built-ins should exist at all before building the machinery. |

### 13.5 Families ported so far

| Family | Built-ins | Result |
| --- | --- | --- |
| `filesystem*` | 12 | **Byte-identical** to the reference, all 11 error strings included |
| `process*` | 8 | **Byte-identical**, including the CLOEXEC exec-error report and 15 error paths |
| `networking*` | 13 | **Byte-identical** across TCP, UDP, Unix sockets, resolution and 10 error paths |
| `sound*` | 9 | Bridged to the Rust `sound` module — validation identical, three documented divergences |

42 of 69 built-ins. Each has a fixture under `examples/builtin_*.lynx`, run by a
`builtin_*` glob in `clynxer/Makefile` that diffs against a sibling `.expected`,
and each was compared against the Python reference by running the same program
through both. All four fixtures are cwd-independent and deterministic across
repeated runs.

### 13.6 `sound*` — the bridge

The reference implements `sound*` on **Arcade**. Clynxer instead bridges the
built-ins to the Rust `sound` stdlib module it already ships, so there is one
audio implementation and the interpreter binary keeps no audio dependency.

- `callBridgedModule(module, operation, args, line, column)` in `ast.cpp`
  (declared in `ast.hpp`) loads `sound.so` on first use through the same
  `resolveModulePath` + `dlopen` + `lynxer_module_init_v1` path an `import`
  uses, caches the registrations, and dispatches through `callNative`. It
  reuses `resolveModulePath`, so the module resolves from the cwd, `stdlib/`,
  `clynxer/stdlib/`, or an embedded library in a compiled executable.
- The nine built-ins add the reference's validation and their **own** handle
  registry on top, mirroring how the reference keeps `_SOUNDS` separate from the
  `sound` module's list. A handle is valid only if `soundLoad` returned it.
- One consequence is documented and verified: a compiled executable carries
  `stdlib/sound.so` only if the program also has `import("sound")`, because
  bundling follows imports. Run from a directory with no stdlib, the built-in
  fails with `cannot load the bundled 'sound.so' backend that 'load' needs` —
  confirmed by building the fixture and running it from `/tmp`.

Three deliberate divergences, all in `limitations.md`:

1. the backend's failure text is the module's, not Arcade's exception text;
2. `soundPause`/`soundResume` **work** — the reference fails by design ("audio
   backend does not support portable pause/resume") because Arcade has no
   portable pause, while rodio does;
3. `soundStop` works — on the installed Arcade the reference fails with
   `'Player' object has no attribute 'stop'`, an environment artefact rather
   than a decision.

Everything the built-in layer controls is identical to the reference: missing
file, unsupported format, wrong argument type, out-of-range volume and
invalid-handle messages all match, which the side-by-side comparison confirms —
the only diff lines are the two pause/resume results and the reference's
failure to stop.

### 13.7 Remaining

`nativeThread*` (6), `ffi*` (6) and `async*` (15) — 27 built-ins. Each is
blocked on a decision rather than on effort; the specific reasons are in
`clynxer/docs/limitations.md` under "Built-in families that are not ported" and
summarised in §13.4 above.

**`nativeThread*` turned out to need two prerequisites, not one** (established in
revision 9 below), so it is not the easy next step it looked like.

### 13.8 `nativeThread*` needs a missing language feature (revision 9)

Investigating it to start work turned up a second blocker, verified by running
the same program on both implementations:

```lynx
global worker(int value){ println(value); }
global main(){ println(returnType(global.worker)); }
```

| | Result |
| --- | --- |
| Python reference | `function`, and printing the value gives `<function worker>` |
| Clynxer | `clynxer: /tmp/fn.lynx:4:24: unknown variable 'worker'` |

So `nativeThreadStart(global.worker, [int 42])` cannot be written in Clynxer at
all: **named global functions are not first-class values**. Clynxer's value model
knows `codeblock` only (`types.cpp`), and codeblocks come from literals
(`codeblock saved = { ... }`) or inline arguments. The other four families needed
native code; this one needs a **language feature** first.

The second prerequisite stands as previously recorded: the thread runs a Lynxer
function, so a second thread must enter the evaluator, and Clynxer has no
interpreter lock — the reference relies on CPython's GIL
(`lynxer/cpp.cpp` → `PyGILState_Ensure`), while `Environment` is unsynchronised
state.

A safe design exists: one interpreter lock held by whichever thread is
evaluating, released while a thread blocks in `nativeThreadJoin`, so no two
threads ever evaluate concurrently (cooperative threads). That is an interpreter
change and is only worth starting once function values exist.

**Not started.** Doing it now would mean adding a language feature, an
interpreter lock and six built-ins in one pass — exactly the pattern that
produced the four defects in the three stdlib modules earlier in this session.
Recorded in `limitations.md` and `todo.md` instead.

Two things worth carrying forward:

- **`Number.null` is `0`.** Every value-less operation in these families returns
  the integer `0`, not Clynxer's `none`. Getting this wrong was the only
  difference in the first `filesystem*` comparison.
- **`substring` is a Clynxer extension.** The Python reference has no such
  built-in (it reports `'substring' is not defined`), so a fixture meant to be
  diffed against the reference must avoid it — the networking fixture parses an
  ephemeral port with `splitStr`/`listInt` instead. The reference also rejects
  `-> int` on a `global` function, which Clynxer accepts.

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

**Modules fixed and documented**

| Reference | What it is |
| --- | --- |
| `clynxer/rust/sound/src/lib.rs` | `SoundEntry { path, volume, sink }`, `SoundState { entries, output }`, `start_playback()`, all 12 ops, `OPS` table |
| `clynxer/rust/sqldb/src/lib.rs` | Path-based rewrite: `with_conn()`, `query_rows()`, `list_tables()`, `json_dumps()`, `value_to_scalar()`, `parse_params_json()`, `value_to_json()`, `OPS` table (10 ops, `open` removed) |
| `clynxer/rust/tui/src/lib.rs` | `tui_enter`, `println!("{}", "─".repeat(40))`, `OPS` table (70 ops). All 18 positional-index reads corrected; `confirm` split from `confirmDefault`; `tui_clear` flushes stdout |
| `clynxer/rust/lua/src/lib.rs` | `eval_source` sets a `clynxer.lua` chunk name |
| `clynxer/rust/image/src/lib.rs` | `image_from_base64` records the guessed format |
| `clynxer/scripts/check_module_contracts.py` | **New.** Static wrapper/backend contract check, run by `make test`; see §8.9 |
| `clynxer/Makefile` (`PYTHON`, `CONTRACT_CHECK`, `test` recipe, parity loop) | Wires the check in and extends the compiled-parity loop |
| `clynxer/docs/stdlib-contracts.md` | **New.** Frozen per-module contracts (§12) |
| `clynxer/docs/extending.md` | **New.** Module authoring guide (§12) |
| `clynxer/examples/assets/tone.wav` | **New.** 0.25 s 44.1 kHz PCM asset for the `sound` fixture |
| `clynxer/examples/stdlib_sqldb.lynx`, `.expected` | Rewritten to a real round-trip against `.clynxer_scratch_sqldb.db`; byte-identical to the Python reference |
| `clynxer/examples/stdlib_tui.lynx`, `.expected` | Corrected `tuiExists`/`tuiVersion` calls and `true`/`true` |
| `clynxer/stdlib/{sound,sqldb,tui}.lynx` | The Lynxer-facing wrappers (unchanged — they were the correct side of Finding E) |

**Contract and reference**

| Reference | What it is |
| --- | --- |
| `clynxer/docs/native-module-abi.md` | The ABI contract; `:82-115` fixed shapes, `:124-160` packed + the per-type indexing rule |
| `clynxer/docs/limitations.md:24-33` | No bytecode backend; `--compile` embeds modules |
| `clynxer/docs/limitations.md:61-72` | "Modules that are not ported" — now accurate |
| `clynxer/docs/limitations.md` (`sound`, `sqldb`, `tui` sections) | Per-module divergences, added this revision |
| `lynxer/stdlib/sound.lynx` | The Python reference semantics for `sound` |
| `lynxer/stdlib/sqldb.lynx` | The Python reference semantics for `sqldb` — path-based, connect/close per call |
| `lynxer/stdlib/tui.lynx:9,22` | `tuiExists` / `tuiVersion` — the canonical names |
| `clynxer/stdlib/tui.lynx:81` | `styleValid(...) -> bool` |
| `clynxer/rust/image/src/lib.rs:209-218` | The correct per-type indexing convention (`args.int(0)` + `args.string(0)`) |
| `clynxer/rust/json/src/lib.rs:21-50` | The `compact()` helper `sqldb`'s `json_dumps()` mirrors |
| `clynxer/ast.cpp` (`kMaxPackedArgs`) | The 64-argument total limit for packed calls |

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
| `clynxer/docs/README.md` (module table) | All 27 modules, sound/sqldb/tui included |
| `clynxer/docs/README.md` ("Adding a stdlib module") | 4-step recipe, including the packed-signature requirement |
| `README.md:6-14` | What Lynxer is; Linux-only, amd64/aarch64 |
| `clynxer/README.md:1-9` | What Clynxer is |

---

**End of Report**
