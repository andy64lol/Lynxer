#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace clynxer {

// Single-file bundling: a copy of the clynxer executable carries an appended
// bytecode payload. Detection reads the executable's own trailer, so a
// bundled program runs directly without arguments.

// Bytecode payload of the running executable, or false when it is a plain
// (unbundled) clynxer binary.
bool readSelfPayload(std::vector<uint8_t>& payload);

// Wraps serialized bytecode in the bundle trailer.
std::vector<uint8_t> makeBundlePayload(const std::vector<uint8_t>& bytecode);

// Copies the running executable to 'outputPath' (stripping any payload it
// already carries), appends 'payload', and marks the result executable.
// Returns false and fills 'error' when writing fails.
bool writeBundledExecutable(const std::string& outputPath,
                            const std::vector<uint8_t>& payload,
                            std::string& error);

} // namespace clynxer
