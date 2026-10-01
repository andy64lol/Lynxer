# Lynxer — TODO

Approved work to remove or reduce the implementation gaps recorded in
[docs/limitations.md](docs/limitations.md). Deliberate non-goals and removed
features are in [docs/removed-features.md](docs/removed-features.md).

## Runtime and native-module ABI

- [ ] **Typed aggregate ABI.** Define direct typed representations for common
  aggregates instead of requiring JSON strings or handles. Add ABI conformance
  tests for nested values and update both C++ and Rust examples. Preserve
  existing signatures through a versioned compatibility path.
- [ ] **Interpreter cleanup on `sys.exit()`.** Route the language-level exit
  through orderly interpreter shutdown so registered cleanup and resource
  releases run. Test normal return, `sys.exit()` and compiled executables.

## `graphics` and `game`

- [ ] **GPU-operation lifecycle.** Make context-dependent operations safe to
  request outside a frame callback, with documented deferred execution or a
  clear error rather than inconsistent failures. Cover windowed and headless
  modes.
- [ ] **Nested headless UI groups.** Support nested `uiWindowBegin` /
  `uiGroupBegin` blocks in the headless layout stack and add fixture coverage
  for nested layout and widget values.
- [ ] **Windowed graphics CI.** Add a display-backed CI job for the windowed
  renderer and run its existing graphics fixture separately from headless CI.
- [ ] **Game maps and collision.** Support multiple tilesets and common TMX
  layer encodings, then add rotation-aware and moving-platform collision while
  retaining existing wall, slope and one-way-platform behavior. Add headless
  fixtures for map selection and collision cases.
- [ ] **Game/audio playback state.** Make sound-playing queries reflect actual
  backend playback state where available, and add deterministic tests for
  completion, looping, stopping and unavailable-device behavior.

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
