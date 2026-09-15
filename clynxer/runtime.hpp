#pragma once

#include <cstdint>
#include <functional>
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
struct RecordValue;
struct EnumValue;
struct CodeblockValue;
class Statement;
struct Function;

struct CharValue {
    std::string text;

    bool operator==(const CharValue& other) const { return text == other.text; }
};

enum class RecordKind { VarGroup, Struct, Class };

using Value = std::variant<
    std::monostate, std::int64_t, double, bool, std::string,
    std::shared_ptr<List>, std::shared_ptr<Tuple>,
    std::shared_ptr<SentinelValue>, std::shared_ptr<ObjectValue>,
    CharValue, std::shared_ptr<RecordValue>, std::shared_ptr<EnumValue>,
    std::shared_ptr<CodeblockValue>>;

struct RecordField {
    std::string type;
    std::string name;
    Value value;
    bool constant = false;
};

struct RecordValue {
    std::string typeName;  // struct/class name; empty for vargroups
    std::string displayName;
    RecordKind kind = RecordKind::VarGroup;
    std::vector<RecordField> fields;
};

struct EnumValue {
    std::string enumName;
    std::string variantName;
    std::vector<std::string> fieldNames;
    std::vector<Value> payload;
};

struct CodeblockValue {
    std::string name;
    std::vector<std::pair<std::string, std::string>> params;  // type, name
    // The declaration AST owns these statements for the lifetime of the
    // parsed program; codeblocks retain non-owning pointers into that AST.
    std::vector<const Statement*> body;
};

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
    bool constant = false;
};

class Environment {
public:
    using UserFunctionHandler = std::function<Value(
        const std::string&, const std::vector<Value>&,
        const std::vector<std::shared_ptr<CodeblockValue>>&, Environment&, int,
        int)>;

    Environment();

    void pushScope();
    void popScope();

    void declare(const std::string& name, const std::string& type, Value value,
                 int line, int column);

    void declareConstant(const std::string& name, const std::string& type,
                         Value value, int line, int column);

    void assign(const std::string& name, Value value, int line, int column);

    const Value& get(const std::string& name, int line, int column) const;

    bool hasVariable(const std::string& name) const;

    Variable variableSnapshot(const std::string& name) const;

    void setVariableRaw(const std::string& name, const Variable& variable);

    void setVariableRawCurrent(const std::string& name,
                               const Variable& variable);

    void removeVariable(const std::string& name);

    void registerFunction(const std::string& name,
                          std::shared_ptr<void> function);

    std::shared_ptr<void> findFunction(const std::string& name) const;

    void setUserFunctionHandler(UserFunctionHandler handler);

    Value callUserFunction(
        const std::string& name, const std::vector<Value>& arguments,
        const std::vector<std::shared_ptr<CodeblockValue>>& codeblocks, int line,
        int column);

    void setMainOverride(std::string name);

    const std::string& mainOverride() const;

    void setSetupInProgress(bool value);

    bool setupInProgress() const;

    void setForeverWarningSuppressed();

    bool foreverWarningSuppressed() const;

    void setDeprecationWarningSuppressed();

    bool deprecationWarningSuppressed() const;

    void setForeverDelay(double seconds);

    double foreverDelay() const;

    static Value convertForType(Value value, const std::string& type, int line,
                                int column);

private:
    [[noreturn]] static void fail(const std::string& message, int line,
                                  int column);

    std::vector<std::unordered_map<std::string, Variable>> scopes_;
    std::vector<std::unordered_map<std::string, std::shared_ptr<void>>>
        functionScopes_;
    UserFunctionHandler userFunctionHandler_;
    std::string mainOverride_;
    bool setupInProgress_ = false;
    bool foreverWarningSuppressed_ = false;
    bool deprecationWarningSuppressed_ = false;
    double foreverDelaySeconds_ = 0.02;
};

bool isTruthy(const Value& value);

std::string valueToString(const Value& value);

bool isNumber(const Value& value);

double asNumber(const Value& value, int line, int column);

std::string typeNameOf(const Value& value);

bool valuesEqual(const Value& left, const Value& right);

} // namespace clynxer
