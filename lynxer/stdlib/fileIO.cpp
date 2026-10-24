// Lynxer `fileIO` stdlib backend: file reading, writing and metadata.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include <unistd.h>

using RegisterFunction = int (*)(const char*, const char*, const char*);
using RegisterConstant = int (*)(const char*, std::int64_t);
using RegisterType = int (*)(const char*, const char*);

namespace fs = std::filesystem;

static const char* stable(std::string value) {
    thread_local std::string result;
    result = std::move(value);
    return result.c_str();
}

static std::string textOrEmpty(const char* text) {
    return text == nullptr ? std::string() : std::string(text);
}

// Prefix absolute paths beyond MAX_PATH with the extended-length marker so the
// C++ streams and <filesystem> reach them on Windows. Values returned to
// Lynxer are untouched.
static fs::path ioPath(const std::string& value) {
#if defined(_WIN32)
    fs::path path = fs::u8path(value);
    const std::wstring native = path.native();
    if (native.size() < 248 || !path.is_absolute() ||
        native.rfind(L"\\\\?\\", 0) == 0) {
        return path;
    }
    if (native.rfind(L"\\\\", 0) == 0) {
        return fs::path(L"\\\\?\\UNC\\" + native.substr(2));
    }
    return fs::path(L"\\\\?\\" + native);
#else
    return fs::path(value);
#endif
}

static bool readWholeFile(const std::string& path, std::string& output) {
    std::ifstream input(ioPath(path), std::ios::binary);
    if (!input) {
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    output = buffer.str();
    return true;
}

static std::vector<std::string> splitContentLines(const std::string& content) {
    std::vector<std::string> lines;
    if (content.empty()) {
        return lines;
    }
    std::string current;
    for (std::size_t index = 0; index < content.size(); ++index) {
        const char character = content[index];
#if defined(_WIN32)
        // Accept LF, CRLF and lone CR on Windows. POSIX keeps its existing
        // LF-only split so a stray CR is not silently discarded there.
        if (character == '\r') {
            if (index + 1 < content.size() && content[index + 1] == '\n') {
                ++index;
            }
            lines.push_back(current);
            current.clear();
            continue;
        }
#endif
        if (character == '\n') {
            lines.push_back(current);
            current.clear();
        } else {
            current += character;
        }
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    return lines;
}

static std::string temporaryDirectory() {
#if defined(_WIN32)
    // No `/tmp`: use the host's temp directory (`%TEMP%`).
    std::error_code error;
    const fs::path directory = fs::temp_directory_path(error);
    return error ? std::string(".") : directory.string();
#else
    const char* value = std::getenv("TMPDIR");
    return value == nullptr || *value == '\0' ? std::string("/tmp")
                                              : std::string(value);
#endif
}

extern "C" const char* fileIO_readFile(const char* path) {
    std::string content;
    if (!readWholeFile(textOrEmpty(path), content)) {
        return stable("");
    }
    return stable(std::move(content));
}

extern "C" std::int64_t fileIO_writeFile(const char* path,
                                         const char* content) {
    std::ofstream output(ioPath(textOrEmpty(path)), std::ios::binary | std::ios::trunc);
    if (!output) {
        return 0;
    }
    output << textOrEmpty(content);
    return output ? 1 : 0;
}

extern "C" std::int64_t fileIO_appendFile(const char* path,
                                          const char* content) {
    std::ofstream output(ioPath(textOrEmpty(path)), std::ios::binary | std::ios::app);
    if (!output) {
        return 0;
    }
    output << textOrEmpty(content);
    return output ? 1 : 0;
}

extern "C" std::int64_t fileIO_fileExists(const char* path) {
    std::error_code error;
    return fs::is_regular_file(ioPath(textOrEmpty(path)), error) ? 1 : 0;
}

extern "C" std::int64_t fileIO_deleteFile(const char* path) {
    std::error_code error;
    return fs::remove(ioPath(textOrEmpty(path)), error) ? 1 : 0;
}

extern "C" std::int64_t fileIO_copyFile(const char* source,
                                        const char* destination) {
    std::error_code error;
    fs::copy_file(ioPath(textOrEmpty(source)),
                  ioPath(textOrEmpty(destination)),
                  fs::copy_options::overwrite_existing, error);
    return error ? 0 : 1;
}

extern "C" std::int64_t fileIO_moveFile(const char* source,
                                        const char* destination) {
    std::error_code error;
    fs::rename(ioPath(textOrEmpty(source)), ioPath(textOrEmpty(destination)),
               error);
    return error ? 0 : 1;
}

extern "C" std::int64_t fileIO_fileSize(const char* path) {
    std::error_code error;
    const std::uintmax_t size =
        fs::file_size(ioPath(textOrEmpty(path)), error);
    return error ? -1 : static_cast<std::int64_t>(size);
}

extern "C" std::int64_t fileIO_countLines(const char* path) {
    std::string content;
    if (!readWholeFile(textOrEmpty(path), content)) {
        return -1;
    }
    return static_cast<std::int64_t>(splitContentLines(content).size());
}

extern "C" const char* fileIO_readLine(const char* path, std::int64_t index) {
    std::string content;
    if (!readWholeFile(textOrEmpty(path), content) || index < 0) {
        return stable("");
    }
    const std::vector<std::string> lines = splitContentLines(content);
    const std::size_t position = static_cast<std::size_t>(index);
    if (position >= lines.size()) {
        return stable("");
    }
    std::string line = lines[position];
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return stable(std::move(line));
}

extern "C" const char* fileIO_fileModTime(const char* path) {
    std::error_code error;
    const fs::path target(textOrEmpty(path));
    const fs::file_time_type time = fs::last_write_time(target, error);
    if (error) {
        return stable("");
    }
    // `file_clock` and `system_clock` need not share a duration: libc++ (clang
    // on Windows ARM64) ticks the file clock in 128-bit nanoseconds, so the sum
    // below would not be a plain `system_clock::time_point` and `to_time_t`
    // would reject it. Pin the delta to the system clock's own duration first.
    const auto delta = time - fs::file_time_type::clock::now();
    const auto systemTime =
        std::chrono::system_clock::now() +
        std::chrono::duration_cast<std::chrono::system_clock::duration>(delta);
    const std::time_t raw = std::chrono::system_clock::to_time_t(systemTime);
    std::tm local {};
#if defined(_WIN32)
    if (::localtime_s(&local, &raw) != 0) {
        return stable("");
    }
#else
    if (::localtime_r(&raw, &local) == nullptr) {
        return stable("");
    }
#endif
    char buffer[32];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local) ==
        0) {
        return stable("");
    }
    return stable(std::string(buffer));
}

extern "C" const char* fileIO_fileExtension(const char* path) {
    return stable(fs::path(textOrEmpty(path)).extension().string());
}

extern "C" std::int64_t fileIO_fileIsEmpty(const char* path) {
    std::error_code error;
    const fs::path target(textOrEmpty(path));
    if (!fs::is_regular_file(target, error)) {
        return 0;
    }
    const std::uintmax_t size = fs::file_size(target, error);
    return (!error && size == 0) ? 1 : 0;
}

extern "C" const char* fileIO_tempFile(const char* suffix) {
    const std::string suffixText = textOrEmpty(suffix);
#if defined(_WIN32)
    // `mkstemps` is POSIX-only; MinGW provides plain `mkstemp`. Create a unique
    // file, then move it to the suffixed name, retrying on the (vanishingly
    // unlikely) collision so the result is still unique.
    for (int attempt = 0; attempt < 128; ++attempt) {
        std::string pattern = temporaryDirectory() + "/lynxerXXXXXX";
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back('\0');
        const int descriptor = ::mkstemp(buffer.data());
        if (descriptor < 0) {
            return stable("");
        }
        ::close(descriptor);
        const std::string created(buffer.data());
        const std::string target = created + suffixText;
        std::error_code error;
        if (!fs::exists(target, error)) {
            fs::rename(created, target, error);
            if (!error) {
                return stable(target);
            }
        }
        fs::remove(created, error);
    }
    return stable("");
#else
    std::string pattern = temporaryDirectory() + "/lynxerXXXXXX" + suffixText;
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    const int descriptor =
        ::mkstemps(buffer.data(), static_cast<int>(suffixText.size()));
    if (descriptor < 0) {
        return stable("");
    }
    ::close(descriptor);
    return stable(std::string(buffer.data()));
#endif
}

extern "C" const char* fileIO_tempDir() {
    std::string pattern = temporaryDirectory() + "/lynxerXXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    if (::mkdtemp(buffer.data()) == nullptr) {
        return stable("");
    }
    return stable(std::string(buffer.data()));
}

extern "C" const char* fileIO_stemName(const char* path) {
    return stable(fs::path(textOrEmpty(path)).stem().string());
}

extern "C" const char* fileIO_readFileLines(const char* path,
                                            const char* separator) {
    std::string content;
    if (!readWholeFile(textOrEmpty(path), content)) {
        return stable("");
    }
    const std::vector<std::string> lines = splitContentLines(content);
    const std::string join = textOrEmpty(separator);
    std::string result;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (index > 0) {
            result += join;
        }
        result += lines[index];
    }
    return stable(std::move(result));
}

extern "C" int lynxer_module_init_v1(RegisterFunction function,
                                     RegisterConstant, RegisterType) {
    return function("readFile", "fileIO_readFile", "cdecl:cstring(cstring)") &&
                   function("writeFile", "fileIO_writeFile",
                            "cdecl:int64(cstring,cstring)") &&
                   function("appendFile", "fileIO_appendFile",
                            "cdecl:int64(cstring,cstring)") &&
                   function("fileExists", "fileIO_fileExists",
                            "cdecl:int64(cstring)") &&
                   function("deleteFile", "fileIO_deleteFile",
                            "cdecl:int64(cstring)") &&
                   function("copyFile", "fileIO_copyFile",
                            "cdecl:int64(cstring,cstring)") &&
                   function("moveFile", "fileIO_moveFile",
                            "cdecl:int64(cstring,cstring)") &&
                   function("fileSize", "fileIO_fileSize",
                            "cdecl:int64(cstring)") &&
                   function("countLines", "fileIO_countLines",
                            "cdecl:int64(cstring)") &&
                   function("readLine", "fileIO_readLine",
                            "cdecl:cstring(cstring,int64)") &&
                   function("fileModTime", "fileIO_fileModTime",
                            "cdecl:cstring(cstring)") &&
                   function("fileExtension", "fileIO_fileExtension",
                            "cdecl:cstring(cstring)") &&
                   function("fileIsEmpty", "fileIO_fileIsEmpty",
                            "cdecl:int64(cstring)") &&
                   function("tempFile", "fileIO_tempFile",
                            "cdecl:cstring(cstring)") &&
                   function("tempDir", "fileIO_tempDir", "cdecl:cstring()") &&
                   function("stemName", "fileIO_stemName",
                            "cdecl:cstring(cstring)") &&
                   function("readFileLines", "fileIO_readFileLines",
                            "cdecl:cstring(cstring,cstring)")
               ? 0
               : 1;
}
