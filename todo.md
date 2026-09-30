# Lynxer — TODO and non-goals

Open work for the standalone C++ implementation in `lynxer/`. Completed work is
struck through under [Done](#done), and the permanent non-goals and removed
features are recorded in [docs/limitations.md](docs/limitations.md) and
[docs/removed-features.md](docs/removed-features.md) rather than duplicated
here. The named-syscall reference is [docs/syscalls.md](docs/syscalls.md).

Any name still registered in `unsupportedTable()` reports
`<name>() is not supported in Lynxer yet`.

## Open work

### Resolving documented limitations

Plans to remove every constraint recorded in
[docs/limitations.md](docs/limitations.md). Ordered foundation-first: the
value-type and ABI work (L1–L3) unblocks the binary-oriented modules, so land it
before the module-specific items.

**Foundation**

- [x] ~~**L1 — first-class `bytes` value type.**~~ **Delivered:** a `BytesValue`
      variant with a `bytes` type name and a full builtin set (`bytesOf`,
      `bytesToStr`, `bytesLength`, `bytesAt`, `bytesToHex`/`bytesFromHex`,
      `listToBytes`/`bytesToList`), covered by `lynxer/examples/builtin_bytes.lynx`.
      The Rust packed modules that still carry base64 payloads are **L1b** (now delivered).
- [x] ~~**L2 — extend the native-module ABI.**~~ **Partly delivered:** a `bytes`
      signature token (a parameter expands to `(const uint8_t*, int64_t)`; a
      return is a length-prefixed buffer) and the packed cap raised to 256. The
      packed `bytes` channel, multiple live string results, and non-Linux loading
      are **L2b**–**L2d** (now delivered — see below).
- [x] ~~**L3 — replace the tab-separated list bridge in `math`.**~~ **Delivered:**
      the 11 statistics/vector ops exchange little-endian `f64` `bytes`, the TSV
      helpers are deleted, and list results are `list<float>`.

**L1–L3 follow-ups — delivered**

- [x] ~~**L1b — migrate the remaining binary modules to `bytes`.**~~ `compress`,
      `crypto`, `encoding` and `uuid` now take/return `bytes`: codec
      Compress/Decompress, `hash`/`hmac`/`verifyHmac`, Ed25519 sign/verify,
      `randomBytes`, `constantTimeEquals`, and the codec Encode/Decode pairs (the
      `hashBase64` workaround is gone). Wrappers, fixtures and docs updated.
- [x] ~~**L2b — packed `bytes`/aggregate parameters.**~~ The buffered packed form
      `cdecl:<ret>(...,bytes)` carries numbers, strings **and** buffers in a
      7-scalar CIF; `lynxer_abi` provides `view_buffers`, `Args::bytes`,
      `store_result_bytes` and the `export_*_buffers!` macros.
- [x] ~~**L2c — non-Linux loading.**~~ `isNativeLibraryPath` accepts `.dylib`
      alongside `.so` (`lynxer/native_name.hpp`, `lynxer/ast.cpp`,
      `lynxer/shell.cpp`). Windows DLL loading stays out (Linux/POSIX-only).
- [x] ~~**L2d — multiple live string results.**~~ **Not needed:** no bundled
      module returns more than one string per call; variable-length results use
      the handle registry (`multiprocessing`) or `bytes`, so the single
      thread-local result stays and no ABI surface is added.

**Modules**

- [x] ~~**L4 — `graphics`: offscreen rendering.**~~ **Partly delivered:** with no
      window the module now rasterizes on the CPU — `clearBackground`,
      `drawRectangle(_Lines)`, `drawLine`, `drawTriangle(_Lines)`, `drawCircle(_Lines)`
      fill a framebuffer, and `screenshot` writes it as a PNG, pre-flipped so the
      file has the same top-left origin the drawing ops use. New fixture
      `stdlib_graphics_raster` runs **in CI** (unlike the display-gated
      `stdlib_graphics`) and asserts individual pixels through the `image`
      module.
- [x] ~~**L4b — `graphics` parity beyond the primitives.**~~ **Delivered:** the
      headless rasterizer now draws every shape primitive (ellipse, arc, regular
      polygons, hexagons and rotated rectangles on top of the existing
      rectangles/lines/circles/triangles), CPU text through `fontdue` with the
      bundled ProggyClean face, texture blits (`drawTexture*`, including source
      regions and rotation), offscreen render targets
      (`renderTarget`/`setRenderTarget`/`endRenderTarget`/`renderTargetTexture`/
      `getScreenData`) and a stacked headless layout for the `ui*` widgets. A
      **pre-existing off-by-one** in the `drawTexture`/`drawTextureScaled`/
      `drawTextureRegion`/`drawTextureRotated` (and `uiGroupBegin`,
      `setTextLabelPos`) exports — the leading handle was read as the first
      float — was found and fixed. Covered by the CI fixture
      `stdlib_graphics_raster_shapes`. **Remaining is hardware, not work:**
      shaders/materials, the 3D `drawCube`/`Sphere`/`Plane`/`Grid` primitives and
      model/mesh loading cannot run without a GPU; the frame-callback
      restriction and nested UI blocks are unchanged. Recorded in
      [docs/limitations.md](docs/limitations.md#graphics--constrained-behavior).
- [x] ~~**L5 — `game` physics.**~~ **Partly delivered:** `updatePhysics` now
      resolves horizontal collision against the wall list (`vx` was ignored
      entirely before), rides a wall sprite whose `angle` is non-zero as a
      slope, and treats a third `makePhysicsEngine` list as one-way platforms.
      Covered by `stdlib_game` (walk into a wall, jump up through a platform,
      land on it, rest on a 30° ramp).
- [ ] **L5b — `game` tilemap artwork and the one-shot sound flag.** Slicing the
      tileset atlas needs a real texture, so it cannot be verified headless; and
      a finished one-shot still reports as playing because `macroquad`'s `Sound`
      exposes neither a playback position nor a duration — detecting it means
      decoding durations with `symphonia` (as `sound` already does), which is
      also unverifiable on a host with no audio device
      (`lynxer/rust/game/src/extras.rs`, `Cargo.toml`).
- [x] ~~**L6 — `server` TLS.**~~ **Partly delivered:** `runHTTPS(cert, key)` and
      `runSSLAdhoc()` start a real HTTPS listener on the `init` host/port, built
      on `axum-server` + `rustls` with the **`ring`** provider (already in the
      tree — no `aws-lc-rs`, so no CMake, and no system OpenSSL), plus
      `rustls-pemfile` for the PEM files and `rcgen` for the self-signed
      certificate. PEM problems are reported before the socket is bound. New
      fixture `stdlib_server_tls` completes a real handshake with `curl` (and is
      skipped when curl/openssl are missing).
- [x] ~~**L6b — `server` request context and templates.**~~ **Delivered (with
      one documented residue):** templates are now rendered by a
      Jinja-compatible engine (`lynxer/rust/server/src/template.rs`) — `{{ }}`
      with filters, `{% if %}`/`{% elif %}`/`{% else %}`, `{% for %}` with the
      `loop` variable, `{% set %}` and `{# comments #}`, over expressions with
      comparisons/boolean/arithmetic operators, `in`, `~` and `range()` — and a
      template is rendered **inside** the request, reading it through a
      `request` object (`args`/`headers`/`cookies`/`body`/`json`/`form`/`method`/
      `path`); the query arguments also overlay the root context. Covered by
      `stdlib_server` (a template route exercised through the HTTP client) and
      five unit tests. **Residue:** `getArg`/`getHeader`/`getBody` still describe
      the most recent request, because a Lynxer route is a fixed string, not a
      callback, and the interpreter evaluates one frame at a time — making
      *those* readers per-request needs route callbacks that run during the
      request (a larger change to the server/interpreter boundary, tracked as
      part of **L15**'s runtime work).
- [x] ~~**L7 — `re`/`regex` engine.**~~ **Delivered:** both modules were rewritten
      as Rust `cdylib`s (`rust/re`, `rust/regex`) over a shared `rust/regex_engine`
      built on `fancy-regex` — Rust and cargo, no system PCRE. Lookbehind
      (including variable length), atomic groups, possessive quantifiers,
      backreferences, `\p{…}` property escapes and positional inline flags all
      work, and `(?x)`/the `X` flag is honoured. The C++ backend (`re.cpp`,
      `regex.cpp`, `native_regex.hpp`) is gone; a 98-case differential harness
      proved the pre-existing surface byte-identical.
- [x] ~~**L8 — `sound`.**~~ **Delivered:** `loadSound` now decodes into memory
      while `loadSoundStreaming` reads only the header — a real static/streaming
      split — and both cache the duration at load time, so `getSoundLength`
      never re-opens the file (it used to answer `0.0` once the file moved).
      The one-shot flag needed no change: `isSoundPlaying` already reports
      `false` once a sink drains, which is what `sink.empty()` tests. Covered by
      three unit tests in `rust/sound` that build a scratch WAV, so they need no
      audio device — the platform fixture is skipped on hosts without one.
- [x] ~~**L9 — `watch`.**~~ **Delivered:** a blocking `watchWait(handle,
      timeoutMs)` — it polls the watch descriptor with the interpreter lock
      released, through a new `blocking` service on `LynxerHostApi`, so it does
      not wedge other Lynxer threads (1 = readable, 0 = timeout, -1 = unknown
      handle). The module was split into backends: inotify on Linux and a
      `kqueue` `EVFILT_VNODE` backend on macOS and the BSDs (recursive
      registration, rescan on directory writes); the kqueue crate builds under
      `--target aarch64-apple-darwin`. Windows has no pollable descriptor and
      stays out, consistent with the POSIX-only native loading (**L2c**).
      Covered by `stdlib_watch` (adds the `watchWait` cases) and three unit
      tests in `rust/watch`.
- [x] ~~**L10 — `sqldb` connection handles.**~~ **Partly delivered:** `open()`
      returns an integer handle and `close()` releases it; the ten `*On` forms
      (`queryOn`, `executeArgsOn`, …) reuse that one live connection instead of
      opening per call. Covered by `stdlib_sqldb`; docs and the contracts row
      updated. The error half is **L10b**, blocked on the language.
- [ ] **L10b — `sqldb` raises instead of returning `"ERROR: …"`.** Blocked:
      Lynxer has no exceptions, so the in-band `"ERROR: <message>"` sentinel is
      still the contract. Revisit once the language can raise.
- [x] ~~**L11 — `js` / `multiprocessing`.**~~ **Delivered:** both apply a
      configurable subprocess timeout (`LYNXER_JS_TIMEOUT` default 30,
      `LYNXER_MP_TIMEOUT` default 300) and capture `stderr` into the result
      instead of inheriting it. Commands already run as real processes (a
      `popen` shell per worker); the threads are only the pool. Covered by the
      `stdlib_js` and `stdlib_multiprocessing` fixtures.
- [x] ~~**L12 — `text`/`typing` Unicode semantics.**~~ **Delivered:** the string
      builtins are code-point aware — `returnLength`, `charAt`, `substring` and
      `charCode`/`charOf` (which now accepts the whole range up to `0x10FFFF`)
      — so `returnLength("café")` is 4 and `text.reverse("café")` no longer
      splits a multi-byte character. A byte that cannot start a valid sequence
      counts as one code point, so any byte string still has a length. Case
      conversion stays ASCII-only. Covered by `stdlib_text` and `stdlib_typing`.
- [x] ~~**L13 — `tui` table row separators.**~~ **Delivered:** `tableSetLines`
      now draws real `├───┼───┤` rules — one under the header and one between
      every pair of rows — instead of widening the column gap. `ratatui`'s
      `Table` has no separators and lays rows contiguously, so the rows are
      copied into a taller buffer with a blank line where each rule goes.
      Covered by `stdlib_tui`.
- [ ] **L13b — `tui` live displays and `printException`.** `progress`/`live`
      updates still print a snapshot rather than animating (that needs a
      refresh timer and a terminal), and `printException` prints a placeholder
      because no exception context crosses the module ABI
      (`lynxer/rust/tui/*`).
- [x] ~~**L14 — remove the `os` Python-compat stubs.**~~ **Partly delivered:**
      `os.getPythonVersion`, `os.getPythonImplementation` and the `python`/
      `pythonImplementation`/`pythonExecutable` fields of `getSystemInfo` are
      gone (with the now-unused executable-path helper), and
      `docs/removed-features.md` records it. `sys.exit()` already runs the
      interpreter's only process-wide cleanup, the `atexit` hook that removes a
      compiled executable's temporary directory, so that half needed no code.
- [x] ~~**L14b — the `path` `encoding` argument.**~~ **Delivered:** the argument
      is no longer ignored. `utf-8` (also the default), `latin-1` and `ascii` are
      honoured in pure C++ — no `iconv`, no system library — and an unknown
      encoding, or a character the encoding cannot represent, is the failure
      sentinel rather than a silent UTF-8 read/write. Covered by `stdlib_path`.
- [x] ~~**L14c — `os`/`path`/`sys` platform follow-ups.**~~ **Delivered:** the
      `sys` system-information opers now have portable backends —
      `cpuCount`/`pageSize` share `sysconf`, `loadAverage` shares `getloadavg`,
      and `memoryTotal`/`memoryAvailable`/`uptime`/`bootTime` read `sysctl`
      (`hw.memsize`, free-page count, `kern.boottime`) on macOS and
      FreeBSD/DragonFly; `executablePath` uses `_NSGetExecutablePath` on macOS
      and `platform()` reports the BSDs. And `argv`/`getArg`/`argCount` now
      describe the **program's own** command line: the interpreter passes it to
      the module through a new `program_args` field on `LynxerHostApi`, so entry
      0 is the script path (or the compiled executable) followed by the
      arguments after it, instead of the `/proc/self/cmdline` reading. (The
      macOS/BSD branches cannot be built on this host; they are `#if`-guarded and
      the Linux path is unchanged.)

**Runtime and concurrency**

- [ ] **L15 — `nativeThread*`/`async*`.** Move past the single recursive-mutex
      GIL to true parallelism (per-environment locking or a scheduler) and/or a
      genuine coroutine `await` instead of a join (`lynxer/ast.cpp:1751`,
      `lynxer/builtins.cpp:4842`, `:6534`).

**Retained removals (no plan).** `venv`, the `rawPy`/`rawPyx`/
`cleanRawPyxCache`/`embedPy` family, Python runtime introspection and bytecode
are removals of Python-runtime features, not constraints of Lynxer; re-adding
them would contradict the project. They stay recorded in
[docs/removed-features.md](docs/removed-features.md).

### Planned modules

None outstanding. The eight items that were listed here — `network` URL
operations, `crypto`, `compress`, `toml`, `ini`, `xml`, `yaml` and `watch` — are
implemented and recorded under [Done](#done). The repo's usual artifacts apply to
every module — see **D7**.

`turtle` (**L17**) is now implemented on top of `graphics`. The one remaining
legacy GUI module, `tkinter`/`tkinterPlus`, is **not** implemented and needs a
decision before it could be:

- [ ] **L16 — `tkinter` / `tkinterPlus`.** A native OS-widget toolkit. Under the
      Rust-and-cargo rule that means `egui`/`iced` (`gtk`/`qt` bindings would be
      system libraries), which is an immediate-mode paradigm of its own rather
      than a widget tree — decide whether that is worth a second GUI stack
      beside `graphics`, or record it as declined.
- [x] ~~**L17 — `turtle`.**~~ **Delivered:** `stdlib/turtle.lynx` provides the
      classic API — `forward`/`back`/`turn`/`turnLeft`/`turnRight`/`goto`/
      `home`/`penUp`/`penDown`/`setColor`/`setWidth`, with `x`/`y`/`heading`/
      `penIsDown` readable — drawing through the `graphics` shape ops so it
      works headless. Because Lynxer has no module-level mutable state, the
      state machine lives in `graphics` as a handle (the same handle pattern as
      every other stateful module): handle `0` is an implicit default turtle the
      wrapper drives, and `graphics.turtleCreate` hands out explicit handles.
      Covered by the headless `stdlib_turtle` fixture, which reads the drawn
      pixels back.

### Module decisions

- [x] **D1 - binary payloads over the module ABI.** **Resolved:** (a)+(c) —
      in-memory payloads cross as text/base64 and arbitrary bytes go through
      `*File` operations. Original options were: Lynxer values are
      int64/double/bool/string/list, but compression and crypto need arbitrary
      bytes. Options: (a) base64 and hex strings - `encoding` now provides
      both, though a decode to text fails for a non-UTF-8 payload, (b) lists of
      integers (slow), (c) file-path APIs for bulk plus base64 in memory, (d) a
      real `bytes` value type in the interpreter (cleanest, largest change).
      Recommended: start with (a)+(c) and evaluate (d) later. Still blocks
      `compress` and `crypto`.
- [x] **D2 - one structured-value bridge.** **Resolved:** a JSON string is the
      one bridge (`serde_json::Value` on the Rust side), shared by `json`,
      `toml`, `ini`, `xml` and `yaml`; the Lynxer side uses `listJson*`. `json` already maps a document onto
      Lynxer values; TOML/YAML/XML/INI must reuse that mapping rather than
      invent four. Name it once and document it.
- [x] **D3 - module granularity.** **Resolved:** one module per capability
      (`crypto`, `compress`, `watch`) and one per format (`toml`, `ini`, `xml`,
      `yaml`). One each of `compress`, `crypto`,
      `encoding`, `uuid` and `watch`, but one module per document format.
      Alternative: a single `formats` module.
- [x] **D4 - offline builds and C toolchains.** **Resolved:** the whole
      workspace (all 21 members) shares one committed `Cargo.lock`, so a build
      resolves to exact, reproducible crate versions offline. A C compiler is
      required in addition to `cargo`, because `zstd` (`zstd-sys`), vendored Lua
      (`mlua`) and bundled SQLite (`rusqlite`) compile C with `cc`. A crate that
      needs a *system* library must be added to **both** CI workflows;
      `libasound2-dev` (for `sound`/`cpal`), `libffi-dev` and `pkg-config` are
      installed identically in `build-lynxer-amd.yml` and `build-lynxer-arm.yml`.
      The ARM workflow additionally installs the aarch64 cross toolchain (needed
      only to cross-build ARM) and pins Python for the contract check. `bzip2` is
      not a dependency, so that clause has no current bearer.
- [x] **D5 - dependency vetting.** **Resolved:** manifests use caret ranges and
      the committed `Cargo.lock` freezes the exact resolved versions, so
      `cargo update` is an explicit act rather than an accident — versions are
      pinned by the lock, not by `=x.y.z` in a manifest. The one crate
      substitution is YAML: `serde_yml`, a maintained fork of the archived
      `serde_yaml`, recorded in `lynxer/rust/yaml/Cargo.toml` and
      [docs/stdlib/yaml.md](docs/stdlib/yaml.md). The remaining non-pure-Rust
      crates (`zstd`, `mlua`, `rusqlite`, `libffi`) are accepted deliberately for
      what they provide.
- [x] **D6 - parser security posture.** **Resolved:** the policy is a size/expansion
      limit plus an in-band guard on every parser that takes unbounded input, and
      it is implemented where it matters. `compress` caps a decompress at 64 MiB
      (`read_limited`) and refuses archive path traversal (`is_safe_name`,
      `enclosed_name`, `unpack_in`); `yaml` caps input at 1 MiB to bound alias
      expansion; `xml` never resolves an external entity; `crypto` compares MACs
      with `subtle`'s constant-time `ct_eq`. Fixtures cover the traversal and
      constant-time guards. The residual hardening — capping `zipRead`/`zipExtract`,
      giving `xml` a size limit, and adding dedicated limit/XXE failure fixtures —
      is tracked under
      [Parser and checker hardening](#parser-and-checker-hardening).
- [x] **D7 - required artifacts per module.** **Resolved:** verified for all 19
      Rust-backed modules — the wrapper `lynxer/stdlib/<name>.lynx`; the crate
      under `lynxer/rust/<name>/`; the name in `LYNXER_RUST_MODULE_NAMES`
      (`Makefile:25`); the `OPS` table and `export_*!` bodies kept in `lib.rs`
      so `check_module_contracts.py` sees them; a fixture
      `lynxer/examples/stdlib_<name>.lynx` + `.expected`; `docs/stdlib/<name>.md`;
      a row in `docs/stdlib-contracts.md`; notes in `docs/limitations.md`; and a
      regenerated site page.
- [x] **D8 - contract-checker compatibility.** **Resolved:**
      `lynxer/scripts/check_module_contracts.py`, run by `make testLynxer`,
      enforces the packed-argument bound (a wrapper must not read an
      `args.<kind>(i)` index it did not pass) and that every
      `global.native<Alias>.<op>(...)` call names a registered op; both rules are
      fatal and currently pass (0 errors across 34 backends). An op registered but
      never called is a non-fatal warning — dead code, not a contract break — and
      calls whose argument kinds cannot be inferred are skipped; tightening those
      is under [Parser and checker hardening](#parser-and-checker-hardening).

## Named syscalls — policy for new wrappers

Every name the original documented, plus the extended and Stretch sets, is
implemented; there are no open syscall items. New wrappers follow these rules:

**Call syntax.** Named syscalls are architecture-gated: a program calls
`syscalls("amd64")` (or `"arm64"`; `x86-64` / `aarch64` are aliases) once to
select the host architecture, then reaches every wrapper through that namespace
— `amd64.syscallRead(...)`. Only one architecture can be selected, the keyword
must name the machine, and a misspelled keyword or namespace prefix answers with
`You meant: <closest>?`. `global.sys.architecture()` returns the keyword to pass.

**Portability rule.** A new wrapper must work on **both** Linux `amd64` and
`aarch64` from the same source: resolve the number in `syscallNumberFor`, guard
each mapping with `#ifdef SYS_<name>` so an older header set degrades to the
"not available on this architecture" error instead of the wrong table, and keep
every name available on both arches — where the raw call differs, split only the
number and argument fix-up, not the name (the portable `poll`/`epoll` wrappers
are the model). No `syscall*` name is architecture-exclusive. Prefer a libc
wrapper when the raw call is an unstable ABI (e.g. `clone`/`clone3`). Because
call sites name an architecture, an architecture-agnostic fixture carries a
`__ARCH__` token that the Makefile substitutes per host (`SYSCALL_ARCH`).

## Open decisions

None. Every module decision — the binary payload ABI, the shared value bridge,
module granularity, dependency vetting, offline builds, the parser security
posture, the per-module artifact set and the contract checker — is resolved
under [Module decisions](#module-decisions). The execution work that remains is
the [limitation plans](#resolving-documented-limitations).

## Done

### Parser and checker hardening

Follow-ups from **D6** (parser security) and **D8** (contract checker), completed:

- [x] ~~**H1 — cap archive extraction.** `zipRead`/`zipExtract` route through the
      shared 64 MiB `read_limited`, and `zipExtract` rejects an entry whose
      declared size is over the cap before writing it. `tarExtract`/`tarGzExtract`
      enforce a total declared-size budget, and the tar.gz stream itself is read
      through a limiting reader, so a crafted archive cannot exhaust memory or
      disk.~~
- [x] ~~**H2 — an `xml` input cap.** `xmlParse`/`xmlValid` refuse a document and
      `xmlSerialize` a JSON tree over 16 MiB, before parsing.~~
- [x] ~~**H3 — dedicated `.expected` failure fixtures.** `stdlib_compress_limits`
      (64 MiB cap across all five codecs, the ZIP read/extract cap and the tar.gz
      expansion cap), `stdlib_yaml_limits` (1 MiB input cap) and
      `stdlib_xml_limits` (16 MiB cap and the no-XXE guarantee).~~
- [x] ~~**H4 — tighten the contract checker.** Wrapper parameters with default
      values are now inferred correctly (all 43 skipped `graphics` calls are
      checked), and an unused registration is fatal. The check reports 1147
      calls checked, 0 skipped, 0 errors.~~
- [x] ~~**Extra: reject an LZ4 bomb before it allocates.** `lz4_flex` reads a
      4-byte little-endian uncompressed-length prefix and allocates that much;
      the decoder now validates the prefix against the 64 MiB cap first.~~

- [x] ~~**`async*` semantics.** Decided: real tasks, not a coroutine runtime.
      `asyncRun` starts a worker thread and returns a task handle; `await` joins
      a task handle (pass-through otherwise); `asyncGather` joins the handles
      among its arguments; `asyncSleep` releases the interpreter lock.
      `async.name(){}` stays eager.~~
- [x] ~~**`nativeThread*` interleaving.** Decided: keep the global interpreter
      lock (globals are unsynchronized), but release it at every blocking,
      joining and yield point — including the new `nativeThreadYield(seconds?)`
      — so a worker can make progress while the main body runs. Pre-emption
      would be a data race and is not attempted.~~
- [x] ~~**`tui` real backend.** Built on `ratatui` (`lynxer/rust/tui`), with
      `pulldown-cmark` for `markdown` and `syntect` for `printSyntax`. Every
      render operation draws a widget into an offscreen buffer and prints it —
      plain text without a TTY, ANSI styling with one — so output is
      deterministic in tests. Prompts read stdin and fall back to defaults at
      EOF; `enter()`/`exit()` gate raw mode on a TTY. The `table*`, `tree*`,
      `layout*`, `progress*`, `status*` and `live*` families keep real state
      behind integer handles, and the module exposes lists, tabs, bar charts,
      sparklines, calendars, JSON trees, gauges, CSV tables, terminal control,
      key input, selection prompts (`select`/`multiselect`/`editor`), a
      full-screen display and theme selection.~~
- [x] ~~**`ffi*` calling convention.** Decided: take the dependency. The
      native-call engine now lives in Rust (`lynxer/rust/ffi`) on the `libffi`
      crate and is linked into the interpreter as a C-ABI `staticlib`
      (`lynxer/ffi_abi.h`), so `cargo` is required and a fixed signature table
      is gone. The signature grammar (`cdecl:<ret>(<args>)`) is unchanged, but
      any combination of `int64`/`float64`/`cstring` parameters — plus `void`
      returns and the packed `...` form — now works. `ffiLoadLibrary` /
      `ffiLookup` / `ffiCloseLibrary` stay `dlopen`/`dlsym`, and `ffiCallback`
      still wraps a Lynxer function without a native closure.~~
- [x] ~~The rest of the original `docs-legacy` surface: pointers/raw addresses,
      atomics and volatile access, native synchronization, `memoryProtect`, the
      `nativeModule*` handles, `ffi*`, typed `memory*` accessors, the managed
      `filesystem*` / `process*` / `networking*` / `sound*` families, and
      `nativeThread*`.~~
- [x] ~~Language: ownership/borrowing (`varTransfer`/`varBorrow`/…, `shared`,
      `unshare()`), the `async*` family, and bracket-literal tuple rebinding
      (accepted, with a once-per-location deprecation warning).~~
- [x] ~~Stdlib modules ported from `docs-legacy`: `typing`, `text`, `csv`,
      `regex`, `image`, `game`, `server`, `network`, `cli` (native Click/Typer
      builders), and `mathPlus` (merged into `math`).~~
- [x] ~~`sys` system information: `architecture`, `cpuCount`, `pageSize`,
      `memoryTotal`, `memoryAvailable`, `uptime`, `bootTime`, `loadAverage`.~~
- [x] ~~Named syscalls: the original set plus the extended and Stretch families
      (io_uring, Landlock/seccomp), architecture-gated and available on both
      arches.~~
- [x] ~~Range `for` loops:
      `for (int i = start (.. | ..=) end [.. step])` beside the C-style form.
      See [docs/loops.md](docs/loops.md).~~
- [x] ~~`cli` builders: `click*` / `typer*` over integer handles — with
      `--help`/`-h`, `--no-<flag>` negation, `noArgsHelp`, repeated options and
      env-var defaults — and removal of the Python availability probes.~~
- [x] ~~Absorbing the legacy docs: the remaining `docs-legacy` reference pages
      (`async`, `filesystem`, `networking`, `process`, `native-memory`,
      `native-modules`) now live under `docs/`, and every claim was verified
      against the interpreter. That audit corrected
      [docs/legacy-surface.md](docs/legacy-surface.md): the `async` language
      syntax **does** exist (it is eager, not a coroutine).~~
- [x] ~~**`uuid` module.** Built on the `uuid` crate (`rust/uuid`) with the
      `v3`, `v4`, `v5` and `v7` features: 14 operations covering generation
      (random v4, time-ordered v7, name-based v3/v5, nil), parsing and
      formatting (hyphenated, simple, `urn:uuid:` and braced), the version
      nibble, the variant, the Unix timestamp, the hex view of the 16 bytes, and
      the well-known namespaces. UUIDs cross as strings; failures are the
      scalar sentinel (`""`, `false`, `-1`). Decisions taken: no opaque handle,
      and no v1/v6 or byte-buffer entry points, so the plan's
      `uuidToBytes`/`FromBytes` became `uuidToHex`/`uuidFromHex` (the simple
      32-hex-character form, the only lossless text view of the bytes).
      `uuidParse` and `uuidFormat` both exist, the first being
      `uuidFormat(s, false)`.~~
- [x] ~~**`encoding` module.** Built on `base64`, `hex`, `data-encoding`,
      `bs58`, `ascii85`, `percent-encoding` and `quoted_printable`
      (`rust/encoding`): 25 operations, an `*Encode`, `*Decode` and `*Valid`
      entry point for each of base64 (standard and URL-safe), hex, base32,
      base58, ascii85, percent-encoding and quoted-printable. Decoding is strict
      apart from optional padding (and either case in base32); a decode returns
      `""` on failure, including when the decoded bytes are not valid UTF-8,
      because Lynxer has no byte type yet.~~
- [x] ~~**`graphics` module.**~~ Built: a Rust **macroquad**-backed drawing,
  window, input and immediate-mode UI module (`rust/graphics`), separate from
  `game` rather than a replacement for it. `iced` is not in the vendored crate
  set, so macroquad is the vehicle.

- [x] ~~**Planned modules.** All eight implemented on the shared JSON bridge
      (D2): `network` URL operations (`urlIsValid`, `urlJoin`, `urlNormalize`,
      `urlGet`, `urlSet*`, `urlQuery*`, `urlEncodeComponent`,
      `urlDecodeComponent`, and an escaped `urlParse`); `crypto` (hashes, HMAC,
      constant-time comparison, OS randomness, Ed25519); `compress`
      (gzip/zlib/zstd/brotli/lz4 plus ZIP/TAR, with extract traversal and size
      guards); `toml`, `ini`, `xml` and `yaml`; and `watch` (inotify, delivered
      through the interpreter's poll set). Decision **D1** is text/base64 plus
      file paths; **D3** is one module per capability and one per format; the
      crates per module are recorded in
      [docs/limitations.md](docs/limitations.md).~~

## Ground rules

- Keep everything under `lynxer/`; documentation in `docs/`, the website in
  `site/`.
- C++17 and the standard library only, unless a native dependency is explicitly
  chosen. The Rust backends and the FFI engine are required: a missing `cargo`
  or a failed `cargo build` is a hard error, never a skip.
- Prefer explicit, source-located errors over partial support.
- Add a focused `.lynx` fixture under `lynxer/examples/` for every user-visible
  feature, with a sibling `.expected`, and keep `make testLynxer` green.
- Regenerate the website with `python3 site/build.py` after editing `docs/`.
