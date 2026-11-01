# Lynxer — TODO

Approved work to remove or reduce the implementation gaps recorded in
[docs/limitations.md](docs/limitations.md). Deliberate non-goals and removed
features are in [docs/removed-features.md](docs/removed-features.md).

## Implementation

- [ ] **Migrate most implementation code to Rust.** Plan an incremental
  migration of the interpreter and supporting implementation from C++ to Rust,
  defining component boundaries and preserving the language behavior, native
  module ABI, embedding interfaces, and platform support through tests.

## Windows support

Lynxer runs on Linux and on Windows through the MSYS2 MinGW-w64 and CLANGARM64
builds; the native-module ABI, `--compile` bundling, embedding runtime and
`--emit-library` all have Windows implementations. The Linux-only named-syscall
surface stays a documented boundary in
[removed-features.md](docs/removed-features.md), not a Windows gap.

- [ ] **Windows API access (Win32, not syscalls).** First slice landed:
  the Windows runtime provides the `winAPI.*` namespace — all `kernel32`:
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
  - Keep the Linux syscall module a Linux-only boundary: do not emulate syscall
    numbers on Windows. Revisit whether the `amd64.syscallX` namespace should
    gain an explicit OS segment.
- [ ] **Release binaries, installer and signing.** The release already ships
  prebuilt `lynxer-windows-amd64/arm64.zip`, each containing both the portable
  program and its installer. `packaging/windows/lynxer.iss` builds an AMD64 or
  ARM64 Inno Setup installer on the `--install` layout, adds `bin` to the user
  `PATH` and registers an uninstall entry; `stage-payload.sh` prepares its
  input; both Windows CI jobs install Inno Setup, compile their architecture's
  installer and upload the combined zip. Still to do: Authenticode-sign the
  installer and binaries so a download does not trip SmartScreen.
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

- [ ] **Package manager publishing.** Submit the generated manifests to winget,
  Scoop and Chocolatey after signed release zips are published.
- [ ] **Asynchronous I/O.** An IOCP backend where POSIX uses `epoll`/`poll`, for
  the async task runtime and the networking paths.
- [ ] **Services and scheduled tasks.** `packaging/windows/lynxer-task.ps1`
  now installs/removes/starts/inspects a Lynxer program as a Task Scheduler
  task (at logon, or at boot with elevation), keeping `server` a foreground
  console process. Still to do: an optional Service Control Manager wrapper.
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

## Completed (summary)

- **Language:** Added `fn` declarations, nested and module-qualified calls, optional `setup()`/required `main()` validation, and compatibility warnings for legacy declaration keywords. Added file-scoped/public macros, function codeblocks, and `importAll()` with collision checks, repeated-import idempotence, and interpreted/compiled fixtures.
- **Graphics and game:** Completed GPU callback lifecycle behavior, nested headless UI rendering, TMX parsing and collision/platform physics, and audio playback state handling with fixtures.
- **Server:** Added per-request callback context, safe and complete template rendering, and pre-bind TLS certificate/key validation.
- **Native modules and standard libraries:** Moved Linux named syscall wrappers into the `syscalls` standard-library module with a native backend, host OS/architecture gating, and updated examples/docs. Expanded compression, crypto, watch, structured document, JSON/CSV, regex, Unicode text, path/platform, sys information, subprocess, debug, TUI, image, SQLite and Lua behavior, with regression coverage.
- **Windows support:** Added the Windows toolchain/host layer, native ABI and embedding/library output, compiled-program bundling, portable stdlib backends, console/process/path/network/registry/editor/pipe support, required CI, docs, package manifests, and runtime packaging.
- **Runtime and distribution:** Added typed aggregate ABI values, orderly `sys.exit()` cleanup, C ABI exports with C++/Python consumer tests, and windowed graphics CI.
