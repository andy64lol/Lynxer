#include "ops.hpp"

#include "error.hpp"

#include <cstdint>
#include <variant>

namespace clynxer {

namespace {

void requireNumbers(const std::string& operation, const Value& left,
                    const Value& right, int line, int column) {
    if (!isNumber(left) || !isNumber(right)) {
        throw SourceError(
            "numeric operands required for '" + operation + "'", line, column);
    }
}

bool compareNumbers(const std::string& operation, double left, double right) {
    if (operation == "<") {
        return left < right;
    }
    if (operation == "<=") {
        return left <= right;
    }
    if (operation == ">") {
        return left > right;
    }
    return left >= right;
}

bool compareStrings(const std::string& operation, const std::string& left,
                    const std::string& right) {
    if (operation == "<") {
        return left < right;
    }
    if (operation == "<=") {
        return left <= right;
    }
    if (operation == ">") {
        return left > right;
    }
    return left >= right;
}

} // namespace

BinOp binOpFromString(const std::string& operation, int line, int column) {
    if (operation == "+") return BinOp::Add;
    if (operation == "-") return BinOp::Sub;
    if (operation == "*") return BinOp::Mul;
    if (operation == "/") return BinOp::Div;
    if (operation == "%") return BinOp::Mod;
    if (operation == "==") return BinOp::Eq;
    if (operation == "!=") return BinOp::Neq;
    if (operation == "<") return BinOp::Lt;
    if (operation == "<=") return BinOp::Le;
    if (operation == ">") return BinOp::Gt;
    if (operation == ">=") return BinOp::Ge;
    throw SourceError("unsupported binary operator '" + operation + "'",
                      line, column);
}

Value applyBinary(BinOp op, const Value& left, const Value& right, int line,
                  int column) {
    switch (op) {
    case BinOp::Add:
        if (std::holds_alternative<std::string>(left) ||
            std::holds_alternative<std::string>(right)) {
            return valueToString(left) + valueToString(right);
        }
        requireNumbers("+", left, right, line, column);
        if (std::holds_alternative<std::int64_t>(left) &&
            std::holds_alternative<std::int64_t>(right)) {
            return std::get<std::int64_t>(left) + std::get<std::int64_t>(right);
        }
        return asNumber(left, line, column) + asNumber(right, line, column);

    case BinOp::Sub:
    case BinOp::Mul:
    case BinOp::Div:
        requireNumbers(op == BinOp::Sub ? "-" : op == BinOp::Mul ? "*" : "/",
                       left, right, line, column);
        if (op == BinOp::Div &&
            asNumber(right, line, column) == 0.0) {
            throw SourceError("division by zero", line, column);
        }
        if (op != BinOp::Div &&
            std::holds_alternative<std::int64_t>(left) &&
            std::holds_alternative<std::int64_t>(right)) {
            const auto lhs = std::get<std::int64_t>(left);
            const auto rhs = std::get<std::int64_t>(right);
            return op == BinOp::Sub ? lhs - rhs : lhs * rhs;
        }
        {
            const double lhs = asNumber(left, line, column);
            const double rhs = asNumber(right, line, column);
            if (op == BinOp::Sub) {
                return lhs - rhs;
            }
            if (op == BinOp::Mul) {
                return lhs * rhs;
            }
            return lhs / rhs;
        }

    case BinOp::Mod:
        if (!std::holds_alternative<std::int64_t>(left) ||
            !std::holds_alternative<std::int64_t>(right)) {
            throw SourceError("'%' requires integer operands", line, column);
        }
        {
            const auto rhs = std::get<std::int64_t>(right);
            if (rhs == 0) {
                throw SourceError("division by zero", line, column);
            }
            return std::get<std::int64_t>(left) % rhs;
        }

    case BinOp::Eq:
    case BinOp::Neq: {
        const bool equal = valuesEqual(left, right);
        return op == BinOp::Eq ? equal : !equal;
    }

    case BinOp::Lt:
    case BinOp::Le:
    case BinOp::Gt:
    case BinOp::Ge: {
        const std::string symbol = op == BinOp::Lt  ? "<"
                                   : op == BinOp::Le ? "<="
                                   : op == BinOp::Gt ? ">"
                                                     : ">=";
        if (std::holds_alternative<std::string>(left) &&
            std::holds_alternative<std::string>(right)) {
            return compareStrings(symbol, std::get<std::string>(left),
                                  std::get<std::string>(right));
        }
        requireNumbers(symbol, left, right, line, column);
        return compareNumbers(symbol, asNumber(left, line, column),
                              asNumber(right, line, column));
    }
    }
    throw SourceError("unsupported binary operator", line, column);
}

Value applyUnary(const std::string& operation, const Value& value, int line,
                 int column) {
    if (operation == "!") {
        return !isTruthy(value);
    }
    if (operation == "-") {
        if (!isNumber(value)) {
            throw SourceError("unary '-' requires a number", line, column);
        }
        if (std::holds_alternative<std::int64_t>(value)) {
            return -std::get<std::int64_t>(value);
        }
        return -std::get<double>(value);
    }
    throw SourceError("unsupported unary operator '" + operation + "'", line,
                      column);
}

std::string assembleInterp(const std::vector<std::string>& literals,
                           const std::vector<Value>& values) {
    std::string output;
    for (std::size_t index = 0; index < literals.size(); ++index) {
        output += literals[index];
        if (index < values.size()) {
            output += valueToString(values[index]);
        }
    }
    return output;
}

} // namespace clynxer
