# Windows support

Lynxer targets Linux today. The Windows port is in progress and is being done
in stages; this page records the state and, in particular, what is deliberately
**excluded** from the Windows build while the Linux-only features wait.

## Status

**Done (host abstraction).** Every POSIX/Win32 primitive the interpreter needs
now lives in one place, `lynxer/platform.hpp` / `lynxer/platform.cpp`:

| Primitive | POSIX | Windows |
| --- | --- | --- |
| Running executable path | `/proc/self/exe` (dyld on macOS) | `GetModuleFileNameW` |
| Dynamic library | `dlopen`/`dlsym`/`dlclose` | `LoadLibraryW`/`GetProcAddress`/`FreeLibrary` |
| Subprocess | `fork`/`execvp`/`waitpid` | `CreateProcessA`/`WaitForSingleObject` |
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
sequences render; a redirected stream has no console mode and is unaffected.

**Windows API surface.** On Windows, selecting the Windows target
(`syscalls("winAPI", "amd64")`, with `windows` and `win32` as synonyms) enables
the `winAPI.*` namespace — all `kernel32`: the system information calls
(`getProcessId`, `getCurrentDirectory`, `getComputerName`, `getTempPath`,
`getSystemDirectory`, `getWindowsDirectory`, `getModuleFileName`,
`getTickCount`, `getLastError`, `getDiskFreeBytes`), the environment calls
(`getEnvironmentVariable`, `setEnvironmentVariable`, `expandEnvironmentStrings`)
and file/handle access (`createFile`, `readFile`, `writeFile`, `closeHandle`,
`fileSize`, `seekFile`, `deleteFile`, `copyFile`, `moveFile`,
`createDirectory`, `removeDirectory`), plus `sleep`, `beep` and
`outputDebugString`. The `winapi.lynx` fixture runs it in the Windows job; the
Win32 calls live in `lynxer/winapi.cpp` so nothing else includes `<windows.h>`.
See [syscalls.md](syscalls.md#windows-api-calls).

`--compile` works unchanged: the payload is appended to the running image and
read back through `platform::executablePath()` (`GetModuleFileNameW`), and the
compiled-executable parity loop passes on Windows. Native modules load through
the same ABI too: `examples/native_signatures.cpp` builds and its fixture passes
(the Rust `libffi` engine compiles under both Windows toolchains).

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
- **POSIX-only stdlib modules.** `cli`, `debug`, `os` and `path` include POSIX
  headers MinGW does not provide (`<pwd.h>`, `<sys/ioctl.h>`, `<sys/wait.h>`,
  `<sys/resource.h>`, `<sys/statvfs.h>`, `<sys/utsname.h>`), so they need a
  Windows backend before they can be built.
- **`js` and `multiprocessing`.** Both run commands through
  `lynxer/stdlib/subprocess.hpp`, which is `fork`/`poll`/`waitpid` based; they
  need a `CreateProcess` + pipe backend before they can be built.
- **The `watch` Rust backend.** Its only backends are Linux (inotify) and
  macOS/BSD (kqueue); Windows needs `ReadDirectoryChangesW`.
- **Test groups.** `testLynxer` skips every check whose fixture imports — directly
  or through a stdlib wrapper — a module Windows does not build, plus the
  low-level/syscall surface: the `sys_exit`/`cli_exit` and `sys_exit_thread`/
  `sys_exit_worker` loops, `native_stdlibs`, `program_args`, `stdlibTestAll`,
  the low-level fixture group, and the fixtures listed in
  `LYNXER_WINDOWS_SKIP_STEMS` (for example `stdlib_crypto` and `stdlib_fileIO`,
  which import `os` through a wrapper). That list is the import closure over the
  excluded modules. Fixtures that render through `graphics`/`game` are skipped
  too (`stdlib_game_api`, `stdlib_graphics_raster*`, `stdlib_turtle`): they need
  an OpenGL context, which the Windows runner does not have, whereas the Linux
  CI installs Mesa llvmpipe. Three more fixtures are skipped: `builtin_ffi` and
  `builtin_ffi_errors` load `libc.so.6` by name (and `ffiLoadLibrary` fails hard
  when a library is missing), `builtin_async` drives the POSIX-only
  `filesystem*` built-ins, and `stdlib_server_tls` mints its certificates under
  a hard-coded `/tmp/...`. The `Makefile` variables default to the full lists
  off Windows, so POSIX coverage is unchanged.

`LYNXER_WINDOWS_SKIP_MODULES` in the `Makefile` names the C++ modules a Windows
build skips (`sys cli debug os path js multiprocessing`), and `watch` is filtered
out of `LYNXER_RUST_MODULE_NAMES`.

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
  defaults. Still to do: `lynxer.dll` and a native MSVC/clang-cl build (with the
  `.def`/`__declspec` exports and the 32-bit `__stdcall` convention).
- **Stdlib backends.** `watch` needs `ReadDirectoryChangesW`; `cli`/`debug`/
  `os`/`path` need Windows equivalents for their POSIX calls; `js`/
  `multiprocessing` need a `CreateProcess` subprocess backend; `tui`/`graphics`/
  `sound` need console and device handling. `fileIO`/`shell` and the data-format
  modules build as-is.
- **Embedding and `--emit-library`.** The runtime is an ELF shared object today,
  so `--emit-library` fails cleanly on Windows with a "not supported yet" message
  rather than emitting ELF-only link flags. A `lynxer.dll` runtime, its export
  definition and the DLL-search-path lookup are still to do.

## The Windows ecosystem

Making the interpreter run is only half of "Lynxer on Windows": it also has to
install, update and tool the way Windows users expect.

| Piece | State | Plan |
| --- | --- | --- |
| Prebuilt binaries | **done** | `lynxer-windows-amd64.zip` / `lynxer-windows-arm64.zip` on every release (CI) |
| `--install` / `--uninstall` | **partial** | lays out `<prefix>/lib/lynxer` and a `bin` launcher (a copy), but does not touch `PATH` or register an uninstaller |
| Package managers | todo | `winget`, `Scoop` and `Chocolatey` manifests for the release zips |
| Installer | todo | an MSI (or self-extracting EXE) that sets `PATH` and registers an uninstall entry, built on the `--install` layout |
| Editor and shell | todo | a VS Code extension (syntax highlighting, `--lint`/`--format`) and PowerShell/CMD completion |
| Code signing | todo | Authenticode signatures so a download does not trip SmartScreen |
| C runtime | todo | decide UCRT static linking vs. the VC++ redistributable, and record the minimum Windows version per target |

## Runtime breadth

Several host capabilities still ride the POSIX implementation and need a Win32
backend behind `lynxer/platform.*`:

- **Console I/O and Unicode.** `cli`/`tui` should read and write through the wide
  console API (or a UTF-8 console code page), so non-ASCII input, colours and
  `beep` behave under both Windows Terminal and the legacy console host.
- **Asynchronous I/O.** An IOCP backend where POSIX uses `epoll`/`poll`, for the
  async task runtime and the networking paths.
- **Process control.** Job Objects for process-tree termination and resource
  limits, replacing the POSIX process-group semantics.
- **IPC.** Named pipes alongside sockets, behind the same surface.
- **Services and tasks.** A Windows Service Control Manager / Task Scheduler
  story for `server` and background work.
- **User data and configuration.** `%APPDATA%`/`%LOCALAPPDATA%` and the Known
  Folders in place of `$HOME`, and the registry in place of `/etc`.
- **Diagnostics.** A crash handler that can print a readable trace (the
  `debug`/`--debug` path is POSIX-only today) and `OutputDebugString` wiring.

Both Windows jobs are **required** (no `continue-on-error`):
`.github/workflows/build-lynxer-windows-amd64.yml` (MSYS2 MINGW64) and
`.github/workflows/build-lynxer-windows-arm64.yml` (MSYS2 CLANGARM64). Bob, the
package manager, is separate and has its own four workflows.

See the **Windows support** section of [../todo.md](../todo.md) for the tracked
items.
