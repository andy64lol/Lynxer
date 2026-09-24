#pragma once

#include <string>

namespace clynxer {

// Reformats Lynxer source without changing its tokens: canonical spacing,
// four-space indentation, comments preserved. With `oneline`, everything is
// collapsed onto a single physical line and `//` comments become `///...///`.
//
// Throws SourceError when the source does not lex or parse. The parser is run
// only to validate, so entry points (`setup`/`main`) are not required.
std::string formatSource(const std::string& source, const std::string& display,
                         bool oneline);

} // namespace clynxer
