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

// Enables ANSI escape-sequence processing on the Windows console, so `tui` and
// `graphics` output renders instead of printing raw escape codes. A no-op on
// POSIX and harmless when the stream is redirected.
void enableVirtualTerminal();

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

// --- Interactive terminal (built-in CLI IDE) --------------------------------

// True when stdin and stdout are both terminals. The CLI IDE refuses to start
// otherwise, because a redirected stream cannot be restored afterwards.
bool isInteractiveTerminal();

// Terminal dimensions in characters, or 80x24 when they cannot be determined.
void terminalSize(int& columns, int& rows);

// Special key codes. All are greater than 0xFF so they never collide with a
// printable byte.
enum : int {
    KEY_UP = 0x1000,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_PAGE_UP,
    KEY_PAGE_DOWN,
    KEY_DELETE,
    KEY_CTRL_LEFT,
    KEY_CTRL_RIGHT,
    KEY_CTRL_UP,
    KEY_CTRL_DOWN,
    KEY_F1,
    KEY_F2,
    KEY_F3,
    KEY_F4,
    KEY_F5,
    KEY_F6,
    KEY_F7,
    KEY_F8,
    KEY_F9,
    KEY_F10,
    KEY_F11,
    KEY_F12,
};

// One key press. `code` is a KEY_* value for special keys; otherwise `bytes`
// holds the raw text (a single control byte or a whole UTF-8 character).
struct KeyEvent {
    int code = 0;
    std::string bytes;
};

// Switch stdin to character-at-a-time, no-echo mode; disableRawInput restores
// the original state. A no-op when already active.
bool enableRawInput(std::string& error);
void disableRawInput();

// Whether a key is waiting (used to tell a lone Escape from an escape
// sequence).
bool keyAvailable(int timeoutMs);

// Blocks for one key press. Never throws; returns an empty event on EOF.
KeyEvent readKey();

struct CapturedCommand {
    std::string output;
    int exitCode = -1;
};

// Runs `command` to completion with stdout and stderr captured into a private
// temp file, so a full-screen TUI can display the result without being
// trampled by the child's output. `command[0]` is resolved on PATH.
bool captureProcess(const std::vector<std::string>& command,
                    CapturedCommand& result, std::string& error);

} // namespace lynxer::platform
