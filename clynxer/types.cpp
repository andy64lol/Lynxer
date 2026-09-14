#include "types.hpp"

#include "error.hpp"

#include <cmath>
#include <variant>

namespace clynxer {

TypeRegistry& TypeRegistry::instance() {
    static TypeRegistry registry;
    return registry;
}

void TypeRegistry::reset() {
    instance().structs_.clear();
    instance().classes_.clear();
    instance().enums_.clear();
}

StructDef* TypeRegistry::findStruct(const std::string& name) {
    const auto found = structs_.find(name);
    return found == structs_.end() ? nullptr : &found->second;
}

ClassDef* TypeRegistry::findClass(const std::string& name) {
    const auto found = classes_.find(name);
    return found == classes_.end() ? nullptr : &found->second;
}

EnumDef* TypeRegistry::findEnum(const std::string& name) {
    const auto found = enums_.find(name);
    return found == enums_.end() ? nullptr : &found->second;
}

bool TypeRegistry::hasNamedType(const std::string& name) const {
    return structs_.count(name) != 0 || classes_.count(name) != 0 ||
           enums_.count(name) != 0;
}

void TypeRegistry::addStruct(StructDef def) {
    structs_.emplace(def.name, std::move(def));
}

void TypeRegistry::addClass(ClassDef def) {
    classes_.emplace(def.name, std::move(def));
}

void TypeRegistry::addEnum(EnumDef def) {
    enums_.emplace(def.name, std::move(def));
}

bool typeMatches(const std::string& type, const Value& value) {
    if (type == "any" || type.empty()) {
        return true;
    }
    if (const auto* enumValue = std::get_if<std::shared_ptr<EnumValue>>(&value)) {
        return *enumValue != nullptr && (*enumValue)->enumName == type;
    }
    if (const auto* record = std::get_if<std::shared_ptr<RecordValue>>(&value)) {
        if (*record != nullptr &&
            ((*record)->kind == RecordKind::Struct ||
             (*record)->kind == RecordKind::Class)) {
            return (*record)->typeName == type;
        }
        return false;
    }
    const bool integer = std::holds_alternative<std::int64_t>(value);
    const bool number = integer || std::holds_alternative<double>(value);
    if (type == "num" || type == "int" || type == "float" || type == "numBool" || type == "bit" || type == "byte" || type == "uint8" || type == "uint16" || type == "uint32" || type == "uint64" || type == "int8" || type == "int16" || type == "int32" || type == "int64" || type == "float32" || type == "float64") {
        return number;
    }
    if (type == "numBool" || type == "bit" || type == "byte" ||
        type == "uint8" || type == "uint16" || type == "uint32" ||
        type == "uint64" || type == "int8" || type == "int16" ||
        type == "int32" || type == "int64") {
        if (const auto* raw = std::get_if<std::int64_t>(&value)) {
            if (type == "numBool" || type == "bit") {
                return *raw == 0 || *raw == 1;
            }
            if (type == "byte" || type == "uint8") {
                return *raw >= 0 && *raw <= 255;
            }
            if (type == "uint16") {
                return *raw >= 0 && *raw <= 65535;
            }
            if (type == "uint32") {
                return *raw >= 0 && *raw <= 4294967295LL;
            }
            if (type == "uint64") {
                return *raw >= 0;
            }
            if (type == "int8") {
                return *raw >= -128 && *raw <= 127;
            }
            if (type == "int16") {
                return *raw >= -32768 && *raw <= 32767;
            }
            if (type == "int32") {
                return *raw >= -2147483648LL && *raw <= 2147483647LL;
            }
            return true;
        }
        return false;
    }
    if (type == "float32" || type == "float64") {
        if (const auto* raw = std::get_if<double>(&value)) {
            return std::isfinite(*raw) &&
                   std::abs(*raw) <= (type == "float32"
                                          ? 3.4028234663852886e38
                                          : 1.7976931348623157e308);
        }
        return integer;
    }
    if (type == "char") {
        return std::holds_alternative<CharValue>(value);
    }
    if (type == "codeblock") {
        return std::holds_alternative<std::shared_ptr<CodeblockValue>>(value);
    }
    if (type == "str") {
        return std::holds_alternative<std::string>(value);
    }
    if (type == "bool") {
        return std::holds_alternative<bool>(value);
    }
    if (type == "list") {
        return std::holds_alternative<std::shared_ptr<List>>(value);
    }
    if (type == "tuple") {
        return std::holds_alternative<std::shared_ptr<Tuple>>(value);
    }
    if (type == "sentinel") {
        return std::holds_alternative<std::shared_ptr<SentinelValue>>(value);
    }
    if (type == "object") {
        return std::holds_alternative<std::shared_ptr<ObjectValue>>(value);
    }
    if (type == "vargroup") {
        if (const auto* record =
                std::get_if<std::shared_ptr<RecordValue>>(&value);
            record != nullptr && *record != nullptr) {
            return (*record)->kind == RecordKind::VarGroup;
        }
        return false;
    }
    return false;
}

} // namespace clynxer
