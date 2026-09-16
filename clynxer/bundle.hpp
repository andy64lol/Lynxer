#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace clynxer {

// One module carried by a compiled executable: Lynxer source for `.lynx`
// modules, or the shared library bytes for native `.so` modules.
struct ArchiveModule {
    std::string name;
    std::string source;
    std::vector<uint8_t> library;
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

// Writes every native module to a private temporary directory and reports a
// module name to filesystem path mapping. The directory is removed at exit.
bool materializeLibraries(const std::vector<ArchiveModule>& modules,
                          std::map<std::string, std::string>& paths,
                          std::string& error);

} // namespace clynxer
