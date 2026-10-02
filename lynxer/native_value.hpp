#pragma once

#include "ffi_abi.h"
#include "runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace lynxer {

// Builds a recursive `LynxerFfiValue` tree from a Lynxer value for the v2
// `cdecl:v2:value(value)` ABI, keeping the backing text and arrays alive for
// the duration of a native call. Extracted from ast.cpp so the embedding
// runtime can reuse it without duplicating the conversion rules.
struct NativeAggregateStorage {
    std::size_t remainingValues = 1 << 20;
    std::deque<std::string> text;
    std::vector<std::unique_ptr<LynxerFfiValue>> nodes;
    std::vector<std::unique_ptr<LynxerFfiValue[]>> arrays;
    std::vector<std::unique_ptr<LynxerFfiValueField[]>> fields;

    const std::uint8_t* storeText(const std::string& value);
    const LynxerFfiValue* encode(const Value& input, int line, int column,
                                 std::size_t depth = 0);
};

// Decodes a length-delimited native text or byte blob, validating the bounds.
std::string decodeNativeText(const std::uint8_t* data, std::int64_t length,
                             int line, int column);

// Rebuilds a Lynxer value from a recursive `LynxerFfiValue` returned by a
// `cdecl:v2:value(value)` native call.
Value decodeNativeValue(const LynxerFfiValue* value, int line, int column,
                        std::size_t depth, std::size_t& remainingValues);

} // namespace lynxer
