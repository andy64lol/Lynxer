#include "runtime.hpp"

#include "error.hpp"
#include "types.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>

namespace lynxer {

Environment::Environment() : scopes_(1), functionScopes_(1) {}

void Environment::pushScope() {
    scopes_.emplace_back();
    functionScopes_.emplace_back();
}

void Environment::popScope() {
    if (scopes_.size() <= 1) {
        throw SourceError("cannot pop the global scope", 0, 0);
    }
    scopes_.pop_back();
    functionScopes_.pop_back();
}

void Environment::declare(const std::string& name, const std::string& type,
                          Value value, int line, int column) {
    // Re-declaration replaces both the value and the recorded type.
    scopes_.back()[name] = Variable{type, std::move(value), false};
}

void Environment::declareConstant(const std::string& name,
                                  const std::string& type, Value value,
                                  int line, int column) {
    scopes_.back()[name] =
        Variable{type, convertForType(std::move(value), type, line, column),
                 true};
}

Variable* Environment::findVariable(const std::string& name) {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        auto found = scope->find(name);
        if (found != scope->end()) {
            return &found->second;
        }
    }
    return nullptr;
}

const Variable* Environment::findVariable(const std::string& name) const {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        auto found = scope->find(name);
        if (found != scope->end()) {
            return &found->second;
        }
    }
    return nullptr;
}

std::string Environment::canonicalName(const std::string& name) const {
    std::string current = name;
    std::set<std::string> seen;
    while (true) {
        const Variable* variable = findVariable(current);
        if (variable == nullptr) {
            return "";
        }
        if (variable->borrowSource.empty()) {
            return current;
        }
        if (!seen.insert(current).second) {
            return "";
        }
        current = variable->borrowSource;
    }
}

void Environment::assign(const std::string& name, Value value, int line,
                         int column) {
    Variable* variable = findVariable(name);
    if (variable == nullptr) {
        fail("unknown variable '" + name + "'", line, column);
    }

    // Writing through a borrow goes to the variable that owns the storage.
    if (!variable->borrowSource.empty()) {
        if (!variable->borrowMutable) {
            fail("Cannot write to borrowed variable '" + name +
                     "'; the borrow is read-only",
                 line, column);
        }
        const std::string canonical = canonicalName(name);
        Variable* source = canonical.empty() ? nullptr : findVariable(canonical);
        if (source == nullptr) {
            fail("unknown variable '" + name + "'", line, column);
        }
        source->value =
            convertForType(std::move(value), source->type, line, column);
        source->ownership = Ownership::Live;
        return;
    }

    if (variable->constant) {
        fail("variable '" + name + "' is constant and cannot be reassigned",
             line, column);
    }
    const std::string error = ownershipError(name, "write to");
    if (!error.empty()) {
        fail(error, line, column);
    }
    variable->value =
        convertForType(std::move(value), variable->type, line, column);
    // A plain assignment reinitialises a moved variable.
    variable->ownership = Ownership::Live;
}

const Value& Environment::get(const std::string& name, int line,
                              int column) const {
    const std::string canonical = canonicalName(name);
    const Variable* variable =
        canonical.empty() ? nullptr : findVariable(canonical);
    if (variable == nullptr) {
        fail("unknown variable '" + name + "'", line, column);
    }
    return variable->value;
}

bool Environment::hasVariable(const std::string& name) const {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        if (scope->find(name) != scope->end()) {
            return true;
        }
    }
    return false;
}

Variable Environment::variableSnapshot(const std::string& name) const {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        auto found = scope->find(name);
        if (found != scope->end()) {
            return found->second;
        }
    }
    throw std::out_of_range("unknown variable");
}

std::unordered_map<std::string, Variable> Environment::currentVariables() const {
    return scopes_.back();
}

void Environment::setVariableRaw(const std::string& name,
                                 const Variable& variable) {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        auto found = scope->find(name);
        if (found != scope->end()) {
            found->second = variable;
            return;
        }
    }
    scopes_.back()[name] = variable;
}

void Environment::setVariableRawCurrent(const std::string& name,
                                        const Variable& variable) {
    scopes_.back()[name] = variable;
}

void Environment::removeVariable(const std::string& name) {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        if (scope->erase(name) != 0) {
            return;
        }
    }
}

std::string Environment::ownershipError(const std::string& name,
                                        const std::string& operation) const {
    const Variable* variable = findVariable(name);
    if (variable == nullptr) {
        return "'" + name + "' is not defined";
    }
    if (!variable->borrowSource.empty()) {
        // The name is a borrower, so its state is "borrowed".
        if (operation == "transfer into" || operation == "borrow into") {
            return "Cannot " + operation + " borrowed variable '" + name +
                   "'; end its borrow first";
        }
        if (operation == "write to" && !variable->borrowMutable) {
            return "Cannot write to borrowed variable '" + name +
                   "'; the borrow is read-only";
        }
        if ((operation == "write to" || operation == "move") &&
            !variable->borrowMutable) {
            return "Cannot " + operation + " '" + name +
                   "' while it is being borrowed; end all active borrows first";
        }
        return "";
    }
    if (variable->ownership == Ownership::Moved) {
        if (operation == "write to") {
            return "";
        }
        const std::string action =
            operation == "move" ? std::string("move from") : operation;
        return "Cannot " + action + " moved variable '" + name +
               "'; reinitialize it before using it again";
    }
    if ((operation == "write to" || operation == "move") &&
        !variable->borrowers.empty()) {
        return "Cannot " + operation + " '" + name +
               "' while it is being borrowed; end all active borrows first";
    }
    return "";
}

std::string Environment::transfer(const std::string& source,
                                  const std::string& destination) {
    const std::string canonical = canonicalName(source);
    if (canonical.empty()) {
        return "source variable is not defined";
    }
    Variable* destinationVariable = findVariable(destination);
    if (destinationVariable == nullptr) {
        return "destination variable is not defined";
    }
    if (canonical == destination) {
        return "a variable cannot be transferred to itself";
    }
    if (!destinationVariable->borrowSource.empty()) {
        return "Cannot transfer into shared variable '" + destination +
               "'; use an independent destination";
    }
    const std::string sourceError = ownershipError(source, "move");
    if (!sourceError.empty()) {
        return sourceError;
    }
    Variable* sourceVariable = findVariable(canonical);
    if (sourceVariable->constant) {
        return "Cannot transfer constant '" + source + "'";
    }
    if (destinationVariable->constant) {
        return "Cannot transfer into constant '" + destination + "'";
    }
    if (!typeMatches(destinationVariable->type, sourceVariable->value)) {
        return "Type mismatch: '" + destination + "' is declared as '" +
               destinationVariable->type + "' but got a '" +
               typeNameOf(sourceVariable->value) + "' value";
    }
    destinationVariable->value = copyForOwnership(sourceVariable->value);
    destinationVariable->ownership = Ownership::Live;
    sourceVariable->ownership = Ownership::Moved;
    return "";
}

std::string Environment::transferMutate(const std::string& source,
                                        const std::string& destination) {
    const std::string canonical = canonicalName(source);
    if (canonical.empty()) {
        return "Cannot transfer from an undefined source variable";
    }
    Variable* destinationVariable = findVariable(destination);
    if (destinationVariable == nullptr) {
        return "Cannot transfer into an undefined destination variable";
    }
    if (canonical == destination) {
        return "a variable cannot be transferred to itself";
    }
    const std::string sourceError = ownershipError(source, "move");
    if (!sourceError.empty()) {
        return sourceError;
    }
    Variable* sourceVariable = findVariable(canonical);
    if (sourceVariable->constant) {
        return "Cannot transfer constant '" + source + "'";
    }
    if (destinationVariable->constant) {
        return "Cannot transfer into constant '" + destination + "'";
    }
    const std::string declared = destinationVariable->type;
    if (!declared.empty() && declared != "any" && declared != "num" &&
        !typeMatches(declared, sourceVariable->value)) {
        return "Type mismatch: '" + destination + "' is declared as '" +
               declared + "' and cannot mutate to '" +
               typeNameOf(sourceVariable->value) + "'";
    }
    destinationVariable->value = copyForOwnership(sourceVariable->value);
    destinationVariable->ownership = Ownership::Live;
    sourceVariable->ownership = Ownership::Moved;
    return "";
}

namespace {

// Shared validation for varSwapAll / varSwapVal.
std::string swapReferenceError(const Variable* variable,
                               const std::string& name,
                               const std::string& label) {
    if (variable == nullptr) {
        return label + " variable is not defined";
    }
    if (!variable->borrowSource.empty()) {
        return "Cannot swap " + label + " shared variable '" + name +
               "'; use independent variables";
    }
    if (variable->constant) {
        return "Cannot swap " + label + " constant '" + name + "'";
    }
    if (variable->ownership == Ownership::Moved) {
        return "Cannot swap " + label + " moved variable '" + name +
               "'; reinitialize it before swapping";
    }
    if (!variable->borrowers.empty()) {
        return "Cannot swap " + label + " variable '" + name +
               "' while it is being borrowed; end all active borrows first";
    }
    return "";
}

} // namespace

std::string Environment::swapAll(const std::string& first,
                                 const std::string& second) {
    Variable* firstVariable = findVariable(first);
    Variable* secondVariable = findVariable(second);
    if (firstVariable == nullptr) {
        return "first variable is not defined";
    }
    if (secondVariable == nullptr) {
        return "second variable is not defined";
    }
    if (first == second) {
        return "a variable cannot be swapped with itself";
    }
    const std::string firstError = swapReferenceError(firstVariable, first, "first");
    if (!firstError.empty()) {
        return firstError;
    }
    const std::string secondError =
        swapReferenceError(secondVariable, second, "second");
    if (!secondError.empty()) {
        return secondError;
    }
    std::swap(firstVariable->value, secondVariable->value);
    std::swap(firstVariable->type, secondVariable->type);
    return "";
}

std::string Environment::swapValue(const std::string& first,
                                   const std::string& second) {
    Variable* firstVariable = findVariable(first);
    Variable* secondVariable = findVariable(second);
    if (firstVariable == nullptr) {
        return "first variable is not defined";
    }
    if (secondVariable == nullptr) {
        return "second variable is not defined";
    }
    if (first == second) {
        return "a variable cannot be swapped with itself";
    }
    const std::string firstError = swapReferenceError(firstVariable, first, "first");
    if (!firstError.empty()) {
        return firstError;
    }
    const std::string secondError =
        swapReferenceError(secondVariable, second, "second");
    if (!secondError.empty()) {
        return secondError;
    }
    if (!typeMatches(firstVariable->type, secondVariable->value)) {
        return "Type mismatch: '" + first + "' is declared as '" +
               firstVariable->type + "' but got a '" +
               typeNameOf(secondVariable->value) + "' value";
    }
    if (!typeMatches(secondVariable->type, firstVariable->value)) {
        return "Type mismatch: '" + second + "' is declared as '" +
               secondVariable->type + "' but got a '" +
               typeNameOf(firstVariable->value) + "' value";
    }
    std::swap(firstVariable->value, secondVariable->value);
    return "";
}

std::string Environment::borrow(const std::string& source,
                                const std::string& borrower) {
    const std::string canonical = canonicalName(source);
    if (canonical.empty()) {
        return "source variable is not defined";
    }
    Variable* sourceVariable = findVariable(canonical);
    Variable* destinationVariable = findVariable(borrower);
    if (destinationVariable == nullptr) {
        return "destination variable is not defined";
    }
    if (canonical == borrower) {
        return "a variable cannot borrow from itself";
    }
    if (!destinationVariable->borrowSource.empty()) {
        return "Cannot borrow into shared variable '" + borrower +
               "'; end or detach that alias first";
    }
    const std::string sourceError = ownershipError(source, "borrow from");
    if (!sourceError.empty()) {
        return sourceError;
    }
    if (destinationVariable->constant) {
        return "Cannot borrow into constant '" + borrower + "'";
    }
    if (!typeMatches(destinationVariable->type, sourceVariable->value)) {
        return "Type mismatch: '" + borrower + "' is declared as '" +
               destinationVariable->type + "' but got a '" +
               typeNameOf(sourceVariable->value) + "' value";
    }
    destinationVariable->value = Value{};
    destinationVariable->borrowSource = canonical;
    destinationVariable->borrowMutable = false;
    sourceVariable->borrowers.insert(borrower);
    return "";
}

std::string Environment::borrowMutate(const std::string& source,
                                      const std::string& borrower) {
    const std::string canonical = canonicalName(source);
    if (canonical.empty()) {
        return "Cannot mutably borrow an undefined source variable";
    }
    Variable* sourceVariable = findVariable(canonical);
    Variable* destinationVariable = findVariable(borrower);
    if (destinationVariable == nullptr) {
        return "Cannot mutably borrow into an undefined destination variable";
    }
    if (canonical == borrower) {
        return "a variable cannot borrow from itself";
    }
    const std::string sourceError = ownershipError(source, "borrow from");
    if (!sourceError.empty()) {
        return sourceError;
    }
    if (!sourceVariable->borrowers.empty()) {
        return "Cannot mutably borrow '" + source +
               "' while it has active borrows; end all active borrows first";
    }
    if (!destinationVariable->borrowSource.empty()) {
        return "Variable '" + borrower + "' is already borrowing";
    }
    if (destinationVariable->constant) {
        return "Cannot borrow into constant '" + borrower + "'";
    }
    const std::string declared = destinationVariable->type;
    if (!declared.empty() && declared != "any" && declared != "num" &&
        !typeMatches(declared, sourceVariable->value)) {
        return "Type mismatch: '" + borrower + "' is declared as '" +
               declared + "' and cannot mutate to '" +
               typeNameOf(sourceVariable->value) + "'";
    }
    destinationVariable->value = Value{};
    destinationVariable->borrowSource = canonical;
    destinationVariable->borrowMutable = true;
    sourceVariable->borrowers.insert(borrower);
    return "";
}

std::string Environment::endBorrow(const std::string& borrower) {
    Variable* variable = findVariable(borrower);
    if (variable == nullptr) {
        return "varEndBorrow() expects a defined variable";
    }
    if (variable->borrowSource.empty()) {
        return "'" + borrower +
               "' is not an active borrow; varEndBorrow() expects a borrowing "
               "variable";
    }
    const std::string canonical = canonicalName(borrower);
    Variable* source = canonical.empty() ? nullptr : findVariable(canonical);
    if (source == nullptr) {
        return "borrowed source '" + variable->borrowSource +
               "' is no longer available";
    }
    Value copy = copyForOwnership(source->value);
    source->borrowers.erase(borrower);
    variable->borrowSource.clear();
    variable->borrowMutable = false;
    variable->value = std::move(copy);
    variable->ownership = Ownership::Live;
    return "";
}

bool Environment::isBorrowing(const std::string& name) const {
    const Variable* variable = findVariable(name);
    return variable != nullptr && !variable->borrowSource.empty();
}

bool Environment::isBeingBorrowed(const std::string& name) const {
    const std::string canonical = canonicalName(name);
    const Variable* variable =
        canonical.empty() ? nullptr : findVariable(canonical);
    return variable != nullptr && !variable->borrowers.empty();
}

void Environment::registerFunction(const std::string& name,
                                   std::shared_ptr<void> function) {
    functionScopes_.back()[name] = std::move(function);
}

std::shared_ptr<void> Environment::findFunction(const std::string& name) const {
    for (auto scope = functionScopes_.rbegin(); scope != functionScopes_.rend();
         ++scope) {
        const auto found = scope->find(name);
        if (found != scope->end()) {
            return found->second;
        }
    }
    return nullptr;
}

void Environment::setUserFunctionHandler(UserFunctionHandler handler) {
    userFunctionHandler_ = std::move(handler);
}

Value Environment::callUserFunction(
    const std::string& name, const std::vector<Value>& arguments,
    const std::vector<std::shared_ptr<CodeblockValue>>& codeblocks, int line,
    int column) {
    std::string qualified = name;
    if (qualified.rfind("global.", 0) == 0) {
        qualified.erase(0, 7);
    }
    const auto module = moduleFunctions_.find(qualified);
    if (module != moduleFunctions_.end()) {
        return module->second(qualified, arguments, codeblocks, *this, line,
                              column);
    }
    if (!userFunctionHandler_) {
        throw SourceError("unknown function '" + name + "'", line, column);
    }
    return userFunctionHandler_(name, arguments, codeblocks, *this, line,
                                column);
}

void Environment::setMainOverride(std::string name) {
    mainOverride_ = std::move(name);
}

const std::string& Environment::mainOverride() const { return mainOverride_; }

void Environment::setSetupInProgress(bool value) { setupInProgress_ = value; }

bool Environment::setupInProgress() const { return setupInProgress_; }

void Environment::setSourceDirectory(std::string directory) {
    sourceDirectory_ = std::move(directory);
}

const std::string& Environment::sourceDirectory() const {
    return sourceDirectory_;
}

void Environment::registerModuleFunction(const std::string& qualifiedName,
                                         ModuleFunction function) {
    moduleFunctions_[qualifiedName] = std::move(function);
}

void Environment::aliasModuleFunctions(const std::string& from,
                                       const std::string& to) {
    const std::string prefix = from + ".";
    std::vector<std::pair<std::string, ModuleFunction>> aliases;
    for (const auto& entry : moduleFunctions_) {
        if (entry.first.rfind(prefix, 0) == 0) {
            aliases.emplace_back(to + entry.first.substr(from.size()),
                                 entry.second);
        }
    }
    for (auto& alias : aliases) {
        moduleFunctions_[std::move(alias.first)] = std::move(alias.second);
    }
}

void Environment::retainNativeModule(std::shared_ptr<void> handle) {
    nativeModules_.push_back(std::move(handle));
}

bool Environment::hasImportedModule(const std::string& name) const {
    return importedModules_.find(name) != importedModules_.end();
}

void Environment::markImportedModule(const std::string& name) {
    importedModules_.insert(name);
}

void Environment::registerModuleNamespace(
    const std::string& name, std::shared_ptr<RecordValue> namespaceValue) {
    modules_[name] = std::move(namespaceValue);
    setVariableRawCurrent(name, Variable{"module", modules_[name], true});
}

std::shared_ptr<RecordValue> Environment::moduleNamespace(
    const std::string& name) const {
    const auto found = modules_.find(name);
    return found == modules_.end() ? nullptr : found->second;
}

void Environment::setForeverWarningSuppressed() {
    foreverWarningSuppressed_ = true;
}

bool Environment::foreverWarningSuppressed() const {
    return foreverWarningSuppressed_;
}

void Environment::setDeprecationWarningSuppressed() {
    deprecationWarningSuppressed_ = true;
}

bool Environment::deprecationWarningSuppressed() const {
    return deprecationWarningSuppressed_;
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
    if (type == "none") {
        // A `-> none` function (or a value-less builtin) converts to `none`,
        // and only to `none`. Without this branch every `-> none` call failed
        // with "value cannot be assigned to type 'none'".
        if (std::holds_alternative<std::monostate>(value)) {
            return value;
        }
    } else if (type == "int") {
        if (std::holds_alternative<std::int64_t>(value)) {
            return value;
        }
        if (std::holds_alternative<UInt64Value>(value)) {
            fail("value " + valueToString(value) +
                     " is out of range for type 'int'",
                 line, column);
        }
        if (const auto* number = std::get_if<double>(&value);
            number != nullptr && *number == static_cast<std::int64_t>(*number)) {
            return static_cast<std::int64_t>(*number);
        }
    } else if (type == "float") {
        if (std::holds_alternative<std::int64_t>(value)) {
            return static_cast<double>(std::get<std::int64_t>(value));
        }
        if (const auto* wide = std::get_if<UInt64Value>(&value)) {
            return static_cast<double>(wide->value);
        }
        if (std::holds_alternative<double>(value)) {
            return value;
        }
    } else if (type == "num") {
        if (std::holds_alternative<std::int64_t>(value) ||
            std::holds_alternative<double>(value) ||
            std::holds_alternative<UInt64Value>(value)) {
            return value;
        }
    } else if (type == "numBool" || type == "bit" || type == "byte" ||
               type == "uint8" || type == "uint16" || type == "uint32" ||
               type == "uint64" || type == "int8" || type == "int16" ||
               type == "int32" || type == "int64") {
        if (std::holds_alternative<UInt64Value>(value)) {
            if (type == "uint64") {
                return value;
            }
            fail("value " + valueToString(value) +
                     " is out of range for type '" + type + "'",
                 line, column);
        }
        if (const auto* integer = std::get_if<std::int64_t>(&value)) {
            if (integerValueInRange(type, *integer)) {
                return value;
            }
            fail("value " + valueToString(value) +
                     " is out of range for type '" + type + "'",
                 line, column);
        }
        if (std::holds_alternative<double>(value)) {
            fail("value cannot be assigned to type '" + type + "'", line,
                 column);
        }
    } else if (type == "float32" || type == "float64") {
        if (std::holds_alternative<std::int64_t>(value)) {
            return value;
        }
        if (const auto* wide = std::get_if<UInt64Value>(&value)) {
            return static_cast<double>(wide->value);
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
    } else if (type == "functionAddress" &&
               (std::holds_alternative<std::int64_t>(value) ||
                std::holds_alternative<UInt64Value>(value))) {
        return value;
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
    } else if (type == "vargroup") {
        if (const auto* record =
                std::get_if<std::shared_ptr<RecordValue>>(&value);
            record != nullptr && *record != nullptr &&
            (*record)->kind == RecordKind::VarGroup) {
            return value;
        }
    } else if (type == "codeblock" &&
               std::holds_alternative<std::shared_ptr<CodeblockValue>>(value)) {
        return value;
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
    if (const auto* wide = std::get_if<UInt64Value>(&value)) {
        return std::to_string(wide->value);
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
    if (const auto* wide = std::get_if<UInt64Value>(&value)) {
        return wide->value != 0;
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
    if (std::holds_alternative<UInt64Value>(value)) {
        return "int";
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
           std::holds_alternative<double>(value) ||
           std::holds_alternative<UInt64Value>(value);
}

double asNumber(const Value& value, int line, int column) {
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return static_cast<double>(*integer);
    }
    if (const auto* number = std::get_if<double>(&value)) {
        return *number;
    }
    if (const auto* wide = std::get_if<UInt64Value>(&value)) {
        return static_cast<double>(wide->value);
    }
    throw SourceError("numeric value required", line, column);
}

Value copyForOwnership(const Value& value) {
    if (const auto* list = std::get_if<std::shared_ptr<List>>(&value)) {
        if (*list != nullptr) {
            return std::make_shared<List>(List{(*list)->elements});
        }
    }
    if (const auto* tuple = std::get_if<std::shared_ptr<Tuple>>(&value)) {
        if (*tuple != nullptr) {
            return std::make_shared<Tuple>(Tuple{(*tuple)->elements});
        }
    }
    if (const auto* enumValue =
            std::get_if<std::shared_ptr<EnumValue>>(&value)) {
        if (*enumValue != nullptr) {
            return std::make_shared<EnumValue>(**enumValue);
        }
    }
    return value;
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

namespace {

std::map<std::string, std::string>& bundledAssets() {
    static std::map<std::string, std::string> instance;
    return instance;
}

} // namespace

void setBundledAssets(std::map<std::string, std::string> assets) {
    bundledAssets() = std::move(assets);
}

std::string bundledAssetPath(const std::string& name) {
    const auto found = bundledAssets().find(name);
    return found == bundledAssets().end() ? std::string() : found->second;
}

std::vector<std::string> bundledAssetNames() {
    std::vector<std::string> names;
    for (const auto& entry : bundledAssets()) {
        // Assets are registered under both the path as included and the bare
        // file name; report the friendly bare names only.
        if (entry.first ==
            std::filesystem::path(entry.first).filename().string()) {
            names.push_back(entry.first);
        }
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

} // namespace lynxer
