// 递归下降解析器。
//
// 优先级不再写死成一串 parseXxx 函数，而是由 operators.hpp 的运算符表驱动，
// 用优先级爬升（precedence climbing）实现：加一个运算符只需要加一行表项。
//
// 两层防护，避免恶意/手滑输入把栈打爆（两者都由 Config 提供，可运行期调整）：
//   * maxParseDepth 限制语法嵌套（括号、前缀运算符、** 的右操作数）
//   * maxNodesPerStatement 限制单条语句的表达式节点数，从而限制 AST 深度
//     （1+1+1+... 这种左倾链在解析时不递归，但生成代码和析构时会递归）
#pragma once

#include "ast.hpp"
#include "config.hpp"
#include "error.hpp"
#include "token.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace sc {

class Parser {
public:
    /// config 必须在 Parser 存活期间有效（Engine 持有它）。
    Parser(std::vector<Token> tokens, const Config& config);

    [[nodiscard]] Result<std::vector<Stmt>> parse();

private:
    [[nodiscard]] const Token& peek() const;
    const Token& advance();
    [[nodiscard]] bool check(TokenType type) const;
    [[nodiscard]] bool checkNext(TokenType type) const;

    [[nodiscard]] Result<Token> expect(TokenType type, std::string_view what);
    [[nodiscard]] Status endStatement();
    [[nodiscard]] Status noteNode(int pos);

    [[nodiscard]] Result<Stmt> parseStatement();
    [[nodiscard]] Result<Expr> parseExpression();
    [[nodiscard]] Result<Expr> parseBinary(int minPrecedence);
    [[nodiscard]] Result<Expr> parseUnary();
    [[nodiscard]] Result<Expr> parsePrimary();

    std::vector<Token> tokens_;
    const Config& config_;
    std::size_t pos_ = 0;
    int depth_ = 0;
    int nodes_ = 0;
};

} // namespace sc
