#pragma once

#include <string>
#include <vector>

// The host-abstraction layer. Every POSIX- or Windows-specific primitive the
// interpreter needs lives here, so the rest of the core stays portable. The
// Linux implementation is the reference; the `_WIN32` implementation mirrors
// it with Win32 calls. Callers never include <dlfcn.h>, <unistd.h> or
// <windows.h> directly.
namespace lynxer::platform {

// Puts stdout and stderr in binary mode on Windows, so `\n` is not translated
// to `\r\n` and Lynxer's output is byte-identical on every host. A no-op on
// POSIX. Call once, before producing any output.
void configureStandardStreams();

// The absolute path of the running executable, or "" when it cannot be
// determined. POSIX reads `/proc/self/exe` (or the dyld path on macOS);
// Windows calls `GetModuleFileNameW`.
std::string executablePath();

// Dynamic libraries. `openLibrary` returns an opaque handle, or nullptr when
// the library cannot be loaded (the reason is in `lastLibraryError`).
void* openLibrary(const std::string& path);
void* librarySymbol(void* handle, const char* name);
void closeLibrary(void* handle);
// The message for the most recent failed `openLibrary`/`librarySymbol`.
std::string lastLibraryError();
// The native shared-library extension for this host: "so", "dylib" or "dll".
const char* libraryExtension();
// The native executable extension for this host: "exe" on Windows, "" elsewhere.
const char* executableExtension();

// The prefix `--install` uses when `LYNXER_PREFIX` is unset: `/usr` on POSIX,
// or a per-user `%LOCALAPPDATA%\Programs\Lynxer` on Windows (no elevation).
std::string defaultInstallPrefix();
// The separator between entries in a `PATH`-style variable: ':' or ';'.
char pathListSeparator();

// Creates a private temporary directory and reports its path. The caller owns
// the directory and removes it.
bool makeTemporaryDirectory(std::string& pathOut, std::string& error);

// Marks `path` executable. On Windows this is a no-op, because executability is
// extension-based.
bool setExecutable(const std::string& path, std::string& error);

// Creates `link` pointing at `target`, replacing anything already there. On
// Windows, where symlinks need elevated privileges, `target` is copied instead.
bool linkOrCopy(const std::string& target, const std::string& link,
                std::string& error);

// Runs `command` to completion and returns its exit status, or -1 when it could
// not be started. `command[0]` is resolved on PATH.
int spawnProcess(const std::vector<std::string>& command);

// Blocks the calling thread for `milliseconds`.
void sleepMilliseconds(long milliseconds);

} // namespace lynxer::platform
