#pragma once

#include "runtime.hpp"

#include <string>
#include <vector>

// The `winAPI.*` surface: Win32 calls a program can make after selecting the
// Windows target with `syscalls("winAPI", "<arch>")` (the operating-system
// keyword also accepts `windows` and `win32`). Names are the camelCase Win32
// forms: `winAPI.getProcessId()`, `winAPI.getCurrentDirectory()`, ...
//
// The Win32 calls live here so no other translation unit includes <windows.h>,
// matching the host-abstraction rule. On a non-Windows host `available()` is
// false and every call is refused.
namespace lynxer::winapi {

// True on a Windows host; false elsewhere.
bool available();

// Every supported operation name, in help/error order, for "You meant: x?".
const std::vector<std::string>& supportedNames();

// Calls `name` with `args`. Returns false and fills `error` when the arguments
// are wrong or the Win32 call fails. Unknown names are never passed here: the
// caller checks `supportedNames()` first so it can suggest a correction.
bool call(const std::string& name, const std::vector<Value>& args, Value& result,
          std::string& error);

} // namespace lynxer::winapi
