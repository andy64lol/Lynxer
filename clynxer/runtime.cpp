#include "runtime.hpp"

#include "error.hpp"
#include "types.hpp"

#include <charconv>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace clynxer {

void Environment::declare(const std::string& name, const std::string& type,
                          Value value, int line, int column) {
    // Re-declaration replaces both the value and the recorded type.
    variables_[name] = Variable{type, std::move(value), false};
}

void Environment::declareConstant(const std::string& name,
                                  const std::string& type, Value value,
                                  int line, int column) {
    variables_[name] =
        Variable{type, convertForType(std::move(value), type, line, column),
                 true};
}

void Environment::assign(const std::string& name, Value value, int line,
                         int column) {
    auto found = variables_.find(name);
    if (found == variables_.end()) {
        fail("unknown variable '" + name + "'", line, column);
    }
    if (found->second.constant) {
        fail("variable '" + name + "' is constant and cannot be reassigned",
             line, column);
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

bool Environment::hasVariable(const std::string& name) const {
    return variables_.find(name) != variables_.end();
}

Variable Environment::variableSnapshot(const std::string& name) const {
    return variables_.at(name);
}

void Environment::setVariableRaw(const std::string& name,
                                 const Variable& variable) {
    variables_[name] = variable;
}

void Environment::removeVariable(const std::string& name) {
    variables_.erase(name);
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

namespace {

// Documented fixed-width ranges (docs/types.md).
bool integerValueInRange(const std::string& type, std::int64_t number) {
    if (type == "numBool" || type == "bit") {
        return number == 0 || number == 1;
    }
    if (type == "byte" || type == "uint8") {
        return number >= 0 && number <= 255;
    }
    if (type == "uint16") {
        return number >= 0 && number <= 65535;
    }
    if (type == "uint32") {
        return number >= 0 && number <= 4294967295LL;
    }
    if (type == "uint64") {
        return number >= 0;
    }
    if (type == "int8") {
        return number >= -128 && number <= 127;
    }
    if (type == "int16") {
        return number >= -32768 && number <= 32767;
    }
    if (type == "int32") {
        return number >= -2147483648LL && number <= 2147483647LL;
    }
    return true;  // int64 covers the whole host range
}

} // namespace

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
    } else if (type == "num") {
        // Accepts int or float freely; the stored kind is untouched.
        if (std::holds_alternative<std::int64_t>(value) ||
            std::holds_alternative<double>(value)) {
            return value;
        }
    } else if (type == "str" && std::holds_alternative<std::string>(value)) {
        return value;
    } else if (type == "bool" && std::holds_alternative<bool>(value)) {
        return value;
    } else if (type == "char") {
        if (std::holds_alternative<CharValue>(value)) {
            return value;
        }
        if (const auto* text = std::get_if<std::string>(&value);
            text != nullptr && text->size() == 1) {
            return CharValue{*text};
        }
        if (std::holds_alternative<std::string>(value)) {
            fail("string length " +
                     std::to_string(std::get<std::string>(value).size()) +
                     " is not a char",
                 line, column);
        }
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
    } else if (type == "codeblock" &&
               std::holds_alternative<std::shared_ptr<CodeblockValue>>(value)) {
        return value;
    } else if (type == "numBool" || type == "bit" || type == "byte" ||
               type == "uint8" || type == "uint16" || type == "uint32" ||
               type == "uint64" || type == "int8" || type == "int16" ||
               type == "int32" || type == "int64") {
        if (const auto* integer = std::get_if<std::int64_t>(&value);
            integer != nullptr && integerValueInRange(type, *integer)) {
            return value;
        }
        fail(integerValueInRange(type, 0) &&
                     std::holds_alternative<double>(value)
                 ? "value cannot be assigned to type '" + type + "'"
                 : "value " + valueToString(value) +
                       " is out of range for type '" + type + "'",
             line, column);
    } else if (type == "float32" || type == "float64") {
        if (std::holds_alternative<std::int64_t>(value)) {
            return value;
        }
        if (const auto* number = std::get_if<double>(&value);
            number != nullptr && std::isfinite(*number) &&
            std::abs(*number) <= (type == "float32"
                                      ? 3.4028234663852886e38
                                      : 1.7976931348623157e308)) {
            return value;
        }
        fail("value " + valueToString(value) + " is out of range for type '" +
                 type + "'",
             line, column);
    } else if (TypeRegistry::instance().hasNamedType(type)) {
        if (const auto* record = std::get_if<std::shared_ptr<RecordValue>>(&value);
            record != nullptr && *record != nullptr &&
            (*record)->typeName == type &&
            ((*record)->kind == RecordKind::Struct) ==
                (TypeRegistry::instance().findStruct(type) != nullptr)) {
            return value;
        }
        if (const auto* enumValue = std::get_if<std::shared_ptr<EnumValue>>(&value);
            enumValue != nullptr && *enumValue != nullptr &&
            (*enumValue)->enumName == type) {
            return value;
        }
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
    if (const auto* character = std::get_if<CharValue>(&value)) {
        return character->text;
    }
    if (const auto* record = std::get_if<std::shared_ptr<RecordValue>>(&value)) {
        std::string parts;
        for (std::size_t index = 0; index < (*record)->fields.size(); ++index) {
            const RecordField& field = (*record)->fields[index];
            if (index != 0) {
                parts += ", ";
            }
            parts += field.type + " " + field.name + " = " +
                     valueToString(field.value);
        }
        if ((*record)->kind == RecordKind::VarGroup) {
            return "<vargroup " + (*record)->displayName + ">";
        }
        return "<" + (*record)->typeName +
               ((*record)->kind == RecordKind::Struct ? " struct" : " instance") +
               (parts.empty() ? "" : " fields=[" + parts + "]") + ">";
    }
    if (const auto* enumValue = std::get_if<std::shared_ptr<EnumValue>>(&value)) {
        if ((*enumValue)->payload.empty()) {
            return (*enumValue)->enumName + "." + (*enumValue)->variantName;
        }
        std::string parts;
        for (std::size_t index = 0; index < (*enumValue)->payload.size();
             ++index) {
            if (index != 0) {
                parts += ", ";
            }
            parts += valueToString((*enumValue)->payload[index]);
        }
        return (*enumValue)->enumName + "." + (*enumValue)->variantName + "(" +
               parts + ")";
    }
    if (std::holds_alternative<std::shared_ptr<CodeblockValue>>(value)) {
        return "<code block>";
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
    // Sentinels, objects, chars, records, enums, and codeblocks are always
    // truthy, matching Lynxer.
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
    if (std::holds_alternative<std::shared_ptr<ObjectValue>>(value)) {
        return "object";
    }
    if (const auto* character = std::get_if<CharValue>(&value)) {
        (void)character;
        return "char";
    }
    if (const auto* record = std::get_if<std::shared_ptr<RecordValue>>(&value)) {
        return (*record)->kind == RecordKind::VarGroup
                   ? "vargroup"
                   : (*record)->typeName;
    }
    if (const auto* enumValue = std::get_if<std::shared_ptr<EnumValue>>(&value)) {
        return (*enumValue)->enumName;
    }
    if (std::holds_alternative<std::shared_ptr<CodeblockValue>>(value)) {
        return "codeblock";
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
    if (const auto* leftChar = std::get_if<CharValue>(&left)) {
        return leftChar->text == std::get<CharValue>(right).text;
    }
    return left == right;
}

} // namespace clynxer
