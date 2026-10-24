// Win32 calls behind the `winAPI.*` surface. See `winapi.hpp`.
//
// Values cross in and out as Lynxer `Value`s: integers, strings and booleans.
// Only this file includes <windows.h>; the rest of the interpreter goes through
// `lynxer/platform.hpp`, and this is a separate, opt-in surface rather than a
// platform primitive.

#include "winapi.hpp"

#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
// `std::size_t` and `std::int64_t` appear only in the Win32 implementations.
#include <cstddef>
#include <cstdint>
#endif

namespace lynxer::winapi {

namespace {

#if defined(_WIN32)

std::string wideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return std::string();
    }
    const int size =
        ::WideCharToMultiByte(CP_UTF8, 0, text.data(),
                              static_cast<int>(text.size()), nullptr, 0, nullptr,
                              nullptr);
    if (size <= 0) {
        return std::string();
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.data(),
                          static_cast<int>(text.size()), result.data(), size,
                          nullptr, nullptr);
    return result;
}

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return std::wstring();
    }
    const int size =
        ::MultiByteToWideChar(CP_UTF8, 0, text.data(),
                              static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) {
        return std::wstring();
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(),
                          static_cast<int>(text.size()), result.data(), size);
    return result;
}

// Reads a Win32 `*W` result into a string, growing the buffer when the call
// reports it is too small (returns the buffer size in that case).
template <typename Filler>
std::string win32Text(Filler fill) {
    std::wstring buffer(260, L'\0');
    for (;;) {
        const DWORD length =
            fill(buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return std::string();
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            return wideToUtf8(buffer);
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::string environmentVariable(const std::string& name) {
    const std::wstring wideName = utf8ToWide(name);
    const DWORD needed = ::GetEnvironmentVariableW(wideName.c_str(), nullptr, 0);
    if (needed == 0) {
        return std::string();
    }
    std::wstring buffer(needed, L'\0');
    const DWORD written =
        ::GetEnvironmentVariableW(wideName.c_str(), buffer.data(), needed);
    if (written == 0 || written >= needed) {
        return std::string();
    }
    buffer.resize(written);
    return wideToUtf8(buffer);
}

std::string win32ErrorText(DWORD code) {
    LPWSTR buffer = nullptr;
    const DWORD length = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    if (length == 0 || buffer == nullptr) {
        return std::string();
    }
    std::wstring text(buffer, length);
    ::LocalFree(buffer);
    while (!text.empty() &&
           (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) {
        text.pop_back();
    }
    return wideToUtf8(text);
}

std::string userName() {
    DWORD size = 0;
    ::GetUserNameW(nullptr, &size);
    if (size == 0) {
        return std::string();
    }
    std::wstring buffer(size, L'\0');
    if (::GetUserNameW(buffer.data(), &size) == 0) {
        return std::string();
    }
    if (size > 0 && buffer[size - 1] == L'\0') {
        buffer.resize(size - 1);
    }
    return wideToUtf8(buffer);
}

std::string osVersion() {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    const auto rtlGetVersion =
        ntdll == nullptr
            ? nullptr
            : reinterpret_cast<RtlGetVersionFn>(
                  ::GetProcAddress(ntdll, "RtlGetVersion"));
    if (rtlGetVersion == nullptr) {
        return std::string();
    }
    OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (rtlGetVersion(&info) != 0) {
        return std::string();
    }
    return std::to_string(info.dwMajorVersion) + "." +
           std::to_string(info.dwMinorVersion) + "." +
           std::to_string(info.dwBuildNumber);
}

std::string localeName() {
    wchar_t buffer[LOCALE_NAME_MAX_LENGTH];
    const int length =
        ::GetUserDefaultLocaleName(buffer, LOCALE_NAME_MAX_LENGTH);
    if (length <= 0) {
        return std::string();
    }
    return wideToUtf8(
        std::wstring(buffer, static_cast<std::size_t>(length - 1)));
}

std::string timeZoneName() {
    TIME_ZONE_INFORMATION info{};
    if (::GetTimeZoneInformation(&info) == TIME_ZONE_ID_INVALID) {
        return std::string();
    }
    return wideToUtf8(info.StandardName);
}

std::wstring namedPipePath(const std::string& name) {
    const std::wstring wide = utf8ToWide(name);
    if (wide.rfind(L"\\\\.\\pipe\\", 0) == 0) {
        return wide;
    }
    return L"\\\\.\\pipe\\" + wide;
}

#endif  // _WIN32

const std::vector<std::string>& names() {
    static const std::vector<std::string> all = {
        // Environment, identity and console-free system information.
        "getProcessId",       "getCurrentDirectory", "getComputerName",
        "getTempPath",        "getSystemDirectory",  "getWindowsDirectory",
        "getModuleFileName",  "getTickCount",        "getLastError",
        "getEnvironmentVariable", "setEnvironmentVariable", "expandEnvironmentStrings",
        "getDiskFreeBytes",   "sleep",               "outputDebugString",
        "beep",               "formatMessage",        "getUserName",
        "isUserAnAdmin",      "getSystemMetrics",     "getLogicalDrives",
        "getDriveType",       "getOSVersion",         "getLocaleName",
        "getTimeZoneName",
        // Shell and clipboard.
        "shellOpen",          "shellOpenWith",        "messageBox",
        "clipboardGet",       "clipboardSet",          "notify",
        // Named pipes.
        "createNamedPipe",    "connectNamedPipe",     "openNamedPipe",
        "readNamedPipe",      "writeNamedPipe",       "disconnectNamedPipe",
        "peekNamedPipe",
        // Files and handles.
        "createFile",         "readFile",            "writeFile",
        "closeHandle",        "fileSize",            "seekFile",
        "deleteFile",         "copyFile",            "moveFile",
        "createDirectory",    "removeDirectory",
        // Process control and the current thread.
        "getCurrentThreadId", "openProcess",         "terminateProcess",
        "getExitCodeProcess", "createProcess",       "waitForSingleObject",
        // Console session.
        "getStdHandle",       "getConsoleMode",      "setConsoleMode",
        "setConsoleTitle",    "getConsoleOutputCP",  "setConsoleOutputCP",
        // Files by name, flushing and the high-resolution timer.
        "getFileAttributes",  "setFileAttributes",   "getFullPathName",
        "flushFileBuffers",   "getKnownFolderPath",
        "queryPerformanceCounter", "queryPerformanceFrequency",
    };
    return all;
}

}  // namespace

bool available() {
#if defined(_WIN32)
    return true;
#else
    return false;
#endif
}

const std::vector<std::string>& supportedNames() { return names(); }

bool call(const std::string& name, const std::vector<Value>& args, Value& result,
          std::string& error) {
#if !defined(_WIN32)
    (void)name;
    (void)args;
    (void)result;
    error = "only available on Windows";
    return false;
#else
    const auto integerArg = [&](std::size_t index) -> std::int64_t {
        const auto* value = std::get_if<std::int64_t>(&args[index]);
        return value == nullptr ? 0 : *value;
    };
    const auto stringArg = [&](std::size_t index) -> std::string {
        const auto* value = std::get_if<std::string>(&args[index]);
        return value == nullptr ? std::string() : *value;
    };
    const auto wrong = [&](const std::string& shape) {
        error = "expects " + shape;
        return false;
    };
    const auto handleArg = [&](std::size_t index) -> HANDLE {
        return reinterpret_cast<HANDLE>(
            static_cast<std::intptr_t>(integerArg(index)));
    };

    if (name == "getProcessId") {
        if (!args.empty()) return wrong("no arguments");
        result = static_cast<std::int64_t>(::GetCurrentProcessId());
        return true;
    }
    if (name == "getCurrentDirectory") {
        if (!args.empty()) return wrong("no arguments");
        result = win32Text([](wchar_t* buffer, DWORD size) {
            return ::GetCurrentDirectoryW(size, buffer);
        });
        return true;
    }
    if (name == "getComputerName") {
        if (!args.empty()) return wrong("no arguments");
        result = win32Text([](wchar_t* buffer, DWORD size) {
            DWORD length = size;
            return ::GetComputerNameW(buffer, &length) != 0 ? length : 0;
        });
        return true;
    }
    if (name == "getTempPath") {
        if (!args.empty()) return wrong("no arguments");
        // Note the reversed (size, buffer) order, unlike the other *W calls.
        result = win32Text([](wchar_t* buffer, DWORD size) {
            return ::GetTempPathW(size, buffer);
        });
        return true;
    }
    if (name == "getSystemDirectory") {
        if (!args.empty()) return wrong("no arguments");
        result = win32Text([](wchar_t* buffer, DWORD size) {
            return static_cast<DWORD>(
                ::GetSystemDirectoryW(buffer, static_cast<UINT>(size)));
        });
        return true;
    }
    if (name == "getWindowsDirectory") {
        if (!args.empty()) return wrong("no arguments");
        result = win32Text([](wchar_t* buffer, DWORD size) {
            return static_cast<DWORD>(
                ::GetWindowsDirectoryW(buffer, static_cast<UINT>(size)));
        });
        return true;
    }
    if (name == "getModuleFileName") {
        if (!args.empty()) return wrong("no arguments");
        result = win32Text([](wchar_t* buffer, DWORD size) {
            return ::GetModuleFileNameW(nullptr, buffer, size);
        });
        return true;
    }
    if (name == "getTickCount") {
        if (!args.empty()) return wrong("no arguments");
        result = static_cast<std::int64_t>(::GetTickCount64());
        return true;
    }
    if (name == "getLastError") {
        if (!args.empty()) return wrong("no arguments");
        result = static_cast<std::int64_t>(::GetLastError());
        return true;
    }
    if (name == "formatMessage") {
        if (args.size() != 1) return wrong("an error code");
        if (std::get_if<std::int64_t>(&args[0]) == nullptr) {
            return wrong("an integer error code");
        }
        const DWORD code = static_cast<DWORD>(integerArg(0));
        result = win32ErrorText(code);
        return true;
    }
    if (name == "getUserName") {
        if (!args.empty()) return wrong("no arguments");
        result = userName();
        return true;
    }
    if (name == "isUserAnAdmin") {
        if (!args.empty()) return wrong("no arguments");
        result = ::IsUserAnAdmin() != 0;
        return true;
    }
    if (name == "getSystemMetrics") {
        if (args.size() != 1) return wrong("an index");
        if (std::get_if<std::int64_t>(&args[0]) == nullptr) {
            return wrong("an integer index");
        }
        result = static_cast<std::int64_t>(
            ::GetSystemMetrics(static_cast<int>(integerArg(0))));
        return true;
    }
    if (name == "getLogicalDrives") {
        if (!args.empty()) return wrong("no arguments");
        result = static_cast<std::int64_t>(::GetLogicalDrives());
        return true;
    }
    if (name == "getDriveType") {
        if (args.size() != 1) return wrong("a drive root");
        if (std::get_if<std::string>(&args[0]) == nullptr) {
            return wrong("a string drive root");
        }
        result = static_cast<std::int64_t>(
            ::GetDriveTypeW(utf8ToWide(stringArg(0)).c_str()));
        return true;
    }
    if (name == "getOSVersion") {
        if (!args.empty()) return wrong("no arguments");
        result = osVersion();
        return true;
    }
    if (name == "getLocaleName") {
        if (!args.empty()) return wrong("no arguments");
        result = localeName();
        return true;
    }
    if (name == "getTimeZoneName") {
        if (!args.empty()) return wrong("no arguments");
        result = timeZoneName();
        return true;
    }
    if (name == "shellOpen") {
        if (args.size() != 1) return wrong("a path");
        if (std::get_if<std::string>(&args[0]) == nullptr) {
            return wrong("a string path");
        }
        const std::wstring path = utf8ToWide(stringArg(0));
        const HINSTANCE instance = ::ShellExecuteW(
            nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        result = reinterpret_cast<std::intptr_t>(instance) > 32;
        return true;
    }
    if (name == "shellOpenWith") {
        if (args.size() != 3) return wrong("a path, arguments and a verb");
        if (std::get_if<std::string>(&args[0]) == nullptr ||
            std::get_if<std::string>(&args[1]) == nullptr ||
            std::get_if<std::string>(&args[2]) == nullptr) {
            return wrong("string path, arguments and verb");
        }
        const std::wstring path = utf8ToWide(stringArg(0));
        const std::wstring arguments = utf8ToWide(stringArg(1));
        const std::wstring verb =
            stringArg(2).empty() ? L"open" : utf8ToWide(stringArg(2));
        const HINSTANCE instance = ::ShellExecuteW(
            nullptr, verb.c_str(), path.c_str(),
            arguments.empty() ? nullptr : arguments.c_str(), nullptr,
            SW_SHOWNORMAL);
        result = reinterpret_cast<std::intptr_t>(instance) > 32;
        return true;
    }
    if (name == "messageBox") {
        if (args.size() != 2) return wrong("text and a title");
        if (std::get_if<std::string>(&args[0]) == nullptr ||
            std::get_if<std::string>(&args[1]) == nullptr) {
            return wrong("string text and title");
        }
        result = static_cast<std::int64_t>(
            ::MessageBoxW(nullptr, utf8ToWide(stringArg(0)).c_str(),
                          utf8ToWide(stringArg(1)).c_str(),
                          MB_OK | MB_ICONINFORMATION));
        return true;
    }
    if (name == "clipboardGet") {
        if (!args.empty()) return wrong("no arguments");
        if (::OpenClipboard(nullptr) == 0) {
            result = std::string();
            return true;
        }
        const HANDLE data = ::GetClipboardData(CF_UNICODETEXT);
        if (data == nullptr) {
            ::CloseClipboard();
            result = std::string();
            return true;
        }
        const auto* text =
            static_cast<const wchar_t*>(::GlobalLock(data));
        if (text == nullptr) {
            ::CloseClipboard();
            result = std::string();
            return true;
        }
        const std::wstring value(text);
        ::GlobalUnlock(data);
        ::CloseClipboard();
        result = wideToUtf8(value);
        return true;
    }
    if (name == "clipboardSet") {
        if (args.size() != 1) return wrong("text");
        if (std::get_if<std::string>(&args[0]) == nullptr) {
            return wrong("string text");
        }
        if (::OpenClipboard(nullptr) == 0) {
            result = false;
            return true;
        }
        if (::EmptyClipboard() == 0) {
            ::CloseClipboard();
            result = false;
            return true;
        }
        const std::wstring text = utf8ToWide(stringArg(0));
        const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL memory = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (memory == nullptr) {
            ::CloseClipboard();
            result = false;
            return true;
        }
        void* target = ::GlobalLock(memory);
        if (target == nullptr) {
            ::GlobalFree(memory);
            ::CloseClipboard();
            result = false;
            return true;
        }
        std::memcpy(target, text.c_str(), bytes);
        ::GlobalUnlock(memory);
        if (::SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
            ::GlobalFree(memory);
            ::CloseClipboard();
            result = false;
            return true;
        }
        ::CloseClipboard();
        result = true;
        return true;
    }
    if (name == "notify") {
        if (args.size() != 2) return wrong("a title and a message");
        if (std::get_if<std::string>(&args[0]) == nullptr ||
            std::get_if<std::string>(&args[1]) == nullptr) {
            return wrong("string title and message");
        }
        const HWND console = ::GetConsoleWindow();
        if (console == nullptr) {
            result = false;
            return true;
        }
        const std::wstring title = utf8ToWide(stringArg(0));
        const std::wstring message = utf8ToWide(stringArg(1));
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = console;
        data.uID = 1;
        data.uFlags = NIF_INFO | NIF_ICON | NIF_TIP;
        data.hIcon = ::LoadIconW(nullptr, MAKEINTRESOURCEW(32516));
        wcsncpy(data.szInfoTitle, title.c_str(),
                sizeof(data.szInfoTitle) / sizeof(wchar_t) - 1);
        wcsncpy(data.szInfo, message.c_str(),
                sizeof(data.szInfo) / sizeof(wchar_t) - 1);
        wcsncpy(data.szTip, L"Lynxer",
                sizeof(data.szTip) / sizeof(wchar_t) - 1);
        data.dwInfoFlags = NIIF_INFO;
        if (::Shell_NotifyIconW(NIM_ADD, &data) == 0) {
            result = false;
            return true;
        }
        ::Shell_NotifyIconW(NIM_MODIFY, &data);
        std::thread([data]() mutable {
            ::Sleep(5000);
            ::Shell_NotifyIconW(NIM_DELETE, &data);
        }).detach();
        result = true;
        return true;
    }
    if (name == "createNamedPipe") {
        if (args.size() != 1) return wrong("a pipe name");
        if (std::get_if<std::string>(&args[0]) == nullptr) {
            return wrong("a string pipe name");
        }
        const std::wstring path = namedPipePath(stringArg(0));
        const HANDLE handle = ::CreateNamedPipeW(
            path.c_str(), PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            result = static_cast<std::int64_t>(-1);
            return true;
        }
        result =
            static_cast<std::int64_t>(reinterpret_cast<std::intptr_t>(handle));
        return true;
    }
    if (name == "connectNamedPipe") {
        if (args.size() != 1) return wrong("a pipe handle");
        if (std::get_if<std::int64_t>(&args[0]) == nullptr) {
            return wrong("an integer pipe handle");
        }
        const HANDLE handle = handleArg(0);
        BOOL connected = ::ConnectNamedPipe(handle, nullptr);
        if (connected == 0 && ::GetLastError() == ERROR_PIPE_CONNECTED) {
            connected = TRUE;
        }
        result = connected != 0;
        return true;
    }
    if (name == "openNamedPipe") {
        if (args.size() != 1) return wrong("a pipe name");
        if (std::get_if<std::string>(&args[0]) == nullptr) {
            return wrong("a string pipe name");
        }
        const std::wstring path = namedPipePath(stringArg(0));
        const ULONGLONG deadline = ::GetTickCount64() + 5000;
        for (;;) {
            const HANDLE handle = ::CreateFileW(
                path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                OPEN_EXISTING, 0, nullptr);
            if (handle != INVALID_HANDLE_VALUE) {
                result = static_cast<std::int64_t>(
                    reinterpret_cast<std::intptr_t>(handle));
                return true;
            }
            const DWORD code = ::GetLastError();
            if (code != ERROR_PIPE_BUSY && code != ERROR_FILE_NOT_FOUND) {
                result = static_cast<std::int64_t>(-1);
                return true;
            }
            if (::GetTickCount64() >= deadline) {
                result = static_cast<std::int64_t>(-1);
                return true;
            }
            ::Sleep(50);
        }
    }
    if (name == "readNamedPipe") {
        if (args.size() != 2) return wrong("a pipe handle and a length");
        if (std::get_if<std::int64_t>(&args[0]) == nullptr ||
            std::get_if<std::int64_t>(&args[1]) == nullptr) {
            return wrong("an integer pipe handle and length");
        }
        std::int64_t length = integerArg(1);
        if (length <= 0) {
            result = std::string();
            return true;
        }
        if (length > 16 * 1024 * 1024) {
            length = 16 * 1024 * 1024;
        }
        std::string buffer(static_cast<std::size_t>(length), '\0');
        DWORD read = 0;
        if (::ReadFile(handleArg(0), buffer.data(),
                       static_cast<DWORD>(length), &read, nullptr) == 0) {
            result = std::string();
            return true;
        }
        buffer.resize(read);
        result = buffer;
        return true;
    }
    if (name == "writeNamedPipe") {
        if (args.size() != 2) return wrong("a pipe handle and text");
        if (std::get_if<std::int64_t>(&args[0]) == nullptr ||
            std::get_if<std::string>(&args[1]) == nullptr) {
            return wrong("an integer pipe handle and string text");
        }
        const std::string& text = stringArg(1);
        DWORD written = 0;
        if (::WriteFile(handleArg(0), text.data(),
                        static_cast<DWORD>(text.size()), &written,
                        nullptr) == 0) {
            result = static_cast<std::int64_t>(-1);
            return true;
        }
        result = static_cast<std::int64_t>(written);
        return true;
    }
    if (name == "disconnectNamedPipe") {
        if (args.size() != 1) return wrong("a pipe handle");
        if (std::get_if<std::int64_t>(&args[0]) == nullptr) {
            return wrong("an integer pipe handle");
        }
        result = ::DisconnectNamedPipe(handleArg(0)) != 0;
        return true;
    }
    if (name == "peekNamedPipe") {
        if (args.size() != 1) return wrong("a pipe handle");
        if (std::get_if<std::int64_t>(&args[0]) == nullptr) {
            return wrong("an integer pipe handle");
        }
        DWORD available = 0;
        if (::PeekNamedPipe(handleArg(0), nullptr, 0, nullptr, &available,
                            nullptr) == 0) {
            result = static_cast<std::int64_t>(-1);
            return true;
        }
        result = static_cast<std::int64_t>(available);
        return true;
    }
    if (name == "getEnvironmentVariable") {
        if (args.size() != 1) return wrong("a name");
        if (std::get_if<std::string>(&args[0]) == nullptr) {
            return wrong("a string name");
        }
        result = environmentVariable(stringArg(0));
        return true;
    }
    if (name == "setEnvironmentVariable") {
        if (args.size() != 2) return wrong("a name and a value");
        if (std::get_if<std::string>(&args[0]) == nullptr ||
            std::get_if<std::string>(&args[1]) == nullptr) {
            return wrong("string name and value");
        }
        result = ::SetEnvironmentVariableW(utf8ToWide(stringArg(0)).c_str(),
                                           utf8ToWide(stringArg(1)).c_str()) != 0;
        return true;
    }
    if (name == "sleep") {
        if (args.size() != 1) return wrong("a millisecond count");
        if (std::get_if<std::int64_t>(&args[0]) == nullptr) {
            return wrong("an integer millisecond count");
        }
        const std::int64_t milliseconds = integerArg(0);
        ::Sleep(static_cast<DWORD>(milliseconds < 0 ? 0 : milliseconds));
        result = Value{};
        return true;
    }
    if (name == "expandEnvironmentStrings") {
        if (args.size() != 1) return wrong("text");
        if (std::get_if<std::string>(&args[0]) == nullptr) {
            return wrong("string text");
        }
        const std::wstring input = utf8ToWide(stringArg(0));
        const DWORD needed =
            ::ExpandEnvironmentStringsW(input.c_str(), nullptr, 0);
        if (needed == 0) {
            result = std::string();
            return true;
        }
        std::wstring buffer(needed, L'\0');
        const DWORD written =
            ::ExpandEnvironmentStringsW(input.c_str(), buffer.data(), needed);
        if (written == 0 || written > needed) {
            result = std::string();
            return true;
        }
        // The returned count includes the terminating NUL.
        buffer.resize(static_cast<std::size_t>(written) - 1);
        result = wideToUtf8(buffer);
        return true;
    }
    if (name == "getDiskFreeBytes") {
        if (args.size() != 1) return wrong("a path");
        if (std::get_if<std::string>(&args[0]) == nullptr) {
            return wrong("a string path");
        }
        ULARGE_INTEGER available{};
        if (::GetDiskFreeSpaceExW(utf8ToWide(stringArg(0)).c_str(), &available,
                                  nullptr, nullptr) == 0) {
            result = static_cast<std::int64_t>(-1);
            return true;
        }
        result = static_cast<std::int64_t>(available.QuadPart);
        return true;
    }
    if (name == "outputDebugString") {
        if (args.size() != 1) return wrong("text");
        if (std::get_if<std::string>(&args[0]) == nullptr) {
            return wrong("string text");
        }
        ::OutputDebugStringW(utf8ToWide(stringArg(0)).c_str());
        result = Value{};
        return true;
    }
    if (name == "beep") {
        if (args.size() != 2) return wrong("a frequency and a duration");
        if (std::get_if<std::int64_t>(&args[0]) == nullptr ||
            std::get_if<std::int64_t>(&args[1]) == nullptr) {
            return wrong("integer frequency and duration");
        }
        result = ::Beep(static_cast<DWORD>(integerArg(0)),
                        static_cast<DWORD>(integerArg(1))) != 0;
        return true;
    }
    if (name == "createFile") {
        if (args.size() != 2) return wrong("a path and a mode");
        if (std::get_if<std::string>(&args[0]) == nullptr ||
            std::get_if<std::string>(&args[1]) == nullptr) {
            return wrong("a string path and mode");
        }
        const std::string mode = stringArg(1);
        DWORD access = GENERIC_READ;
        DWORD disposition = OPEN_EXISTING;
        if (mode == "read") {
            access = GENERIC_READ;
            disposition = OPEN_EXISTING;
        } else if (mode == "write") {
            access = GENERIC_WRITE;
            disposition = CREATE_ALWAYS;
        } else if (mode == "append") {
            access = GENERIC_WRITE;
            disposition = OPEN_ALWAYS;
        } else if (mode == "readwrite") {
            access = GENERIC_READ | GENERIC_WRITE;
            disposition = OPEN_ALWAYS;
        } else {
            return wrong("mode \"read\", \"write\", \"append\" or "
                         "\"readwrite\"");
        }
        const HANDLE handle =
            ::CreateFileW(utf8ToWide(stringArg(0)).c_str(), access,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                          disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            result = static_cast<std::int64_t>(-1);
            return true;
        }
        if (mode == "append") {
            LARGE_INTEGER end{};
            ::SetFilePointerEx(handle, end, nullptr, FILE_END);
        }
        result =
            static_cast<std::int64_t>(reinterpret_cast<std::intptr_t>(handle));
        return true;
    }
    if (name == "readFile") {
        if (args.size() != 2) return wrong("a handle and a length");
        if (std::get_if<std::int64_t>(&args[0]) == nullptr ||
            std::get_if<std::int64_t>(&args[1]) == nullptr) {
            return wrong("an integer handle and length");
        }
        const std::int64_t length = integerArg(1);
        if (length <= 0) {
            result = std::string();
            return true;
        }
        const HANDLE handle =
            reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(integerArg(0)));
        std::string buffer(static_cast<std::size_t>(length), '\0');
        DWORD read = 0;
        if (::ReadFile(handle, buffer.data(), static_cast<DWORD>(length), &read,
                       nullptr) == 0) {
            result = std::string();
            return true;
        }
        buffer.resize(read);
        result = buffer;
        return true;
    }
    if (name == "writeFile") {
        if (args.size() != 2) return wrong("a handle and text");
        if (std::get_if<std::int64_t>(&args[0]) == nullptr ||
            std::get_if<std::string>(&args[1]) == nullptr) {
            return wrong("an integer handle and string text");
        }
        const HANDLE handle =
            reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(integerArg(0)));
        const std::string& text = stringArg(1);
        DWORD written = 0;
        if (::WriteFile(handle, text.data(), static_cast<DWORD>(text.size()),
                        &written, nullptr) == 0) {
            result = static_cast<std::int64_t>(-1);
            return true;
        }
        result = static_cast<std::int64_t>(written);
        return true;
    }
    if (name == "closeHandle") {
        if (args.size() != 1) return wrong("a handle");
        if (std::get_if<std::int64_t>(&args[0]) == nullptr) {
            return wrong("an integer handle");
        }
        const HANDLE handle =
            reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(integerArg(0)));
        result = ::CloseHandle(handle) != 0;
        return true;
    }
    if (name == "fileSize") {
        if (args.size() != 1) return wrong("a handle");
        if (std::get_if<std::int64_t>(&args[0]) == nullptr) {
            return wrong("an integer handle");
        }
        const HANDLE handle =
            reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(integerArg(0)));
        LARGE_INTEGER size{};
        if (::GetFileSizeEx(handle, &size) == 0) {
            result = static_cast<std::int64_t>(-1);
            return true;
        }
        result = static_cast<std::int64_t>(size.QuadPart);
        return true;
    }
    if (name == "seekFile") {
        if (args.size() != 2) return wrong("a handle and an offset");
        if (std::get_if<std::int64_t>(&args[0]) == nullptr ||
            std::get_if<std::int64_t>(&args[1]) == nullptr) {
            return wrong("an integer handle and offset");
        }
        const HANDLE handle =
            reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(integerArg(0)));
        LARGE_INTEGER distance{};
        distance.QuadPart = integerArg(1);
        LARGE_INTEGER position{};
        if (::SetFilePointerEx(handle, distance, &position, FILE_BEGIN) == 0) {
            result = static_cast<std::int64_t>(-1);
            return true;
        }
        result = static_cast<std::int64_t>(position.QuadPart);
        return true;
    }
    if (name == "deleteFile" || name == "createDirectory" ||
        name == "removeDirectory") {
        if (args.size() != 1) return wrong("a path");
        if (std::get_if<std::string>(&args[0]) == nullptr) {
            return wrong("a string path");
        }
        const std::wstring path = utf8ToWide(stringArg(0));
        if (name == "deleteFile") {
            result = ::DeleteFileW(path.c_str()) != 0;
        } else if (name == "createDirectory") {
            result = ::CreateDirectoryW(path.c_str(), nullptr) != 0;
        } else {
            result = ::RemoveDirectoryW(path.c_str()) != 0;
        }
        return true;
    }
    if (name == "copyFile") {
        if (args.size() != 3) return wrong("a source, a destination and a flag");
        if (std::get_if<std::string>(&args[0]) == nullptr ||
            std::get_if<std::string>(&args[1]) == nullptr ||
            std::get_if<bool>(&args[2]) == nullptr) {
            return wrong("string source and destination and a bool flag");
        }
        const auto* overwrite = std::get_if<bool>(&args[2]);
        result = ::CopyFileW(utf8ToWide(stringArg(0)).c_str(),
                             utf8ToWide(stringArg(1)).c_str(),
                             *overwrite ? FALSE : TRUE) != 0;
        return true;
    }
    if (name == "moveFile") {
        if (args.size() != 2) return wrong("a source and a destination");
        if (std::get_if<std::string>(&args[0]) == nullptr ||
            std::get_if<std::string>(&args[1]) == nullptr) {
            return wrong("string source and destination");
        }
        result = ::MoveFileExW(utf8ToWide(stringArg(0)).c_str(),
                               utf8ToWide(stringArg(1)).c_str(),
                               MOVEFILE_REPLACE_EXISTING) != 0;
        return true;
    }
    if (name == "getCurrentThreadId") {
        if (!args.empty()) return wrong("no arguments");
        result = static_cast<std::int64_t>(::GetCurrentThreadId());
        return true;
    }
    if (name == "openProcess") {
        if (args.size() != 1) return wrong("a process id");
        if (std::get_if<std::int64_t>(&args[0]) == nullptr) {
            return wrong("an integer process id");
        }
        const HANDLE handle = ::OpenProcess(
            PROCESS_QUERY_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE, FALSE,
            static_cast<DWORD>(integerArg(0)));
        result = handle == nullptr
                     ? static_cast<std::int64_t>(-1)
                     : static_cast<std::int64_t>(
                           reinterpret_cast<std::intptr_t>(handle));
        return true;
    }
    if (name == "terminateProcess") {
        if (args.size() != 2) return wrong("a handle and an exit code");
        if (std::get_if<std::int64_t>(&args[0]) == nullptr ||
            std::get_if<std::int64_t>(&args[1]) == nullptr) {
            return wrong("an integer handle and exit code");
        }
        result =
            ::TerminateProcess(handleArg(0),
                               static_cast<UINT>(integerArg(1))) != 0;
        return true;
    }
    if (name == "getExitCodeProcess") {
        if (args.size() != 1) return wrong("a handle");
        if (std::get_if<std::int64_t>(&args[0]) == nullptr) {
            return wrong("an integer handle");
        }
        DWORD code = 0;
        if (::GetExitCodeProcess(handleArg(0), &code) == 0) {
            result = static_cast<std::int64_t>(-1);
            return true;
        }
        result = static_cast<std::int64_t>(code);
        return true;
    }
    if (name == "createProcess") {
        if (args.size() != 1) return wrong("a command line");
        if (std::get_if<std::string>(&args[0]) == nullptr) {
            return wrong("a string command line");
        }
        // CreateProcessW may write into the command line, so keep a copy.
        std::wstring command = utf8ToWide(stringArg(0));
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info{};
        if (::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0,
                             nullptr, nullptr, &startup, &info) == 0) {
            result = static_cast<std::int64_t>(-1);
            return true;
        }
        ::CloseHandle(info.hThread);
        result = static_cast<std::int64_t>(
            reinterpret_cast<std::intptr_t>(info.hProcess));
        return true;
    }
    if (name == "waitForSingleObject") {
        if (args.size() != 2) return wrong("a handle and a timeout");
        if (std::get_if<std::int64_t>(&args[0]) == nullptr ||
            std::get_if<std::int64_t>(&args[1]) == nullptr) {
            return wrong("an integer handle and timeout");
        }
        const DWORD status = ::WaitForSingleObject(
            handleArg(0), static_cast<DWORD>(integerArg(1)));
        result = status == WAIT_FAILED ? static_cast<std::int64_t>(-1)
                                       : static_cast<std::int64_t>(status);
        return true;
    }
    if (name == "getStdHandle") {
        if (args.size() != 1) return wrong("a stream name");
        if (std::get_if<std::string>(&args[0]) == nullptr) {
            return wrong("a string stream name");
        }
        const std::string which = stringArg(0);
        DWORD id = 0;
        if (which == "input") {
            id = STD_INPUT_HANDLE;
        } else if (which == "output") {
            id = STD_OUTPUT_HANDLE;
        } else if (which == "error") {
            id = STD_ERROR_HANDLE;
        } else {
            return wrong("stream \"input\", \"output\" or \"error\"");
        }
        const HANDLE handle = ::GetStdHandle(id);
        result = (handle == nullptr || handle == INVALID_HANDLE_VALUE)
                     ? static_cast<std::int64_t>(-1)
                     : static_cast<std::int64_t>(
                           reinterpret_cast<std::intptr_t>(handle));
        return true;
    }
    if (name == "getConsoleMode") {
        if (args.size() != 1) return wrong("a handle");
        if (std::get_if<std::int64_t>(&args[0]) == nullptr) {
            return wrong("an integer handle");
        }
        DWORD mode = 0;
        result = ::GetConsoleMode(handleArg(0), &mode) != 0
                     ? static_cast<std::int64_t>(mode)
                     : static_cast<std::int64_t>(-1);
        return true;
    }
    if (name == "setConsoleMode") {
        if (args.size() != 2) return wrong("a handle and a mode");
        if (std::get_if<std::int64_t>(&args[0]) == nullptr ||
            std::get_if<std::int64_t>(&args[1]) == nullptr) {
            return wrong("an integer handle and mode");
        }
        result = ::SetConsoleMode(handleArg(0),
                                  static_cast<DWORD>(integerArg(1))) != 0;
        return true;
    }
    if (name == "setConsoleTitle") {
        if (args.size() != 1) return wrong("text");
        if (std::get_if<std::string>(&args[0]) == nullptr) {
            return wrong("string text");
        }
        result = ::SetConsoleTitleW(utf8ToWide(stringArg(0)).c_str()) != 0;
        return true;
    }
    if (name == "getConsoleOutputCP") {
        if (!args.empty()) return wrong("no arguments");
        result = static_cast<std::int64_t>(::GetConsoleOutputCP());
        return true;
    }
    if (name == "setConsoleOutputCP") {
        if (args.size() != 1) return wrong("a code page");
        if (std::get_if<std::int64_t>(&args[0]) == nullptr) {
            return wrong("an integer code page");
        }
        result = ::SetConsoleOutputCP(static_cast<UINT>(integerArg(0))) != 0;
        return true;
    }
    if (name == "getFileAttributes") {
        if (args.size() != 1) return wrong("a path");
        if (std::get_if<std::string>(&args[0]) == nullptr) {
            return wrong("a string path");
        }
        const DWORD attributes =
            ::GetFileAttributesW(utf8ToWide(stringArg(0)).c_str());
        result = attributes == INVALID_FILE_ATTRIBUTES
                     ? static_cast<std::int64_t>(-1)
                     : static_cast<std::int64_t>(attributes);
        return true;
    }
    if (name == "setFileAttributes") {
        if (args.size() != 2) return wrong("a path and attributes");
        if (std::get_if<std::string>(&args[0]) == nullptr ||
            std::get_if<std::int64_t>(&args[1]) == nullptr) {
            return wrong("a string path and integer attributes");
        }
        result = ::SetFileAttributesW(utf8ToWide(stringArg(0)).c_str(),
                                      static_cast<DWORD>(integerArg(1))) != 0;
        return true;
    }
    if (name == "getFullPathName") {
        if (args.size() != 1) return wrong("a path");
        if (std::get_if<std::string>(&args[0]) == nullptr) {
            return wrong("a string path");
        }
        const std::wstring input = utf8ToWide(stringArg(0));
        const DWORD needed =
            ::GetFullPathNameW(input.c_str(), 0, nullptr, nullptr);
        if (needed == 0) {
            result = std::string();
            return true;
        }
        std::wstring buffer(needed, L'\0');
        const DWORD written =
            ::GetFullPathNameW(input.c_str(), needed, buffer.data(), nullptr);
        if (written == 0 || written >= needed) {
            result = std::string();
            return true;
        }
        buffer.resize(written);
        result = wideToUtf8(buffer);
        return true;
    }
    if (name == "flushFileBuffers") {
        if (args.size() != 1) return wrong("a handle");
        if (std::get_if<std::int64_t>(&args[0]) == nullptr) {
            return wrong("an integer handle");
        }
        result = ::FlushFileBuffers(handleArg(0)) != 0;
        return true;
    }
    if (name == "getKnownFolderPath") {
        if (args.size() != 1) return wrong("a folder name");
        if (std::get_if<std::string>(&args[0]) == nullptr) {
            return wrong("a string folder name");
        }
        const std::string which = stringArg(0);
        const KNOWNFOLDERID* folder = nullptr;
        if (which == "desktop") {
            folder = &FOLDERID_Desktop;
        } else if (which == "documents") {
            folder = &FOLDERID_Documents;
        } else if (which == "downloads") {
            folder = &FOLDERID_Downloads;
        } else if (which == "music") {
            folder = &FOLDERID_Music;
        } else if (which == "pictures") {
            folder = &FOLDERID_Pictures;
        } else if (which == "videos") {
            folder = &FOLDERID_Videos;
        } else if (which == "localAppData") {
            folder = &FOLDERID_LocalAppData;
        } else if (which == "roamingAppData") {
            folder = &FOLDERID_RoamingAppData;
        } else if (which == "programData") {
            folder = &FOLDERID_ProgramData;
        } else if (which == "profile") {
            folder = &FOLDERID_Profile;
        } else if (which == "public") {
            folder = &FOLDERID_Public;
        } else if (which == "windows") {
            folder = &FOLDERID_Windows;
        } else if (which == "system") {
            folder = &FOLDERID_System;
        } else {
            result = std::string();
            return true;
        }
        PWSTR raw = nullptr;
        if (FAILED(::SHGetKnownFolderPath(*folder, KF_FLAG_DEFAULT, nullptr,
                                          &raw))) {
            result = std::string();
            return true;
        }
        const std::wstring path(raw);
        ::CoTaskMemFree(raw);
        result = wideToUtf8(path);
        return true;
    }
    if (name == "queryPerformanceCounter") {
        if (!args.empty()) return wrong("no arguments");
        LARGE_INTEGER value{};
        result = ::QueryPerformanceCounter(&value) != 0
                     ? static_cast<std::int64_t>(value.QuadPart)
                     : static_cast<std::int64_t>(-1);
        return true;
    }
    if (name == "queryPerformanceFrequency") {
        if (!args.empty()) return wrong("no arguments");
        LARGE_INTEGER value{};
        result = ::QueryPerformanceFrequency(&value) != 0
                     ? static_cast<std::int64_t>(value.QuadPart)
                     : static_cast<std::int64_t>(-1);
        return true;
    }
    error = "unknown operation";
    return false;
#endif
}

} // namespace lynxer::winapi
