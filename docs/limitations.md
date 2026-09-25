# Limitations and non-goals

Lynxer is a standalone C++/Rust runtime. This page documents **technical limitations** and **deliberate omissions** in the language, toolchain, and standard library.

Entries are marked:

- **Not planned** — deliberately will not be implemented. These are permanent
  non-goals; do not expect them to appear later.
- **Constrained** — implemented, but with behavior you should know before
  relying on it.

## Deliberate Omissions (Not Planned)

| Feature | Reason |
| --- | --- |
| `venv` module | A virtual-environment manager is a Python concept with no equivalent in a standalone runtime. |
| `rawPy`, `rawPyx`, `cleanRawPyxCache`, `embedPy` | Embedding CPython/Cython. Lynxer does not ship or link a Python runtime. |
| `tkinter`, `tkinterPlus` | No Python GUI toolkit. A future `graphics` module, if any, would be Rust-backed. |
| `turtle` | Rust's `turtle` crate has not been maintained since 2019. |
| `http`, `net` modules | Superseded by `network` + `server`. |
| Click/Typer builders (`click*`, `typer*`) | The `cli` module does not depend on Python's Click or Typer. |
| Python runtime introspection (`sys.path`, `addPath`, `prependPath`, `removeFromPath`, `getModules`, `isModuleLoaded`, `getRecursionLimit`, `setRecursionLimit`, `os.getPythonVersion`, `os.getPythonImplementation`) | There is no Python runtime to introspect. The `os` getters return `""` / `"Lynxer"` for compatibility and will be removed. |
| Bytecode (`.lynxc`, `--view-bytecode`, `--benchmark-compile`, `--no-cache`) | Removed with the bytecode backend; `--compile` produces a standalone ELF executable instead (`--bundle` is an alias). Running a `.lynxc` file reports that bytecode is unsupported. |
| `varBorrow*` family | Python's borrowed references have no meaning across the native ABI. |
| FFI / native-module handle built-ins | Superseded by direct `import` of a native `.so` through the documented ABI. |
| `nativeMutex*`, `nativeCondition*`, `nativeSemaphore*` | Lynxer runs cooperatively on one interpreter thread; there is one global lock and no shared mutable state to protect. |
| `async*` family (`Run`, `Gather`, `Sleep`, `Poll*`, timers, wakeups) | Lynxer has no `async` language support and no event loop to serve. |

## Language and Toolchain Design

- **Module self-calls.** Inside a module, `global.name(...)` resolves to a *core
  builtin* named `name`, not to the module's own `global name`. A module calling
  `global.round(...)` when it defines its own `round` fails with
  `unknown function 'global.round'`. Call the module's own functions with a bare
  name (`round(...)`); use `global.name(...)` only for builtins such as
  `assert`, `trim`, or `upper`.

- **Compiled executables embed their modules.** The payload carries the program
  source, the source of every transitively imported `.lynx` module, and the bytes
  of every imported native `.so`. Native modules are written to a temporary
  directory at startup so `dlopen` can load them, and the directory is removed
  when the process exits.

- **`--compile` accepts multiple input files.** The first `.lynx` file is the
  program; any further `.lynx` or `.so` files — given positionally or with
  `--include <file>` — are embedded and become importable by name, even when they
  live outside the module search path. The output name comes from
  `-o <name>`/`--name <name>`, or from a trailing bare argument, and defaults to
  the first input without its `.lynx` extension.

- **`--include` also supports data files.** A file that is neither `.lynx` nor `.so`
  is embedded as an asset, written to the executable's private temporary
  directory at startup, and reachable with `bundledFile(name)` (a path) or listed
  with `bundledFiles()`. Interpreted runs see neither and return `""` / an empty
  list, so a program must tolerate missing assets when run from source.

## Optimizer

Before execution, Lynxer runs a semantics-preserving AST optimization pass:
- Constant folding of literal-only expressions.
- Short-circuit simplification of constant `and`/`or` operations.
- Dead-branch elimination for constant `if`, `while (false)`, and `iterate (0)`.

The optimizer preserves behavior, so operations that could raise, warn, or coerce differently (e.g., division by zero, deprecated symbolic operators, or string concatenation with integers) are left for the runtime at their original source location.

- `--no-opt` disables the optimizer. This is a runtime switch: compiled executables always optimize, regardless of command-line arguments.
- `LYNXER_OPT_REPORT=1` prints transformation counts to stderr after the program runs. This is for diagnostic purposes only and does not affect output.

## Native Module ABI

- **Signature constraints.** A native signature uses either:
  - One of the fixed shapes listed in [native-module-abi.md](native-module-abi.md) (at most four arguments), or
  - The packed `...` form, which passes every argument as two arrays and accepts at most 64 arguments in total.
  A Rust `cdylib` must use the packed form due to ABI compatibility constraints.

- **Data exchange.** No aggregate types (lists, tuples, records) cross the ABI directly. They are exchanged as JSON strings or handles.

- **String handling.** Only one live string result per call is supported (`thread_local` buffer).

- **Platform support.** `.so` imports are currently Linux/POSIX-only and not available on Windows.

## Standard Library

### Modules Not Ported

The following modules are not ported to Lynxer:
- `tkinter`, `tkinterPlus`, and `turtle` (no equivalent in a standalone runtime).
- `http`/`net` modules are superseded by `network` + `server`.
- `mathPlus` is merged into `math`.

### Native Backing

Lynxer ships modules backed by native implementations. Nine of them are Rust crates:
- `game` (`macroquad`)
- `image`
- `json` (`serde_json`)
- `lua` (vendored Lua through `mlua`)
- `network` (`ureq` + `tungstenite`)
- `server` (`axum` + `tokio`)
- `sound` (`rodio`/`cpal`)
- `sqldb` (`rusqlite`)
- `tui` (`ratatui`/`crossterm`)

These modules are skipped with a warning if `cargo` is missing, allowing the rest of Lynxer to build without a Rust toolchain. The Rust workspace includes an `ffi` member, an intentional no-op `cdylib`, since the `ffi*` builtins are implemented in C++.

### `json` — Constrained Behavior

- Non-finite numbers (`NaN`, `Infinity`) are encoded as `null` to ensure valid JSON output.
- Object key order follows insertion order.
- `jsonGet` renders booleans as `true`/`false`.

### `re` and `regex` — Constrained Behavior

Both modules use `std::regex` with the ECMAScript grammar, which is narrower than full PCRE:

- **Unsupported features:** Lookbehind `(?<=...)`/`(?<!...)`, atomic groups `(?>...)`, and Unicode property escapes (`\p{L}`) are not supported and return error results.
- **Named captures:** `(?P<name>...)` is translated to a plain capturing group with the name recorded, enabling `named`/`extract`/`extractAll`. `(?P=name)` becomes a numeric backreference.
- **Inline flags:** `(?i)`, `(?m)`, and `(?s)` are applied to the entire pattern, not from their position. `(?x)` verbose mode is ignored.
- **Error handling:** Invalid or unsupported patterns return sentinel results (predicates `false`, strings `""`, index helpers `-1`) instead of raising errors.
- **ASCII-only matching:** `findLetters`/`findDigits` match ASCII letter and digit runs only.
- **Multi-group `findall`:** Returns arrays of groups when two or more capture groups are used.

### `csv` — Constrained Behavior

- **Line terminators:** Output uses `\r\n` line terminators.
- **Non-string values:** Non-string JSON values are rendered as `""` (empty string), `true`, or `false`.
- **Column handling:** Values beyond the header width are dropped, and missing columns are filled with `""` (empty string).
- **API note:** The rendered `docs/stdlib/csv.md` describes an older API. Lynxer implements the API shipped with `csv.lynx`.

### `os` and `path` — Constrained Behavior

- **Python compatibility:** `os.getPythonVersion()` returns `""` (empty string), and `os.getPythonImplementation()` returns `"Lynxer"` for compatibility.
- **Encoding:** `path.readTextEncoding` and `path.writeTextEncoding` accept an encoding argument for API compatibility but always use UTF-8 internally.
- **Platform info:** Platform helpers report the host through `uname(2)`.

*Note:* Python runtime introspection features are not planned for Lynxer.

### `sys` — Constrained Behavior

- **Version:** `version()` returns the Lynxer version (e.g., `Lynxer 0.1.8`).
- **Python runtime concepts:** Not supported (`sys.path`, `addPath`, `prependPath`, `removeFromPath`, `getModules`, `isModuleLoaded`, `getRecursionLimit`, `setRecursionLimit`).
- **Command-line arguments:** `argv()`, `getArg`, and `argCount` describe the `lynxer` process command line, not the program's arguments.
- **Exit behavior:** `exit()` calls `std::exit` directly, bypassing interpreter cleanup.

### `cli` — Constrained Behavior

- **Click/Typer builders:** The `click*` and `typer*` builder functions are not supported. Calling them results in a hard "unknown function" error. `clickExists()` and `typerExists()` return `false`, and version helpers return `""` (empty string).

### `multiprocessing` — Constrained Behavior

- **Process handling:** Commands run in worker threads, each spawning its own shell subprocess. `runParallelProcess` is an alias for `runParallel`.
- **Timeout:** No timeout is applied to subprocess execution.
- **Resource management:** Results are collected through a native handle, which must be released. The wrappers handle this automatically.

### `js` — Constrained Behavior

- **Dependency:** Requires `node` to be on `PATH`.
- **Timeout:** No timeout is applied to JavaScript execution.
- **Error handling:** `stderr` is inherited rather than captured.

### `debug` — Constrained Behavior

- **Type mapping:** `typeOf` maps the interpreter's `none` to `null`.
- **String rendering:** `dump` and `pp` print `strOf` rendering, so strings are not quoted.
- **Logging:** `log`/`info`/`warn`/`error`/`debug` embed a wall-clock timestamp and are not covered by the fixture suite.

### `math` — Constrained Behavior

- **Statistics:** NumPy-backed statistics (`median`, `std`, `variance`, `percentile`, `corrcoef`, `dot`, `linspace`, `cumsum`, `diff`, `clip`, `normalize`) are reimplemented natively. NumPy is not required.
  - Population variance and standard deviation are used.
  - `percentile` follows NumPy's linear interpolation.

- **List handling:** Lists cross the native boundary as tab-separated strings, and list results are returned the same way.

- **Merged modules:** `mathPlus` is merged into `math`. The float-accepting `sign` from `mathPlus` is available as `signFloat`.

### `sound` — Constrained Behavior

- **Loading:** `loadSound` and `loadSoundStreaming` are identical operations. Rodio decodes from the file handle in both cases, so there is no static/streaming split. Both register a handle and return its index.

- **Playback:**
  - Requires an audio device. If none is available, `playSound` and `loopSound` return `false` instead of aborting. Loading, `soundCount()`, and `releaseSound` continue to work.
  - `pauseSound` and `resumeSound` require a player that has been started. They return `false` for a valid handle that has not been played yet.

- **Resource management:**
  - `releaseSound` returns `false` for already-released handles.
  - `soundCount()` counts only handles that have not been released.

- **Sound metadata:** `getSoundLength` re-decodes the file on each call, returning `0.0` if the file has been moved or deleted since loading.

### `sqldb` — Constrained Behavior

- **Connection handling:** Every function takes a database path and opens a connection for the duration of the call. There is no connection handle to manage or close.

- **Error handling:** Failures are returned in-band as `"ERROR: <message>"` (and as `-1`/`false` for integer/boolean functions) instead of being raised.

- **JSON formatting:** `query`, `queryArgs`, and `tables` emit JSON with `": "` and `", "` separators.

- **BLOB handling:** SQLite BLOB values are rendered as base64 strings, as JSON has no native byte type.

- **Scalar queries:** `scalar` converts the first column of the first row using `strOf` and returns `""` (empty string) if the query yields no row or the value is NULL.

### `tui` — Constrained (Placeholder Backend)

The API surface is complete, but the backend is a placeholder rather than a Rich equivalent:

- **Rendering:** Does not reproduce Rich's output. With an active terminal, it draws a fixed placeholder through `ratatui`. Otherwise, it prints plain-text lines like `markdown: ...` or `table: ...`.

- **Prompt operations:** `ask`, `askPassword`, `askInt`, `askFloat`, and `askDefault` return empty or zero defaults and do not read input.

- **Style validation:** `styleValid` reports every style as valid, including invalid ones.

- **Stateful families:** `table*`, `tree*`, `layout*`, `progress*`, `status*`, and `live*` return success and placeholder handles but do not retain state between calls.

- **Real implementations:** `markupEscape`, `enter`, and `exit` (raw mode and alternate screen) are fully implemented. `tuiVersion` reports the backend crate's version.

- **Output handling:** The module writes fallback output directly to stdout (line-buffered), interleaving with interpreter output. Writes without trailing newlines may not flush correctly (e.g., `clear()` emits its ANSI sequence without a newline and flushes explicitly).

### `image` — Constrained Behavior

- **Pixel getters:** Return a bracketed list (e.g., `[10,20,30,255]`).

- **Mutating operations:** `save`, `saveQuality`, `setPixel`, `setPixelA`, `fill`, `paste`, `pasteWithAlpha`, and `close` return a boolean.

- **Grayscale conversion:** `grayscale` retains the alpha channel, so an `RGBA` image becomes `LA`.

- **JSON formatting:** `info` returns compact JSON, not the spaced form produced by the `json` module.

- **Format detection:** `getFormat` and `info` report the detected format (`PNG`, `JPEG`, etc.) for images decoded with `fromBase64`, not just for images opened from a file.

### `lua` — Constrained Behavior

- **Existence check:** `luaExists()` returns a boolean.

- **Version:** `luaVersion()` reports the vendored engine (`Lua 5.4`).

- **Error handling:**
  - Error strings include the failure kind and the chunk, e.g., `Error: syntax error: [string "lynxer.lua"]:1: syntax error near 'is'`.
  - Lua runtime errors include a Lua traceback in their text. Fixtures only assert that a message was returned.

## Built-in Families

The managed `filesystem*`, `process*`, `networking*`, and `sound*` families are implemented and documented in [builtins.md](builtins.md).

### Unsupported Built-ins

`builtins.cpp` maintains an `unsupportedTable()` of names that are recognized but deliberately unimplemented. Calling one raises:
`<name>() is not supported in Lynxer yet`.

This includes:
- `rawPy`/`rawPyx`, `varBorrow*`, FFI/native-module handles, and mutual-exclusion primitives (not planned).
- The `async*` family (not supported).

### `nativeThread*` — Constrained (Cooperative Model)

- **Function resolution:** `nativeThreadStart(global.worker, [int 42])` passes a named global function. Lynxer resolves `global.<name>` to a callable value when no variable has that name. `returnType` reports `codeblock` for such a value.

- **Cooperative threading:** The interpreter evaluates Lynxer code on one thread at a time, guarded by a single lock. A worker takes the lock before calling back.
  - A thread runs only while the thread that started it is blocked in `nativeThreadJoin`/`nativeThreadJoinAll`, which release the lock before waiting.
  - No two threads evaluate simultaneously, ensuring no data races by design.
  - Trade-off: A thread does not progress while the main body is running.

- **Behavioral notes:**
  - `nativeThreadIsAlive` is `true` and `nativeThreadStatus` is `running` immediately after `nativeThreadStart`, because the worker cannot have started yet.
  - A thread left running by the program is joined when the program finishes.

## CLI Tools

- **Implemented flags:** `--lint`, `--ast`, `--format`, `--format-oneline`, `--validate-executable`, `--install`, and `--uninstall`.
- **AST output:** `--ast` prints Lynxer's own node and field names, covering the executable AST (functions and statements) rather than named type declarations in the type registry.
- **Removed flags:** `--view-bytecode`, `--benchmark-compile`, and `--no-cache` were removed with the bytecode backend. See [CLI.md](CLI.md) for details.

- **Formatter:** Token-based and idempotent. It never changes tokens, preserves line comments and `///`/`////` blocks verbatim.

## Testing Notes

- **Fixture execution:** `make testLynxer` (from the repo root) runs one fixture per `examples/stdlib_*.lynx` and compares its output against a sibling `.expected` file.
  Fixtures that would print host-specific values (Node version, terminal size, `uname` strings) assert a boolean property instead.

- **Sound module:** The `sound` fixture includes assertions that only hold on hosts with a real ALSA card (e.g., starting/stopping playback, per-handle volume). If `/dev/snd/controlC*` is unavailable, `make testLynxer` skips `stdlib_sound.lynx` and prints:
  ```
  lynxer: skipping stdlib_sound.lynx: no audio device (/dev/snd/controlC*) on this host
  ```
  Device-independent aspects (loading, decoding, handle bookkeeping, error paths) remain covered.

- **Module contract checks:** Before running fixtures, `scripts/check_module_contracts.py` performs a static comparison of every `stdlib/<name>.lynx` wrapper against its backend. This ensures:
  - Every `global.native<Alias>.<op>(...)` call names a registered operation.
  - Rust backends' packed `args.<kind>(i)` reads are within the range of arguments passed by the wrapper.
  This script requires `python3`. If missing, `make test` fails with a clear message instead of skipping the check.
