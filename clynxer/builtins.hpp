#pragma once

#include "runtime.hpp"

#include <string>
#include <vector>

namespace clynxer {

// True when 'name' (already stripped of a 'global.' prefix) is a known
// built-in, whether it is implemented here or registered as an explicit
// unsupported feature.
bool isBuiltinName(const std::string& name);

// Calls the built-in 'name' with already-evaluated arguments. Unknown names,
// wrong argument shapes, and unported features raise SourceError with the
// call site position.
Value callBuiltin(const std::string& name, const std::vector<Value>& args,
                  Environment& environment, int line, int column);

} // namespace clynxer
