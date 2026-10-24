# Windows support

Lynxer runs on Windows through the MSYS2 MinGW-w64 and CLANGARM64 builds. This
page records the port's state, the supported surface, and what is deliberately
**excluded** because it depends on Linux-only syscalls or platform APIs.

## Status

**Done (host abstraction).** Every POSIX/Win32 primitive the interpreter needs
now lives in one place, `lynxer/platform.hpp` / `lynxer/platform.cpp`:

| Primitive | POSIX | Windows |
| --- | --- | --- |
| Running executable path | `/proc/self/exe` (dyld on macOS) | `GetModuleFileNameW` |
| Dynamic library | `dlopen`/`dlsym`/`dlclose` | `LoadLibraryW`/`GetProcAddress`/`FreeLibrary` |
| Subprocess | `fork`/`execvp`/`waitpid` | `CreateProcessA` in a kill-on-close Job Object |
| Temporary directory | `mkdtemp` | unique directory under `GetTempPath` |
| Executable bit | `chmod 0755` | no-op (extension-based) |
| Install link | `symlink` | copy |

The core (`config.cpp`, `bundle.cpp`, `ast.cpp`, `builtins.cpp`, `shell.cpp`)
calls the layer instead of the POSIX APIs directly, and the native shared
library extension is `.dll` where the host needs it. The Linux behavior is
unchanged and the Linux test suite is the regression gate.

The standard streams are put in binary mode on Windows, so `\n` is not
translated to `\r\n`: Lynxer's output is byte-identical to POSIX, which the
fixture comparisons rely on. The console is also switched to ANSI escape
processing (`platform::enableVirtualTerminal()`), so `tui` and `graphics`
sequences render, and both console code pages are set to UTF-8 so non-ASCII
text round-trips. A redirected stream has no console mode and is unaffected.

**Named pipes.** `winpipe` (`stdlib/winpipe.cpp` + `stdlib/winpipe.lynx`) is a
Windows-only blocking named-pipe module (`create`/`connect`/`open`/`read`/
`write`/`disconnect`/`peek`/`close`). It complements the low-level `winAPI`
pipe calls and gives `server`-style programs a local IPC option beside sockets.
See [stdlib/winpipe.md](stdlib/winpipe.md).

**Registry.** `winreg` (`stdlib/winreg.cpp` + `stdlib/winreg.lynx`) is a
Windows-only module for reading and writing `HKCU`/`HKLM`/`HKCR`/`HKU`/`HKCC`
values. It supports string, expand-string, multi-string, DWORD and QWORD
values, key/value enumeration, and recursive deletion. The `winreg_basic.lynx`
fixture writes under `HKCU` and cleans up after itself. See
[stdlib/winreg.md](stdlib/winreg.md).

**Windows API surface.** On Windows, selecting the Windows target
(`syscalls("winAPI", "amd64")`, with `windows` and `win32` as synonyms) enables
the `winAPI.*` namespace — mostly `kernel32`, with `shell32`/`ole32` for Known
Folders: the system information calls
(`getProcessId`, `getCurrentDirectory`, `getComputerName`, `getTempPath`,
`getSystemDirectory`, `getWindowsDirectory`, `getModuleFileName`,
`getTickCount`, `getLastError`, `getDiskFreeBytes`), the environment calls
(`getEnvironmentVariable`, `setEnvironmentVariable`, `expandEnvironmentStrings`)
and file/handle access (`createFile`, `readFile`, `writeFile`, `closeHandle`,
`fileSize`, `seekFile`, `deleteFile`, `copyFile`, `moveFile`,
`createDirectory`, `removeDirectory`), process control (`getCurrentThreadId`,
`openProcess`, `terminateProcess`, `getExitCodeProcess`, `createProcess`,
`waitForSingleObject`), the console session (`getStdHandle`, `getConsoleMode`,
`setConsoleMode`, `setConsoleTitle`, `getConsoleOutputCP`,
`setConsoleOutputCP`), file attributes and paths (`getFileAttributes`,
`setFileAttributes`, `getFullPathName`, `flushFileBuffers`), the
high-resolution timer (`queryPerformanceCounter`, `queryPerformanceFrequency`),
Known Folders (`getKnownFolderPath`), and `sleep`,
`beep` and `outputDebugString`. The `winapi.lynx` fixture runs it in the Windows job; the
Win32 calls live in `lynxer/winapi.cpp` so nothing else includes `<windows.h>`.
See [syscalls.md](syscalls.md#windows-api-calls).

`--compile` works unchanged: the payload is appended to the running image and
read back through `platform::executablePath()` (`GetModuleFileNameW`), and the
compiled-executable parity loop passes on Windows. Native modules load through
the same ABI too: `examples/native_signatures.cpp` builds and its fixture passes
(the Rust `libffi` engine compiles under both Windows toolchains). The shared
`LYNXER_MODULE_EXPORT` macro in `stdlib/lynxer_native_abi.h` expands to
`__declspec(dllexport)` on Windows, so the entry point and any op marked with
it are exported under MSVC and clang-cl as well as MinGW. Every registered op
must be exported: the bundled MinGW/CLANGARM64 module rule passes
`-Wl,--export-all-symbols`, while an MSVC/clang-cl module marks each op with
`LYNXER_MODULE_EXPORT` (or uses a `.def` file); see
[native-module-abi.md](native-module-abi.md#platform-support).

One consequence of the copy-instead-of-symlink install: on POSIX
`/proc/self/exe` resolves `$PREFIX/bin/lynxer` to the real binary, so
`<exeDir>/stdlib` is the stdlib. On Windows the launcher is a copy in
`<prefix>/bin`, so `stdlibDirectory()` falls back to
`<prefix>/lib/lynxer/stdlib` (and `lynxer.config` is looked up there too).

## What is excluded on Windows (for now)

These features need work that is being done slowly and are **not** part of the
Windows build yet:

- **Named Linux syscalls.** The `amd64.syscall*`/`arm64.syscall*` built-ins
  require a Linux runtime; on Windows they are unavailable rather than emulated
  (their tables and `<sys/syscall.h>` includes are Linux-only). Selecting
  `"Linux"` is refused on a Windows host, and selecting `winAPI`/`"windows"` is
  refused on Linux.
- **The `sys` stdlib module.** It is built on Linux system calls and is not
  compiled for Windows.
- **The managed POSIX built-ins.** The `filesystem*`, `networking*` and
  `process*` families are already routed to `unsupportedTable()` on a non-POSIX
  host; a Windows implementation comes later.
- **Low-level fixtures.** `lowlevel_*` and `syscall*` fixtures are Linux-only
  and stay out of the Windows test run.
- **Windows-specific stdlib fixture parity.** `cli`, `debug`, `os` and `path`
  now build with Win32 equivalents for their OS calls. `path`/`fileIO` also
  accept `/` and `\`, open absolute paths beyond `MAX_PATH` through the
  `\\?\` extended-length prefix, and normalize CRLF/lone-CR on Windows text
  reads. The current fixture exclusions remain because they assert POSIX
  separators or invoke shell commands such as `sh`/`printf`; they do not
  indicate missing module DLLs.
- **Process modules.** `js` and `multiprocessing` now run commands through a
  `CreateProcess`/pipe backend with timeout termination through Job Objects.
  Their fixtures remain filtered because they use POSIX shell commands.
- **The `watch` Rust backend.** Windows uses `ReadDirectoryChangesW` with
  overlapped I/O. Directory watches have no pollable descriptor there, so
  `watchFd` returns `-1` and programs must use `watchWait`; the dedicated
  `winwatch` fixture covers that path.
- **Test groups.** `testLynxer` skips low-level/syscall fixtures and tests that
  assume POSIX command names, paths or exit behavior: `native_stdlibs`,
  `program_args`, `stdlibTestAll`, and the entries in
  `LYNXER_WINDOWS_SKIP_STEMS`. The module DLLs for `cli`, `debug`, `os` and
  `path` are built; their current fixtures still contain POSIX-specific
  expectations. A locally cross-linked x86_64 build (vendored libffi) passes
  `winapi`, `winapi_pipe`, `winreg_basic`, `winpipe_basic`, `winwatch`,
  `stdlib_os_dirs`, `stdlib_debug_crash`, `stdlib_debug`, `stdlib_fileIO`,
  `stdlib_compress`, `stdlib_compress_limits`, `stdlib_crypto`,
  `stdlib_image_codecs`, `stdlib_image_formats`, `stdlib_image_svg` and
  `stdlib_xml_limits` under Wine; the CI jobs run the same fixtures on real
  Windows. Fixtures that render through `graphics`/`game` are skipped
  too (`stdlib_game_api`, `stdlib_graphics_raster*`, `stdlib_turtle`): they need
  an OpenGL context, which the Windows runner does not have, whereas the Linux
  CI installs Mesa llvmpipe. Three more fixtures are skipped: `builtin_ffi` and
  `builtin_ffi_errors` load `libc.so.6` by name (and `ffiLoadLibrary` fails hard
  when a library is missing), `builtin_async` drives the POSIX-only
  `filesystem*` built-ins, and `stdlib_server_tls` mints its certificates under
  a hard-coded `/tmp/...`. The `Makefile` variables default to the full lists
  off Windows, so POSIX coverage is unchanged.

`LYNXER_WINDOWS_SKIP_MODULES` in the `Makefile` names the C++ module a Windows
build skips (`sys`); `watch` now builds on Windows.

## Shared-library embedding

The MinGW-w64 Windows builds now produce `lynxer.dll` and
`lynxer.dll.a`. The DLL exports only the `lynxer_embed_*` C ABI. `make sdk`
stages the public headers under `build/sdk/include`, the import library under
`build/sdk/lib`, and the runtime under `build/sdk/bin`. Windows
`lynxer --emit-library app.lynx -o app.dll` emits `app.dll`, `app.dll.a`, and
`app.h`; the generated DLL links to `lynxer.dll`.

At run time, place `lynxer.dll` next to the host executable or add the runtime
directory to `PATH`. The installed SDK runtime lives under
`<prefix>/lib/lynxer`; `--emit-library` locates it from an installed Lynxer
launcher. Emitted libraries are currently supported by the MSYS2 MinGW-w64
GNU ABI used in CI; an MSVC/clang-cl runtime is still unavailable.

## Still to do

- **Toolchain and build target.** The `Makefile` detects the host once
  (`LYNXER_HOST_OS` / `LYNXER_ON_WINDOWS`, from `OS` and `uname`, mapping
  MINGW/MSYS/CYGWIN) and keys the compile flags, link libraries and artifact
  names off it: a Windows build gets no `-fPIC`/`-ftls-model=global-dynamic` and
  no `-ldl` (macOS no longer gets `-ldl` either), builds `lynxer.exe`, and finds
  the Rust backends at cargo's `<name>.dll` rather than `lib<name>.so`/`.dylib`.
  The MSYS2 MinGW-w64 path compiles the whole C++ core and every portable Rust
  backend. The staticlib's native imports
  (the Windows system DLLs: `ntdll`, `ws2_32`, `userenv`, …) come from
  `rustc --print native-static-libs` (`LYNXER_FFI_NATIVE_LIBS`) and are added on
  Windows only, where nothing else supplies them — a Rust `staticlib` does not
  carry its dependencies' link directives; POSIX keeps the proven driver
  defaults. Still to do: a native MSVC/clang-cl build and the 32-bit
  `__stdcall` convention.
- **Stdlib backends.** `cli`/`debug`/`os`/`path` and `js`/`multiprocessing` now
  have Win32 implementations; `network` maps to Winsock through Rust's
  `std::net`; `watch` uses `ReadDirectoryChangesW`. `tui` uses crossterm's
  Windows console backend, `graphics`/`game` use macroquad's Windows OpenGL
  backend plus the headless CPU raster path, and `sound` uses cpal/rodio over
  WASAPI and tolerates a missing device. All three Rust backends pass
  `cargo check --target x86_64-pc-windows-gnullvm`, and the Windows CI builds
  their cdylibs. `fileIO`/`shell` and the data-format modules build as-is.
- **Every fixture in `LYNXER_WINDOWS_SKIP_STEMS`.** The list is intentional;
  the modules themselves build on Windows unless noted.

  | Fixture | Why it is skipped |
  | --- | --- |
  | `stdlib_cli` | Drives `printf` through the shell |
  | `stdlib_js` | Needs Node.js on `PATH`; the Windows runner does not install it |
  | `stdlib_multiprocessing` | Uses `printf` commands |
  | `stdlib_os`, `stdlib_path` | Assert POSIX separators, `/tmp`, and POSIX path semantics |
  | `stdlib_sys` | The `sys` module is not built on Windows |
  | `stdlib_watch` | Replaced by the Windows-only `winwatch` fixture |
  | `stdlibTestAll` | Runs the aggregate suite, including POSIX-only modules |
  | `native_stdlibs` | Imports `sys` and checks for `sh` |
  | `program_args` | Launched with POSIX shell quoting in the gate |
  | `stdlib_game_api`, `stdlib_graphics_raster`, `stdlib_graphics_raster_shapes`, `stdlib_turtle` | Need an OpenGL context; the Windows runner has none |

- **Embedding and `--emit-library`.** Supported on the MinGW-w64 builds. A
  Windows emitted DLL depends on `lynxer.dll`, which must be available through
  the Windows DLL search path. MSVC/clang-cl is not supported yet.

## C runtime and minimum Windows version

Lynxer targets **Windows 10 or newer** on both x64 and ARM64. The AMD64 build
uses the MSYS2 MINGW64 toolchain and therefore `msvcrt.dll` plus the MSYS2 GCC
runtime DLLs (`libgcc_s_seh-1.dll`, `libstdc++-6.dll`,
`libwinpthread-1.dll`). `libffi` is built from the vendored crate and linked
into the interpreter, so no separate `libffi` DLL is shipped. The ARM64 build
uses CLANGARM64 and the UCRT plus the CLANGARM64 runtime DLLs. The Windows CI
workflows run `ldd` on `lynxer.exe` and copy the non-system DLLs into
`lynxer/runtime`; release zips must ship those DLLs next to `lynxer.exe`.
`packaging/windows/stage-payload.sh` copies them into the installer payload,
and the installer puts them in `<prefix>/lib/lynxer` and adds that directory
to the user `PATH` (so both `lynxer.exe` and emitted DLLs find them).
No Visual C++ Redistributable is required.

## The Windows ecosystem

Making the interpreter run is only half of "Lynxer on Windows": it also has to
install, update and tool the way Windows users expect.

| Piece | State | Plan |
| --- | --- | --- |
| Prebuilt binaries | **done** | `lynxer-windows-amd64.zip` / `lynxer-windows-arm64.zip` on every release (CI) |
| `--install` / `--uninstall` | **partial** | lays out `<prefix>/lib/lynxer` and a `bin` launcher (a copy), but does not touch `PATH` or register an uninstaller |
| Package managers | **partial** | `render-package-manifests.py` renders `winget`, `Scoop` and `Chocolatey` manifests; registry submission remains |
| Installer | **partial** | `packaging/windows/lynxer.iss` builds an Inno Setup installer on the `--install` layout, adds `bin` and `lib/lynxer` to the user `PATH`, and registers an uninstall entry; CI compiles it on the AMD64 job; signing remains |
| Editor and shell | **done** | `editors/vscode` (syntax highlighting, `--lint`/`--format`) plus PowerShell and Clink completion |
| Background tasks | **partial** | `packaging/windows/lynxer-task.ps1` registers a program with Task Scheduler; a SCM service host is not shipped |
| Code signing | todo | Authenticode signatures so a download does not trip SmartScreen |
| C runtime | **done** | Windows 10+; AMD64 MSYS2 MINGW64 (`msvcrt.dll` + GCC runtime DLLs), ARM64 CLANGARM64 (UCRT + CLANGARM64 runtime DLLs); CI collects them with `ldd` |

## Runtime breadth

Several host capabilities still ride the POSIX implementation and need a Win32
backend behind `lynxer/platform.*`:

- **Console I/O and Unicode.** Done: both console code pages are set to UTF-8
  and ANSI/VT processing is enabled when the process owns a console, so
  non-ASCII input, colours and `beep` behave under Windows Terminal and the
  legacy console host. Redirected streams stay byte-identical.
- **Asynchronous I/O.** An IOCP backend where POSIX uses `epoll`/`poll`, for the
  async task runtime and the networking paths.
- **Process control.** Core subprocesses now run in a kill-on-close Job
  Object (`platform::spawnProcess`), so the child tree is torn down with the
  interpreter. Per-process resource limits remain.
- **IPC and shell.** The `winpipe` module provides blocking named pipes beside
  sockets; `shellOpen`, clipboard access and tray notifications are available
  through `winAPI`.
- **Services and tasks.** The `server` module stays a foreground console
  process on Windows (it does not install a Linux unit). Background startup is
  left to Task Scheduler (`schtasks /create`) or an external SCM wrapper; a
  built-in `--install-service` path is not planned for the first Windows
  release. The registry API is available through `winreg` for programs that
  need to store configuration there.
- **User data and configuration.** `%APPDATA%`/`%LOCALAPPDATA%` and the Known
  Folders in place of `$HOME`, and the registry in place of `/etc`.
- **Diagnostics.** A crash handler that can print a readable trace (the
  `debug`/`--debug` path is POSIX-only today) and `OutputDebugString` wiring.

Both Windows jobs are **required** (no `continue-on-error`):
`.github/workflows/build-lynxer-windows-amd64.yml` (MSYS2 MINGW64) and
`.github/workflows/build-lynxer-windows-arm64.yml` (MSYS2 CLANGARM64).

See the **Windows support** section of [../todo.md](../todo.md) for the tracked
items.
