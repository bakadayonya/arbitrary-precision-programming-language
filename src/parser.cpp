#include "parser.hpp"

#include "operators.hpp"

#include <format>
#include <utility>

namespace sc {

namespace {

/// 进入/离开递归层时维护深度计数。
class DepthGuard {
public:
    explicit DepthGuard(int& depth) : depth_(depth) { ++depth_; }
    ~DepthGuard() { --depth_; }
    DepthGuard(const DepthGuard&) = delete;
    DepthGuard& operator=(const DepthGuard&) = delete;

private:
    int& depth_;
};

} // namespace

Parser::Parser(std::vector<Token> tokens, const Config& config)
    : tokens_(std::move(tokens)), config_(config) {}

const Token& Parser::peek() const { return tokens_[pos_]; }

const Token& Parser::advance() { return tokens_[pos_++]; }

bool Parser::check(TokenType type) const { return peek().type == type; }

bool Parser::checkNext(TokenType type) const {
    return pos_ + 1 < tokens_.size() && tokens_[pos_ + 1].type == type;
}

Result<Token> Parser::expect(TokenType type, std::string_view what) {
    if (check(type)) return advance();
    if (check(TokenType::End))
        return incomplete(std::format("输入未结束：缺少 {}", what), peek().pos);
    return fail(std::format("期望 {}，但遇到 {}", what, tokenName(peek().type)), peek().pos);
}

Status Parser::noteNode(int pos) {
    if (++nodes_ > config_.maxNodesPerStatement)
        return fail(std::format("表达式过于复杂（超过 {} 个节点）", config_.maxNodesPerStatement),
                    pos);
    return {};
}

/// 语句结束：';' 或输入结束。允许省略最后一条语句的分号。
Status Parser::endStatement() {
    if (check(TokenType::Semicolon)) {
        advance();
        return {};
    }
    if (check(TokenType::End)) return {};
    return fail(std::format("期望 ';' 或输入结束，但遇到 {}", tokenName(peek().type)), peek().pos);
}

Result<std::vector<Stmt>> Parser::parse() {
    std::vector<Stmt> stmts;
    while (!check(TokenType::End)) {
        if (check(TokenType::Semicolon)) { // 空语句
            advance();
            continue;
        }
        nodes_ = 0;
        depth_ = 0;
        auto stmt = parseStatement();
        if (!stmt) return std::unexpected(stmt.error());
        stmts.push_back(std::move(*stmt));
    }
    return stmts;
}

Result<Stmt> Parser::parseStatement() {
    if (check(TokenType::Print)) {
        const int printPos = advance().pos;
        auto expr = parseExpression();
        if (!expr) return std::unexpected(expr.error());
        if (auto ok = endStatement(); !ok) return std::unexpected(ok.error());
        return makePrint(printPos, std::move(*expr));
    }

    if (check(TokenType::Ident) && checkNext(TokenType::Assign)) {
        const Token& nameToken = advance();
        std::string name = nameToken.value;
        advance(); // '='
        auto expr = parseExpression();
        if (!expr) return std::unexpected(expr.error());
        if (auto ok = endStatement(); !ok) return std::unexpected(ok.error());
        return makeAssign(std::move(name), nameToken.pos, std::move(*expr));
    }

    const int stmtPos = peek().pos;
    auto expr = parseExpression();
    if (!expr) return std::unexpected(expr.error());
    if (auto ok = endStatement(); !ok) return std::unexpected(ok.error());
    return makeExprStmt(stmtPos, std::move(*expr));
}

Result<Expr> Parser::parseExpression() { return parseBinary(0); }

Result<Expr> Parser::parseBinary(int minPrecedence) {
    // 所有递归环（括号、前缀运算符、** 的右操作数）都经过 parseBinary，
    // 因此这一处深度计数就等于真实递归深度，不会被重复计数。
    if (depth_ >= config_.maxParseDepth) return fail("表达式嵌套过深", peek().pos);
    const DepthGuard guard(depth_);

    auto first = parseUnary();
    if (!first) return std::unexpected(first.error());
    Expr left = std::move(*first);

    while (const BinaryOpInfo* info = findBinary(peek().type)) {
        if (info->precedence < minPrecedence) break;
        const Token& opToken = advance();
        // 左结合：右操作数必须绑得更紧；右结合：允许同级继续向右递归。
        const int nextMin = info->assoc == Assoc::Right ? info->precedence : info->precedence + 1;
        auto right = parseBinary(nextMin);
        if (!right) return std::unexpected(right.error());
        if (auto ok = noteNode(opToken.pos); !ok) return std::unexpected(ok.error());
        left = makeBinary(opToken.type, opToken.pos, std::move(left), std::move(*right));
    }
    return left;
}

Result<Expr> Parser::parseUnary() {
    if (const UnaryOpInfo* info = findUnary(peek().type)) {
        const Token& opToken = advance();
        auto operand = parseBinary(info->operandPrecedence);
        if (!operand) return std::unexpected(operand.error());
        if (auto ok = noteNode(opToken.pos); !ok) return std::unexpected(ok.error());
        return makeUnary(opToken.type, opToken.pos, std::move(*operand));
    }
    return parsePrimary();
}

Result<Expr> Parser::parsePrimary() {
    if (check(TokenType::Number)) {
        const Token& token = advance();
        mpz_class z;
        if (z.set_str(token.value, 10) != 0)
            return fail(std::format("整数解析失败: {}", token.value), token.pos);
        if (auto ok = noteNode(token.pos); !ok) return std::unexpected(ok.error());
        return makeInt(std::move(z));
    }

    if (check(TokenType::Float)) {
        const Token& token = advance();
        auto value = Mpfr::fromString(token.value, config_.precision);
        if (!value) return fail(std::format("小数解析失败: {}", token.value), token.pos);
        // MPFR 对"合法但超范围"的字面量会静默给出 inf/nan；默认按策略拒绝。
        if (!config_.allowNonFinite && !value->isFinite())
            return fail(std::format("小数超出可表示范围: {}", token.value), token.pos);
        if (auto ok = noteNode(token.pos); !ok) return std::unexpected(ok.error());
        return makeFloat(std::move(*value));
    }

    if (check(TokenType::String)) {
        const Token& token = advance();
        if (auto ok = noteNode(token.pos); !ok) return std::unexpected(ok.error());
        return makeStr(token.value);
    }

    if (check(TokenType::Ident)) {
        const Token& token = advance();
        if (auto ok = noteNode(token.pos); !ok) return std::unexpected(ok.error());
        return makeVar(token.value, token.pos);
    }

    if (check(TokenType::LParen)) {
        advance();
        auto expr = parseExpression();
        if (!expr) return std::unexpected(expr.error());
        auto rparen = expect(TokenType::RParen, "')'");
        if (!rparen) return std::unexpected(rparen.error());
        return expr;
    }

    if (check(TokenType::End)) return incomplete("表达式未结束", peek().pos);
    return fail(std::format("意外的 token: {}", tokenName(peek().type)), peek().pos);
}

} // namespace sc
