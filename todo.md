# Lynxer — TODO

Approved work to remove or reduce the implementation gaps recorded in
[docs/limitations.md](docs/limitations.md). Deliberate non-goals and removed
features are in [docs/removed-features.md](docs/removed-features.md).

## Language

- [x] **File-scoped macros and function codeblocks.** Add macros using the
  syntax `macro macroName!(*args){//code}`. Macros are visible only within
  their declaring file unless prefixed with `pub` (`pub macro
  macroName!(*args){//code}`), which makes them visible to files that import
  the declaring file. Macros must accept caller-supplied code blocks as
  arguments. Support caller-supplied code blocks on `func` declarations too.
  Cover macro expansion, `pub` visibility across imports, and code-block
  arguments with fixtures.

## `graphics` and `game`

- [x] **GPU-operation lifecycle.** Context-dependent graphics operations now
  return consistent failure sentinels outside registered frame callbacks;
  headless CPU paths remain available.
- [x] **Nested headless UI groups.** Nested windows and groups are buffered,
  replayed and rasterized recursively, with fixture coverage for nested layout
  and widget values.
- [x] **Game maps and collision.** TMX parsing supports multiple tilesets,
  common layer encodings and compressed base64 data. Sprite collision handles
  rotation, and physics carries grounded players with moving/rotating
  platforms while retaining existing wall, slope and one-way behavior.
- [x] **Game/audio playback state.** Sound-playing queries use backend sink
  state where audio is available, with tests for completion, looping, stopping
  and unavailable-device behavior.

## `server`

- [x] **Per-request route context.** Added named Lynxer callback routes that
  run under the interpreter lock, with thread-local request readers and
  explicit response construction; fixed-string routes remain supported.
- [x] **Template safety and completeness.** Added HTML output escaping and
  dangerous URL-scheme filtering, suppression in active HTML attributes,
  include/inheritance/macro rendering, and explicit errors for unknown
  variables.
- [x] **TLS validation.** Certificate/private-key mismatch is rejected before
  binding, with unit and TLS fixture coverage.

## Native modules

- [x] **Compression formats.** `compress` enables bzip2 and zstd streams and
  AES-encrypted ZIP entries (a manifest entry may carry a `password`; empty
  passwords are refused). Decompression keeps the 64 MiB cap and extraction
  keeps path confinement. Round-trip, malformed-input and encrypted-archive
  coverage is in the crate unit tests and the `stdlib_compress` fixture.
- [x] **Crypto digest aliases.** `crypto` normalizes conventional digest names
  (case- and separator-insensitive, e.g. `SHA-256`, `sha_256`, `sha3_256`) for
  hashes and HMACs, while malformed names stay invalid. Ed25519 remains the
  signature algorithm, MAC comparison stays constant-time and random output
  stays bounded. Covered by unit tests and the `stdlib_crypto` fixture.
- [x] **Watch backend fidelity.** The kqueue backend infers the changed child
  entry from directory snapshots where the OS reports only the directory
  descriptor, through a shared helper that the Linux tests exercise; the
  kqueue wiring itself stays BSD-gated and outside the Linux CI host.
- [x] **Structured document round-trips.** TOML datetimes round-trip through a
  reserved tagged JSON object (`$lynxer.toml.datetime`) instead of becoming
  quoted strings, and any YAML mapping with a non-string key fails the whole
  document rather than being silently coerced. TOML, INI, XML and YAML gained
  round-trip fixtures.
- [x] **JSON and CSV edge cases.** JSON rejects non-finite numbers and preserves
  key insertion order; CSV accepts CR, LF and CRLF input, emits CRLF, follows
  the header width for short and ragged rows, and renders non-string values as
  compact JSON scalars. Both have explicit round-trip fixtures.
- [x] **Regex Unicode offsets.** `re` and `regex` keep byte offsets and add
  `matchStartChar` / `matchEndChar` / `findSpansChar` and `firstMatchCharPos`,
  which count Unicode scalar values (exclusive end). Unicode matching,
  captures, replacements and invalid patterns are covered for both engines.
- [x] **Unicode text operations.** A new Rust `text` backend provides Unicode
  case conversion and alpha/numeric/digit predicates; the existing ASCII
  helpers remain (`upperAscii`, `isAlphaAscii`, …). The `stdlib_text` fixture
  covers multi-byte and combining characters.
- [x] **Path encoding and platform information.** `path` reads and writes the
  UTF-8 default plus Latin-1 and common Windows code pages (including
  UTF-16LE), and exposes portable `platform` / `separator` / `listSeparator`
  values. Unknown encodings and invalid input are tested without changing the
  UTF-8 default.
- [x] **Portable `sys` information.** The remaining POSIX backends return real
  values (CPU count, page size, memory totals and availability, uptime, boot
  time, load average) instead of placeholders, with available-value and
  sentinel coverage in the `stdlib_sys` fixture.
- [x] **Subprocess result management.** `js` and `multiprocessing` share a
  documented timeout/stderr/result-handle contract: timeouts kill the whole
  process group, stdout and stderr are captured in pipe order, and released or
  unknown handles return their invalid-result sentinels. They are tested
  together, including missing-runtime and failed-process cases.
- [x] **Deterministic debug logging tests.** The `debug` backend accepts
  `LYNXER_DEBUG_TEST_EPOCH` and `LYNXER_DEBUG_TEST_CLOCK_MS` test clock
  overrides, and the `stdlib_debug` fixture asserts timestamped levels without
  wall-clock flakes.
- [x] **TUI exception context.** The wrapper forwards `exceptionInfo()` through
  the native-module string argument, so `printException` shows the formatted
  traceback and original error message.
- [x] **Interactive TUI behavior.** A live terminal backend drives displays,
  selection prompts and terminal controls while redirected runs keep the
  deterministic snapshot output; both TTY and non-TTY behavior are covered.
- [x] **Image API consistency.** Image pixels and metadata use the shared
  JSON/value conventions, with grayscale alpha, mutating operations and format
  reporting covered by the `stdlib_image` fixture.
- [x] **SQLite value fidelity.** SQLite BLOBs round-trip as `bytes` through an
  explicit tagged value (and a `bytes:` scalar prefix) instead of ambiguous
  base64 JSON, with NULL, numeric, text and BLOB round-trip coverage.
- [x] **Lua error classification.** `lua` exposes stable structured error kinds
  while retaining the engine diagnostic and traceback, with syntax and runtime
  failure tests.

## Planning rule

Keep a task only while implementation is intended. Keep hardware, security,
platform-policy, compatibility and deliberate semantic constraints that are
outside this plan in [docs/removed-features.md](docs/removed-features.md), not
as implied future work.

## Completed

- [x] **Typed aggregate ABI.** Added `cdecl:v2:value(value)` with recursive
  representations for scalar values, bytes, lists, tuples, named records and
  enum payloads. Existing `cdecl:` signatures remain unchanged. Nested C++
  round-trip coverage and C++/Rust ABI examples document the new shape.
- [x] **Interpreter cleanup on `sys.exit()`.** `sys.exit()` and `cli.exit()`
  now request shutdown through the host API and unwind through interpreter
  scopes and managed thread cleanup before returning the requested status,
  including exit requests raised from worker threads. Direct and
  compiled-executable fixtures cover both exit APIs and thread cleanup.
- [x] **Export functions to a C ABI.** A program can declare
  `export "cdecl:<ret>(<args>)" name(...) { ... }` and `lynxer --emit-library`
  builds a shared library whose typed `extern "C"` wrappers marshal through a new
  embedding runtime (`liblynxer.so`, `lynxer/lynxer.h`). Covers `int64`,
  `float64`, `cstring`, `bytes` and `void` in both directions with located
  validation errors. Exercised by a C++ and a Python `ctypes` consumer in
  `make testLynxerEmit`, wired into the CI gate.
- [x] **Windowed graphics CI.** The AMD64 workflow now starts Xvfb with Mesa
  llvmpipe software OpenGL and runs bounded windowed smoke fixtures for both
  `graphics` and `game`. The graphics fixture loads and draws a real texture,
  captures a frame, and checks its dimensions; both fixtures assert that the
  window opened and that update/draw callbacks ran. Headless CI remains a
  separate test path.
