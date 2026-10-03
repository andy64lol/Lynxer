// Win32 calls behind the `winAPI.*` surface. See `winapi.hpp`.
//
// Values cross in and out as Lynxer `Value`s: integers, strings and booleans.
// Only this file includes <windows.h>; the rest of the interpreter goes through
// `lynxer/platform.hpp`, and this is a separate, opt-in surface rather than a
// platform primitive.

#include "winapi.hpp"

#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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

#endif  // _WIN32

const std::vector<std::string>& names() {
    static const std::vector<std::string> all = {
        // Environment, identity and console-free system information.
        "getProcessId",       "getCurrentDirectory", "getComputerName",
        "getTempPath",        "getSystemDirectory",  "getWindowsDirectory",
        "getModuleFileName",  "getTickCount",        "getLastError",
        "getEnvironmentVariable", "setEnvironmentVariable", "expandEnvironmentStrings",
        "getDiskFreeBytes",   "sleep",               "outputDebugString",
        "beep",
        // Files and handles.
        "createFile",         "readFile",            "writeFile",
        "closeHandle",        "fileSize",            "seekFile",
        "deleteFile",         "copyFile",            "moveFile",
        "createDirectory",    "removeDirectory",
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
    error = "unknown operation";
    return false;
#endif
}

} // namespace lynxer::winapi
