#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace lynxer {

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

// An unsigned 64-bit integer that does not fit in the signed `std::int64_t`
// representation (`18446744073709551615` and friends). It exists so the
// unsigned memory accessors can round-trip their full range; it is a plain
// scalar value otherwise, with no arithmetic beyond numeric coercion.
struct UInt64Value {
    std::uint64_t value = 0;

    bool operator==(const UInt64Value& other) const {
        return value == other.value;
    }
};

enum class RecordKind { VarGroup, Struct, Class };

using Value = std::variant<
    std::monostate, std::int64_t, double, bool, std::string,
    std::shared_ptr<List>, std::shared_ptr<Tuple>,
    std::shared_ptr<SentinelValue>, std::shared_ptr<ObjectValue>,
    CharValue, std::shared_ptr<RecordValue>, std::shared_ptr<EnumValue>,
    std::shared_ptr<CodeblockValue>, UInt64Value>;

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

// Whether a variable still owns its value, or it was moved out by varTransfer.
enum class Ownership { Live, Moved };

struct Variable {
    Variable() = default;
    Variable(std::string declaredType, Value initialValue, bool isConstant)
        : type(std::move(declaredType)), value(std::move(initialValue)),
          constant(isConstant) {}

    std::string type;
    Value value;
    bool constant = false;

    // Ownership / borrowing state. A moved variable may not be read until it is
    // reinitialised by an assignment; a variable with a non-empty borrowSource
    // is an alias of that variable (read-only unless borrowMutable is set).
    Ownership ownership = Ownership::Live;
    std::string borrowSource;
    bool borrowMutable = false;
    std::set<std::string> borrowers;
};

class Environment {
public:
    using UserFunctionHandler = std::function<Value(
        const std::string&, const std::vector<Value>&,
        const std::vector<std::shared_ptr<CodeblockValue>>&, Environment&, int,
        int)>;
    using ModuleFunction = UserFunctionHandler;

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
    std::unordered_map<std::string, Variable> currentVariables() const;

    void setVariableRaw(const std::string& name, const Variable& variable);

    void setVariableRawCurrent(const std::string& name,
                               const Variable& variable);

    void removeVariable(const std::string& name);

    // --- ownership and borrowing -------------------------------------------
    // Every operation returns an empty string on success, or a user-facing
    // error message (mirroring the semantics the Python build had).
    std::string ownershipError(const std::string& name,
                               const std::string& operation) const;
    std::string transfer(const std::string& source,
                         const std::string& destination);
    std::string transferMutate(const std::string& source,
                               const std::string& destination);
    std::string swapAll(const std::string& first, const std::string& second);
    std::string swapValue(const std::string& first, const std::string& second);
    std::string borrow(const std::string& source,
                       const std::string& borrower);
    std::string borrowMutate(const std::string& source,
                             const std::string& borrower);
    std::string endBorrow(const std::string& borrower);
    bool isBorrowing(const std::string& name) const;
    bool isBeingBorrowed(const std::string& name) const;

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

    void setSourceDirectory(std::string directory);
    const std::string& sourceDirectory() const;
    void registerModuleFunction(const std::string& qualifiedName,
                                ModuleFunction function);
    void aliasModuleFunctions(const std::string& from,
                              const std::string& to);
    void retainNativeModule(std::shared_ptr<void> handle);
    bool hasImportedModule(const std::string& name) const;
    void markImportedModule(const std::string& name);
    void registerModuleNamespace(const std::string& name,
                                 std::shared_ptr<RecordValue> namespaceValue);
    std::shared_ptr<RecordValue> moduleNamespace(const std::string& name) const;

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

    // Innermost-out variable lookup; nullptr when the name is unknown.
    Variable* findVariable(const std::string& name);
    const Variable* findVariable(const std::string& name) const;

    // Follows a borrowSource chain to the variable that owns the storage.
    // Returns "" when the name (or its source) is not defined.
    std::string canonicalName(const std::string& name) const;

    std::vector<std::unordered_map<std::string, Variable>> scopes_;
    std::vector<std::unordered_map<std::string, std::shared_ptr<void>>>
        functionScopes_;
    UserFunctionHandler userFunctionHandler_;
    std::unordered_map<std::string, ModuleFunction> moduleFunctions_;
    std::unordered_map<std::string, std::shared_ptr<RecordValue>> modules_;
    std::unordered_set<std::string> importedModules_;
    std::vector<std::shared_ptr<void>> nativeModules_;
    std::string sourceDirectory_;
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

// Copy for transfer / borrow-end: list, tuple and enum get a fresh outer
// object (their elements stay shared); records, objects, sentinels and
// codeblocks are shared as-is.
Value copyForOwnership(const Value& value);

bool valuesEqual(const Value& left, const Value& right);

// Files carried by a compiled executable and materialized into its private
// temporary directory at startup. `setBundledAssets` is called once by the
// bundle runner; the accessors back the `bundledFile()`/`bundledFiles()`
// builtins and return nothing when the program is not bundled.
void setBundledAssets(std::map<std::string, std::string> assets);

std::string bundledAssetPath(const std::string& name);

std::vector<std::string> bundledAssetNames();

} // namespace lynxer
