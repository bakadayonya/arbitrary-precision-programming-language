#include "compiler.hpp"

#include "builtins.hpp"
#include "operators.hpp"

#include <cstdint>
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
    Codegen gen{&unit.code, &symbols, &config};
    if (auto status = genStmtList(stmts, gen); !status) return std::unexpected(status.error());
    unit.code.push_back({OpCode::Halt, 0, -1});

    // 单元自带常量池快照，保证产物自洽（见 bytecode.hpp 的说明）。
    unit.constants = symbols.constants();
    unit.numVars = symbols.numVars();
    return unit;
}

Status Compiler::genStmtList(const std::vector<Stmt>& stmts, Codegen& gen) const {
    for (const Stmt& stmt : stmts) {
        if (auto status = genStmt(stmt, gen); !status) return status;
    }
    return {};
}

Status Compiler::genCondition(const Expr& condition, Codegen& gen, int pos) const {
    if (auto status = genExpr(condition, gen, 0); !status) return status;
    // 条件必须是布尔：Test 在运行期校验并保留值，"0 为假"这类隐式转换不存在。
    gen.code->push_back({OpCode::Test, 0, pos});
    return {};
}

Status Compiler::genBlock(const BlockStmt& node, Codegen& gen) const {
    (void)node.pos;
    return genStmtList(node.body, gen);
}

Status Compiler::genIf(const IfStmt& node, Codegen& gen) const {
    if (auto status = genCondition(*node.condition, gen, node.conditionPos); !status) return status;

    const std::size_t toElse = gen.jump(OpCode::JumpIfFalse, node.pos);
    if (auto status = genStmt(*node.thenBranch, gen); !status) return status;

    if (node.elseBranch == nullptr) {
        // 没有 else：跳到整个 if 之后（也就是"当前末尾"）。
        return gen.patch(toElse);
    }

    const std::size_t toEnd = gen.jump(OpCode::Jump, node.pos);
    if (auto status = gen.patch(toElse); !status) return status; // 条件为假 -> 跳进 else 分支
    if (auto status = genStmt(*node.elseBranch, gen); !status) return status;
    return gen.patch(toEnd);
}

Status Compiler::genWhile(const WhileStmt& node, Codegen& gen) const {
    const std::size_t loopStart = gen.code->size();

    if (auto status = genCondition(*node.condition, gen, node.conditionPos); !status) return status;
    const std::size_t exitJump = gen.jump(OpCode::JumpIfFalse, node.pos);

    gen.breakStack.emplace_back();
    gen.continueStack.emplace_back();

    if (auto status = genStmt(*node.body, gen); !status) return status;

    // continue 回到条件判断处。
    for (std::size_t from : gen.continueStack.back()) {
        if (auto status = gen.patchTo(from, loopStart); !status) return status;
    }
    gen.continueStack.pop_back();

    // 回到循环开头无条件跳转（负偏移）。
    const std::size_t back = gen.jump(OpCode::Jump, node.pos);
    if (auto status = gen.patchTo(back, loopStart); !status) return status;

    // 出口：条件为假与 break 都到这里。
    if (auto status = gen.patch(exitJump); !status) return status;
    for (std::size_t from : gen.breakStack.back()) {
        if (auto status = gen.patch(from); !status) return status;
    }
    gen.breakStack.pop_back();
    return {};
}

Status Compiler::genFor(const ForStmt& node, Codegen& gen) const {
    if (node.init != nullptr) {
        if (auto status = genStmt(*node.init, gen); !status) return status;
    }

    const std::size_t loopStart = gen.code->size();
    std::size_t exitJump = 0;
    bool hasExitJump = false;
    if (node.condition != nullptr) {
        if (auto status = genCondition(*node.condition, gen, node.conditionPos); !status)
            return status;
        exitJump = gen.jump(OpCode::JumpIfFalse, node.pos);
        hasExitJump = true;
    }

    gen.breakStack.emplace_back();
    gen.continueStack.emplace_back();

    if (auto status = genStmt(*node.body, gen); !status) return status;

    // continue 跳到后置语句。
    const std::size_t postTarget = gen.code->size();
    for (std::size_t from : gen.continueStack.back()) {
        if (auto status = gen.patchTo(from, postTarget); !status) return status;
    }
    gen.continueStack.pop_back();

    if (node.post != nullptr) {
        if (auto status = genStmt(*node.post, gen); !status) return status;
    }

    const std::size_t back = gen.jump(OpCode::Jump, node.pos);
    if (auto status = gen.patchTo(back, loopStart); !status) return status;

    if (hasExitJump) {
        if (auto status = gen.patch(exitJump); !status) return status;
    }
    for (std::size_t from : gen.breakStack.back()) {
        if (auto status = gen.patch(from); !status) return status;
    }
    gen.breakStack.pop_back();
    return {};
}

Status Compiler::genBreak(const BreakStmt& node, Codegen& gen) const {
    if (gen.breakStack.empty()) return fail("break 只能用在循环里", node.pos);
    gen.breakStack.back().push_back(gen.jump(OpCode::Jump, node.pos));
    return {};
}

Status Compiler::genContinue(const ContinueStmt& node, Codegen& gen) const {
    if (gen.continueStack.empty()) return fail("continue 只能用在循环里", node.pos);
    gen.continueStack.back().push_back(gen.jump(OpCode::Jump, node.pos));
    return {};
}

Status Compiler::genExpr(const Expr& expr, Codegen& gen, int depth) const {
    if (depth > gen.config->maxExprDepth) return fail("表达式嵌套过深");

    return std::visit(
        [&](const auto& node) -> Status {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, IntLit>) {
                gen.code->push_back(
                    {OpCode::PushConst, gen.symbols->intern(Value(node.value)), -1});
            } else if constexpr (std::is_same_v<T, FloatLit>) {
                gen.code->push_back(
                    {OpCode::PushConst, gen.symbols->intern(Value(node.value)), -1});
            } else if constexpr (std::is_same_v<T, StrLit>) {
                gen.code->push_back(
                    {OpCode::PushConst, gen.symbols->intern(Value(node.value)), -1});
            } else if constexpr (std::is_same_v<T, BoolLit>) {
                gen.code->push_back(
                    {OpCode::PushConst, gen.symbols->intern(Value(node.value)), -1});
            } else if constexpr (std::is_same_v<T, VarExpr>) {
                auto slot = gen.symbols->lookup(node.name, node.pos);
                if (!slot) return std::unexpected(slot.error());
                gen.code->push_back({OpCode::Load, *slot, node.pos});
            } else if constexpr (std::is_same_v<T, BinaryExpr>) {
                if (auto status = genExpr(*node.left, gen, depth + 1); !status) return status;
                if (auto status = genExpr(*node.right, gen, depth + 1); !status) return status;
                auto op = binaryOpcodeFor(node.op);
                if (!op) return std::unexpected(op.error());
                gen.code->push_back({*op, 0, node.pos});
            } else if constexpr (std::is_same_v<T, UnaryExpr>) {
                if (auto status = genExpr(*node.operand, gen, depth + 1); !status) return status;
                auto op = unaryOpcodeFor(node.op);
                if (!op) return std::unexpected(op.error());
                gen.code->push_back({*op, 0, node.pos});
            } else if constexpr (std::is_same_v<T, CallExpr>) {
                // 实参按书写顺序求值后依次压栈；CALL 的操作数是解析期定下的表项下标，
                // 参数个数由表项决定，所以这里不需要把个数也编进指令。
                for (const Expr& arg : node.args) {
                    if (auto status = genExpr(arg, gen, depth + 1); !status) return status;
                }
                // 解析器保证下标合法；这里再查一次是为了拦住"手搓 AST"这类调用方，
                // 让非法下标在编译期就失败，而不是等 VM 跑到这条指令。
                if (builtinArity(node.builtin) < 0)
                    return fail(std::format("内部错误：非法内建函数下标 {}", node.builtin),
                                node.pos);
                gen.code->push_back({OpCode::Call, node.builtin, node.pos});
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

Status Compiler::genStmt(const Stmt& stmt, Codegen& gen) const {
    return std::visit(
        [&](const auto& node) -> Status {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, AssignExpr>) {
                // 先求值再定义：'x = x + 1' 里的 x 必须是已定义的变量
                if (auto status = genExpr(*node.value, gen, 0); !status) return status;
                gen.code->push_back({OpCode::Store, gen.symbols->define(node.name), node.pos});
            } else if constexpr (std::is_same_v<T, PrintStmt>) {
                if (auto status = genExpr(*node.expr, gen, 0); !status) return status;
                gen.code->push_back({OpCode::Print, 0, node.pos});
            } else if constexpr (std::is_same_v<T, ExprStmt>) {
                if (auto status = genExpr(*node.expr, gen, 0); !status) return status;
                gen.code->push_back({OpCode::Print, 0, node.pos});
            } else if constexpr (std::is_same_v<T, BlockStmt>) {
                if (auto status = genBlock(node, gen); !status) return status;
            } else if constexpr (std::is_same_v<T, IfStmt>) {
                if (auto status = genIf(node, gen); !status) return status;
            } else if constexpr (std::is_same_v<T, WhileStmt>) {
                if (auto status = genWhile(node, gen); !status) return status;
            } else if constexpr (std::is_same_v<T, ForStmt>) {
                if (auto status = genFor(node, gen); !status) return status;
            } else if constexpr (std::is_same_v<T, BreakStmt>) {
                if (auto status = genBreak(node, gen); !status) return status;
            } else if constexpr (std::is_same_v<T, ContinueStmt>) {
                if (auto status = genContinue(node, gen); !status) return status;
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
