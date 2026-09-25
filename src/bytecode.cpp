#include "bytecode.hpp"

#include <cstddef>
#include <ostream>
#include <print>

namespace sc {

void dumpBytecode(std::ostream& os, const CompilationUnit& unit, int digits) {
    const auto validConst = [&unit](int index) {
        return index >= 0 && static_cast<std::size_t>(index) < unit.constants.size();
    };

    for (std::size_t i = 0; i < unit.code.size(); ++i) {
        const Instruction& ins = unit.code[i];
        switch (ins.op) {
            case OpCode::PushConst:
                if (validConst(ins.operand))
                    std::println(os, "{:>3}: {:5} [{}] {}", i, opName(ins.op), ins.operand,
                                 unit.constants[static_cast<std::size_t>(ins.operand)].to_literal(digits));
                else
                    std::println(os, "{:>3}: {:5} [{}] <非法常量索引>", i, opName(ins.op),
                                 ins.operand);
                break;
            case OpCode::Load:
            case OpCode::Store:
                std::println(os, "{:>3}: {:5} {}", i, opName(ins.op), ins.operand);
                break;
            default:
                std::println(os, "{:>3}: {}", i, opName(ins.op));
                break;
        }
    }
}

} // namespace sc
