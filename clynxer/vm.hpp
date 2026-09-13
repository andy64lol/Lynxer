#pragma once

#include "compiler.hpp"
#include "runtime.hpp"

namespace clynxer {

// Executes a compiled program: setup then main, matching the AST path's
// setup-in-progress handling. Throws SourceError with source-located
// positions taken from the section trap tables.
void runProgram(const CompiledProgram& program, Environment& environment);

} // namespace clynxer
