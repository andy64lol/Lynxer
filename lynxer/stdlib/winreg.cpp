// Lynxer `winreg` stdlib backend: Windows registry reader/writer.
//
// Windows-only. The wrapper (`winreg.lynx`) exposes a small string/int API;
// roots are named `HKCU`, `HKLM`, `HKCR`, `HKU` and `HKCC` (the long
// `HKEY_*` spellings are accepted too). Writes create the key when it does
// not exist, so a program can set a value without a separate create call.

#include <cctype>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

using RegisterFunction = int (*)(const char*, const char*, const char*);
using RegisterConstant = int (*)(const char*, std::int64_t);
using RegisterType = int (*)(const char*, const char*);

namespace {

const char* stable(std::string value) {
    thread_local std::string result;
    result = std::move(value);
    return result.c_str();
}

std::string textOrEmpty(const char* text) {
    return text == nullptr ? std::string() : std::string(text);
}

std::wstring toWide(const std::string& text) {
    if (text.empty()) {
        return std::wstring();
    }
    const int size = ::MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                           static_cast<int>(text.size()),
                                           nullptr, 0);
    if (size <= 0) {
        return std::wstring();
    }
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(),
                          static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

std::string toUtf8(const std::wstring& wide) {
    if (wide.empty()) {
        return std::string();
    }
    const int size = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                           static_cast<int>(wide.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return std::string();
    }
    std::string text(static_cast<std::size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                          static_cast<int>(wide.size()), text.data(), size,
                          nullptr, nullptr);
    return text;
}

std::string upper(std::string text) {
    for (char& character : text) {
        character = static_cast<char>(
            std::toupper(static_cast<unsigned char>(character)));
    }
    return text;
}

HKEY rootKey(const char* name) {
    const std::string root = upper(textOrEmpty(name));
    if (root == "HKCU" || root == "HKEY_CURRENT_USER") {
        return HKEY_CURRENT_USER;
    }
    if (root == "HKLM" || root == "HKEY_LOCAL_MACHINE") {
        return HKEY_LOCAL_MACHINE;
    }
    if (root == "HKCR" || root == "HKEY_CLASSES_ROOT") {
        return HKEY_CLASSES_ROOT;
    }
    if (root == "HKU" || root == "HKEY_USERS") {
        return HKEY_USERS;
    }
    if (root == "HKCC" || root == "HKEY_CURRENT_CONFIG") {
        return HKEY_CURRENT_CONFIG;
    }
    return nullptr;
}

bool openKey(const char* root, const char* path, REGSAM access, HKEY& key) {
    const HKEY base = rootKey(root);
    if (base == nullptr) {
        return false;
    }
    const std::wstring subkey = toWide(textOrEmpty(path));
    const wchar_t* subkeyName = subkey.empty() ? nullptr : subkey.c_str();
    key = nullptr;
    return ::RegOpenKeyExW(base, subkeyName, 0, access, &key) ==
           ERROR_SUCCESS;
}

bool createKey(const char* root, const char* path, REGSAM access, HKEY& key) {
    const HKEY base = rootKey(root);
    if (base == nullptr) {
        return false;
    }
    const std::wstring subkey = toWide(textOrEmpty(path));
    if (subkey.empty()) {
        return false;
    }
    DWORD disposition = 0;
    key = nullptr;
    return ::RegCreateKeyExW(base, subkey.c_str(), 0, nullptr,
                             REG_OPTION_NON_VOLATILE, access, nullptr, &key,
                             &disposition) == ERROR_SUCCESS;
}

struct Value {
    DWORD type = REG_NONE;
    std::vector<BYTE> bytes;
};

bool queryValue(const char* root, const char* path, const char* name,
                Value& value) {
    HKEY key = nullptr;
    if (!openKey(root, path, KEY_QUERY_VALUE, key)) {
        return false;
    }
    const std::wstring valueName = toWide(textOrEmpty(name));
    const wchar_t* valueNamePointer =
        valueName.empty() ? L"" : valueName.c_str();
    DWORD size = 0;
    LONG status = ::RegQueryValueExW(key, valueNamePointer, nullptr,
                                     &value.type, nullptr, &size);
    if (status != ERROR_SUCCESS && status != ERROR_MORE_DATA) {
        ::RegCloseKey(key);
        return false;
    }
    value.bytes.resize(size);
    status = ::RegQueryValueExW(key, valueNamePointer, nullptr, &value.type,
                                value.bytes.empty() ? nullptr
                                                    : value.bytes.data(),
                                &size);
    ::RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    value.bytes.resize(size);
    return true;
}

std::string stringFromValue(const Value& value) {
    if (value.type == REG_SZ || value.type == REG_EXPAND_SZ) {
        const std::size_t count = value.bytes.size() / sizeof(wchar_t);
        const wchar_t* text = reinterpret_cast<const wchar_t*>(
            value.bytes.data());
        std::size_t length = count;
        while (length > 0 && text[length - 1] == L'\0') {
            --length;
        }
        return toUtf8(std::wstring(text, length));
    }
    if (value.type == REG_MULTI_SZ) {
        const std::size_t count = value.bytes.size() / sizeof(wchar_t);
        const wchar_t* text = reinterpret_cast<const wchar_t*>(
            value.bytes.data());
        std::string joined;
        std::size_t start = 0;
        for (std::size_t index = 0; index < count; ++index) {
            if (text[index] != L'\0') {
                continue;
            }
            if (index > start) {
                if (!joined.empty()) {
                    joined += '\n';
                }
                joined += toUtf8(std::wstring(text + start, index - start));
            }
            start = index + 1;
        }
        return joined;
    }
    if (value.type == REG_DWORD && value.bytes.size() >= sizeof(DWORD)) {
        DWORD number = 0;
        std::memcpy(&number, value.bytes.data(), sizeof(number));
        return std::to_string(number);
    }
    if (value.type == REG_QWORD && value.bytes.size() >= sizeof(ULONGLONG)) {
        ULONGLONG number = 0;
        std::memcpy(&number, value.bytes.data(), sizeof(number));
        return std::to_string(number);
    }
    return std::string();
}

std::string joinNames(const std::vector<std::string>& names) {
    std::string joined;
    for (const std::string& name : names) {
        if (!joined.empty()) {
            joined += '\n';
        }
        joined += name;
    }
    return joined;
}

std::string listKeysImpl(const char* root, const char* path) {
    HKEY key = nullptr;
    if (!openKey(root, path, KEY_ENUMERATE_SUB_KEYS, key)) {
        return std::string();
    }
    std::vector<std::string> names;
    for (DWORD index = 0;; ++index) {
        wchar_t buffer[512];
        DWORD length = static_cast<DWORD>(std::size(buffer));
        const LONG status = ::RegEnumKeyExW(key, index, buffer, &length,
                                            nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (status != ERROR_SUCCESS) {
            ::RegCloseKey(key);
            return std::string();
        }
        names.push_back(toUtf8(std::wstring(buffer, length)));
    }
    ::RegCloseKey(key);
    return joinNames(names);
}

std::string listValuesImpl(const char* root, const char* path) {
    HKEY key = nullptr;
    if (!openKey(root, path, KEY_QUERY_VALUE, key)) {
        return std::string();
    }
    std::vector<std::string> names;
    for (DWORD index = 0;; ++index) {
        wchar_t buffer[512];
        DWORD length = static_cast<DWORD>(std::size(buffer));
        const LONG status =
            ::RegEnumValueW(key, index, buffer, &length, nullptr, nullptr,
                            nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (status != ERROR_SUCCESS) {
            ::RegCloseKey(key);
            return std::string();
        }
        names.push_back(toUtf8(std::wstring(buffer, length)));
    }
    ::RegCloseKey(key);
    return joinNames(names);
}

bool writeStringValue(const char* root, const char* path, const char* name,
                      const char* value, DWORD type) {
    HKEY key = nullptr;
    if (!createKey(root, path, KEY_SET_VALUE, key)) {
        return false;
    }
    const std::wstring valueName = toWide(textOrEmpty(name));
    const std::wstring text = toWide(textOrEmpty(value));
    const DWORD size =
        static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t));
    const LONG status = ::RegSetValueExW(
        key, valueName.empty() ? L"" : valueName.c_str(), 0, type,
        reinterpret_cast<const BYTE*>(text.c_str()), size);
    ::RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

bool writeIntValue(const char* root, const char* path, const char* name,
                   std::int64_t value) {
    HKEY key = nullptr;
    if (!createKey(root, path, KEY_SET_VALUE, key)) {
        return false;
    }
    const std::wstring valueName = toWide(textOrEmpty(name));
    const wchar_t* valueNamePointer =
        valueName.empty() ? L"" : valueName.c_str();
    LONG status = ERROR_SUCCESS;
    if (value >= 0 && value <= 0xFFFFFFFFLL) {
        const DWORD number = static_cast<DWORD>(value);
        status = ::RegSetValueExW(key, valueNamePointer, 0, REG_DWORD,
                                  reinterpret_cast<const BYTE*>(&number),
                                  sizeof(number));
    } else {
        const ULONGLONG number = static_cast<ULONGLONG>(value);
        status = ::RegSetValueExW(key, valueNamePointer, 0, REG_QWORD,
                                  reinterpret_cast<const BYTE*>(&number),
                                  sizeof(number));
    }
    ::RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

} // namespace

extern "C" std::int64_t winreg_key_exists(const char* root,
                                          const char* path) {
    HKEY key = nullptr;
    if (!openKey(root, path, KEY_READ, key)) {
        return 0;
    }
    ::RegCloseKey(key);
    return 1;
}

extern "C" std::int64_t winreg_value_exists(const char* root,
                                            const char* path,
                                            const char* name) {
    Value value;
    return queryValue(root, path, name, value) ? 1 : 0;
}

extern "C" const char* winreg_read_string(const char* root, const char* path,
                                          const char* name) {
    Value value;
    if (!queryValue(root, path, name, value)) {
        return stable("");
    }
    return stable(stringFromValue(value));
}

extern "C" std::int64_t winreg_read_int(const char* root, const char* path,
                                        const char* name) {
    Value value;
    if (!queryValue(root, path, name, value)) {
        return -1;
    }
    if (value.type == REG_DWORD && value.bytes.size() >= sizeof(DWORD)) {
        DWORD number = 0;
        std::memcpy(&number, value.bytes.data(), sizeof(number));
        return static_cast<std::int64_t>(number);
    }
    if (value.type == REG_QWORD && value.bytes.size() >= sizeof(ULONGLONG)) {
        ULONGLONG number = 0;
        std::memcpy(&number, value.bytes.data(), sizeof(number));
        return static_cast<std::int64_t>(number);
    }
    return -1;
}

extern "C" const char* winreg_read_default(const char* root,
                                           const char* path) {
    return winreg_read_string(root, path, "");
}

extern "C" std::int64_t winreg_write_string(const char* root,
                                            const char* path,
                                            const char* name,
                                            const char* value) {
    return writeStringValue(root, path, name, value, REG_SZ) ? 1 : 0;
}

extern "C" std::int64_t winreg_write_expand_string(const char* root,
                                                   const char* path,
                                                   const char* name,
                                                   const char* value) {
    return writeStringValue(root, path, name, value, REG_EXPAND_SZ) ? 1 : 0;
}

extern "C" std::int64_t winreg_write_int(const char* root, const char* path,
                                         const char* name,
                                         std::int64_t value) {
    return writeIntValue(root, path, name, value) ? 1 : 0;
}

extern "C" std::int64_t winreg_write_default(const char* root,
                                             const char* path,
                                             const char* value) {
    return writeStringValue(root, path, "", value, REG_SZ) ? 1 : 0;
}

extern "C" std::int64_t winreg_create_key(const char* root,
                                          const char* path) {
    HKEY key = nullptr;
    if (!createKey(root, path, KEY_WRITE, key)) {
        return 0;
    }
    ::RegCloseKey(key);
    return 1;
}

extern "C" std::int64_t winreg_delete_value(const char* root,
                                            const char* path,
                                            const char* name) {
    HKEY key = nullptr;
    if (!openKey(root, path, KEY_SET_VALUE, key)) {
        return 0;
    }
    const std::wstring valueName = toWide(textOrEmpty(name));
    const LONG status =
        ::RegDeleteValueW(key, valueName.empty() ? L"" : valueName.c_str());
    ::RegCloseKey(key);
    return status == ERROR_SUCCESS ? 1 : 0;
}

extern "C" std::int64_t winreg_delete_key(const char* root,
                                          const char* path) {
    const HKEY base = rootKey(root);
    const std::wstring subkey = toWide(textOrEmpty(path));
    if (base == nullptr || subkey.empty()) {
        return 0;
    }
    return ::RegDeleteKeyW(base, subkey.c_str()) == ERROR_SUCCESS ? 1 : 0;
}

extern "C" std::int64_t winreg_delete_tree(const char* root,
                                           const char* path) {
    const HKEY base = rootKey(root);
    const std::wstring subkey = toWide(textOrEmpty(path));
    if (base == nullptr || subkey.empty()) {
        return 0;
    }
    const LONG status = ::RegDeleteTreeW(base, subkey.c_str());
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND ? 1 : 0;
}

extern "C" const char* winreg_list_keys(const char* root,
                                        const char* path) {
    return stable(listKeysImpl(root, path));
}

extern "C" const char* winreg_list_values(const char* root,
                                          const char* path) {
    return stable(listValuesImpl(root, path));
}

extern "C" int lynxer_module_init_v1(RegisterFunction function,
                                     RegisterConstant, RegisterType) {
    return (function("keyExists", "winreg_key_exists",
                     "cdecl:int64(cstring,cstring)") &&
            function("valueExists", "winreg_value_exists",
                     "cdecl:int64(cstring,cstring,cstring)") &&
            function("readString", "winreg_read_string",
                     "cdecl:cstring(cstring,cstring,cstring)") &&
            function("readInt", "winreg_read_int",
                     "cdecl:int64(cstring,cstring,cstring)") &&
            function("readDefault", "winreg_read_default",
                     "cdecl:cstring(cstring,cstring)") &&
            function("writeString", "winreg_write_string",
                     "cdecl:int64(cstring,cstring,cstring,cstring)") &&
            function("writeExpandString", "winreg_write_expand_string",
                     "cdecl:int64(cstring,cstring,cstring,cstring)") &&
            function("writeInt", "winreg_write_int",
                     "cdecl:int64(cstring,cstring,cstring,int64)") &&
            function("writeDefault", "winreg_write_default",
                     "cdecl:int64(cstring,cstring,cstring)") &&
            function("createKey", "winreg_create_key",
                     "cdecl:int64(cstring,cstring)") &&
            function("deleteValue", "winreg_delete_value",
                     "cdecl:int64(cstring,cstring,cstring)") &&
            function("deleteKey", "winreg_delete_key",
                     "cdecl:int64(cstring,cstring)") &&
            function("deleteTree", "winreg_delete_tree",
                     "cdecl:int64(cstring,cstring)") &&
            function("listKeys", "winreg_list_keys",
                     "cdecl:cstring(cstring,cstring)") &&
            function("listValues", "winreg_list_values",
                     "cdecl:cstring(cstring,cstring)"))
               ? 0
               : 1;
}
