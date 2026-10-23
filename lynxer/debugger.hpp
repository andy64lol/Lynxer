#pragma once

#include "ast.hpp"

#include <string>

namespace lynxer {

void setDebuggerEnabled(bool enabled);
bool debuggerEnabled();
void debugBeforeStatement(const Statement& statement, Environment& environment);
void debugPushFrame(const std::string& name, Environment& environment);
void debugPopFrame();

} // namespace lynxer
