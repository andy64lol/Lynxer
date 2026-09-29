# Limitations and non-goals

Lynxer is a standalone C++/Rust runtime. This page documents **technical limitations** and **deliberate omissions** in the language, toolchain, and standard library.

Entries are marked:

- **Not planned** — a deliberate, permanent removal of a Python-runtime feature;
  it will not be re-added.
- **Planned** — a constraint with a resolution plan in [todo.md](../todo.md)
  under *Resolving documented limitations*.
- **Constrained** — implemented, but with behavior you should know before
  relying on it.

## Deliberate Omissions (Not Planned)

Retained removals: Python-runtime features with no place in a standalone runtime.

| Feature | Reason |
| --- | --- |
| `venv` module | A virtual-environment manager is a Python concept with no equivalent in a standalone runtime. |
| `rawPy`, `rawPyx`, `cleanRawPyxCache`, `embedPy` | Embedding CPython/Cython. Lynxer does not ship or link a Python runtime. |
| Python runtime introspection (`sys.path`, `addPath`, `prependPath`, `removeFromPath`, `getModules`, `isModuleLoaded`, `getRecursionLimit`, `setRecursionLimit`, `os.getPythonVersion`, `os.getPythonImplementation`) | There is no Python runtime to introspect. The `os` getters return `""` / `"Lynxer"` for compatibility and will be removed. |
| Bytecode (`.lynxc`, `--view-bytecode`, `--benchmark-compile`, `--no-cache`) | Removed with the bytecode backend; `--compile` produces a standalone ELF executable instead (`--bundle` is an alias). Running a `.lynxc` file reports that bytecode is unsupported. |

## Planned Work

Every constraint below has a resolution plan in [todo.md](../todo.md) under
*Resolving documented limitations*:

| Area | Plan |
| --- | --- |
| `tkinter` / `tkinterPlus` (a native OS-widget GUI module) | **L16** |
| `turtle` (reimplement on `graphics`) | **L17** |
| `http` / `net` (keep superseded by `network` + `server`, or add a shim) | **L18** |
| Binary payloads — a first-class `bytes` type, replacing the base64/hex/TSV workarounds in `compress`, `crypto`, `encoding`, `uuid`, `math` | **L1**, **L3** |
| Native module ABI — aggregate parameters, multiple string results, the 64-argument cap, non-Linux loading | **L2** |
| Module constraints — `graphics`, `game`, `server`, `re`/`regex`, `sound`, `watch`, `sqldb`, `js`, `multiprocessing`, `text`/`typing`, `tui`, `os`/`path`/`sys` | **L4**–**L14** |
| `nativeThread*` / `async*` true parallelism and coroutine `await` | **L15** |

## Native Module ABI

- **Signature constraints.** A native signature uses either:
  - One of the fixed shapes listed in [native-module-abi.md](native-module-abi.md) (at most four arguments), or
  - The packed `...` form, which passes every argument as two arrays and accepts at most 64 arguments in total.
  A Rust `cdylib` must use the packed form due to ABI compatibility constraints.

- **Data exchange.** No aggregate types (lists, tuples, records) cross the ABI directly. They are exchanged as JSON strings or handles.

- **String handling.** Only one live string result per call is supported (`thread_local` buffer).

- **Platform support.** `.so` imports are currently Linux/POSIX-only and not available on Windows.

## Standard Library

### Module Coverage

- `tkinter`, `tkinterPlus`, and `turtle` are planned (see **L16**–**L17** in
  [todo.md](../todo.md)).
- `http`/`net` are superseded by `network` + `server` (**L18**).
- `mathPlus` is merged into `math`.

### Native Backing

Lynxer ships modules backed by native implementations. Nineteen of them are Rust crates:
- `compress` (`flate2`/`zstd`/`brotli`/`lz4_flex`/`zip`/`tar`)
- `crypto` (`sha2`/`sha1`/`md-5`/`sha3`/`blake3`/`hmac`/`subtle`/`getrandom`/`ed25519-dalek`)
- `encoding` (`base64`/`hex`/`data-encoding`/`bs58`/`ascii85`/`percent-encoding`/`quoted_printable`)
- `game` (`macroquad`)
- `graphics` (`macroquad`)
- `image`
- `ini` (`rust-ini`)
- `json` (`serde_json`)
- `lua` (vendored Lua through `mlua`)
- `network` (`ureq` + `tungstenite`)
- `server` (`axum` + `tokio`)
- `sound` (`rodio`/`cpal`)
- `sqldb` (`rusqlite`)
- `tui` (`ratatui`/`crossterm`)
- `uuid` (`uuid`)
- `watch` (`inotify`)
- `toml` (`toml`)
- `xml` (`quick-xml`)
- `yaml` (`serde_yml`)

A Rust toolchain is **required**: every one of these backends is built and
installed by `cargo`, and the interpreter links the `ffi` `staticlib`
native-call engine. A missing toolchain or a failed `cargo build` fails the
build outright — there is no reduced build that leaves a backend out. The
Makefile checks for `cargo` up front and says so.

### `graphics` — Constrained Behavior

- **A display is required for real rendering.** macroquad has no offscreen
  backend, so any drawing needs a window and OpenGL (X11 or Wayland). With
  `LYNXER_GRAPHICS_HEADLESS=1` no window is opened, drawing is a no-op and
  context-dependent ops return `-1` or zero.
- **Ops that need the GPU context only work inside a frame callback.**
  `loadTexture`, `loadFont`, `loadMaterial`, `renderTarget`, `screenshot`, the
  `draw*` ops and the `ui*` widgets all fail if called from `setup` or `main`.
- **No audio, native widgets or gamepads.** macroquad's audio feature is not
  vendored, the `ui*` widgets are canvas-drawn, and 0.4.16 has no gamepad API.
- **No model loading or mesh construction.** Only the generated 3D primitives
  are available.
- **`uiWindowBegin`/`uiGroupBegin` buffer one block deep.** A nested `*Begin`
  returns `-1`; widget values are updated when the block is replayed, so read
  them after `*End`.
- **The windowed path is not covered by CI.** CI sets
  `LYNXER_SKIP_DISPLAY=1`, which skips the graphics fixture.

### `game` — Constrained Behavior

- **Tilemaps carry geometry, not artwork.** `loadTilemap` reads only the first
  `<tileset>` (for the tile size) and each `<layer>`'s CSV `<data>`; tiles
  become solid sprites on the grid. The tileset image is not sliced, so the map
  is useful for collision and layout rather than rendering.
- **Physics is vertical only.** `updatePhysics` applies gravity and resolves
  landing and ceiling contact against a wall list. There is no horizontal
  collision resolution, and no slope/one-way platform support.
- **Animated sprites and sound are texture/audio-backed.** In headless mode
  `makeAnimatedSprite` and `loadSound` return `-1`, and `screenshot` returns
  `-1`; the drawing, sound and screenshot ops are otherwise no-ops.
- **Sound playback state is tracked by the module.** A one-shot that has
  finished still reports as playing through `isSoundPlaying` until
  `stopSound`.

### `server` — Constrained Behavior

- **TLS is not built.** `runHTTPS(cert, key)` and `runSSLAdhoc()` return an
  explanatory `ERROR:` string instead of starting a listener. Servers terminate
  TLS in a reverse proxy, or the module is rebuilt with a TLS backend
  (`axum-server`, `rustls-pemfile`, `rcgen` are not among the pinned
  dependencies; adding them would make the module require network access).
- **Request-context readers describe the last request.** `getArg`, `getHeader`,
  `getBody` and friends read the most recently handled request, because a Lynxer
  route is a fixed string rather than a callback and the interpreter evaluates
  one frame at a time. The original's Flask request context has no equivalent.
- **Templates are a substitution subset.** `template`, `templatePost` and
  `templateString` render `{{ key }}` and `{{ nested.key }}` from the `dataJson`
  object. Jinja2 loops, conditionals, filters and inheritance are not
  implemented, and an unknown key renders as the empty string.
- **`run()` blocks.** It starts the listener on the `init` host/port and then
  blocks, matching the original. Use `start()` + `stop()` when the program has
  to keep running.
- **Static paths are confined.** `staticFiles`, `staticSite` and `serveFile`
  refuse any `..` component, and a bad file read is a 404.

### `compress` — Constrained Behavior

- **No byte type.** An in-memory `<codec>Compress` returns base64 and its
  `<codec>Decompress` takes base64. A decompressed payload need not be valid
  UTF-8, and `Decompress` answers text only when it is (`""` otherwise); the
  `*File` ops read and write the bytes themselves and are the binary-safe path.
  See the binary-payload decision in [todo.md](../todo.md).
- **Two sentinel families.** The in-memory stream ops use the scalar sentinel
  (`""`); the file and archive ops answer with `"ok"` / `"ERROR: <message>"`,
  because they can fail with a reason (a missing file, a bad archive).
- **A decompress is capped at 64 MiB** of output, for the in-memory stream ops
  and the `*File` ops alike, so a crafted stream cannot exhaust memory; an LZ4
  stream whose declared length exceeds the cap is refused before it is decoded.
- **Archive extraction is capped at 64 MiB**: a ZIP entry on `zipRead` and
  `zipExtract`, and the total declared entry size on `tarExtract` and
  `tarGzExtract` (whose decompressed stream is bounded too), so a crafted
  archive cannot exhaust memory or disk.
- **An extract refuses to escape its destination.** ZIP entries are checked with
  the archive's enclosed-name rule and TAR entries with `unpack_in`; a name that
  is absolute or contains `..` is an error (and `zipCreate`/`tarCreate` reject
  such a name when the archive is written).
- **`zstd` vendors the zstd C sources**, so a C compiler is required in addition
  to the Rust toolchain. `zip` is built with deflate only (`bzip2`/`zstd`/`aes`
  support is off).

### `crypto` — Constrained Behavior

- **No byte type.** Lynxer has no byte type, so a digest or signature crosses the
  ABI as text: `hash`/`hmac` return lower-case hex, `signEd25519` and the key
  pair return base64. A binary payload held in memory is passed base64 through
  `hashBase64`; arbitrary file contents go through `hashFile`/`hmacFile`, which
  read the bytes inside the module. See the binary-payload decision in
  [todo.md](../todo.md).
- **Failures are in-band.** An unknown algorithm, a malformed payload or an I/O
  error yields `""`, and a predicate yields `false`; there is no exception.
- **The algorithm names are a fixed set.** `sha1`, `sha224`, `sha256`, `sha384`,
  `sha512`, `md5`, `sha3-256`, `sha3-512` and `blake3` for a digest;
  `hash`/`hmac` ignore case but do not accept aliases such as `SHA-256`.
- **`verifyHmac` compares in constant time**; a MAC that is not valid hex is a
  mismatch, not an error.
- **Ed25519 only.** The module signs and verifies Ed25519 keys (32-byte seed,
  base64); RSA and ECDSA are not exposed. Key generation uses OS entropy, so
  `generateEd25519KeyPair` is not reproducible.
- **`randomBytes`/`randomHex`/`randomToken` cap the count** at 1 MiB and answer
  `""` outside `0..=1 MiB`, rather than allocating an unbounded buffer.

### `watch` — Constrained Behavior

- **Linux only.** The backend is inotify; there is no portable fallback.
- **Handles, closed by the caller.** Each watch is an integer handle; dropping
  it (`watchRemove` / `watchClose`) closes the descriptor.
- **No blocking wait.** A module cannot release the interpreter lock, so there
  is no `watchWait`: `watchFd` returns the inotify descriptor for the
  interpreter's own `asyncPollRegister` / `asyncPollWait`, and `watchDrain`
  reads the pending events without blocking.
- **Failure is in-band.** `watchAdd`/`watchFd` yield `-1`, `watchDrain` yields
  `""` for an unknown handle, and the predicates yield `false`.
- **`watchSetDebounce` coalesces within a drain**: events for the same path
  inside the window keep only the most recent kind. 0 disables it.

### `toml` — Constrained Behavior

- **Documents are JSON strings.** A TOML document crosses as JSON (the shared
  structured-value bridge); a JSON object serializes back to TOML. A TOML
  datetime becomes a JSON string.
- **Failures are in-band.** A document op yields `""` and `tomlValid` yields
  `false`; a JSON value that TOML cannot hold (a top-level non-table, or a
  `null`) makes `tomlSerialize` answer `""`.

### `ini` — Constrained Behavior

- **Documents are JSON strings.** An INI document crosses as an object of
  sections, each an object of string values; the unnamed leading section uses
  `""` as its key. Values are strings in both directions.
- **Failures are in-band.** A document op yields `""` and `iniValid` yields
  `false`. An empty document is valid, so it parses to `{"":{}}`.

### `xml` — Constrained Behavior

- **A document maps onto a JSON element tree**:
  `{"name", "attributes", "text", "children"}`. Comments, processing
  instructions and the doctype are ignored, and **no external entity is ever
  resolved**.
- **Input is capped at 16 MiB.** A larger document (`xmlParse`/`xmlValid`) or
  JSON tree (`xmlSerialize`) is refused before parsing, so a huge input cannot
  balloon into a proportionally larger tree.
- **`text` is the character data directly inside an element**; it is escaped on
  serialize, so a round trip of a document with entities is exact.
- **Failures are in-band.** A malformed document yields `""` and `xmlValid`
  yields `false`; `xmlUnescape` yields `""` for a malformed entity.

### `yaml` — Constrained Behavior

- **Documents are JSON strings.** A YAML mapping becomes a JSON object and a
  sequence a JSON array. A mapping whose key is not a string (which JSON cannot
  hold) is a failure.
- **Anchors and aliases are resolved while parsing**, so the input is capped at
  1 MiB to bound an alias-expansion payload; a larger input is a failure.
- **Failures are in-band.** A document op yields `""` and `yamlValid` yields
  `false`.

### `json` — Constrained Behavior

- Non-finite numbers (`NaN`, `Infinity`) are encoded as `null` to ensure valid JSON output.
- Object key order follows insertion order.
- `jsonGet` renders booleans as `true`/`false`.

### `encoding` — Constrained Behavior

- **No byte type.** Lynxer strings are the byte carrier: an encode function uses
  the UTF-8 bytes of its input, and a decode function returns the decoded bytes
  as text. Lynxer has no byte type yet, so a payload that is not valid UTF-8 has
  no representation — a decode that produces one returns `""`, the same sentinel
  a malformed input returns. `*Valid` reports well-formedness in the codec only,
  so it can be `true` while `*Decode` answers `""`. Bulk binary is expected to
  travel through files instead; see the binary-payload decision in
  [todo.md](../todo.md).
- **Failures are in-band.** A decode returns `""` and a predicate returns
  `false`; there is no exception to catch.
- **Padding is optional when decoding** and always emitted when encoding, for
  base64 and base32 alike; base32 also accepts either case. Whitespace inside a
  payload is never accepted.
- **`percentDecode` validates the escapes itself.** The underlying decoder
  leaves a malformed escape in place (`%2` stays `%2`), so a bare `%` is a
  failure here rather than a passthrough.
- **`ascii85Encode` includes the Adobe `<~`/`~>` frame**, and decoding accepts a
  framed or bare stream. The `z` shorthand is expanded before decoding because
  `ascii85` 0.2.1 rejects every `z` it should accept.
- **`quotedPrintableEncode` is the text encoding of RFC 2045**, so `CR`/`LF`
  become `=0D`/`=0A` and long lines gain soft breaks; decoding is strict.

### `uuid` — Constrained Behavior

- **UUIDs are strings.** Lynxer has no byte type, so a UUID crosses the ABI as
  text: `uuidToHex` returns the simple 32-hex-character form as the hex view of
  the 16 bytes, and `uuidFromHex` accepts exactly that.
- **Failures are in-band.** An unparseable UUID yields `""`, `uuidValid` yields
  `false`, and `uuidVersion`/`uuidTimestamp` yield `-1`; there is no exception to
  catch.
- **Accepted input forms.** Hyphenated, simple, `urn:uuid:` and braced, with
  surrounding whitespace trimmed. The URN and braced forms must wrap the
  *hyphenated* form; wrapping the simple form is rejected, matching
  [RFC 9562](https://www.rfc-editor.org/rfc/rfc9562).
- **`uuidVersion` reports the raw nibble.** A UUID that is not a well-formed RFC
  variant reports whatever its 13th hex digit says (for example `12`), rather
  than failing.
- **`v1` and `v6` are not exposed**, and neither are byte-buffer entry points.

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
- **API note:** `docs/stdlib/csv.md` is the current API. The legacy `csv`
  names (`parseCSV`, `parseCSVRaw`, `readCSVRaw`, `csvColumn`, `csvRowCount`,
  `filterCSV`, `csvHeaders`, `csvRow`, `dedupCSV`) are available as forwarders;
  `csvRow` returns a JSON object and `csvHeaders` a comma-joined string, which
  are the legacy shapes.

### `text` and `typing` — Byte Strings

- **Byte semantics.** Lynxer strings are byte strings: `returnLength`, `charAt`,
  `substring` and the `charCode`/`charOf` builtins count bytes, so a code point
  is a byte value in `0..255`, not a Unicode scalar value. `typing.charCodeOf`
  returns `-1` for a non-char/non-string or an empty string, and `typing.charOf`
  returns a NUL byte outside `0..255`.
- **`typing.isNumeric`.** In `typing`, `isNumeric(value)` means "is an `int` or
  `float`", not the legacy "the string parses as a number". Use `typing.isDigit`
  for digit-only strings.
- **`typing.toFloat32` / `toFloat64`.** Both return a `float`; Lynxer `float` is
  a double, so there is no single-precision narrowing.

### `os` and `path` — Constrained Behavior

- **Python compatibility:** `os.getPythonVersion()` returns `""` (empty string), and `os.getPythonImplementation()` returns `"Lynxer"` for compatibility.
- **Encoding:** `path.readTextEncoding` and `path.writeTextEncoding` accept an encoding argument for API compatibility but always use UTF-8 internally.
- **Platform info:** Platform helpers report the host through `uname(2)`.

*Note:* Python runtime introspection features are not planned for Lynxer.

### `sys` — Constrained Behavior

- **Version:** `version()` returns the Lynxer version (e.g., `Lynxer 0.1.8.1`).
- **Architecture:** `architecture()` returns the canonical syscall architecture of this build (`amd64` or `arm64`), not the raw machine string from `uname(2)`.
- **System information:** `cpuCount`, `pageSize`, `memoryTotal`, `memoryAvailable`, `uptime`, `bootTime` and `loadAverage` are Linux-only and return `0` (or `[]`) when the host cannot provide the value.
- **Python runtime concepts:** Not supported (`sys.path`, `addPath`, `prependPath`, `removeFromPath`, `getModules`, `isModuleLoaded`, `getRecursionLimit`, `setRecursionLimit`).
- **Command-line arguments:** `argv()`, `getArg`, and `argCount` describe the `lynxer` process command line, not the program's arguments.
- **Exit behavior:** `exit()` calls `std::exit` directly, bypassing interpreter cleanup.

### `cli` — Constrained Behavior

- **Click/Typer builders:** The `click*` and `typer*` builders are implemented natively (integer handles over JSON descriptors) rather than by binding the Python packages. A failed parse returns `{"error": "…"}` rather than raising. The old Python-availability probes (`clickExists`, `typerExists`, `clickVersion`, `typerVersion`) were removed as meaningless.

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

### `tui` — Constrained Behavior

The backend is real (`ratatui`), but it is not a pixel-for-pixel Rich equivalent:

- **Rendering is offscreen.** Each call draws a widget into a buffer and prints the text — plain text without a TTY, ANSI styling with one. Box-drawing, table sizing and layout follow `ratatui`, not Rich, and `setWidth` (default `80`) pins the width.

- **`tableSetLines` is approximate.** `ratatui`'s `Table` has no row separators, so the flag widens the gap between columns instead of drawing inner lines.

- **Markdown and syntax highlighting use crates.** `markdown` is `pulldown-cmark` and `printSyntax` is `syntect` (embedded syntax/theme sets, pure-Rust `fancy-regex` backend). Nesting, tables and every CommonMark extension are therefore limited to what those crates emit; syntax colors appear only on a color-capable terminal.

- **Terminal control and input are TTY-only.** `setCursor`, `moveCursor`, `hideCursor`, `showCursor`, `bell` and `input`/`pollInput` act only when stdout/stdin is a terminal; otherwise they are inert and return `""`/`false`. `terminalWidth`/`terminalHeight` fall back to the configured width and a default height.

- **`printException` has no traceback to show.** No exception context crosses the module ABI, so it prints a fixed placeholder.

- **Progress, status and live displays do not animate.** There is no refresh thread or timer; each update prints a snapshot, and `stop` prints the final one.

- **Prompts require stdin.** They read one line and return the documented default at end-of-file rather than blocking; `askPassword` disables echo only on a terminal.

- **`enter`/`exit` gate raw mode on a TTY.** Without a terminal they are inert, so a non-interactive run never switches screens. The same applies to `menu` (it falls back to a typed line) and `image` (it prints `[image WxH: path]` until a color terminal renders the half blocks).

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
- `rawPy`/`rawPyx`/`cleanRawPyxCache` and `embedPy` (removed fully — there is no Python runtime).

[legacy-surface.md](legacy-surface.md) catalogues the original built-ins and
modules that were not carried over — pointers/raw addresses, native structs,
async, `rawPy`, and the un-ported modules — with their replacements.

### `nativeThread*` — Constrained (Cooperative Model)

- **Function resolution:** `nativeThreadStart(global.worker, [int 42])` passes a named global function. Lynxer resolves `global.<name>` to a callable value when no variable has that name. `returnType` reports `codeblock` for such a value.

- **Cooperative threading:** The interpreter evaluates Lynxer code on one thread at a time, guarded by a single lock (the GIL). A worker takes the lock before calling back.
  - The GIL is released wherever the holding thread blocks or yields: `nativeThreadJoin`/`nativeThreadJoinAll`, the blocking synchronization waits, `asyncSleep`, `asyncPollWait`, and the explicit `nativeThreadYield(seconds?)`.
  - No two threads evaluate simultaneously, ensuring no data races by design.
  - Trade-off: CPU-bound tasks do not run in parallel; interleave them with `nativeThreadYield`.

- **Behavioral notes:**
  - `nativeThreadIsAlive` is `true` and `nativeThreadStatus` is `running` immediately after `nativeThreadStart`, because the worker cannot have started yet.
  - A thread left running by the program is joined when the program finishes.

### `async*` — Thread-Backed Tasks

- `asyncRun` starts a real worker thread and returns a task handle (it no longer returns the function's value); `await` joins a task handle — re-raising its error — and passes any other value through; `asyncGather` joins the handles among its arguments.
- Concurrency is real but subject to the GIL above: tasks interleave (and a task's `asyncSleep`/`asyncPollWait` lets others run) rather than executing in parallel.
- `async name(){}` local sub-functions remain **eager** — they run like an ordinary call — so the polling helpers stay usable inside an `async` block; use `asyncRun` for a task.
- There are no coroutines: `await` is a join, not a suspension. A task that awaits itself is reported (`a task cannot await itself`) rather than deadlocking; two tasks that await each other still deadlock.
