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
fixture comparisons rely on.

One consequence of the copy-instead-of-symlink install: on POSIX
`/proc/self/exe` resolves `$PREFIX/bin/lynxer` to the real binary, so
`<exeDir>/stdlib` is the stdlib. On Windows the launcher is a copy in
`<prefix>/bin`, so `stdlibDirectory()` falls back to
`<prefix>/lib/lynxer/stdlib` (and `lynxer.config` is looked up there too).

## What is excluded on Windows (for now)

These features need work that is being done slowly and are **not** part of the
Windows build yet:

- **Named syscalls.** The `syscalls()` selector and the `syscall*`/`<arch>.*`
  built-ins require a Linux runtime; on Windows they are unavailable rather
  than emulated. Their tables and `<sys/syscall.h>` includes are Linux-only.
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
- **Test groups.** `testLynxer` skips the checks that need a module or surface
  Windows does not build: the `sys_exit`/`cli_exit` and `sys_exit_thread`/
  `sys_exit_worker` loops, the `native_stdlibs` check (it also asserts a Linux
  host value), the consolidated `stdlibTestAll`, the low-level fixture group,
  and the parity fixtures that import an excluded module or drive the
  syscall/native surface (`native_stdlibs`, `stdlib_path`, `stdlib_watch`,
  `lowlevel_*`). The `Makefile` variables default to the full lists off
  Windows, so POSIX coverage is unchanged.

`LYNXER_WINDOWS_SKIP_MODULES` in the `Makefile` names the C++ modules a Windows
build skips (`sys cli debug os path js multiprocessing`), and `watch` is filtered
out of `LYNXER_RUST_MODULE_NAMES`.

## Still to do

- **Toolchain and build target.** The `Makefile` detects the host once
  (`LYNXER_HOST_OS` / `LYNXER_ON_WINDOWS`, from `OS` and `uname`, mapping
  MINGW/MSYS/CYGWIN) and keys the compile flags, link libraries and artifact
  names off it: a Windows build gets no `-fPIC`/`-ftls-model=global-dynamic` and
  no `-ldl` (macOS no longer gets `-ldl` either), builds `lynxer.exe`, and finds
  the Rust backends at cargo's `<name>.dll` rather than `lib<name>.so`/`.dylib`. The MSYS2 MinGW-w64 path compiles the
  whole C++ core and every portable Rust backend. The staticlib's native imports
  (the Windows system DLLs: `ntdll`, `ws2_32`, `userenv`, …) come from
  `rustc --print native-static-libs` (`LYNXER_FFI_NATIVE_LIBS`) and are added on
  Windows only, where nothing else supplies them — a Rust `staticlib` does not
  carry its dependencies' link directives; POSIX keeps the proven driver
  defaults. Still to do: `lynxer.dll` and a native MSVC/clang-cl build, plus the
  ELF version script for the shared-library step.
- **libffi on Windows.** The Rust `libffi` engine compiles under the MinGW
  toolchain; the C++ link now pulls its native imports. Native modules still
  need to be validated on Windows.
- **Stdlib backends.** `watch` needs `ReadDirectoryChangesW`; `cli`/`debug`/
  `os`/`path` need Windows equivalents for their POSIX calls; `js`/
  `multiprocessing` need a `CreateProcess` subprocess backend; `tui`/`graphics`/
  `sound` need console and device handling. `fileIO`/`shell` and the data-format
  modules build as-is.
- **`--compile` bundling.** The payload is appended to the running image; the
  PE equivalent and `GetModuleFileNameW`-based self-read are still to do.
- **CI.** An experimental, allowed-to-fail `windows-latest` job now attempts a
  real build and test on both architectures, in
  `.github/workflows/build-lynxer-windows-amd.yml` (MSYS2 MINGW64) and
  `.github/workflows/build-lynxer-windows-arm.yml` (MSYS2 CLANGARM64). They
  graduate to required jobs once green, and must skip the Linux-only fixtures
  explicitly.

See the **Windows support** section of [../todo.md](../todo.md) for the tracked
items.
