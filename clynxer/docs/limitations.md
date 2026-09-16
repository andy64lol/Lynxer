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
  when the process exits. Both `--compile` and `--bundle` accept an optional
  output name; without one the executable is named after the input file.

## Native module ABI

- At most four arguments per native signature, and only the shapes listed in
  [native-module-abi.md](native-module-abi.md) are callable.
- No aggregate types cross the ABI: lists, tuples and records are exchanged as
  JSON strings or handles.
- Only one live string result per call (`thread_local` buffer).
- Not available on Windows: `.so` imports fail on non-POSIX hosts.

## `venv`

Deliberately **not implemented**. A virtual-environment manager is a Python
concept with no C++ runtime equivalent.

Modules not yet ported: `game`, `image`, `tui`, `http`, `net`, `lua`.
(`http`/`net` need libcurl/Boost.Asio-class dependencies; `game`/`image` need
SDL2 and an image codec; `lua` embeds the Lua runtime. These are planned as
opt-in modules so the core build never depends on them.) The Python reference's
`tkinter`, `tkinterPlus`, `turtle`, `sound`, `server` and `sqldb` modules are
out of scope.

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

## Testing notes

`make -C clynxer test` runs one fixture per `examples/stdlib_*.lynx` and diffs
its output against a sibling `.expected` file. Fixtures that would print
host-specific values (Node version, terminal size, `uname` strings) assert a
boolean property instead.
