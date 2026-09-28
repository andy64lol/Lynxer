# Lynxer — TODO and non-goals

Open work for the standalone C++ implementation in `lynxer/`. Completed work is
struck through under [Done](#done), and the permanent non-goals and removed
features are recorded in [docs/limitations.md](docs/limitations.md) and
[docs/removed-features.md](docs/removed-features.md) rather than duplicated
here. The named-syscall reference is [docs/syscalls.md](docs/syscalls.md).

Any name still registered in `unsupportedTable()` reports
`<name>() is not supported in Lynxer yet`.

## Open work

### Modules

- [ ] `tkinter` / `tkinterPlus` / `turtle` — the `graphics` module (below)
      covers the immediate-mode drawing/window/UI space; an OS-native widget
      binding remains out of scope.

### Planned modules

Seven new capabilities, none implemented yet. Each entry lists the proposed
crates, the operations to expose, and the decisions it is blocked on. The
repo's usual artifacts apply to every one of them - see **D7**.

- [ ] **`encoding` (Rust).** Base64 (standard and URL-safe, padded or not),
      hex, base32, base58, ascii85, percent-encoding and quoted-printable.
      Proposed crates: `base64`, `hex`, `data-encoding` (base32 plus custom
      alphabets), `bs58`, `ascii85`, `percent-encoding`, `quoted_printable`.
  - Ops: `base64Encode`/`Decode`, `base64UrlEncode`/`Decode`,
    `hexEncode`/`Decode`, `base32Encode`/`Decode`, `base58Encode`/`Decode`,
    `ascii85Encode`/`Decode`, `percentEncode`/`Decode`,
    `quotedPrintableEncode`/`Decode`.
  - Decide: strict or lenient decoding (whitespace, missing padding) and the
    exact error text. The URL *codec* belongs here; URL *semantics* belong in
    `network`.
  - Cheapest module, and it settles **D1** option (a): implement it first.

- [ ] **`uuid` (Rust).** Generate, parse and format UUIDs. Crate: `uuid` with
      the `v4`, `v7`, `v5`, `v3` and `rng` features.
  - Ops: `uuidV4`, `uuidV7`, `uuidV3(namespace, name)`,
    `uuidV5(namespace, name)`, `uuidNil`, `uuidParse`, `uuidFormat`,
    `uuidValid`, `uuidVersion`, `uuidVariant`, `uuidToBytes`/`FromBytes`,
    `uuidTimestamp`, plus the standard namespace constants (`dns`, `url`,
    `oid`, `x500`).
  - Decide: string round-trip versus an opaque handle; whether the deprecated
    v1/v6 variants are in scope (recommended: no).

- [ ] **`network` - URL operations (extend the existing module).** Add the
      `url` crate (already a transitive dependency of `ureq`, so likely already
      in `Cargo.lock`).
  - Ops: `urlParse`, `urlIsValid`, `urlJoin(base, ref)`, `urlNormalize`,
    `urlGet(url, part)`, `urlSetScheme`/`Host`/`Port`/`Path`,
    `urlQueryGet`/`Set`/`Remove`/`Append`,
    `urlEncodeComponent`/`DecodeComponent`.
  - Decide: IDN/punycode support via `idna`; the trailing-slash normalisation
    policy; structured handle versus JSON return.

- [ ] **`crypto` (Rust)** - hashes, MACs, signatures and secure random.
      Proposed crates: `sha2`, `sha1`, `md-5`, `sha3`, `blake3`, `digest`,
      `hmac`, `subtle` (constant-time compare), `rand`/`getrandom` (OS
      entropy), `ed25519-dalek`, and `rsa` + `p256`/`ecdsa` with `pkcs8`/`pem`
      for key material.
  - Ops: `hash(name, data)`, `hashFile`, `hmac(name, key, data)`,
    `verifyHmac` (constant time), `constantTimeEquals`, `randomBytes(n)`,
    `randomHex(n)`, `randomToken(n)`, `generateEd25519KeyPair`, `signEd25519`,
    `verifyEd25519`, then RSA/ECDSA verify and PEM/DER import/export.
  - Decide: the v1 algorithm set (recommended: the SHA-2 family, SHA-1 and MD5
    as *hashes only*, BLAKE3, HMAC-SHA256 and Ed25519, with RSA/ECDSA second);
    key representation (PEM strings versus raw base64); whether password
    hashing (`argon2`/`scrypt`/`bcrypt`) is a separate follow-up.
  - Blocked by **D1**.

- [ ] **`compress` (Rust)** - gzip/zlib, zstd, brotli, lz4, plus ZIP and TAR
      archives. Proposed crates: `flate2` (gzip/zlib/deflate, pure-Rust
      `miniz_oxide` backend), `brotli` (pure Rust), `lz4_flex` (pure Rust),
      `zstd` (vendors C sources; `cc` is already required), `zip`, `tar`.
  - Ops: `gzipCompress`/`Decompress`, `zlibCompress`/`Decompress`,
    `zstdCompress`/`Decompress`, `brotliCompress`/`Decompress`,
    `lz4Compress`/`Decompress`, `zipCreate`/`List`/`Read`/`Write`/`Extract`,
    `tarCreate`/`List`/`Extract`, `tarGzCreate`/`Extract`.
  - Decide: whole-buffer versus streaming APIs; decompression limits (a
    zip-bomb guard and a maximum output size); path-traversal rejection on
    extract (`../` and absolute entries) and the overwrite policy; whether
    `bzip2` is in scope.
  - Blocked by **D1**.

- [ ] **`toml` (Rust).** `toml` + `serde`; `toml_edit` only if preserving
      formatting and comments is wanted.
- [ ] **`ini` (Rust).** `rust-ini`.
- [ ] **`xml` (Rust).** `quick-xml` for reading and writing (pure Rust; it
      resolves no external entities, so XXE is not reachable by default);
      `roxmltree` if a read-only DOM is wanted.
- [ ] **`yaml` (Rust).** `serde_yaml` is archived, so pick a maintained crate
      (`serde_yml`, or `saphyr`/`yaml-rust2`) and verify maintenance before
      committing the lockfile.
  - Decide (all four): one module per format (matching the existing `json`
    module, and keeping each `.so` small) versus a single `formats` module;
    parse-only versus parse-and-serialize in v1; how duplicate and nested keys
    map onto Lynxer values.
  - Security: all four parse untrusted input, so require input-size limits,
    YAML alias/expansion limits, defined duplicate-key behaviour, and XML
    entity-expansion limits - each with a failure fixture.
  - Blocked by **D2**.

- [ ] **`watch` (Rust)** - filesystem change events. Crate: `notify` (inotify
      on Linux), optionally `notify-debouncer-mini` for coalescing.
  - Ops: `watchAdd(path, recursive)`, `watchRemove`, `watchClose`,
    `watchFd(handle)`, `watchDrain(handle)`, `watchWait(timeoutMs)`,
    `watchSetDebounce(ms)`.
  - Decide: **how events reach Lynxer.** A module cannot call back into the
    interpreter by itself, so the recommended delivery is the existing poll
    set: `watchFd()` returns the inotify descriptor, the program registers it
    with `asyncPollRegister` and waits with `asyncPollWait` (which already
    releases the interpreter lock around `poll(2)`), then calls `watchDrain()`
    for the paths. Alternatives: a blocking `watchWait`, or a real callback
    once the Rust ABI grows a "call a Lynxer function" hook.
  - Also decide: the event-kind vocabulary, debounce/burst behaviour, queue
    limits, and what happens when a watched directory disappears.

Suggested order: `encoding` -> `uuid` -> `network` URL ops -> `crypto` ->
`compress` -> `toml` -> `ini` -> `xml` -> `yaml` -> `watch`. The first three
need no new ABI decision, and `encoding` settles **D1** option (a).

### Module decisions

- [ ] **D1 - binary payloads over the module ABI.** Lynxer values are
      int64/double/bool/string/list, but compression and crypto need arbitrary
      bytes. Options: (a) base64 strings (simple, +33% size, needs `encoding`
      first), (b) lists of integers (slow), (c) file-path APIs for bulk plus
      base64 in memory, (d) a real `bytes` value type in the interpreter
      (cleanest, largest change). Recommended: start with (a)+(c) and evaluate
      (d) later. Blocks `compress` and `crypto`.
- [ ] **D2 - one structured-value bridge.** `json` already maps a document onto
      Lynxer values; TOML/YAML/XML/INI must reuse that mapping rather than
      invent four. Name it once and document it.
- [ ] **D3 - module granularity.** One each of `compress`, `crypto`,
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
- [x] ~~**`graphics` module.**~~ Built: a Rust **macroquad**-backed drawing,
  window, input and immediate-mode UI module (`rust/graphics`), separate from
  `game` rather than a replacement for it. `iced` is not in the vendored crate
  set, so macroquad is the vehicle.

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
