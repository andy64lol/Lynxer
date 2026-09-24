#include "ops.hpp"

#include "error.hpp"

#include <cmath>
#include <cstdint>
#include <variant>

namespace lynxer {

namespace {

void requireNumbers(const std::string& operation, const Value& left,
                    const Value& right, int line, int column) {
    if (!isNumber(left) || !isNumber(right)) {
        throw SourceError(
            "numeric operands required for '" + operation + "'", line, column);
    }
}

void requireIntegers(const std::string& operation, const Value& left,
                     const Value& right, int line, int column) {
    if (!std::holds_alternative<std::int64_t>(left) ||
        !std::holds_alternative<std::int64_t>(right)) {
        throw SourceError("'" + operation + "' requires integer operands",
                          line, column);
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

// Exact floor division toward negative infinity (C++ integer division
// truncates toward zero, which would be wrong for negatives).
std::int64_t floorDivide(std::int64_t lhs, std::int64_t rhs) {
    std::int64_t quotient = lhs / rhs;
    std::int64_t remainder = lhs % rhs;
    if (remainder != 0 && ((rhs > 0) != (remainder > 0))) {
        --quotient;
    }
    return quotient;
}

std::int64_t integerPower(std::int64_t base, std::int64_t exponent) {
    std::int64_t result = 1;
    const bool negative = exponent < 0;
    std::uint64_t remaining =
        static_cast<std::uint64_t>(negative ? -exponent : exponent);
    std::uint64_t value = static_cast<std::uint64_t>(base);
    while (remaining > 0) {
        if (remaining & 1) {
            result *= static_cast<std::int64_t>(value);
        }
        value *= value;
        remaining >>= 1;
    }
    return negative ? (result == 0 ? 0 : 1 / result) : result;
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
    if (operation == "&") return BinOp::BitAnd;
    if (operation == "|") return BinOp::BitOr;
    if (operation == "^") return BinOp::BitXor;
    if (operation == "!&") return BinOp::BitNand;
    if (operation == "!^") return BinOp::BitXnor;
    if (operation == "!|") return BinOp::BitNor;
    if (operation == "<<") return BinOp::Shl;
    if (operation == ">>") return BinOp::Shr;
    if (operation == "**") return BinOp::Exp;
    if (operation == "/%") return BinOp::FloorDiv;
    if (operation == "!&&") return BinOp::LogicNand;
    if (operation == "!||") return BinOp::LogicNor;
    // Keyword operators. These are the preferred spellings; the symbolic forms
    // above are kept working so that existing sources still run. `and`/`or` are
    // absent here on purpose: they short-circuit in BinaryExpression::evaluate
    // and never reach this table.
    if (operation == "is") return BinOp::Eq;
    if (operation == "isnt") return BinOp::Neq;
    if (operation == "nand") return BinOp::LogicNand;
    if (operation == "nor") return BinOp::LogicNor;
    if (operation == "xor") return BinOp::LogicXor;
    if (operation == "xnor") return BinOp::LogicXnor;
    if (operation == "bitand") return BinOp::BitAnd;
    if (operation == "bitor") return BinOp::BitOr;
    if (operation == "bitxor") return BinOp::BitXor;
    if (operation == "bitnand") return BinOp::BitNand;
    if (operation == "bitnor") return BinOp::BitNor;
    if (operation == "bitxnor") return BinOp::BitXnor;
    if (operation == "bitleft") return BinOp::Shl;
    if (operation == "bitright") return BinOp::Shr;
    if (operation == "not is") return BinOp::Neq;
    throw SourceError("unsupported binary operator '" + operation + "'",
                      line, column);
}

Value applyBinary(BinOp op, const Value& left, const Value& right, int line,
                  int column) {
    switch (op) {
    case BinOp::Add:
        if (std::holds_alternative<std::string>(left) ||
            std::holds_alternative<std::string>(right) ||
            std::holds_alternative<CharValue>(left) ||
            std::holds_alternative<CharValue>(right)) {
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

    case BinOp::BitAnd:
    case BinOp::BitOr:
    case BinOp::BitXor: {
        requireIntegers(op == BinOp::BitAnd ? "&" : op == BinOp::BitOr ? "|" : "^",
                        left, right, line, column);
        const auto lhs = std::get<std::int64_t>(left);
        const auto rhs = std::get<std::int64_t>(right);
        if (op == BinOp::BitAnd) return lhs & rhs;
        if (op == BinOp::BitOr) return lhs | rhs;
        return lhs ^ rhs;
    }

    case BinOp::BitNand:
    case BinOp::BitXnor:
    case BinOp::BitNor: {
        requireIntegers(op == BinOp::BitNand ? "!&" : op == BinOp::BitXnor
                        ? "!^" : "!|", left, right, line, column);
        const auto lhs = std::get<std::int64_t>(left);
        const auto rhs = std::get<std::int64_t>(right);
        if (op == BinOp::BitNand) return ~(lhs & rhs);
        if (op == BinOp::BitXnor) return ~(lhs ^ rhs);
        return ~(lhs | rhs);
    }

    case BinOp::Shl:
    case BinOp::Shr: {
        requireIntegers(op == BinOp::Shl ? "<<" : ">>", left, right, line,
                        column);
        const auto lhs = std::get<std::int64_t>(left);
        const auto rhs = std::get<std::int64_t>(right);
        if (rhs < 0 || rhs >= 64) {
            throw SourceError("shift count must be in range 0..63", line,
                              column);
        }
        if (op == BinOp::Shl) return lhs << rhs;
        return lhs >> rhs;
    }

    case BinOp::Exp: {
        requireNumbers("**", left, right, line, column);
        if (std::holds_alternative<std::int64_t>(left) &&
            std::holds_alternative<std::int64_t>(right)) {
            const auto lhs = std::get<std::int64_t>(left);
            const auto rhs = std::get<std::int64_t>(right);
            if (rhs < 0) {
                return std::pow(static_cast<double>(lhs),
                                static_cast<double>(rhs));
            }
            return integerPower(lhs, rhs);
        }
        return std::pow(asNumber(left, line, column),
                        asNumber(right, line, column));
    }

    case BinOp::FloorDiv:
        requireIntegers("/%", left, right, line, column);
        {
            const auto lhs = std::get<std::int64_t>(left);
            const auto rhs = std::get<std::int64_t>(right);
            if (rhs == 0) {
                throw SourceError("division by zero", line, column);
            }
            return floorDivide(lhs, rhs);
        }

    case BinOp::LogicNand:
    case BinOp::LogicNor:
    case BinOp::LogicXor:
    case BinOp::LogicXnor: {
        const bool leftTruthy = isTruthy(left);
        const bool rightTruthy = isTruthy(right);
        switch (op) {
        case BinOp::LogicNand:
            return !(leftTruthy && rightTruthy);
        case BinOp::LogicNor:
            return !(leftTruthy || rightTruthy);
        case BinOp::LogicXor:
            return leftTruthy != rightTruthy;
        default:
            return leftTruthy == rightTruthy;
        }
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
    if (operation == "!!" || operation == "not") {
        return !isTruthy(value);
    }
    if (operation == "~" || operation == "bitnot") {
        if (!std::holds_alternative<std::int64_t>(value)) {
            throw SourceError("'" + operation +
                                  "' requires an integer operand",
                              line, column);
        }
        return ~std::get<std::int64_t>(value);
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

} // namespace lynxer
