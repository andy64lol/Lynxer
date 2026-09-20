# Limitations and divergences

Clynxer is a standalone C++ implementation of Lynxer. It is not a drop-in
replacement for the Python implementation, and several stdlib behaviours
deliberately differ. This page lists everything you need to know before relying
on a module.

## Language and toolchain

- **Module self-calls.** Inside a module, `global.name(...)` resolves to a *core
  builtin* named `name`, not to the module's own `global name`. A module calling
  `global.round(...)` when it defines its own `round` fails with
  `unknown function 'global.round'`. Call the module's own functions with a bare
  name (`round(...)`); use `global.name(...)` only for builtins such as
  `assert`, `trim` or `upper`.
- **Comments.** Only `//` line comments and `/// ... ///` / `//// ... ////`
  delimiters are supported. `/* ... */` is a syntax error.
- **Logical NOT.** A bare `!` is invalid; use `!!value` or `not value`.
  `!=` is fine.
- **String escapes.** Only `\n`, `\r`, `\t`, `\\`, `\"` and `\e` are accepted.
  There is no `\x`/`\u` escape, so byte values such as `\x1f` cannot be written
  in Lynxer source. Modules that need such a separator use a writable one
  (for example a tab) instead.
- **There is no bytecode backend.** `.lynxc` files, `--view-bytecode`,
  `--benchmark-compile`, `--no-cache` and `--no-opt` were removed. `--compile`
  now produces a standalone ELF executable (the old `--bundle`); `--bundle`
  remains as an alias. Running a `.lynxc` file reports that bytecode is no longer
  supported.
- **Compiled executables embed their modules.** The payload carries the program
  source, the source of every transitively imported `.lynx` module, and the bytes
  of every imported native `.so`. Native modules are written to a temporary
  directory at startup so `dlopen` can load them, and the directory is removed
  when the process exits.
- **`--compile` accepts several input files.** The first `.lynx` file is the
  program; any further `.lynx` or `.so` files — given positionally or with
  `--include <file>` — are embedded and become importable by name, even when they
  live outside the module search path. `import` inside the program can resolve
  them, so a program may import a module that only exists as an explicit input.
  The output name comes from `-o <name>`/`--name <name>`, or from a trailing bare
  argument, and defaults to the first input without its `.lynx` extension.
- **`--include` also takes data files.** A file that is neither `.lynx` nor `.so`
  is embedded as an asset, written to the executable's private temporary
  directory at startup, and reachable with `bundledFile(name)` (a path) or listed
  with `bundledFiles()`. Interpreted runs see neither and return `""` / an empty
  list, so a program must tolerate missing assets when run from source.

## Native module ABI

- A native signature uses either one of the fixed shapes listed in
  [native-module-abi.md](native-module-abi.md) — at most four arguments — or the
  packed `...` form, which passes every argument as two arrays and accepts at
  most 64 arguments in total. A Rust `cdylib` must use the packed form; see that
  page for why a fixed shape segfaults instead of failing to build.
- No aggregate types cross the ABI: lists, tuples and records are exchanged as
  JSON strings or handles.
- Only one live string result per call (`thread_local` buffer).
- Not available on Windows: `.so` imports fail on non-POSIX hosts.

## `venv`

Deliberately **not implemented**. A virtual-environment manager is a Python
concept with no C++ runtime equivalent.

## Modules that are not ported

`tkinter`, `tkinterPlus` and `turtle` have no Clynxer equivalent. The plan is a
`graphics` module backed by Rust `iced` instead of tkinter, and Rust's `turtle`
crate has not been maintained since 2019. The Python `http`/`net` modules are
superseded by Clynxer's `network` + `server` pair, and `mathPlus` is merged into
`math`.

Every other Python module has a native backend. Nine of them are Rust crates —
`game` (`macroquad`), `image`, `json` (`serde_json`), `lua` (vendored Lua through
`mlua`), `network` (`ureq` + `tungstenite`), `server` (`axum` + `tokio`),
`sound` (`rodio`/`cpal`), `sqldb` (`rusqlite`) and `tui` (`ratatui`/`crossterm`).
They are skipped with a warning when `cargo` is missing, so the rest of Clynxer
still builds without a Rust toolchain.

## `json`

- Non-finite numbers (`NaN`, `Infinity`) are encoded as `null` so output is
  always valid JSON; Python emits the non-standard `NaN`/`Infinity` tokens.
- Object key order is insertion order, matching Python 3.7+.
- `jsonGet` renders booleans as `true`/`false` (Python's `str()` produces
  `True`/`False`).

## `re` and `regex`

Both run on `std::regex` with the ECMAScript grammar, which is narrower than
Python's `re`:

- Unsupported and reported as an error result: lookbehind `(?<=...)`/`(?<!...)`,
  atomic groups `(?>...)`, and Unicode property escapes such as `\p{L}`.
- `(?P<name>...)` is translated to a plain capturing group with the name
  recorded, so `named`/`extract`/`extractAll` work; `(?P=name)` becomes a numeric
  backreference.
- Inline flags `(?i)`, `(?m)`, `(?s)` are applied to the whole pattern rather
  than from the point they appear. `(?x)` verbose mode is ignored.
- Invalid or unsupported patterns produce sentinel results (predicates `false`,
  strings `""`, index helpers `-1`) instead of raising.
- `findLetters`/`findDigits` match ASCII letter and digit runs (`regex` falls
  back to `re` in the Python reference too).
- `findall` with two or more capture groups returns arrays of groups; the Python
  reference stringifies tuples.

## `csv`

- Output uses `\r\n` line terminators, matching Python's `csv` module.
- Non-string JSON values are rendered as `""` / `true` / `false`; Python's
  `str()` would produce `None` / `True` / `False`.
- Values beyond the header width are dropped, and missing columns become `""`
  (Python's `DictReader` uses `restkey`/`restval`).
- The bundled reference `docs/stdlib/csv.md` describes a different, older API
  than the shipped `csv.lynx`; Clynxer implements the shipped API.

## `os` and `path`

- `os.getPythonVersion()` returns `""` and `os.getPythonImplementation()`
  returns `"CLynxer"` — there is no Python runtime.
- `path.readTextEncoding` / `path.writeTextEncoding` accept an encoding argument
  for API compatibility but always use UTF-8.
- Platform helpers report the host through `uname(2)`.

## `sys`

- `version()` returns the CLynxer version (for example `CLynxer 0.1.8`), not a
  Python version string.
- Python-runtime concepts have no equivalent and are not defined: `sys.path`,
  `addPath`, `prependPath`, `removeFromPath`, `getModules`, `isModuleLoaded`,
  `getRecursionLimit`, `setRecursionLimit`.
- Clynxer does not forward extra arguments to a program, so `argv()`, `getArg`
  and `argCount` describe the `clynxer` process command line.
- `exit()` calls `std::exit` directly; interpreter cleanup does not run.

## `cli`

- The Click/Typer builder functions (`click*`, `typer*`) are **not defined**;
  calling one is a hard "unknown function" error. `clickExists()`/`typerExists()`
  return `false` and the version helpers return `""`, which is accurate.

## `multiprocessing`

- Commands run in worker threads, each spawning its own shell subprocess, so the
  `runParallelProcess` variant is an alias of `runParallel`.
- No timeout is applied. Results are collected through a native handle and must
  be released; the wrappers do this automatically.

## `js`

- Requires `node` on `PATH`. No timeout is applied (the Python reference used
  30 s / 10 s limits). `stderr` is inherited rather than captured.

## `debug`

- `typeOf` maps the interpreter's `none` to `null` for parity with the reference.
- `dump` and `pp` print `strOf` rendering rather than Python's `repr`, so strings
  are not quoted.
- `log`/`info`/`warn`/`error`/`debug` embed a wall-clock timestamp, so they are
  not covered by the fixture suite.

## `math`

- The NumPy-backed statistics (`median`, `std`, `variance`, `percentile`,
  `corrcoef`, `dot`, `linspace`, `cumsum`, `diff`, `clip`, `normalize`) are
  reimplemented natively — NumPy is not required. Population variance and
  standard deviation are used, and `percentile` follows NumPy's linear
  interpolation.
- Lists cross the native boundary as tab-separated strings, and list results are
  returned the same way.
- The former separate `mathPlus` module has been merged into `math`; the
  float-accepting `sign` from `mathPlus` is available as `signFloat`.

## `sound`

- `loadSound` and `loadSoundStreaming` are the same operation. rodio decodes
  from the file handle either way, so there is no static/streaming split; both
  register a handle and return its index.
- Playback needs an audio device. When none can be opened, `playSound` and
  `loopSound` report `false` instead of aborting — loading, `soundCount()` and
  `releaseSound` keep working.
- `pauseSound` and `resumeSound` require a player that has actually been
  started. They return `false` for a valid handle that has not been played yet,
  matching the reference's check for a live player.
- `releaseSound` returns `false` for an already-released handle, and
  `soundCount()` counts only the handles that have not been released.
- `getSoundLength` re-decodes the file on each call, so it returns `0.0` if the
  file has been moved or deleted since it was loaded.

## `sqldb`

- Every function takes a database path and opens a connection for the duration
  of the call, matching the reference. There is no connection handle to manage
  and nothing for the caller to close.
- Failures are returned in band as `"ERROR: <message>"` — and as `-1` / `false`
  for the integer and boolean functions — rather than being raised.
- `query`, `queryArgs` and `tables` emit JSON with Python's `json.dumps`
  separators (`": "` and `", "`), so their output is byte-identical to the
  reference.
- SQLite BLOB values are rendered as base64 strings, because JSON has no byte
  type.
- `scalar` converts the first column of the first row the way `str()` would, and
  returns `""` when the query yields no row or the value is NULL.

## `tui`

The API surface is complete, but the backend is a placeholder rather than a Rich
equivalent:

- The rendering operations do not reproduce Rich's output. With an active
  terminal they draw a fixed placeholder through `ratatui`; otherwise they print
  a plain-text line such as `markdown: ...` or `table: ...`.
- The prompt operations (`ask`, `askPassword`, `askInt`, `askFloat`,
  `askDefault`) return empty or zero defaults; they do not read input.
- `styleValid` reports every style as valid, including invalid ones.
- The stateful families (`table*`, `tree*`, `layout*`, `progress*`, `status*`,
  `live*`) return success and placeholder handles but keep no state between
  calls.
- `markupEscape`, `enter` and `exit` (raw mode plus the alternate screen) are
  real implementations. `tuiVersion` reports the backend crate's version, not
  ratatui's.
- The module writes its fallback output straight to stdout rather than through
  the interpreter. Both sides are line-buffered, so lines interleave in order,
  but a write with no trailing newline would not: `clear()` emits its ANSI
  sequence without one and flushes explicitly for that reason.

## `image`

- The pixel getters return a bracketed list (`[10,20,30,255]`); the reference
  returns a comma-joined string (`10,20,30,255`).
- The mutating operations (`save`, `saveQuality`, `setPixel`, `setPixelA`,
  `fill`, `paste`, `pasteWithAlpha`, `close`) return a boolean, where the
  reference returns `0` on success.
- `grayscale` keeps the alpha channel, so an `RGBA` image becomes `LA`; the
  reference produces `L`.
- `info` returns compact JSON, not the spaced form Python's `json.dumps`
  produces. The `json` module matches Python's spacing; `info` does not.
- `getFormat` and `info` report the detected format (`PNG`, `JPEG`, ...) for
  images decoded with `fromBase64`, not just for images opened from a file.

## `lua`

- `luaExists()` returns a `bool`; the reference returns an integer `0`/`1`.
- `luaVersion()` reports the vendored engine (`Lua 5.4`). The reference reports
  whichever version the system's `lupa` links against, so the strings differ by
  environment rather than by design.
- Error strings differ in both prefix and chunk name. Clynxer names the kind of
  failure and the chunk, e.g.
  `Error: syntax error: [string "clynxer.lua"]:1: syntax error near 'is'`,
  whereas the reference emits
  `Error: error loading code: [string "<python>"]:1: syntax error near 'is'`.
  The trailing part comes from Lua itself and is the same on both sides.
- A Lua runtime error is reported the same way and its text includes a Lua
  traceback, so the fixture asserts only that a message came back.

## Testing notes

`make -C clynxer test` runs one fixture per `examples/stdlib_*.lynx` and diffs
its output against a sibling `.expected` file. Fixtures that would print
host-specific values (Node version, terminal size, `uname` strings) assert a
boolean property instead.

Before the fixtures, it runs `scripts/check_module_contracts.py`, a static
comparison of every `stdlib/<name>.lynx` wrapper against its backend. The
fixture suite alone cannot detect a backend that contradicts its own wrapper,
because a fixture records what the code does — a module can be broken in exactly
the way its `.expected` file asserts. The check verifies that every
`global.native<Alias>.<op>(...)` call names a registered op, and that a Rust
backend's packed `args.<kind>(i)` reads are in range for the arguments the
wrapper actually passes. Both of the contract defects found in `sqldb` and
`tui` are caught by it. It requires `python3`; `make test` fails with a clear
message if it is missing rather than skipping the check.
