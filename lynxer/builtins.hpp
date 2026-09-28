#pragma once

#include "runtime.hpp"

#include <string>
#include <vector>

namespace lynxer {

// True when 'name' (already stripped of a 'global.' prefix) is a known
// built-in, whether it is implemented here or registered as an explicit
// unsupported feature.
bool isBuiltinName(const std::string& name);

// Joins any thread a program left running. Called when a program finishes, so a
// worker cannot call back into an environment that is going away.
void joinNativeThreadsAtExit();

// Joins any async task a program left running. Called when a program finishes,
// for the same reason as `joinNativeThreadsAtExit`.
void joinAsyncTasksAtExit();

// Resolves an `await`: joins a task handle produced by `asyncRun` and returns
// its value, raising the task's failure if it failed; other values pass through.
Value awaitValue(const Value& value, int line, int column);

// Calls the built-in 'name' with already-evaluated arguments. Unknown names,
// wrong argument shapes, and unported features raise SourceError with the
// call site position.
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
