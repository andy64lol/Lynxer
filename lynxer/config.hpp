#pragma once

#include <string>
#include <unordered_map>

namespace lynxer {

// Directory containing the running executable (fallback: '.').
std::string executableDirectory();

// Full path of the running executable (fallback: empty).
std::string executablePath();

// Loads lynxer.config from the executable directory (or the working
// directory). When the file is missing the compiled-in defaults are used, so
// the binary behaves identically to a distribution that ships the file.
class Config {
public:
    static const Config& instance();

    // Value for a configuration key, or fallback when the key is absent.
    std::string get(const std::string& key, const std::string& fallback) const;

    // Replaces 'search' with 'replacement' in a configuration value.
    std::string format(const std::string& key, const std::string& fallback,
                       const std::string& search,
                       const std::string& replacement) const;

private:
    Config();

    void setDefault(const std::string& key, const std::string& value);

    std::unordered_map<std::string, std::string> values_;
};

} // namespace lynxer
