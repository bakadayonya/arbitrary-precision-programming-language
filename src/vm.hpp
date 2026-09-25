// 栈式虚拟机。
//
// 字节码由本项目的 Compiler 生成，正常路径不会越界；但 VM 不假设这一点：
// 常量下标、变量下标、栈高度全部检查，任何异常字节码都返回 Error 而不是 UB。
//
// 指令分两类：
//   * VM 核心（isVmCore）：常量/变量/栈/输出；
//   * 运算符表（operators.hpp）：求值函数驱动，所以新增运算符不需要改这里。
// 两类合起来必须恰好覆盖全部 OpCode，由 operators.hpp 的 static_assert 保证。
//
// 变量取值跨多次 run 保留（REPL 语义），snapshot/restore 用于整行回滚。
#pragma once

#include "bytecode.hpp"
#include "config.hpp"
#include "error.hpp"
#include "value.hpp"

#include <iosfwd>
#include <vector>

namespace sc {

class VM {
public:
    using Snapshot = std::vector<Value>;

    [[nodiscard]] Status run(const CompilationUnit& unit, const Config& config, std::ostream& out);

    [[nodiscard]] Snapshot snapshot() const { return variables_; }
    void restore(const Snapshot& snapshot) {
        variables_ = snapshot;
        stack_.clear();
    }
    void reset() {
        variables_.clear();
        stack_.clear();
    }

    /// 变量槽当前值；越界返回默认值 0。
    [[nodiscard]] Value variable(int slot) const;

private:
    std::vector<Value> stack_;
    std::vector<Value> variables_;
};

} // namespace sc
