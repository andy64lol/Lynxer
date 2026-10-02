// Value conversion between Lynxer values and the recursive `LynxerFfiValue`
// wire type (see `lynxer/ffi_abi.h` and `docs/native-module-abi.md`). Moved
// verbatim out of `ast.cpp` so both the native-call path and the embedding
// runtime share one implementation.

#include "native_value.hpp"

#include "error.hpp"

#include <variant>

namespace lynxer {

const std::uint8_t* NativeAggregateStorage::storeText(
    const std::string& value) {
    text.push_back(value);
    return reinterpret_cast<const std::uint8_t*>(text.back().data());
}

const LynxerFfiValue* NativeAggregateStorage::encode(const Value& input,
                                                     int line, int column,
                                                     std::size_t depth) {
    if (depth > 128) {
        throw SourceError("native value nesting exceeds 128 levels", line,
                          column);
    }
    if (remainingValues == 0) {
        throw SourceError("native value contains too many items", line, column);
    }
    --remainingValues;
    auto node = std::make_unique<LynxerFfiValue>();
    *node = {};
    LynxerFfiValue* result = node.get();
    nodes.push_back(std::move(node));

    if (std::holds_alternative<std::monostate>(input)) {
        result->tag = LYNXER_FFI_VALUE_NULL;
    } else if (const auto* integer = std::get_if<std::int64_t>(&input)) {
        result->tag = LYNXER_FFI_VALUE_INT64;
        result->value.i = *integer;
    } else if (const auto* number = std::get_if<double>(&input)) {
        result->tag = LYNXER_FFI_VALUE_FLOAT64;
        result->value.f = *number;
    } else if (const auto* boolean = std::get_if<bool>(&input)) {
        result->tag = LYNXER_FFI_VALUE_BOOL;
        result->value.boolean = *boolean ? 1U : 0U;
    } else if (const auto* string = std::get_if<std::string>(&input)) {
        result->tag = LYNXER_FFI_VALUE_STRING;
        result->value.string.data = storeText(*string);
        result->value.string.length =
            static_cast<std::int64_t>(string->size());
    } else if (const auto* character = std::get_if<CharValue>(&input)) {
        result->tag = LYNXER_FFI_VALUE_CHAR;
        result->value.string.data = storeText(character->text);
        result->value.string.length =
            static_cast<std::int64_t>(character->text.size());
    } else if (const auto* wide = std::get_if<UInt64Value>(&input)) {
        result->tag = LYNXER_FFI_VALUE_UINT64;
        result->value.u = wide->value;
    } else if (const auto* bytes =
                   std::get_if<std::shared_ptr<BytesValue>>(&input)) {
        result->tag = LYNXER_FFI_VALUE_BYTES;
        result->value.bytes.data = (*bytes)->data.empty()
                                       ? nullptr
                                       : (*bytes)->data.data();
        result->value.bytes.length =
            static_cast<std::int64_t>((*bytes)->data.size());
    } else if (const auto* list = std::get_if<std::shared_ptr<List>>(&input)) {
        result->tag = LYNXER_FFI_VALUE_ARRAY;
        const std::size_t count =
            *list == nullptr ? 0 : (*list)->elements.size();
        if (count > remainingValues) {
            throw SourceError("native value contains too many items", line,
                              column);
        }
        auto values =
            count == 0 ? nullptr : std::make_unique<LynxerFfiValue[]>(count);
        for (std::size_t index = 0; index < count; ++index) {
            values[index] =
                *encode((*list)->elements[index], line, column, depth + 1);
        }
        result->value.array.items = values.get();
        result->value.array.count = static_cast<std::int64_t>(count);
        if (values != nullptr) {
            arrays.push_back(std::move(values));
        }
    } else if (const auto* tuple =
                   std::get_if<std::shared_ptr<Tuple>>(&input)) {
        result->tag = LYNXER_FFI_VALUE_TUPLE;
        const std::size_t count =
            *tuple == nullptr ? 0 : (*tuple)->elements.size();
        if (count > remainingValues) {
            throw SourceError("native value contains too many items", line,
                              column);
        }
        auto values =
            count == 0 ? nullptr : std::make_unique<LynxerFfiValue[]>(count);
        for (std::size_t index = 0; index < count; ++index) {
            values[index] =
                *encode((*tuple)->elements[index], line, column, depth + 1);
        }
        result->value.array.items = values.get();
        result->value.array.count = static_cast<std::int64_t>(count);
        if (values != nullptr) {
            arrays.push_back(std::move(values));
        }
    } else if (const auto* record =
                   std::get_if<std::shared_ptr<RecordValue>>(&input)) {
        result->tag = LYNXER_FFI_VALUE_RECORD;
        const std::size_t count =
            *record == nullptr ? 0 : (*record)->fields.size();
        if (count > remainingValues) {
            throw SourceError("native value contains too many items", line,
                              column);
        }
        auto encodedFields =
            count == 0 ? nullptr
                       : std::make_unique<LynxerFfiValueField[]>(count);
        if (*record != nullptr) {
            for (std::size_t index = 0; index < count; ++index) {
                const RecordField& field = (*record)->fields[index];
                text.push_back(field.name);
                encodedFields[index].name = text.back().c_str();
                encodedFields[index].name_length =
                    static_cast<std::int64_t>(field.name.size());
                text.push_back(field.type);
                encodedFields[index].type = text.back().c_str();
                encodedFields[index].type_length =
                    static_cast<std::int64_t>(field.type.size());
                encodedFields[index].constant = field.constant ? 1U : 0U;
                encodedFields[index].value =
                    encode(field.value, line, column, depth + 1);
            }
            text.push_back((*record)->typeName);
            result->value.record.type_name = text.back().c_str();
            result->value.record.type_name_length =
                static_cast<std::int64_t>((*record)->typeName.size());
            text.push_back((*record)->displayName);
            result->value.record.display_name = text.back().c_str();
            result->value.record.display_name_length =
                static_cast<std::int64_t>((*record)->displayName.size());
            result->value.record.kind =
                static_cast<std::uint32_t>((*record)->kind);
        }
        result->value.record.fields = encodedFields.get();
        result->value.record.count = static_cast<std::int64_t>(count);
        if (encodedFields != nullptr) {
            fields.push_back(std::move(encodedFields));
        }
    } else if (const auto* enumeration =
                   std::get_if<std::shared_ptr<EnumValue>>(&input)) {
        result->tag = LYNXER_FFI_VALUE_ENUM;
        const std::size_t count =
            *enumeration == nullptr ? 0 : (*enumeration)->payload.size();
        if (count > remainingValues) {
            throw SourceError("native value contains too many items", line,
                              column);
        }
        auto encodedFields =
            count == 0 ? nullptr
                       : std::make_unique<LynxerFfiValueField[]>(count);
        if (*enumeration != nullptr) {
            text.push_back((*enumeration)->enumName);
            result->value.enumeration.enum_name = text.back().c_str();
            result->value.enumeration.enum_name_length =
                static_cast<std::int64_t>((*enumeration)->enumName.size());
            text.push_back((*enumeration)->variantName);
            result->value.enumeration.variant_name = text.back().c_str();
            result->value.enumeration.variant_name_length =
                static_cast<std::int64_t>((*enumeration)->variantName.size());
            for (std::size_t index = 0; index < count; ++index) {
                const std::string name =
                    index < (*enumeration)->fieldNames.size()
                        ? (*enumeration)->fieldNames[index]
                        : std::string();
                text.push_back(name);
                encodedFields[index].name = text.back().c_str();
                encodedFields[index].name_length =
                    static_cast<std::int64_t>(name.size());
                encodedFields[index].type = "any";
                encodedFields[index].type_length = 3;
                encodedFields[index].constant = 0;
                encodedFields[index].value = encode(
                    (*enumeration)->payload[index], line, column, depth + 1);
            }
        }
        result->value.enumeration.fields = encodedFields.get();
        result->value.enumeration.count = static_cast<std::int64_t>(count);
        if (encodedFields != nullptr) {
            fields.push_back(std::move(encodedFields));
        }
    } else {
        throw SourceError("native value ABI does not support type '" +
                              typeNameOf(input) + "'",
                          line, column);
    }
    return result;
}

std::string decodeNativeText(const std::uint8_t* data, std::int64_t length,
                             int line, int column) {
    if (length < 0 || length > (1LL << 30) || (data == nullptr && length != 0)) {
        throw SourceError("native value contains an invalid text length", line,
                          column);
    }
    return length == 0
               ? std::string()
               : std::string(reinterpret_cast<const char*>(data),
                             static_cast<std::size_t>(length));
}

Value decodeNativeValue(const LynxerFfiValue* value, int line, int column,
                        std::size_t depth, std::size_t& remainingValues) {
    constexpr std::int64_t maxItems = 1 << 20;
    if (value == nullptr) {
        throw SourceError("native value result is null", line, column);
    }
    if (depth > 128) {
        throw SourceError("native value nesting exceeds 128 levels", line,
                          column);
    }
    if (remainingValues == 0) {
        throw SourceError("native value contains too many items", line, column);
    }
    --remainingValues;
    switch (value->tag) {
        case LYNXER_FFI_VALUE_NULL:
            return std::monostate{};
        case LYNXER_FFI_VALUE_INT64:
            return value->value.i;
        case LYNXER_FFI_VALUE_FLOAT64:
            return value->value.f;
        case LYNXER_FFI_VALUE_BOOL:
            return value->value.boolean != 0;
        case LYNXER_FFI_VALUE_STRING:
            return decodeNativeText(value->value.string.data,
                                    value->value.string.length, line, column);
        case LYNXER_FFI_VALUE_CHAR:
            return CharValue{decodeNativeText(value->value.string.data,
                                              value->value.string.length, line,
                                              column)};
        case LYNXER_FFI_VALUE_UINT64:
            return UInt64Value{value->value.u};
        case LYNXER_FFI_VALUE_BYTES: {
            const std::string raw = decodeNativeText(
                value->value.bytes.data, value->value.bytes.length, line,
                column);
            auto bytes = std::make_shared<BytesValue>();
            bytes->data.assign(raw.begin(), raw.end());
            return bytes;
        }
        case LYNXER_FFI_VALUE_ARRAY:
        case LYNXER_FFI_VALUE_TUPLE: {
            const auto& array = value->value.array;
            if (array.count < 0 || array.count > maxItems ||
                (array.items == nullptr && array.count != 0)) {
                throw SourceError("native value contains an invalid array",
                                  line, column);
            }
            if (value->tag == LYNXER_FFI_VALUE_ARRAY) {
                auto list = std::make_shared<List>();
                list->elements.reserve(static_cast<std::size_t>(array.count));
                for (std::int64_t index = 0; index < array.count; ++index) {
                    list->elements.push_back(decodeNativeValue(
                        &array.items[index], line, column, depth + 1,
                        remainingValues));
                }
                return list;
            }
            auto tuple = std::make_shared<Tuple>();
            tuple->elements.reserve(static_cast<std::size_t>(array.count));
            for (std::int64_t index = 0; index < array.count; ++index) {
                tuple->elements.push_back(decodeNativeValue(
                    &array.items[index], line, column, depth + 1,
                    remainingValues));
            }
            return tuple;
        }
        case LYNXER_FFI_VALUE_RECORD: {
            const auto& record = value->value.record;
            if (record.count < 0 || record.count > maxItems ||
                (record.fields == nullptr && record.count != 0) ||
                record.kind > LYNXER_FFI_RECORD_CLASS) {
                throw SourceError("native value contains an invalid record",
                                  line, column);
            }
            auto output = std::make_shared<RecordValue>();
            output->typeName = decodeNativeText(
                reinterpret_cast<const std::uint8_t*>(record.type_name),
                record.type_name_length, line, column);
            output->displayName = decodeNativeText(
                reinterpret_cast<const std::uint8_t*>(record.display_name),
                record.display_name_length, line, column);
            output->kind = static_cast<RecordKind>(record.kind);
            output->fields.reserve(static_cast<std::size_t>(record.count));
            for (std::int64_t index = 0; index < record.count; ++index) {
                const LynxerFfiValueField& field = record.fields[index];
                const std::string name = decodeNativeText(
                    reinterpret_cast<const std::uint8_t*>(field.name),
                    field.name_length, line, column);
                const std::string fieldType = decodeNativeText(
                    reinterpret_cast<const std::uint8_t*>(field.type),
                    field.type_length, line, column);
                output->fields.push_back(RecordField{
                    fieldType, name,
                    decodeNativeValue(field.value, line, column, depth + 1,
                                      remainingValues),
                    field.constant != 0});
            }
            return output;
        }
        case LYNXER_FFI_VALUE_ENUM: {
            const auto& enumeration = value->value.enumeration;
            if (enumeration.count < 0 || enumeration.count > maxItems ||
                (enumeration.fields == nullptr && enumeration.count != 0)) {
                throw SourceError("native value contains an invalid enum",
                                  line, column);
            }
            auto output = std::make_shared<EnumValue>();
            output->enumName = decodeNativeText(
                reinterpret_cast<const std::uint8_t*>(enumeration.enum_name),
                enumeration.enum_name_length, line, column);
            output->variantName = decodeNativeText(
                reinterpret_cast<const std::uint8_t*>(
                    enumeration.variant_name),
                enumeration.variant_name_length, line, column);
            output->fieldNames.reserve(
                static_cast<std::size_t>(enumeration.count));
            output->payload.reserve(static_cast<std::size_t>(enumeration.count));
            for (std::int64_t index = 0; index < enumeration.count; ++index) {
                const LynxerFfiValueField& field = enumeration.fields[index];
                output->fieldNames.push_back(decodeNativeText(
                    reinterpret_cast<const std::uint8_t*>(field.name),
                    field.name_length, line, column));
                output->payload.push_back(
                    decodeNativeValue(field.value, line, column, depth + 1,
                                      remainingValues));
            }
            return output;
        }
        default:
            throw SourceError("native value result has an unknown type tag",
                              line, column);
    }
}

} // namespace lynxer
