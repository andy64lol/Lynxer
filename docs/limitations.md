# Runtime and standard-library constraints

## Native Module ABI

- **Signature constraints.** A native signature names each parameter with a
  type token: `int64` (any integer width), `double`/`float64`, `cstring`, or
  `bytes`. A `bytes` parameter is passed as two C arguments (a pointer and a
  length), and a `bytes` return is a length-prefixed buffer. The packed `...`
  form passes numbers and strings as two arrays and accepts at most 256
  arguments in total; the buffered packed form `...,bytes` additionally carries
  byte buffers (`const uint8_t* const* bufs, const int64_t* lens, int64_t
  buf_count`).

- **Aggregates.** A numeric list crosses as `bytes` holding little-endian
  `f64` (the `listToBytes` / `bytesToList` encoding); other structured data
  still crosses as a JSON string or a handle.

- **String handling.** Only one live string result per call is supported
  (`thread_local` buffer); a `bytes` result likewise has one live buffer.

- **Platform support.** `.so` imports are currently Linux/POSIX-only and not
  available on Windows.

## Standard Library Constraints

### `graphics` — Constrained Behavior

- **A display is required for the GPU renderer.** macroquad draws through
  OpenGL, so with a window it needs X11 or Wayland. With
  `LYNXER_GRAPHICS_HEADLESS=1` no window is opened and the module rasterizes on
  the CPU instead: every shape primitive fills a framebuffer — `clearBackground`,
  `drawRectangle(_Lines)`, `drawLine`, `drawTriangle(_Lines)`,
  `drawCircle(_Lines)`, `drawEllipse(_Lines)`, `drawPoly(_Lines)`,
  `drawHexagon`, `drawArc` and the rotated-rectangle ops — and `screenshot`
  writes that frame as a PNG (in the same top-left origin the drawing ops use).
  `stdlib_graphics_raster.lynx` and `stdlib_graphics_raster_shapes.lynx` check
  this in CI.
- **The headless path also covers text, textures, render targets and the UI.**
  Text is rasterized on the CPU with `fontdue`; the default face is a copy of
  the ProggyClean TTF macroquad embeds, and `loadFont` parses a user TTF the same
  way. `loadTexture` decodes an image to a CPU texture and the `drawTexture*`
  ops blit it (with scale, source region, rotation and tint); `renderTarget` /
  `setRenderTarget` / `endRenderTarget` redirect drawing into an offscreen
  framebuffer and `renderTargetTexture` / `getScreenData` hand it back as a
  texture or image. `uiWindowBegin`/`uiGroupBegin` blocks are drawn as a stacked
  approximation of macroquad's layout (no input or hit-testing headless). Text
  anti-aliasing and UI layout are therefore close to, but not pixel-identical
  with, the GPU path.
- **Shaders, materials and 3D meshes are GPU-only.** A fragment/vertex shader
  cannot execute without a GPU, so `loadMaterial`/`setMaterial`/`setShader*`,
  the `drawCube`/`drawSphere`/`drawPlane`/`drawGrid` primitives and model/mesh
  loading stay no-ops headless and return their `-1`/placeholder results.
- **Ops that need the GPU context only work inside a frame callback.**
  `loadMaterial`, `renderTarget`, `screenshot`, the `draw*` ops and the `ui*`
  widgets all fail if called from `setup` or `main`; the headless path relaxes
  this for the CPU-expressible ops above.
- **No audio, native widgets or gamepads.** macroquad's audio feature is not
  vendored, the `ui*` widgets are canvas-drawn, and 0.4.16 has no gamepad API.
- **`uiWindowBegin`/`uiGroupBegin` buffer one block deep.** A nested `*Begin`
  returns `-1`; widget values are updated when the block is replayed, so read
  them after `*End`.
- **The windowed path is not covered by CI.** CI sets
  `LYNXER_SKIP_DISPLAY=1`, which skips the graphics fixture.

### `game` — Constrained Behavior

- **Tilemaps use the first atlas, with a headless fallback.** `loadTilemap`
  reads the first `<tileset>` and CSV layer data, resolves the atlas path
  relative to the TMX file, and crops nonzero GIDs using `firstgid`, `margin`
  and `spacing`. Tiles remain solid sprites. If the texture is unavailable or
  the module is headless, it uses shaded blocks instead.
- **Physics covers walls, slopes and one-way platforms.** `updatePhysics`
  resolves horizontal and vertical collision against a wall list, rides a wall
  whose sprite `angle` is non-zero as a slope, and treats the list passed as
  `makePhysicsEngine`'s third argument as one-way platforms (solid only from
  above, within a two-unit snap). The player is still a single axis-aligned
  box: there is no rotation-aware collision, no sprite-against-sprite physics
  and no moving-platform carry.
- **Animated sprites and sound are texture/audio-backed.** In headless mode
  `makeAnimatedSprite` and `loadSound` return `-1`, and `screenshot` returns
  `-1`; the drawing, sound and screenshot ops are otherwise no-ops.
- **Sound playback state uses decoded duration.** `isSoundPlaying` expires
  one-shots after their probed duration and keeps loops active until stopped.
  This is not an audio-backend playback-position query; if duration probing
  fails, the state remains active until `stopSound`.

### `server` — Constrained Behavior

- **TLS is built, and does not block.** `runHTTPS(cert, key)` and
  `runSSLAdhoc()` start an HTTPS listener on the `init` host/port with `rustls`
  (the `ring` provider — no system OpenSSL) and return `"ok"`; `stop()` ends it.
  They do not block the way `run()` does. A certificate/key mismatch is only
  found when a client connects.
- **Request-context readers describe the last request.** `getArg`, `getHeader`,
  `getBody` and friends read the most recently handled request, because a Lynxer
  route is a fixed string rather than a callback and the interpreter evaluates
  one frame at a time. The original's Flask request context has no equivalent.
  A **template**, however, is rendered *inside* the request: it can read the
  request through the `request` object (`request.args`, `request.headers`,
  `request.cookies`, `request.body`, `request.json`, `request.form`,
  `request.method`, `request.path`), and the query arguments also overlay the
  root context.
- **Templates are a Jinja-compatible subset.** `template`, `templatePost` and
  `templateString` render `{{ expression }}` (with filters), `{% if %}`/
  `{% elif %}`/`{% else %}`, `{% for x in items %}` (with the `loop` variable),
  `{% set %}` and `{# comments #}`. Expressions cover literals, dotted paths,
  `or`/`and`/`not`, `==`/`!=`/`<`/`<=`/`>`/`>=`, `in`, `+`/`-`/`*`/`/`/`%`,
  `~` (concatenation), the `range()` helper and the filters `upper`, `lower`,
  `trim`, `capitalize`, `title`, `length`, `first`, `last`, `reverse`, `join`,
  `default`, `int`, `float`, `round`, `replace` and `string`. Inheritance
  (`{% extends %}`/`{% block %}`), macros and includes are **not** implemented,
  there is no auto-escaping, and an unknown key renders as the empty string.
- **`run()` blocks.** It starts the listener on the `init` host/port and then
  blocks, matching the original. Use `start()` + `stop()` when the program has
  to keep running.
- **Static paths are confined.** `staticFiles`, `staticSite` and `serveFile`
  refuse any `..` component, and a bad file read is a 404.

### `compress` — Constrained Behavior

- **Payloads are `bytes`.** An in-memory `<codec>Compress` takes `bytes` and
  returns `bytes`, and its `<codec>Decompress` does the same, so a payload that
  is not valid UTF-8 is representable. The `*File` ops work on paths and read and
  write the bytes themselves.
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

- **Payloads are `bytes`.** A message, key, signature or random payload crosses
  as `bytes`; `hash`/`hmac` return lower-case hex (a text form is the API) and
  `signEd25519` returns `bytes`. `hashFile`/`hmacFile` read the file's bytes
  inside the module.
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

- **Linux, macOS and the BSDs.** The backend is inotify on Linux and
  kqueue `EVFILT_VNODE` on macOS and the BSDs. Windows has no descriptor to poll
  and is not supported. The kqueue backend reports the registered path itself
  (a directory reports that its contents changed) rather than the individual
  file name, and is not covered by CI — only the Linux path is.
- **Handles, closed by the caller.** Each watch is an integer handle; dropping
  it (`watchRemove` / `watchClose`) closes the descriptor.
- **Blocking wait, with the lock released.** `watchWait(handle, timeoutMs)`
  blocks on the descriptor (1 = readable, 0 = timeout, -1 = unknown handle) and
  releases the interpreter lock while it waits, so it does not wedge other
  Lynxer threads. `watchFd` still returns the descriptor for the interpreter's
  own `asyncPollRegister` / `asyncPollWait`, and `watchDrain` reads the pending
  events without blocking.
- **Failure is in-band.** `watchAdd`/`watchFd`/`watchWait` yield `-1`,
  `watchDrain` yields `""` for an unknown handle, and the predicates yield
  `false`.
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

- **Payloads are `bytes`.** An encode takes `bytes` and returns a `str` (the
  encoded form is text); a decode takes a `str` and returns `bytes`, so a payload
  that is not valid UTF-8 is representable. `*Valid` reports whether the input is
  well formed in the codec.
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

- **UUIDs are strings.** The canonical form is the lower-case hyphenated one;
  `uuidToHex` returns the simple 32-hex-character form, and `uuidToBytes` /
  `uuidFromBytes` exchange the raw 16 bytes as a `bytes` value.
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
- **`v1` and `v6` are not exposed.**

### `re` and `regex` — Constrained Behavior

Both modules run a Rust `fancy-regex` engine (`rust/regex_engine`, shared by
`rust/re` and `rust/regex`) — a superset of ECMAScript that adds the PCRE
constructs `std::regex` lacked. No system regex library is involved.

- **Supported syntax:** lookahead and lookbehind `(?<=...)`/`(?<!...)` including
  variable-length lookbehind, atomic groups `(?>...)`, possessive quantifiers
  (`a++`), backreferences (`\1` and `(?P=name)`), Unicode property escapes
  (`\p{L}`, `\p{Greek}`), named groups `(?P<name>...)`/`(?<name>...)`, and
  inline flags — which now apply from the position they appear — plus `(?x)`
  verbose mode.
- **Flags:** `I`, `M` and `S` set case-insensitivity, multiline and dotall;
  `X` (alias `x`) enables verbose mode, which the C++ engine ignored.
- **Error handling:** Invalid patterns — and a pattern that fails at match time,
  such as an exhausted backtracking budget — return sentinel results
  (predicates `false`, strings `""`, index helpers `-1`) instead of raising.
- **Matching:** `\d`/`\w` are Unicode-aware, as in PCRE, while `findLetters`/
  `findDigits` keep matching ASCII letter and digit runs only. Offsets reported
  by `matchStart`/`matchEnd`/`findSpans` are byte offsets.
- **Multi-group `findall`:** Returns arrays of groups when two or more capture
  groups are used.
- **Replacement groups:** An undefined group *name* is left as the literal
  `$name`, and an undefined group *number* expands to nothing.

### `csv` — Constrained Behavior

- **Line terminators:** Output uses `\r\n` line terminators.
- **Non-string values:** Non-string JSON values are rendered as `""` (empty string), `true`, or `false`.
- **Column handling:** Values beyond the header width are dropped, and missing columns are filled with `""` (empty string).
- **API note:** `docs/stdlib/csv.md` is the current API. The legacy `csv`
  names (`parseCSV`, `parseCSVRaw`, `readCSVRaw`, `csvColumn`, `csvRowCount`,
  `filterCSV`, `csvHeaders`, `csvRow`, `dedupCSV`) are available as forwarders;
  `csvRow` returns a JSON object and `csvHeaders` a comma-joined string, which
  are the legacy shapes.

### `text` and `typing` — Code Points

- **Code-point semantics.** Lynxer strings hold UTF-8, and `returnLength`,
  `charAt`, `substring` and the `charCode`/`charOf` builtins work on **code
  points**, so a character is one Unicode scalar value rather than one byte:
  `returnLength("café")` is 4, `charAt("café", 3)` is `"é"` and `charCode` of it
  is 233. `charOf` accepts any code point up to `0x10FFFF` except the surrogate
  range, and returns a UTF-8 sequence. A byte that cannot start a valid sequence
  counts as one code point, so any byte string still has a length.
  `typing.charCodeOf` returns `-1` for a non-char/non-string or an empty string,
  and `typing.charOf` returns a NUL `char` for a code outside the valid range.
- **Case conversion stays ASCII.** `text.upper`/`text.lower` change only
  `a`-`z`/`A`-`Z`, and `typing.isAlpha`/`isDigit` test ASCII, so `"é"` is
  neither alpha nor digit.
- **`typing.isNumeric`.** In `typing`, `isNumeric(value)` means "is an `int` or
  `float`", not the legacy "the string parses as a number". Use `typing.isDigit`
  for digit-only strings.
- **`typing.toFloat32` / `toFloat64`.** Both return a `float`; Lynxer `float` is
  a double, so there is no single-precision narrowing.

### `os` and `path` — Constrained Behavior

- **Encodings:** `path.readTextEncoding` and `path.writeTextEncoding` honour
  `utf-8` (also the default when the name is empty), `latin-1` and `ascii`. They
  no longer ignore the name: an unknown encoding, or a character the encoding
  cannot represent, is a failure sentinel (`""` / `false`) instead of a silent
  read or write as UTF-8. UTF-8 text is passed through without validation, so a
  non-UTF-8 byte still survives a `utf-8` read.
- **Platform info:** Platform helpers report the host through `uname(2)`.

### `sys` — Constrained Behavior

- **Version:** `version()` returns the Lynxer version (e.g., `Lynxer 0.1.8.1`).
- **Architecture:** `architecture()` returns the canonical syscall architecture of this build (`amd64` or `arm64`), not the raw machine string from `uname(2)`.
- **System information:** `cpuCount`, `pageSize`, `memoryTotal`, `memoryAvailable`, `uptime`, `bootTime` and `loadAverage` report the host on Linux, macOS and FreeBSD/DragonFly (`sysctl`), sharing the POSIX `sysconf`/`getloadavg` paths where they exist. NetBSD/OpenBSD fall back to `0` (or `[]`) for the `sysctl`-backed values, and any host that cannot provide a value returns `0` (or `[]`).
- **Command-line arguments:** `argv()`, `getArg` and `argCount` describe the **program's own** command line — the script path (or the compiled executable) followed by the arguments passed after it — not the `lynxer` process command line.
- **Exit behavior:** `exit()` calls `std::exit` directly, bypassing interpreter cleanup.

### `cli` — Constrained Behavior

- **Click/Typer builders:** The `click*` and `typer*` builders are implemented natively (integer handles over JSON descriptors) rather than by binding the Python packages. A failed parse returns `{"error": "…"}` rather than raising. The old Python-availability probes (`clickExists`, `typerExists`, `clickVersion`, `typerVersion`) were removed as meaningless.

### `multiprocessing` — Constrained Behavior

- **Process handling:** Commands run in worker threads, each spawning its own shell subprocess. `runParallelProcess` is an alias for `runParallel`.
- **Timeout and stderr:** A command is killed after `LYNXER_MP_TIMEOUT` seconds (default `300`, `0` for no limit) and reports exit code `124`; its `stderr` is captured into the output.
- **Resource management:** Results are collected through a native handle, which must be released. The wrappers handle this automatically.

### `js` — Constrained Behavior

- **Dependency:** Requires `node` to be on `PATH`.
- **Timeout:** A program is killed after `LYNXER_JS_TIMEOUT` seconds (default `30`) and answers an explanatory error.
- **Error handling:** `stderr` is captured into the result rather than inherited.

### `debug` — Constrained Behavior

- **Type mapping:** `typeOf` maps the interpreter's `none` to `null`.
- **String rendering:** `dump` and `pp` print `strOf` rendering, so strings are not quoted.
- **Logging:** `log`/`info`/`warn`/`error`/`debug` embed a wall-clock timestamp and are not covered by the fixture suite.

### `math` — Constrained Behavior

- **Statistics:** NumPy-backed statistics (`median`, `std`, `variance`, `percentile`, `corrcoef`, `dot`, `linspace`, `cumsum`, `diff`, `clip`, `normalize`) are reimplemented natively. NumPy is not required.
  - Population variance and standard deviation are used.
  - `percentile` follows NumPy's linear interpolation.

- **Merged modules:** `mathPlus` is merged into `math`. The float-accepting `sign` from `mathPlus` is available as `signFloat`.

### `sound` — Constrained Behavior

- **Loading:** `loadSound` decodes the file into memory at load time, so
  playback survives the file being moved or deleted; `loadSoundStreaming` reads
  only the header and decodes as it plays, so the file must still be there. Both
  register a handle and return its index.

- **Playback:**
  - Requires an audio device. If none is available, `playSound` and `loopSound` return `false` instead of aborting. Loading, `soundCount()`, and `releaseSound` continue to work.
  - `pauseSound` and `resumeSound` require a player that has been started. They return `false` for a valid handle that has not been played yet.

- **Resource management:**
  - `releaseSound` returns `false` for already-released handles.
  - `soundCount()` counts only handles that have not been released.

- **Sound metadata:** `getSoundLength` returns the duration resolved at load
  time and never re-reads the file. For a container with no duration header the
  streaming load decodes once at load time to learn it, then drops the samples.

### `sqldb` — Constrained Behavior

- **Connection handling:** A function takes either a database **path** — a
  connection is opened for that call and closed after it — or a **handle** from
  `open()`, which keeps one connection live until `close()`. The *On forms
  (`queryOn`, `executeArgsOn`, …) run against a handle; a handle stays valid for
  the run, and calling `close` twice (or with an unknown handle) reports `0`.

- **Error handling:** String-returning operations raise a catchable runtime
  error on failure. Integer and boolean functions retain their `-1`/`false`
  sentinels for failures, including unknown or closed handles.

- **JSON formatting:** `query`, `queryArgs`, and `tables` emit JSON with `": "` and `", "` separators.

- **BLOB handling:** SQLite BLOB values are rendered as base64 strings, as JSON has no native byte type.

- **Scalar queries:** `scalar` converts the first column of the first row using `strOf` and returns `""` (empty string) if the query yields no row or the value is NULL.

### `tui` — Constrained Behavior

The backend is real (`ratatui`), but it is not a pixel-for-pixel Rich equivalent:

- **Rendering is offscreen.** Each call draws a widget into a buffer and prints the text — plain text without a TTY, ANSI styling with one. Box-drawing, table sizing and layout follow `ratatui`, not Rich, and `setWidth` (default `80`) pins the width.

- **`tableSetLines` draws real rules.** `ratatui`'s `Table` has no row separators,
  so the rows are re-laid-out with one blank line under the header and between
  every pair of rows, and those lines are painted as `├───┼───┤` rules (a bare
  `───┼───` when the table has no box). With rules the columns sit one space
  apart, without them two — the flag therefore still changes the column gap.

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
