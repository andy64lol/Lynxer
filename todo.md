# Lynxer — TODO

Approved work to remove or reduce the implementation gaps recorded in
[docs/limitations.md](docs/limitations.md). Deliberate non-goals and removed
features are in [docs/removed-features.md](docs/removed-features.md).

## Language

- [x] ~~**File-scoped macros and function codeblocks.** Add macros using the
  syntax `macro macroName!(*args){//code}`. Macros are visible only within
  their declaring file unless prefixed with `pub` (`pub macro
  macroName!(*args){//code}`), which makes them visible to files that import
  the declaring file. Macros must accept caller-supplied code blocks as
  arguments. Support caller-supplied code blocks on `func` declarations too.
  Cover macro expansion, `pub` visibility across imports, and code-block
  arguments with fixtures.~~

## `graphics` and `game`

- [x] ~~**GPU-operation lifecycle.** Context-dependent graphics operations now
  return consistent failure sentinels outside registered frame callbacks;
  headless CPU paths remain available.~~
- [x] ~~**Nested headless UI groups.** Nested windows and groups are buffered,
  replayed and rasterized recursively, with fixture coverage for nested layout
  and widget values.~~
- [x] ~~**Game maps and collision.** TMX parsing supports multiple tilesets,
  common layer encodings and compressed base64 data. Sprite collision handles
  rotation, and physics carries grounded players with moving/rotating
  platforms while retaining existing wall, slope and one-way behavior.~~
- [x] ~~**Game/audio playback state.** Sound-playing queries use backend sink
  state where audio is available, with tests for completion, looping, stopping
  and unavailable-device behavior.~~

## `server`

- [x] ~~**Per-request route context.** Added named Lynxer callback routes that
  run under the interpreter lock, with thread-local request readers and
  explicit response construction; fixed-string routes remain supported.~~
- [x] ~~**Template safety and completeness.** Added HTML output escaping and
  dangerous URL-scheme filtering, suppression in active HTML attributes,
  include/inheritance/macro rendering, and explicit errors for unknown
  variables.~~
- [x] ~~**TLS validation.** Certificate/private-key mismatch is rejected before
  binding, with unit and TLS fixture coverage.~~

## Native modules

- [x] ~~**Compression formats.** `compress` enables bzip2 and zstd streams and
  AES-encrypted ZIP entries (a manifest entry may carry a `password`; empty
  passwords are refused). Decompression keeps the 64 MiB cap and extraction
  keeps path confinement. Round-trip, malformed-input and encrypted-archive
  coverage is in the crate unit tests and the `stdlib_compress` fixture.~~
- [x] ~~**Crypto digest aliases.** `crypto` normalizes conventional digest names
  (case- and separator-insensitive, e.g. `SHA-256`, `sha_256`, `sha3_256`) for
  hashes and HMACs, while malformed names stay invalid. Ed25519 remains the
  signature algorithm, MAC comparison stays constant-time and random output
  stays bounded. Covered by unit tests and the `stdlib_crypto` fixture.~~
- [x] ~~**Watch backend fidelity.** The kqueue backend infers the changed child
  entry from directory snapshots where the OS reports only the directory
  descriptor, through a shared helper that the Linux tests exercise; the
  kqueue wiring itself stays BSD-gated and outside the Linux CI host.~~
- [x] ~~**Structured document round-trips.** TOML datetimes round-trip through a
  reserved tagged JSON object (`$lynxer.toml.datetime`) instead of becoming
  quoted strings, and any YAML mapping with a non-string key fails the whole
  document rather than being silently coerced. TOML, INI, XML and YAML gained
  round-trip fixtures.~~
- [x] ~~**JSON and CSV edge cases.** JSON rejects non-finite numbers and preserves
  key insertion order; CSV accepts CR, LF and CRLF input, emits CRLF, follows
  the header width for short and ragged rows, and renders non-string values as
  compact JSON scalars. Both have explicit round-trip fixtures.~~
- [x] ~~**Regex Unicode offsets.** `re` and `regex` keep byte offsets and add
  `matchStartChar` / `matchEndChar` / `findSpansChar` and `firstMatchCharPos`,
  which count Unicode scalar values (exclusive end). Unicode matching,
  captures, replacements and invalid patterns are covered for both engines.~~
- [x] ~~**Unicode text operations.** A new Rust `text` backend provides Unicode
  case conversion and alpha/numeric/digit predicates; the existing ASCII
  helpers remain (`upperAscii`, `isAlphaAscii`, …). The `stdlib_text` fixture
  covers multi-byte and combining characters.~~
- [x] ~~**Path encoding and platform information.** `path` reads and writes the
  UTF-8 default plus Latin-1 and common Windows code pages (including
  UTF-16LE), and exposes portable `platform` / `separator` / `listSeparator`
  values. Unknown encodings and invalid input are tested without changing the
  UTF-8 default.~~
- [x] ~~**Portable `sys` information.** The remaining POSIX backends return real
  values (CPU count, page size, memory totals and availability, uptime, boot
  time, load average) instead of placeholders, with available-value and
  sentinel coverage in the `stdlib_sys` fixture.~~
- [x] ~~**Subprocess result management.** `js` and `multiprocessing` share a
  documented timeout/stderr/result-handle contract: timeouts kill the whole
  process group, stdout and stderr are captured in pipe order, and released or
  unknown handles return their invalid-result sentinels. They are tested
  together, including missing-runtime and failed-process cases.~~
- [x] ~~**Deterministic debug logging tests.** The `debug` backend accepts
  `LYNXER_DEBUG_TEST_EPOCH` and `LYNXER_DEBUG_TEST_CLOCK_MS` test clock
  overrides, and the `stdlib_debug` fixture asserts timestamped levels without
  wall-clock flakes.~~
- [x] ~~**TUI exception context.** The wrapper forwards `exceptionInfo()` through
  the native-module string argument, so `printException` shows the formatted
  traceback and original error message.~~
- [x] ~~**Interactive TUI behavior.** A live terminal backend drives displays,
  selection prompts and terminal controls while redirected runs keep the
  deterministic snapshot output; both TTY and non-TTY behavior are covered.~~
- [x] ~~**Image API consistency.** Image pixels and metadata use the shared
  JSON/value conventions, with grayscale alpha, mutating operations and format
  reporting covered by the `stdlib_image` fixture.~~
- [x] ~~**SQLite value fidelity.** SQLite BLOBs round-trip as `bytes` through an
  explicit tagged value (and a `bytes:` scalar prefix) instead of ambiguous
  base64 JSON, with NULL, numeric, text and BLOB round-trip coverage.~~
- [x] ~~**Lua error classification.** `lua` exposes stable structured error kinds
  while retaining the engine diagnostic and traceback, with syntax and runtime
  failure tests.~~

## Windows support

Lynxer runs on Linux and on Windows through the MSYS2 MinGW-w64 and CLANGARM64
builds; the native-module ABI, `--compile` bundling, embedding runtime and
`--emit-library` all have Windows implementations. The Linux-only named-syscall
surface stays a documented boundary in
[removed-features.md](docs/removed-features.md), not a Windows gap.

- [x] ~~**Build system and toolchain.** The `Makefile` detects the host
  (`LYNXER_HOST_OS`/`LYNXER_ON_WINDOWS`), builds `lynxer.exe`, drops the
  POSIX-only flags and supplies the Rust staticlib's native imports; the MSYS2
  MinGW-w64 and CLANGARM64 paths build and test green. It also builds
  `lynxer.dll` with a `liblynxer.def` export list and the `lynxer.dll.a` import
  library. Still to do: a native MSVC/clang-cl path.~~
- [x] ~~**Portable host layer.** Added `lynxer/platform.hpp`/`platform.cpp`,
  which centralizes the running-executable path, dynamic loading, subprocess
  spawn, temporary directories, the executable bit and the install link, with
  POSIX and `_WIN32` implementations; the core calls it, and `.dll` is
  recognized as a native library. Linux behavior is unchanged. Process-group
  termination and path-separator normalization remain.~~
- [x] ~~**Native-module ABI on Windows.** `.dll` (and `.so`-named) modules load
  through `platform::openLibrary` with the same `lynxer_module_init_v1` entry
  point and `cdecl:` grammar. The C++ module rule passes
  `-Wl,--export-all-symbols` on Windows so every registered op is resolvable
  under both MinGW-w64 and CLANGARM64: `examples/native_signatures.cpp` builds
  and its fixture passes on Windows. The `.def`/`__declspec` exports and the
  32-bit `__stdcall` convention ride with the MSVC path in the toolchain item.~~
- [x] ~~**Embedding and `--emit-library`.** The runtime builds as `lynxer.dll`
  (using `liblynxer.def` instead of the ELF version script), and
  `--emit-library` emits a `.dll` plus its `.dll.a` import library and generated
  header. `make sdk` and `--install` stage the DLL, import library and public
  headers. Windows resolves the runtime through the DLL search path rather than
  an `rpath`; the workflows compile and run a C++ consumer against an emitted
  DLL.~~
- [x] ~~**`--compile` bundling.** The payload is appended to the running image and
  read back through `platform::executablePath()` (`GetModuleFileNameW`), keeping
  the materialize-to-temp-dir behavior for embedded modules and assets; the
  compiled-executable parity loop passes on Windows.~~
- [x] ~~**Stdlib platform matrix.** Portable as-is: `json`, `toml`, `yaml`,
  `ini`, `xml`, `regex`, `text`, `math`, `csv`, `random`, `time`, `fileIO`,
  `shell`, `cli`, `debug`, `os` and `path` (these now build with Windows APIs).
  `watch` uses `ReadDirectoryChangesW` (with `watchFd` returning `-1` on
  Windows). `tui` uses crossterm (Windows console), `graphics`/`game` use
  macroquad (Windows OpenGL plus a headless CPU raster path), and `sound` uses
  cpal/rodio (WASAPI, tolerating a missing device). All three Rust backends
  pass `cargo check --target x86_64-pc-windows-gnullvm`, and the Windows CI
  builds their cdylibs. `js` and `multiprocessing` use `CreateProcess` with
  Job Object timeout cleanup. Excluded from the Windows build (named by
  `LYNXER_WINDOWS_SKIP_MODULES`): `sys` and the syscall built-ins, plus the
  `lowlevel_*` fixtures. Startup code lives on the host layer; see
  [docs/windows.md](docs/windows.md).~~
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
  `<windows.h>`. The named Linux syscalls stay Linux-only. Since the first
  slice, process control, console, file attributes/paths, Known Folders,
  high-resolution timers, shell open/dialogs, clipboard, tray notifications,
  system/locale/time-zone information and blocking named pipes have landed.
  Still to do:
  - Grow the table further: sockets (`WSAStartup`, `socket`, … — needs
    `ws2_32`), registry wrappers over `advapi32` (the managed `winreg` module
    covers the common cases) and first-class `HANDLE` values.
  - Cover the Win32 specifics: the `__stdcall` calling convention on 32-bit
    (unified on x64), so the `cdecl:` signature grammar needs a `stdcall:` or
    convention-aware counterpart; UTF-16 `*W` strings (handled internally) and
    the `A`/`W` pairs; `HANDLE`/`HWND`/`SOCKET` handles; `BOOL` results with a
    separate `GetLastError` code; and struct layout/packing.
  - Keep the Linux syscall surface a Linux-only boundary: do not emulate
    syscall numbers on Windows. The selector takes the operating system as well
    as the architecture; revisit whether the `amd64.syscallX` namespace should
    gain the OS segment too.
- [x] ~~**Windows terminal behavior.** `platform::enableVirtualTerminal()` switches
  the console to `ENABLE_VIRTUAL_TERMINAL_PROCESSING` at startup, so `tui` and
  `graphics` escape sequences render. Redirection is unaffected: the console-mode
  call is skipped when the stream is not a console.~~
- [x] ~~**Tests and CI.** Both Windows jobs are required (no
  `continue-on-error`):
  [.github/workflows/build-lynxer-windows-amd64.yml](.github/workflows/build-lynxer-windows-amd64.yml)
  (MSYS2 MINGW64) and
  [.github/workflows/build-lynxer-windows-arm64.yml](.github/workflows/build-lynxer-windows-arm64.yml)
  (MSYS2 CLANGARM64). Both jobs also build `lynxer --emit-library` and run the
  C++ consumer against the emitted DLL. Every skipped module and fixture is
  listed in [docs/windows.md](docs/windows.md).~~
- [x] ~~**Documentation.** `docs/windows.md` records the port, its exclusions and
  the skipped test groups; `install.md` and `CLI.md` carry the Windows install
  notes; `README.md` links the Windows workflows; and the syscall surface is
  documented as Linux-only and target-gated by operating system.~~

- [x] ~~**Process control and groups.** `platform::spawnProcess` now creates
  the child suspended in a kill-on-close Job Object before it runs, so a
  timed-out or killed core child takes its whole tree down; if the assignment
  is refused (a non-nesting parent job), the child still runs. `js` and
  `multiprocessing` already use `CreateProcess` with Job Object timeout
  cleanup (see the stdlib platform matrix above).~~
- [x] ~~**Path and long-path parity.** `path`/`fileIO` accept both separators
  through `std::filesystem`, prefix absolute paths beyond `MAX_PATH` with the
  `\\?\` extended-length marker for stream and filesystem I/O (including UNC
  shares), and normalize CRLF/lone-CR on Windows text reads. The Linux path is
  unchanged; short paths and values returned to Lynxer are not rewritten.~~
- [x] ~~**Sockets and networking.** The `network` backend uses Rust's
  `std::net`/`std::io`, which maps to Winsock on Windows; the crate cross-builds
  for `x86_64-pc-windows-gnullvm` with `WS2_32` imports, and `getHostname` runs
  through Winsock under Wine. The Lynxer API is unchanged.~~
- [x] ~~**Registry and services.** Added the Windows-only `winreg` module
  (`stdlib/winreg.cpp` + `winreg.lynx`) for `HKCU`/`HKLM`/`HKCR`/`HKU`/`HKCC`
  string/int/enumeration/recursive-delete operations, with a self-cleaning
  `winreg_basic.lynx` fixture. `server` remains a foreground console process on
  Windows; background startup is delegated to Task Scheduler or an external
  SCM wrapper rather than Linux units.~~
- [x] ~~**Native-module authoring on Windows.** Added `LYNXER_MODULE_EXPORT`
  (`__declspec(dllexport)` on Windows) to `stdlib/lynxer_native_abi.h`, used it
  in `examples/native_signatures.cpp`, and documented the MSVC `cl /LD` build
  and `.dll` loading path in `docs/native-module-abi.md`. Every op in the
  example is marked too; the MinGW build was verified locally, and the
  `native-module-msvc` CI job checks both the entry point and an op export.~~
- [ ] **Release binaries, installer and signing.** The release already ships
  prebuilt `lynxer-windows-amd64/arm64.zip`. `packaging/windows/lynxer.iss`
  now builds an Inno Setup installer on the `--install` layout, adds `bin` to
  the user `PATH` and registers an uninstall entry; `stage-payload.sh` prepares
  its input; the AMD64 CI job installs Inno Setup, compiles the installer and
  uploads it as an artifact. Still to do: Authenticode-sign the installer and
  binaries so a download does not trip SmartScreen.
- [ ] **Test parity.** The Windows-only fixtures now include `winreg_basic`,
  `winwatch`, `winapi`, `winapi_pipe`, `winpipe_basic` (the pipe fixtures have
  server/client helpers), plus the portable `stdlib_os_dirs`,
  `stdlib_debug_crash`, `stdlib_compress`/`_limits`, `stdlib_crypto`,
  `stdlib_image_*`, `stdlib_xml_limits`, `stdlib_debug` and `stdlib_fileIO`
  fixtures. [docs/windows.md](docs/windows.md) lists every skipped stem with
  its reason. A locally cross-linked x86_64 build (vendored libffi) runs all of
  these under Wine; the skip list has shrunk to the POSIX shell/path fixtures,
  `stdlib_sys`/`stdlib_js`/`stdlib_watch`, the aggregate suite and the OpenGL
  renderers. Remaining: track amd64/ARM64 parity and re-check the remaining
  skips on a real runner.

- [ ] **Package managers.** `packaging/windows/render-package-manifests.py`
  now renders winget, Scoop and Chocolatey manifests (with SHA-256 hashes) from
  the released `lynxer-windows-*.zip`. Still to do: submit them to the three
  registries once the signed zips are published.
- [x] ~~**C runtime and minimum Windows version.** Windows 10 is the minimum;
  AMD64 uses MSYS2 MINGW64 (`msvcrt.dll` + GCC runtime DLLs), ARM64 uses
  CLANGARM64 (UCRT + CLANGARM64 runtime DLLs). CI collects non-system DLLs with
  `ldd` into `lynxer/runtime`, and release zips/installer payloads ship them
  next to `lynxer.exe`. No Visual C++ Redistributable is required.~~
- [x] ~~**Editor and shell tooling.** Added `editors/vscode` (TextMate grammar,
  `--lint` diagnostics and `--format`), plus native-command completion for
  PowerShell 7 (`editors/powershell/lynxer.ps1`) and CMD through Clink
  (`editors/clink/lynxer.lua`). The extension and completions are published
  from the repository; packaging the VSIX is left to the release job.~~
- [x] ~~**Console I/O and Unicode.** `configureStandardStreams()` sets both the
  input and output console code pages to UTF-8 when the process is attached to a
  console, alongside the existing ANSI/VT mode, so `cli`/`tui` round-trip
  non-ASCII text, colours and `beep` under Windows Terminal and the legacy
  console. Redirected streams keep the binary byte-identical behavior.~~
- [ ] **Asynchronous I/O.** An IOCP backend where POSIX uses `epoll`/`poll`, for
  the async task runtime and the networking paths.
- [x] ~~**Named pipes and Shell integration.** Added the Windows-only `winpipe`
  module (`create`/`connect`/`open`/`read`/`write`/`disconnect`/`peek`/
  `close`) with a server/client fixture, plus low-level `winAPI` pipe calls,
  `shellOpen`, `shellOpenWith`, `messageBox`, clipboard get/set and tray
  `notify`.~~
- [ ] **Services and scheduled tasks.** `packaging/windows/lynxer-task.ps1`
  now installs/removes/starts/inspects a Lynxer program as a Task Scheduler
  task (at logon, or at boot with elevation), keeping `server` a foreground
  console process. Still to do: an optional Service Control Manager wrapper.
- [x] ~~**User data, configuration and diagnostics.** `os.configDir`,
  `os.dataDir` and `os.cacheDir` map to `%APPDATA%`/`%LOCALAPPDATA%` on Windows
  and the XDG base directories on POSIX; `winAPI.getKnownFolderPath` covers
  Desktop/Documents/Downloads and the other Known Folders; the registry reader
  is `winreg`; and `debug.crashLogPath`/`debug.recordCrash` write
  `%LOCALAPPDATA%\Lynxer\crash.log` (overridable with `LYNXER_CRASH_LOG`).~~

## Distribution

- [ ] **Arch User Repository (AUR).** Two PKGBUILDs are ready in
  [`packaging/aur/`](packaging/aur/): `lynxer` (source build) and `lynxer-bin`
  (prebuilt; unpacks the release zip). Still to do: publish them — blocked for
  now, since AUR account registration is temporarily closed and publishing
  needs an AUR account with a registered SSH key.

## Planning rule

Keep a task only while implementation is intended. Keep hardware, security,
platform-policy, compatibility and deliberate semantic constraints that are
outside this plan in [docs/removed-features.md](docs/removed-features.md), not
as implied future work.

## Completed

- [x] ~~**Typed aggregate ABI.** Added `cdecl:v2:value(value)` with recursive
  representations for scalar values, bytes, lists, tuples, named records and
  enum payloads. Existing `cdecl:` signatures remain unchanged. Nested C++
  round-trip coverage and C++/Rust ABI examples document the new shape.~~
- [x] ~~**Interpreter cleanup on `sys.exit()`.** `sys.exit()` and `cli.exit()`
  now request shutdown through the host API and unwind through interpreter
  scopes and managed thread cleanup before returning the requested status,
  including exit requests raised from worker threads. Direct and
  compiled-executable fixtures cover both exit APIs and thread cleanup.~~
- [x] ~~**Export functions to a C ABI.** A program can declare
  `export "cdecl:<ret>(<args>)" name(...) { ... }` and `lynxer --emit-library`
  builds a shared library whose typed `extern "C"` wrappers marshal through a new
  embedding runtime (`liblynxer.so`, `lynxer/lynxer.h`). Covers `int64`,
  `float64`, `cstring`, `bytes` and `void` in both directions with located
  validation errors. Exercised by a C++ and a Python `ctypes` consumer in
  `make testLynxerEmit`, wired into the CI gate.~~
- [x] ~~**Windowed graphics CI.** The AMD64 workflow now starts Xvfb with Mesa
  llvmpipe software OpenGL and runs bounded windowed smoke fixtures for both
  `graphics` and `game`. The graphics fixture loads and draws a real texture,
  captures a frame, and checks its dimensions; both fixtures assert that the
  window opened and that update/draw callbacks ran. Headless CI remains a
  separate test path.~~
