// Host-abstraction layer implementation. The POSIX half is the reference and is
// exercised by the Linux test suite; the `_WIN32` half mirrors it with Win32
// calls and is compiled only when targeting Windows.

#include "platform.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
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
    PROCESS_INFORMATION process{};
    if (::CreateProcessA(nullptr, mutableLine.data(), nullptr, nullptr, FALSE,
                         0, nullptr, nullptr, &startup, &process) == 0) {
        return -1;
    }
    ::WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    ::GetExitCodeProcess(process.hProcess, &code);
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
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

} // namespace lynxer::platform
