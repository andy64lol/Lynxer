#pragma once

#include "ast.hpp"
#include "bytecode.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace clynxer {

// Grows one code section while AST nodes emit instructions. Also owns the
// program-wide string table and constant pool. Loop contexts collect
// break/continue jump patches; the loop statements close them explicitly.
class ProgramEmitter {
public:
    ProgramEmitter(CodeSection& section, std::vector<std::string>& strings,
                   std::vector<Value>& constants);

    uint32_t internString(const std::string& text);

    uint32_t internConstant(const Value& value);

    uint32_t newForeverSite();

    uint32_t foreverSites() const;

    std::size_t offset() const;

    void emit(Op op);

    void emitPushConst(uint32_t constantIndex);

    void emitOneVar(Op op, uint32_t a);

    void emitTwoVars(Op op, uint32_t a, uint32_t b);

    void emitForeverBegin(uint32_t site, bool warn);

    // Reserves a jump with a 4-byte delta operand; returns the operand
    // offset for patchJump.
    std::size_t emitJump(Op op);

    void patchJump(std::size_t operandOffset, std::size_t target);

    void trap(int line, int column);

    enum class LoopKind { Plain, Iterate };

    void pushLoop(LoopKind kind = LoopKind::Plain);

    // Patches every recorded continue jump in the innermost loop.
    void patchContinues(std::size_t target);

    // Patches every recorded break jump and pops the innermost loop.
    void closeLoop(std::size_t breakTarget);

    void recordBreak();

    void recordContinue();

private:
    struct LoopContext {
        LoopKind kind = LoopKind::Plain;
        std::vector<std::size_t> breakPatches;
        std::vector<std::size_t> continuePatches;
    };

    void noteDepth(int delta);

    CodeSection& section_;
    std::vector<std::string>& strings_;
    std::vector<Value>& constants_;
    std::unordered_map<std::string, uint32_t> stringIndices_;
    std::unordered_map<std::string, uint32_t> constantIndices_;
    std::vector<LoopContext> loops_;
    int depth_ = 0;
    uint32_t depthLimit_ = 0;
    uint32_t nextForeverSite_ = 0;
};

CompiledProgram compileProgram(
    const std::unordered_map<std::string, Function>& functions,
    const std::string& sourcePath, const std::string& source, bool optimize);

std::vector<uint8_t> serializeProgram(const CompiledProgram& program);

// Parses, validates, and returns a program; throws BytecodeError.
CompiledProgram loadProgram(const std::vector<uint8_t>& bytes);

std::string disassembleProgram(const CompiledProgram& program);

} // namespace clynxer
