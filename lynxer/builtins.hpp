#pragma once

#include "runtime.hpp"

#include <string>
#include <vector>

namespace lynxer {

// True when 'name' (already stripped of a 'global.' prefix) is a known
// built-in, whether it is implemented here or registered as an explicit
// unsupported feature.
bool isBuiltinName(const std::string& name);

// Calls the built-in 'name' with already-evaluated arguments. Unknown names,
// wrong argument shapes, and unported features raise SourceError with the
// call site position.
// Joins any thread a program left running. Called when a program finishes, so a
// worker cannot call back into an environment that is going away.
void joinNativeThreadsAtExit();

Value callBuiltin(const std::string& name, const std::vector<Value>& args,
                  Environment& environment, int line, int column);

// The ownership family (varTransfer, varBorrow, varSwapAll, ...) takes variable
// names rather than evaluated values, so the call site extracts the names and
// dispatches here instead of through callBuiltin.
bool isOwnershipBuiltin(const std::string& name);

Value callOwnershipBuiltin(const std::string& name,
                           const std::vector<std::string>& names,
                           Environment& environment, int line, int column);

} // namespace lynxer
