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
        if (check(TokenType::Semicolon)) { // 空语句（语句分隔符，不是语句）
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

// ---------------------------------------------------------------- Pratt 驱动

Result<Expr> Parser::parseExpr(int minBp) {
    // 所有递归环（括号、前缀运算符的操作数、中缀运算符的右操作数）都经过这里，
    // 所以深度计数只需要放在这一处。
    if (depth_ >= config_.maxParseDepth) return fail("表达式嵌套过深", peek().pos);
    const DepthGuard guard(depth_);

    // 前缀部分（nud）：查 UNARY_OPS 里的前缀运算符，或基本前缀表。
    const Token& first = peek();
    const PrefixParselet* prefix = nullptr;
    for (const PrefixParselet& candidate : PRIMARY_PREFIX_PARSELETS) {
        if (candidate.token == first.type) {
            prefix = &candidate;
            break;
        }
    }
    const bool isPrefixOperator = findUnary(first.type) != nullptr;
    if (prefix == nullptr && !isPrefixOperator) {
        if (check(TokenType::End)) return incomplete("表达式未结束", first.pos);
        return fail(std::format("意外的 token: {}", tokenName(first.type)), first.pos);
    }

    const Token& head = advance();
    auto operand = isPrefixOperator ? parsePrefixOperator(head) : (this->*(prefix->parse))(head);
    if (!operand) return std::unexpected(operand.error());
    Expr left = std::move(*operand);

    // 中缀部分（led 环）：绑定力决定是否把下一个运算符吸收进来。
    // 这张表就是 BINARY_OPS —— 加一个中缀运算符不需要改本文件。
    while (const BinaryOpInfo* op = findBinary(peek().type)) {
        if (op->leftBp < minBp) break;
        const Token& opToken = advance();
        auto combined = parseBinaryOperator(*op, opToken, std::move(left));
        if (!combined) return std::unexpected(combined.error());
        left = std::move(*combined);
    }
    return left;
}

Result<Stmt> Parser::parseStatement() {
    // 语句 parselet：按起始 token 分派。
    for (const StmtParselet& parselet : STMT_PARSELETS) {
        if (parselet.token != peek().type) continue;
        const Token& keyword = advance();
        return (this->*(parselet.parse))(keyword);
    }

    // 赋值在本语言里是语句而不是表达式（所以 `x = 1` 不打印、`x = y = 1` 不合法），
    // 需要两 token 前瞻 Ident '='。将来若把赋值提成表达式，
    // 就是加一个绑定力最低、右结合的中缀 parselet，这两行即可删掉。
    if (check(TokenType::Ident) && checkNext(TokenType::Assign)) return parseAssignStatement();
    return parseExpressionStatement();
}

// ------------------------------------------------------------ 表达式 parselet

Result<Expr> Parser::parseIntLiteral(const Token& token) {
    mpz_class value;
    if (value.set_str(token.value, 10) != 0)
        return fail(std::format("整数解析失败: {}", token.value), token.pos);
    if (auto ok = noteNode(token.pos); !ok) return std::unexpected(ok.error());
    return makeInt(std::move(value));
}

Result<Expr> Parser::parseFloatLiteral(const Token& token) {
    auto value = Mpfr::fromString(token.value, config_.precision);
    if (!value) return fail(std::format("小数解析失败: {}", token.value), token.pos);
    // MPFR 对"合法但超范围"的字面量会静默给出 inf/nan；默认按策略拒绝。
    if (!config_.allowNonFinite && !value->isFinite())
        return fail(std::format("小数超出可表示范围: {}", token.value), token.pos);
    if (auto ok = noteNode(token.pos); !ok) return std::unexpected(ok.error());
    return makeFloat(std::move(*value));
}

Result<Expr> Parser::parseStringLiteral(const Token& token) {
    if (auto ok = noteNode(token.pos); !ok) return std::unexpected(ok.error());
    return makeStr(token.value);
}

Result<Expr> Parser::parseVariable(const Token& token) {
    if (auto ok = noteNode(token.pos); !ok) return std::unexpected(ok.error());
    return makeVar(token.value, token.pos);
}

Result<Expr> Parser::parseGroup(const Token& token) {
    (void)token; // 分组不产生节点，位置信息用不上
    auto inner = parseExpr(0);
    if (!inner) return std::unexpected(inner.error());
    auto closing = expect(TokenType::RParen, "')'");
    if (!closing) return std::unexpected(closing.error());
    return inner;
}

Result<Expr> Parser::parsePrefixOperator(const Token& token) {
    const UnaryOpInfo* op = findUnary(token.type);
    if (op == nullptr)
        return fail(std::format("无效的前缀运算符: {}", tokenName(token.type)), token.pos);

    auto operand = parseExpr(op->operandBp);
    if (!operand) return std::unexpected(operand.error());
    if (auto ok = noteNode(token.pos); !ok) return std::unexpected(ok.error());
    return makeUnary(token.type, token.pos, std::move(*operand));
}

Result<Expr> Parser::parseBinaryOperator(const BinaryOpInfo& op, const Token& token, Expr left) {
    // 右操作数按 rightBp 解析：下界更高就不再吸收同级运算符（左结合），
    // 下界相同则允许同级继续进右子树（右结合）。
    auto right = parseExpr(op.rightBp);
    if (!right) return std::unexpected(right.error());
    if (auto ok = noteNode(token.pos); !ok) return std::unexpected(ok.error());
    return makeBinary(token.type, token.pos, std::move(left), std::move(*right));
}

// -------------------------------------------------------------- 语句 parselet

Result<Stmt> Parser::parsePrintStatement(const Token& keyword) {
    auto expr = parseExpr(0);
    if (!expr) return std::unexpected(expr.error());
    if (auto ok = endStatement(); !ok) return std::unexpected(ok.error());
    return makePrint(keyword.pos, std::move(*expr));
}

Result<Stmt> Parser::parseAssignStatement() {
    const Token& nameToken = advance(); // Ident
    advance();                          // '='
    auto value = parseExpr(0);
    if (!value) return std::unexpected(value.error());
    if (auto ok = endStatement(); !ok) return std::unexpected(ok.error());
    return makeAssign(nameToken.value, nameToken.pos, std::move(*value));
}

Result<Stmt> Parser::parseExpressionStatement() {
    const int start = peek().pos;
    auto expr = parseExpr(0);
    if (!expr) return std::unexpected(expr.error());
    if (auto ok = endStatement(); !ok) return std::unexpected(ok.error());
    return makeExprStmt(start, std::move(*expr));
}

} // namespace sc
