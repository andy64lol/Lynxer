#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace lynxer {

struct SourceError : std::runtime_error {
    SourceError(std::string message, int line, int column)
        : std::runtime_error(std::move(message)), line(line), column(column) {}

    SourceError(std::string message, int line, int column, std::string source)
        : std::runtime_error(std::move(message)), line(line), column(column),
          source(std::move(source)) {}

    int line;
    int column;
    // The file the line/column refer to. Empty for the program the user ran;
    // set to the module path when the failure happened inside an imported
    // module, so the diagnostic is not misattributed to the importing program.
    std::string source;
};

} // namespace lynxer
