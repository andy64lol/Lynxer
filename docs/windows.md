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

`LYNXER_LINUX_ONLY_MODULES` in the `Makefile` names the modules a Windows build
must skip (`sys`).

## Still to do

- **Toolchain and build target.** An MSVC or clang-cl (or mingw-w64) build that
  produces `lynxer.exe` and `lynxer.dll`, replacing `-fPIC`,
  `-ftls-model=global-dynamic` and the ELF version script with Windows
  equivalents. A Windows cross-toolchain is not required to build Linux.
- **libffi on Windows.** Native modules and `ffiCall` dispatch through the Rust
  `libffi` engine; that engine must build and link on Windows before native
  modules can load there.
- **Stdlib backends.** `watch` needs `ReadDirectoryChangesW`; `tui`/`graphics`/
  `sound` need console and device handling; the data-format modules should port
  as-is.
- **`--compile` bundling.** The payload is appended to the running image; the
  PE equivalent and `GetModuleFileNameW`-based self-read are still to do.
- **CI.** An experimental, allowed-to-fail `windows-latest` job now attempts a
  real build and test in `.github/workflows/build-lynxer-windows.yml`; it builds
  inside MSYS2 MINGW64. It graduates to a required job once it is green, and it
  must skip the Linux-only fixtures explicitly.

See the **Windows support** section of [../todo.md](../todo.md) for the tracked
items.
