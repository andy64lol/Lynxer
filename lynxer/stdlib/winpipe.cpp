// Lynxer `winpipe` stdlib backend: blocking Windows named pipes.
//
// Windows-only. Names are relative to `\\.\pipe\`; a full `\\.\pipe\name`
// path is also accepted. Server handles come from `create` and block in
// `connect`; client handles come from `open`, which retries for five seconds
// while the server is still creating the pipe. Reads and writes are blocking,
// and every handle is released with `close`.

#include "lynxer_native_abi.h"

#include <cstdint>
#include <string>

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

std::wstring toWide(const char* text) {
    if (text == nullptr || *text == '\0') {
        return std::wstring();
    }
    const int size =
        ::MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (size <= 0) {
        return std::wstring();
    }
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text, -1, wide.data(), size);
    wide.resize(static_cast<std::size_t>(size - 1));
    return wide;
}

std::wstring pipePath(const char* name) {
    const std::wstring wide = toWide(name);
    if (wide.rfind(L"\\\\.\\pipe\\", 0) == 0) {
        return wide;
    }
    return L"\\\\.\\pipe\\" + wide;
}

HANDLE handleOf(std::int64_t value) {
    return reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(value));
}

std::int64_t valueOf(HANDLE handle) {
    return static_cast<std::int64_t>(reinterpret_cast<std::intptr_t>(handle));
}

}  // namespace

extern "C" std::int64_t winpipe_create(const char* name) {
    const std::wstring path = pipePath(name);
    if (path.empty()) {
        return -1;
    }
    const HANDLE handle = ::CreateNamedPipeW(
        path.c_str(), PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, nullptr);
    return handle == INVALID_HANDLE_VALUE ? -1 : valueOf(handle);
}

extern "C" std::int64_t winpipe_connect(std::int64_t handle) {
    BOOL connected = ::ConnectNamedPipe(handleOf(handle), nullptr);
    if (connected == 0 && ::GetLastError() == ERROR_PIPE_CONNECTED) {
        connected = TRUE;
    }
    return connected != 0 ? 1 : 0;
}

extern "C" std::int64_t winpipe_open(const char* name) {
    const std::wstring path = pipePath(name);
    if (path.empty()) {
        return -1;
    }
    const ULONGLONG deadline = ::GetTickCount64() + 5000;
    for (;;) {
        const HANDLE handle =
            ::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                          nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            return valueOf(handle);
        }
        const DWORD code = ::GetLastError();
        if (code != ERROR_PIPE_BUSY && code != ERROR_FILE_NOT_FOUND) {
            return -1;
        }
        if (::GetTickCount64() >= deadline) {
            return -1;
        }
        ::Sleep(50);
    }
}

extern "C" const char* winpipe_read(std::int64_t handle, std::int64_t length) {
    if (length <= 0) {
        return stable(std::string());
    }
    if (length > 16 * 1024 * 1024) {
        length = 16 * 1024 * 1024;
    }
    std::string buffer(static_cast<std::size_t>(length), '\0');
    DWORD read = 0;
    if (::ReadFile(handleOf(handle), buffer.data(),
                   static_cast<DWORD>(length), &read, nullptr) == 0) {
        return stable(std::string());
    }
    buffer.resize(read);
    return stable(std::move(buffer));
}

extern "C" std::int64_t winpipe_write(std::int64_t handle, const char* text) {
    const std::string value = text == nullptr ? std::string() : std::string(text);
    DWORD written = 0;
    if (::WriteFile(handleOf(handle), value.data(),
                    static_cast<DWORD>(value.size()), &written, nullptr) == 0) {
        return -1;
    }
    return static_cast<std::int64_t>(written);
}

extern "C" std::int64_t winpipe_disconnect(std::int64_t handle) {
    return ::DisconnectNamedPipe(handleOf(handle)) != 0 ? 1 : 0;
}

extern "C" std::int64_t winpipe_peek(std::int64_t handle) {
    DWORD available = 0;
    if (::PeekNamedPipe(handleOf(handle), nullptr, 0, nullptr, &available,
                        nullptr) == 0) {
        return -1;
    }
    return static_cast<std::int64_t>(available);
}

extern "C" std::int64_t winpipe_close(std::int64_t handle) {
    return ::CloseHandle(handleOf(handle)) != 0 ? 1 : 0;
}

extern "C" LYNXER_MODULE_EXPORT int lynxer_module_init_v1(
    RegisterFunction function, RegisterConstant, RegisterType) {
    return (function("create", "winpipe_create", "cdecl:int64(cstring)") &&
            function("connect", "winpipe_connect", "cdecl:int64(int64)") &&
            function("open", "winpipe_open", "cdecl:int64(cstring)") &&
            function("read", "winpipe_read", "cdecl:cstring(int64,int64)") &&
            function("write", "winpipe_write",
                     "cdecl:int64(int64,cstring)") &&
            function("disconnect", "winpipe_disconnect",
                     "cdecl:int64(int64)") &&
            function("peek", "winpipe_peek", "cdecl:int64(int64)") &&
            function("close", "winpipe_close", "cdecl:int64(int64)"))
               ? 0
               : 1;
}
