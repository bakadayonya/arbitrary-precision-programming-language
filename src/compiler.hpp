// AST -> 字节码。
//
// 无状态：持久状态全部在 SymbolTable，产物全部在 CompilationUnit。
// 这样"编译器"只是一个纯函数式的变换，可以被多个会话共享、可以并发调用
// （只要每个会话有自己的 SymbolTable）。
//
// 运算符到指令的映射不在这份文件里手写 switch，而是查 operators.hpp 的表：
// 加一个运算符不需要改代码生成。
#pragma once

#include "ast.hpp"
#include "bytecode.hpp"
#include "config.hpp"
#include "error.hpp"
#include "symbols.hpp"

#include <vector>

namespace sc {

class Compiler {
public:
    /// 把语句编译成一个自洽的 CompilationUnit。
    /// 会向 symbols 里登记新变量/常量；调用方负责失败时回滚（见 Engine）。
    [[nodiscard]] Result<CompilationUnit> compile(const std::vector<Stmt>& stmts,
                                                  SymbolTable& symbols, const Config& config) const;

private:
    [[nodiscard]] Status genExpr(const Expr& expr, SymbolTable& symbols, const Config& config,
                                 std::vector<Instruction>& code, int depth) const;
    [[nodiscard]] Status genStmt(const Stmt& stmt, SymbolTable& symbols, const Config& config,
                                 std::vector<Instruction>& code) const;

    /// 运算符 token -> 指令，查表得来。
    /// 同一个 token 可以既是中缀又是前缀（例如 '-'），所以必须按上下文查对应的表。
    [[nodiscard]] Result<OpCode> binaryOpcodeFor(TokenType op) const;
    [[nodiscard]] Result<OpCode> unaryOpcodeFor(TokenType op) const;
};

} // namespace sc
