#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace clynxer {

struct List;
struct Tuple;
struct SentinelValue;
struct ObjectValue;

using Value = std::variant<
    std::monostate, std::int64_t, double, bool, std::string,
    std::shared_ptr<List>, std::shared_ptr<Tuple>,
    std::shared_ptr<SentinelValue>, std::shared_ptr<ObjectValue>>;

struct List {
    std::vector<Value> elements;
};

struct Tuple {
    std::vector<Value> elements;
};

struct SentinelValue {
    std::string name;
};

struct ObjectValue {
    std::uint64_t id = 0;
};

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

    void setSetupInProgress(bool value);

    bool setupInProgress() const;

    void setForeverWarningSuppressed();

    bool foreverWarningSuppressed() const;

    void setForeverDelay(double seconds);

    double foreverDelay() const;

    static Value convertForType(Value value, const std::string& type, int line,
                                int column);

private:
    [[noreturn]] static void fail(const std::string& message, int line,
                                  int column);

    std::unordered_map<std::string, Variable> variables_;
    bool setupInProgress_ = false;
    bool foreverWarningSuppressed_ = false;
    double foreverDelaySeconds_ = 0.02;
};

bool isTruthy(const Value& value);

std::string valueToString(const Value& value);

bool isNumber(const Value& value);

double asNumber(const Value& value, int line, int column);

std::string typeNameOf(const Value& value);

bool valuesEqual(const Value& left, const Value& right);

} // namespace clynxer
