#include "compiler.hpp"

#include "error.hpp"
#include "ops.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <sstream>

namespace clynxer {

namespace {

bool isArith(Op op) {
    return op == Op::Add || op == Op::Sub || op == Op::Mul || op == Op::Div ||
           op == Op::Mod || op == Op::Eq || op == Op::Neq || op == Op::Lt ||
           op == Op::Le || op == Op::Gt || op == Op::Ge ||
           op == Op::BitAnd || op == Op::BitOr || op == Op::BitXor ||
           op == Op::BitNand || op == Op::BitXnor || op == Op::BitNor ||
           op == Op::Shl || op == Op::Shr || op == Op::Exp ||
           op == Op::FloorDiv || op == Op::LogicNand || op == Op::LogicNor;
}

BinOp binOpOf(Op op) {
    switch (op) {
    case Op::Add: return BinOp::Add;
    case Op::Sub: return BinOp::Sub;
    case Op::Mul: return BinOp::Mul;
    case Op::Div: return BinOp::Div;
    case Op::Mod: return BinOp::Mod;
    case Op::Eq: return BinOp::Eq;
    case Op::Neq: return BinOp::Neq;
    case Op::Lt: return BinOp::Lt;
    case Op::Le: return BinOp::Le;
    case Op::Gt: return BinOp::Gt;
    case Op::Ge: return BinOp::Ge;
    case Op::BitAnd: return BinOp::BitAnd;
    case Op::BitOr: return BinOp::BitOr;
    case Op::BitXor: return BinOp::BitXor;
    case Op::BitNand: return BinOp::BitNand;
    case Op::BitXnor: return BinOp::BitXnor;
    case Op::BitNor: return BinOp::BitNor;
    case Op::Shl: return BinOp::Shl;
    case Op::Shr: return BinOp::Shr;
    case Op::Exp: return BinOp::Exp;
    case Op::FloorDiv: return BinOp::FloorDiv;
    case Op::LogicNand: return BinOp::LogicNand;
    case Op::LogicNor: return BinOp::LogicNor;
    default: return BinOp::Add;
    }
}

// Net stack effect of one instruction (fall-through path for branches).
int stackEffect(Op op, uint64_t a) {
    switch (op) {
    case Op::PushConst:
    case Op::LoadVar:
    case Op::IterInit:
        return 1;
    case Op::Dup:
        return 1;
    case Op::DeclareVar:
    case Op::StoreVar:
    case Op::Pop:
        return -1;
    case Op::Call:
        return -static_cast<int>(a) + 1;
    case Op::BuildList:
    case Op::BuildTuple:
    case Op::Interp:
        return -static_cast<int>(a) + 1;
    case Op::JumpIfFalse:
    case Op::JumpIfTrue:
        return -1;
    case Op::Add:
    case Op::Sub:
    case Op::Mul:
    case Op::Div:
    case Op::Mod:
    case Op::Eq:
    case Op::Neq:
    case Op::Lt:
    case Op::Le:
    case Op::Gt:
    case Op::Ge:
    case Op::BitAnd:
    case Op::BitOr:
    case Op::BitXor:
    case Op::BitNand:
    case Op::BitXnor:
    case Op::BitNor:
    case Op::Shl:
    case Op::Shr:
    case Op::Exp:
    case Op::FloorDiv:
    case Op::LogicNand:
    case Op::LogicNor:
        return -1;
    default:
        return 0;
    }
}

// One decoded instruction: opcode, its raw operand bytes, and span.
struct Decoded {
    Op op;
    std::size_t start;
    std::size_t end;
    std::vector<uint8_t> operands;
};

uint64_t readVarintAt(const std::vector<uint8_t>& code, std::size_t& cursor) {
    uint64_t value = 0;
    int shift = 0;
    for (;;) {
        if (cursor >= code.size()) {
            throw BytecodeError("truncated varint in instruction stream");
        }
        const uint8_t byte = code[cursor++];
        if (shift >= 64 || (shift == 63 && (byte & 0xFE) != 0)) {
            throw BytecodeError("varint overflow in instruction stream");
        }
        value |= static_cast<uint64_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            return value;
        }
        shift += 7;
    }
}

std::vector<Decoded> decodeInstructions(const std::vector<uint8_t>& code) {
    std::vector<Decoded> instructions;
    std::size_t cursor = 0;
    while (cursor < code.size()) {
        Decoded instruction;
        instruction.start = cursor;
        instruction.op = static_cast<Op>(code[cursor++]);
        switch (instruction.op) {
        case Op::PushConst:
        case Op::LoadVar:
        case Op::StoreVar:
        case Op::BuildList:
        case Op::BuildTuple:
        case Op::Interp: {
            const std::size_t operandStart = cursor;
            (void)readVarintAt(code, cursor);
            instruction.operands.assign(code.begin() + operandStart,
                                        code.begin() + cursor);
            break;
        }
        case Op::DeclareVar:
        case Op::Call: {
            const std::size_t operandStart = cursor;
            readVarintAt(code, cursor);
            readVarintAt(code, cursor);
            instruction.operands.assign(code.begin() + operandStart,
                                        code.begin() + cursor);
            break;
        }
        case Op::ForeverBegin: {
            const std::size_t operandStart = cursor;
            readVarintAt(code, cursor);
            if (cursor >= code.size()) {
                throw BytecodeError("truncated ForeverBegin operand");
            }
            ++cursor;
            instruction.operands.assign(code.begin() + operandStart,
                                        code.begin() + cursor);
            break;
        }
        case Op::TryBegin: {
            const std::size_t operandStart = cursor;
            if (cursor + 4 > code.size()) {
                throw BytecodeError("truncated TryBegin jump operand");
            }
            cursor += 4;
            (void)readVarintAt(code, cursor);
            instruction.operands.assign(code.begin() + operandStart,
                                        code.begin() + cursor);
            break;
        }
        case Op::Jump:
        case Op::JumpIfFalse:
        case Op::JumpIfTrue:
        case Op::IterNext: {
            if (cursor + 4 > code.size()) {
                throw BytecodeError("truncated jump operand");
            }
            instruction.operands.assign(code.begin() + cursor,
                                        code.begin() + cursor + 4);
            cursor += 4;
            break;
        }
        case Op::Halt:
        case Op::Pop:
        case Op::Neg:
        case Op::Not:
        case Op::BitNot:
        case Op::Dup:
        case Op::Truthy:
        case Op::ToString:
        case Op::IterInit:
        case Op::Add:
        case Op::Sub:
        case Op::Mul:
        case Op::Div:
        case Op::Mod:
        case Op::Eq:
        case Op::Neq:
        case Op::Lt:
        case Op::Le:
        case Op::Gt:
        case Op::Ge:
        case Op::BitAnd:
        case Op::BitOr:
        case Op::BitXor:
        case Op::BitNand:
        case Op::BitXnor:
        case Op::BitNor:
        case Op::Shl:
        case Op::Shr:
        case Op::Exp:
        case Op::FloorDiv:
        case Op::LogicNand:
        case Op::LogicNor:
        case Op::ForeverSleep:
        case Op::TryEnd:
            break;
        default:
            throw BytecodeError("unknown opcode 0x" + [&] {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "%02x",
                              static_cast<uint8_t>(instruction.op));
                return std::string(buffer);
            }());
        }
        instruction.end = cursor;
        instructions.push_back(std::move(instruction));
    }
    return instructions;
}

int32_t jumpDelta(const Decoded& instruction) {
    uint32_t raw = 0;
    for (int index = 3; index >= 0; --index) {
        raw = (raw << 8) | instruction.operands[index];
    }
    int32_t delta = 0;
    std::memcpy(&delta, &raw, sizeof(delta));
    return delta;
}

// One folding attempt: finds the first foldable pattern, rebuilds the
// section, and reports whether anything changed. Patterns:
//   PUSH_CONST a, PUSH_CONST b, arith  ->  PUSH_CONST result
//   PUSH_CONST a, NEG / NOT            ->  PUSH_CONST result
// A fold that would raise a runtime error (division by zero, '%' on a
// non-integer, a type error) is skipped so the VM raises the identical
// source-located error at run time. Blocked patterns are remembered so a
// failed fold is not retried forever.
bool attemptFold(CodeSection& section, std::vector<Value>& constants,
                 std::vector<std::size_t>& blocked) {
    const std::vector<Decoded> instructions = decodeInstructions(section.code);
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> trapByInstruction;
    for (const Trap& trap : section.traps) {
        trapByInstruction.emplace(trap.instruction,
                                  std::make_pair(trap.line, trap.column));
    }

    std::size_t anchor = 0;
    std::size_t patternLength = 0;
    Op patternOp = Op::Add;
    bool found = false;
    for (std::size_t index = 0; index + 1 < instructions.size(); ++index) {
        if (instructions[index].op != Op::PushConst) {
            continue;
        }
        const std::size_t blockedKeyStart = index;
        if (instructions[index + 1].op == Op::Neg ||
            instructions[index + 1].op == Op::Not ||
            instructions[index + 1].op == Op::BitNot) {
            patternOp = instructions[index + 1].op;
            patternLength = 2;
            if (std::find(blocked.begin(), blocked.end(), blockedKeyStart) ==
                blocked.end()) {
                anchor = index;
                found = true;
                break;
            }
            continue;
        }
        if (index + 2 < instructions.size() &&
            instructions[index + 1].op == Op::PushConst &&
            isArith(instructions[index + 2].op)) {
            patternOp = instructions[index + 2].op;
            patternLength = 3;
            if (std::find(blocked.begin(), blocked.end(), blockedKeyStart) ==
                blocked.end()) {
                anchor = index;
                found = true;
                break;
            }
        }
    }
    if (!found) {
        return false;
    }

    std::size_t cursor = 0;
    const uint64_t indexA =
        readVarintAt(instructions[anchor].operands, cursor);
    if (indexA >= constants.size()) {
        blocked.push_back(anchor);
        return true; // blocked, but progress was made
    }
    Value folded;
    if (patternLength == 3) {
        std::size_t cursorB = 0;
        const uint64_t indexB =
            readVarintAt(instructions[anchor + 1].operands, cursorB);
        if (indexB >= constants.size()) {
            blocked.push_back(anchor);
            return true;
        }
        const auto trapIt = trapByInstruction.find(
            static_cast<uint32_t>(anchor + 2));
        const int line =
            trapIt == trapByInstruction.end() ? 0 : static_cast<int>(trapIt->second.first);
        const int column = trapIt == trapByInstruction.end()
                               ? 0
                               : static_cast<int>(trapIt->second.second);
        try {
            folded = applyBinary(binOpOf(patternOp), constants[indexA],
                                 constants[indexB], line, column);
        } catch (const SourceError&) {
            blocked.push_back(anchor); // keep the runtime error
            return true;
        }
    } else {
        const auto trapIt =
            trapByInstruction.find(static_cast<uint32_t>(anchor + 1));
        const int line =
            trapIt == trapByInstruction.end() ? 0 : static_cast<int>(trapIt->second.first);
        const int column = trapIt == trapByInstruction.end()
                               ? 0
                               : static_cast<int>(trapIt->second.second);
        try {
            folded = applyUnary(patternOp == Op::Neg ? "-"
                                : patternOp == Op::BitNot ? "~" : "!!",
                                constants[indexA], line, column);
        } catch (const SourceError&) {
            blocked.push_back(anchor);
            return true;
        }
    }
    constants.push_back(folded);
    std::vector<uint8_t> foldedOperand;
    appendVarint(foldedOperand, static_cast<uint32_t>(constants.size() - 1));

    // Absolute jump targets before the rewrite.
    std::map<std::size_t, std::size_t> absoluteTarget;
    for (std::size_t position = 0; position < instructions.size(); ++position) {
        const Decoded& instruction = instructions[position];
        if (instruction.op == Op::Jump || instruction.op == Op::JumpIfFalse ||
            instruction.op == Op::JumpIfTrue || instruction.op == Op::IterNext) {
            absoluteTarget[position] = static_cast<std::size_t>(
                static_cast<int64_t>(instruction.end) +
                jumpDelta(instruction));
        }
    }

    // Rebuild the instruction list, dropping folded-away instructions and
    // recording the new start offset of every kept instruction.
    std::vector<uint8_t> rebuilt;
    std::map<std::size_t, std::size_t> newStartByOldIndex;
    std::map<std::size_t, std::size_t> newIndexByOldIndex;
    std::size_t newIndex = 0;
    for (std::size_t position = 0; position < instructions.size(); ++position) {
        if (position == anchor) {
            newStartByOldIndex[position] = rebuilt.size();
            newIndexByOldIndex[position] = newIndex++;
            rebuilt.push_back(static_cast<uint8_t>(Op::PushConst));
            rebuilt.insert(rebuilt.end(), foldedOperand.begin(),
                           foldedOperand.end());
            continue;
        }
        if (patternLength == 3 && (position == anchor + 1 || position == anchor + 2)) {
            continue;
        }
        if (patternLength == 2 && position == anchor + 1) {
            continue;
        }
        const Decoded& instruction = instructions[position];
        newStartByOldIndex[position] = rebuilt.size();
        newIndexByOldIndex[position] = newIndex++;
        rebuilt.push_back(static_cast<uint8_t>(instruction.op));
        rebuilt.insert(rebuilt.end(), instruction.operands.begin(),
                       instruction.operands.end());
    }

    // Rewrite jump operands: re-decode the rebuilt stream and re-emit each
    // jump's delta from its (unchanged semantics) absolute target.
    const std::vector<Decoded> rebuiltInstructions =
        decodeInstructions(rebuilt);
    {
        std::size_t rebuiltIndex = 0;
        for (std::size_t position = 0; position < instructions.size();
             ++position) {
            if (position == anchor) {
                ++rebuiltIndex;
                continue;
            }
            if (patternLength == 3 &&
                (position == anchor + 1 || position == anchor + 2)) {
                continue;
            }
            if (patternLength == 2 && position == anchor + 1) {
                continue;
            }
            const Decoded& instruction = instructions[position];
            if (instruction.op == Op::Jump ||
                instruction.op == Op::JumpIfFalse ||
                instruction.op == Op::JumpIfTrue ||
                instruction.op == Op::IterNext) {
                const std::size_t oldTarget = absoluteTarget[position];
                // Find the old instruction whose span contains the target.
                std::size_t targetOldIndex = instructions.size();
                for (std::size_t probe = 0; probe < instructions.size();
                     ++probe) {
                    if (instructions[probe].start == oldTarget) {
                        targetOldIndex = probe;
                        break;
                    }
                }
                if (targetOldIndex == instructions.size()) {
                    throw BytecodeError("jump target lands inside an instruction");
                }
                const std::size_t newTarget =
                    newStartByOldIndex.at(targetOldIndex);
                const Decoded& rebuiltJump = rebuiltInstructions[rebuiltIndex];
                const int32_t newDelta = static_cast<int32_t>(
                    static_cast<int64_t>(newTarget) -
                    static_cast<int64_t>(rebuiltJump.end));
                std::memcpy(rebuilt.data() +
                                static_cast<std::ptrdiff_t>(rebuiltJump.start + 1),
                            &newDelta, sizeof(newDelta));
            }
            ++rebuiltIndex;
        }
    }

    // Reindex traps; traps on folded-away instructions disappear.
    std::vector<Trap> rebuiltTraps;
    for (const auto& entry : trapByInstruction) {
        const auto mapped = newIndexByOldIndex.find(entry.first);
        if (mapped == newIndexByOldIndex.end()) {
            continue;
        }
        Trap trap;
        trap.instruction = static_cast<uint32_t>(mapped->second);
        trap.line = entry.second.first;
        trap.column = entry.second.second;
        rebuiltTraps.push_back(trap);
    }

    section.code = std::move(rebuilt);
    section.traps = std::move(rebuiltTraps);
    return true;
}

void foldConstants(CodeSection& section, std::vector<Value>& constants) {
    if (section.code.empty()) {
        return;
    }
    std::vector<std::size_t> blocked;
    // Bounded by instruction count: each fold removes at least one
    // instruction, and blocked patterns stop rescans.
    const std::size_t limit = section.code.size() * 2 + 16;
    for (std::size_t iteration = 0; iteration < limit; ++iteration) {
        if (!attemptFold(section, constants, blocked)) {
            break;
        }
    }
    const std::vector<Decoded> instructions = decodeInstructions(section.code);
    std::map<std::size_t, std::size_t> byOffset;
    for (std::size_t index = 0; index < instructions.size(); ++index) {
        byOffset[instructions[index].start] = index;
    }
    std::vector<std::pair<std::size_t, int>> work{{0, 0}};
    std::map<std::size_t, int> seen;
    int maximum = 0;
    while (!work.empty()) {
        const auto [offset, depth] = work.back();
        work.pop_back();
        if (seen.count(offset) != 0) {
            continue;
        }
        seen[offset] = depth;
        const auto found = byOffset.find(offset);
        if (found == byOffset.end()) {
            continue;
        }
        const Decoded& instruction = instructions[found->second];
        maximum = std::max(maximum, depth);
        if (instruction.op == Op::Halt) {
            continue;
        }
        if (instruction.op == Op::Jump) {
            work.emplace_back(
                static_cast<std::size_t>(static_cast<int64_t>(instruction.end) +
                                          jumpDelta(instruction)),
                depth);
            continue;
        }
        if (instruction.op == Op::JumpIfFalse ||
            instruction.op == Op::JumpIfTrue) {
            const int nextDepth = depth - 1;
            work.emplace_back(instruction.end, nextDepth);
            work.emplace_back(
                static_cast<std::size_t>(static_cast<int64_t>(instruction.end) +
                                          jumpDelta(instruction)),
                nextDepth);
            continue;
        }
        if (instruction.op == Op::IterNext) {
            work.emplace_back(instruction.end, depth);
            work.emplace_back(
                static_cast<std::size_t>(static_cast<int64_t>(instruction.end) +
                                          jumpDelta(instruction)),
                depth - 2);
            continue;
        }
        if (instruction.op == Op::TryBegin) {
            work.emplace_back(instruction.end, depth);
            work.emplace_back(
                static_cast<std::size_t>(static_cast<int64_t>(instruction.end) +
                                          jumpDelta(instruction)),
                depth);
            continue;
        }
        uint64_t operand = 0;
        if (!instruction.operands.empty()) {
            std::size_t cursor = 0;
            operand = readVarintAt(instruction.operands, cursor);
            if (instruction.op == Op::Call) {
                operand = readVarintAt(instruction.operands, cursor);
            }
        }
        work.emplace_back(instruction.end, depth + stackEffect(instruction.op,
                                                                operand));
    }
    section.maxStack = static_cast<uint32_t>(std::max(0, maximum));
}

} // namespace

// --- ProgramEmitter -----------------------------------------------------------

ProgramEmitter::ProgramEmitter(CodeSection& section,
                               std::vector<std::string>& strings,
                               std::vector<Value>& constants)
    : section_(section), strings_(strings), constants_(constants) {}

uint32_t ProgramEmitter::internString(const std::string& text) {
    const auto found = stringIndices_.find(text);
    if (found != stringIndices_.end()) {
        return found->second;
    }
    const uint32_t index = static_cast<uint32_t>(strings_.size());
    strings_.push_back(text);
    stringIndices_.emplace(text, index);
    return index;
}

uint32_t ProgramEmitter::internConstant(const Value& value) {
    // Keys on the encoded variant representation; constants are scalars.
    std::string key;
    switch (value.index()) {
    case 0: key = "n"; break;
    case 1:
        key = "i" + std::to_string(std::get<std::int64_t>(value));
        break;
    case 2: {
        char buffer[32];
        const double number = std::get<double>(value);
        std::memcpy(buffer, &number, sizeof(number));
        key = std::string("d") + std::string(buffer, sizeof(number));
        break;
    }
    case 3: key = std::get<bool>(value) ? "bt" : "bf"; break;
    case 4: key = "s" + std::get<std::string>(value); break;
    default:
        throw std::runtime_error("constant type cannot be encoded");
    }
    const auto found = constantIndices_.find(key);
    if (found != constantIndices_.end()) {
        return found->second;
    }
    const uint32_t index = static_cast<uint32_t>(constants_.size());
    constants_.push_back(value);
    constantIndices_.emplace(key, index);
    return index;
}

uint32_t ProgramEmitter::newForeverSite() { return nextForeverSite_++; }

uint32_t ProgramEmitter::foreverSites() const { return nextForeverSite_; }

std::size_t ProgramEmitter::offset() const { return section_.code.size(); }

void ProgramEmitter::noteDepth(int delta) {
    depth_ += delta;
    if (depth_ < 0) {
        throw std::runtime_error("internal error: negative operand stack depth");
    }
    depthLimit_ = std::max(depthLimit_, static_cast<uint32_t>(depth_));
    section_.maxStack = depthLimit_;
}

void ProgramEmitter::emit(Op op) {
    section_.code.push_back(static_cast<uint8_t>(op));
    noteDepth(stackEffect(op, 0));
}

void ProgramEmitter::emitPushConst(uint32_t constantIndex) {
    section_.code.push_back(static_cast<uint8_t>(Op::PushConst));
    appendVarint(section_.code, constantIndex);
    noteDepth(stackEffect(Op::PushConst, 0));
}

void ProgramEmitter::emitOneVar(Op op, uint32_t a) {
    const std::size_t start = offset();
    section_.code.push_back(static_cast<uint8_t>(op));
    appendVarint(section_.code, a);
    noteDepth(stackEffect(op, a));
    (void)start;
}

void ProgramEmitter::emitTwoVars(Op op, uint32_t a, uint32_t b) {
    section_.code.push_back(static_cast<uint8_t>(op));
    appendVarint(section_.code, a);
    appendVarint(section_.code, b);
    // For CALL the depth effect depends on the argument count (b), not the
    // name index (a).
    noteDepth(stackEffect(op, op == Op::Call ? b : a));
}

void ProgramEmitter::emitForeverBegin(uint32_t site, bool warn) {
    section_.code.push_back(static_cast<uint8_t>(Op::ForeverBegin));
    appendVarint(section_.code, site);
    section_.code.push_back(warn ? 1 : 0);
}

std::size_t ProgramEmitter::emitTryBegin(uint32_t catchNameIndex) {
    section_.code.push_back(static_cast<uint8_t>(Op::TryBegin));
    const std::size_t operandOffset = section_.code.size();
    section_.code.insert(section_.code.end(), 4, 0);
    appendVarint(section_.code, catchNameIndex);
    return operandOffset;
}

void ProgramEmitter::patchTryBegin(std::size_t operandOffset,
                                   std::size_t target) {
    std::size_t cursor = operandOffset + 4;
    while (cursor < section_.code.size() &&
           (section_.code[cursor++] & 0x80) != 0) {
    }
    const int32_t delta =
        static_cast<int32_t>(static_cast<int64_t>(target) -
                             static_cast<int64_t>(cursor));
    std::memcpy(section_.code.data() +
                    static_cast<std::ptrdiff_t>(operandOffset),
                &delta, sizeof(delta));
}

std::size_t ProgramEmitter::emitJump(Op op) {
    const std::size_t operandOffset = offset() + 1;
    section_.code.push_back(static_cast<uint8_t>(op));
    for (int index = 0; index < 4; ++index) {
        section_.code.push_back(0);
    }
    noteDepth(stackEffect(op, 0));
    return operandOffset;
}

void ProgramEmitter::patchJump(std::size_t operandOffset,
                               std::size_t target) {
    const int32_t delta =
        static_cast<int32_t>(static_cast<int64_t>(target) -
                             static_cast<int64_t>(operandOffset + 4));
    std::memcpy(section_.code.data() +
                    static_cast<std::ptrdiff_t>(operandOffset),
                &delta, sizeof(delta));
}

void ProgramEmitter::trap(int line, int column) {
    // Attached to the instruction that ends at the current offset.
    if (section_.code.empty()) {
        return;
    }
    uint32_t instruction = 0;
    try {
        instruction = static_cast<uint32_t>(
            decodeInstructions(section_.code).size() - 1);
    } catch (const BytecodeError&) {
        return;
    }
    for (const Trap& existing : section_.traps) {
        if (existing.instruction == instruction) {
            return; // keep the first (innermost expression) position
        }
    }
    Trap recorded;
    recorded.instruction = instruction;
    recorded.line = static_cast<uint32_t>(line);
    recorded.column = static_cast<uint32_t>(column);
    section_.traps.push_back(recorded);
}

void ProgramEmitter::setDepth(int depth) {
    if (depth < 0) {
        throw std::runtime_error("internal error: negative operand stack depth");
    }
    depth_ = depth;
}

void ProgramEmitter::pushLoop(LoopKind kind) {
    loops_.emplace_back();
    loops_.back().kind = kind;
}

void ProgramEmitter::patchContinues(std::size_t target) {
    for (const std::size_t patch : loops_.back().continuePatches) {
        patchJump(patch, target);
    }
    loops_.back().continuePatches.clear();
}

void ProgramEmitter::closeLoop(std::size_t breakTarget) {
    for (const std::size_t patch : loops_.back().breakPatches) {
        patchJump(patch, breakTarget);
    }
    loops_.pop_back();
}

void ProgramEmitter::recordBreak() {
    if (!loops_.empty() && loops_.back().kind == LoopKind::Iterate) {
        // The loop keeps count and counter on the operand stack; a break
        // must discard them before jumping to the exit.
        emit(Op::Pop);
        emit(Op::Pop);
    }
    loops_.back().breakPatches.push_back(emitJump(Op::Jump));
}

void ProgramEmitter::recordContinue() {
    loops_.back().continuePatches.push_back(emitJump(Op::Jump));
}

// --- AST node compilation -------------------------------------------------------

void LiteralExpression::compile(ProgramEmitter& emitter) const {
    emitter.emitPushConst(emitter.internConstant(value_));
}

void VariableExpression::compile(ProgramEmitter& emitter) const {
    emitter.emitOneVar(Op::LoadVar, emitter.internString(name_));
    emitter.trap(line_, column_);
}

void UnaryExpression::compile(ProgramEmitter& emitter) const {
    operand_->compile(emitter);
    emitter.emit(operation_ == "-" ? Op::Neg
                  : operation_ == "~" ? Op::BitNot : Op::Not);
    emitter.trap(line_, column_);
}

void BinaryExpression::compile(ProgramEmitter& emitter) const {
    if (operation_ == "&&" || operation_ == "||") {
        left_->compile(emitter);
        emitter.emit(Op::Truthy);
        const std::size_t shortCircuit = emitter.emitJump(
            operation_ == "&&" ? Op::JumpIfFalse : Op::JumpIfTrue);
        right_->compile(emitter);
        emitter.emit(Op::Truthy);
        const std::size_t end = emitter.emitJump(Op::Jump);
        emitter.patchJump(shortCircuit, emitter.offset());
        emitter.setDepth(0);
        emitter.emitPushConst(
            emitter.internConstant(operation_ == "&&" ? Value{false}
                                                       : Value{true}));
        emitter.patchJump(end, emitter.offset());
        return;
    }
    left_->compile(emitter);
    right_->compile(emitter);
    Op op = Op::Add;
    if (operation_ == "+") op = Op::Add;
    else if (operation_ == "-") op = Op::Sub;
    else if (operation_ == "*") op = Op::Mul;
    else if (operation_ == "/") op = Op::Div;
    else if (operation_ == "%") op = Op::Mod;
    else if (operation_ == "==") op = Op::Eq;
    else if (operation_ == "!=") op = Op::Neq;
    else if (operation_ == "<") op = Op::Lt;
    else if (operation_ == "<=") op = Op::Le;
    else if (operation_ == ">") op = Op::Gt;
    else if (operation_ == ">=") op = Op::Ge;
    else if (operation_ == "&") op = Op::BitAnd;
    else if (operation_ == "|") op = Op::BitOr;
    else if (operation_ == "^") op = Op::BitXor;
    else if (operation_ == "!&") op = Op::BitNand;
    else if (operation_ == "!^") op = Op::BitXnor;
    else if (operation_ == "!|") op = Op::BitNor;
    else if (operation_ == "<<") op = Op::Shl;
    else if (operation_ == ">>") op = Op::Shr;
    else if (operation_ == "**") op = Op::Exp;
    else if (operation_ == "/%") op = Op::FloorDiv;
    else if (operation_ == "!&&") op = Op::LogicNand;
    else if (operation_ == "!||") op = Op::LogicNor;
    else if (operation_ == "is") op = Op::Eq;
    else if (operation_ == "not is") op = Op::Neq;
    emitter.emit(op);
    emitter.trap(line_, column_);
}

void CallExpression::compile(ProgramEmitter& emitter) const {
    for (const auto& argument : arguments_) {
        argument->compile(emitter);
    }
    emitter.emitTwoVars(Op::Call, emitter.internString(name_),
                        static_cast<uint32_t>(arguments_.size()));
    emitter.trap(line_, column_);
}

void ListLiteralExpression::compile(ProgramEmitter& emitter) const {
    for (const auto& element : elements_) {
        element->compile(emitter);
    }
    emitter.emitOneVar(Op::BuildList,
                       static_cast<uint32_t>(elements_.size()));
}

void TupleLiteralExpression::compile(ProgramEmitter& emitter) const {
    for (const auto& element : elements_) {
        element->compile(emitter);
    }
    emitter.emitOneVar(Op::BuildTuple,
                       static_cast<uint32_t>(elements_.size()));
}

void InterpStringExpression::compile(ProgramEmitter& emitter) const {
    for (std::size_t index = 0; index < expressions_.size(); ++index) {
        emitter.emitPushConst(emitter.internConstant(literals_[index]));
        expressions_[index]->compile(emitter);
        emitter.emit(Op::ToString);
    }
    emitter.emitPushConst(emitter.internConstant(literals_.back()));
    emitter.emitOneVar(Op::Interp,
                       static_cast<uint32_t>(expressions_.size() * 2 + 1));
}

void DeclarationStatement::compile(ProgramEmitter& emitter) const {
    if (isConstant()) {
        throw SourceError(
            "bytecode compiler does not support const declarations", 0, 0);
    }
    value_->compile(emitter);
    emitter.emitTwoVars(Op::DeclareVar, emitter.internString(name_),
                        emitter.internString(type_));
    emitter.trap(line_, column_);
}

void AssignmentStatement::compile(ProgramEmitter& emitter) const {
    value_->compile(emitter);
    emitter.emitOneVar(Op::StoreVar, emitter.internString(name_));
    emitter.trap(line_, column_);
}

void ExpressionStatement::compile(ProgramEmitter& emitter) const {
    expression_->compile(emitter);
    emitter.emit(Op::Pop);
}

void LoopControlStatement::compile(ProgramEmitter& emitter) const {
    if (kind_ == LoopControlKind::Break) {
        emitter.recordBreak();
    } else {
        emitter.recordContinue();
    }
}

void IfStatement::compile(ProgramEmitter& emitter) const {
    condition_->compile(emitter);
    const std::size_t elseJump = emitter.emitJump(Op::JumpIfFalse);
    for (const auto& statement : thenStatements_) {
        statement->compile(emitter);
    }
    if (hasElse_) {
        const std::size_t endJump = emitter.emitJump(Op::Jump);
        emitter.patchJump(elseJump, emitter.offset());
        for (const auto& statement : elseStatements_) {
            statement->compile(emitter);
        }
        emitter.patchJump(endJump, emitter.offset());
    } else {
        emitter.patchJump(elseJump, emitter.offset());
    }
}

void WhileStatement::compile(ProgramEmitter& emitter) const {
    const std::size_t conditionStart = emitter.offset();
    condition_->compile(emitter);
    const std::size_t exitJump = emitter.emitJump(Op::JumpIfFalse);
    emitter.pushLoop();
    for (const auto& statement : statements_) {
        statement->compile(emitter);
    }
    emitter.patchContinues(conditionStart);
    const std::size_t backJumpOperand = emitter.emitJump(Op::Jump);
    emitter.patchJump(backJumpOperand, conditionStart);
    const std::size_t exit = emitter.offset();
    emitter.patchJump(exitJump, exit);
    emitter.closeLoop(exit);
}

void ForStatement::compile(ProgramEmitter& emitter) const {
    initializer_->compile(emitter);
    const std::size_t conditionStart = emitter.offset();
    condition_->compile(emitter);
    const std::size_t exitJump = emitter.emitJump(Op::JumpIfFalse);
    emitter.pushLoop();
    for (const auto& statement : statements_) {
        statement->compile(emitter);
    }
    emitter.patchContinues(emitter.offset()); // continue jumps to the update
    update_->compile(emitter);
    const std::size_t backJumpOperand = emitter.emitJump(Op::Jump);
    emitter.patchJump(backJumpOperand, conditionStart);
    const std::size_t exit = emitter.offset();
    emitter.patchJump(exitJump, exit);
    emitter.closeLoop(exit);
}

void DoWhileStatement::compile(ProgramEmitter& emitter) const {
    const std::size_t bodyStart = emitter.offset();
    emitter.pushLoop();
    for (const auto& statement : statements_) {
        statement->compile(emitter);
    }
    emitter.patchContinues(emitter.offset()); // continue checks the condition
    if (condition_ != nullptr) {
        condition_->compile(emitter);
        const std::size_t backJumpOperand = emitter.emitJump(Op::JumpIfTrue);
        emitter.patchJump(backJumpOperand, bodyStart);
    } else {
        const std::size_t backJumpOperand = emitter.emitJump(Op::Jump);
        emitter.patchJump(backJumpOperand, bodyStart);
    }
    const std::size_t exit = emitter.offset();
    emitter.closeLoop(exit);
}

void IterateStatement::compile(ProgramEmitter& emitter) const {
    count_->compile(emitter);
    emitter.emit(Op::IterInit);
    emitter.trap(line_, column_);
    const std::size_t iterNext = emitter.offset();
    const std::size_t exitJump = emitter.emitJump(Op::IterNext);
    emitter.pushLoop(ProgramEmitter::LoopKind::Iterate);
    for (const auto& statement : statements_) {
        statement->compile(emitter);
    }
    const std::size_t backJumpOperand = emitter.emitJump(Op::Jump);
    emitter.patchJump(backJumpOperand, iterNext);
    const std::size_t exit = emitter.offset();
    emitter.patchJump(exitJump, exit);
    emitter.patchContinues(iterNext);
    emitter.closeLoop(exit);
}

void ForeverStatement::compile(ProgramEmitter& emitter) const {
    const uint32_t site = emitter.newForeverSite();
    emitter.emitForeverBegin(site, statementsContainBreak(statements_) ? 0 : 1);
    const std::size_t bodyStart = emitter.offset();
    emitter.pushLoop();
    for (const auto& statement : statements_) {
        statement->compile(emitter);
    }
    const std::size_t sleepPosition = emitter.offset();
    emitter.emit(Op::ForeverSleep);
    emitter.patchContinues(sleepPosition);
    const std::size_t backJumpOperand = emitter.emitJump(Op::Jump);
    emitter.patchJump(backJumpOperand, bodyStart);
    const std::size_t exit = emitter.offset();
    emitter.closeLoop(exit);
}

} // namespace clynxer

// Reopen for the driver + serialization + loader + disassembler.
namespace clynxer {

CompiledProgram compileProgram(
    const std::unordered_map<std::string, Function>& functions,
    const std::string& sourcePath, const std::string& source, bool optimize) {
    CompiledProgram program;
    program.flags = optimize ? BYTECODE_FLAG_OPTIMIZED : 0;
    program.sourcePath = sourcePath;
    program.sourceHash = fnv1a64(source);

    CodeSection setupSection;
    CodeSection mainSection;
    ProgramEmitter setupEmitter(setupSection, program.strings,
                                program.constants);
    ProgramEmitter mainEmitter(mainSection, program.strings,
                               program.constants);

    const auto setup = functions.find("setup");
    if (setup != functions.end()) {
        for (const auto& statement : setup->second.statements) {
            statement->compile(setupEmitter);
        }
    }
    setupEmitter.emit(Op::Halt);

    const auto main = functions.find("main");
    if (main != functions.end()) {
        for (const auto& statement : main->second.statements) {
            statement->compile(mainEmitter);
        }
    }
    mainEmitter.emit(Op::Halt);

    program.foreverSiteCount =
        std::max(setupEmitter.foreverSites(), mainEmitter.foreverSites());

    if (optimize) {
        foldConstants(setupSection, program.constants);
        foldConstants(mainSection, program.constants);
    }

    program.setup = std::move(setupSection);
    program.main = std::move(mainSection);
    return program;
}

std::vector<uint8_t> serializeProgram(const CompiledProgram& program) {
    std::vector<uint8_t> out;
    out.insert(out.end(), BYTECODE_MAGIC,
               BYTECODE_MAGIC + sizeof(BYTECODE_MAGIC));
    out.push_back(BYTECODE_FORMAT_VERSION);
    out.push_back(program.flags);

    appendVarint(out, program.sourcePath.size());
    out.insert(out.end(), program.sourcePath.begin(),
               program.sourcePath.end());
    for (int index = 0; index < 8; ++index) {
        out.push_back(static_cast<uint8_t>((program.sourceHash >> (8 * index)) &
                                           0xFF));
    }

    appendVarint(out, program.strings.size());
    for (const std::string& text : program.strings) {
        appendVarint(out, text.size());
        out.insert(out.end(), text.begin(), text.end());
    }

    appendVarint(out, program.constants.size());
    for (const Value& value : program.constants) {
        switch (value.index()) {
        case 0:
            out.push_back(0);
            break;
        case 1:
            out.push_back(1);
            appendZigZag(out, std::get<std::int64_t>(value));
            break;
        case 2: {
            out.push_back(2);
            const double number = std::get<double>(value);
            uint64_t raw = 0;
            std::memcpy(&raw, &number, sizeof(raw));
            for (int index = 0; index < 8; ++index) {
                out.push_back(static_cast<uint8_t>((raw >> (8 * index)) & 0xFF));
            }
            break;
        }
        case 3:
            out.push_back(3);
            out.push_back(std::get<bool>(value) ? 1 : 0);
            break;
        case 4: {
            out.push_back(4);
            const std::string& text = std::get<std::string>(value);
            appendVarint(out, text.size());
            out.insert(out.end(), text.begin(), text.end());
            break;
        }
        default:
            throw std::runtime_error("constant type cannot be serialized");
        }
    }

    const auto writeSection = [&out](const CodeSection& section) {
        appendVarint(out, section.code.size());
        out.insert(out.end(), section.code.begin(), section.code.end());
        appendVarint(out, section.traps.size());
        for (const Trap& trap : section.traps) {
            appendVarint(out, trap.instruction);
            appendVarint(out, trap.line);
            appendVarint(out, trap.column);
        }
        appendVarint(out, section.maxStack);
    };
    writeSection(program.setup);
    writeSection(program.main);
    return out;
}

namespace {

void validateSection(CodeSection& section, const char* name) {
    if (section.code.size() > MAX_CODE_SECTION_SIZE) {
        throw BytecodeError(std::string(name) + " code section is too large");
    }
    const std::vector<Decoded> instructions = decodeInstructions(section.code);

    std::vector<std::pair<uint32_t, uint32_t>> positions(
        instructions.size(), {0, 0});
    for (const Trap& trap : section.traps) {
        if (trap.instruction >= instructions.size()) {
            throw BytecodeError("trap points outside the code section");
        }
        positions[trap.instruction] = {trap.line, trap.column};
    }

    // Flow validation: consistent stack depths, boundary jump targets.
    std::map<std::size_t, int> seenDepth;
    std::vector<std::pair<std::size_t, int>> worklist;
    worklist.emplace_back(0, 0);
    int observedMax = 0;
    std::map<std::size_t, std::size_t> startByOffset;
    for (std::size_t index = 0; index < instructions.size(); ++index) {
        startByOffset[instructions[index].start] = index;
    }
    while (!worklist.empty()) {
        auto [offset, depth] = worklist.back();
        worklist.pop_back();
        const auto found = startByOffset.find(offset);
        if (found == startByOffset.end()) {
            throw BytecodeError("jump target lands inside an instruction");
        }
        const auto existing = seenDepth.find(offset);
        if (existing != seenDepth.end()) {
            if (existing->second != depth) {
                throw BytecodeError("inconsistent operand stack depth at 0x" +
                                    [&] {
                                        char buffer[16];
                                        std::snprintf(buffer, sizeof(buffer),
                                                      "%zx", offset);
                                        return std::string(buffer);
                                    }() +
                                    " (seen " +
                                    std::to_string(existing->second) +
                                    ", now " + std::to_string(depth) + ")");
            }
            continue;
        }
        seenDepth.emplace(offset, depth);
        const Decoded& instruction = instructions[found->second];
        if (instruction.op == Op::Halt) {
            if (depth != 0) {
                throw BytecodeError("operand stack is not empty at HALT");
            }
            continue;
        }
        const auto successor = [&worklist, &seenDepth, &observedMax](
                                   std::size_t target, int newDepth) {
            if (newDepth < 0) {
                throw BytecodeError("operand stack underflow");
            }
            observedMax = std::max(observedMax, newDepth);
            worklist.emplace_back(target, newDepth);
        };
        observedMax = std::max(observedMax, depth);
        switch (instruction.op) {
        case Op::Jump: {
            const int64_t target = static_cast<int64_t>(instruction.end) +
                                   jumpDelta(instruction);
            if (target < 0 ||
                target > static_cast<int64_t>(section.code.size())) {
                throw BytecodeError("jump target out of range");
            }
            successor(static_cast<std::size_t>(target), depth);
            break;
        }
        case Op::JumpIfFalse:
        case Op::JumpIfTrue: {
            const int64_t target = static_cast<int64_t>(instruction.end) +
                                   jumpDelta(instruction);
            if (target < 0 ||
                target > static_cast<int64_t>(section.code.size())) {
                throw BytecodeError("jump target out of range");
            }
            successor(static_cast<std::size_t>(target), depth - 1);
            successor(instruction.end, depth - 1);
            break;
        }
        case Op::IterNext: {
            const int64_t target = static_cast<int64_t>(instruction.end) +
                                   jumpDelta(instruction);
            if (target < 0 ||
                target > static_cast<int64_t>(section.code.size())) {
                throw BytecodeError("jump target out of range");
            }
            successor(instruction.end, depth);
            successor(static_cast<std::size_t>(target), depth - 2);
            break;
        }
        case Op::TryBegin: {
            const int64_t target = static_cast<int64_t>(instruction.end) +
                                   jumpDelta(instruction);
            if (target < 0 ||
                target > static_cast<int64_t>(section.code.size())) {
                throw BytecodeError("try catch target out of range");
            }
            successor(static_cast<std::size_t>(target), depth);
            successor(instruction.end, depth);
            break;
        }
        default: {
            int effect = 0;
            switch (instruction.op) {
            case Op::Call: {
                std::size_t cursor = 0;
                readVarintAt(instruction.operands, cursor);
                const uint64_t argc =
                    readVarintAt(instruction.operands, cursor);
                effect = -static_cast<int>(argc) + 1;
                break;
            }
            case Op::BuildList:
            case Op::BuildTuple:
            case Op::Interp: {
                std::size_t cursor = 0;
                const uint64_t count =
                    readVarintAt(instruction.operands, cursor);
                effect = -static_cast<int>(count) + 1;
                break;
            }
            case Op::PushConst:
            case Op::LoadVar:
            case Op::IterInit:
            case Op::Dup:
                effect = 1;
                break;
            case Op::DeclareVar:
            case Op::StoreVar:
            case Op::Pop:
            case Op::Add:
            case Op::Sub:
            case Op::Mul:
            case Op::Div:
            case Op::Mod:
            case Op::Eq:
            case Op::Neq:
            case Op::Lt:
            case Op::Le:
            case Op::Gt:
            case Op::Ge:
            case Op::BitAnd:
            case Op::BitOr:
            case Op::BitXor:
            case Op::BitNand:
            case Op::BitXnor:
            case Op::BitNor:
            case Op::Shl:
            case Op::Shr:
            case Op::Exp:
            case Op::FloorDiv:
            case Op::LogicNand:
            case Op::LogicNor:
                effect = -1;
                break;
            default:
                effect = 0;
                break;
            }
            successor(instruction.end, depth + effect);
            break;
        }
        }
    }
    if (observedMax != static_cast<int>(section.maxStack)) {
        throw BytecodeError("operand stack depth does not match maxStack");
    }

    // Expand traps to a dense per-instruction table for the VM.
    section.traps = {};
    CodeSection dense;
    dense.code = std::move(section.code);
    dense.maxStack = section.maxStack;
    for (std::size_t index = 0; index < instructions.size(); ++index) {
        Trap trap;
        trap.instruction = static_cast<uint32_t>(index);
        trap.line = positions[index].first;
        trap.column = positions[index].second;
        dense.traps.push_back(trap);
        dense.instructionStarts.push_back(
            static_cast<uint32_t>(instructions[index].start));
    }
    section = std::move(dense);
}

} // namespace

CompiledProgram loadProgram(const std::vector<uint8_t>& bytes) {
    if (bytes.size() > MAX_BYTECODE_FILE_SIZE) {
        throw BytecodeError("bytecode file is too large");
    }
    if (bytes.size() < sizeof(BYTECODE_MAGIC) ||
        std::memcmp(bytes.data(), BYTECODE_MAGIC, sizeof(BYTECODE_MAGIC)) !=
            0) {
        throw BytecodeError("bad magic bytes");
    }
    BytecodeReader reader(bytes);
    reader.readBytes(sizeof(BYTECODE_MAGIC));
    const uint8_t version = reader.readByte();
    if (version != BYTECODE_FORMAT_VERSION) {
        throw BytecodeError("unsupported bytecode version " +
                            std::to_string(static_cast<int>(version)) +
                            "; recompile the program");
    }
    const uint8_t flags = reader.readByte();
    if ((flags & ~BYTECODE_FLAG_OPTIMIZED) != 0) {
        throw BytecodeError("unknown bytecode flags");
    }

    CompiledProgram program;
    program.flags = flags;
    program.sourcePath = reader.readVarintString();
    program.sourceHash = reader.readU64();

    const uint64_t stringCount = reader.readVarint();
    if (stringCount > MAX_TABLE_ENTRIES) {
        throw BytecodeError("too many strings");
    }
    program.strings.reserve(static_cast<std::size_t>(stringCount));
    for (uint64_t index = 0; index < stringCount; ++index) {
        program.strings.push_back(reader.readVarintString());
    }

    const uint64_t constantCount = reader.readVarint();
    if (constantCount > MAX_TABLE_ENTRIES) {
        throw BytecodeError("too many constants");
    }
    program.constants.reserve(static_cast<std::size_t>(constantCount));
    for (uint64_t index = 0; index < constantCount; ++index) {
        const uint8_t tag = reader.readByte();
        switch (tag) {
        case 0:
            program.constants.emplace_back();
            break;
        case 1:
            program.constants.emplace_back(reader.readZigZag());
            break;
        case 2: {
            const uint64_t raw = reader.readU64();
            double number = 0.0;
            std::memcpy(&number, &raw, sizeof(number));
            program.constants.emplace_back(number);
            break;
        }
        case 3:
            program.constants.emplace_back(reader.readByte() != 0);
            break;
        case 4:
            program.constants.emplace_back(reader.readVarintString());
            break;
        default:
            throw BytecodeError("unknown constant tag " +
                                std::to_string(static_cast<int>(tag)));
        }
    }

    const auto readSection = [&](CodeSection& section, const char* name) {
        const uint64_t codeLength = reader.readVarint();
        if (codeLength > MAX_CODE_SECTION_SIZE) {
            throw BytecodeError(std::string(name) +
                                " code section is too large");
        }
        section.code = reader.readBytes(static_cast<std::size_t>(codeLength));
        const uint64_t trapCount = reader.readVarint();
        if (trapCount > codeLength) {
            throw BytecodeError("too many traps");
        }
        for (uint64_t index = 0; index < trapCount; ++index) {
            Trap trap;
            trap.instruction = static_cast<uint32_t>(reader.readVarint());
            trap.line = static_cast<uint32_t>(reader.readVarint());
            trap.column = static_cast<uint32_t>(reader.readVarint());
            section.traps.push_back(trap);
        }
        section.maxStack = static_cast<uint32_t>(reader.readVarint());
        if (section.maxStack > MAX_TABLE_ENTRIES) {
            throw BytecodeError("maxStack is too large");
        }
    };
    readSection(program.setup, "setup");
    readSection(program.main, "main");
    if (reader.remaining() != 0) {
        throw BytecodeError("trailing bytes after bytecode payload");
    }

    // The site count is not stored; derive it from ForeverBegin operands.
    for (const CodeSection* section : {&program.setup, &program.main}) {
        for (const Decoded& instruction : decodeInstructions(section->code)) {
            if (instruction.op != Op::ForeverBegin) {
                continue;
            }
            std::size_t cursor = 0;
            const uint64_t site = readVarintAt(instruction.operands, cursor);
            program.foreverSiteCount =
                std::max<uint64_t>(program.foreverSiteCount, site + 1);
        }
    }

    // Range-check table indices before validation runs.
    const auto checkIndices = [&](const CodeSection& section) {
        for (const Decoded& instruction : decodeInstructions(section.code)) {
            std::size_t cursor = 0;
            const auto first = [&]() {
                return instruction.operands.empty()
                           ? 0
                           : readVarintAt(instruction.operands, cursor);
            };
            switch (instruction.op) {
            case Op::PushConst:
                if (first() >= program.constants.size()) {
                    throw BytecodeError("constant index out of range");
                }
                break;
            case Op::LoadVar:
            case Op::StoreVar:
                if (first() >= program.strings.size()) {
                    throw BytecodeError("name index out of range");
                }
                break;
            case Op::DeclareVar: {
                const uint64_t a = first();
                const uint64_t b = readVarintAt(instruction.operands, cursor);
                if (a >= program.strings.size() ||
                    b >= program.strings.size()) {
                    throw BytecodeError("name index out of range");
                }
                break;
            }
            case Op::Call: {
                const uint64_t a = first();
                const uint64_t b = readVarintAt(instruction.operands, cursor);
                if (a >= program.strings.size()) {
                    throw BytecodeError("builtin name index out of range");
                }
                if (b > 255) {
                    throw BytecodeError("too many call arguments");
                }
                break;
            }
            case Op::BuildList:
            case Op::BuildTuple:
            case Op::Interp:
                if (first() > MAX_TABLE_ENTRIES) {
                    throw BytecodeError("builder count out of range");
                }
                break;
            case Op::ForeverBegin: {
                const uint64_t a = first();
                if (a >= program.foreverSiteCount) {
                    throw BytecodeError("forever site index out of range");
                }
                break;
            }
            case Op::TryBegin: {
                if (instruction.operands.size() < 4) {
                    throw BytecodeError("truncated try handler operand");
                }
                std::size_t nameCursor = 4;
                const uint64_t nameIndex =
                    readVarintAt(instruction.operands, nameCursor);
                if (nameIndex != UINT32_MAX &&
                    nameIndex >= program.strings.size()) {
                    throw BytecodeError("catch variable name index out of range");
                }
                break;
            }
            default:
                break;
            }
        }
    };
    checkIndices(program.setup);
    checkIndices(program.main);

    validateSection(program.setup, "setup");
    validateSection(program.main, "main");
    return program;
}

namespace {

const char* mnemonic(Op op) {
    switch (op) {
    case Op::Halt: return "HALT";
    case Op::PushConst: return "PUSH_CONST";
    case Op::LoadVar: return "LOAD_VAR";
    case Op::DeclareVar: return "DECLARE_VAR";
    case Op::StoreVar: return "STORE_VAR";
    case Op::Pop: return "POP";
    case Op::Neg: return "NEG";
    case Op::Not: return "NOT";
    case Op::Truthy: return "TRUTHY";
    case Op::Call: return "CALL";
    case Op::BuildList: return "BUILD_LIST";
    case Op::BuildTuple: return "BUILD_TUPLE";
    case Op::ToString: return "TOSTRING";
    case Op::Interp: return "INTERP";
    case Op::Jump: return "JUMP";
    case Op::JumpIfFalse: return "JUMP_IF_FALSE";
    case Op::JumpIfTrue: return "JUMP_IF_TRUE";
    case Op::Add: return "ADD";
    case Op::Sub: return "SUB";
    case Op::Mul: return "MUL";
    case Op::Div: return "DIV";
    case Op::Mod: return "MOD";
    case Op::Eq: return "EQ";
    case Op::Neq: return "NEQ";
    case Op::Lt: return "LT";
    case Op::Le: return "LE";
    case Op::Gt: return "GT";
    case Op::Ge: return "GE";
    case Op::IterInit: return "ITER_INIT";
    case Op::IterNext: return "ITER_NEXT";
    case Op::ForeverBegin: return "FOREVER_BEGIN";
    case Op::ForeverSleep: return "FOREVER_SLEEP";
    case Op::BitAnd: return "BIT_AND";
    case Op::BitOr: return "BIT_OR";
    case Op::BitXor: return "BIT_XOR";
    case Op::BitNand: return "BIT_NAND";
    case Op::BitXnor: return "BIT_XNOR";
    case Op::BitNor: return "BIT_NOR";
    case Op::Shl: return "SHL";
    case Op::Shr: return "SHR";
    case Op::Exp: return "EXP";
    case Op::FloorDiv: return "FLOOR_DIV";
    case Op::LogicNand: return "LOGIC_NAND";
    case Op::LogicNor: return "LOGIC_NOR";
    case Op::BitNot: return "BIT_NOT";
    case Op::Dup: return "DUP";
    case Op::TryBegin: return "TRY_BEGIN";
    case Op::TryEnd: return "TRY_END";
    }
    return "UNKNOWN";
}

void disassembleSection(const CodeSection& section, const char* name,
                        const CompiledProgram& program, std::ostringstream& out) {
    out << name << " section: " << section.traps.size()
        << " instructions, maxStack " << section.maxStack << "\n";
    const std::vector<Decoded> instructions =
        decodeInstructions(section.code);
    for (std::size_t index = 0; index < instructions.size(); ++index) {
        const Decoded& instruction = instructions[index];
        out << "  " << index << "  0x" << std::hex << instruction.start
            << std::dec << "  " << mnemonic(instruction.op);
        std::size_t cursor = 0;
        const auto first = [&]() {
            return instruction.operands.empty()
                       ? 0
                       : readVarintAt(instruction.operands, cursor);
        };
        switch (instruction.op) {
        case Op::PushConst: {
            const uint64_t index2 = first();
            out << " " << index2;
            if (index2 < program.constants.size()) {
                out << "  ; " << valueToString(program.constants[index2]);
            }
            break;
        }
        case Op::LoadVar:
        case Op::StoreVar: {
            const uint64_t index2 = first();
            out << " " << index2;
            if (index2 < program.strings.size()) {
                out << "  ; " << program.strings[index2];
            }
            break;
        }
        case Op::DeclareVar: {
            const uint64_t a = first();
            const uint64_t b = readVarintAt(instruction.operands, cursor);
            out << " " << a << " " << b;
            if (a < program.strings.size() && b < program.strings.size()) {
                out << "  ; " << program.strings[b] << " " << program.strings[a];
            }
            break;
        }
        case Op::Call: {
            const uint64_t a = first();
            const uint64_t b = readVarintAt(instruction.operands, cursor);
            out << " " << a << " " << b;
            if (a < program.strings.size()) {
                out << "  ; " << program.strings[a];
            }
            break;
        }
        case Op::BuildList:
        case Op::BuildTuple:
        case Op::Interp:
            out << " " << first();
            break;
        case Op::ForeverBegin: {
            const uint64_t a = first();
            out << " " << a << " "
                << (cursor < instruction.operands.size() &&
                            instruction.operands[cursor] != 0
                        ? "warn"
                        : "quiet");
            break;
        }
        case Op::Jump:
        case Op::JumpIfFalse:
        case Op::JumpIfTrue:
        case Op::IterNext:
            out << " -> 0x" << std::hex
                << static_cast<std::size_t>(
                       static_cast<int64_t>(instruction.end) +
                       jumpDelta(instruction))
                << std::dec;
            break;
        case Op::TryBegin: {
            std::size_t nameCursor = 4;
            const uint64_t nameIndex =
                readVarintAt(instruction.operands, nameCursor);
            out << " -> 0x" << std::hex
                << static_cast<std::size_t>(
                       static_cast<int64_t>(instruction.end) +
                       jumpDelta(instruction))
                << std::dec << " " << nameIndex;
            if (nameIndex < program.strings.size()) {
                out << " ; " << program.strings[nameIndex];
            }
            break;
        }
        default:
            break;
        }
        out << "\n";
    }
}

} // namespace

std::string disassembleProgram(const CompiledProgram& program) {
    std::ostringstream out;
    out << "CLynxer bytecode  version " << static_cast<int>(BYTECODE_FORMAT_VERSION)
        << "  flags " << (program.flags & BYTECODE_FLAG_OPTIMIZED ? "optimized"
                                                                   : "unoptimized")
        << "\n";
    out << "  Source : " << program.sourcePath << "\n";
    std::ostringstream hash;
    hash << std::hex << program.sourceHash;
    out << "  Hash   : " << hash.str() << "\n";
    out << "  Strings: " << program.strings.size() << "  Constants: "
        << program.constants.size() << "\n";
    disassembleSection(program.setup, "setup", program, out);
    disassembleSection(program.main, "main", program, out);
    return out.str();
}

} // namespace clynxer
