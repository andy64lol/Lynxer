// Win32 calls behind the `winAPI.*` surface. See `winapi.hpp`.
//
// Values cross in and out as Lynxer `Value`s: integers, strings and booleans.
// Only this file includes <windows.h>; the rest of the interpreter goes through
// `lynxer/platform.hpp`, and this is a separate, opt-in surface rather than a
// platform primitive.

#include "winapi.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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
        "getProcessId",       "getCurrentDirectory", "getComputerName",
        "getTempPath",        "getSystemDirectory",  "getWindowsDirectory",
        "getModuleFileName",  "getTickCount",        "getLastError",
        "getEnvironmentVariable", "setEnvironmentVariable", "sleep",
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
    error = "unknown operation";
    return false;
#endif
}

} // namespace lynxer::winapi
