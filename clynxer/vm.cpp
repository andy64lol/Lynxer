#include "vm.hpp"

#include "builtins.hpp"
#include "config.hpp"
#include "error.hpp"
#include "ops.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

namespace clynxer {

namespace {

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

uint64_t readVarint(const std::vector<uint8_t>& code, std::size_t& pc) {
    uint64_t value = 0;
    int shift = 0;
    for (;;) {
        if (pc >= code.size()) {
            fail("truncated bytecode instruction");
        }
        const uint8_t byte = code[pc++];
        if (shift >= 64 || (shift == 63 && (byte & 0xFE) != 0)) {
            fail("varint overflow in bytecode");
        }
        value |= static_cast<uint64_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            return value;
        }
        shift += 7;
    }
}

int32_t readJumpDelta(const std::vector<uint8_t>& code, std::size_t& pc) {
    if (pc + 4 > code.size()) {
        fail("truncated jump operand");
    }
    int32_t delta = 0;
    std::memcpy(&delta, code.data() + pc, sizeof(delta));
    pc += 4;
    return delta;
}

BinOp binOpOfOpcode(Op op) {
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
    default: fail("not a binary opcode");
    }
}

Value popStack(std::vector<Value>& stack) {
    if (stack.empty()) {
        fail("operand stack underflow");
    }
    Value value = std::move(stack.back());
    stack.pop_back();
    return value;
}

// Source position for the current instruction; line 0 means the trap table
// has no entry, which is a compiler invariant violation, not a user error.
std::pair<int, int> positionOf(const CodeSection& section,
                               std::size_t instructionIndex,
                               std::size_t opcodePc) {
    if (instructionIndex >= section.traps.size()) {
        fail("bytecode position table is inconsistent at instruction " +
             std::to_string(instructionIndex));
    }
    const Trap& trap = section.traps[instructionIndex];
    if (trap.line == 0) {
        fail("bytecode instruction is missing a source position at " +
             std::to_string(instructionIndex) + " (pc " +
             std::to_string(opcodePc) + ")");
    }
    return {static_cast<int>(trap.line), static_cast<int>(trap.column)};
}

void runSection(const CodeSection& section, const CompiledProgram& program,
                Environment& environment, std::vector<bool>& foreverWarned) {
    const std::vector<uint8_t>& code = section.code;
    const auto& strings = program.strings;
    const auto& constants = program.constants;
    std::vector<Value> stack;
    stack.reserve(section.maxStack);
    struct Handler {
        std::size_t target = 0;
        uint32_t nameIndex = UINT32_MAX;
    };
    std::vector<Handler> handlers;

    std::size_t pc = 0;
    while (pc < code.size()) {
        const std::size_t opcodePc = pc;
        const Op op = static_cast<Op>(code[pc++]);
        const auto startIt = std::lower_bound(
            section.instructionStarts.begin(), section.instructionStarts.end(),
            static_cast<uint32_t>(opcodePc));
        const std::size_t currentInstruction =
            section.instructionStarts.empty()
                ? 0
                : static_cast<std::size_t>(startIt -
                                           section.instructionStarts.begin());
        try {
        switch (op) {
        case Op::Halt:
            return;

        case Op::PushConst: {
            const uint64_t index = readVarint(code, pc);
            if (index >= constants.size()) {
                fail("constant index out of range");
            }
            stack.push_back(constants[index]);
            break;
        }

        case Op::LoadVar: {
            const uint64_t index = readVarint(code, pc);
            const auto [line, column] = positionOf(section, currentInstruction, opcodePc);
            if (index >= strings.size()) {
                fail("name index out of range");
            }
            stack.push_back(environment.get(strings[index], line, column));
            break;
        }

        case Op::DeclareVar: {
            const uint64_t nameIndex = readVarint(code, pc);
            const uint64_t typeIndex = readVarint(code, pc);
            const auto [line, column] = positionOf(section, currentInstruction, opcodePc);
            if (nameIndex >= strings.size() || typeIndex >= strings.size()) {
                fail("name index out of range");
            }
            const std::string& type = strings[typeIndex];
            Value value = popStack(stack);
            value = Environment::convertForType(std::move(value), type, line,
                                                column);
            environment.declare(strings[nameIndex], type, std::move(value),
                                line, column);
            break;
        }

        case Op::StoreVar: {
            const uint64_t index = readVarint(code, pc);
            const auto [line, column] = positionOf(section, currentInstruction, opcodePc);
            if (index >= strings.size()) {
                fail("name index out of range");
            }
            Value value = popStack(stack);
            environment.assign(strings[index], std::move(value), line, column);
            break;
        }

        case Op::Pop:
            popStack(stack);
            break;

        case Op::Dup:
            if (stack.empty()) {
                fail("operand stack underflow in dup");
            }
            stack.push_back(stack.back());
            break;

        case Op::Neg:
        case Op::Not:
        case Op::BitNot: {
            const auto [line, column] = positionOf(section, currentInstruction, opcodePc);
            Value value = popStack(stack);
            const char* name = op == Op::Neg ? "-" : op == Op::BitNot ? "~" : "!!";
            stack.push_back(applyUnary(name, std::move(value), line, column));
            break;
        }

        case Op::Truthy: {
            Value value = popStack(stack);
            stack.push_back(isTruthy(value));
            break;
        }

        case Op::Call: {
            const uint64_t nameIndex = readVarint(code, pc);
            const uint64_t argc = readVarint(code, pc);
            const auto [line, column] = positionOf(section, currentInstruction, opcodePc);
            if (nameIndex >= strings.size()) {
                fail("builtin name index out of range");
            }
            if (argc > stack.size()) {
                fail("operand stack underflow in call");
            }
            std::vector<Value> arguments(static_cast<std::size_t>(argc));
            for (std::size_t index = argc; index > 0; --index) {
                arguments[index - 1] = popStack(stack);
            }
            stack.push_back(callBuiltin(strings[nameIndex], arguments,
                                        environment, line, column));
            break;
        }

        case Op::BuildList:
        case Op::BuildTuple: {
            const uint64_t count = readVarint(code, pc);
            if (count > stack.size()) {
                fail("operand stack underflow in builder");
            }
            std::vector<Value> elements(static_cast<std::size_t>(count));
            for (std::size_t index = count; index > 0; --index) {
                elements[index - 1] = popStack(stack);
            }
            if (op == Op::BuildList) {
                stack.push_back(
                    std::make_shared<List>(List{std::move(elements)}));
            } else {
                stack.push_back(
                    std::make_shared<Tuple>(Tuple{std::move(elements)}));
            }
            break;
        }

        case Op::ToString: {
            Value value = popStack(stack);
            stack.push_back(valueToString(value));
            break;
        }

        case Op::Interp: {
            const uint64_t count = readVarint(code, pc);
            if (count > stack.size()) {
                fail("operand stack underflow in interpolation");
            }
            std::vector<Value> segments(static_cast<std::size_t>(count));
            for (std::size_t index = count; index > 0; --index) {
                segments[index - 1] = popStack(stack);
            }
            std::string output;
            for (const Value& segment : segments) {
                output += valueToString(segment);
            }
            stack.push_back(output);
            break;
        }

        case Op::Jump: {
            const int32_t delta = readJumpDelta(code, pc);
            pc = static_cast<std::size_t>(
                static_cast<int64_t>(pc) + delta);
            break;
        }

        case Op::JumpIfFalse: {
            const int32_t delta = readJumpDelta(code, pc);
            Value condition = popStack(stack);
            if (!isTruthy(condition)) {
                pc = static_cast<std::size_t>(
                    static_cast<int64_t>(pc) + delta);
            }
            break;
        }

        case Op::JumpIfTrue: {
            const int32_t delta = readJumpDelta(code, pc);
            Value condition = popStack(stack);
            if (isTruthy(condition)) {
                pc = static_cast<std::size_t>(
                    static_cast<int64_t>(pc) + delta);
            }
            break;
        }

        case Op::TryBegin: {
            const int32_t delta = readJumpDelta(code, pc);
            const uint64_t nameIndex = readVarint(code, pc);
            if (nameIndex != UINT32_MAX && nameIndex >= strings.size()) {
                fail("catch variable name index out of range");
            }
            handlers.push_back(
                {static_cast<std::size_t>(static_cast<int64_t>(pc) + delta),
                 static_cast<uint32_t>(nameIndex)});
            break;
        }

        case Op::TryEnd:
            if (handlers.empty()) {
                fail("try handler stack underflow");
            }
            handlers.pop_back();
            break;

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
        case Op::LogicNor: {
            int line = 0;
            int column = 0;
            if (currentInstruction < section.traps.size() &&
                section.traps[currentInstruction].line != 0) {
                line = static_cast<int>(section.traps[currentInstruction].line);
                column =
                    static_cast<int>(section.traps[currentInstruction].column);
            }
            Value right = popStack(stack);
            Value left = popStack(stack);
            stack.push_back(
                applyBinary(binOpOfOpcode(op), left, right, line, column));
            break;
        }

        case Op::IterInit: {
            const auto [line, column] = positionOf(section, currentInstruction, opcodePc);
            Value count = popStack(stack);
            if (!std::holds_alternative<std::int64_t>(count)) {
                throw SourceError("iterate() count must be an integer", line,
                                  column);
            }
            stack.push_back(count);           // loop bound
            stack.push_back(std::int64_t{0}); // counter
            break;
        }

        case Op::IterNext: {
            const int32_t delta = readJumpDelta(code, pc);
            if (stack.size() < 2) {
                fail("operand stack underflow in iterate");
            }
            const auto counter = std::get_if<std::int64_t>(&stack[stack.size() - 1]);
            const auto bound = std::get_if<std::int64_t>(&stack[stack.size() - 2]);
            if (counter == nullptr || bound == nullptr) {
                fail("iterate loop state is not numeric");
            }
            if (*counter >= *bound) {
                stack.pop_back();
                stack.pop_back();
                pc = static_cast<std::size_t>(
                    static_cast<int64_t>(pc) + delta);
            } else {
                ++*counter;
            }
            break;
        }

        case Op::ForeverBegin: {
            const uint64_t site = readVarint(code, pc);
            const uint8_t warn = readVarint(code, pc);
            if (warn != 0 && warn != 1) {
                fail("invalid ForeverBegin warn flag");
            }
            if (warn == 1 && !environment.foreverWarningSuppressed() &&
                site < foreverWarned.size() && !foreverWarned[site]) {
                foreverWarned[site] = true;
                std::cerr << "Warning: "
                          << Config::instance().get(
                                 "warning.forever_no_break",
                                 "forever() has no break; it will run until "
                                 "the process is stopped. Add break; or call "
                                 "suppressForeverWarning() in global "
                                 "setup(){}.")
                          << '\n';
            }
            break;
        }

        case Op::ForeverSleep: {
            const double seconds = environment.foreverDelay();
            if (seconds > 0.0) {
                std::this_thread::sleep_for(
                    std::chrono::duration<double>(seconds));
            }
            break;
        }
        }
        } catch (const SourceError& error) {
            if (handlers.empty()) {
                throw;
            }
            const Handler handler = handlers.back();
            handlers.pop_back();
            stack.clear();
            if (handler.nameIndex != UINT32_MAX) {
                const auto [line, column] =
                    positionOf(section, currentInstruction, opcodePc);
                const std::string& name = strings[handler.nameIndex];
                if (environment.hasVariable(name)) {
                    const Variable existing = environment.variableSnapshot(name);
                    if (existing.constant) {
                        throw SourceError("Cannot bind catch variable '" + name +
                                              "': it is declared as const",
                                          line, column);
                    }
                    if (existing.type != "str" && existing.type != "any") {
                        throw SourceError(
                            "Cannot bind catch variable '" + name +
                                "' as 'str': '" + name +
                                "' is already declared as '" + existing.type +
                                "'",
                            line, column);
                    }
                    environment.assign(name, std::string(error.what()), line,
                                       column);
                } else {
                    environment.declare(name, "str", std::string(error.what()),
                                        line, column);
                }
            }
            pc = handler.target;
        }
    }
    fail("bytecode section fell off the end without HALT");
}

} // namespace

void runProgram(const CompiledProgram& program, Environment& environment) {
    std::vector<bool> foreverWarned(program.foreverSiteCount, false);
    environment.setSetupInProgress(true);
    runSection(program.setup, program, environment, foreverWarned);
    environment.setSetupInProgress(false);
    runSection(program.main, program, environment, foreverWarned);
}

} // namespace clynxer
