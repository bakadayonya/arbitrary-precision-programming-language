// 字节码与一次编译的产物。
#pragma once

#include "opcode.hpp"
#include "value.hpp"

#include <iosfwd>
#include <vector>

namespace sc {

struct Instruction {
    OpCode op;
    int operand = 0;
    int pos = -1; // 对应的源码偏移，运行期错误据此定位
};

/// 一次编译的完整产物：代码 + 它引用的常量池 + 变量槽数。
///
/// 常量池在单元里自带一份快照，因此编译产物是自洽的：执行/反汇编都不需要
/// 回头去问编译器要状态。代价是每次编译复制一次常量池（O(常量数)），
/// 在 REPL 会话里这个代价与已有的 VM 全量快照同量级。
struct CompilationUnit {
    std::vector<Instruction> code;
    std::vector<Value> constants;
    int numVars = 0;
};

/// 反汇编输出（调试信息走 stderr，不污染程序输出）。
/// digits 是浮点值的人类可读位数，来自 Config。
void dumpBytecode(std::ostream& os, const CompilationUnit& unit, int digits);

} // namespace sc
