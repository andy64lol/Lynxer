#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace clynxer {

struct SourceError : std::runtime_error {
    SourceError(std::string message, int line, int column)
        : std::runtime_error(std::move(message)), line(line), column(column) {}

    int line;
    int column;
};

} // namespace clynxer
