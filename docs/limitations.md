# Planned implementation gaps

This page summarizes known gaps that are intended to be addressed. Each entry
has an implementation task in [todo.md](../todo.md). Intentional product
boundaries, removed features and compatibility decisions are documented in
[removed-features.md](removed-features.md).

## Native modules

- `compress` does not enable bzip2 or encrypted ZIP support; zstd builds
  vendored C sources and needs a C compiler.
- `crypto` supports a fixed set of digest names and Ed25519 signatures only;
  conventional digest aliases are not all accepted.
- The kqueue `watch` backend reports the registered path rather than individual
  changed entries where the OS does not expose them, and is not covered by CI.
- TOML, INI, XML and YAML use a JSON document bridge. TOML datetimes become
  strings, and YAML mappings with non-string keys cannot be represented.
- `regex` and `re` report byte offsets; callers needing Unicode character
  positions have no corresponding API.
- Text case conversion and `typing.isAlpha` / `isDigit` are ASCII-only.
- `path` supports only UTF-8, Latin-1 and ASCII text encodings. The remaining
  system-information backends are not implemented on every POSIX target.
- `js` requires Node.js on `PATH`; subprocess timeout, stderr and
  result-handle behavior need consistent cross-module tests.
- Timestamped debug log output has no deterministic fixture coverage.
- The TUI prints snapshots instead of animating live displays, and
  `printException` cannot show the original exception context.
- Image operations use inconsistent return/value representations; grayscale
  alpha, metadata formatting and format detection need compatibility coverage.
- SQLite BLOBs are rendered as base64 in JSON, and scalar/query result
  conventions need a documented, lossless value representation.
- Lua error output preserves engine diagnostics, but does not expose stable
  structured error classifications.

## Test and compatibility coverage

- JSON and CSV edge cases lack one explicit compatibility contract and
  round-trip suite: non-finite numbers, key order, line endings, non-string
  values and ragged rows.
- The regex engines need cross-module tests for Unicode offsets, capture and
  replacement behavior, and malformed patterns.
