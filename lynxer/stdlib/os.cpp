// Lynxer `os` stdlib backend: filesystem, process, environment and platform
// helpers implemented with <filesystem> plus POSIX APIs.

#include "native_json.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <lmcons.h>
#else
#include <pwd.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

using RegisterFunction = int (*)(const char*, const char*, const char*);
using RegisterConstant = int (*)(const char*, std::int64_t);
using RegisterType = int (*)(const char*, const char*);

namespace fs = std::filesystem;
using native_json::Value;

static fs::path pathFromUtf8(const std::string& value) {
#if defined(_WIN32)
    return fs::u8path(value);
#else
    return fs::path(value);
#endif
}
static std::string pathToUtf8(const fs::path& value) {
#if defined(_WIN32)
    return value.u8string();
#else
    return value.string();
#endif
}

static const char* stable(std::string value) {
    thread_local std::string result;
    result = std::move(value);
    return result.c_str();
}

static std::string textOrEmpty(const char* text) {
    return text == nullptr ? std::string() : std::string(text);
}

static std::string environmentValue(const char* key,
                                    const std::string& fallback) {
    const char* value = key == nullptr ? nullptr : std::getenv(key);
    return value == nullptr ? fallback : std::string(value);
}

/* ---------- Directories ---------- */

extern "C" const char* os_getcwd() {
    std::error_code error;
    const fs::path current = fs::current_path(error);
    return stable(error ? std::string() : pathToUtf8(current));
}

extern "C" std::int64_t os_chdir(const char* path) {
    std::error_code error;
    fs::current_path(pathFromUtf8(textOrEmpty(path)), error);
    return error ? 0 : 1;
}

extern "C" const char* os_listdir(const char* path) {
    std::error_code error;
    fs::directory_iterator iterator(pathFromUtf8(textOrEmpty(path)), error);
    if (error) {
        return stable("");
    }
    std::string result;
    for (const auto& entry : iterator) {
        if (!result.empty()) {
            result += "\n";
        }
        result += pathToUtf8(entry.path().filename());
    }
    return stable(std::move(result));
}

extern "C" std::int64_t os_mkdir(const char* path) {
    std::error_code error;
    return fs::create_directory(pathFromUtf8(textOrEmpty(path)), error) ? 1 : 0;
}

extern "C" std::int64_t os_makedirs(const char* path) {
    std::error_code error;
    fs::create_directories(pathFromUtf8(textOrEmpty(path)), error);
    if (error) {
        return 0;
    }
    return 1;
}

extern "C" std::int64_t os_rmdir(const char* path) {
    std::error_code error;
    return fs::remove(pathFromUtf8(textOrEmpty(path)), error) ? 1 : 0;
}

extern "C" std::int64_t os_remove(const char* path) {
    std::error_code error;
    return fs::remove(pathFromUtf8(textOrEmpty(path)), error) ? 1 : 0;
}

extern "C" std::int64_t os_rename(const char* source, const char* destination) {
    std::error_code error;
    fs::rename(pathFromUtf8(textOrEmpty(source)), pathFromUtf8(textOrEmpty(destination)),
               error);
    return error ? 0 : 1;
}

extern "C" std::int64_t os_exists(const char* path) {
    std::error_code error;
    return fs::exists(pathFromUtf8(textOrEmpty(path)), error) ? 1 : 0;
}

extern "C" std::int64_t os_isFile(const char* path) {
    std::error_code error;
    return fs::is_regular_file(pathFromUtf8(textOrEmpty(path)), error) ? 1 : 0;
}

extern "C" std::int64_t os_isDir(const char* path) {
    std::error_code error;
    return fs::is_directory(pathFromUtf8(textOrEmpty(path)), error) ? 1 : 0;
}

extern "C" std::int64_t os_rmTree(const char* path) {
    std::error_code error;
    const std::uintmax_t removed =
        fs::remove_all(pathFromUtf8(textOrEmpty(path)), error);
    return error ? 0 : (removed > 0 ? 1 : 0);
}

extern "C" std::int64_t os_copyTree(const char* source,
                                    const char* destination) {
    std::error_code error;
    const fs::path target = pathFromUtf8(textOrEmpty(destination));
    if (fs::exists(target, error)) {
        return 0;
    }
    fs::copy(pathFromUtf8(textOrEmpty(source)), target,
             fs::copy_options::recursive, error);
    return error ? 0 : 1;
}

extern "C" const char* os_listdirExt(const char* path, const char* extension) {
    std::error_code error;
    fs::directory_iterator iterator(pathFromUtf8(textOrEmpty(path)), error);
    if (error) {
        return stable("");
    }
    const std::string suffix = textOrEmpty(extension);
    std::string result;
    for (const auto& entry : iterator) {
        const std::string name = pathToUtf8(entry.path().filename());
        if (name.size() >= suffix.size() &&
            name.compare(name.size() - suffix.size(), suffix.size(), suffix) ==
                0) {
            if (!result.empty()) {
                result += "\n";
            }
            result += name;
        }
    }
    return stable(std::move(result));
}

extern "C" const char* os_walkFiles(const char* path) {
    std::error_code error;
    fs::recursive_directory_iterator iterator(pathFromUtf8(textOrEmpty(path)),
                                              error);
    if (error) {
        return stable("");
    }
    std::string result;
    for (const auto& entry : iterator) {
        std::error_code typeError;
        if (!entry.is_regular_file(typeError)) {
            continue;
        }
        if (!result.empty()) {
            result += "\n";
        }
        result += pathToUtf8(entry.path());
    }
    return stable(std::move(result));
}

/* ---------- Paths and environment ---------- */

extern "C" const char* os_joinPath(const char* first, const char* second) {
    return stable(
        pathToUtf8(pathFromUtf8(textOrEmpty(first)) / pathFromUtf8(textOrEmpty(second))));
}

extern "C" const char* os_basename(const char* path) {
    return stable(pathToUtf8(pathFromUtf8(textOrEmpty(path)).filename()));
}

extern "C" const char* os_dirname(const char* path) {
    return stable(pathToUtf8(pathFromUtf8(textOrEmpty(path)).parent_path()));
}

extern "C" const char* os_absPath(const char* path) {
    std::error_code error;
    const fs::path absolute =
        fs::absolute(pathFromUtf8(textOrEmpty(path)), error).lexically_normal();
    return stable(error ? std::string() : pathToUtf8(absolute));
}

extern "C" const char* os_extname(const char* path) {
    return stable(pathToUtf8(pathFromUtf8(textOrEmpty(path)).extension()));
}

extern "C" const char* os_normPath(const char* path) {
    const std::string input = textOrEmpty(path);
    if (input.empty()) {
        return stable(".");
    }
    return stable(pathToUtf8(pathFromUtf8(input).lexically_normal()));
}

extern "C" const char* os_expandUser(const char* path) {
    const std::string input = textOrEmpty(path);
    if (input.empty() || input[0] != '~') {
        return stable(input);
    }
    const std::size_t slash = input.find_first_of("/\\");
    const std::string user = input.substr(
        1, slash == std::string::npos ? std::string::npos : slash - 1);
    std::string home;
    if (user.empty()) {
#if defined(_WIN32)
        home = environmentValue("USERPROFILE", "");
#else
        home = environmentValue("HOME", "");
#endif
    }
#if !defined(_WIN32)
    else if (passwd* entry = ::getpwnam(user.c_str()); entry != nullptr) {
        home = entry->pw_dir;
    }
#endif
    else {
        return stable(input);
    }
    if (home.empty()) {
        return stable(input);
    }
    return stable(home + (slash == std::string::npos ? "" : input.substr(slash)));
}

extern "C" const char* os_sep() {
#if defined(_WIN32)
    return "\\";
#else
    return "/";
#endif
}

extern "C" const char* os_getenv(const char* key) {
    return stable(environmentValue(key, ""));
}

extern "C" std::int64_t os_setenv(const char* key, const char* value) {
    if (key == nullptr || value == nullptr) {
        return 0;
    }
#if defined(_WIN32)
    return ::_putenv_s(key, value) == 0 ? 1 : 0;
#else
    return ::setenv(key, value, 1) == 0 ? 1 : 0;
#endif
}

extern "C" const char* os_tempDir() {
#if defined(_WIN32)
    char buffer[MAX_PATH + 1]{};
    const DWORD length = ::GetTempPathA(MAX_PATH, buffer);
    return length > 0 && length <= MAX_PATH ? stable(std::string(buffer, length)) : stable("");
#else
    for (const char* key : {"TMPDIR", "TEMP", "TMP"}) {
        const char* value = std::getenv(key);
        if (value != nullptr && *value != '\0') {
            return stable(std::string(value));
        }
    }
    return stable("/tmp");
#endif
}

extern "C" const char* os_homedir() {
#if defined(_WIN32)
    return stable(environmentValue("USERPROFILE", ""));
#else
    return stable(environmentValue("HOME", ""));
#endif
}

extern "C" const char* os_username() {
#if defined(_WIN32)
    char name[UNLEN + 1]{};
    DWORD length = UNLEN + 1;
    return ::GetUserNameA(name, &length) ? stable(name) : stable("");
#else
    if (passwd* entry = ::getpwuid(::getuid()); entry != nullptr) {
        return stable(std::string(entry->pw_name));
    }
    return stable("");
#endif
}

extern "C" const char* os_hostname() {
    char buffer[256];
#if defined(_WIN32)
    DWORD length = static_cast<DWORD>(sizeof(buffer));
    if (::GetComputerNameA(buffer, &length)) {
#else
    if (::gethostname(buffer, sizeof(buffer)) == 0) {
#endif
        buffer[sizeof(buffer) - 1] = '\0';
        return stable(std::string(buffer));
    }
    return stable("");
}

extern "C" std::int64_t os_getpid() {
#if defined(_WIN32)
    return static_cast<std::int64_t>(::GetCurrentProcessId());
#else
    return static_cast<std::int64_t>(::getpid());
#endif
}

extern "C" std::int64_t os_cpuCount() {
#if defined(_WIN32)
    SYSTEM_INFO info{};
    ::GetSystemInfo(&info);
    const long count = static_cast<long>(info.dwNumberOfProcessors);
#else
    const long count = ::sysconf(_SC_NPROCESSORS_ONLN);
#endif
    return count > 0 ? static_cast<std::int64_t>(count) : 1;
}

extern "C" std::int64_t os_diskTotal(const char* path) {
#if defined(_WIN32)
    ULARGE_INTEGER available{}, total{};
    if (!::GetDiskFreeSpaceExA(textOrEmpty(path).c_str(), &available, &total, nullptr)) return -1;
    return static_cast<std::int64_t>(total.QuadPart);
#else
    struct statvfs info {};
    if (::statvfs(textOrEmpty(path).c_str(), &info) != 0) {
        return -1;
    }
    return static_cast<std::int64_t>(info.f_blocks) *
           static_cast<std::int64_t>(info.f_frsize);
#endif
}

extern "C" std::int64_t os_diskFree(const char* path) {
#if defined(_WIN32)
    ULARGE_INTEGER available{};
    if (!::GetDiskFreeSpaceExA(textOrEmpty(path).c_str(), &available, nullptr, nullptr)) return -1;
    return static_cast<std::int64_t>(available.QuadPart);
#else
    struct statvfs info {};
    if (::statvfs(textOrEmpty(path).c_str(), &info) != 0) {
        return -1;
    }
    return static_cast<std::int64_t>(info.f_bavail) *
           static_cast<std::int64_t>(info.f_frsize);
#endif
}

/* ---------- Platform information ---------- */

#if defined(_WIN32)
struct utsname { char sysname[64], nodename[256], release[64], version[128], machine[64]; };
static bool readUname(struct utsname& info) {
    std::snprintf(info.sysname, sizeof(info.sysname), "Windows");
    DWORD length = static_cast<DWORD>(sizeof(info.nodename));
    if (!::GetComputerNameA(info.nodename, &length)) info.nodename[0] = '\0';
    OSVERSIONINFOA version{}; version.dwOSVersionInfoSize = sizeof(version);
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#endif
    if (::GetVersionExA(&version)) {
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        std::snprintf(info.release, sizeof(info.release), "%lu.%lu", version.dwMajorVersion, version.dwMinorVersion);
        std::snprintf(info.version, sizeof(info.version), "Build %lu", version.dwBuildNumber);
    } else { info.release[0] = '\0'; info.version[0] = '\0'; }
    SYSTEM_INFO system{}; ::GetNativeSystemInfo(&system);
    const char* machine = system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? "x86_64" :
        system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? "aarch64" :
        system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL ? "i686" : "unknown";
    std::snprintf(info.machine, sizeof(info.machine), "%s", machine);
    return true;
}
#else
static bool readUname(struct utsname& info) { return ::uname(&info) == 0; }
#endif

extern "C" const char* os_getSystemName() {
    struct utsname info {};
    return readUname(info) ? stable(std::string(info.sysname)) : stable("");
}

extern "C" const char* os_getSystemRelease() {
    struct utsname info {};
    return readUname(info) ? stable(std::string(info.release)) : stable("");
}

extern "C" const char* os_getSystemVersion() {
    struct utsname info {};
    return readUname(info) ? stable(std::string(info.version)) : stable("");
}

extern "C" const char* os_getSystemMachine() {
    struct utsname info {};
    return readUname(info) ? stable(std::string(info.machine)) : stable("");
}

extern "C" const char* os_getSystemProcessor() {
    struct utsname info {};
    return readUname(info) ? stable(std::string(info.machine)) : stable("");
}

extern "C" const char* os_getSystemArchitecture() {
    return stable(sizeof(void*) == 8 ? "64bit" : "32bit");
}

extern "C" const char* os_getSystemNode() {
    struct utsname info {};
    return readUname(info) ? stable(std::string(info.nodename)) : stable("");
}

extern "C" const char* os_getSystemUname() {
    struct utsname info {};
    if (!readUname(info)) {
        return stable("{}");
    }
    Value object = native_json::makeObject();
    native_json::setField(object, "system",
                          native_json::makeString(info.sysname));
    native_json::setField(object, "node",
                          native_json::makeString(info.nodename));
    native_json::setField(object, "release",
                          native_json::makeString(info.release));
    native_json::setField(object, "version",
                          native_json::makeString(info.version));
    native_json::setField(object, "machine",
                          native_json::makeString(info.machine));
    native_json::setField(object, "processor",
                          native_json::makeString(info.machine));
    return stable(native_json::dump(object, false));
}

extern "C" const char* os_getSystemInfo() {
    struct utsname info {};
    if (!readUname(info)) {
        return stable("{}");
    }
    Value object = native_json::makeObject();
    native_json::setField(object, "system",
                          native_json::makeString(info.sysname));
    native_json::setField(object, "node",
                          native_json::makeString(info.nodename));
    native_json::setField(object, "release",
                          native_json::makeString(info.release));
    native_json::setField(object, "version",
                          native_json::makeString(info.version));
    native_json::setField(object, "machine",
                          native_json::makeString(info.machine));
    native_json::setField(object, "processor",
                          native_json::makeString(info.machine));
    native_json::setField(object, "architecture",
                          native_json::makeString(sizeof(void*) == 8 ? "64bit"
                                                                    : "32bit"));
    return stable(native_json::dump(object, false));
}

extern "C" const char* os_getSystemDistro() {
#if defined(_WIN32)
    Value object = native_json::makeObject();
    native_json::setField(object, "NAME", native_json::makeString("Windows"));
    native_json::setField(object, "ID", native_json::makeString("windows"));
    return stable(native_json::dump(object, false));
#else
    std::ifstream input("/etc/os-release");
    if (!input) {
        return stable("{}");
    }
    Value object = native_json::makeObject();
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, equals);
        std::string value = line.substr(equals + 1);
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        native_json::setField(object, key, native_json::makeString(value));
    }
    return stable(native_json::dump(object, false));
#endif
}

extern "C" int lynxer_module_init_v1(RegisterFunction function,
                                     RegisterConstant, RegisterType) {
    return function("getcwd", "os_getcwd", "cdecl:cstring()") &&
                   function("chdir", "os_chdir", "cdecl:int64(cstring)") &&
                   function("listdir", "os_listdir", "cdecl:cstring(cstring)") &&
                   function("mkdir", "os_mkdir", "cdecl:int64(cstring)") &&
                   function("makedirs", "os_makedirs", "cdecl:int64(cstring)") &&
                   function("rmdir", "os_rmdir", "cdecl:int64(cstring)") &&
                   function("remove", "os_remove", "cdecl:int64(cstring)") &&
                   function("rename", "os_rename",
                            "cdecl:int64(cstring,cstring)") &&
                   function("exists", "os_exists", "cdecl:int64(cstring)") &&
                   function("isFile", "os_isFile", "cdecl:int64(cstring)") &&
                   function("isDir", "os_isDir", "cdecl:int64(cstring)") &&
                   function("rmTree", "os_rmTree", "cdecl:int64(cstring)") &&
                   function("copyTree", "os_copyTree",
                            "cdecl:int64(cstring,cstring)") &&
                   function("listdirExt", "os_listdirExt",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("walkFiles", "os_walkFiles",
                            "cdecl:cstring(cstring)") &&
                   function("joinPath", "os_joinPath",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("basename", "os_basename",
                            "cdecl:cstring(cstring)") &&
                   function("dirname", "os_dirname",
                            "cdecl:cstring(cstring)") &&
                   function("absPath", "os_absPath",
                            "cdecl:cstring(cstring)") &&
                   function("extname", "os_extname",
                            "cdecl:cstring(cstring)") &&
                   function("normPath", "os_normPath",
                            "cdecl:cstring(cstring)") &&
                   function("expandUser", "os_expandUser",
                            "cdecl:cstring(cstring)") &&
                   function("sep", "os_sep", "cdecl:cstring()") &&
                   function("getenv", "os_getenv", "cdecl:cstring(cstring)") &&
                   function("setenv", "os_setenv",
                            "cdecl:int64(cstring,cstring)") &&
                   function("tempDir", "os_tempDir", "cdecl:cstring()") &&
                   function("homedir", "os_homedir", "cdecl:cstring()") &&
                   function("username", "os_username", "cdecl:cstring()") &&
                   function("hostname", "os_hostname", "cdecl:cstring()") &&
                   function("getpid", "os_getpid", "cdecl:int64()") &&
                   function("cpuCount", "os_cpuCount", "cdecl:int64()") &&
                   function("diskTotal", "os_diskTotal",
                            "cdecl:int64(cstring)") &&
                   function("diskFree", "os_diskFree",
                            "cdecl:int64(cstring)") &&
                   function("getSystemName", "os_getSystemName",
                            "cdecl:cstring()") &&
                   function("getSystemRelease", "os_getSystemRelease",
                            "cdecl:cstring()") &&
                   function("getSystemVersion", "os_getSystemVersion",
                            "cdecl:cstring()") &&
                   function("getSystemMachine", "os_getSystemMachine",
                            "cdecl:cstring()") &&
                   function("getSystemProcessor", "os_getSystemProcessor",
                            "cdecl:cstring()") &&
                   function("getSystemArchitecture", "os_getSystemArchitecture",
                            "cdecl:cstring()") &&
                   function("getSystemNode", "os_getSystemNode",
                            "cdecl:cstring()") &&
                   function("getSystemUname", "os_getSystemUname",
                            "cdecl:cstring()") &&
                   function("getSystemInfo", "os_getSystemInfo",
                            "cdecl:cstring()") &&
                   function("getSystemDistro", "os_getSystemDistro",
                            "cdecl:cstring()")
               ? 0
               : 1;
}
