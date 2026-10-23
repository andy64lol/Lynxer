#include "lint.hpp"

#include "builtins.hpp"
#include "types.hpp"

#include <algorithm>
#include <memory>
#include <unordered_set>

namespace lynxer {
namespace {

struct VariableInfo {
    std::string type;
    std::string name;
    int line = 0;
    int column = 0;
    std::size_t reads = 0;
    bool parameter = false;
};

using Scope = std::unordered_map<std::string, std::shared_ptr<VariableInfo>>;
using FunctionMap = std::unordered_map<std::string, const Function*>;

struct Context {
    std::vector<Scope> scopes;
    FunctionMap functions;
    std::unordered_set<std::string> genericTypeNames;
    std::string returnType = "any";
    int currentLine = 0;
    int currentColumn = 0;
    std::vector<LintDiagnostic>* diagnostics = nullptr;
};

void report(Context& context, LintDiagnostic::Severity severity, int line,
            int column, std::string message) {
    context.diagnostics->push_back(
        {severity, line, column, std::move(message)});
}

std::shared_ptr<VariableInfo> findVariable(Context& context,
                                           const std::string& name) {
    for (auto scope = context.scopes.rbegin(); scope != context.scopes.rend();
         ++scope) {
        const auto found = scope->find(name);
        if (found != scope->end()) {
            return found->second;
        }
    }
    return nullptr;
}

std::string stripFunctionPrefix(std::string name) {
    if (name.rfind("global.", 0) == 0) {
        name.erase(0, 7);
    } else if (name.rfind("local.", 0) == 0) {
        name.erase(0, 6);
    }
    return name;
}

const Function* findFunction(Context& context, const std::string& name) {
    const std::string bare = stripFunctionPrefix(name);
    const auto found = context.functions.find(bare);
    return found == context.functions.end() ? nullptr : found->second;
}

bool isIntegerType(const std::string& type) {
    static const std::unordered_set<std::string> types = {
        "int", "byte", "numBool", "bit", "uint8", "uint16", "uint32",
        "uint64", "int8", "int16", "int32", "int64"};
    return types.count(type) != 0;
}

bool isNumericType(const std::string& type) {
    return isIntegerType(type) || type == "num" || type == "float" ||
           type == "float32" || type == "float64";
}

bool canAssign(const std::string& expected, const std::string& actual) {
    if (expected.empty() || expected == "any" || actual.empty() ||
        actual == "any" || expected == actual) {
        return true;
    }
    if (expected == "num") {
        return isNumericType(actual);
    }
    if (expected == "float" || expected == "float32" ||
        expected == "float64") {
        return isNumericType(actual);
    }
    if (expected == "int") {
        return isIntegerType(actual) || actual == "float" ||
               actual == "float32" || actual == "float64" || actual == "num";
    }
    if (isIntegerType(expected)) {
        return isIntegerType(actual);
    }
    if (expected == "codeblock" && actual == "function") {
        return true;
    }
    if (TypeRegistry::instance().findClass(expected) != nullptr &&
        TypeRegistry::instance().findClass(actual) != nullptr) {
        return classIsA(actual, expected);
    }
    return false;
}

bool conversionIsSuspicious(const std::string& expected,
                            const std::string& actual) {
    if (expected == "int" &&
        (actual == "float" || actual == "float32" ||
         actual == "float64" || actual == "num")) {
        return true;
    }
    return isIntegerType(expected) && expected != "int" && actual == "int";
}

void checkAssignment(Context& context, const std::string& expected,
                     const std::string& actual, int line, int column,
                     const std::string& subject) {
    if (actual.empty() || expected.empty() || expected == "any" ||
        context.genericTypeNames.count(expected) != 0 ||
        actual == "any") {
        return;
    }
    if (!canAssign(expected, actual)) {
        report(context, LintDiagnostic::Severity::Error, line, column,
               subject + " has type '" + actual + "', which cannot be assigned to '" +
                   expected + "'");
    } else if (conversionIsSuspicious(expected, actual)) {
        report(context, LintDiagnostic::Severity::Warning, line, column,
               subject + " converts '" + actual + "' to '" + expected +
                   "'; the value may be rejected or lose information at runtime");
    }
}

std::string infer(Context& context, const Expression& expression);
void analyzeStatements(Context& context, const StatementList& statements);

std::string infer(Context& context, const Expression& expression) {
    if (const auto* literal = dynamic_cast<const LiteralExpression*>(&expression)) {
        return typeNameOf(literal->value());
    }
    if (const auto* variable =
            dynamic_cast<const VariableExpression*>(&expression)) {
        if (const auto info = findVariable(context, variable->name())) {
            ++info->reads;
            return info->type;
        }
        if (findFunction(context, variable->name()) != nullptr) {
            return "function";
        }
        return "";
    }
    if (const auto* unary = dynamic_cast<const UnaryExpression*>(&expression)) {
        const std::string operand = infer(context, unary->operandExpr());
        if (unary->operation() == "not" || unary->operation() == "!!") {
            return "bool";
        }
        return operand;
    }
    if (const auto* await = dynamic_cast<const AwaitExpression*>(&expression)) {
        (void)infer(context, await->expression());
        return "";
    }
    if (const auto* binary = dynamic_cast<const BinaryExpression*>(&expression)) {
        const std::string left = infer(context, binary->leftExpr());
        const std::string right = infer(context, binary->rightExpr());
        const std::string& operation = binary->operation();
        if (operation == "is" || operation == "isnt" || operation == "==" ||
            operation == "!=" || operation == "<" || operation == ">" ||
            operation == "<=" || operation == ">=" || operation == "and" ||
            operation == "or" || operation == "xor" || operation == "&&" ||
            operation == "||") {
            return "bool";
        }
        if (operation == "+" && left == "str" && right == "str") {
            return "str";
        }
        if (isNumericType(left) && isNumericType(right)) {
            return left == "float" || left == "float32" || left == "float64" ||
                           right == "float" || right == "float32" ||
                           right == "float64"
                       ? "float"
                       : "int";
        }
        return "";
    }
    if (const auto* call = dynamic_cast<const CallExpression*>(&expression)) {
        std::vector<std::string> argumentTypes;
        argumentTypes.reserve(call->arguments().size());
        for (const auto& argument : call->arguments()) {
            argumentTypes.push_back(infer(context, *argument));
        }
        if (const auto callee = findVariable(context, call->name())) {
            ++callee->reads;
            if (callee->type != "any" && callee->type != "function" &&
                callee->type != "codeblock") {
                report(context, LintDiagnostic::Severity::Error,
                       context.currentLine, context.currentColumn,
                       "value '" + call->name() + "' of type '" + callee->type +
                           "' is not callable");
            }
            return "";
        }
        const Function* function = findFunction(context, call->name());
        if (function == nullptr) {
            const std::string name = stripFunctionPrefix(call->name());
            if (name == "toString" || name == "str") return "str";
            if (name == "len" || name == "count" || name == "size") return "int";
            if (name == "println" || name == "print" || name == "write") return "none";
            return "";
        }
        std::size_t required = 0;
        for (const Parameter& parameter : function->parameters) {
            if (parameter.defaultValue == nullptr) {
                ++required;
            }
        }
        if (argumentTypes.size() < required ||
            argumentTypes.size() > function->parameters.size()) {
            report(context, LintDiagnostic::Severity::Error,
                   context.currentLine, context.currentColumn,
                   "function '" + function->name + "' expects " +
                       std::to_string(required) + " to " +
                       std::to_string(function->parameters.size()) +
                       " arguments, received " +
                       std::to_string(argumentTypes.size()));
        }
        std::unordered_map<std::string, std::string> genericTypes;
        const std::size_t count =
            std::min(argumentTypes.size(), function->parameters.size());
        for (std::size_t index = 0; index < count; ++index) {
            const Parameter& parameter = function->parameters[index];
            const std::string& actual = argumentTypes[index];
            std::string expected = parameter.type;
            if (parameter.infersGenericType) {
                if (!actual.empty()) {
                    genericTypes[expected] = actual;
                }
                continue;
            }
            const auto generic = genericTypes.find(expected);
            if (generic != genericTypes.end()) {
                expected = generic->second;
            }
            checkAssignment(context, expected, actual,
                            context.currentLine, context.currentColumn,
                            "argument " + std::to_string(index + 1) +
                                " to '" + function->name + "'");
        }
        const auto resultType = genericTypes.find(function->returnType);
        return resultType == genericTypes.end() ? function->returnType
                                                 : resultType->second;
    }
    if (const auto* list =
            dynamic_cast<const ListLiteralExpression*>(&expression)) {
        for (const auto& element : list->elements()) (void)infer(context, *element);
        return "list";
    }
    if (const auto* tuple =
            dynamic_cast<const TupleLiteralExpression*>(&expression)) {
        for (const auto& element : tuple->elements()) (void)infer(context, *element);
        return "tuple";
    }
    if (const auto* coerce =
            dynamic_cast<const TypeCoerceExpression*>(&expression)) {
        const std::string actual = infer(context, coerce->inner());
        checkAssignment(context, coerce->type(), actual, context.currentLine,
                        context.currentColumn,
                        "explicit conversion");
        return coerce->type();
    }
    if (const auto* dot = dynamic_cast<const DotAccessExpression*>(&expression)) {
        const std::string receiver = infer(context, dot->object());
        if (const ClassDef* definition =
                TypeRegistry::instance().findClass(receiver)) {
            for (const ClassDef* current : classLineage(definition)) {
                const auto field = std::find_if(
                    current->fields.begin(), current->fields.end(),
                    [&](const ClassFieldDef& item) {
                        return item.name == dot->fieldName();
                    });
                if (field != current->fields.end()) return field->type;
            }
        }
        if (const StructDef* definition =
                TypeRegistry::instance().findStruct(receiver)) {
            const auto field = std::find_if(
                definition->fields.begin(), definition->fields.end(),
                [&](const NamedField& item) {
                    return item.name == dot->fieldName();
                });
            if (field != definition->fields.end()) return field->type;
        }
        return "";
    }
    if (const auto* method =
            dynamic_cast<const MethodCallExpression*>(&expression)) {
        const std::string receiver = infer(context, method->receiver());
        std::vector<std::string> argumentTypes;
        for (const auto& argument : method->arguments()) {
            argumentTypes.push_back(infer(context, *argument));
        }
        const ClassDef* definition =
            TypeRegistry::instance().findClass(receiver);
        const ClassMethod* target = findClassMethod(definition,
                                                    method->methodName());
        if (target != nullptr) {
            if (argumentTypes.size() != target->params.size()) {
                report(context, LintDiagnostic::Severity::Error,
                       context.currentLine, context.currentColumn,
                       "method '" + method->methodName() + "' expects " +
                           std::to_string(target->params.size()) +
                           " arguments, received " +
                           std::to_string(argumentTypes.size()));
            }
            for (std::size_t index = 0;
                 index < std::min(argumentTypes.size(), target->params.size());
                 ++index) {
                checkAssignment(context, target->params[index].first,
                                argumentTypes[index],
                                context.currentLine, context.currentColumn,
                                "argument " + std::to_string(index + 1) +
                                    " to method '" + method->methodName() + "'");
            }
        }
        return "";
    }
    if (const auto* created = dynamic_cast<const NewExpression*>(&expression)) {
        std::vector<std::string> argumentTypes;
        for (const auto& argument : created->arguments()) {
            argumentTypes.push_back(infer(context, *argument));
        }
        if (const StructDef* definition =
                TypeRegistry::instance().findStruct(created->typeName())) {
            if (argumentTypes.size() != definition->fields.size()) {
                report(context, LintDiagnostic::Severity::Error,
                       context.currentLine, context.currentColumn,
                       "struct '" + created->typeName() + "' expects " +
                           std::to_string(definition->fields.size()) +
                           " arguments, received " +
                           std::to_string(argumentTypes.size()));
            }
            for (std::size_t index = 0;
                 index < std::min(argumentTypes.size(), definition->fields.size());
                 ++index) {
                checkAssignment(context, definition->fields[index].type,
                                argumentTypes[index],
                                context.currentLine, context.currentColumn,
                                "struct field");
            }
            return created->typeName();
        }
        if (const ClassDef* definition =
                TypeRegistry::instance().findClass(created->typeName())) {
            if (const ClassMethod* constructor =
                    findClassMethod(definition, "init")) {
                if (argumentTypes.size() != constructor->params.size()) {
                    report(context, LintDiagnostic::Severity::Error,
                           context.currentLine, context.currentColumn,
                           "constructor for '" + created->typeName() +
                               "' expects " +
                               std::to_string(constructor->params.size()) +
                               " arguments, received " +
                               std::to_string(argumentTypes.size()));
                }
                for (std::size_t index = 0;
                     index < std::min(argumentTypes.size(),
                                      constructor->params.size()); ++index) {
                    checkAssignment(context, constructor->params[index].first,
                                    argumentTypes[index],
                                    context.currentLine, context.currentColumn,
                                    "constructor argument");
                }
            } else if (!argumentTypes.empty()) {
                report(context, LintDiagnostic::Severity::Error,
                       context.currentLine, context.currentColumn,
                       "class '" + created->typeName() +
                           "' has no init() constructor but arguments were given");
            }
            return created->typeName();
        }
        report(context, LintDiagnostic::Severity::Error, context.currentLine,
               context.currentColumn,
               "unknown type '" + created->typeName() + "'");
        return "";
    }
    if (const auto* add =
            dynamic_cast<const AddVarGroupExpression*>(&expression)) {
        (void)infer(context, add->target());
        (void)infer(context, add->valueExpr());
        return "vargroup";
    }
    if (const auto* remove =
            dynamic_cast<const RemoveVarGroupExpression*>(&expression)) {
        (void)infer(context, remove->target());
        return "vargroup";
    }
    if (const auto* group =
            dynamic_cast<const VarGroupLiteralExpression*>(&expression)) {
        for (const auto& field : group->fields()) {
            (void)infer(context, *field.value);
        }
        return "vargroup";
    }
    if (const auto* interpolation =
            dynamic_cast<const InterpStringExpression*>(&expression)) {
        for (const auto& part : interpolation->expressions()) {
            (void)infer(context, *part);
        }
        return "str";
    }
    return "";
}

bool terminates(const Statement& statement);

bool statementsTerminate(const StatementList& statements) {
    for (const auto& statement : statements) {
        if (terminates(*statement)) return true;
    }
    return false;
}

bool terminates(const Statement& statement) {
    if (dynamic_cast<const ReturnStatement*>(&statement) != nullptr ||
        dynamic_cast<const LoopControlStatement*>(&statement) != nullptr) {
        return true;
    }
    if (const auto* branch = dynamic_cast<const IfStatement*>(&statement)) {
        return branch->hasElse() &&
               statementsTerminate(branch->thenStatements()) &&
               statementsTerminate(branch->elseStatements());
    }
    if (const auto* attempt = dynamic_cast<const TryCatchStatement*>(&statement)) {
        return statementsTerminate(attempt->tryStatements()) &&
               statementsTerminate(attempt->catchStatements());
    }
    if (const auto* selection = dynamic_cast<const SwitchStatement*>(&statement)) {
        bool hasDefault = false;
        for (const SwitchCase& item : selection->cases()) {
            if (item.pattern == nullptr) hasDefault = true;
            if (!statementsTerminate(item.body)) return false;
        }
        return hasDefault;
    }
    return false;
}

void analyzeFunction(Context& parent, const Function& function,
                     std::string receiverType = "") {
    Context context = parent;
    context.returnType = function.returnType;
    context.scopes.emplace_back();
    const int declarationLine = function.sourceLine;
    for (const Parameter& parameter : function.parameters) {
        auto info = std::make_shared<VariableInfo>();
        info->type = parameter.infersGenericType ? "any" : parameter.type;
        info->name = parameter.name;
        info->line = declarationLine;
        info->parameter = true;
        context.scopes.back()[parameter.name] = std::move(info);
        if (parameter.infersGenericType) {
            context.genericTypeNames.insert(parameter.type);
        }
    }
    if (!receiverType.empty()) {
        auto info = std::make_shared<VariableInfo>();
        info->type = receiverType;
        info->name = "this";
        context.scopes.back()["this"] = std::move(info);
    }
    analyzeStatements(context, function.statements);
    for (const auto& entry : context.scopes.back()) {
        const auto& info = entry.second;
        if (info->name == "this" || info->reads != 0) continue;
        report(context, LintDiagnostic::Severity::Warning, info->line, info->column,
               std::string(info->parameter ? "unused parameter '" :
                                             "unused variable '") +
                   info->name + "'");
    }
}

void analyzeStatements(Context& context, const StatementList& statements) {
    bool unreachable = false;
    for (const auto& owned : statements) {
        const Statement& statement = *owned;
        const int line = statement.sourceLine();
        const int column = statement.sourceColumn();
        context.currentLine = line;
        context.currentColumn = column;
        if (unreachable) {
            report(context, LintDiagnostic::Severity::Warning, line, column,
                   "unreachable statement");
        }

        if (const auto* declaration =
                dynamic_cast<const DeclarationStatement*>(&statement)) {
            const std::string actual = infer(context, declaration->valueExpr());
            checkAssignment(context, declaration->declaredType(), actual, line,
                            column, "initializer for '" + declaration->name() + "'");
            auto info = std::make_shared<VariableInfo>();
            info->type = declaration->declaredType();
            info->name = declaration->name();
            info->line = line;
            info->column = column;
            context.scopes.back()[info->name] = std::move(info);
        } else if (const auto* shared =
                       dynamic_cast<const SharedDeclarationStatement*>(&statement)) {
            auto source = findVariable(context, shared->sourceName());
            if (source != nullptr) {
                ++source->reads;
                checkAssignment(context, shared->declaredType(), source->type,
                                line, column,
                                "shared initializer for '" + shared->name() + "'");
            }
            auto info = std::make_shared<VariableInfo>();
            info->type = shared->declaredType();
            info->name = shared->name();
            info->line = line;
            info->column = column;
            context.scopes.back()[info->name] = std::move(info);
        } else if (const auto* assignment =
                       dynamic_cast<const AssignmentStatement*>(&statement)) {
            const std::string actual = infer(context, assignment->valueExpr());
            if (const auto target = findVariable(context, assignment->name())) {
                checkAssignment(context, target->type, actual, line, column,
                                "assignment to '" + assignment->name() + "'");
            }
        } else if (const auto* assignment =
                       dynamic_cast<const DotAssignmentStatement*>(&statement)) {
            if (!assignment->path().empty()) {
                if (const auto target = findVariable(context,
                                                    assignment->path().front())) {
                    ++target->reads;
                    std::string fieldType = assignment->declaredType();
                    if (fieldType.empty() && assignment->path().size() > 1) {
                        const std::string& fieldName = assignment->path()[1];
                        if (const ClassDef* definition =
                                TypeRegistry::instance().findClass(target->type)) {
                            for (const ClassDef* current :
                                 classLineage(definition)) {
                                const auto field = std::find_if(
                                    current->fields.begin(), current->fields.end(),
                                    [&](const ClassFieldDef& item) {
                                        return item.name == fieldName;
                                    });
                                if (field != current->fields.end()) {
                                    fieldType = field->type;
                                    break;
                                }
                            }
                        } else if (const StructDef* definition =
                                       TypeRegistry::instance().findStruct(
                                           target->type)) {
                            const auto field = std::find_if(
                                definition->fields.begin(),
                                definition->fields.end(),
                                [&](const NamedField& item) {
                                    return item.name == fieldName;
                                });
                            if (field != definition->fields.end()) {
                                fieldType = field->type;
                            }
                        }
                    }
                    checkAssignment(context, fieldType,
                                    infer(context, assignment->valueExpr()),
                                    line, column, "field assignment");
                } else {
                    (void)infer(context, assignment->valueExpr());
                }
            } else {
                (void)infer(context, assignment->valueExpr());
            }
        } else if (const auto* returned =
                       dynamic_cast<const ReturnStatement*>(&statement)) {
            const Expression* value = returned->valueExpr();
            const std::string actual = value == nullptr ? "none"
                                                         : infer(context, *value);
            checkAssignment(context, context.returnType, actual, line, column,
                            "return value");
        } else if (const auto* expression =
                       dynamic_cast<const ExpressionStatement*>(&statement)) {
            (void)infer(context, expression->expression());
        } else if (const auto* branch = dynamic_cast<const IfStatement*>(&statement)) {
            (void)infer(context, branch->condition());
            analyzeStatements(context, branch->thenStatements());
            analyzeStatements(context, branch->elseStatements());
        } else if (const auto* loop = dynamic_cast<const WhileStatement*>(&statement)) {
            (void)infer(context, loop->condition());
            analyzeStatements(context, loop->statements());
        } else if (const auto* loop = dynamic_cast<const ForStatement*>(&statement)) {
            if (loop->initializer() != nullptr) {
                if (const auto* declaration = dynamic_cast<const DeclarationStatement*>(
                        loop->initializer())) {
                    const std::string actual =
                        infer(context, declaration->valueExpr());
                    checkAssignment(context, declaration->declaredType(), actual,
                                    line, column, "for initializer");
                    auto info = std::make_shared<VariableInfo>();
                    info->type = declaration->declaredType();
                    info->name = declaration->name();
                    info->line = line;
                    info->column = column;
                    context.scopes.back()[info->name] = std::move(info);
                } else if (const auto* assignment =
                               dynamic_cast<const AssignmentStatement*>(
                                   loop->initializer())) {
                    (void)infer(context, assignment->valueExpr());
                } else if (const auto* expression =
                               dynamic_cast<const ExpressionStatement*>(
                                   loop->initializer())) {
                    (void)infer(context, expression->expression());
                }
            }
            if (loop->condition() != nullptr) (void)infer(context, *loop->condition());
            analyzeStatements(context, loop->statements());
            if (loop->update() != nullptr) {
                if (const auto* assignment = dynamic_cast<const AssignmentStatement*>(
                        loop->update())) {
                    (void)infer(context, assignment->valueExpr());
                } else if (const auto* expression =
                               dynamic_cast<const ExpressionStatement*>(
                                   loop->update())) {
                    (void)infer(context, expression->expression());
                }
            }
        } else if (const auto* loop =
                       dynamic_cast<const RangeForStatement*>(&statement)) {
            (void)infer(context, loop->start());
            (void)infer(context, loop->end());
            if (loop->step() != nullptr) (void)infer(context, *loop->step());
            auto info = std::make_shared<VariableInfo>();
            info->type = loop->declaredType();
            info->name = loop->name();
            info->line = line;
            info->column = column;
            context.scopes.back()[info->name] = info;
            analyzeStatements(context, loop->statements());
        } else if (const auto* loop =
                       dynamic_cast<const DoWhileStatement*>(&statement)) {
            analyzeStatements(context, loop->statements());
            (void)infer(context, loop->condition());
        } else if (const auto* loop = dynamic_cast<const IterateStatement*>(&statement)) {
            (void)infer(context, loop->count());
            analyzeStatements(context, loop->statements());
        } else if (const auto* loop = dynamic_cast<const ForeverStatement*>(&statement)) {
            analyzeStatements(context, loop->statements());
        } else if (const auto* selection =
                       dynamic_cast<const SwitchStatement*>(&statement)) {
            (void)infer(context, selection->valueExpr());
            for (const SwitchCase& item : selection->cases()) {
                if (item.pattern != nullptr) (void)infer(context, *item.pattern);
                analyzeStatements(context, item.body);
            }
        } else if (const auto* attempt =
                       dynamic_cast<const TryCatchStatement*>(&statement)) {
            analyzeStatements(context, attempt->tryStatements());
            if (!attempt->catchName().empty()) {
                context.scopes.back()[attempt->catchName()] =
                    std::make_shared<VariableInfo>(VariableInfo{
                        "str", attempt->catchName(), line, column});
            }
            analyzeStatements(context, attempt->catchStatements());
        } else if (const auto* declaration =
                       dynamic_cast<const FunctionDeclarationStatement*>(&statement)) {
            const auto& nested = declaration->function();
            if (nested != nullptr) {
                context.functions[nested->name] = nested.get();
                analyzeFunction(context, *nested);
            }
        } else if (const auto* superCall =
                       dynamic_cast<const SuperConstructorStatement*>(&statement)) {
            for (const auto& argument : superCall->arguments()) {
                (void)infer(context, *argument);
            }
            const ClassDef* parent =
                TypeRegistry::instance().findClass(superCall->parentClass());
            const ClassMethod* constructor = findClassMethod(parent, "init");
            if (constructor != nullptr &&
                superCall->arguments().size() != constructor->params.size()) {
                report(context, LintDiagnostic::Severity::Error, line, column,
                       "parent constructor expects " +
                           std::to_string(constructor->params.size()) +
                           " arguments, received " +
                           std::to_string(superCall->arguments().size()));
            }
            if (constructor != nullptr) {
                for (std::size_t index = 0;
                     index < std::min(superCall->arguments().size(),
                                      constructor->params.size()); ++index) {
                    const std::string actual =
                        infer(context, *superCall->arguments()[index]);
                    checkAssignment(context,
                                    constructor->params[index].first, actual,
                                    line, column, "parent constructor argument");
                }
            } else if (!superCall->arguments().empty()) {
                report(context, LintDiagnostic::Severity::Error, line, column,
                       "parent class '" + superCall->parentClass() +
                           "' has no init() constructor but arguments were given");
            }
        }
        if (terminates(statement)) unreachable = true;
    }
}

} // namespace

std::vector<LintDiagnostic> analyzeLint(
    const std::unordered_map<std::string, Function>& functions) {
    std::vector<LintDiagnostic> diagnostics;
    Context context;
    context.scopes.emplace_back();
    context.diagnostics = &diagnostics;
    for (const auto& entry : functions) {
        context.functions[entry.first] = &entry.second;
    }
    for (const auto& entry : functions) {
        analyzeFunction(context, entry.second);
    }
    TypeRegistry::instance().forEachClass([&](ClassDef& definition) {
        Context classContext = context;
        for (const ClassMethod& method : definition.methods) {
            Context methodContext = classContext;
            methodContext.scopes.emplace_back();
            methodContext.returnType = "any";
            auto receiver = std::make_shared<VariableInfo>();
            receiver->type = definition.name;
            receiver->name = "this";
            methodContext.scopes.back()["this"] = receiver;
            for (const auto& parameter : method.params) {
                auto info = std::make_shared<VariableInfo>();
                info->type = parameter.first;
                info->name = parameter.second;
                info->line = method.sourceLine;
                info->column = method.sourceColumn;
                methodContext.scopes.back()[info->name] = std::move(info);
            }
            analyzeStatements(methodContext, method.body);
            for (const auto& variable : methodContext.scopes.back()) {
                if (variable.first == "this" || variable.second->reads != 0) continue;
                report(methodContext, LintDiagnostic::Severity::Warning,
                       variable.second->line, variable.second->column,
                       "unused method parameter or variable '" + variable.first + "'");
            }
        }
    });
    std::stable_sort(
        diagnostics.begin(), diagnostics.end(),
        [](const LintDiagnostic& left, const LintDiagnostic& right) {
            if (left.line != right.line) return left.line < right.line;
            if (left.column != right.column) return left.column < right.column;
            if (left.severity != right.severity) {
                return left.severity == LintDiagnostic::Severity::Error;
            }
            return left.message < right.message;
        });
    return diagnostics;
}

} // namespace lynxer
