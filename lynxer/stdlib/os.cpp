// Clynxer `os` stdlib backend: filesystem, process, environment and platform
// helpers implemented with <filesystem> plus POSIX APIs.

#include "native_json.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include <pwd.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>

using RegisterFunction = int (*)(const char*, const char*, const char*);
using RegisterConstant = int (*)(const char*, std::int64_t);
using RegisterType = int (*)(const char*, const char*);

namespace fs = std::filesystem;
using native_json::Value;

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
    return stable(error ? std::string() : current.string());
}

extern "C" std::int64_t os_chdir(const char* path) {
    std::error_code error;
    fs::current_path(fs::path(textOrEmpty(path)), error);
    return error ? 0 : 1;
}

extern "C" const char* os_listdir(const char* path) {
    std::error_code error;
    fs::directory_iterator iterator(fs::path(textOrEmpty(path)), error);
    if (error) {
        return stable("");
    }
    std::string result;
    for (const auto& entry : iterator) {
        if (!result.empty()) {
            result += "\n";
        }
        result += entry.path().filename().string();
    }
    return stable(std::move(result));
}

extern "C" std::int64_t os_mkdir(const char* path) {
    std::error_code error;
    return fs::create_directory(fs::path(textOrEmpty(path)), error) ? 1 : 0;
}

extern "C" std::int64_t os_makedirs(const char* path) {
    std::error_code error;
    fs::create_directories(fs::path(textOrEmpty(path)), error);
    if (error) {
        return 0;
    }
    return 1;
}

extern "C" std::int64_t os_rmdir(const char* path) {
    std::error_code error;
    return fs::remove(fs::path(textOrEmpty(path)), error) ? 1 : 0;
}

extern "C" std::int64_t os_remove(const char* path) {
    std::error_code error;
    return fs::remove(fs::path(textOrEmpty(path)), error) ? 1 : 0;
}

extern "C" std::int64_t os_rename(const char* source, const char* destination) {
    std::error_code error;
    fs::rename(fs::path(textOrEmpty(source)), fs::path(textOrEmpty(destination)),
               error);
    return error ? 0 : 1;
}

extern "C" std::int64_t os_exists(const char* path) {
    std::error_code error;
    return fs::exists(fs::path(textOrEmpty(path)), error) ? 1 : 0;
}

extern "C" std::int64_t os_isFile(const char* path) {
    std::error_code error;
    return fs::is_regular_file(fs::path(textOrEmpty(path)), error) ? 1 : 0;
}

extern "C" std::int64_t os_isDir(const char* path) {
    std::error_code error;
    return fs::is_directory(fs::path(textOrEmpty(path)), error) ? 1 : 0;
}

extern "C" std::int64_t os_rmTree(const char* path) {
    std::error_code error;
    const std::uintmax_t removed =
        fs::remove_all(fs::path(textOrEmpty(path)), error);
    return error ? 0 : (removed > 0 ? 1 : 0);
}

extern "C" std::int64_t os_copyTree(const char* source,
                                    const char* destination) {
    std::error_code error;
    const fs::path target(textOrEmpty(destination));
    if (fs::exists(target, error)) {
        return 0;
    }
    fs::copy(fs::path(textOrEmpty(source)), target,
             fs::copy_options::recursive, error);
    return error ? 0 : 1;
}

extern "C" const char* os_listdirExt(const char* path, const char* extension) {
    std::error_code error;
    fs::directory_iterator iterator(fs::path(textOrEmpty(path)), error);
    if (error) {
        return stable("");
    }
    const std::string suffix = textOrEmpty(extension);
    std::string result;
    for (const auto& entry : iterator) {
        const std::string name = entry.path().filename().string();
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
    fs::recursive_directory_iterator iterator(fs::path(textOrEmpty(path)),
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
        result += entry.path().string();
    }
    return stable(std::move(result));
}

/* ---------- Paths and environment ---------- */

extern "C" const char* os_joinPath(const char* first, const char* second) {
    return stable(
        (fs::path(textOrEmpty(first)) / fs::path(textOrEmpty(second)))
            .string());
}

extern "C" const char* os_basename(const char* path) {
    return stable(fs::path(textOrEmpty(path)).filename().string());
}

extern "C" const char* os_dirname(const char* path) {
    return stable(fs::path(textOrEmpty(path)).parent_path().string());
}

extern "C" const char* os_absPath(const char* path) {
    std::error_code error;
    const fs::path absolute =
        fs::absolute(fs::path(textOrEmpty(path)), error).lexically_normal();
    return stable(error ? std::string() : absolute.string());
}

extern "C" const char* os_extname(const char* path) {
    return stable(fs::path(textOrEmpty(path)).extension().string());
}

extern "C" const char* os_normPath(const char* path) {
    const std::string input = textOrEmpty(path);
    if (input.empty()) {
        return stable(".");
    }
    return stable(fs::path(input).lexically_normal().string());
}

extern "C" const char* os_expandUser(const char* path) {
    const std::string input = textOrEmpty(path);
    if (input.empty() || input[0] != '~') {
        return stable(input);
    }
    const std::size_t slash = input.find('/');
    const std::string user = input.substr(
        1, slash == std::string::npos ? std::string::npos : slash - 1);
    std::string home;
    if (user.empty()) {
        home = environmentValue("HOME", "");
    } else if (passwd* entry = ::getpwnam(user.c_str()); entry != nullptr) {
        home = entry->pw_dir;
    } else {
        return stable(input);
    }
    if (home.empty()) {
        return stable(input);
    }
    return stable(home + (slash == std::string::npos ? "" : input.substr(slash)));
}

extern "C" const char* os_sep() { return "/"; }

extern "C" const char* os_getenv(const char* key) {
    return stable(environmentValue(key, ""));
}

extern "C" std::int64_t os_setenv(const char* key, const char* value) {
    if (key == nullptr || value == nullptr) {
        return 0;
    }
    return ::setenv(key, value, 1) == 0 ? 1 : 0;
}

extern "C" const char* os_tempDir() {
    for (const char* key : {"TMPDIR", "TEMP", "TMP"}) {
        const char* value = std::getenv(key);
        if (value != nullptr && *value != '\0') {
            return stable(std::string(value));
        }
    }
    return stable("/tmp");
}

extern "C" const char* os_homedir() {
    return stable(environmentValue("HOME", ""));
}

extern "C" const char* os_username() {
    if (passwd* entry = ::getpwuid(::getuid()); entry != nullptr) {
        return stable(std::string(entry->pw_name));
    }
    return stable("");
}

extern "C" const char* os_hostname() {
    char buffer[256];
    if (::gethostname(buffer, sizeof(buffer)) == 0) {
        buffer[sizeof(buffer) - 1] = '\0';
        return stable(std::string(buffer));
    }
    return stable("");
}

extern "C" std::int64_t os_getpid() {
    return static_cast<std::int64_t>(::getpid());
}

extern "C" std::int64_t os_cpuCount() {
    const long count = ::sysconf(_SC_NPROCESSORS_ONLN);
    return count > 0 ? static_cast<std::int64_t>(count) : 1;
}

extern "C" std::int64_t os_diskTotal(const char* path) {
    struct statvfs info {};
    if (::statvfs(textOrEmpty(path).c_str(), &info) != 0) {
        return -1;
    }
    return static_cast<std::int64_t>(info.f_blocks) *
           static_cast<std::int64_t>(info.f_frsize);
}

extern "C" std::int64_t os_diskFree(const char* path) {
    struct statvfs info {};
    if (::statvfs(textOrEmpty(path).c_str(), &info) != 0) {
        return -1;
    }
    return static_cast<std::int64_t>(info.f_bavail) *
           static_cast<std::int64_t>(info.f_frsize);
}

/* ---------- Platform information ---------- */

static bool readUname(struct utsname& info) { return ::uname(&info) == 0; }

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

// Lynxer has no Python runtime; these report the host implementation instead.
extern "C" const char* os_getPythonVersion() { return ""; }
extern "C" const char* os_getPythonImplementation() { return "Lynxer"; }

static std::string executablePath() {
    char buffer[4096];
    const ssize_t length =
        ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) {
        return "";
    }
    buffer[length] = '\0';
    return std::string(buffer);
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
    native_json::setField(object, "python",
                          native_json::makeString(""));
    native_json::setField(object, "pythonImplementation",
                          native_json::makeString("Lynxer"));
    native_json::setField(object, "pythonExecutable",
                          native_json::makeString(executablePath()));
    return stable(native_json::dump(object, false));
}

extern "C" const char* os_getSystemDistro() {
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
}

extern "C" int clynxer_module_init_v1(RegisterFunction function,
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
                   function("getPythonVersion", "os_getPythonVersion",
                            "cdecl:cstring()") &&
                   function("getPythonImplementation",
                            "os_getPythonImplementation", "cdecl:cstring()") &&
                   function("getSystemUname", "os_getSystemUname",
                            "cdecl:cstring()") &&
                   function("getSystemInfo", "os_getSystemInfo",
                            "cdecl:cstring()") &&
                   function("getSystemDistro", "os_getSystemDistro",
                            "cdecl:cstring()")
               ? 0
               : 1;
}
