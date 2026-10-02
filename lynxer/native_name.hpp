#pragma once

#include <cctype>
#include <filesystem>
#include <string>

namespace lynxer {

// The module name derived from a source or shared-object path, with the
// `.lynx` / `.lynxc` / `.so` suffix removed. `importAs` and the explicit
// `nativeModule*` handles share this so both agree on a module's name.
inline std::string moduleNameFromPath(const std::string& path) {
    std::filesystem::path name(path);
    std::string value = name.filename().string();
    for (const std::string suffix : {".lynx", ".lynxc", ".so", ".dylib", ".dll"}) {
        if (value.size() > suffix.size() &&
            value.compare(value.size() - suffix.size(), suffix.size(),
                          suffix) == 0) {
            value.resize(value.size() - suffix.size());
            break;
        }
    }
    return value;
}

// True when `path` names a native shared library the loader can open: `.so` on
// Linux/BSD, `.dylib` on macOS (which also accepts `.so`), `.dll` on Windows.
inline bool isNativeLibraryPath(const std::string& path) {
    for (const std::string suffix : {".so", ".dylib", ".dll"}) {
        if (path.size() > suffix.size() &&
            path.compare(path.size() - suffix.size(), suffix.size(), suffix) ==
                0) {
            return true;
        }
    }
    return false;
}

// The native-module ABI's registration-name rule: a leading letter or
// underscore followed by letters, digits or underscores.
inline bool validNativeName(const char* name) {
    if (name == nullptr || *name == '\0' ||
        !(std::isalpha(static_cast<unsigned char>(*name)) || *name == '_')) {
        return false;
    }
    for (const char* cursor = name + 1; *cursor; ++cursor) {
        if (!(std::isalnum(static_cast<unsigned char>(*cursor)) ||
              *cursor == '_')) {
            return false;
        }
    }
    return true;
}

}  // namespace lynxer
