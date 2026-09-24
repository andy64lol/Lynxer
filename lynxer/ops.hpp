#pragma once

#include "runtime.hpp"

#include <string>
#include <vector>

namespace lynxer {

// Shared operator semantics used by BOTH the AST interpreter (ast.cpp) and
// the bytecode VM (vm.cpp). Keeping one implementation prevents drift: a
// change here updates every execution path at once.

enum class BinOp {
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Eq,
    Neq,
    Lt,
    Le,
    Gt,
    Ge,
    BitAnd,
    BitOr,
    BitXor,
    BitNand,
    BitXnor,
    BitNor,
    Shl,
    Shr,
    Exp,
    FloorDiv,
    LogicNand,
    LogicNor,
    LogicXor,
    LogicXnor,
};

// Maps a source-level operator symbol to its enum. Unknown symbols raise
// "unsupported binary operator" exactly like BinaryExpression used to.
BinOp binOpFromString(const std::string& operation, int line, int column);

// Applies a non-short-circuit binary operator. Short-circuit && and || are
// handled by their callers (AST evaluation order / compiled jumps).
Value applyBinary(BinOp op, const Value& left, const Value& right, int line,
                  int column);

// Applies unary '-' or '!'.
Value applyUnary(const std::string& operation, const Value& value, int line,
                 int column);

// Assembles an interpolated string: literals has one more entry than values,
// mirroring InterpStringExpression's parallel vectors.
std::string assembleInterp(const std::vector<std::string>& literals,
                           const std::vector<Value>& values);

} // namespace lynxer
