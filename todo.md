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

## Bob (package manager)

`Bob/` is the Lynxer package manager, a separate Rust component with its own
version (`Bob/README.md`). It has no registry yet, so only `bob --ver` and
`bob --init` exist. Its planned work is tracked in [Bob/todo.md](Bob/todo.md).

## Windows support

Lynxer currently targets Linux only: the interpreter, the native-module ABI
(`dlopen`/`.so`), the `--compile` bundler and the syscall-facing modules all
assume a POSIX/Linux host. This plan stages a port; the Linux-only syscall
surface stays a documented boundary in
[removed-features.md](docs/removed-features.md), not a Windows gap.

- [ ] **Build system and toolchain.** The `Makefile` detects the host
  (`LYNXER_HOST_OS`/`LYNXER_ON_WINDOWS`), builds `lynxer.exe`, drops the
  POSIX-only flags and supplies the Rust staticlib's native imports; the MSYS2
  MinGW-w64 and CLANGARM64 paths build and test green. Still to do: a
  `lynxer.dll` embedding runtime, a native MSVC/clang-cl path, and the
  `.def`/`__declspec` export definitions.
- [x] **Portable host layer.** Added `lynxer/platform.hpp`/`platform.cpp`,
  which centralizes the running-executable path, dynamic loading, subprocess
  spawn, temporary directories, the executable bit and the install link, with
  POSIX and `_WIN32` implementations; the core calls it, and `.dll` is
  recognized as a native library. Linux behavior is unchanged. Process-group
  termination and path-separator normalization remain.
- [x] **Native-module ABI on Windows.** `.dll` (and `.so`-named) modules load
  through `platform::openLibrary` with the same `lynxer_module_init_v1` entry
  point and `cdecl:` grammar, and the MinGW-w64/CLANGARM64 toolchains export the
  symbols by default: `examples/native_signatures.cpp` builds and its fixture
  passes on Windows. The `.def`/`__declspec` exports and the 32-bit `__stdcall`
  convention ride with the MSVC path in the toolchain item.
- [ ] **Embedding and `--emit-library`.** Build the runtime as `lynxer.dll`,
  emit `.dll` plus the generated header, and use a Windows export definition
  instead of the ELF version script; resolve the runtime through the DLL search
  path rather than an `rpath`. Until then `--emit-library` fails cleanly on
  Windows instead of emitting ELF-only link flags.
- [x] **`--compile` bundling.** The payload is appended to the running image and
  read back through `platform::executablePath()` (`GetModuleFileNameW`), keeping
  the materialize-to-temp-dir behavior for embedded modules and assets; the
  compiled-executable parity loop passes on Windows.
- [ ] **Stdlib platform matrix.** Portable as-is: `json`, `toml`, `yaml`,
  `ini`, `xml`, `regex`, `text`, `math`, `csv`, `random`, `time`, `fileIO`,
  `shell`, …. Needs a Windows backend: `watch` (`ReadDirectoryChangesW`),
  `tui`/`graphics`/`sound` console and device handling, `cli`/`debug`/`os`/`path`
  (POSIX headers MinGW lacks) and `js`/`multiprocessing` (the `fork`-based
  `subprocess.hpp`, needs `CreateProcess`). Excluded from the Windows build
  (named by `LYNXER_WINDOWS_SKIP_MODULES` and the `watch` filter on
  `LYNXER_RUST_MODULE_NAMES`): `sys` and the syscall built-ins, the modules
  above, plus the `lowlevel_*` fixtures. Startup code lives on the host layer;
  see [docs/windows.md](docs/windows.md).
- [ ] **Windows API access (Win32, not syscalls).** First slice landed:
  selecting the Windows target (`syscalls("winAPI", "amd64")`, with `windows`
  and `win32` as synonyms) enables the `winAPI.*` namespace — all `kernel32`:
  system information (`getProcessId`, `getCurrentDirectory`, `getComputerName`,
  `getTempPath`, `getSystemDirectory`, `getWindowsDirectory`,
  `getModuleFileName`, `getTickCount`, `getLastError`, `getDiskFreeBytes`),
  environment (`getEnvironmentVariable`, `setEnvironmentVariable`,
  `expandEnvironmentStrings`), file/handle access (`createFile`, `readFile`,
  `writeFile`, `closeHandle`, `fileSize`, `seekFile`, `deleteFile`, `copyFile`,
  `moveFile`, `createDirectory`, `removeDirectory`), `sleep`, `beep` and
  `outputDebugString` — with the `winapi.lynx` fixture run by the Windows job.
  The Win32 calls live in `lynxer/winapi.cpp`, so nothing else includes
  `<windows.h>`. The named Linux syscalls stay Linux-only. Still to do:
  - Grow the table further: process and socket calls (`CreateProcessW`,
    `WSAStartup`, ...), console calls, and first-class `HANDLE` values. The long
    tail is already reachable through
    `ffiLoadLibrary`/`ffiLookup`/`ffiCall` ([native-modules.md](docs/native-modules.md)).
  - Cover the Win32 specifics: the `__stdcall` calling convention on 32-bit
    (unified on x64), so the `cdecl:` signature grammar needs a `stdcall:` or
    convention-aware counterpart; UTF-16 `*W` strings (handled internally) and
    the `A`/`W` pairs; `HANDLE`/`HWND`/`SOCKET` handles; `BOOL` results with a
    separate `GetLastError` code; and struct layout/packing.
  - Keep the Linux syscall surface a Linux-only boundary: do not emulate
    syscall numbers on Windows. The selector takes the operating system as well
    as the architecture; revisit whether the `amd64.syscallX` namespace should
    gain the OS segment too.
- [x] **Windows terminal behavior.** `platform::enableVirtualTerminal()` switches
  the console to `ENABLE_VIRTUAL_TERMINAL_PROCESSING` at startup, so `tui` and
  `graphics` escape sequences render. Redirection is unaffected: the console-mode
  call is skipped when the stream is not a console.
- [x] **Tests and CI.** Both Windows jobs are required (no
  `continue-on-error`):
  [.github/workflows/build-lynxer-windows-amd.yml](.github/workflows/build-lynxer-windows-amd.yml)
  (MSYS2 MINGW64) and
  [.github/workflows/build-lynxer-windows-arm.yml](.github/workflows/build-lynxer-windows-arm.yml)
  (MSYS2 CLANGARM64). Every skipped module and fixture is listed in
  [docs/windows.md](docs/windows.md), and Bob has its own four workflows.
- [x] **Documentation.** `docs/windows.md` records the port, its exclusions and
  the skipped test groups; `install.md` and `CLI.md` carry the Windows install
  notes; `README.md` links the Windows workflows; and the syscall surface is
  documented as Linux-only and target-gated by operating system.

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
