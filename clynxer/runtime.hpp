#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>

namespace clynxer {

using Value = std::variant<std::monostate, std::int64_t, double, bool, std::string>;

struct Variable {
    std::string type;
    Value value;
};

class Environment {
public:
    void declare(const std::string& name, const std::string& type, Value value,
                 int line, int column);

    void assign(const std::string& name, Value value, int line, int column);

    const Value& get(const std::string& name, int line, int column) const;

    void setForeverDelay(double seconds);

    double foreverDelay() const;

    static Value convertForType(Value value, const std::string& type, int line,
                                int column);

private:
    [[noreturn]] static void fail(const std::string& message, int line,
                                  int column);

    std::unordered_map<std::string, Variable> variables_;
    double foreverDelaySeconds_ = 0.02;
};

bool isTruthy(const Value& value);

std::string valueToString(const Value& value);

bool isNumber(const Value& value);

double asNumber(const Value& value, int line, int column);

} // namespace clynxer
