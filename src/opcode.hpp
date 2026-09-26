// 指令集定义。
//
// 这个头文件刻意只依赖 <string_view>：语法层（优先级表）需要引用 OpCode，
// 但不该因此拖入 MPFR 之类的运行时依赖。
//
// 新增一条指令的流程（漏做任何一步都会在编译期报错，而不是静默退化）：
//   1. 在 OpCode 里加一个枚举值（放在 Count 之前）；
//   2. 在 opName() 里给它一个名字   —— 否则 allOpNamesDefined() 断言失败；
//   3. 在 isVmCore() 或运算符表（operators.hpp）里登记恰好一次
//                                   —— 否则 opcodeCoverageOk() 断言失败。
#pragma once

#include <string_view>

namespace sc {

/// 字节码指令。Count 是哨兵，不是可执行指令。
///
/// 约定：VM 核心指令（isVmCore）由 VM 主循环直接处理；其余指令必须来自
/// 运算符表，由表里的求值函数驱动。这样"加一个运算符"不需要改 VM。
enum class OpCode {
    PushConst, // 压入常量池第 operand 个值
    Load,      // 读取变量槽 operand
    Store,     // 写入变量槽 operand（弹出栈顶）
    Add,
    Sub,
    Mul,
    Div,
    Rem, // 取余
    Shl, // 左移
    Shr, // 右移
    Pow,
    Lt,          // <
    Le,          // <=
    Gt,          // >
    Ge,          // >=
    Eq,          // ==
    Ne,          // !=
    Neg,         // 一元负号
    Call,        // 调用内建函数：operand 是 builtins 表下标，参数个数由表项决定
    Test,        // 校验栈顶是布尔（条件表达式），原样留在栈上
    Jump,        // 无条件跳转：pc += operand（相对下一条指令）
    JumpIfFalse, // 弹出布尔，为假时按 operand 跳转
    Print,       // 弹出栈顶并输出
    Halt,
    Count // 哨兵：可执行指令数
};

/// 可执行指令条数（不含 Count）。
inline constexpr int OPCODE_COUNT = static_cast<int>(OpCode::Count);

/// 指令名。未知/哨兵返回 "?"，由 allOpNamesDefined() 在编译期拦住漏登记的指令。
constexpr std::string_view opName(OpCode op) {
    // 手对齐的名字表，拆开反而难读；下面这段保持原样。
    // clang-format off
    switch (op) {
        case OpCode::PushConst: return "PUSH";
        case OpCode::Load:      return "LOAD";
        case OpCode::Store:     return "STORE";
        case OpCode::Add:       return "ADD";
        case OpCode::Sub:       return "SUB";
        case OpCode::Mul:       return "MUL";
        case OpCode::Div:       return "DIV";
        case OpCode::Rem:       return "REM";
        case OpCode::Shl:       return "SHL";
        case OpCode::Shr:       return "SHR";
        case OpCode::Pow:       return "POW";
        case OpCode::Lt:        return "LT";
        case OpCode::Le:        return "LE";
        case OpCode::Gt:        return "GT";
        case OpCode::Ge:        return "GE";
        case OpCode::Eq:        return "EQ";
        case OpCode::Ne:        return "NE";
        case OpCode::Neg:       return "NEG";
        case OpCode::Call:      return "CALL";
        case OpCode::Test:      return "TEST";
        case OpCode::Jump:      return "JMP";
        case OpCode::JumpIfFalse: return "JMPF";
        case OpCode::Print:     return "PRINT";
        case OpCode::Halt:      return "HALT";
        case OpCode::Count:     break;
    }
    // clang-format on
    return "?";
}

/// 由 VM 主循环直接处理的指令。
constexpr bool isVmCore(OpCode op) {
    switch (op) {
        case OpCode::PushConst:
        case OpCode::Load:
        case OpCode::Store:
        case OpCode::Call:
        case OpCode::Test:
        case OpCode::Jump:
        case OpCode::JumpIfFalse:
        case OpCode::Print:
        case OpCode::Halt: return true;
        case OpCode::Add:
        case OpCode::Sub:
        case OpCode::Mul:
        case OpCode::Div:
        case OpCode::Rem:
        case OpCode::Shl:
        case OpCode::Shr:
        case OpCode::Pow:
        case OpCode::Lt:
        case OpCode::Le:
        case OpCode::Gt:
        case OpCode::Ge:
        case OpCode::Eq:
        case OpCode::Ne:
        case OpCode::Neg: return false;
        case OpCode::Count: break;
    }
    return false;
}

namespace detail {

constexpr bool allOpNamesDefined() {
    for (int i = 0; i < OPCODE_COUNT; ++i) {
        if (opName(static_cast<OpCode>(i)) == std::string_view{"?"}) return false;
    }
    return true;
}

} // namespace detail

static_assert(detail::allOpNamesDefined(),
              "有 OpCode 没有在 opName() 里登记名字：请在 opcode.hpp 补上");

} // namespace sc
