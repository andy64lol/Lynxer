# Limitations and non-goals

Lynxer is a standalone C++/Rust runtime. This page is the canonical register of
what it deliberately does **not** do and of the behaviour that is constrained.
Everything here is a decision, not an accident.

Entries are marked:

- **Not planned** — deliberately will not be implemented. These are permanent
  non-goals; do not expect them to appear later.
- **Constrained** — implemented, but with behaviour you should know before
  relying on it.

## Not planned (will not be implemented)

| Feature | Status | Why |
| --- | --- | --- |
| `venv` module | **Not planned** | A virtual-environment manager is a Python concept with no equivalent in a standalone runtime. |
| `rawPy`, `rawPyx`, `cleanRawPyxCache`, `embedPy` | **Not planned** | Embedding CPython/Cython. Lynxer does not ship or link a Python runtime. |
| `tkinter`, `tkinterPlus` | **Not planned** | No Python GUI toolkit. A future `graphics` module, if any, would be Rust-backed. |
| `turtle` | **Not planned** | Rust's `turtle` crate has not been maintained since 2019. |
| `http`, `net` modules | **Not planned** | Superseded by `network` + `server`. |
| Click/Typer builders (`click*`, `typer*`) | **Not planned** | The `cli` module does not depend on Python's Click or Typer. |
| Python runtime introspection (`sys.path`, `addPath`, `prependPath`, `removeFromPath`, `getModules`, `isModuleLoaded`, `getRecursionLimit`, `setRecursionLimit`, `os.getPythonVersion`, `os.getPythonImplementation`) | **Not planned** | There is no Python runtime to introspect. The `os` getters return `""` / `"Lynxer"` for compatibility and will be removed. |
| Bytecode (`.lynxc`, `--view-bytecode`, `--benchmark-compile`, `--no-cache`) | **Not planned** | Removed with the bytecode backend; `--compile` produces a standalone ELF executable instead (`--bundle` is an alias). Running a `.lynxc` file reports that bytecode is unsupported. |
| FFI / native-module handle built-ins | **Not planned** | Superseded by direct `import` of a native `.so` through the documented ABI. |
| `nativeMutex*`, `nativeCondition*`, `nativeSemaphore*` | **Not planned** | Lynxer runs cooperatively on one interpreter thread; there is one global lock and no shared mutable state to protect. |
| `async*` family (`Run`, `Gather`, `Sleep`, `Poll*`, timers, wakeups) | **Not planned** | Lynxer has no `async` language support and no event loop to serve. |
| `\x` / `\u` string escapes | **Not planned** | Only `\n`, `\r`, `\t`, `\\`, `\"` and `\e` are accepted. Modules that need a separator use a writable one (for example a tab). |

## Language and toolchain

- **Module self-calls.** Inside a module, `global.name(...)` resolves to a *core
  builtin* named `name`, not to the module's own `global name`. A module calling
  `global.round(...)` when it defines its own `round` fails with
  `unknown function 'global.round'`. Call the module's own functions with a bare
  name (`round(...)`); use `global.name(...)` only for builtins such as
  `assert`, `trim` or `upper`.
- **Compiled executables embed their modules.** The payload carries the program
  source, the source of every transitively imported `.lynx` module, and the bytes
  of every imported native `.so`. Native modules are written to a temporary
  directory at startup so `dlopen` can load them, and the directory is removed
  when the process exits.
- **`--compile` accepts several input files.** The first `.lynx` file is the
  program; any further `.lynx` or `.so` files — given positionally or with
  `--include <file>` — are embedded and become importable by name, even when they
  live outside the module search path. The output name comes from
  `-o <name>`/`--name <name>`, or from a trailing bare argument, and defaults to
  the first input without its `.lynx` extension.
- **`--include` also takes data files.** A file that is neither `.lynx` nor `.so`
  is embedded as an asset, written to the executable's private temporary
  directory at startup, and reachable with `bundledFile(name)` (a path) or listed
  with `bundledFiles()`. Interpreted runs see neither and return `""` / an empty
  list, so a program must tolerate missing assets when run from source.

## Optimizer

Before execution Lynxer runs a semantics-preserving AST optimization pass:
constant folding of literal-only expressions, short-circuit simplification of
constant `and`/`or`, and dead-branch elimination for a constant `if`,
`while (false)` and `iterate (0)`. It may not change behaviour, so anything that
could raise, warn or coerce differently — a division by zero, a deprecated
symbolic operator, a string plus an int — is left for the runtime at its original
source location.

- `--no-opt` runs the program without the pass. It is a run-time switch: a
  compiled executable ignores its command line and always optimizes.
- `LYNXER_OPT_REPORT=1` prints one line of transformation counts to stderr after
  the program runs. It is diagnostic only and never affects output.

## Native module ABI

- A native signature uses either one of the fixed shapes listed in
  [native-module-abi.md](native-module-abi.md) — at most four arguments — or the
  packed `...` form, which passes every argument as two arrays and accepts at
  most 64 arguments in total. A Rust `cdylib` must use the packed form; see that
  page for why a fixed shape segfaults instead of failing to build.
- No aggregate types cross the ABI: lists, tuples and records are exchanged as
  JSON strings or handles.
- Only one live string result per call (`thread_local` buffer).
- Linux/POSIX only: `.so` imports are not available on Windows.

## Standard library

### Modules that are not ported

`tkinter`, `tkinterPlus` and `turtle` have no Lynxer equivalent (**not planned**,
see the table above). The `http`/`net` modules are replaced by `network` +
`server`, and the old `mathPlus` is merged into `math`.

The modules Lynxer ships are backed natively. Nine of them are Rust crates —
`game` (`macroquad`), `image`, `json` (`serde_json`), `lua` (vendored Lua through
`mlua`), `network` (`ureq` + `tungstenite`), `server` (`axum` + `tokio`),
`sound` (`rodio`/`cpal`), `sqldb` (`rusqlite`) and `tui` (`ratatui`/`crossterm`).
They are skipped with a warning when `cargo` is missing, so the rest of Lynxer
still builds without a Rust toolchain. The Rust workspace also has an `ffi`
member, an intentional no-op `cdylib`: the `ffi*` builtins are implemented in
C++.

### `json` — **constrained**

- Non-finite numbers (`NaN`, `Infinity`) encode as `null` so output is always
  valid JSON.
- Object key order is insertion order.
- `jsonGet` renders booleans as `true`/`false`.

### `re` and `regex` — **constrained**

Both run on `std::regex` with the ECMAScript grammar, which is narrower than
full PCRE:

- Unsupported and reported as an error result: lookbehind `(?<=...)`/`(?<!...)`,
  atomic groups `(?>...)`, and Unicode property escapes such as `\p{L}`.
- `(?P<name>...)` is translated to a plain capturing group with the name
  recorded, so `named`/`extract`/`extractAll` work; `(?P=name)` becomes a numeric
  backreference.
- Inline flags `(?i)`, `(?m)`, `(?s)` are applied to the whole pattern rather
  than from the point they appear. `(?x)` verbose mode is ignored.
- Invalid or unsupported patterns produce sentinel results (predicates `false`,
  strings `""`, index helpers `-1`) instead of raising.
- `findLetters`/`findDigits` match ASCII letter and digit runs.
- `findall` with two or more capture groups returns arrays of groups.

### `csv` — **constrained**

- Output uses `\r\n` line terminators.
- Non-string JSON values are rendered as `""` / `true` / `false`.
- Values beyond the header width are dropped, and missing columns become `""`.
- The rendered `docs/stdlib/csv.md` describes a different, older API than the
  shipped `csv.lynx`; Lynxer implements the shipped API.

### `os` and `path` — **constrained**

- `os.getPythonVersion()` returns `""` and `os.getPythonImplementation()` returns
  `"Lynxer"` (**not planned**, see above).
- `path.readTextEncoding` / `path.writeTextEncoding` accept an encoding argument
  for API compatibility but always use UTF-8.
- Platform helpers report the host through `uname(2)`.

### `sys` — **constrained**

- `version()` returns the Lynxer version (for example `Lynxer 0.1.8`).
- Python-runtime concepts are **not planned** and are not defined (`sys.path`,
  `addPath`, `prependPath`, `removeFromPath`, `getModules`, `isModuleLoaded`,
  `getRecursionLimit`, `setRecursionLimit`).
- Lynxer does not forward extra arguments to a program, so `argv()`, `getArg`
  and `argCount` describe the `lynxer` process command line.
- `exit()` calls `std::exit` directly; interpreter cleanup does not run.

### `cli` — **constrained**

- The Click/Typer builder functions (`click*`, `typer*`) are **not planned**;
  calling one is a hard "unknown function" error. `clickExists()`/`typerExists()`
  return `false` and the version helpers return `""`, which is accurate.

### `multiprocessing` — **constrained**

- Commands run in worker threads, each spawning its own shell subprocess, so the
  `runParallelProcess` variant is an alias of `runParallel`.
- No timeout is applied. Results are collected through a native handle and must
  be released; the wrappers do this automatically.

### `js` — **constrained**

- Requires `node` on `PATH`. No timeout is applied. `stderr` is inherited rather
  than captured.

### `debug` — **constrained**

- `typeOf` maps the interpreter's `none` to `null`.
- `dump` and `pp` print `strOf` rendering, so strings are not quoted.
- `log`/`info`/`warn`/`error`/`debug` embed a wall-clock timestamp, so they are
  not covered by the fixture suite.

### `math` — **constrained**

- The NumPy-backed statistics (`median`, `std`, `variance`, `percentile`,
  `corrcoef`, `dot`, `linspace`, `cumsum`, `diff`, `clip`, `normalize`) are
  reimplemented natively — NumPy is not required. Population variance and
  standard deviation are used, and `percentile` follows NumPy's linear
  interpolation.
- Lists cross the native boundary as tab-separated strings, and list results are
  returned the same way.
- `mathPlus` is merged into `math`; its float-accepting `sign` is available as
  `signFloat`.

### `sound` — **constrained**

- `loadSound` and `loadSoundStreaming` are the same operation: rodio decodes from
  the file handle either way, so there is no static/streaming split; both register
  a handle and return its index.
- Playback needs an audio device. When none can be opened, `playSound` and
  `loopSound` report `false` instead of aborting — loading, `soundCount()` and
  `releaseSound` keep working.
- `pauseSound` and `resumeSound` require a player that has actually been started;
  they return `false` for a valid handle that has not been played yet.
- `releaseSound` returns `false` for an already-released handle, and `soundCount()`
  counts only handles that have not been released.
- `getSoundLength` re-decodes the file on each call, so it returns `0.0` if the
  file has been moved or deleted since it was loaded.

### `sqldb` — **constrained**

- Every function takes a database path and opens a connection for the duration of
  the call; there is no connection handle to manage and nothing to close.
- Failures are returned in band as `"ERROR: <message>"` — and as `-1` / `false`
  for the integer and boolean functions — rather than being raised.
- `query`, `queryArgs` and `tables` emit JSON with `": "` and `", "` separators.
- SQLite BLOB values are rendered as base64 strings, because JSON has no byte
  type.
- `scalar` converts the first column of the first row with `strOf`, and returns
  `""` when the query yields no row or the value is NULL.

### `tui` — **constrained (placeholder backend)**

The API surface is complete, but the backend is a placeholder rather than a Rich
equivalent:

- Rendering does not reproduce Rich's output. With an active terminal it draws a
  fixed placeholder through `ratatui`; otherwise it prints a plain-text line such
  as `markdown: ...` or `table: ...`.
- The prompt operations (`ask`, `askPassword`, `askInt`, `askFloat`,
  `askDefault`) return empty or zero defaults; they do not read input.
- `styleValid` reports every style as valid, including invalid ones.
- The stateful families (`table*`, `tree*`, `layout*`, `progress*`, `status*`,
  `live*`) return success and placeholder handles but keep no state between calls.
- `markupEscape`, `enter` and `exit` (raw mode plus the alternate screen) are real
  implementations. `tuiVersion` reports the backend crate's version.
- The module writes its fallback output straight to stdout rather than through the
  interpreter. Both sides are line-buffered, so lines interleave in order, but a
  write with no trailing newline would not: `clear()` emits its ANSI sequence
  without one and flushes explicitly for that reason.

### `image` — **constrained**

- The pixel getters return a bracketed list (`[10,20,30,255]`).
- The mutating operations (`save`, `saveQuality`, `setPixel`, `setPixelA`, `fill`,
  `paste`, `pasteWithAlpha`, `close`) return a boolean.
- `grayscale` keeps the alpha channel, so an `RGBA` image becomes `LA`.
- `info` returns compact JSON, not the spaced form the `json` module produces.
- `getFormat` and `info` report the detected format (`PNG`, `JPEG`, ...) for
  images decoded with `fromBase64`, not just for images opened from a file.

### `lua` — **constrained**

- `luaExists()` returns a `bool`.
- `luaVersion()` reports the vendored engine (`Lua 5.4`).
- Error strings name the failure kind and the chunk, e.g.
  `Error: syntax error: [string "lynxer.lua"]:1: syntax error near 'is'`.
- A Lua runtime error is reported the same way and its text includes a Lua
  traceback, so the fixture asserts only that a message came back.

## Built-in families

The managed `filesystem*`, `process*`, `networking*` and `sound*` families are
implemented and documented in [builtins.md](builtins.md).

`builtins.cpp` keeps an `unsupportedTable()` of names that are recognised but
deliberately unimplemented; calling one raises
`<name>() is not supported in Lynxer yet`. That table is the **not planned**
surface listed at the top of this page — `rawPy`/`rawPyx`, the FFI and
native-module handles, and the mutual-exclusion primitives — plus the `async*`
family and `unshare`.

### `nativeThread*` — **constrained (cooperative model)**

`nativeThreadStart(global.worker, [int 42])` passes a named global function, so
Lynxer resolves `global.<name>` to a callable value when no variable has that name.
`returnType` reports `codeblock` for such a value.

Threads are **cooperative**: the interpreter evaluates Lynxer code on one thread
at a time, guarded by one lock, and a worker takes that lock before calling back
in. A thread therefore runs while the thread that started it is blocked in
`nativeThreadJoin`/`nativeThreadJoinAll`, which release the lock before waiting.
No two threads ever evaluate at once, so no data race is possible — by
construction, not by careful locking. The trade-off is that a thread does not make
progress while the main body is running.

Two smaller consequences: `nativeThreadIsAlive` is true and
`nativeThreadStatus` is `running` immediately after `nativeThreadStart`,
deterministically, because the worker cannot have started yet; and a thread a
program leaves running is joined when the program finishes.

## CLI tools

`--lint`, `--ast`, `--format`, `--format-oneline`, `--validate-executeable` and
`--install`/`--uninstall` are implemented. `--ast` prints Lynxer's own node and
field names and covers the executable AST (functions and statements) rather than
the named type declarations held in the type registry. The flags removed with the
bytecode backend (`--view-bytecode`, `--benchmark-compile`, `--no-cache`) are
listed in [CLI.md](CLI.md).

The formatter is token-based: it never changes tokens, preserves line comments and
`///`/`////` blocks verbatim, and is idempotent.

## Testing notes

`make testLynxer` (from the repo root) runs one fixture per
`examples/stdlib_*.lynx` and diffs its output against a sibling `.expected` file.
Fixtures that would print host-specific values (Node version, terminal size,
`uname` strings) assert a boolean property instead.

The `sound` fixture is the exception: several of its assertions (starting and
stopping playback, per-handle volume) only hold on a host with a real ALSA card.
When there is no `/dev/snd/controlC*`, `make testLynxer` skips
`stdlib_sound.lynx` — in both the `.expected` diff and the interpreted-vs-compiled
parity loop — and prints

```
lynxer: skipping stdlib_sound.lynx: no audio device (/dev/snd/controlC*) on this host
```

Everything else about the module (loading, decoding, handle bookkeeping, the
error paths) is device-independent and stays covered.

Before the fixtures, it runs `scripts/check_module_contracts.py`, a static
comparison of every `stdlib/<name>.lynx` wrapper against its backend. The fixture
suite alone cannot detect a backend that contradicts its own wrapper, because a
fixture records what the code does — a module can be broken in exactly the way its
`.expected` file asserts. The check verifies that every
`global.native<Alias>.<op>(...)` call names a registered op, and that a Rust
backend's packed `args.<kind>(i)` reads are in range for the arguments the wrapper
actually passes. It requires `python3`; `make test` fails with a clear message if
it is missing rather than skipping the check.
