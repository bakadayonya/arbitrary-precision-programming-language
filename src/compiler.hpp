// AST -> 字节码。
//
// 无状态：持久状态全部在 SymbolTable，产物全部在 CompilationUnit。
// 这样"编译器"只是一个纯函数式的变换，可以被多个会话共享、可以并发调用
// （只要每个会话有自己的 SymbolTable）。每次 compile() 内部的跳转回填与
// 循环上下文都放在一个临时结构里，编译结束即丢弃，不进入 Compiler 本身。
//
// 运算符到指令的映射不在这份文件里手写 switch，而是查 operators.hpp 的表：
// 加一个运算符不需要改代码生成。
#pragma once

#include "ast.hpp"
#include "bytecode.hpp"
#include "config.hpp"
#include "error.hpp"
#include "symbols.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sc {

class Compiler {
public:
    /// 把语句编译成一个自洽的 CompilationUnit。
    /// 会向 symbols 里登记新变量/常量；调用方负责失败时回滚（见 Engine）。
    [[nodiscard]] Result<CompilationUnit> compile(const std::vector<Stmt>& stmts,
                                                  SymbolTable& symbols, const Config& config) const;

private:
    /// 一次 compile() 的代码生成状态：跳转回填 + 循环上下文（break/continue）。
    struct Codegen {
        std::vector<Instruction>* code = nullptr;
        SymbolTable* symbols = nullptr;
        const Config* config = nullptr;
        /// 各层循环的 break 跳转（跳到循环出口）。
        std::vector<std::vector<std::size_t>> breakStack{};
        /// 各层循环的 continue 跳转（while 跳回条件、for 跳到后置语句）。
        std::vector<std::vector<std::size_t>> continueStack{};

        /// 发出一条待回填的跳转，返回它的下标。
        [[nodiscard]] std::size_t jump(OpCode op, int pos) {
            code->push_back({op, 0, pos});
            return code->size() - 1;
        }
        /// 把跳转的目标填成"当前末尾"。
        [[nodiscard]] Status patch(std::size_t from) { return patchTo(from, code->size()); }
        /// 把跳转的目标填成绝对下标 target；操作数是相对下一条指令的偏移。
        [[nodiscard]] Status patchTo(std::size_t from, std::size_t target) {
            if (from >= code->size()) return fail("内部错误：跳转回填越界");
            const std::size_t base = from + 1; // 偏移相对"跳转的下一条指令"
            if (target > base && target - base > static_cast<std::size_t>(INT32_MAX))
                return fail("跳转距离过大");
            if (base > target && base - target > static_cast<std::size_t>(INT32_MAX))
                return fail("跳转距离过大");
            (*code)[from].operand = static_cast<int>(target) - static_cast<int>(base);
            return {};
        }
    };

    [[nodiscard]] Status genStmtList(const std::vector<Stmt>& stmts, Codegen& gen) const;
    [[nodiscard]] Status genStmt(const Stmt& stmt, Codegen& gen) const;
    [[nodiscard]] Status genExpr(const Expr& expr, Codegen& gen, int depth) const;

    [[nodiscard]] Status genBlock(const BlockStmt& node, Codegen& gen) const;
    [[nodiscard]] Status genIf(const IfStmt& node, Codegen& gen) const;
    [[nodiscard]] Status genWhile(const WhileStmt& node, Codegen& gen) const;
    [[nodiscard]] Status genFor(const ForStmt& node, Codegen& gen) const;
    [[nodiscard]] Status genBreak(const BreakStmt& node, Codegen& gen) const;
    [[nodiscard]] Status genContinue(const ContinueStmt& node, Codegen& gen) const;

    /// 条件表达式 -> TEST 指令。条件必须是布尔，类型检查在 VM 里做
    /// （报错点用 pos：比较运算符的位置或子句结束位置）。
    [[nodiscard]] Status genCondition(const Expr& condition, Codegen& gen, int pos) const;

    /// 运算符 token -> 指令，查表得来。
    /// 同一个 token 可以既是中缀又是前缀（例如 '-'），所以必须按上下文查对应的表。
    [[nodiscard]] Result<OpCode> binaryOpcodeFor(TokenType op) const;
    [[nodiscard]] Result<OpCode> unaryOpcodeFor(TokenType op) const;
};

} // namespace sc
