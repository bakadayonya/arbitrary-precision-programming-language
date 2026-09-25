// 运算符表：语法（优先级/结合性）与语义（指令/求值函数）的单一来源。
//
// 加一个中缀运算符 = 在 BINARY_OPS 里加一行（若引用了新的符号，再在 lexer.cpp 加一个 token）。
// 加一个前缀运算符 = 在 UNARY_OPS 里加一行。
// 解析器、代码生成、VM 都从这张表推导，因此都不需要改动。
//
// 表本身受两道编译期检查保护：
//   * opcode.hpp 的 allOpNamesDefined()  —— 新指令必须有名字；
//   * 本文件末尾的 opcodeCoverageOk()     —— 新指令必须恰好被 VM 核心或某张表处理一次。
#pragma once

#include "error.hpp"
#include "opcode.hpp"
#include "token.hpp"
#include "value.hpp"

#include <array>

namespace sc {

/// 中缀运算符的结合性。
enum class Assoc { Left, Right };

/// 求值函数签名：两个操作数 + 数值策略。
using BinaryEval = Result<Value> (*)(const Value&, const Value&, const ValueLimits&);

/// 前缀运算符的求值函数签名。
using UnaryEval = Result<Value> (*)(const Value&, const ValueLimits&);

/// 优先级表。数值越大结合越紧；低位预留给将来可能加入的比较/逻辑运算符。
/// 这组数值让 -2**2 == -(2**2) 且 2**-1 == 2**(-1) 同时成立，
/// 并保持 C 的结合顺序：移位 < 加减 < 乘除取余 < 一元 < 幂。
inline constexpr int PREC_SHIFT = 1;
inline constexpr int PREC_ADD = 2;
inline constexpr int PREC_MUL = 3;
inline constexpr int PREC_UNARY = 4;
inline constexpr int PREC_POWER = 5;

struct BinaryOpInfo {
    TokenType token;
    OpCode opcode;
    int precedence; // 越大结合越紧
    Assoc assoc;
    BinaryEval eval;
};

struct UnaryOpInfo {
    TokenType token;
    OpCode opcode;
    int operandPrecedence; // 操作数按这个优先级解析
    UnaryEval eval;
};

inline constexpr std::array<BinaryOpInfo, 8> BINARY_OPS{{
    {TokenType::ShiftLeft, OpCode::Shl, PREC_SHIFT, Assoc::Left, &valueShl},
    {TokenType::ShiftRight, OpCode::Shr, PREC_SHIFT, Assoc::Left, &valueShr},
    {TokenType::Plus, OpCode::Add, PREC_ADD, Assoc::Left, &valueAdd},
    {TokenType::Minus, OpCode::Sub, PREC_ADD, Assoc::Left, &valueSub},
    {TokenType::Star, OpCode::Mul, PREC_MUL, Assoc::Left, &valueMul},
    {TokenType::Slash, OpCode::Div, PREC_MUL, Assoc::Left, &valueDiv},
    {TokenType::Percent, OpCode::Rem, PREC_MUL, Assoc::Left, &valueRem},
    {TokenType::StarStar, OpCode::Pow, PREC_POWER, Assoc::Right, &valuePow},
}};

inline constexpr std::array<UnaryOpInfo, 1> UNARY_OPS{{
    {TokenType::Minus, OpCode::Neg, PREC_UNARY, &valueNeg},
}};

/// 中缀查找（解析器用）。不是运算符则返回 nullptr。
constexpr const BinaryOpInfo* findBinary(TokenType type) {
    for (const BinaryOpInfo& info : BINARY_OPS) {
        if (info.token == type) return &info;
    }
    return nullptr;
}

/// 前缀查找（解析器用）。
constexpr const UnaryOpInfo* findUnary(TokenType type) {
    for (const UnaryOpInfo& info : UNARY_OPS) {
        if (info.token == type) return &info;
    }
    return nullptr;
}

/// 按指令反查（VM 用）。运算符表是 VM 唯一需要知道的"额外"指令来源。
constexpr const BinaryOpInfo* binaryByOpcode(OpCode op) {
    for (const BinaryOpInfo& info : BINARY_OPS) {
        if (info.opcode == op) return &info;
    }
    return nullptr;
}

constexpr const UnaryOpInfo* unaryByOpcode(OpCode op) {
    for (const UnaryOpInfo& info : UNARY_OPS) {
        if (info.opcode == op) return &info;
    }
    return nullptr;
}

namespace detail {

/// 指令是否被某张表覆盖。
/// 注意：这里刻意比较 opcode 字段，而不是 binaryByOpcode(...) != nullptr。
/// 在 -fsanitize=undefined 下（见 Makefile 的 ubsan 目标），GCC 不把
/// "常量数组元素的地址 != nullptr" 当作常量表达式，指针写法会让 static_assert 直接编译失败。
constexpr bool hasBinaryOpcode(OpCode op) {
    for (const BinaryOpInfo& info : BINARY_OPS) {
        if (info.opcode == op) return true;
    }
    return false;
}

constexpr bool hasUnaryOpcode(OpCode op) {
    for (const UnaryOpInfo& info : UNARY_OPS) {
        if (info.opcode == op) return true;
    }
    return false;
}

/// 每条可执行指令必须恰好有一个归属：VM 核心，或某张运算符表。
constexpr bool opcodeCoverageOk() {
    for (int i = 0; i < OPCODE_COUNT; ++i) {
        const OpCode op = static_cast<OpCode>(i);
        const int owners = (isVmCore(op) ? 1 : 0) + (hasBinaryOpcode(op) ? 1 : 0) +
                           (hasUnaryOpcode(op) ? 1 : 0);
        if (owners != 1) return false;
    }
    return true;
}

} // namespace detail

static_assert(detail::opcodeCoverageOk(),
              "有 OpCode 既不属于 VM 核心、也不在运算符表里（或同时属于两者）："
              "请在 opcode.hpp 的 isVmCore() 或 operators.hpp 的表里登记恰好一次");

} // namespace sc
