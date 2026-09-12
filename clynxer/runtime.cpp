#include "runtime.hpp"

#include "error.hpp"

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
    }
    fail("value cannot be assigned to type '" + type + "'", line, column);
}

void Environment::fail(const std::string& message, int line, int column) {
    throw SourceError(message, line, column);
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
    return !std::get<std::string>(value).empty();
}

std::string valueToString(const Value& value) {
    if (std::holds_alternative<std::monostate>(value)) {
        return "none";
    }
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return std::to_string(*integer);
    }
    if (const auto* number = std::get_if<double>(&value)) {
        std::ostringstream output;
        output << std::setprecision(15) << *number;
        return output.str();
    }
    if (const auto* boolean = std::get_if<bool>(&value)) {
        return *boolean ? "true" : "false";
    }
    return std::get<std::string>(value);
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

} // namespace clynxer
