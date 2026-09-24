// Clynxer `path` stdlib backend: pathlib-style path manipulation built on
// <filesystem> plus POSIX stat calls.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include <pwd.h>
#include <sys/stat.h>
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

static std::string joinLines(std::vector<std::string> values, bool sorted) {
    if (sorted) {
        std::sort(values.begin(), values.end());
    }
    std::string result;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            result += "\n";
        }
        result += values[index];
    }
    return result;
}

static std::vector<std::string> splitComponents(const std::string& value) {
    std::vector<std::string> parts;
    std::string current;
    for (const char character : value) {
        if (character == '/') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else {
            current += character;
        }
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    return parts;
}

// Single-component glob match: '*' and '?' do not cross '/'.
static bool componentMatch(const std::string& pattern,
                           const std::string& text) {
    std::size_t p = 0;
    std::size_t t = 0;
    std::size_t star = std::string::npos;
    std::size_t mark = 0;
    while (t < text.size()) {
        if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            mark = t;
            continue;
        }
        bool matched = false;
        if (p < pattern.size()) {
            if (pattern[p] == '?') {
                matched = true;
            } else if (pattern[p] == '[') {
                const std::size_t close = pattern.find(']', p + 1);
                if (close != std::string::npos) {
                    bool negate = false;
                    std::size_t cursor = p + 1;
                    if (cursor < close &&
                        (pattern[cursor] == '!' || pattern[cursor] == '^')) {
                        negate = true;
                        ++cursor;
                    }
                    bool inside = false;
                    for (; cursor < close; ++cursor) {
                        if (cursor + 2 < close && pattern[cursor + 1] == '-') {
                            if (text[t] >= pattern[cursor] &&
                                text[t] <= pattern[cursor + 2]) {
                                inside = true;
                            }
                            cursor += 2;
                        } else if (pattern[cursor] == text[t]) {
                            inside = true;
                        }
                    }
                    matched = inside != negate;
                }
            } else if (pattern[p] == text[t]) {
                matched = true;
            }
        }
        if (matched) {
            ++p;
            ++t;
            continue;
        }
        if (star != std::string::npos) {
            p = star + 1;
            t = ++mark;
            continue;
        }
        return false;
    }
    while (p < pattern.size() && pattern[p] == '*') {
        ++p;
    }
    return p == pattern.size();
}

static bool matchComponents(const std::vector<std::string>& pattern,
                            const std::vector<std::string>& text,
                            std::size_t p, std::size_t t) {
    while (p < pattern.size()) {
        if (pattern[p] == "**") {
            for (std::size_t skip = t; skip <= text.size(); ++skip) {
                if (matchComponents(pattern, text, p + 1, skip)) {
                    return true;
                }
            }
            return false;
        }
        if (t >= text.size() || !componentMatch(pattern[p], text[t])) {
            return false;
        }
        ++p;
        ++t;
    }
    return t == text.size();
}

static bool globMatch(const std::string& pattern, const std::string& path) {
    return matchComponents(splitComponents(pattern), splitComponents(path), 0,
                           0);
}

static std::string expandHome(const std::string& input) {
    if (input.empty() || input[0] != '~') {
        return input;
    }
    const std::size_t slash = input.find('/');
    const std::string user = input.substr(
        1, slash == std::string::npos ? std::string::npos : slash - 1);
    std::string home;
    if (user.empty()) {
        const char* value = std::getenv("HOME");
        home = value == nullptr ? "" : value;
    } else if (passwd* entry = ::getpwnam(user.c_str()); entry != nullptr) {
        home = entry->pw_dir;
    } else {
        return input;
    }
    if (home.empty()) {
        return input;
    }
    return home +
           (slash == std::string::npos ? "" : input.substr(slash));
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

/* ---------- Construction ---------- */

extern "C" const char* path_cwd() {
    std::error_code error;
    const fs::path current = fs::current_path(error);
    return stable(error ? std::string() : current.string());
}

extern "C" const char* path_home() {
    const char* value = std::getenv("HOME");
    return stable(value == nullptr ? std::string() : std::string(value));
}

extern "C" const char* path_absolute(const char* value) {
    std::error_code error;
    const fs::path result = fs::absolute(fs::path(textOrEmpty(value)), error);
    return stable(error ? std::string() : result.string());
}

extern "C" const char* path_resolve(const char* value) {
    std::error_code error;
    const fs::path result =
        fs::weakly_canonical(fs::path(textOrEmpty(value)), error);
    return stable(error ? std::string() : result.string());
}

extern "C" const char* path_expandUser(const char* value) {
    return stable(expandHome(textOrEmpty(value)));
}

extern "C" const char* path_join(const char* base, const char* child) {
    return stable(
        (fs::path(textOrEmpty(base)) / fs::path(textOrEmpty(child))).string());
}

extern "C" const char* path_join3(const char* first, const char* second,
                                  const char* third) {
    return stable((fs::path(textOrEmpty(first)) / fs::path(textOrEmpty(second)) /
                   fs::path(textOrEmpty(third)))
                      .string());
}

extern "C" const char* path_normalize(const char* value) {
    const std::string input = textOrEmpty(value);
    if (input.empty()) {
        return stable(".");
    }
    return stable(fs::path(input).lexically_normal().string());
}

/* ---------- Components ---------- */

extern "C" const char* path_name(const char* value) {
    return stable(fs::path(textOrEmpty(value)).filename().string());
}

extern "C" const char* path_stem(const char* value) {
    return stable(fs::path(textOrEmpty(value)).stem().string());
}

extern "C" const char* path_suffix(const char* value) {
    return stable(fs::path(textOrEmpty(value)).extension().string());
}

extern "C" const char* path_suffixes(const char* value) {
    std::string name = fs::path(textOrEmpty(value)).filename().string();
    if (name.empty() || name.back() == '.') {
        return stable("");
    }
    if (name.front() == '.') {
        name.erase(name.begin());
    }
    std::vector<std::string> suffixes;
    std::size_t start = name.find('.');
    while (start != std::string::npos) {
        const std::size_t next = name.find('.', start + 1);
        suffixes.push_back("." + name.substr(
                                    start + 1, next == std::string::npos
                                                   ? std::string::npos
                                                   : next - start - 1));
        start = next;
    }
    return stable(joinLines(suffixes, false));
}

extern "C" const char* path_parent(const char* value) {
    return stable(fs::path(textOrEmpty(value)).parent_path().string());
}

extern "C" const char* path_anchor(const char* value) {
    return stable(fs::path(textOrEmpty(value)).is_absolute() ? "/" : "");
}

extern "C" const char* path_root(const char* value) {
    return stable(fs::path(textOrEmpty(value)).is_absolute() ? "/" : "");
}

extern "C" const char* path_drive(const char*) { return ""; }

extern "C" std::int64_t path_isAbsolute(const char* value) {
    return fs::path(textOrEmpty(value)).is_absolute() ? 1 : 0;
}

extern "C" const char* path_parts(const char* value) {
    const fs::path target(textOrEmpty(value));
    std::vector<std::string> parts;
    if (target.is_absolute()) {
        parts.push_back("/");
    }
    for (const auto& component : target) {
        const std::string text = component.string();
        if (text == "/" || text.empty()) {
            continue;
        }
        parts.push_back(text);
    }
    return stable(joinLines(parts, false));
}

extern "C" std::int64_t path_match(const char* value, const char* pattern) {
    const std::string patternText = textOrEmpty(pattern);
    const fs::path target(textOrEmpty(value));
    const std::vector<std::string> patternParts =
        splitComponents(patternText);
    const bool absolutePattern =
        !patternText.empty() && patternText.front() == '/';
    std::vector<std::string> targetParts = splitComponents(target.string());
    if (!absolutePattern && targetParts.size() > patternParts.size()) {
        targetParts.erase(
            targetParts.begin(),
            targetParts.end() -
                static_cast<std::ptrdiff_t>(patternParts.size()));
    }
    return matchComponents(patternParts, targetParts, 0, 0) ? 1 : 0;
}

extern "C" const char* path_relativeTo(const char* value, const char* base) {
    const fs::path target = fs::path(textOrEmpty(value)).lexically_normal();
    const fs::path root = fs::path(textOrEmpty(base)).lexically_normal();
    const std::vector<std::string> targetParts =
        splitComponents(target.string());
    const std::vector<std::string> baseParts = splitComponents(root.string());
    if (baseParts.size() > targetParts.size()) {
        return stable("");
    }
    for (std::size_t index = 0; index < baseParts.size(); ++index) {
        if (baseParts[index] != targetParts[index]) {
            return stable("");
        }
    }
    std::vector<std::string> remainder(
        targetParts.begin() +
            static_cast<std::ptrdiff_t>(baseParts.size()),
        targetParts.end());
    return stable(joinLines(remainder, false));
}

extern "C" const char* path_withName(const char* value, const char* newName) {
    const fs::path target(textOrEmpty(value));
    const std::string name = textOrEmpty(newName);
    if (name.empty() || name.find('/') != std::string::npos) {
        return stable("");
    }
    if (target.filename().empty()) {
        return stable("");
    }
    return stable((target.parent_path() / name).string());
}

extern "C" const char* path_withSuffix(const char* value,
                                       const char* newSuffix) {
    const fs::path target(textOrEmpty(value));
    const std::string suffix = textOrEmpty(newSuffix);
    const std::string name = target.filename().string();
    if (name.empty()) {
        return stable("");
    }
    if (!suffix.empty() && suffix.front() != '.') {
        return stable("");
    }
    if (target.extension().empty() && suffix.empty()) {
        return stable("");
    }
    fs::path replaced = target;
    replaced.replace_extension(suffix.empty() ? fs::path() : fs::path(suffix));
    return stable(replaced.string());
}

/* ---------- Predicates ---------- */

extern "C" std::int64_t path_exists(const char* value) {
    std::error_code error;
    return fs::exists(fs::path(textOrEmpty(value)), error) ? 1 : 0;
}

extern "C" std::int64_t path_isFile(const char* value) {
    std::error_code error;
    return fs::is_regular_file(fs::path(textOrEmpty(value)), error) ? 1 : 0;
}

extern "C" std::int64_t path_isDir(const char* value) {
    std::error_code error;
    return fs::is_directory(fs::path(textOrEmpty(value)), error) ? 1 : 0;
}

extern "C" std::int64_t path_isSymlink(const char* value) {
    std::error_code error;
    return fs::is_symlink(fs::symlink_status(fs::path(textOrEmpty(value)),
                                             error)) &&
                   !error
               ? 1
               : 0;
}

extern "C" std::int64_t path_isMount(const char* value) {
    const std::string target = textOrEmpty(value);
    if (target.empty()) {
        return 0;
    }
    struct stat info {};
    if (::stat(target.c_str(), &info) != 0) {
        return 0;
    }
    struct stat parent {};
    const std::string parentPath =
        fs::path(target).parent_path().string();
    if (parentPath.empty() || ::stat(parentPath.c_str(), &parent) != 0) {
        return 0;
    }
    return info.st_dev != parent.st_dev ? 1 : 0;
}

extern "C" std::int64_t path_sameFile(const char* first, const char* second) {
    struct stat left {};
    struct stat right {};
    if (::stat(textOrEmpty(first).c_str(), &left) != 0) {
        return 0;
    }
    if (::stat(textOrEmpty(second).c_str(), &right) != 0) {
        return 0;
    }
    return left.st_dev == right.st_dev && left.st_ino == right.st_ino ? 1 : 0;
}

/* ---------- Traversal ---------- */

static std::string listDirectory(const std::string& base) {
    std::error_code error;
    fs::directory_iterator iterator(fs::path(base), error);
    if (error) {
        return "";
    }
    std::vector<std::string> entries;
    for (const auto& entry : iterator) {
        entries.push_back(entry.path().string());
    }
    return joinLines(entries, true);
}

extern "C" const char* path_iterDir(const char* value) {
    return stable(listDirectory(textOrEmpty(value)));
}

extern "C" const char* path_glob(const char* value, const char* pattern) {
    const fs::path base(textOrEmpty(value));
    const std::string patternText = textOrEmpty(pattern);
    std::error_code error;
    std::vector<std::string> matches;
    if (patternText.find("**") != std::string::npos) {
        fs::recursive_directory_iterator iterator(base, error);
        if (error) {
            return stable("");
        }
        for (const auto& entry : iterator) {
            const std::string relative =
                fs::relative(entry.path(), base, error).string();
            if (!error && globMatch(patternText, relative)) {
                matches.push_back(entry.path().string());
            }
        }
    } else {
        const std::size_t patternDepth = splitComponents(patternText).size();
        fs::recursive_directory_iterator iterator(
            base, fs::directory_options::skip_permission_denied, error);
        if (error) {
            return stable("");
        }
        for (const auto& entry : iterator) {
            const std::string relative =
                fs::relative(entry.path(), base, error).string();
            if (error) {
                continue;
            }
            if (splitComponents(relative).size() >= patternDepth) {
                iterator.disable_recursion_pending();
            }
            if (globMatch(patternText, relative)) {
                matches.push_back(entry.path().string());
            }
        }
    }
    return stable(joinLines(matches, true));
}

extern "C" const char* path_rglob(const char* value, const char* pattern) {
    const std::string combined = "**/" + textOrEmpty(pattern);
    return path_glob(value, combined.c_str());
}

/* ---------- Mutation ---------- */

extern "C" std::int64_t path_mkdir(const char* value) {
    std::error_code error;
    return fs::create_directory(fs::path(textOrEmpty(value)), error) ? 1 : 0;
}

extern "C" std::int64_t path_mkdirs(const char* value) {
    std::error_code error;
    fs::create_directories(fs::path(textOrEmpty(value)), error);
    return error ? 0 : 1;
}

extern "C" std::int64_t path_rmdir(const char* value) {
    std::error_code error;
    return fs::remove(fs::path(textOrEmpty(value)), error) ? 1 : 0;
}

extern "C" std::int64_t path_unlink(const char* value) {
    std::error_code error;
    return fs::remove(fs::path(textOrEmpty(value)), error) ? 1 : 0;
}

extern "C" std::int64_t path_unlinkMissingOk(const char* value) {
    std::error_code error;
    fs::remove(fs::path(textOrEmpty(value)), error);
    return 1;
}

extern "C" std::int64_t path_touch(const char* value) {
    std::ofstream output(textOrEmpty(value),
                         std::ios::binary | std::ios::app);
    if (!output) {
        return 0;
    }
    output.close();
    std::error_code error;
    fs::last_write_time(fs::path(textOrEmpty(value)),
                        fs::file_time_type::clock::now(), error);
    return error ? 0 : 1;
}

extern "C" const char* path_rename(const char* value, const char* target) {
    std::error_code error;
    const fs::path destination(textOrEmpty(target));
    fs::rename(fs::path(textOrEmpty(value)), destination, error);
    return stable(error ? std::string() : destination.string());
}

extern "C" const char* path_replace(const char* value, const char* target) {
    std::error_code error;
    const fs::path destination(textOrEmpty(target));
    fs::rename(fs::path(textOrEmpty(value)), destination, error);
    return stable(error ? std::string() : destination.string());
}

/* ---------- Text helpers ---------- */

extern "C" const char* path_readText(const char* value) {
    std::string content;
    if (!readWholeFile(textOrEmpty(value), content)) {
        return stable("");
    }
    return stable(std::move(content));
}

extern "C" const char* path_readTextEncoding(const char* value, const char*) {
    return path_readText(value);
}

extern "C" std::int64_t path_writeText(const char* value,
                                       const char* content) {
    std::ofstream output(textOrEmpty(value), std::ios::binary | std::ios::trunc);
    if (!output) {
        return 0;
    }
    output << textOrEmpty(content);
    return output ? 1 : 0;
}

extern "C" std::int64_t path_writeTextEncoding(const char* value,
                                               const char* content,
                                               const char*) {
    return path_writeText(value, content);
}

extern "C" std::int64_t path_appendText(const char* value,
                                        const char* content) {
    std::ofstream output(textOrEmpty(value), std::ios::binary | std::ios::app);
    if (!output) {
        return 0;
    }
    output << textOrEmpty(content);
    return output ? 1 : 0;
}

extern "C" std::int64_t path_size(const char* value) {
    std::error_code error;
    const std::uintmax_t size =
        fs::file_size(fs::path(textOrEmpty(value)), error);
    return error ? -1 : static_cast<std::int64_t>(size);
}

extern "C" double path_modifiedTime(const char* value) {
    struct stat info {};
    if (::stat(textOrEmpty(value).c_str(), &info) != 0) {
        return -1.0;
    }
#if defined(__APPLE__)
    return static_cast<double>(info.st_mtime) +
           static_cast<double>(info.st_mtimespec.tv_nsec) / 1000000000.0;
#else
    return static_cast<double>(info.st_mtime) +
           static_cast<double>(info.st_mtim.tv_nsec) / 1000000000.0;
#endif
}

extern "C" const char* path_asUri(const char* value) {
    std::error_code error;
    const fs::path absolute = fs::absolute(fs::path(textOrEmpty(value)), error);
    if (error) {
        return stable("");
    }
    std::string uri = "file://";
    for (const char character : absolute.string()) {
        const bool unreserved =
            std::isalnum(static_cast<unsigned char>(character)) != 0 ||
            character == '-' || character == '.' || character == '_' ||
            character == '~' || character == '/';
        if (unreserved) {
            uri += character;
        } else {
            char buffer[8];
            std::snprintf(buffer, sizeof(buffer), "%%%02X",
                          static_cast<unsigned char>(character));
            uri += buffer;
        }
    }
    return stable(std::move(uri));
}

extern "C" int clynxer_module_init_v1(RegisterFunction function,
                                     RegisterConstant, RegisterType) {
    return function("cwd", "path_cwd", "cdecl:cstring()") &&
                   function("home", "path_home", "cdecl:cstring()") &&
                   function("absolute", "path_absolute",
                            "cdecl:cstring(cstring)") &&
                   function("resolve", "path_resolve",
                            "cdecl:cstring(cstring)") &&
                   function("expandUser", "path_expandUser",
                            "cdecl:cstring(cstring)") &&
                   function("join", "path_join",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("join3", "path_join3",
                            "cdecl:cstring(cstring,cstring,cstring)") &&
                   function("normalize", "path_normalize",
                            "cdecl:cstring(cstring)") &&
                   function("name", "path_name", "cdecl:cstring(cstring)") &&
                   function("stem", "path_stem", "cdecl:cstring(cstring)") &&
                   function("suffix", "path_suffix", "cdecl:cstring(cstring)") &&
                   function("suffixes", "path_suffixes",
                            "cdecl:cstring(cstring)") &&
                   function("parent", "path_parent", "cdecl:cstring(cstring)") &&
                   function("anchor", "path_anchor", "cdecl:cstring(cstring)") &&
                   function("root", "path_root", "cdecl:cstring(cstring)") &&
                   function("drive", "path_drive", "cdecl:cstring(cstring)") &&
                   function("isAbsolute", "path_isAbsolute",
                            "cdecl:int64(cstring)") &&
                   function("parts", "path_parts", "cdecl:cstring(cstring)") &&
                   function("match", "path_match",
                            "cdecl:int64(cstring,cstring)") &&
                   function("relativeTo", "path_relativeTo",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("withName", "path_withName",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("withSuffix", "path_withSuffix",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("exists", "path_exists", "cdecl:int64(cstring)") &&
                   function("isFile", "path_isFile", "cdecl:int64(cstring)") &&
                   function("isDir", "path_isDir", "cdecl:int64(cstring)") &&
                   function("isSymlink", "path_isSymlink",
                            "cdecl:int64(cstring)") &&
                   function("isMount", "path_isMount", "cdecl:int64(cstring)") &&
                   function("sameFile", "path_sameFile",
                            "cdecl:int64(cstring,cstring)") &&
                   function("iterDir", "path_iterDir",
                            "cdecl:cstring(cstring)") &&
                   function("glob", "path_glob",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("rglob", "path_rglob",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("mkdir", "path_mkdir", "cdecl:int64(cstring)") &&
                   function("mkdirs", "path_mkdirs", "cdecl:int64(cstring)") &&
                   function("rmdir", "path_rmdir", "cdecl:int64(cstring)") &&
                   function("unlink", "path_unlink", "cdecl:int64(cstring)") &&
                   function("unlinkMissingOk", "path_unlinkMissingOk",
                            "cdecl:int64(cstring)") &&
                   function("touch", "path_touch", "cdecl:int64(cstring)") &&
                   function("rename", "path_rename",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("replace", "path_replace",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("readText", "path_readText",
                            "cdecl:cstring(cstring)") &&
                   function("readTextEncoding", "path_readTextEncoding",
                            "cdecl:cstring(cstring,cstring)") &&
                   function("writeText", "path_writeText",
                            "cdecl:int64(cstring,cstring)") &&
                   function("writeTextEncoding", "path_writeTextEncoding",
                            "cdecl:int64(cstring,cstring,cstring)") &&
                   function("appendText", "path_appendText",
                            "cdecl:int64(cstring,cstring)") &&
                   function("size", "path_size", "cdecl:int64(cstring)") &&
                   function("modifiedTime", "path_modifiedTime",
                            "cdecl:double(cstring)") &&
                   function("asUri", "path_asUri", "cdecl:cstring(cstring)")
               ? 0
               : 1;
}
