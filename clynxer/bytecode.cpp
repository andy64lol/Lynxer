#include "bytecode.hpp"

namespace clynxer {

uint64_t fnv1a64(const std::string& text) {
    uint64_t hash = 1469598103934665603ull;
    for (const unsigned char byte : text) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

void appendVarint(std::vector<uint8_t>& out, uint64_t value) {
    while (value >= 0x80) {
        out.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    out.push_back(static_cast<uint8_t>(value));
}

void appendZigZag(std::vector<uint8_t>& out, int64_t value) {
    const auto encoded =
        static_cast<uint64_t>((value << 1) ^ (value >> 63));
    appendVarint(out, encoded);
}

void BytecodeReader::fail(const char* what) const {
    throw BytecodeError(std::string("truncated or malformed bytecode (") +
                        what + ")");
}

uint8_t BytecodeReader::readByte() {
    if (position_ >= data_.size()) {
        fail("byte");
    }
    return data_[position_++];
}

uint64_t BytecodeReader::readVarint() {
    uint64_t value = 0;
    int shift = 0;
    for (;;) {
        const uint8_t byte = readByte();
        if (shift >= 64 || (shift == 63 && (byte & 0xFE) != 0)) {
            fail("varint overflow");
        }
        value |= static_cast<uint64_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            return value;
        }
        shift += 7;
    }
}

int64_t BytecodeReader::readZigZag() {
    const uint64_t encoded = readVarint();
    return static_cast<int64_t>((encoded >> 1) ^
                                (~(encoded & 1) + 1));
}

uint32_t BytecodeReader::readU32() {
    if (remaining() < 4) {
        fail("u32");
    }
    const uint32_t value = static_cast<uint32_t>(data_[position_]) |
                           (static_cast<uint32_t>(data_[position_ + 1]) << 8) |
                           (static_cast<uint32_t>(data_[position_ + 2]) << 16) |
                           (static_cast<uint32_t>(data_[position_ + 3]) << 24);
    position_ += 4;
    return value;
}

uint64_t BytecodeReader::readU64() {
    if (remaining() < 8) {
        fail("u64");
    }
    uint64_t value = 0;
    for (int index = 7; index >= 0; --index) {
        value = (value << 8) | data_[position_ + index];
    }
    position_ += 8;
    return value;
}

std::string BytecodeReader::readLengthString() {
    const uint32_t length = readU32();
    if (length > remaining()) {
        fail("string length");
    }
    std::string value(data_.begin() + position_,
                      data_.begin() + position_ + length);
    position_ += length;
    return value;
}

std::string BytecodeReader::readVarintString() {
    const uint64_t length = readVarint();
    if (length > remaining()) {
        fail("string length");
    }
    std::string value(data_.begin() + position_,
                      data_.begin() + position_ + length);
    position_ += static_cast<std::size_t>(length);
    return value;
}

std::vector<uint8_t> BytecodeReader::readBytes(std::size_t count) {
    if (count > remaining()) {
        fail("bytes");
    }
    std::vector<uint8_t> bytes(data_.begin() + position_,
                               data_.begin() + position_ + count);
    position_ += count;
    return bytes;
}

} // namespace clynxer
