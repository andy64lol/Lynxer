#pragma once

#include "runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace clynxer {

// Bytecode container (magic "CLYXC\x00", format version 1) and instruction
// set for the clynxer stack-machine VM. This format is native to CLynxer and
// intentionally unrelated to the Python Lynxer ".lynxc" container, which
// encodes the Python AST class table.

inline constexpr char BYTECODE_MAGIC[6] = {'C', 'L', 'Y', 'X', 'C', '\0'};
inline constexpr uint8_t BYTECODE_FORMAT_VERSION = 1;
inline constexpr uint8_t BYTECODE_FLAG_OPTIMIZED = 0x01;
inline constexpr uint8_t BYTECODE_FLAG_SOURCE_FALLBACK = 0x02;

inline constexpr std::size_t MAX_BYTECODE_FILE_SIZE = 64ull * 1024 * 1024;
inline constexpr std::size_t MAX_CODE_SECTION_SIZE = 16ull * 1024 * 1024;
inline constexpr std::size_t MAX_TABLE_ENTRIES = 1000000;

enum class Op : uint8_t {
    Halt = 0x00,
    PushConst = 0x01,    // u: const index
    LoadVar = 0x02,      // u: name index
    DeclareVar = 0x03,   // u: name index, u: type index
    StoreVar = 0x04,     // u: name index
    Pop = 0x05,
    Neg = 0x06,
    Not = 0x07,
    Truthy = 0x08,
    Call = 0x09,         // u: builtin name index, u: argc
    BuildList = 0x0A,    // u: count
    BuildTuple = 0x0B,   // u: count
    ToString = 0x0C,
    Interp = 0x0D,       // u: segment count
    Jump = 0x0E,         // s: delta from operand end
    JumpIfFalse = 0x0F,  // s: delta, pops
    Add = 0x10,
    Sub = 0x11,
    Mul = 0x12,
    Div = 0x13,
    Mod = 0x14,
    Eq = 0x15,
    Neq = 0x16,
    Lt = 0x17,
    Le = 0x18,
    Gt = 0x19,
    Ge = 0x1A,
    JumpIfTrue = 0x1B,   // s: delta, pops
    IterInit = 0x1C,     // pops count, pushes loop counter
    IterNext = 0x1D,     // s: exit delta; pops counter on exit branch
    ForeverBegin = 0x1E, // u: site index, u8: warn flag
    ForeverSleep = 0x1F,
    BitAnd = 0x20,
    BitOr = 0x21,
    BitXor = 0x22,
    BitNand = 0x23,
    BitXnor = 0x24,
    BitNor = 0x25,
    Shl = 0x26,
    Shr = 0x27,
    Exp = 0x28,
    FloorDiv = 0x29,
    LogicNand = 0x2A,
    LogicNor = 0x2B,
    BitNot = 0x2C,
    Dup = 0x2D,
    TryBegin = 0x2E, // s: catch delta, u: catch variable name index
    TryEnd = 0x2F,
};

// A source position attached to one trapping instruction.
struct Trap {
    uint32_t instruction = 0;
    uint32_t line = 0;
    uint32_t column = 0;
};

struct CodeSection {
    std::vector<uint8_t> code;
    std::vector<Trap> traps;   // dense after load: one entry per instruction
    std::vector<uint32_t> instructionStarts;  // byte offset per static index
    uint32_t maxStack = 0;
};

struct CompiledProgram {
    uint8_t flags = 0;
    std::string sourcePath;
    uint64_t sourceHash = 0;
    std::string sourceText;
    std::vector<std::string> strings;
    std::vector<Value> constants;
    CodeSection setup;
    CodeSection main;
    uint32_t foreverSiteCount = 0;
};

// Malformed bytecode container or instruction stream.
struct BytecodeError : std::runtime_error {
    explicit BytecodeError(const std::string& message)
        : std::runtime_error(message) {}
};

// FNV-1a 64: a cache key only, not a security hash.
uint64_t fnv1a64(const std::string& text);

void appendVarint(std::vector<uint8_t>& out, uint64_t value);

void appendZigZag(std::vector<uint8_t>& out, int64_t value);

// Bytecode reader cursor with bounds checking. All reads throw
// BytecodeError on truncation.
class BytecodeReader {
public:
    explicit BytecodeReader(const std::vector<uint8_t>& data) : data_(data) {}

    std::size_t position() const { return position_; }

    std::size_t remaining() const { return data_.size() - position_; }

    uint8_t readByte();

    uint64_t readVarint();

    int64_t readZigZag();

    uint32_t readU32();

    uint64_t readU64();

    std::string readLengthString(); // u32 length + bytes

    std::string readVarintString(); // varint length + bytes

    std::vector<uint8_t> readBytes(std::size_t count);

private:
    [[noreturn]] void fail(const char* what) const;

    const std::vector<uint8_t>& data_;
    std::size_t position_ = 0;
};

} // namespace clynxer
