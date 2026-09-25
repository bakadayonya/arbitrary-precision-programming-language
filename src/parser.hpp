// Pratt（top-down operator precedence）解析器 + 表驱动的语句分派。
//
// 三层都按 token 查 parselet，加语法就是加一行表项：
//   * 前缀 parselet（nud）：字面量、标识符、括号、前缀运算符 —— PREFIX_PARSELETS
//   * 中缀环（led）：绑定力与求值都来自 operators.hpp 的 BINARY_OPS，
//     所以加一个中缀运算符**不需要改本文件**
//   * 语句 parselet：print，将来的 if/while/block —— STMT_PARSELETS
//
// 绑定力是显式的 (leftBp, rightBp) 对（见 operators.hpp），
// 结合性由这对数决定；驱动只有一个 parseExpr(minBp) 循环。
#pragma once

#include "ast.hpp"
#include "config.hpp"
#include "error.hpp"
#include "token.hpp"

#include <array>
#include <cstddef>
#include <string_view>
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
    [[nodiscard]] Result<Expr> parseVariable(const Token& token);
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

    // ---------- 表：新增语法通常只改这里 ----------
    /// 基本前缀 parselet（字面量/标识符/分组）。
    /// 前缀**运算符**不在这张表里：它们由图外的 UNARY_OPS 驱动（见 parseExpr），
    /// 所以加一个前缀运算符只需要在 UNARY_OPS 加一行 + 词法加一个 token。
    static constexpr std::array<PrefixParselet, 5> PRIMARY_PREFIX_PARSELETS{{
        {TokenType::Number, &Parser::parseIntLiteral},
        {TokenType::Float, &Parser::parseFloatLiteral},
        {TokenType::String, &Parser::parseStringLiteral},
        {TokenType::Ident, &Parser::parseVariable},
        {TokenType::LParen, &Parser::parseGroup},
    }};

    /// 语句起始 token。加语句（if/while/block…）在这里加一行 + 一个方法 + 一个 AST 节点。
    static constexpr std::array<StmtParselet, 1> STMT_PARSELETS{{
        {TokenType::Print, &Parser::parsePrintStatement},
    }};

    // ---------- 驱动 ----------
    /// Pratt 主循环：先取前缀（nud），再按绑定力吸收中缀（led）。
    /// 所有递归环（括号、前缀操作数、中缀右操作数）都经过这里，
    /// 所以深度计数只需要放在这一处。
    [[nodiscard]] Result<Expr> parseExpr(int minBp);
    [[nodiscard]] Result<Stmt> parseStatement();

    [[nodiscard]] const Token& peek() const;
    const Token& advance();
    [[nodiscard]] bool check(TokenType type) const;
    [[nodiscard]] bool checkNext(TokenType type) const;

    [[nodiscard]] Result<Token> expect(TokenType type, std::string_view what);
    [[nodiscard]] Status endStatement();
    [[nodiscard]] Status noteNode(int pos);

    std::vector<Token> tokens_;
    const Config& config_;
    std::size_t pos_ = 0;
    int depth_ = 0;
    int nodes_ = 0;
};

} // namespace sc
