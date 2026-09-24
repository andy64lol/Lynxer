# Lynxer rebuild from scratch

Lynxer is the standalone C++ implementation being rebuilt beside the original
Python Lynxer. The Python implementation is the behavior reference; it is not a
runtime dependency and its internals are not copied into Lynxer.

## Next up — compiler pivot and stdlib consolidation

Planned order of work, newest direction first.

- [x] Migrate every implemented third-party backend to Rust behind the same C ABI.
  `json` is now `serde_json` (+`preserve_order`, byte-identical output),
  `network` is `ureq` (rustls) + `tungstenite`, and `server` is `axum` + `tokio`;
  `image` uses the Rust `image` crate and `lua` uses vendored Lua through
  `mlua` — removing nlohmann/json, cpp-httplib, Crow, Boost and the system
  OpenSSL dependency. All six are self-contained `cdylib`s under `rust/` that
  export `clynxer_module_init_v1` and their ops directly (no C++ shim, no
  `--whole-archive`); `rust/abi` (`lynxer_abi`) holds the shared FFI helpers.
  CMake, `cmake/FetchDeps.cmake`, `third_party/` and the `make cmake` /
  `lynxerDeps` / `cmake-modules` targets are gone. `stdlib/network.cpp`,
  `stdlib/server.cpp` and `stdlib/json.cpp` are deleted. Still C++ and not
  third-party: `native_json.hpp`, `native_regex.hpp`. The optional `image`
  (`image` crate), `lua` (vendored `mlua`), `sound` (`rodio`/`cpal`), `sqldb`
  (`rusqlite`) and `tui` (`ratatui`/`crossterm`) backends now use the same Rust
  `cdylib` + C ABI path. Docs:
  `docs/install.md`, `docs/README.md`,
  `docs/stdlib/{json,network,server,sound,sqldb,tui}.md`.
- [x] Add the `game` stdlib module: `stdlib/game.lynx` wraps `stdlib/game.so`,
  a Rust + macroquad backend (`rust/game`, a `cdylib` exporting
  `clynxer_module_init_v1`/`clynxer_module_attach_v1` directly) exposed through
  the native-module C ABI. An example clicker lives in
  `examples/game_clicker.lynx`. Covers the window, draw loop with `setUpdateCallback` /
  `setDrawCallback`, shapes, text, input, sprites, sprite lists, textures,
  camera and grid helpers. Two additive native-module ABI extensions make this
  possible: the packed `cdecl:<ret>(...)` signature, and the optional
  `clynxer_module_attach_v1` host API that lets a module call a Lynxer function
  by name. `LYNXER_GAME_HEADLESS=1` runs the module without a display; the
  `examples/stdlib_game.lynx` fixture exercises it in `make test`. Deferred:
  sound, scenes, tilemaps, physics, shape batches, animated sprites. Docs:
  `docs/stdlib/game.md`, `docs/native-module-abi.md`.
- [x] Merge `mathPlus` into `math`: the statistics/vector helpers (`median`,
  `std`, `variance`, `percentile`, `corrcoef`, `dot`, `linspace`, `cumsum`,
  `diff`, `clip`, `normalize`) now live in `stdlib/math.cpp` + `math.lynx`, and
  `maxInt()`/`minInt()` are exposed on the `math` namespace. `stdlib/mathPlus.*`,
  its fixture and its docs page are gone; coverage moved to
  `examples/stdlib_math.lynx`.
- [x] Add a GitHub Actions workflow that builds Lynxer and runs
  `make testLynxer` on push and pull request
  (`.github/workflows/build-lynxer-amd.yml` and `build-lynxer-arm.yml`). The
  CI run passes `LYNXER_SKIP_DISPLAY=1` so the tests that need a display or
  audio device are skipped on a runner.
- [x] Remove the Lynxer bytecode stack: `bytecode.*`, `vm.*`, `compiler.*`, the
  `CLYXC` container, `.lynxc` execution, `--view-bytecode`,
  `--benchmark-compile`, `--no-cache`, `--no-opt`, and the per-AST-node
  `compile(ProgramEmitter&)` methods are gone. `--compile` produces an ELF
  executable (the former `--bundle`); `--bundle` remains an alias, and a
  `.lynxc` argument now reports that bytecode is no longer supported.
- [x] Make compiled executables support module imports: the payload carries the
  program source, every transitively imported `.lynx` module source, and every
  imported native `.so`. Native libraries are materialized into a temporary
  directory at startup for `dlopen` and removed at exit, so the executable is
  self-contained and needs nothing from the build tree. Imports, stdlibs and
  every language feature behave exactly as in an interpreted run — including the
  features the old bytecode compiler rejected (field access, methods, codeblocks,
  switch patterns, local functions, imports).
- [x] Let `--compile` bundle more than one input file into a single executable:
  the first `.lynx` file is the program, and further `.lynx`/`.so` inputs are
  embedded and resolvable by `import` even when they live outside the module
  search path. `-o`/`--name` sets the output, and a trailing bare argument still
  works as the output name. Covered by the `bundle_app`/`bundle_extras` check in
  `make test`.
- [x] Add `--include <file>` for including further files in a compiled
  executable. Modules the program imports are still collected automatically;
  `--include` additionally embeds modules, native libraries, and data files of
  any other kind. Data files are materialized into the executable's private
  temporary directory at startup and exposed through the new `bundledFile(name)`
  and `bundledFiles()` builtins. Covered by the `bundle_assets` check in
  `make test`.

## Rebuild rules

- Keep Lynxer under `lynxer/`, separate from `clynxer/`.
- Use C++17 and the standard library only unless a future milestone explicitly
  adds a native dependency.
- Build one small vertical slice at a time: lexer, parser, runtime, CLI,
  fixture, and a passing `make test`.
- Prefer explicit source-located errors over partial support or Python fallback.
- Add a focused `.lynx` fixture for every user-visible language feature.
- Keep the original Lynxer runnable so behavior can be compared during the
  rewrite.

## Milestone 0 — small executable

- [x] Create a standalone C++17 executable and Makefile.
- [x] Read and run a `.lynx` source file.
- [x] Add `--help` and `--version`.
- [x] Add a source-located error path.

## Milestone 1 — language core

- [x] Lex comments, identifiers, numbers, strings, operators, and punctuation.
- [x] Support `/// ... ///` multiline comments as a delimiter without
  consuming comment-like text inside strings.
- [x] Parse `global setup(){}` and `global main(){}`.
- [x] Enforce both required global entry points.
- [x] Implement typed variables, assignment, and shared setup/main state.
- [x] Implement literals, arithmetic, comparisons, equality, and booleans.
- [x] Implement `print(...)` and `println(...)`.

## Milestone 2 — control flow

- [x] Implement `if` / `else`.
- [x] Implement `while`.
- [x] Implement `for`, including the implicit `i = i + 1` update.
- [x] Implement `doWhile`, including the no-condition form.
- [x] Implement `iterate(count)`.
- [x] Implement `forever()`.
- [x] Implement `break`, `continue`, and `restart`.
- [x] Test nested loops and loop-control behavior with bounded fixtures.

## Milestone 3 — runtime model

- [x] Replace the initial value variant with an extended value model:
  list, tuple, sentinel, and object values with identity comparison, plus
  Python-style number formatting.
- [x] Add list and tuple literal syntax and the list/tuple built-in families
  with Lynxer value semantics (new-list results, negative indices,
  string-based membership).
- [x] Add `inter"..."` interpolation in `print`, `println`, `input`, and
  `inputln` arguments, including `\{`, `\}`, and `\\` escapes and the
  empty/missing/stray-brace syntax errors.
- [x] Add the remaining documented scalar types: `num`, `char`, `numBool`,
  `bit`, `byte`, `int8`..`uint64`, `float32`/`float64`, and the `codeblock`
  type.
- [x] Add typed element literals for sequences: `[int 1, int 2]` and
  `(int 10, int 20)`.
- [x] Add lexical scopes and match Lynxer declaration lifetime rules.
- [x] Add `const` declarations whose reassignment is a runtime error.
- [x] Add structs, classes, enums, vargroups, and pattern matching.

## Milestone 4 — operators, statements, and errors

- [x] Implement bitwise operators `&`, `|`, `^`, `!&`, `!^`, `!|`, `~`, `<<`,
  `>>`, exponent `**`, and integer division `/%`.
- [x] Implement the NAND/NOR logic forms `!&&` and `!||`.
- [x] Implement the word operators `and`, `or`, and `not`.
- [x] Accept the legacy equality forms `is` and `not is` with deprecation
  warnings.
- [x] Implement `switch` / `case` / `default` and `elif`.
- [x] Support the `\e` string escape.
- [x] Implement `try` / `catch` with source-located error values.

## Milestone 5 — functions and code blocks

- [x] Support file-wide `func` declarations and named `global` functions
  beyond `setup`/`main`.
- [x] Add typed and default parameters and return values.
- [x] Add local functions.
- [x] Add caller-supplied code blocks and multiple code blocks.
- [x] Add named `codeblock` values and `exec(){{name}}`.
- [x] Implement the `overrideMain` entry-point override.
- [x] Implement classes and class methods.

## Milestone 6 — module system and standard library

- [x] Implement `import` and `importAs` with nested/path-based imports.
- [x] Support calling module functions and accessing module globals.
- [x] Serve `--list-stdlibs` from Lynxer's own stdlib directory. The command
  resolves the directory next to the running executable, so it works from any
  current working directory, and lists only regular `.lynx` module files.
- [x] Port standard-library modules one small module at a time. Lynxer ships
  every feasible package-free module from `clynxer/stdlib/`; `mathPlus` is merged
  into `math`, `http`/`net` are replaced by `network`, and the unsupported
  modules are documented in `lynxer/docs/limitations.md`.
- [x] Keep optional `-> type` return annotations available while stdlibs use
  the shared registration ABI.
- [x] Implement the first Rust-backed third-party set: `game`, `image`, `json`,
  `lua`, `network`, and `server` as self-contained `cdylib` modules exporting
  the shared ABI directly. Their Rust implementations and Lynxer wrappers are
  complete; future work below is about portability and parity, not initial
  Rust ports.
- [x] Implement the remaining Python third-party stdlib alternatives as Rust
  `cdylib` backends behind the shared C ABI. The Lynxer-facing
  `stdlib/<name>.lynx` module is the wrapper; its `setup()` imports
  `stdlib/<name>.so`, and the Rust backend registers the native operations that
  the wrapper calls. Use the Python implementation and package behavior as the
  reference, not as a Lynxer runtime dependency:
  - [x] `sound`: replace the Python Arcade audio backend with Rust
    `rodio`/`cpal`, adding `symphonia` where decoding is needed. Preserve
    loading, streaming, play/loop/stop, pause/resume, volume, duration, and
    release handles.
  - [x] `sqldb`: replace Python `sqlite3` with Rust `rusqlite`/`libsqlite3-sys`.
    Preserve execute, scripts, parameterized queries, JSON row results, scalar
    values, last-insert IDs, table inspection, and cleanup.
  - [x] `tui`: replace Python Rich with Rust `ratatui`/`crossterm`. Preserve
    styled text, Markdown, panels, tables, prompts, progress, and recorded
    console output, with a headless rendering mode for fixtures.
- [x] Decide whether `tkinter`, `tkinterPlus`, and `turtle` belong in this Rust
  backend phase. **Decided: they do not.** `tkinter`/`tkinterPlus` are replaced
  by a planned `graphics` module on Rust `iced`; `turtle` is left behind (its
  Rust crate was last updated in 2019 and `turtle_rs` does not offer the same
  experience). Recorded in `lynxer/docs/limitations.md` under "Modules that are
  not ported".
- [x] For every new Rust backend, add the crate to the Rust workspace, export
  `clynxer_module_init_v1` through `lynxer_abi`, add the module to the Makefile,
  create the matching `stdlib/<name>.lynx` forwarding wrapper, document the
  API, and add a sibling expected-output fixture.
- [x] Freeze each module's operation names, signatures, handle ownership,
  string lifetime, error sentinels, callbacks, interruption behavior, and
  cleanup before introducing a second backend. Keep third-party calls behind
  backend-local adapters so the Lynxer wrapper never depends on crate-specific
  types or APIs. Frozen at Lynxer 0.1.8 in
  `lynxer/docs/stdlib-contracts.md`, per module: the conventions that apply
  everywhere, the identity model and cleanup owner for each of the 27 modules,
  and what changing a contract requires. The wrapper names only
  `global.native<Name>.<op>(...)`, so no crate or library type crosses it.
  The mechanical half — op names and packed argument bounds — is enforced by
  `lynxer/scripts/check_module_contracts.py` in `make test`.
- [x] Add Rust-backend fixtures for success, malformed input, invalid handles,
  missing files, timeouts, cleanup, optional-dependency failures, and the
  compiled/bundled executable path. Compare the wrapper's behavior with the
  Python reference where the API is intended to remain compatible.
  `sound` (missing file, real asset, invalid handles, volume, release, cleanup),
  `image` (missing file, non-image file, malformed base64, invalid handles),
  `lua` (syntax errors, missing script, runtime error) and `tui` (the full
  fallback path and the stateful placeholders) now cover their failure paths;
  `sqldb` and `json` already did. All 27 fixtures are diffed byte-for-byte in
  `make test`, and the hermetic Rust ones are additionally checked through
  `--compile` for compiled/interpreted parity. Three divergences surfaced by
  the comparison and are documented in `limitations.md`: `image` pixel and
  `info` formatting, `image.grayscale` keeping alpha, and `lua`'s
  `luaExists`/error-text shape. `sound` and `sqldb` produce output identical to
  the Python reference.
- [x] Only extend the ABI when a real module cannot be expressed with its
  scalar/string/handle conventions; every additive ABI change needs C and Rust
  examples, compatibility coverage, and documentation. Stated as policy in
  `lynxer/docs/native-module-abi.md` under "Extending the ABI". No shape has
  been added since the packed `cdecl:<ret>(...)` form; `tui`, `sound` and
  `sqldb` needed none.
- [x] Add a detailed extending.md for making modules for Lynxer.
  `lynxer/docs/extending.md` covers choosing C++ or Rust, the wrapper and
  backend pair, the packed ABI and per-kind argument indexing, build wiring,
  what a fixture must cover, the contract check, and the completion checklist.

## Milestone 7 — native APIs

- [x] Port built-ins with explicit unsupported-feature errors for everything
  not yet implemented (I/O, conversions, introspection, sequences, lists,
  tuples, `assert`, `sleep`, forever helpers).
- [x] Port the native-memory family: allocator, typed accessors, endian
  operations, `memoryTypeSize`/`memoryTypeAlignment`, and `sizeOf`.
- [x] Port the named Linux syscall wrappers through a host syscall table with
  errno surfaced as source-located errors.
- [x] Port the managed filesystem, process, networking, async, sound, FFI,
  and native-thread APIs. One family at a time, each with a fixture that is
  byte-diffed against the Python reference where the API is meant to be
  compatible. 69 built-ins in total; every name has been moved from
  `unsupportedTable()` to `handlerTable()` in `builtins.cpp`.
  - [x] `filesystem*` (12): `Open`, `Read`, `Write`, `Close`, `Stat`, `List`,
    `Mkdir`, `Remove`, `Rename`, `Link`, `ReadLink`, `Chmod`. POSIX
    `open`/`read`/`stat`/`dirent`, handles in a registry, errno preserved in
    every message. Output is byte-identical to the reference, including all
    eleven error strings, and covers the failure paths (bad mode, missing
    path, unknown and closed handle, non-empty directory). Fixture:
    `examples/builtin_filesystem.lynx`; the `builtin_*` fixture glob and its
    `.expected` check are new in the Makefile. Divergence worth remembering:
    the reference's `Number.null` is `0`, so the value-less operations return
    `0`, not Lynxer's `none`.
  - [x] `process*` (8): `Spawn`, `Write`, `CloseInput`, `Read`, `Poll`, `Wait`,
    `SendSignal`, `Close`. POSIX `fork`/`exec` with one pipe per standard
    stream, environment overrides, timeouts and signals; the child's exec
    failure is reported through a CLOEXEC pipe so the message matches the
    reference (`processSpawn() failed: [Errno 2] No such file or directory:
    '<cmd>'`). Commands are never shell-parsed. A signal death reports the
    negative signal number. SIGPIPE is ignored process-wide so a write to a
    dead child reports EPIPE instead of killing the interpreter. Fixture:
    `examples/builtin_process.lynx`, byte-identical to the reference across
    success, stderr, exit-status, signal, timeout and fifteen error paths.
  - [x] `networking*` (13): `Open`, `Bind`, `Listen`, `Accept`, `Connect`,
    `Send`, `Receive`, `Close`, `Shutdown`, `Blocking`, `Option`, `Resolve`,
    `Address`. Managed TCP, UDP and Unix-domain sockets over POSIX
    `socket`/`bind`/`listen`/`accept`/`send`/`recv`; `Accept` allocates from
    the same handle registry as `Open`. Fixture:
    `examples/builtin_networking.lynx`, byte-identical to the reference across
    TCP, UDP, Unix sockets, resolution and ten error paths. It binds to an
    ephemeral port and reads it back from `networkingAddress`, so no fixed port
    can collide.
  - [x] `sound*` (9): `Load`, `Play`, `Loop`, `Stop`, `Pause`, `Resume`,
    `SetVolume`, `IsPlaying`, `Release`. Backed by the Rust `sound` stdlib
    module rather than a second audio stack: `callBridgedModule()` in
    `ast.cpp` loads `sound.so` on first use through the same `dlopen` +
    `clynxer_module_init_v1` path an import uses, and the built-ins add the
    reference's validation and their own handle registry on top. Three
    deliberate divergences, recorded in `lynxer/docs/limitations.md`: the
    backend's failure text is not Arcade's, `soundPause`/`soundResume` work
    (the reference fails by design), and `soundStop` works (the installed
    Arcade has no `Player.stop`). Fixture: `examples/builtin_sound.lynx`.
  - [x] `nativeThread*` (6): `Start`, `Join`, `JoinAll`, `IsAlive`, `Status`,
    `Detach`. Needed one language feature first: `global.<name>` now resolves to
    a callable value when no variable has that name, which is what
    `nativeThreadStart(global.worker, [int 42])` passes. Threads are cooperative
    — one interpreter lock, held while evaluating and released while a thread
    blocks in `Join`/`JoinAll` — so two threads never evaluate at once and no
    data race is possible; a thread runs while its starter waits. Program exit
    joins anything left running. Fixture: `examples/builtin_nativeThread.lynx`,
    deterministic across repeated runs. Divergences from the reference (which
    lets a worker interleave via the GIL, and reports `function` rather than
    `codeblock`) are in `lynxer/docs/limitations.md`.
  - [x] `ffi*` (6): `LoadLibrary`, `Lookup`, `CloseLibrary`, `Call`,
    `Callback`, `FreeCallback`. Uses POSIX `dlopen/dlsym/dlclose` (no libffi
    dependency); signature-based dispatch via the native call table in `ast.cpp`
    maps C calling conventions (`cdecl:ret(args...)`) to typed handlers.
    Callbacks are registered by handle and resolved through the Lynxer function
    registry. Fixture: `examples/builtin_ffi.lynx`.
  - [x] `async*` (15): `Run`, `Gather`, `Sleep`, the `Poll*` family, the
    `Timer*` pair and the `Wakeup*` trio. Real POSIX primitives: `poll(2)` for
    event multiplexing, `std::chrono::steady_clock` monotonic timers, pipe-backed
    wakeups. `asyncRun`/`asyncGather` wrap cooperative scheduling on the single
    interpreter thread; `await` evaluates the operation inline. Fixture:
    `examples/builtin_async.lynx`.

## Milestone 8 — compiler, bytecode, and CLI surface

> Superseded by the compiler pivot above: bytecode, the `CLYXC` container and
> `--view-bytecode` were removed, and `--compile` now produces an ELF executable
> that embeds source and re-parses it at startup. The checked items below record
> what was built before the pivot; the optimization item is the post-pivot
> replacement of the old bytecode optimizer.

- [x] CLI parity for run/help/version/lint/list-stdlibs/easter egg via
  `shell.cpp`, with the version and message templates in `lynxer.config`.
- [x] Fail explicitly for bundle, ast, format, benchmark, validate, install.
- [x] Define a stable Lynxer bytecode: opcode set, string table, constant
  pool, per-section trap tables, and eager load validation (jump targets,
  stack depths, table ranges) behind the `CLYXC` container (format v1,
  uncompressed; intentionally unrelated to the Python `.lynxc` container).
- [x] Add bytecode generation: AST-to-bytecode compiler with constant folding
  (`--no-opt` disables) and jump backpatching for break/continue/restart.
- [x] Add a native stack-machine VM executing `.lynxc` files with
  byte-identical output, exit codes, and source-located errors versus the
  interpreter path (fixture parity is enforced by `make test`).
- [x] Implement `--compile` (`-c`, `--no-cache`, `--no-opt`), direct `.lynxc`
  execution, and `--view-bytecode` disassembly.
- [x] Implement `--bundle <file.lynx> [name]`: compile to bytecode and append
  it to a copy of the lynxer executable (`CLYXPAYLD` trailer); a bundled
  executable detects its payload at startup and runs the embedded program
  directly, with the same output parity as `.lynxc` runs.
- [x] Merge the Lynxer and Lynxer Makefile entry points, including root
  `buildLynxer`, `testLynxer`, `cleanLynxer`, and combined build/test/clean
  targets.
- [x] Add the AST optimization pass (`lynxer/optimizer.{hpp,cpp}`): constant
  folding of literal-only expressions, short-circuit simplification of constant
  `and`/`or`, and dead-branch elimination for a constant `if`, `while (false)`
  and `iterate (0)`. It runs after parsing and before execution in interpreted
  and compiled runs and for imported modules, and builds on the shared
  `applyBinary`/`applyUnary` semantics. Anything that could raise, warn or
  coerce differently is left for the runtime, so output is byte-identical with
  and without the pass. `--no-opt` disables it; `LYNXER_OPT_REPORT=1` reports
  counts. Covered by `examples/optimizer.lynx` (plus
  `examples/optimizer_deprecated.lynx` for the warning path) and a `make test`
  gate that diffs the optimized and `--no-opt` runs.

## Milestone 9 — compatibility gates

Lynxer is the behaviour reference for its own surface, not a byte-for-byte
clone of the Python implementation, and it has deliberately diverged — no
bytecode, an ELF `--compile`, Rust stdlib backends, cooperative threads and
several re-implemented modules. These gates therefore compare only where parity
is intended and assert Lynxer's own behaviour everywhere else; the canonical
divergence register is `lynxer/docs/limitations.md`, and
`lynxer/docs/parity.md` summarises what is and is not a parity target.

- [x] Baseline comparison against the Python test fixtures: 15 of 55 pass
  (2026-09-13). The failures are now mostly surface Lynxer implements itself
  (functions, bitwise/word operators, `const`, typed element literals, module
  imports, native APIs) rather than genuinely missing features.
- [x] Do **not** compare lexer/token streams: the two implementations have
  different token models, so a token diff is noise. Each lexical divergence is
  now gated by a Lynxer fixture — `examples/lexical_bang.lynx`,
  `lexical_block_comment.lynx`, `lexical_hex_escape.lynx` and
  `lexical_unicode_escape.lynx` — asserted through the golden CLI cases, and the
  list lives in `lynxer/docs/limitations.md`.
- [x] Compare parser and runtime behaviour only where parity is meant to hold.
  `lynxer/docs/parity.md` records the parity allowlist (the language core and
  the stdlib APIs whose docs claim parity) and the divergence denylist
  (`image`/`lua` formatting, cooperative `nativeThread*`, working
  `soundPause`/`soundStop`, re-implemented `math` statistics, the `std::regex`
  grammar in `re`/`regex`, `json` non-finite numbers, and the rest of
  `limitations.md`).
- [x] Golden tests for Lynxer's **own** output and diagnostic text:
  `lynxer/scripts/check_golden.py` runs `lynxer/golden/cases.json` and pins
  the CLI surface (`--version`, removed/unsupported flags, file-not-found,
  `--lint` success and error) and the source-located error strings. Wired into
  `make testLynxer`.
- [x] Run the complete Lynxer test suite on every milestone. The GitHub Actions
  workflows (`.github/workflows/build-lynxer-amd.yml` and
  `.github/workflows/build-lynxer-arm.yml`) now run
  `make testLynxer LYNXER_SKIP_DISPLAY=1` on push and pull request.
  `LYNXER_SKIP_DISPLAY=1` drops the tests that need a display or an audio
  device (`stdlib_game`, `stdlib_sound`, `stdlibTestAll`, `game_clicker`),
  because a runner has neither and the graphics backend crashes without a
  display.
- [x] Low-level tests that run on both architectures:
  `examples/lowlevel_memory.lynx` (typed and endian native memory, the int64
  round-trip regression, and the three memory error messages) and
  `examples/lowlevel_syscalls.lynx` (the portable named syscalls and their
  raised-error path). Both are `.expected`-diffed and compiled in
  `make testLynxer`, so the amd64 and arm64 CI jobs both run them.
- [x] Architecture-dependent tests that need neither graphics nor sound:
  `examples/lowlevel_arch.lynx` (LP64 sizes, host endianness, and the named
  syscalls `yield`/`gettid`/`getpid`/`getppid`/`clock_gettime`/`clock_getres`/
  `pipe2`/`read`/`write`/`close`), plus `examples/lowlevel_memory.lynx`,
  `lowlevel_syscalls.lynx` and `language_fields.lynx`. All are
  `.expected`-diffed and compiled, so both the amd64 and arm64 CI jobs run them.
  `stdlibTestAll` now skips only its `game` section under
  `LYNXER_SKIP_DISPLAY=1`, so the rest of its coverage runs on a runner too.
- [x] Fixed the two language bugs found while verifying the docs: compound
  assignment on a field (`this.v += x`, `instance.field *= x`, and the typed
  vargroup form `int p.n += x`) desugared to `field op x` instead of
  `object.field op x`; and `-> none` return annotations failed in
  `Environment::convertForType`. Regression fixture:
  `examples/language_fields.lynx`.
- [x] Lynxer is the primary implementation (2026-09-23). It has surpassed the
  Python reference for real use; the state is flagged on `README.md`,
  `clynxer/__init__.py` and `clynxer/shell.py`, and recorded in
  `.agents/memory/lynxer_investigation.md` (Revision 11).
- [x] Rework the documentation set so it matches the implementation. Every
  `lynxer/docs/*.md` page was restructured with headings and cross-links and
  its examples re-verified against the interpreter; `language.md`, `types.md`,
  `lists.md`, `structs.md`, `classes.md`, `enums.md`, `vargroups.md`,
  `modules.md`, `importAs.md`, `CLI.md`, `install.md` and `README.md` were
  rewritten, and `builtins.md` corrected (async and FFI **are** implemented; the
  exact typed memory names; the unsupported-name table). The 27 `stdlib/*.md`
  pages got a uniform footer and the flagged errors were fixed. The built-in
  `--help` text no longer advertises the six unsupported flags and is now a
  golden case; the `sound`, `sqldb` and `tui` wrappers use a standalone `////`
  docstring line so `--list-stdlibs` prints their descriptions.
- [x] Document intentional differences and dropped Python-only features.
  `lynxer/docs/limitations.md` is the canonical register; `parity.md`
  summarises what is and is not a parity target, and `docs/limitations.md`
  keeps the "will not be done" framing on the Python side.
- [x] Treat differences with no Python counterpart as out of scope for parity,
  listed in `lynxer/docs/parity.md`: the `--compile` ELF executable, bundling
  and `bundledFile()`, the Rust `cdylib` ABI, `network`/`server`, cooperative
  `nativeThread*`, the AST optimizer and `--no-opt`/`LYNXER_OPT_REPORT`, the
  formatter and `--validate-executeable`. `--ast` is the only flag still
  reported as unavailable.
- [x] Implement the remaining CLI tools: a token-based formatter for
  `--format`/`--format-oneline` (`lynxer/formatter.cpp`; comments preserved
  verbatim, idempotent, never changes tokens), a built-in interpreter self-check
  for `--validate-executeable` (17 cases, no external files), and
  `--install`/`--uninstall` (`/usr/bin/clynxer`). The lexer's tokens now carry
  byte offsets, and `Parser::parseProgram(false)` allows tooling to validate a
  file that has no entry points. Covered by the formatter fixture gate, the
  `--validate-executeable` gate, and new golden CLI cases. `--ast` still reports
  unavailable.

## Known parity bugs

None open. All three original entries are resolved (2026-09-23). `test25` (a
double `memoryFree`) and `test26` (an invalid address) raise `address refers to
freed memory` and `invalid native memory address` with source locations through
the allocation registry in `builtins.cpp`. `test22`'s typed 8-byte writes now
preserve the full signed and unsigned range: `typedWrite`/`typedWriteEndian` no
longer coerce the value through a `double`, so
`memoryWriteInt64(p, off, 9223372036854775807)` round-trips and any value above
2^53 a double cannot represent is no longer corrupted. Covered by
`examples/lowlevel_memory.lynx`.

## Current boundary

Lynxer currently supports the core language, all loop forms, the extended
value model (list/tuple/sentinel/object/char/codeblock), the full documented
scalar type set (`num`, `numBool`, `bit`, `byte`, `int8`..`uint64`,
`float32`/`float64`), typed element literals (`[int 1, int 2]` and
`(int 10, int 20)`), a flat per-function lexical scope, `const` with
reassignment errors, and structs/classes/enums/vargroups with `switch` /
pattern matching. The list/tuple/IO/conversion/introspection built-ins, the
native-memory family, and the named syscalls are also implemented.

There is no bytecode backend: `--compile` writes a standalone ELF executable
that embeds the program, every transitively imported module source, and every
imported native library, so a compiled program supports imports and stdlibs and
behaves exactly like an interpreted one. An AST optimizer runs before execution
(`--no-opt` disables it). The root Makefile builds and tests both Lynxer and
Lynxer targets; `make testLynxer` runs the module-contract check, the
CLI/diagnostic golden cases, every `.expected` fixture (including the low-level
native-memory and syscall fixtures), and interpreted-versus-compiled parity, and
the two Lynxer GitHub Actions workflows run it on push and pull request with
`LYNXER_SKIP_DISPLAY=1` (the display/audio tests are skipped on a runner). The
bundled standard library covers `math`, `json`, `re`, `regex`,
`os`, `path`, `fileIO`, `csv`, `time`, `debug`, `sys`, `shell`, `cli`, `js`,
`multiprocessing`, `random`, `image`, `lua`, `game`, `network`, `server`,
`sound`, `sqldb`, `tui`, and `text`/`typing`/`colorlib`. The remaining Python
reference modules not yet ported are `tkinter`, `tkinterPlus`, and `turtle` —
a `graphics` module on Rust `iced` is planned in place of tkinter; the older
`http`/`net` modules are superseded by `network`/`server`.
