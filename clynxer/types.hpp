#pragma once

#include "ast.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace clynxer {

// Program-level named types (structs, classes, enums). Declared between
// global setup() and global main(); the parser fills the registry and the
// runtime consults it for named-type assignments, construction, and method
// dispatch.
struct NamedField {
    std::string type;
    std::string name;
    bool constant = false;
};

struct StructDef {
    std::string name;
    std::vector<NamedField> fields;
};

struct ClassMethod {
    std::string name;
    std::vector<std::pair<std::string, std::string>> params;  // type, name
    StatementList body;
};

struct ClassFieldDef {
    std::string type;
    std::string name;
    bool constant = false;
    ExpressionPtr initializer;
};

struct ClassDef {
    std::string name;
    std::vector<ClassFieldDef> fields;
    std::vector<ClassMethod> methods;
};

struct EnumVariant {
    std::string name;
    std::vector<NamedField> fields;
};

struct EnumDef {
    std::string name;
    std::vector<EnumVariant> variants;
};

class TypeRegistry {
public:
    static TypeRegistry& instance();

    static void reset();

    StructDef* findStruct(const std::string& name);
    ClassDef* findClass(const std::string& name);
    EnumDef* findEnum(const std::string& name);

    bool hasNamedType(const std::string& name) const;

    void addStruct(StructDef def);
    void addClass(ClassDef def);
    void addEnum(EnumDef def);

private:
    TypeRegistry() = default;

    std::unordered_map<std::string, StructDef> structs_;
    std::unordered_map<std::string, ClassDef> classes_;
    std::unordered_map<std::string, EnumDef> enums_;
};

// Whether a runtime value satisfies a Lynxer declaration type (docs/types.md
// and the Python type_matches semantics: checks without coercion).
bool typeMatches(const std::string& type, const Value& value);

} // namespace clynxer
