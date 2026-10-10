// Lynxer `path` stdlib backend: pathlib-style path manipulation built on
// <filesystem> plus POSIX stat calls.

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sys/stat.h>
#else
#include <iconv.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using RegisterFunction = int (*)(const char*, const char*, const char*);
using RegisterConstant = int (*)(const char*, std::int64_t);
using RegisterType = int (*)(const char*, const char*);

namespace fs = std::filesystem;

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
        if (character == '/' || character == '\\') {
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
    const std::size_t slash = input.find_first_of("/\\");
    const std::string user = input.substr(
        1, slash == std::string::npos ? std::string::npos : slash - 1);
    std::string home;
    if (user.empty()) {
#if defined(_WIN32)
        const char* value = std::getenv("USERPROFILE");
#else
        const char* value = std::getenv("HOME");
#endif
        home = value == nullptr ? "" : value;
    }
#if !defined(_WIN32)
    else if (passwd* entry = ::getpwnam(user.c_str()); entry != nullptr) {
        home = entry->pw_dir;
    }
#endif
    else {
        return input;
    }
    if (home.empty()) {
        return input;
    }
    return home +
           (slash == std::string::npos ? "" : input.substr(slash));
}

static bool readWholeFile(const std::string& path, std::string& output) {
    std::ifstream input(pathFromUtf8(path), std::ios::binary);
    if (!input) {
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    output = buffer.str();
    return true;
}

/* ---------- Construction ---------- */

extern "C" const char* path_platform() {
#if defined(__linux__)
    return "linux";
#elif defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "darwin";
#elif defined(__FreeBSD__)
    return "freebsd";
#elif defined(__NetBSD__)
    return "netbsd";
#elif defined(__OpenBSD__)
    return "openbsd";
#elif defined(__DragonFly__)
    return "dragonfly";
#else
    return "unknown";
#endif
}

extern "C" const char* path_separator() {
#if defined(_WIN32)
    return "\\";
#else
    return "/";
#endif
}
extern "C" const char* path_listSeparator() {
#if defined(_WIN32)
    return ";";
#else
    return ":";
#endif
}

extern "C" const char* path_cwd() {
    std::error_code error;
    const fs::path current = fs::current_path(error);
    return stable(error ? std::string() : pathToUtf8(current));
}

extern "C" const char* path_home() {
#if defined(_WIN32)
    const char* value = std::getenv("USERPROFILE");
#else
    const char* value = std::getenv("HOME");
#endif
    return stable(value == nullptr ? std::string() : std::string(value));
}

extern "C" const char* path_absolute(const char* value) {
    std::error_code error;
    const fs::path result = fs::absolute(pathFromUtf8(textOrEmpty(value)), error);
    return stable(error ? std::string() : pathToUtf8(result));
}

extern "C" const char* path_resolve(const char* value) {
    std::error_code error;
    const fs::path result =
        fs::weakly_canonical(pathFromUtf8(textOrEmpty(value)), error);
    return stable(error ? std::string() : pathToUtf8(result));
}

extern "C" const char* path_expandUser(const char* value) {
    return stable(expandHome(textOrEmpty(value)));
}

extern "C" const char* path_join(const char* base, const char* child) {
    return stable(
        pathToUtf8(pathFromUtf8(textOrEmpty(base)) / pathFromUtf8(textOrEmpty(child))));
}

extern "C" const char* path_join3(const char* first, const char* second,
                                  const char* third) {
    return stable(pathToUtf8(pathFromUtf8(textOrEmpty(first)) /
                             pathFromUtf8(textOrEmpty(second)) /
                             pathFromUtf8(textOrEmpty(third))));
}

extern "C" const char* path_normalize(const char* value) {
    const std::string input = textOrEmpty(value);
    if (input.empty()) {
        return stable(".");
    }
    return stable(pathToUtf8(pathFromUtf8(input).lexically_normal()));
}

/* ---------- Components ---------- */

extern "C" const char* path_name(const char* value) {
    return stable(pathToUtf8(pathFromUtf8(textOrEmpty(value)).filename()));
}

extern "C" const char* path_stem(const char* value) {
    return stable(pathToUtf8(pathFromUtf8(textOrEmpty(value)).stem()));
}

extern "C" const char* path_suffix(const char* value) {
    return stable(pathToUtf8(pathFromUtf8(textOrEmpty(value)).extension()));
}

extern "C" const char* path_suffixes(const char* value) {
    std::string name = pathToUtf8(pathFromUtf8(textOrEmpty(value)).filename());
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
    return stable(pathToUtf8(pathFromUtf8(textOrEmpty(value)).parent_path()));
}

extern "C" const char* path_anchor(const char* value) {
#if defined(_WIN32)
    const fs::path path = pathFromUtf8(textOrEmpty(value));
    return stable(path.has_root_name() ? pathToUtf8(path.root_name() / path.root_directory()) : "");
#else
    return stable(pathFromUtf8(textOrEmpty(value)).is_absolute() ? "/" : "");
#endif
}

extern "C" const char* path_root(const char* value) {
    return stable(pathToUtf8(pathFromUtf8(textOrEmpty(value)).root_directory()));
}

extern "C" const char* path_drive(const char* value) {
#if defined(_WIN32)
    return stable(pathToUtf8(pathFromUtf8(textOrEmpty(value)).root_name()));
#else
    (void)value; return "";
#endif
}

extern "C" std::int64_t path_isAbsolute(const char* value) {
    return pathFromUtf8(textOrEmpty(value)).is_absolute() ? 1 : 0;
}

extern "C" const char* path_parts(const char* value) {
    const fs::path target(textOrEmpty(value));
    std::vector<std::string> parts;
    if (target.is_absolute()) {
        parts.push_back("/");
    }
    for (const auto& component : target) {
        const std::string text = pathToUtf8(component);
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
    std::vector<std::string> targetParts = splitComponents(pathToUtf8(target));
    if (!absolutePattern && targetParts.size() > patternParts.size()) {
        targetParts.erase(
            targetParts.begin(),
            targetParts.end() -
                static_cast<std::ptrdiff_t>(patternParts.size()));
    }
    return matchComponents(patternParts, targetParts, 0, 0) ? 1 : 0;
}

extern "C" const char* path_relativeTo(const char* value, const char* base) {
    const fs::path target = pathFromUtf8(textOrEmpty(value)).lexically_normal();
    const fs::path root = pathFromUtf8(textOrEmpty(base)).lexically_normal();
    const std::vector<std::string> targetParts =
        splitComponents(pathToUtf8(target));
    const std::vector<std::string> baseParts = splitComponents(pathToUtf8(root));
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
    return stable(pathToUtf8(target.parent_path() / pathFromUtf8(name)));
}

extern "C" const char* path_withSuffix(const char* value,
                                       const char* newSuffix) {
    const fs::path target(textOrEmpty(value));
    const std::string suffix = textOrEmpty(newSuffix);
    const std::string name = pathToUtf8(target.filename());
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
    replaced.replace_extension(suffix.empty() ? fs::path() : pathFromUtf8(suffix));
    return stable(pathToUtf8(replaced));
}

/* ---------- Predicates ---------- */

extern "C" std::int64_t path_exists(const char* value) {
    std::error_code error;
    return fs::exists(pathFromUtf8(textOrEmpty(value)), error) ? 1 : 0;
}

extern "C" std::int64_t path_isFile(const char* value) {
    std::error_code error;
    return fs::is_regular_file(pathFromUtf8(textOrEmpty(value)), error) ? 1 : 0;
}

extern "C" std::int64_t path_isDir(const char* value) {
    std::error_code error;
    return fs::is_directory(pathFromUtf8(textOrEmpty(value)), error) ? 1 : 0;
}

extern "C" std::int64_t path_isSymlink(const char* value) {
    std::error_code error;
    return fs::is_symlink(fs::symlink_status(pathFromUtf8(textOrEmpty(value)),
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
#if defined(_WIN32)
    const std::wstring wide = pathFromUtf8(target).wstring();
    wchar_t volume[MAX_PATH + 1]{};
    if (!::GetVolumePathNameW(wide.c_str(), volume, MAX_PATH)) return 0;
    std::error_code volumeError;
    return fs::equivalent(pathFromUtf8(target), fs::path(volume), volumeError) && !volumeError ? 1 : 0;
#else
    struct stat info {};
    if (::stat(target.c_str(), &info) != 0) {
        return 0;
    }
    struct stat parent {};
    const std::string parentPath =
        pathToUtf8(pathFromUtf8(target).parent_path());
    if (parentPath.empty() || ::stat(parentPath.c_str(), &parent) != 0) {
        return 0;
    }
    return info.st_dev != parent.st_dev ? 1 : 0;
#endif
}

extern "C" std::int64_t path_sameFile(const char* first, const char* second) {
#if defined(_WIN32)
    std::error_code error;
    return fs::equivalent(pathFromUtf8(textOrEmpty(first)),
                          pathFromUtf8(textOrEmpty(second)), error) && !error ? 1 : 0;
#else
    struct stat left {};
    struct stat right {};
    if (::stat(textOrEmpty(first).c_str(), &left) != 0) {
        return 0;
    }
    if (::stat(textOrEmpty(second).c_str(), &right) != 0) {
        return 0;
    }
    return left.st_dev == right.st_dev && left.st_ino == right.st_ino ? 1 : 0;
#endif
}

/* ---------- Traversal ---------- */

static std::string listDirectory(const std::string& base) {
    std::error_code error;
    fs::directory_iterator iterator(pathFromUtf8(base), error);
    if (error) {
        return "";
    }
    std::vector<std::string> entries;
    for (const auto& entry : iterator) {
        entries.push_back(pathToUtf8(entry.path()));
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
                pathToUtf8(fs::relative(entry.path(), base, error));
            if (!error && globMatch(patternText, relative)) {
                matches.push_back(pathToUtf8(entry.path()));
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
                pathToUtf8(fs::relative(entry.path(), base, error));
            if (error) {
                continue;
            }
            if (splitComponents(relative).size() >= patternDepth) {
                iterator.disable_recursion_pending();
            }
            if (globMatch(patternText, relative)) {
                matches.push_back(pathToUtf8(entry.path()));
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
    return fs::create_directory(pathFromUtf8(textOrEmpty(value)), error) ? 1 : 0;
}

extern "C" std::int64_t path_mkdirs(const char* value) {
    std::error_code error;
    fs::create_directories(pathFromUtf8(textOrEmpty(value)), error);
    return error ? 0 : 1;
}

extern "C" std::int64_t path_rmdir(const char* value) {
    std::error_code error;
    return fs::remove(pathFromUtf8(textOrEmpty(value)), error) ? 1 : 0;
}

extern "C" std::int64_t path_unlink(const char* value) {
    std::error_code error;
    return fs::remove(pathFromUtf8(textOrEmpty(value)), error) ? 1 : 0;
}

extern "C" std::int64_t path_unlinkMissingOk(const char* value) {
    std::error_code error;
    fs::remove(pathFromUtf8(textOrEmpty(value)), error);
    return 1;
}

extern "C" std::int64_t path_touch(const char* value) {
    std::ofstream output(pathFromUtf8(textOrEmpty(value)),
                         std::ios::binary | std::ios::app);
    if (!output) {
        return 0;
    }
    output.close();
    std::error_code error;
    fs::last_write_time(pathFromUtf8(textOrEmpty(value)),
                        fs::file_time_type::clock::now(), error);
    return error ? 0 : 1;
}

extern "C" const char* path_rename(const char* value, const char* target) {
    std::error_code error;
    const fs::path destination(textOrEmpty(target));
    fs::rename(pathFromUtf8(textOrEmpty(value)), destination, error);
    return stable(error ? std::string() : pathToUtf8(destination));
}

extern "C" const char* path_replace(const char* value, const char* target) {
    std::error_code error;
    const fs::path destination(textOrEmpty(target));
    fs::rename(pathFromUtf8(textOrEmpty(value)), destination, error);
    return stable(error ? std::string() : pathToUtf8(destination));
}

/* ---------- Text helpers ---------- */

extern "C" const char* path_readText(const char* value) {
    std::string content;
    if (!readWholeFile(textOrEmpty(value), content)) {
        return stable("");
    }
    return stable(std::move(content));
}

// --- Text encodings --------------------------------------------------------

static bool parseTextEncoding(const std::string& raw, std::string& out) {
    std::string name;
    for (const char character : raw) {
        name += static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }
    for (char& character : name) {
        if (character == '_') {
            character = '-';
        }
    }
    if (name.empty() || name == "utf-8" || name == "utf8") {
        out = "UTF-8";
        return true;
    }
    if (name == "latin-1" || name == "latin1" || name == "iso-8859-1" ||
        name == "iso8859-1" || name == "iso_8859-1") {
        out = "ISO-8859-1";
        return true;
    }
    if (name == "ascii" || name == "us-ascii") {
        out = "ASCII";
        return true;
    }
    if (name == "windows-1252" || name == "cp1252" || name == "windows1252") {
        out = "CP1252";
        return true;
    }
    if (name == "utf16" || name == "utf-16") {
        out = "UTF-16";
        return true;
    }
    if (name == "utf16le" || name == "utf-16-le") {
        out = "UTF-16LE";
        return true;
    }
    if (name == "utf16be" || name == "utf-16-be") {
        out = "UTF-16BE";
        return true;
    }
    if (name == "utf32" || name == "utf-32") {
        out = "UTF-32";
        return true;
    }
    if (name == "utf32le" || name == "utf-32-le") {
        out = "UTF-32LE";
        return true;
    }
    if (name == "utf32be" || name == "utf-32-be") {
        out = "UTF-32BE";
        return true;
    }
    // POSIX iconv provides the remaining names supported by the host, such as
    // additional ISO-8859 pages and Shift-JIS. iconv_open rejects unknown names.
    out = name;
    return true;
}

static bool convertEncoding(const std::string& input, const std::string& from,
                            const std::string& to, std::string& output) {
#if defined(_WIN32)
    auto codePage = [](const std::string& name) -> UINT {
        if (name == "UTF-8") return CP_UTF8;
        if (name == "ASCII") return 20127;
        if (name == "ISO-8859-1") return 28591;
        if (name == "CP1252") return 1252;
        if (name == "CP1250") return 1250;
        if (name == "CP1251") return 1251;
        if (name == "CP1254") return 1254;
        if (name == "CP1255") return 1255;
        if (name == "CP1256") return 1256;
        if (name == "CP1257") return 1257;
        if (name == "CP1258") return 1258;
        if (name == "CP932" || name == "SHIFT-JIS" || name == "SHIFT_JIS") return 932;
        if (name == "CP936" || name == "GBK") return 936;
        if (name == "CP949") return 949;
        if (name == "CP950" || name == "BIG5") return 950;
        if (name == "CP437") return 437;
        if (name == "CP850") return 850;
        return 0;
    };
    auto fromWide = [&](const std::wstring& wide, const std::string& encoding, std::string& result) {
        if (encoding == "UTF-32" || encoding == "UTF-32LE" || encoding == "UTF-32BE") {
            result.clear();
            const bool be = encoding == "UTF-32BE";
            for (std::size_t i = 0; i < wide.size(); ++i) {
                std::uint32_t cp = static_cast<std::uint16_t>(wide[i]);
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    if (i + 1 >= wide.size()) return false;
                    const std::uint32_t low = static_cast<std::uint16_t>(wide[++i]);
                    if (low < 0xdc00 || low > 0xdfff) return false;
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                } else if (cp >= 0xdc00 && cp <= 0xdfff) return false;
                for (int n = 0; n < 4; ++n) result.push_back(static_cast<char>((cp >> ((be ? 3 - n : n) * 8)) & 255));
            }
            return true;
        }
        if (encoding == "UTF-16" || encoding == "UTF-16LE") {
            result.assign(reinterpret_cast<const char*>(wide.data()), wide.size() * sizeof(wchar_t));
            return true;
        }
        if (encoding == "UTF-16BE") {
            result.clear();
            for (wchar_t ch : wide) { result.push_back(static_cast<char>((ch >> 8) & 255)); result.push_back(static_cast<char>(ch & 255)); }
            return true;
        }
        const UINT cp = codePage(encoding);
        if (!cp) return false;
        const DWORD flags = cp == CP_UTF8 ? 0 : WC_NO_BEST_FIT_CHARS;
        const int n = ::WideCharToMultiByte(cp, flags, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        if (n <= 0) return wide.empty();
        result.resize(static_cast<std::size_t>(n));
        return ::WideCharToMultiByte(cp, flags, wide.data(), static_cast<int>(wide.size()), result.data(), n, nullptr, nullptr) == n;
    };
    auto toWide = [&](const std::string& bytes, const std::string& encoding, std::wstring& wide) {
        if (encoding == "UTF-32" || encoding == "UTF-32LE" || encoding == "UTF-32BE") {
            if (bytes.size() % 4) return false;
            const bool be = encoding == "UTF-32BE";
            wide.clear();
            for (std::size_t i = 0; i < bytes.size(); i += 4) {
                std::uint32_t cp = 0;
                for (int n = 0; n < 4; ++n) cp |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i + n])) << ((be ? 3 - n : n) * 8);
                if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
                if (cp < 0x10000) wide.push_back(static_cast<wchar_t>(cp));
                else { cp -= 0x10000; wide.push_back(static_cast<wchar_t>(0xd800 + (cp >> 10))); wide.push_back(static_cast<wchar_t>(0xdc00 + (cp & 0x3ff))); }
            }
            return true;
        }
        if (encoding == "UTF-16" || encoding == "UTF-16LE") {
            if (bytes.size() % 2) return false;
            wide.resize(bytes.size() / 2);
            std::memcpy(wide.data(), bytes.data(), bytes.size());
            return true;
        }
        if (encoding == "UTF-16BE") {
            if (bytes.size() % 2) return false;
            wide.resize(bytes.size() / 2);
            for (std::size_t i = 0; i < wide.size(); ++i) wide[i] = static_cast<unsigned char>(bytes[i*2]) * 256 + static_cast<unsigned char>(bytes[i*2+1]);
            return true;
        }
        const UINT cp = codePage(encoding);
        if (!cp) return false;
        const int n = ::MultiByteToWideChar(cp, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        if (n <= 0) return bytes.empty();
        wide.resize(static_cast<std::size_t>(n));
        return ::MultiByteToWideChar(cp, 0, bytes.data(), static_cast<int>(bytes.size()), wide.data(), n) == n;
    };
    std::wstring wide;
    return toWide(input, from, wide) && fromWide(wide, to, output);
#else
    iconv_t converter = ::iconv_open(to.c_str(), from.c_str());
    if (converter == reinterpret_cast<iconv_t>(-1)) {
        return false;
    }
    output.clear();
    const char* inputCursor = input.data();
    std::size_t inputRemaining = input.size();
    while (inputRemaining > 0) {
        char buffer[4096];
        char* outputCursor = buffer;
        std::size_t outputRemaining = sizeof(buffer);
        char* mutableInput = const_cast<char*>(inputCursor);
        const std::size_t result = ::iconv(
            converter, &mutableInput, &inputRemaining, &outputCursor,
            &outputRemaining);
        inputCursor = mutableInput;
        output.append(buffer, static_cast<std::size_t>(outputCursor - buffer));
        if (result == static_cast<std::size_t>(-1) && errno != E2BIG) {
            ::iconv_close(converter);
            output.clear();
            return false;
        }
    }
    for (;;) {
        char buffer[64];
        char* outputCursor = buffer;
        std::size_t outputRemaining = sizeof(buffer);
        const std::size_t result =
            ::iconv(converter, nullptr, nullptr, &outputCursor, &outputRemaining);
        output.append(buffer, static_cast<std::size_t>(outputCursor - buffer));
        if (result != static_cast<std::size_t>(-1)) {
            break;
        }
        if (errno != E2BIG) {
            ::iconv_close(converter);
            output.clear();
            return false;
        }
    }
    ::iconv_close(converter);
    return true;
#endif
}

extern "C" const char* path_readTextEncoding(const char* value,
                                             const char* encoding) {
    std::string kind;
    if (!parseTextEncoding(textOrEmpty(encoding), kind)) {
        return stable("");
    }
    std::string content;
    if (!readWholeFile(textOrEmpty(value), content)) {
        return stable("");
    }
    std::string decoded;
    return stable(convertEncoding(content, kind, "UTF-8", decoded)
                      ? std::move(decoded)
                      : std::string());
}

extern "C" std::int64_t path_writeText(const char* value,
                                       const char* content) {
    std::ofstream output(pathFromUtf8(textOrEmpty(value)), std::ios::binary | std::ios::trunc);
    if (!output) {
        return 0;
    }
    output << textOrEmpty(content);
    return output ? 1 : 0;
}

extern "C" std::int64_t path_writeTextEncoding(const char* value,
                                               const char* content,
                                               const char* encoding) {
    std::string kind;
    if (!parseTextEncoding(textOrEmpty(encoding), kind)) {
        return 0;
    }
    const std::string text = textOrEmpty(content);
    std::string bytes;
    if (kind == "UTF-8") {
        bytes = text;
    } else if (!convertEncoding(text, "UTF-8", kind, bytes)) {
        return 0;
    }
    std::ofstream output(pathFromUtf8(textOrEmpty(value)), std::ios::binary | std::ios::trunc);
    if (!output) {
        return 0;
    }
    output << bytes;
    return output ? 1 : 0;
}

extern "C" std::int64_t path_appendText(const char* value,
                                        const char* content) {
    std::ofstream output(pathFromUtf8(textOrEmpty(value)), std::ios::binary | std::ios::app);
    if (!output) {
        return 0;
    }
    output << textOrEmpty(content);
    return output ? 1 : 0;
}

extern "C" std::int64_t path_size(const char* value) {
    std::error_code error;
    const std::uintmax_t size =
        fs::file_size(pathFromUtf8(textOrEmpty(value)), error);
    return error ? -1 : static_cast<std::int64_t>(size);
}

extern "C" double path_modifiedTime(const char* value) {
#if defined(_WIN32)
    std::error_code error;
    const auto stamp = fs::last_write_time(pathFromUtf8(textOrEmpty(value)), error);
    if (error) return -1.0;
    const auto systemStamp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        stamp - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::duration<double>(systemStamp.time_since_epoch()).count();
#else
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
#endif
}

extern "C" const char* path_asUri(const char* value) {
    std::error_code error;
    const fs::path absolute = fs::absolute(pathFromUtf8(textOrEmpty(value)), error);
    if (error) {
        return stable("");
    }
    std::string uri = "file://";
    for (const char character : pathToUtf8(absolute)) {
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

extern "C" int lynxer_module_init_v1(RegisterFunction function,
                                     RegisterConstant, RegisterType) {
    return function("cwd", "path_cwd", "cdecl:cstring()") &&
                   function("platform", "path_platform", "cdecl:cstring()") &&
                   function("separator", "path_separator", "cdecl:cstring()") &&
                   function("listSeparator", "path_listSeparator",
                            "cdecl:cstring()") &&
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
