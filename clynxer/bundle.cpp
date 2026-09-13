#include "bundle.hpp"

#include "config.hpp"

#include <cstring>
#include <fstream>
#include <sys/stat.h>

namespace clynxer {

namespace {

// Trailer appended to a bundled executable:
//   ... executable bytes ... | magic (10) | u64le payload size | payload
inline constexpr char BUNDLE_MAGIC[10] = {'C', 'L', 'Y', 'X', 'P',
                                          'A', 'Y', 'L', 'D', '\0'};
inline constexpr std::size_t BUNDLE_TRAILER = sizeof(BUNDLE_MAGIC) + 8;

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
// payload, or everything before an existing payload when re-bundling.
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

std::vector<uint8_t> makeBundlePayload(const std::vector<uint8_t>& bytecode) {
    std::vector<uint8_t> payload;
    payload.reserve(bytecode.size() + BUNDLE_TRAILER);
    payload.insert(payload.end(), bytecode.begin(), bytecode.end());
    payload.insert(payload.end(), BUNDLE_MAGIC,
                   BUNDLE_MAGIC + sizeof(BUNDLE_MAGIC));
    const uint64_t size = bytecode.size();
    for (int index = 0; index < 8; ++index) {
        payload.push_back(static_cast<uint8_t>((size >> (8 * index)) & 0xFF));
    }
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

    // Copy the executable, stripping any payload it already carries so a
    // bundle built from a bundled binary stays valid.
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

} // namespace clynxer
