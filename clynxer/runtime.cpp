#include "runtime.hpp"

#include "error.hpp"

#include <charconv>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace clynxer {

void Environment::declare(const std::string& name, const std::string& type,
                          Value value, int line, int column) {
    if (variables_.find(name) != variables_.end()) {
        fail("variable '" + name + "' is already declared", line, column);
    }
    variables_.emplace(name, Variable{type, std::move(value)});
}

void Environment::assign(const std::string& name, Value value, int line,
                         int column) {
    auto found = variables_.find(name);
    if (found == variables_.end()) {
        fail("unknown variable '" + name + "'", line, column);
    }
    found->second.value =
        convertForType(std::move(value), found->second.type, line, column);
}

const Value& Environment::get(const std::string& name, int line,
                              int column) const {
    auto found = variables_.find(name);
    if (found == variables_.end()) {
        fail("unknown variable '" + name + "'", line, column);
    }
    return found->second.value;
}

void Environment::setSetupInProgress(bool value) { setupInProgress_ = value; }

bool Environment::setupInProgress() const { return setupInProgress_; }

void Environment::setForeverWarningSuppressed() {
    foreverWarningSuppressed_ = true;
}

bool Environment::foreverWarningSuppressed() const {
    return foreverWarningSuppressed_;
}

void Environment::setForeverDelay(double seconds) {
    foreverDelaySeconds_ = seconds;
}

double Environment::foreverDelay() const { return foreverDelaySeconds_; }

Value Environment::convertForType(Value value, const std::string& type,
                                  int line, int column) {
    if (type == "any") {
        return value;
    }
    if (type == "int") {
        if (std::holds_alternative<std::int64_t>(value)) {
            return value;
        }
        if (const auto* number = std::get_if<double>(&value);
            number != nullptr && *number == static_cast<std::int64_t>(*number)) {
            return static_cast<std::int64_t>(*number);
        }
    } else if (type == "float") {
        if (std::holds_alternative<std::int64_t>(value)) {
            return static_cast<double>(std::get<std::int64_t>(value));
        }
        if (std::holds_alternative<double>(value)) {
            return value;
        }
    } else if (type == "str" && std::holds_alternative<std::string>(value)) {
        return value;
    } else if (type == "bool" && std::holds_alternative<bool>(value)) {
        return value;
    } else if (type == "list" && std::holds_alternative<std::shared_ptr<List>>(value)) {
        return value;
    } else if (type == "tuple" && std::holds_alternative<std::shared_ptr<Tuple>>(value)) {
        return value;
    } else if (type == "sentinel" &&
               std::holds_alternative<std::shared_ptr<SentinelValue>>(value)) {
        return value;
    } else if (type == "object" &&
               std::holds_alternative<std::shared_ptr<ObjectValue>>(value)) {
        return value;
    }
    fail("value cannot be assigned to type '" + type + "'", line, column);
}

void Environment::fail(const std::string& message, int line, int column) {
    throw SourceError(message, line, column);
}

static std::string formatDouble(double number) {
    if (std::isnan(number)) {
        return "nan";
    }
    if (std::isinf(number)) {
        return number > 0 ? "inf" : "-inf";
    }
    char buffer[64];
    const auto result =
        std::to_chars(buffer, buffer + sizeof(buffer), number);
    return std::string(buffer, result.ptr);
}

std::string valueToString(const Value& value) {
    if (std::holds_alternative<std::monostate>(value)) {
        return "none";
    }
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return std::to_string(*integer);
    }
    if (const auto* number = std::get_if<double>(&value)) {
        return formatDouble(*number);
    }
    if (const auto* boolean = std::get_if<bool>(&value)) {
        return *boolean ? "true" : "false";
    }
    if (const auto* text = std::get_if<std::string>(&value)) {
        return *text;
    }
    if (const auto* list = std::get_if<std::shared_ptr<List>>(&value)) {
        std::string output = "[";
        for (std::size_t index = 0; index < (*list)->elements.size(); ++index) {
            if (index != 0) {
                output += ", ";
            }
            output += valueToString((*list)->elements[index]);
        }
        return output + "]";
    }
    if (const auto* tuple = std::get_if<std::shared_ptr<Tuple>>(&value)) {
        const auto& elements = (*tuple)->elements;
        if (elements.empty()) {
            return "()";
        }
        if (elements.size() == 1) {
            return "(" + valueToString(elements[0]) + ",)";
        }
        std::string output = "(";
        for (std::size_t index = 0; index < elements.size(); ++index) {
            if (index != 0) {
                output += ", ";
            }
            output += valueToString(elements[index]);
        }
        return output + ")";
    }
    if (const auto* sentinel =
            std::get_if<std::shared_ptr<SentinelValue>>(&value)) {
        return (*sentinel)->name.empty() ? "<sentinel>" : (*sentinel)->name;
    }
    if (std::holds_alternative<std::shared_ptr<ObjectValue>>(value)) {
        return "<object>";
    }
    return "<unknown>";
}

bool isTruthy(const Value& value) {
    if (std::holds_alternative<std::monostate>(value)) {
        return false;
    }
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return *integer != 0;
    }
    if (const auto* number = std::get_if<double>(&value)) {
        return *number != 0.0;
    }
    if (const auto* boolean = std::get_if<bool>(&value)) {
        return *boolean;
    }
    if (const auto* text = std::get_if<std::string>(&value)) {
        return !text->empty();
    }
    if (const auto* list = std::get_if<std::shared_ptr<List>>(&value)) {
        return !(*list)->elements.empty();
    }
    if (const auto* tuple = std::get_if<std::shared_ptr<Tuple>>(&value)) {
        return !(*tuple)->elements.empty();
    }
    // Sentinels and objects are always truthy, matching Lynxer.
    return true;
}

std::string typeNameOf(const Value& value) {
    if (std::holds_alternative<std::monostate>(value)) {
        return "none";
    }
    if (std::holds_alternative<std::int64_t>(value)) {
        return "int";
    }
    if (std::holds_alternative<double>(value)) {
        return "float";
    }
    if (std::holds_alternative<bool>(value)) {
        return "bool";
    }
    if (std::holds_alternative<std::string>(value)) {
        return "str";
    }
    if (std::holds_alternative<std::shared_ptr<List>>(value)) {
        return "list";
    }
    if (std::holds_alternative<std::shared_ptr<Tuple>>(value)) {
        return "tuple";
    }
    if (std::holds_alternative<std::shared_ptr<SentinelValue>>(value)) {
        return "sentinel";
    }
    return "object";
}

bool isNumber(const Value& value) {
    return std::holds_alternative<std::int64_t>(value) ||
           std::holds_alternative<double>(value);
}

double asNumber(const Value& value, int line, int column) {
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return static_cast<double>(*integer);
    }
    if (const auto* number = std::get_if<double>(&value)) {
        return *number;
    }
    throw SourceError("numeric value required", line, column);
}

bool valuesEqual(const Value& left, const Value& right) {
    const bool leftNumber = isNumber(left);
    const bool rightNumber = isNumber(right);
    if (leftNumber && rightNumber) {
        return asNumber(left, 0, 0) == asNumber(right, 0, 0);
    }
    if (leftNumber != rightNumber) {
        return false;
    }
    if (left.index() != right.index()) {
        return false;
    }
    if (const auto* leftList = std::get_if<std::shared_ptr<List>>(&left)) {
        const auto& rightList = std::get<std::shared_ptr<List>>(right);
        if (*leftList == rightList) {
            return true;
        }
        if ((*leftList)->elements.size() != rightList->elements.size()) {
            return false;
        }
        for (std::size_t index = 0; index < (*leftList)->elements.size();
             ++index) {
            if (!valuesEqual((*leftList)->elements[index],
                             rightList->elements[index])) {
                return false;
            }
        }
        return true;
    }
    if (const auto* leftTuple = std::get_if<std::shared_ptr<Tuple>>(&left)) {
        const auto& rightTuple = std::get<std::shared_ptr<Tuple>>(right);
        if (*leftTuple == rightTuple) {
            return true;
        }
        if ((*leftTuple)->elements.size() != rightTuple->elements.size()) {
            return false;
        }
        for (std::size_t index = 0; index < (*leftTuple)->elements.size();
             ++index) {
            if (!valuesEqual((*leftTuple)->elements[index],
                             rightTuple->elements[index])) {
                return false;
            }
        }
        return true;
    }
    return left == right;
}

} // namespace clynxer
