// Clynxer `fileIO` stdlib backend: file reading, writing and metadata.

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

static bool readWholeFile(const std::string& path, std::string& output) {
    std::ifstream input(path, std::ios::binary);
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
    for (const char character : content) {
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
    const char* value = std::getenv("TMPDIR");
    return value == nullptr || *value == '\0' ? std::string("/tmp")
                                              : std::string(value);
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
    std::ofstream output(textOrEmpty(path), std::ios::binary | std::ios::trunc);
    if (!output) {
        return 0;
    }
    output << textOrEmpty(content);
    return output ? 1 : 0;
}

extern "C" std::int64_t fileIO_appendFile(const char* path,
                                          const char* content) {
    std::ofstream output(textOrEmpty(path), std::ios::binary | std::ios::app);
    if (!output) {
        return 0;
    }
    output << textOrEmpty(content);
    return output ? 1 : 0;
}

extern "C" std::int64_t fileIO_fileExists(const char* path) {
    std::error_code error;
    return fs::is_regular_file(fs::path(textOrEmpty(path)), error) ? 1 : 0;
}

extern "C" std::int64_t fileIO_deleteFile(const char* path) {
    std::error_code error;
    return fs::remove(fs::path(textOrEmpty(path)), error) ? 1 : 0;
}

extern "C" std::int64_t fileIO_copyFile(const char* source,
                                        const char* destination) {
    std::error_code error;
    fs::copy_file(fs::path(textOrEmpty(source)),
                  fs::path(textOrEmpty(destination)),
                  fs::copy_options::overwrite_existing, error);
    return error ? 0 : 1;
}

extern "C" std::int64_t fileIO_moveFile(const char* source,
                                        const char* destination) {
    std::error_code error;
    fs::rename(fs::path(textOrEmpty(source)), fs::path(textOrEmpty(destination)),
               error);
    return error ? 0 : 1;
}

extern "C" std::int64_t fileIO_fileSize(const char* path) {
    std::error_code error;
    const std::uintmax_t size =
        fs::file_size(fs::path(textOrEmpty(path)), error);
    return error ? -1 : static_cast<std::int64_t>(size);
}

extern "C" std::int64_t fileIO_countLines(const char* path) {
    std::string content;
    if (!readWholeFile(textOrEmpty(path), content)) {
        return -1;
    }
    if (content.empty()) {
        return 0;
    }
    std::int64_t lines = 0;
    for (const char character : content) {
        if (character == '\n') {
            ++lines;
        }
    }
    if (content.back() != '\n') {
        ++lines;
    }
    return lines;
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
    const auto systemTime = std::chrono::system_clock::now() +
                            (time - fs::file_time_type::clock::now());
    const std::time_t raw = std::chrono::system_clock::to_time_t(systemTime);
    std::tm local {};
    if (::localtime_r(&raw, &local) == nullptr) {
        return stable("");
    }
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

extern "C" int clynxer_module_init_v1(RegisterFunction function,
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
