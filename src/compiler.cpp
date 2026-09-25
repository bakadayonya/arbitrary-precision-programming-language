#include "compiler.hpp"

#include "operators.hpp"

#include <format>
#include <type_traits>
#include <utility>
#include <variant>

namespace sc {

Result<OpCode> Compiler::binaryOpcodeFor(TokenType op) const {
    const BinaryOpInfo* info = findBinary(op);
    if (info == nullptr) return fail(std::format("无效的中缀运算符: {}", tokenName(op)));
    return info->opcode;
}

Result<OpCode> Compiler::unaryOpcodeFor(TokenType op) const {
    const UnaryOpInfo* info = findUnary(op);
    if (info == nullptr) return fail(std::format("无效的前缀运算符: {}", tokenName(op)));
    return info->opcode;
}

Result<CompilationUnit> Compiler::compile(const std::vector<Stmt>& stmts, SymbolTable& symbols,
                                          const Config& config) const {
    CompilationUnit unit;
    for (const Stmt& stmt : stmts) {
        if (auto status = genStmt(stmt, symbols, config, unit.code); !status)
            return std::unexpected(status.error());
    }
    unit.code.push_back({OpCode::Halt, 0, -1});

    // 单元自带常量池快照，保证产物自洽（见 bytecode.hpp 的说明）。
    unit.constants = symbols.constants();
    unit.numVars = symbols.numVars();
    return unit;
}

Status Compiler::genExpr(const Expr& expr, SymbolTable& symbols, const Config& config,
                         std::vector<Instruction>& code, int depth) const {
    if (depth > config.maxExprDepth) return fail("表达式嵌套过深");

    return std::visit(
        [&](const auto& node) -> Status {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, IntLit>) {
                code.push_back({OpCode::PushConst, symbols.intern(Value(node.value)), -1});
            } else if constexpr (std::is_same_v<T, FloatLit>) {
                code.push_back({OpCode::PushConst, symbols.intern(Value(node.value)), -1});
            } else if constexpr (std::is_same_v<T, StrLit>) {
                code.push_back({OpCode::PushConst, symbols.intern(Value(node.value)), -1});
            } else if constexpr (std::is_same_v<T, VarExpr>) {
                auto slot = symbols.lookup(node.name, node.pos);
                if (!slot) return std::unexpected(slot.error());
                code.push_back({OpCode::Load, *slot, node.pos});
            } else if constexpr (std::is_same_v<T, BinaryExpr>) {
                if (auto status = genExpr(*node.left, symbols, config, code, depth + 1); !status)
                    return status;
                if (auto status = genExpr(*node.right, symbols, config, code, depth + 1); !status)
                    return status;
                auto op = binaryOpcodeFor(node.op);
                if (!op) return std::unexpected(op.error());
                code.push_back({*op, 0, node.pos});
            } else if constexpr (std::is_same_v<T, UnaryExpr>) {
                if (auto status = genExpr(*node.operand, symbols, config, code, depth + 1); !status)
                    return status;
                auto op = unaryOpcodeFor(node.op);
                if (!op) return std::unexpected(op.error());
                code.push_back({*op, 0, node.pos});
            } else {
                // 新增 AST 节点却忘了在这里处理：编译期报错，而不是静默生成空代码。
                static_assert(detail::alwaysFalse<T>,
                              "genExpr 没有处理这个表达式节点：请在 compiler.cpp 补上分支");
                return fail("未处理的表达式节点");
            }
            return {};
        },
        expr.v);
}

Status Compiler::genStmt(const Stmt& stmt, SymbolTable& symbols, const Config& config,
                         std::vector<Instruction>& code) const {
    return std::visit(
        [&](const auto& node) -> Status {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, AssignExpr>) {
                // 先求值再定义：'x = x + 1' 里的 x 必须是已定义的变量
                if (auto status = genExpr(*node.value, symbols, config, code, 0); !status)
                    return status;
                code.push_back({OpCode::Store, symbols.define(node.name), node.pos});
            } else if constexpr (std::is_same_v<T, PrintStmt>) {
                if (auto status = genExpr(*node.expr, symbols, config, code, 0); !status)
                    return status;
                code.push_back({OpCode::Print, 0, node.pos});
            } else if constexpr (std::is_same_v<T, ExprStmt>) {
                if (auto status = genExpr(*node.expr, symbols, config, code, 0); !status)
                    return status;
                code.push_back({OpCode::Print, 0, node.pos});
            } else {
                static_assert(detail::alwaysFalse<T>,
                              "genStmt 没有处理这个语句节点：请在 compiler.cpp 补上分支");
                return fail("未处理的语句节点");
            }
            return {};
        },
        stmt.v);
}

} // namespace sc
