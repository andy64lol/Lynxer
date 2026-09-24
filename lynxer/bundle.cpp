#include "bundle.hpp"

#include "config.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace lynxer {

namespace {

// Trailer appended to a compiled executable:
//   ... executable bytes ... | magic (10) | u64le body size | body
inline constexpr char BUNDLE_MAGIC[10] = {'C', 'L', 'Y', 'X', 'S',
                                          'R', 'C', 'P', 'A', 'Y'};
inline constexpr std::size_t BUNDLE_TRAILER = sizeof(BUNDLE_MAGIC) + 8;

// Body layout version. Bump when the payload layout changes so an older or
// newer executable rejects the payload instead of misreading it.
inline constexpr std::uint64_t BUNDLE_FORMAT_VERSION = 2;

/* ---------- little-endian helpers ---------- */

void appendU64(std::vector<uint8_t>& output, std::uint64_t value) {
    for (int index = 0; index < 8; ++index) {
        output.push_back(static_cast<uint8_t>((value >> (8 * index)) & 0xFF));
    }
}

void appendBlob(std::vector<uint8_t>& output, const std::string& text) {
    appendU64(output, text.size());
    output.insert(output.end(), text.begin(), text.end());
}

void appendBytes(std::vector<uint8_t>& output,
                 const std::vector<uint8_t>& bytes) {
    appendU64(output, bytes.size());
    output.insert(output.end(), bytes.begin(), bytes.end());
}

// Cursor over a payload body; every read is bounds checked.
class Reader {
public:
    explicit Reader(const std::vector<uint8_t>& bytes) : bytes_(bytes) {}

    bool readU64(std::uint64_t& value) {
        if (position_ + 8 > bytes_.size()) {
            return false;
        }
        value = 0;
        for (int index = 7; index >= 0; --index) {
            value = (value << 8) | bytes_[position_ + index];
        }
        position_ += 8;
        return true;
    }

    bool readBlob(std::string& text) {
        std::uint64_t size = 0;
        if (!readU64(size) || position_ + size > bytes_.size()) {
            return false;
        }
        text.assign(reinterpret_cast<const char*>(bytes_.data() + position_),
                    static_cast<std::size_t>(size));
        position_ += static_cast<std::size_t>(size);
        return true;
    }

    bool readBytes(std::vector<uint8_t>& bytes) {
        std::uint64_t size = 0;
        if (!readU64(size) || position_ + size > bytes_.size()) {
            return false;
        }
        bytes.assign(bytes_.begin() + static_cast<std::ptrdiff_t>(position_),
                     bytes_.begin() +
                         static_cast<std::ptrdiff_t>(position_ + size));
        position_ += static_cast<std::size_t>(size);
        return true;
    }

    bool atEnd() const { return position_ == bytes_.size(); }

private:
    const std::vector<uint8_t>& bytes_;
    std::size_t position_ = 0;
};

bool readTrailer(std::ifstream& input, std::vector<uint8_t>& payload) {
    input.seekg(0, std::ios::end);
    const std::streamoff fileSize = input.tellg();
    if (fileSize < static_cast<std::streamoff>(BUNDLE_TRAILER)) {
        return false;
    }
    input.seekg(-(static_cast<std::streamoff>(BUNDLE_TRAILER)), std::ios::end);
    char trailer[BUNDLE_TRAILER];
    input.read(trailer, static_cast<std::streamsize>(BUNDLE_TRAILER));
    if (input.gcount() != static_cast<std::streamsize>(BUNDLE_TRAILER) ||
        std::memcmp(trailer, BUNDLE_MAGIC, sizeof(BUNDLE_MAGIC)) != 0) {
        return false;
    }
    uint64_t size = 0;
    for (int index = 7; index >= 0; --index) {
        size = (size << 8) |
               static_cast<uint8_t>(trailer[sizeof(BUNDLE_MAGIC) + index]);
    }
    if (size == 0 || size > static_cast<uint64_t>(fileSize) - BUNDLE_TRAILER) {
        return false;
    }
    input.seekg(static_cast<std::streamoff>(fileSize) -
                    static_cast<std::streamoff>(BUNDLE_TRAILER) -
                    static_cast<std::streamoff>(size),
                std::ios::beg);
    payload.assign(static_cast<std::size_t>(size), 0);
    input.read(reinterpret_cast<char*>(payload.data()),
               static_cast<std::streamsize>(size));
    return input.gcount() == static_cast<std::streamsize>(size);
}

// Bytes of 'path' to carry into a new bundle: the whole file when it has no
// payload, or everything before an existing payload when re-compiling.
std::size_t executableCopySize(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return 0;
    }
    input.seekg(0, std::ios::end);
    const std::streamoff fileSize = input.tellg();
    if (fileSize < static_cast<std::streamoff>(BUNDLE_TRAILER)) {
        return static_cast<std::size_t>(fileSize);
    }
    input.seekg(-(static_cast<std::streamoff>(BUNDLE_TRAILER)), std::ios::end);
    char trailer[BUNDLE_TRAILER];
    input.read(trailer, static_cast<std::streamsize>(BUNDLE_TRAILER));
    if (input.gcount() != static_cast<std::streamsize>(BUNDLE_TRAILER) ||
        std::memcmp(trailer, BUNDLE_MAGIC, sizeof(BUNDLE_MAGIC)) != 0) {
        return static_cast<std::size_t>(fileSize);
    }
    uint64_t size = 0;
    for (int index = 7; index >= 0; --index) {
        size = (size << 8) |
               static_cast<uint8_t>(trailer[sizeof(BUNDLE_MAGIC) + index]);
    }
    if (size == 0 || size > static_cast<uint64_t>(fileSize) - BUNDLE_TRAILER) {
        return static_cast<std::size_t>(fileSize);
    }
    return static_cast<std::size_t>(fileSize) - BUNDLE_TRAILER -
           static_cast<std::size_t>(size);
}

std::string temporaryDirectory() {
    char pattern[] = "/tmp/lynxer-bundle-XXXXXX";
    if (::mkdtemp(pattern) == nullptr) {
        return "";
    }
    return pattern;
}

// Registered with std::atexit: native libraries stay mapped after their files
// are removed, so the directory can be cleaned up when the process ends.
std::string& cleanupDirectory() {
    static std::string path;
    return path;
}

void removeCleanupDirectory() {
    std::error_code error;
    std::filesystem::remove_all(cleanupDirectory(), error);
}

} // namespace

bool readSelfPayload(std::vector<uint8_t>& payload) {
    const std::string selfPath = executablePath();
    if (selfPath.empty()) {
        return false;
    }
    std::ifstream self(selfPath, std::ios::binary);
    if (!self) {
        return false;
    }
    return readTrailer(self, payload);
}

bool decodeProgramArchive(const std::vector<uint8_t>& payload,
                          ProgramArchive& archive) {
    Reader reader(payload);
    std::uint64_t version = 0;
    if (!reader.readU64(version) || version != BUNDLE_FORMAT_VERSION) {
        return false;
    }
    if (!reader.readBlob(archive.mainPath)) {
        return false;
    }
    if (!reader.readBlob(archive.mainSource)) {
        return false;
    }
    std::uint64_t count = 0;
    if (!reader.readU64(count)) {
        return false;
    }
    for (std::uint64_t index = 0; index < count; ++index) {
        ArchiveModule module;
        if (!reader.readBlob(module.name)) {
            return false;
        }
        if (!reader.readBlob(module.source)) {
            return false;
        }
        if (!reader.readBytes(module.library)) {
            return false;
        }
        if (!reader.readBytes(module.asset)) {
            return false;
        }
        archive.modules.push_back(std::move(module));
    }
    return reader.atEnd();
}

std::vector<uint8_t> makeBundlePayload(const ProgramArchive& archive) {
    std::vector<uint8_t> payload;
    appendU64(payload, BUNDLE_FORMAT_VERSION);
    appendBlob(payload, archive.mainPath);
    appendBlob(payload, archive.mainSource);
    appendU64(payload, archive.modules.size());
    for (const auto& module : archive.modules) {
        appendBlob(payload, module.name);
        appendBlob(payload, module.source);
        appendBytes(payload, module.library);
        appendBytes(payload, module.asset);
    }
    const std::uint64_t bodySize = payload.size();
    payload.insert(payload.end(), BUNDLE_MAGIC,
                   BUNDLE_MAGIC + sizeof(BUNDLE_MAGIC));
    appendU64(payload, bodySize);
    return payload;
}

bool writeBundledExecutable(const std::string& outputPath,
                            const std::vector<uint8_t>& payload,
                            std::string& error) {
    const std::string selfPath = executablePath();
    if (selfPath.empty()) {
        error = "cannot determine the running executable";
        return false;
    }

    // Copy the executable, stripping any payload it already carries so a bundle
    // built from a bundled binary stays valid.
    std::ifstream source(selfPath, std::ios::binary);
    if (!source) {
        error = "cannot read '" + selfPath + "'";
        return false;
    }
    const std::size_t keepBytes = executableCopySize(selfPath);
    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "cannot write '" + outputPath + "'";
        return false;
    }
    char buffer[65536];
    std::size_t written = 0;
    while (written < keepBytes) {
        const std::size_t chunk =
            std::min(sizeof(buffer), keepBytes - written);
        source.read(buffer, static_cast<std::streamsize>(chunk));
        if (source.gcount() <= 0) {
            break;
        }
        output.write(buffer, source.gcount());
        written += static_cast<std::size_t>(source.gcount());
    }
    output.write(reinterpret_cast<const char*>(payload.data()),
                 static_cast<std::streamsize>(payload.size()));
    output.flush();
    if (!output) {
        error = "cannot write '" + outputPath + "'";
        return false;
    }
    output.close();

    if (::chmod(outputPath.c_str(), 0755) != 0) {
        error = "cannot mark '" + outputPath + "' executable";
        return false;
    }
    return true;
}

bool materializeBundle(const std::vector<ArchiveModule>& modules,
                       std::map<std::string, std::string>& libraries,
                       std::map<std::string, std::string>& assets,
                       std::string& error) {
    bool needsDirectory = false;
    for (const auto& module : modules) {
        if (!module.library.empty() || !module.asset.empty()) {
            needsDirectory = true;
            break;
        }
    }
    if (!needsDirectory) {
        return true;
    }

    const std::string directory = temporaryDirectory();
    if (directory.empty()) {
        error = "cannot create a temporary directory for bundled files";
        return false;
    }
    cleanupDirectory() = directory;
    std::atexit(removeCleanupDirectory);

    for (const auto& module : modules) {
        if (module.library.empty() && module.asset.empty()) {
            continue;
        }
        const std::string name =
            std::filesystem::path(module.name).filename().string();
        const std::string path = directory + "/" + name;
        const bool isLibrary = !module.library.empty();
        const std::vector<uint8_t>& bytes =
            isLibrary ? module.library : module.asset;
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "cannot write bundled file '" + name + "'";
            return false;
        }
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        output.close();
        if (isLibrary) {
            if (::chmod(path.c_str(), 0755) != 0) {
                error = "cannot mark native module '" + name + "' loadable";
                return false;
            }
            libraries[name] = path;
            libraries[module.name] = path;
        } else {
            assets[name] = path;
            assets[module.name] = path;
        }
    }
    return true;
}

} // namespace lynxer
