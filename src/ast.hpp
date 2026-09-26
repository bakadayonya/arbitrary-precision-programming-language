// 抽象语法树。表达式/语句都是 variant。
//
// 表达式节点用 unique_ptr 串联（树形，move-only）；语句体用 std::vector<Stmt>——
// 语句是顺序结构，用 vector 比链表更贴切，也让 AST 的析构不需要递归，
// 顺带把"深层嵌套块"的析构栈深度压力挪到 vector 的迭代析构上。
#pragma once

#include "number.hpp"
#include "token.hpp"

#include <memory>
#include <string>
#include <variant>
#include <vector>

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

/// 布尔字面量（true / false）。只有两个字面量，但和其他字面量一样走常量池，
/// 这样调试输出（-d / :consts）里能看到它，去重也由常量池统一负责。
struct BoolLit {
    bool value = false;
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

/// 内建函数调用：`名字(实参, ...)`。
/// 名字与实参个数在**解析期**就对着 builtins 表查定（未知名字、个数不对都是编译错误），
/// 所以节点里直接带表项下标，编译器与 VM 都不需要再做名字解析。
/// name 仍然保留一份，供 -d / 反汇编这类自省输出显示可读形式。
/// 实参用 vector<Expr> 而不是 unique_ptr 链：参数是顺序结构，和 BlockStmt::body 同理。
struct CallExpr {
    std::string name;
    int pos = -1;     // 函数名位置：运行期的定义域/溢出错误据此定位
    int builtin = -1; // builtins 表下标
    std::vector<Expr> args;
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

/// 块：`{ 语句* }`。空块合法。变量作用域是扁平的（没有块作用域）。
struct BlockStmt {
    int pos = -1; // '{' 的位置
    std::vector<Stmt> body;
};

/// `if (条件) 语句 [else 语句]`。条件必须是布尔。
/// then/else 都是单条语句，所以 `if (c) { ... } else { ... }` 直接写成两个块。
struct IfStmt {
    int pos = -1;          // if 关键字位置
    int conditionPos = -1; // 条件表达式位置，运行期"条件必须是布尔"指这里
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> thenBranch;
    std::unique_ptr<Stmt> elseBranch; // 没有 else 时为空
};

/// `while (条件) 语句`。
struct WhileStmt {
    int pos = -1;
    int conditionPos = -1;
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> body;
};

/// `for (初始化语句; 条件; 后置语句) 语句`（C 风格）。
/// 三部分都可省略：`for (;;)` 是死循环。初始化和后置都是**语句**，
/// 因此初始化可以直接是赋值/定义（`i = 0`），也不需要额外的声明语法。
struct ForStmt {
    int pos = -1;
    int conditionPos = -1;
    std::unique_ptr<Stmt> init;      // 可为空
    std::unique_ptr<Expr> condition; // 可为空（视为恒真）
    std::unique_ptr<Stmt> post;      // 可为空
    std::unique_ptr<Stmt> body;
};

/// `break` / `continue`：编译期检查必须在循环里。
struct BreakStmt {
    int pos = -1;
};

struct ContinueStmt {
    int pos = -1;
};

using ExprVariant =
    std::variant<IntLit, FloatLit, StrLit, BoolLit, VarExpr, BinaryExpr, UnaryExpr, CallExpr>;
using StmtVariant = std::variant<AssignExpr, PrintStmt, ExprStmt, BlockStmt, IfStmt, WhileStmt,
                                 ForStmt, BreakStmt, ContinueStmt>;

struct Expr {
    ExprVariant v;
};

struct Stmt {
    StmtVariant v;
};

[[nodiscard]] Expr makeInt(mpz_class z);
[[nodiscard]] Expr makeFloat(Mpfr f);
[[nodiscard]] Expr makeStr(std::string value);
[[nodiscard]] Expr makeBool(bool value);
[[nodiscard]] Expr makeVar(std::string name, int pos);
[[nodiscard]] Expr makeBinary(TokenType op, int pos, Expr left, Expr right);
[[nodiscard]] Expr makeUnary(TokenType op, int pos, Expr operand);
[[nodiscard]] Expr makeCall(std::string name, int pos, int builtin, std::vector<Expr> args);
[[nodiscard]] Stmt makeAssign(std::string name, int pos, Expr value);
[[nodiscard]] Stmt makePrint(int pos, Expr expr);
[[nodiscard]] Stmt makeExprStmt(int pos, Expr expr);
[[nodiscard]] Stmt makeBlock(int pos, std::vector<Stmt> body);
[[nodiscard]] Stmt makeIf(int pos, int conditionPos, Expr condition, Stmt thenBranch,
                          std::unique_ptr<Stmt> elseBranch);
[[nodiscard]] Stmt makeWhile(int pos, int conditionPos, Expr condition, Stmt body);
[[nodiscard]] Stmt makeFor(int pos, int conditionPos, std::unique_ptr<Stmt> init,
                           std::unique_ptr<Expr> condition, std::unique_ptr<Stmt> post, Stmt body);
[[nodiscard]] Stmt makeBreak(int pos);
[[nodiscard]] Stmt makeContinue(int pos);

} // namespace sc
