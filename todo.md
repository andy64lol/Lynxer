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

- [ ] **Per-request route context.** Add route callbacks evaluated in the
  request so `getArg`, `getHeader`, `getBody` and related readers refer to the
  current request, not the most recently completed request. Test concurrent
  requests and retain fixed-string route compatibility.
- [ ] **Template safety and completeness.** Add context-sensitive auto-escaping,
  template includes/inheritance/macros, and explicit errors for unknown
  variables. Test HTML escaping and template composition without changing the
  existing supported syntax.
- [ ] **TLS validation.** Detect certificate/private-key mismatch before
  binding the HTTPS listener and add a negative TLS fixture.

## Native modules

- [ ] **Compression formats.** Add optional bzip2, zstd and encrypted ZIP
  support where dependencies can be built consistently on supported targets.
  Keep decompression-size limits and extraction path confinement; cover each
  enabled format with round-trip and malformed-input fixtures.
- [ ] **Crypto digest aliases.** Normalize conventional digest-name aliases
  and test case, separator and invalid-name handling. Keep Ed25519 as the
  supported signature algorithm, constant-time MAC verification and bounded
  random generation.
- [ ] **Watch backend fidelity.** Make kqueue events identify changed entries
  where the OS API permits and add macOS/BSD coverage for the existing backend.
- [ ] **Structured document round-trips.** Preserve format-specific values
  that JSON cannot represent (including TOML datetimes), and define handling
  for YAML non-string keys without silent data loss. Add round-trip fixtures
  for TOML, INI, XML and YAML.
- [ ] **JSON and CSV edge cases.** Define and test round-trip behavior for
  non-finite JSON numbers, object key order, CSV line endings, non-string
  values, and rows whose width differs from the header. Preserve existing
  behavior unless a concrete compatibility requirement justifies a change.
- [ ] **Regex Unicode offsets.** Provide character-index results alongside the
  existing byte offsets and add cross-module tests for Unicode matching,
  captures, replacements and invalid patterns.
- [ ] **Unicode text operations.** Add Unicode case conversion and
  Unicode-aware alpha/digit predicates, keeping current ASCII helpers
  available for compatibility. Cover multi-byte and combining characters.
- [ ] **Path encoding and platform information.** Expand the supported text
  encodings and provide portable path/platform information on each supported
  host. Test invalid input and missing host values without changing the
  existing UTF-8 default.
- [ ] **Portable `sys` information.** Implement the remaining system
  information backends on supported POSIX targets rather than returning
  placeholders, and test both available values and unavailable-value errors.
- [ ] **Subprocess result management.** Specify and test timeout, stderr,
  process termination and result-handle lifetime behavior consistently for
  `js` and `multiprocessing`, including missing-runtime and failed-process
  cases.
- [ ] **Deterministic debug logging tests.** Inject or control the clock in
  tests so timestamped log levels can be asserted without wall-clock flakes.
- [ ] **TUI exception context.** Carry formatted exception context through the
  native-module ABI so `printException` can show the original error details.
- [ ] **Interactive TUI behavior.** Add an interactive terminal backend for
  live displays, selection prompts and terminal controls while preserving the
  deterministic redirected-output behavior. Cover both TTY and non-TTY runs.
- [ ] **Image API consistency.** Standardize image pixel and metadata
  representations with the shared JSON/value conventions, and add API
  compatibility tests for mutating operations, grayscale alpha and format
  reporting.
- [ ] **SQLite value fidelity.** Preserve SQLite BLOB values as `bytes` rather
  than base64-only JSON strings and define consistent JSON/scalar query output.
  Add round-trip tests for NULL, numeric, text and BLOB values.
- [ ] **Lua error classification.** Expose stable structured error kinds while
  retaining the underlying Lua diagnostic and traceback, with tests for syntax
  and runtime failures.

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
- [x] **Windowed graphics CI.** The AMD64 workflow now starts Xvfb with Mesa
  llvmpipe software OpenGL and runs bounded windowed smoke fixtures for both
  `graphics` and `game`. The graphics fixture loads and draws a real texture,
  captures a frame, and checks its dimensions; both fixtures assert that the
  window opened and that update/draw callbacks ran. Headless CI remains a
  separate test path.
