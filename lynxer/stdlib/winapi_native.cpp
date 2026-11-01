#include "../ffi_abi.h"
#include "../runtime.hpp"
#include "../winapi.hpp"
#include "lynxer_native_abi.h"

#include "../winapi.cpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using RegisterFunction = int (*)(const char*, const char*, const char*);

namespace {

thread_local LynxerFfiValue result{};
thread_local std::string resultText;
thread_local std::string lastError;

const LynxerFfiValue* makeResult(std::uint32_t tag) {
    result = {};
    result.tag = tag;
    return &result;
}

bool unpackArguments(const LynxerFfiValue* packed,
                     std::vector<lynxer::Value>& arguments) {
    if (packed == nullptr ||
        packed->tag != LYNXER_FFI_VALUE_ARRAY ||
        packed->value.array.count < 0 ||
        (packed->value.array.count > 0 &&
         packed->value.array.items == nullptr)) {
        lastError = "expected an array of WinAPI arguments";
        return false;
    }
    arguments.reserve(static_cast<std::size_t>(packed->value.array.count));
    for (std::int64_t index = 0; index < packed->value.array.count; ++index) {
        const LynxerFfiValue& value = packed->value.array.items[index];
        switch (value.tag) {
            case LYNXER_FFI_VALUE_INT64:
                arguments.emplace_back(value.value.i);
                break;
            case LYNXER_FFI_VALUE_FLOAT64:
                arguments.emplace_back(value.value.f);
                break;
            case LYNXER_FFI_VALUE_BOOL:
                arguments.emplace_back(value.value.boolean != 0);
                break;
            case LYNXER_FFI_VALUE_STRING: {
                if (value.value.string.length < 0 ||
                    (value.value.string.length > 0 &&
                     value.value.string.data == nullptr)) {
                    lastError = "invalid string argument";
                    return false;
                }
                std::string text;
                if (value.value.string.length > 0) {
                    text.assign(
                        reinterpret_cast<const char*>(value.value.string.data),
                        static_cast<std::size_t>(value.value.string.length));
                }
                arguments.emplace_back(std::move(text));
                break;
            }
            default:
                lastError = "WinAPI arguments must be integers, strings, or booleans";
                return false;
        }
    }
    return true;
}

}  // namespace

extern "C" const char* winapi_native_architecture() {
#if defined(_WIN32) && (defined(_M_ARM64) || defined(__aarch64__))
    return "arm64";
#elif defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    return "amd64";
#else
    return "";
#endif
}

extern "C" const char* winapi_native_lastError() {
    return lastError.c_str();
}

extern "C" const LynxerFfiValue* winapi_native_call(
    const LynxerFfiValue* packedName, const LynxerFfiValue* packedArguments) {
    lastError.clear();
    if (packedName == nullptr ||
        packedName->tag != LYNXER_FFI_VALUE_STRING ||
        packedName->value.string.data == nullptr ||
        packedName->value.string.length < 0) {
        lastError = "expected a WinAPI operation name";
        return makeResult(LYNXER_FFI_VALUE_NULL);
    }
    const std::string name(
        reinterpret_cast<const char*>(packedName->value.string.data),
        static_cast<std::size_t>(packedName->value.string.length));
    std::vector<lynxer::Value> arguments;
    if (!unpackArguments(packedArguments, arguments)) {
        return makeResult(LYNXER_FFI_VALUE_NULL);
    }
    lynxer::Value value;
    std::string error;
    if (!lynxer::winapi::call(name, arguments, value, error)) {
        lastError = std::move(error);
        return makeResult(LYNXER_FFI_VALUE_NULL);
    }
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        result = {};
        result.tag = LYNXER_FFI_VALUE_INT64;
        result.value.i = *integer;
        return &result;
    }
    if (const auto* boolean = std::get_if<bool>(&value)) {
        result = {};
        result.tag = LYNXER_FFI_VALUE_BOOL;
        result.value.boolean = *boolean ? 1U : 0U;
        return &result;
    }
    if (const auto* unsignedInteger = std::get_if<lynxer::UInt64Value>(&value)) {
        result = {};
        result.tag = LYNXER_FFI_VALUE_UINT64;
        result.value.u = unsignedInteger->value;
        return &result;
    }
    if (const auto* text = std::get_if<std::string>(&value)) {
        resultText = *text;
        result = {};
        result.tag = LYNXER_FFI_VALUE_STRING;
        result.value.string.data =
            reinterpret_cast<const std::uint8_t*>(resultText.data());
        result.value.string.length =
            static_cast<std::int64_t>(resultText.size());
        return &result;
    }
    if (std::holds_alternative<std::monostate>(value)) {
        return makeResult(LYNXER_FFI_VALUE_NULL);
    }
    lastError = "WinAPI operation returned an unsupported value";
    return makeResult(LYNXER_FFI_VALUE_NULL);
}

extern "C" LYNXER_MODULE_EXPORT int lynxer_module_init_v1(
    RegisterFunction function, void (*)(const char*, std::int64_t),
    void (*)(const char*, const char*)) {
    return function("architecture", "winapi_native_architecture",
                    "cdecl:cstring()") &&
                   function("lastError", "winapi_native_lastError",
                            "cdecl:cstring()") &&
                   function("call", "winapi_native_call",
                            "cdecl:v2:value(value,value)")
               ? 0
               : 1;
}
