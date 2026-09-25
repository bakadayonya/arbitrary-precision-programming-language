// 抽象语法树。表达式/语句都是 variant，节点用 unique_ptr 串联（move-only）。
#pragma once

#include "number.hpp"
#include "token.hpp"

#include <memory>
#include <string>
#include <variant>

namespace sc {

struct Expr;
struct Stmt;

struct IntLit {
    mpz_class value;
};

struct FloatLit {
    Mpfr value;
};

/// 字符串字面量。value 是已经解好转义的内容（词法层保证它是合法 UTF-8）。
struct StrLit {
    std::string value;
};

struct VarExpr {
    std::string name;
    int pos = -1; // 变量引用位置，用于"未定义变量"报错
};

struct BinaryExpr {
    TokenType op;
    int pos = -1; // 运算符位置，运行期错误据此定位
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
};

/// 前缀运算符（目前只有一元负号）。独立成节点而不是脱糖成 0-x：
/// 这样只产生一条 NEG 指令、不污染常量池，运行期错误也指向运算符本身。
struct UnaryExpr {
    TokenType op;
    int pos = -1;
    std::unique_ptr<Expr> operand;
};

struct AssignExpr {
    std::string name;
    int pos = -1;
    std::unique_ptr<Expr> value;
};

struct PrintStmt {
    int pos = -1;
    std::unique_ptr<Expr> expr;
};

/// 表达式语句：求值并输出结果（便于当计算器用）。
struct ExprStmt {
    int pos = -1; // 语句起始位置，运行期错误据此定位
    std::unique_ptr<Expr> expr;
};

using ExprVariant = std::variant<IntLit, FloatLit, StrLit, VarExpr, BinaryExpr, UnaryExpr>;
using StmtVariant = std::variant<AssignExpr, PrintStmt, ExprStmt>;

struct Expr {
    ExprVariant v;
};

struct Stmt {
    StmtVariant v;
};

[[nodiscard]] Expr makeInt(mpz_class z);
[[nodiscard]] Expr makeFloat(Mpfr f);
[[nodiscard]] Expr makeStr(std::string value);
[[nodiscard]] Expr makeVar(std::string name, int pos);
[[nodiscard]] Expr makeBinary(TokenType op, int pos, Expr left, Expr right);
[[nodiscard]] Expr makeUnary(TokenType op, int pos, Expr operand);
[[nodiscard]] Stmt makeAssign(std::string name, int pos, Expr value);
[[nodiscard]] Stmt makePrint(int pos, Expr expr);
[[nodiscard]] Stmt makeExprStmt(int pos, Expr expr);

} // namespace sc
