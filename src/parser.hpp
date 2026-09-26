// Pratt（top-down operator precedence）解析器 + 表驱动的语句分派。
//
// 三层都按 token 查 parselet，加语法就是加一行表项：
//   * 前缀 parselet（nud）：字面量、标识符、括号、前缀运算符 —— PRIMARY_PREFIX_PARSELETS
//   * 中缀环（led）：绑定力与求值都来自 operators.hpp 的 BINARY_OPS，
//     所以加一个中缀运算符**不需要改本文件**
//   * 语句 parselet：print/if/while/for/… —— STMT_PARSELETS
//
// 绑定力是显式的 (leftBp, rightBp) 对（见 operators.hpp），
// 结合性由这对数决定；驱动只有一个 parseExpr(minBp) 循环。
//
// 语句体是 std::vector<Stmt>，分号规则只有一条：**分号是语句分隔符，不是语句的一部分**。
// 列表驱动（parseStatementList）负责消费分隔符，语句自己的解析函数一律在分号前停下；
// 于是"最后一条可以省略分号"是自然的（后面是 `}` 或输入结束），而
// `if (c) print 1; else print 2;` 也能工作——then 分支把分号留给了 else。
#pragma once

#include "ast.hpp"
#include "config.hpp"
#include "error.hpp"
#include "token.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace sc {

/// 运算符表项。parser.cpp 需要它的定义（绑定力），这里只转发声明，避免头文件依赖。
struct BinaryOpInfo;

class Parser {
public:
    /// config 必须在 Parser 存活期间有效（Engine 持有它）。
    Parser(std::vector<Token> tokens, const Config& config);

    [[nodiscard]] Result<std::vector<Stmt>> parse();

private:
    // ---------- parselet 的两种形态 ----------
    using PrefixParseletFn = Result<Expr> (Parser::*)(const Token&);
    using StmtParseletFn = Result<Stmt> (Parser::*)(const Token&);

    struct PrefixParselet {
        TokenType token;
        PrefixParseletFn parse;
    };

    struct StmtParselet {
        TokenType token;
        StmtParseletFn parse;
    };

    // ---------- 表达式 parselet ----------
    [[nodiscard]] Result<Expr> parseIntLiteral(const Token& token);
    [[nodiscard]] Result<Expr> parseFloatLiteral(const Token& token);
    [[nodiscard]] Result<Expr> parseStringLiteral(const Token& token);
    [[nodiscard]] Result<Expr> parseBoolLiteral(const Token& token);
    [[nodiscard]] Result<Expr> parseVariable(const Token& token);
    /// 内建函数调用：`名字(实参, ...)`。进入时 Ident 已消费、当前是 '('。
    /// 名字与实参个数在这里就对着 builtins 表查定，所以未知名字/个数不对是**编译错误**。
    [[nodiscard]] Result<Expr> parseCall(const Token& nameToken);
    [[nodiscard]] Result<Expr> parseGroup(const Token& token);
    /// 前缀运算符：操作数按 UNARY_OPS 里的 operandBp 解析。
    [[nodiscard]] Result<Expr> parsePrefixOperator(const Token& token);
    /// 中缀运算符：右操作数按 op.rightBp 解析，结合性由此决定。
    [[nodiscard]] Result<Expr> parseBinaryOperator(const BinaryOpInfo& op, const Token& token,
                                                   Expr left);

    // ---------- 语句 parselet ----------
    [[nodiscard]] Result<Stmt> parsePrintStatement(const Token& keyword);
    [[nodiscard]] Result<Stmt> parseAssignStatement();
    [[nodiscard]] Result<Stmt> parseExpressionStatement();
    [[nodiscard]] Result<Stmt> parseBlockStatement(const Token& keyword);
    [[nodiscard]] Result<Stmt> parseIfStatement(const Token& keyword);
    [[nodiscard]] Result<Stmt> parseWhileStatement(const Token& keyword);
    [[nodiscard]] Result<Stmt> parseForStatement(const Token& keyword);
    [[nodiscard]] Result<Stmt> parseBreakStatement(const Token& keyword);
    [[nodiscard]] Result<Stmt> parseContinueStatement(const Token& keyword);

    // ---------- 表：新增语法通常只改这里 ----------
    /// 基本前缀 parselet（字面量/标识符/分组）。
    /// 标识符这一项同时负责 `名字(实参)` 形式的内建函数调用（见 parseVariable）。
    /// 前缀**运算符**不在这张表里：它们由图外的 UNARY_OPS 驱动（见 parseExpr），
    /// 所以加一个前缀运算符只需要在 UNARY_OPS 加一行 + 词法加一个 token。
    static constexpr std::array<PrefixParselet, 7> PRIMARY_PREFIX_PARSELETS{{
        {TokenType::Number, &Parser::parseIntLiteral},
        {TokenType::Float, &Parser::parseFloatLiteral},
        {TokenType::String, &Parser::parseStringLiteral},
        {TokenType::True, &Parser::parseBoolLiteral},
        {TokenType::False, &Parser::parseBoolLiteral},
        {TokenType::Ident, &Parser::parseVariable},
        {TokenType::LParen, &Parser::parseGroup},
    }};

    /// 语句起始 token。加语句在这里加一行 + 一个方法 + 一个 AST 节点。
    static constexpr std::array<StmtParselet, 7> STMT_PARSELETS{{
        {TokenType::Print, &Parser::parsePrintStatement},
        {TokenType::LBrace, &Parser::parseBlockStatement},
        {TokenType::If, &Parser::parseIfStatement},
        {TokenType::While, &Parser::parseWhileStatement},
        {TokenType::For, &Parser::parseForStatement},
        {TokenType::Break, &Parser::parseBreakStatement},
        {TokenType::Continue, &Parser::parseContinueStatement},
    }};

    // ---------- 驱动 ----------
    /// Pratt 主循环：先取前缀（nud），再按绑定力吸收中缀（led）。
    /// 所有递归环（括号、前缀操作数、中缀右操作数）都经过这里，
    /// 所以深度计数只需要放在这一处。
    [[nodiscard]] Result<Expr> parseExpr(int minBp);
    [[nodiscard]] Result<Stmt> parseStatement();
    /// `{ 语句* }`。语句列表的唯一实现，程序顶层与块共用。
    [[nodiscard]] Result<std::vector<Stmt>> parseStatementList(TokenType end);
    /// 条件：`( 表达式 )`。返回表达式与条件的报错位置（条件表达式起点）。
    [[nodiscard]] Result<std::pair<Expr, int>> parseCondition(std::string_view what);
    /// for 的初始化/后置子句：可省略，且不吃掉作为分隔符的 `;`。
    [[nodiscard]] Result<std::unique_ptr<Stmt>> parseForClause();

    [[nodiscard]] const Token& peek() const;
    const Token& advance();
    [[nodiscard]] bool check(TokenType type) const;
    [[nodiscard]] bool checkNext(TokenType type) const;

    [[nodiscard]] Result<Token> expect(TokenType type, std::string_view what);
    /// 列表层：检查一条解析完的语句后面是不是合法收尾（分号 / 列表结束 / 输入结束）。
    [[nodiscard]] Status finishStatement(TokenType end);
    /// for 子句里嵌套的块也要按列表规则检查收尾。
    [[nodiscard]] Status registerSubStatements(const Stmt& stmt);
    [[nodiscard]] Status noteNode(int pos);

    std::vector<Token> tokens_;
    const Config& config_;
    std::size_t pos_ = 0;
    int nodes_ = 0;
    /// 表达式嵌套深度（parseExpr 的 DepthGuard）。
    int exprDepth_ = 0;
    /// 语句嵌套深度（parseStatement 的 DepthGuard）。两者分开计数：
    /// `max-parse-depth` 对两者都是上限，但表达式深度不该被外层语句的层数占用，
    /// 否则 `if (c) { print (((1))) }` 会因为语句层数把括号预算挤没而误报。
    int stmtDepth_ = 0;
};

} // namespace sc
