// Host-abstraction layer implementation. The POSIX half is the reference and is
// exercised by the Linux test suite; the `_WIN32` half mirrors it with Win32
// calls and is compiled only when targeting Windows.

#include "platform.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// `std::size_t` and `std::getenv` are used by the Win32 implementation only.
#include <cstddef>
#include <cstdlib>
#include <fcntl.h>
#include <io.h>
#include <conio.h>
#include <stdio.h>
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
// `std::uint32_t` is used by the dyld self-path lookup only.
#include <cstdint>
#include <mach-o/dyld.h>
#endif
#endif

namespace lynxer::platform {

namespace {

#if defined(_WIN32)

std::string wideToUtf8(const std::wstring& wide) {
    if (wide.empty()) {
        return std::string();
    }
    const int size = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                           static_cast<int>(wide.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return std::string();
    }
    std::string output(static_cast<std::size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                          static_cast<int>(wide.size()), output.data(), size,
                          nullptr, nullptr);
    return output;
}

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return std::wstring();
    }
    const int size = ::MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                           static_cast<int>(text.size()),
                                           nullptr, 0);
    if (size <= 0) {
        return std::wstring();
    }
    std::wstring output(static_cast<std::size_t>(size), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(),
                          static_cast<int>(text.size()), output.data(), size);
    return output;
}

// Quotes one argument the way the C runtime parses `CreateProcess` command
// lines: wrap in double quotes and backslash-escape embedded quotes.
std::string quoteArgument(const std::string& argument) {
    std::string quoted = "\"";
    for (const char character : argument) {
        if (character == '"') {
            quoted += "\\\"";
        } else {
            quoted += character;
        }
    }
    quoted += '"';
    return quoted;
}

#endif // _WIN32

} // namespace

void enableVirtualTerminal() {
#if defined(_WIN32)
    bool attachedToConsole = false;
    const DWORD streams[] = {STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
    for (const DWORD which : streams) {
        const HANDLE stream = ::GetStdHandle(which);
        if (stream == nullptr || stream == INVALID_HANDLE_VALUE) {
            continue;
        }
        DWORD mode = 0;
        // Fails when the stream is redirected rather than a console, which is
        // fine: there is nothing to switch on.
        if (::GetConsoleMode(stream, &mode) == 0) {
            continue;
        }
        attachedToConsole = true;
        ::SetConsoleMode(stream, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING |
                                     DISABLE_NEWLINE_AUTO_RETURN);
    }
    // The narrow console entry points (ReadFile/WriteFile on the standard
    // handles, and the CRT's stdio) use the console code page, not UTF-8.
    // Switch both directions so `cli`/`tui` round-trip non-ASCII text under
    // Windows Terminal and the legacy console. This is skipped when neither
    // stream is a console, so redirected output stays byte-identical.
    if (attachedToConsole) {
        ::SetConsoleCP(CP_UTF8);
        ::SetConsoleOutputCP(CP_UTF8);
    }
#endif
}

void configureStandardStreams() {
#if defined(_WIN32)
    // Text mode translates every `\n` written to stdout/stderr into `\r\n`.
    // Lynxer's output should be byte-identical on every host, and the fixture
    // comparisons (and any text processing) depend on it, so write LF.
    ::_setmode(::_fileno(stdout), _O_BINARY);
    ::_setmode(::_fileno(stderr), _O_BINARY);
    enableVirtualTerminal();
#endif
}

std::string executablePath() {
#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = ::GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return std::string();
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            return wideToUtf8(buffer);
        }
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    ::_NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size, '\0');
    if (::_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return std::string();
    }
    std::error_code error;
    const std::filesystem::path resolved =
        std::filesystem::weakly_canonical(buffer.data(), error);
    return error ? std::string(buffer.data()) : resolved.string();
#else
    char buffer[PATH_MAX];
    const ssize_t length = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) {
        return std::string();
    }
    buffer[length] = '\0';
    return std::string(buffer);
#endif
}

void* openLibrary(const std::string& path) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(::LoadLibraryW(utf8ToWide(path).c_str()));
#else
    return ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void* librarySymbol(void* handle, const char* name) {
    if (handle == nullptr || name == nullptr) {
        return nullptr;
    }
#if defined(_WIN32)
    return reinterpret_cast<void*>(
        ::GetProcAddress(static_cast<HMODULE>(handle), name));
#else
    return ::dlsym(handle, name);
#endif
}

void closeLibrary(void* handle) {
    if (handle == nullptr) {
        return;
    }
#if defined(_WIN32)
    ::FreeLibrary(static_cast<HMODULE>(handle));
#else
    ::dlclose(handle);
#endif
}

std::string lastLibraryError() {
#if defined(_WIN32)
    const DWORD code = ::GetLastError();
    if (code == 0) {
        return std::string();
    }
    LPWSTR message = nullptr;
    const DWORD length = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<LPWSTR>(&message), 0, nullptr);
    std::string text = length == 0 ? std::string() : wideToUtf8(message);
    if (message != nullptr) {
        ::LocalFree(message);
    }
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    return text;
#else
    const char* message = ::dlerror();
    return message == nullptr ? std::string() : std::string(message);
#endif
}

const char* libraryExtension() {
#if defined(_WIN32)
    return "dll";
#elif defined(__APPLE__)
    return "dylib";
#else
    return "so";
#endif
}

const char* executableExtension() {
#if defined(_WIN32)
    return "exe";
#else
    return "";
#endif
}

std::string defaultInstallPrefix() {
#if defined(_WIN32)
    // A per-user install needs no elevation, the way Windows tools such as
    // VS Code install themselves by default.
    const char* local = std::getenv("LOCALAPPDATA");
    if (local != nullptr && local[0] != '\0') {
        return std::string(local) + "\\Programs\\Lynxer";
    }
    const char* profile = std::getenv("USERPROFILE");
    if (profile != nullptr && profile[0] != '\0') {
        return std::string(profile) + "\\Lynxer";
    }
    return "Lynxer";
#else
    return "/usr";
#endif
}

char pathListSeparator() {
#if defined(_WIN32)
    return ';';
#else
    return ':';
#endif
}

bool makeTemporaryDirectory(std::string& pathOut, std::string& error) {
    std::error_code filesystemError;
    const std::filesystem::path base =
        std::filesystem::temp_directory_path(filesystemError);
    if (filesystemError) {
        error = "cannot locate the temporary directory: " +
                filesystemError.message();
        return false;
    }
#if defined(_WIN32)
    for (int attempt = 0; attempt < 512; ++attempt) {
        const std::filesystem::path candidate =
            base / ("lynxer-" + std::to_string(::GetCurrentProcessId()) + "-" +
                    std::to_string(attempt));
        std::error_code createError;
        if (std::filesystem::create_directory(candidate, createError)) {
            pathOut = candidate.string();
            return true;
        }
    }
    error = "cannot create a temporary directory under " + base.string();
    return false;
#else
    std::string pattern = (base / "lynxer-XXXXXX").string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    if (::mkdtemp(buffer.data()) == nullptr) {
        error = "cannot create a temporary directory under " + base.string();
        return false;
    }
    pathOut = buffer.data();
    return true;
#endif
}

bool setExecutable(const std::string& path, std::string& error) {
#if defined(_WIN32)
    (void)path;
    (void)error;
    return true;
#else
    if (::chmod(path.c_str(), 0755) != 0) {
        error = "cannot mark '" + path + "' executable";
        return false;
    }
    return true;
#endif
}

bool linkOrCopy(const std::string& target, const std::string& link,
                std::string& error) {
    std::error_code removeError;
    std::filesystem::remove(link, removeError);
#if defined(_WIN32)
    // Symlinks require developer mode or elevation on Windows; a copy always
    // works and the installed binary behaves identically for the user.
    std::error_code copyError;
    std::filesystem::copy_file(target, link,
                               std::filesystem::copy_options::overwrite_existing,
                               copyError);
    if (copyError) {
        error = "cannot copy '" + target + "' to '" + link +
                "': " + copyError.message();
        return false;
    }
    return true;
#else
    std::error_code linkError;
    std::filesystem::create_symlink(target, link, linkError);
    if (linkError) {
        error = "cannot link '" + link + "' -> '" + target +
                "': " + linkError.message();
        return false;
    }
    return true;
#endif
}

int spawnProcess(const std::vector<std::string>& command) {
    if (command.empty()) {
        return -1;
    }
#if defined(_WIN32)
    std::string line;
    for (std::size_t index = 0; index < command.size(); ++index) {
        if (index != 0) {
            line += ' ';
        }
        line += quoteArgument(command[index]);
    }
    std::vector<char> mutableLine(line.begin(), line.end());
    mutableLine.push_back('\0');
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);

    // Put the child in a kill-on-close job before it runs, so descendants it
    // spawns are torn down with it. The process is created suspended to close
    // the window between CreateProcess and AssignProcessToJobObject. If the
    // assignment fails (for example when the parent is already in a job that
    // does not allow nesting), the child still runs normally.
    HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
    if (job != nullptr) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (::SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                      &limits, sizeof(limits)) == 0) {
            ::CloseHandle(job);
            job = nullptr;
        }
    }
    const DWORD creationFlags = CREATE_SUSPENDED;
    PROCESS_INFORMATION process{};
    if (::CreateProcessA(nullptr, mutableLine.data(), nullptr, nullptr, FALSE,
                         creationFlags, nullptr, nullptr, &startup,
                         &process) == 0) {
        if (job != nullptr) {
            ::CloseHandle(job);
        }
        return -1;
    }
    if (job != nullptr && ::AssignProcessToJobObject(job, process.hProcess) == 0) {
        ::CloseHandle(job);
        job = nullptr;
    }
    ::ResumeThread(process.hThread);
    const DWORD waitStatus =
        ::WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    if (waitStatus == WAIT_FAILED) {
        // Do not leave a stuck child behind if the wait itself fails.
        if (job != nullptr) {
            ::TerminateJobObject(job, 1);
        } else {
            ::TerminateProcess(process.hProcess, 1);
        }
        code = 1;
    } else {
        ::GetExitCodeProcess(process.hProcess, &code);
    }
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
    if (job != nullptr) {
        ::CloseHandle(job);
    }
    return static_cast<int>(code);
#else
    std::vector<char*> argv;
    argv.reserve(command.size() + 1);
    for (const std::string& part : command) {
        argv.push_back(const_cast<char*>(part.c_str()));
    }
    argv.push_back(nullptr);
    const pid_t pid = ::fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        ::execvp(argv[0], argv.data());
        ::_exit(127);
    }
    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

void sleepMilliseconds(long milliseconds) {
    if (milliseconds <= 0) {
        return;
    }
#if defined(_WIN32)
    ::Sleep(static_cast<DWORD>(milliseconds));
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
#endif
}


// ---------------------------------------------------------------------------
// Interactive terminal primitives used by the built-in CLI IDE.
// ---------------------------------------------------------------------------

namespace {

#if defined(_WIN32)
DWORD savedInputMode = 0;
#else
struct termios savedInputMode;
#endif
bool rawInputActive = false;

#if !defined(_WIN32)
int decodeCsi(const std::string& sequence) {
    if (sequence == "A") return KEY_UP;
    if (sequence == "B") return KEY_DOWN;
    if (sequence == "C") return KEY_RIGHT;
    if (sequence == "D") return KEY_LEFT;
    if (sequence == "H" || sequence == "1~" || sequence == "7~") return KEY_HOME;
    if (sequence == "F" || sequence == "4~" || sequence == "8~") return KEY_END;
    if (sequence == "3~") return KEY_DELETE;
    if (sequence == "5~") return KEY_PAGE_UP;
    if (sequence == "6~") return KEY_PAGE_DOWN;
    if (sequence == "1;5D") return KEY_CTRL_LEFT;
    if (sequence == "1;5C") return KEY_CTRL_RIGHT;
    if (sequence == "1;5A") return KEY_CTRL_UP;
    if (sequence == "1;5B") return KEY_CTRL_DOWN;
    if (sequence == "11~") return KEY_F1;
    if (sequence == "12~") return KEY_F2;
    if (sequence == "13~") return KEY_F3;
    if (sequence == "14~") return KEY_F4;
    if (sequence == "15~") return KEY_F5;
    if (sequence == "17~") return KEY_F6;
    if (sequence == "18~") return KEY_F7;
    if (sequence == "19~") return KEY_F8;
    if (sequence == "20~") return KEY_F9;
    if (sequence == "21~") return KEY_F10;
    if (sequence == "23~") return KEY_F11;
    if (sequence == "24~") return KEY_F12;
    // Terminals report modifiers as extra parameters: Delete arrives as
    // "3~", but Ctrl+Delete as "3;5~" and Shift+Delete as "3;2~". The
    // modifier is ignored, the key itself is the same.
    const std::size_t semicolon = sequence.find(';');
    if (semicolon != std::string::npos && !sequence.empty() &&
        sequence.back() == '~') {
        const std::string base = sequence.substr(0, semicolon) + "~";
        if (base == "1~" || base == "7~") return KEY_HOME;
        if (base == "4~" || base == "8~") return KEY_END;
        if (base == "3~") return KEY_DELETE;
        if (base == "5~") return KEY_PAGE_UP;
        if (base == "6~") return KEY_PAGE_DOWN;
    }
    return 0;
}
#endif

} // namespace

bool isInteractiveTerminal() {
#if defined(_WIN32)
    // `_isatty()` treats any character device as a terminal, including the NUL
    // device that `/dev/null` maps to under MSYS2. A redirected stream still
    // looked interactive, so the IDE skipped its guard and failed later in
    // enableRawInput() with a confusing message. GetConsoleMode only succeeds
    // on a real console handle, which is exactly what the IDE needs.
    DWORD mode = 0;
    const DWORD streams[] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE};
    for (const DWORD which : streams) {
        const HANDLE stream = ::GetStdHandle(which);
        if (stream == nullptr || stream == INVALID_HANDLE_VALUE ||
            ::GetConsoleMode(stream, &mode) == 0) {
            return false;
        }
    }
    return true;
#else
    return ::isatty(STDIN_FILENO) != 0 && ::isatty(STDOUT_FILENO) != 0;
#endif
}

void terminalSize(int& columns, int& rows) {
    columns = 80;
    rows = 24;
#if defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (::GetConsoleScreenBufferInfo(::GetStdHandle(STD_OUTPUT_HANDLE),
                                     &info) != 0) {
        const int width = info.srWindow.Right - info.srWindow.Left + 1;
        const int height = info.srWindow.Bottom - info.srWindow.Top + 1;
        if (width > 0 && height > 0) {
            columns = width;
            rows = height;
        }
    }
#else
    struct winsize size {};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0 &&
        size.ws_row > 0) {
        columns = size.ws_col;
        rows = size.ws_row;
    }
#endif
}

bool enableRawInput(std::string& error) {
    if (rawInputActive) {
        return true;
    }
#if defined(_WIN32)
    const HANDLE input = ::GetStdHandle(STD_INPUT_HANDLE);
    if (::GetConsoleMode(input, &savedInputMode) == 0) {
        error = "cannot read the console input mode";
        return false;
    }
    DWORD mode = savedInputMode;
    mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
    if (::SetConsoleMode(input, mode) == 0) {
        error = "cannot switch the console to raw input";
        return false;
    }
#else
    if (::tcgetattr(STDIN_FILENO, &savedInputMode) != 0) {
        error = "cannot read the terminal attributes";
        return false;
    }
    struct termios raw = savedInputMode;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (::tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        error = "cannot switch the terminal to raw input";
        return false;
    }
#endif
    rawInputActive = true;
    return true;
}

void disableRawInput() {
    if (!rawInputActive) {
        return;
    }
#if defined(_WIN32)
    ::SetConsoleMode(::GetStdHandle(STD_INPUT_HANDLE), savedInputMode);
#else
    ::tcsetattr(STDIN_FILENO, TCSANOW, &savedInputMode);
#endif
    rawInputActive = false;
}

bool keyAvailable(int timeoutMs) {
#if defined(_WIN32)
    const int iterations = (timeoutMs + 4) / 5;
    for (int index = 0; index < iterations; ++index) {
        if (::_kbhit() != 0) {
            return true;
        }
        sleepMilliseconds(5);
    }
    return ::_kbhit() != 0;
#else
    fd_set set;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);
    struct timeval timeout {};
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;
    const int ready =
        ::select(STDIN_FILENO + 1, &set, nullptr, nullptr, &timeout);
    return ready > 0 && FD_ISSET(STDIN_FILENO, &set) != 0;
#endif
}

KeyEvent readKey() {
    KeyEvent event;
#if defined(_WIN32)
    const int first = ::_getch();
    // Ctrl+Alt+C arrives as ETX on the console: processed input is disabled in
    // raw mode, so Ctrl+C is delivered as byte 3 instead of raising SIGINT.
    if (first == 3) {
        event.code = KEY_COMMAND;
        return event;
    }
    if (first == 0 || first == 224) {
        if (!keyAvailable(50)) {
            return event;
        }
        const int second = ::_getch();
        switch (second) {
            case 72: event.code = KEY_UP; break;
            case 80: event.code = KEY_DOWN; break;
            case 75: event.code = KEY_LEFT; break;
            case 77: event.code = KEY_RIGHT; break;
            case 71: event.code = KEY_HOME; break;
            case 79: event.code = KEY_END; break;
            case 73: event.code = KEY_PAGE_UP; break;
            case 81: event.code = KEY_PAGE_DOWN; break;
            case 83: event.code = KEY_DELETE; break;
            // Alt+C exposes the same command menu as Ctrl+Alt+C.
            case 46: event.code = KEY_COMMAND; break;
            // Alt+1 .. Alt+9 select an open buffer (scan codes 120..128).
            case 120: case 121: case 122: case 123: case 124:
            case 125: case 126: case 127: case 128:
                event.code = KEY_ALT_1 + (second - 120);
                break;
            case 115: event.code = KEY_CTRL_LEFT; break;
            case 116: event.code = KEY_CTRL_RIGHT; break;
            case 141: event.code = KEY_CTRL_UP; break;
            case 145: event.code = KEY_CTRL_DOWN; break;
            case 59: event.code = KEY_F1; break;
            case 60: event.code = KEY_F2; break;
            case 61: event.code = KEY_F3; break;
            case 62: event.code = KEY_F4; break;
            case 63: event.code = KEY_F5; break;
            case 64: event.code = KEY_F6; break;
            case 65: event.code = KEY_F7; break;
            case 66: event.code = KEY_F8; break;
            case 67: event.code = KEY_F9; break;
            case 68: event.code = KEY_F10; break;
            case 133: event.code = KEY_F11; break;
            case 134: event.code = KEY_F12; break;
            default: break;
        }
        return event;
    }
    event.bytes.push_back(static_cast<char>(first & 0xFF));
#else
    unsigned char first = 0;
    if (::read(STDIN_FILENO, &first, 1) != 1) {
        return event;
    }
    event.bytes.push_back(static_cast<char>(first));

    if (first == 27) {
        if (!keyAvailable(25)) {
            return event;  // a lone Escape
        }
        unsigned char second = 0;
        if (::read(STDIN_FILENO, &second, 1) != 1) {
            return event;
        }
        if (second == '[') {
            std::string sequence;
            while (keyAvailable(25)) {
                unsigned char part = 0;
                if (::read(STDIN_FILENO, &part, 1) != 1) {
                    break;
                }
                sequence.push_back(static_cast<char>(part));
                if (part >= 0x40 && part <= 0x7E) {
                    break;
                }
            }
            event.bytes.clear();
            event.code = decodeCsi(sequence);
            return event;
        }
        if (second == 'O') {
            event.bytes.clear();
            if (keyAvailable(25)) {
                unsigned char part = 0;
                if (::read(STDIN_FILENO, &part, 1) == 1) {
                    if (part >= 'P' && part <= 'S') {
                        event.code = KEY_F1 + (part - 'P');
                    }
                }
            }
            return event;
        }
        // Ctrl+Alt+C: terminals send Alt as a leading Escape followed by the
        // (possibly control-modified) character. Accept both ETX and plain c/C
        // so the combo works across terminal emulators.
        if (second == 3 || second == 'c' || second == 'C') {
            event.bytes.clear();
            event.code = KEY_COMMAND;
            return event;
        }
        // Alt+1 .. Alt+9 select an open buffer without consuming a digit that
        // would otherwise be typed into the editor.
        if (second >= '1' && second <= '9') {
            event.bytes.clear();
            event.code = KEY_ALT_1 + (second - '1');
            return event;
        }
        // Unknown escape: report the lone Escape.
        event.bytes.assign(1, static_cast<char>(27));
        return event;
    }

    // Read the remaining bytes of a UTF-8 character as one key event.
    int extra = 0;
    if (first >= 0xC2 && first <= 0xDF) {
        extra = 1;
    } else if (first >= 0xE0 && first <= 0xEF) {
        extra = 2;
    } else if (first >= 0xF0 && first <= 0xF4) {
        extra = 3;
    }
    for (int index = 0; index < extra && keyAvailable(25); ++index) {
        unsigned char part = 0;
        if (::read(STDIN_FILENO, &part, 1) != 1) {
            break;
        }
        event.bytes.push_back(static_cast<char>(part));
    }
#endif
    return event;
}

bool captureProcess(const std::vector<std::string>& command,
                    CapturedCommand& result, std::string& error) {
    if (command.empty()) {
        error = "empty command";
        return false;
    }
    std::string directory;
    if (!makeTemporaryDirectory(directory, error)) {
        return false;
    }
    struct Cleanup {
        std::string path;
        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{directory};
    const std::string outputPath = directory + "/output.txt";

#if defined(_WIN32)
    std::string line;
    for (std::size_t index = 0; index < command.size(); ++index) {
        if (index != 0) {
            line += ' ';
        }
        line += quoteArgument(command[index]);
    }
    std::vector<char> mutableLine(line.begin(), line.end());
    mutableLine.push_back('\0');

    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    const HANDLE output = ::CreateFileA(outputPath.c_str(), GENERIC_WRITE, 0,
                                        &attributes, CREATE_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        error = "cannot create the capture file";
        return false;
    }
    const HANDLE nullInput =
        ::CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                      &attributes, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                      nullptr);
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nullInput != INVALID_HANDLE_VALUE
                            ? nullInput
                            : ::GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = output;
    startup.hStdError = output;
    PROCESS_INFORMATION process{};
    const BOOL started =
        ::CreateProcessA(nullptr, mutableLine.data(), nullptr, nullptr, TRUE,
                         0, nullptr, nullptr, &startup, &process);
    ::CloseHandle(output);
    if (nullInput != INVALID_HANDLE_VALUE) {
        ::CloseHandle(nullInput);
    }
    if (started == 0) {
        error = "cannot start the process";
        return false;
    }
    ::WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    ::GetExitCodeProcess(process.hProcess, &code);
    result.exitCode = static_cast<int>(code);
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
#else
    std::vector<char*> argv;
    argv.reserve(command.size() + 1);
    for (const std::string& part : command) {
        argv.push_back(const_cast<char*>(part.c_str()));
    }
    argv.push_back(nullptr);

    const pid_t pid = ::fork();
    if (pid < 0) {
        error = "cannot fork";
        return false;
    }
    if (pid == 0) {
        const int nullInput = ::open("/dev/null", O_RDONLY);
        if (nullInput >= 0) {
            ::dup2(nullInput, STDIN_FILENO);
            if (nullInput > STDERR_FILENO) {
                ::close(nullInput);
            }
        }
        const int fd = ::open(outputPath.c_str(),
                              O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd >= 0) {
            ::dup2(fd, STDOUT_FILENO);
            ::dup2(fd, STDERR_FILENO);
            if (fd > STDERR_FILENO) {
                ::close(fd);
            }
        }
        ::execvp(argv[0], argv.data());
        ::_exit(127);
    }
    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        error = "cannot wait for the process";
        return false;
    }
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif

    std::ifstream input(outputPath, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    result.output = buffer.str();
    return true;
}

} // namespace lynxer::platform
