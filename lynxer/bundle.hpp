#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace lynxer {

// One file carried by a compiled executable: Lynxer source for a `.lynx`
// module, shared library bytes for a native `.so` module, or raw bytes for an
// included data file.
struct ArchiveModule {
    std::string name;
    std::string source;
    std::vector<uint8_t> library;
    std::vector<uint8_t> asset;
};

// A program together with every module it needs to run.
struct ProgramArchive {
    std::string mainPath;
    std::string mainSource;
    std::vector<ArchiveModule> modules;
};

// Reads the payload appended to the running executable, if there is one.
bool readSelfPayload(std::vector<uint8_t>& payload);

// Decodes a payload produced by makeBundlePayload.
bool decodeProgramArchive(const std::vector<uint8_t>& payload,
                          ProgramArchive& archive);

// Serializes an archive into a payload, including the trailing magic and size.
std::vector<uint8_t> makeBundlePayload(const ProgramArchive& archive);

// Copies the running executable and appends `payload` to it.
bool writeBundledExecutable(const std::string& outputPath,
                            const std::vector<uint8_t>& payload,
                            std::string& error);

// Writes every native module and included data file to a private temporary
// directory and reports `name -> filesystem path` mappings. The directory is
// removed when the process exits.
bool materializeBundle(const std::vector<ArchiveModule>& modules,
                       std::map<std::string, std::string>& libraries,
                       std::map<std::string, std::string>& assets,
                       std::string& error);

} // namespace lynxer
