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

- [ ] **L1 — first-class `bytes` value type.** Removes the "No byte type"
      constraint in `compress`, `crypto`, `encoding`, `uuid` and `math`, and the
      base64/hex/TSV workarounds built on it; also unlocks byte parameters in
      the native ABI and UTF-8-correct round-trips. Thread a `Bytes` variant
      through `lynxer/runtime.hpp` (Value), `lynxer/parser.cpp` (a `bytes`
      keyword), `lynxer/types.cpp`, `lynxer/runtime.cpp`
      (coercion/render/compare), `lynxer/ffi_abi.h`,
      `lynxer/rust/abi/src/lib.rs`, `lynxer/rust/ffi/src/lib.rs` and
      `lynxer/stdlib/lynxer_native_abi.h`. `byte`/`uint8` stay 0..255 integers —
      do not conflate them with the new type.
- [ ] **L2 — extend the native-module ABI.** Allow aggregate (`list`/`bytes`)
      parameters and multiple live string results, raise the 64-argument packed
      cap, and support `.so`/DLL loading beyond Linux (`lynxer/stdlib/lynxer_native_abi.h`,
      `lynxer/rust/abi`, `lynxer/rust/ffi`, `lynxer/ffi_abi.h`). While here,
      reconcile the "fixed shapes … at most four arguments" wording in
      `docs/limitations.md` with `docs/native-module-abi.md`, which states there
      is no fixed shape table (a standing contradiction).
- [ ] **L3 — replace the tab-separated list bridge** in `math` (and any other
      `listJoin("\t")` crossing) with the real list/bytes ABI
      (`lynxer/stdlib/math.cpp:379`, `lynxer/stdlib/math.lynx:224`).

**Modules**

- [ ] **L4 — `graphics`: offscreen rendering.** Add a render-target readback or
      software-rasterizer path so drawing, `screenshot`, textures, fonts and the
      `ui*` widgets work without a window; relax the frame-callback restriction
      where the GPU context permits; allow nested UI blocks; add gamepad and
      model/mesh loading; add a CI-runnable headless render fixture
      (`lynxer/rust/graphics/*`, `Cargo.toml`).
- [ ] **L5 — `game`.** Slice the tileset atlas so tilemaps render artwork, not
      just geometry; add horizontal collision and slope/one-way platform support
      to `updatePhysics`; stop reporting a finished one-shot as playing
      (`lynxer/rust/game/src/extras.rs`, `state.rs`).
- [ ] **L6 — `server`.** Build TLS for `runHTTPS`/`runSSLAdhoc`
      (`tokio-rustls`/`rustls-pemfile`/`rcgen`); give routes a per-request
      context so `getArg`/`getHeader`/`getBody` describe the current request; add
      a Jinja-compatible template engine (`lynxer/rust/server/src/lib.rs`,
      `server/Cargo.toml`).
- [ ] **L7 — `re`/`regex` engine.** Replace `std::regex` (ECMAScript) with a
      PCRE-compatible engine — PCRE2, or Rust `fancy-regex` — for lookbehind,
      atomic groups, `\p{…}`, positional inline flags and `(?x)` verbose mode
      (`lynxer/stdlib/native_regex.hpp`, `re.cpp`, `regex.cpp`).
- [ ] **L8 — `sound`.** Split static load from streaming load; do not report a
      finished one-shot as playing; cache the decoded length instead of
      re-decoding on every call (`lynxer/rust/sound/src/lib.rs`).
- [ ] **L9 — `watch`.** Add portable backends (kqueue/Windows) and a blocking
      `watchWait` (`lynxer/rust/watch/src/lib.rs`).
- [ ] **L10 — `sqldb`.** Add connection handles (`open`/`close`/reuse) instead of
      opening per call, and raise errors once exceptions exist rather than
      returning in-band `"ERROR: …"` (`lynxer/rust/sqldb/src/lib.rs`).
- [ ] **L11 — `js` / `multiprocessing`.** Apply subprocess timeouts, capture
      `stderr`, and use real processes (not worker threads) for
      `multiprocessing` (`lynxer/stdlib/js.cpp`, `multiprocessing.cpp`).
- [ ] **L12 — `text`/`typing` Unicode semantics** (depends on L1). Make
      `returnLength`, `charAt`, `substring` and the `charCode`/`charOf` builtins
      code-point aware instead of byte-oriented (`lynxer/runtime.cpp`,
      `lynxer/builtins.cpp`).
- [ ] **L13 — `tui` parity.** Close the feasible Rich-parity gaps (manual table
      row separators, animated progress/live displays, a real `printException`
      traceback via exception context); record the rest as intentional
      divergence (`lynxer/rust/tui/*`).
- [ ] **L14 — `os`/`path`/`sys`.** Remove the Python-compat stubs once nothing
      depends on them; honour or drop the ignored `encoding` argument; add
      portable system-info backends; make `sys.exit()` run interpreter cleanup;
      distinguish program from process `argv` (`lynxer/stdlib/os.cpp`, `path.cpp`,
      `sys.cpp`, `lynxer/builtins.cpp`).

**Runtime and concurrency**

- [ ] **L15 — `nativeThread*`/`async*`.** Move past the single recursive-mutex
      GIL to true parallelism (per-environment locking or a scheduler) and/or a
      genuine coroutine `await` instead of a join (`lynxer/ast.cpp:1751`,
      `lynxer/builtins.cpp:4842`, `:6534`).

**Language-surface omissions**

- [ ] **L16 — `tkinter`/`tkinterPlus`.** Add a native OS-widget GUI module (Rust
      `egui` + `tao`/`wry`, or a GTK/Qt binding), replacing the "no GUI toolkit"
      non-goal.
- [ ] **L17 — `turtle`.** Reimplement on top of `graphics` (pen/line drawing)
      rather than the unmaintained `turtle` crate.
- [ ] **L18 — `http`/`net`.** Decision: keep them superseded by `network` +
      `server` (recommended — record as resolved) or add a compatibility shim.

**Retained removals (no plan).** `venv`, the `rawPy`/`rawPyx`/
`cleanRawPyxCache`/`embedPy` family, Python runtime introspection and bytecode
are removals of Python-runtime features, not constraints of Lynxer; re-adding
them would contradict the project. They stay recorded in
[docs/removed-features.md](docs/removed-features.md).

### Modules

- [ ] `tkinter` / `tkinterPlus` / `turtle` — tracked as **L16**–**L17** under
      [Resolving documented limitations](#resolving-documented-limitations).

### Planned modules

None outstanding. The eight items that were listed here — `network` URL
operations, `crypto`, `compress`, `toml`, `ini`, `xml`, `yaml` and `watch` — are
implemented and recorded under [Done](#done). The repo's usual artifacts apply to
every module — see **D7**.

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
- [ ] **D4 - offline builds and C toolchains.** Every new crate needs a
      committed `Cargo.lock`; `zstd` (and `bzip2`, if added) vendor C sources
      and need a C compiler, which the build already requires. A crate that
      needs a *system* package must be added to both CI workflows.
- [ ] **D5 - dependency vetting.** Prefer maintained, pure-Rust crates, pin
      versions, and record any substitution (notably YAML) in `docs/`.
- [ ] **D6 - parser security posture.** Each parser takes untrusted input:
      size/expansion limits, archive path-traversal guards, no XML external
      entities, YAML expansion limits, and constant-time comparison in
      `crypto`. Every guard gets a `.expected` failure fixture.
- [ ] **D7 - required artifacts per module.** The wrapper
      `lynxer/stdlib/<name>.lynx`; the crate under `lynxer/rust/<name>/`; the
      name in `LYNXER_RUST_MODULE_NAMES`; the `OPS` table and all `export_*!`
      bodies kept in `lib.rs` so `check_module_contracts.py` sees them; a
      fixture `lynxer/examples/stdlib_<name>.lynx` + `.expected`;
      `docs/stdlib/<name>.md`; a row in `docs/stdlib-contracts.md`; notes in
      `docs/limitations.md`; and a regenerated site.
- [ ] **D8 - contract-checker compatibility.** A wrapper call must not read
      argument indices (per kind) beyond what it passes, and every registered
      op must be called by the wrapper.

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

Work that needs a decision before it can be built.

The open decisions for the planned modules — the binary payload ABI, the shared
value bridge, module granularity, dependency vetting and the parser limits —
are [Module decisions](#module-decisions), listed beside the modules they gate.
Everything else is resolved; see [Done](#done).

## Done

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
