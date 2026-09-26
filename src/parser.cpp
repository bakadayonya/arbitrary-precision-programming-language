#include "parser.hpp"

#include "builtins.hpp"
#include "operators.hpp"

#include <format>
#include <memory>
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

// ---------------------------------------------------------------- Pratt 驱动

Result<Expr> Parser::parseExpr(int minBp) {
    // 所有递归环（括号、前缀运算符的操作数、中缀运算符的右操作数）都经过这里，
    // 所以深度计数只需要放在这一处。
    if (exprDepth_ >= config_.maxParseDepth) return fail("表达式嵌套过深", peek().pos);
    const DepthGuard guard(exprDepth_);

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

// -------------------------------------------------------------- 语句列表驱动

Result<std::vector<Stmt>> Parser::parse() {
    // 预算按"顶层语句"重置一次。**不能在 parseStatementList 里重置深度**：
    // 那会让每一层块的进入都把深度清零，嵌套深度永远涨不上去，
    // 5 万层嵌套块就会在解析期打穿 C++ 调用栈（段错误）而不是报"嵌套过深"。
    nodes_ = 0;
    exprDepth_ = 0;
    stmtDepth_ = 0;
    return parseStatementList(TokenType::End);
}

/// 一条列表里的语句解析完之后（语句自己的解析函数一律不吃分号），在列表层统一检查：
/// 分号（分隔符）或列表结束都算合法收尾，其他就说明漏了分号。
/// "列表结束"两种都要认：程序顶层的列表 token 是 `End`，但 `}` 也是一个块语句的
/// 合法收尾（块自己会消费它）；反之块里的列表 token 是 `}`，此时 `End` 说明输入
/// 在块里就结束了。只认其中一种会让 `{ print 1 } print 2` 里的 `}` 被内层列表当成
/// "漏了分号"，块随后把 `}` 消费掉，外层列表就撞上 `print`。
Status Parser::finishStatement(TokenType end) {
    if (check(TokenType::Semicolon) || check(end) || check(TokenType::End) ||
        check(TokenType::RBrace))
        return {};
    return fail(std::format("期望 ';'，但遇到 {}", tokenName(peek().type)), peek().pos);
}

/// for 的子句本身不是语句列表的成员，但它里面的块仍然要按列表规则检查收尾。
Status Parser::registerSubStatements(const Stmt& stmt) {
    if (const auto* block = std::get_if<BlockStmt>(&stmt.v)) {
        for (const Stmt& inner : block->body) {
            if (auto status = registerSubStatements(inner); !status) return status;
        }
    }
    return finishStatement(TokenType::RParen);
}

Result<std::vector<Stmt>> Parser::parseStatementList(TokenType end) {
    std::vector<Stmt> stmts;
    while (true) {
        // 分号是纯分隔符：连续多个就是空语句，前导/尾随的也都合法
        //（`;;;`、`{ print 1;; }`、`print x;`）。
        while (check(TokenType::Semicolon))
            advance();
        if (check(end)) break;
        if (check(TokenType::End))
            return incomplete(std::format("输入未结束：缺少 {}", tokenName(end)), peek().pos);

        nodes_ = 0; // 单条语句的节点预算；表达式深度在 parseExpr 里独立累计
        auto stmt = parseStatement();
        if (!stmt) return std::unexpected(stmt.error());
        if (auto status = finishStatement(end); !status) return std::unexpected(status.error());
        stmts.push_back(std::move(*stmt));
    }
    // 列表自己的结束 token 由列表消费：块列表（end == '}'）消费掉自己的 '}'，
    // 这样"块语句"整体只由它内部的列表构成，parseBlockStatement 不再自己 expect。
    // 顶层列表（end == End）不消费任何东西——那是输入的结尾。
    if (check(end) && check(TokenType::RBrace)) advance();
    return stmts;
}

Result<Stmt> Parser::parseStatement() {
    // 语句也能递归嵌套（`if (c) { if (d) { ... } }`、深层块），而且这条路径
    // 不经过 parseExpr，所以深度防护必须在这里也做一次——否则 5 万层嵌套块
    // 会在解析期把 C++ 调用栈打穿（段错误），而不是报"嵌套过深"。
    if (stmtDepth_ >= config_.maxParseDepth) return fail("语句嵌套过深", peek().pos);
    const DepthGuard guard(stmtDepth_);

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

/// 条件：`( 表达式 )`。返回表达式与**条件的报错位置**（条件表达式的起点）——
/// 运行期的"条件必须是布尔"就指这里，而不是指整个 if/while 关键字。
Result<std::pair<Expr, int>> Parser::parseCondition(std::string_view what) {
    auto open = expect(TokenType::LParen, what);
    if (!open) return std::unexpected(open.error());
    const int conditionPos = peek().pos;
    auto condition = parseExpr(0);
    if (!condition) return std::unexpected(condition.error());
    auto close = expect(TokenType::RParen, "')'");
    if (!close) return std::unexpected(close.error());
    return std::pair<Expr, int>{std::move(*condition), conditionPos};
}

/// 一个 for 子句（初始化或后置）：一条赋值或一个表达式，可省略。
/// 注意这里返回的是**表达式/赋值本身**，不是语句——for 的子句不是语句列表的成员，
/// 所以不需要在列表层（parseStatementList）登记，也不带结尾分号。
Result<std::unique_ptr<Stmt>> Parser::parseForClause() {
    // 空子句：`for (;;)`、`for (i = 0;;)`、`for (i = 0; i < 3;)`。
    if (check(TokenType::Semicolon) || check(TokenType::RParen)) return std::unique_ptr<Stmt>();

    nodes_ = 0; // 子句自己有节点预算；深度计数保留（for 本身还在递归里）
    if (check(TokenType::Ident) && checkNext(TokenType::Assign)) {
        const Token& nameToken = advance(); // Ident
        advance();                          // '='
        auto value = parseExpr(0);
        if (!value) return std::unexpected(value.error());
        return std::make_unique<Stmt>(
            makeAssign(nameToken.value, nameToken.pos, std::move(*value)));
    }

    const int start = peek().pos;
    auto expr = parseExpr(0);
    if (!expr) return std::unexpected(expr.error());
    // for 子句不是列表成员：包成表达式语句只是为了复用 genStmt（求值一次，不打印）。
    return std::make_unique<Stmt>(makeExprStmt(start, std::move(*expr)));
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

Result<Expr> Parser::parseBoolLiteral(const Token& token) {
    if (auto ok = noteNode(token.pos); !ok) return std::unexpected(ok.error());
    return makeBool(token.type == TokenType::True);
}

Result<Expr> Parser::parseVariable(const Token& token) {
    // `名字(` 是内建函数调用。引入调用语法之前，"标识符后紧跟左括号"是语法错误
    // （解析器会报"期望 ';'"或"意外的 token"），所以这个前瞻不会改变任何已有程序的语义。
    if (check(TokenType::LParen)) return parseCall(token);
    if (auto ok = noteNode(token.pos); !ok) return std::unexpected(ok.error());
    return makeVar(token.value, token.pos);
}

Result<Expr> Parser::parseCall(const Token& nameToken) {
    advance(); // '('

    std::vector<Expr> args;
    // 实参之间用 ',' 分隔，允许零个实参（常量类内建函数：pi()、e()）。
    // 每个实参都走 parseExpr(0)，因此逗号天然成为最低优先级的分隔符，
    // `min(1 + 2, 3)` 不会被解析成 `min(1 + (2, 3))`。
    if (!check(TokenType::RParen)) {
        while (true) {
            auto arg = parseExpr(0);
            if (!arg) return std::unexpected(arg.error());
            args.push_back(std::move(*arg));
            if (!check(TokenType::Comma)) break;
            advance();
        }
    }
    // 这里用 expect 而不是直接检查：REPL 里 `sqrt(2` 应当报"输入未结束"（可续行），
    // 而不是"意外的 token"。
    auto closing = expect(TokenType::RParen, "')'");
    if (!closing) return std::unexpected(closing.error());

    const int arity = static_cast<int>(args.size());
    const int builtin = findBuiltin(nameToken.value, arity);
    if (builtin < 0) return fail(builtinLookupError(nameToken.value, arity), nameToken.pos);
    if (auto ok = noteNode(nameToken.pos); !ok) return std::unexpected(ok.error());
    return makeCall(nameToken.value, nameToken.pos, builtin, std::move(args));
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
    return makePrint(keyword.pos, std::move(*expr));
}

Result<Stmt> Parser::parseAssignStatement() {
    const Token& nameToken = advance(); // Ident
    advance();                          // '='
    auto value = parseExpr(0);
    if (!value) return std::unexpected(value.error());
    return makeAssign(nameToken.value, nameToken.pos, std::move(*value));
}

Result<Stmt> Parser::parseExpressionStatement() {
    const int start = peek().pos;
    auto expr = parseExpr(0);
    if (!expr) return std::unexpected(expr.error());
    return makeExprStmt(start, std::move(*expr));
}

Result<Stmt> Parser::parseBlockStatement(const Token& keyword) {
    // '}' 由 parseStatementList 自己消费（见那里的注释），这里不再 expect 一次。
    auto body = parseStatementList(TokenType::RBrace);
    if (!body) return std::unexpected(body.error());
    return makeBlock(keyword.pos, std::move(*body));
}

Result<Stmt> Parser::parseIfStatement(const Token& keyword) {
    auto condition = parseCondition("if 的条件");
    if (!condition) return std::unexpected(condition.error());
    auto thenBranch = parseStatement();
    if (!thenBranch) return std::unexpected(thenBranch.error());

    std::unique_ptr<Stmt> elseBranch;
    // then 与 else 之间的分号是可选的，但**跳过之前必须先记住位置**：
    // 语句自己的解析函数一律不吃分号（分号归列表层），所以这里要替列表看一眼
    // `; else`。若后面并不是 else，就得把分号还回去，否则
    // `while (c) { if (x) continue; print i }` 里的 if 会把 continue 后面的
    // 分隔符吃掉，列表层看到孤立的 print 就报"期望 ';'"。
    const std::size_t beforeSeparators = pos_;
    while (check(TokenType::Semicolon))
        advance();
    if (check(TokenType::Else)) {
        advance();
        auto parsed = parseStatement();
        if (!parsed) return std::unexpected(parsed.error());
        elseBranch = std::make_unique<Stmt>(std::move(*parsed));
    } else {
        pos_ = beforeSeparators; // 没有 else：分隔符留给列表
    }
    return makeIf(keyword.pos, condition->second, std::move(condition->first),
                  std::move(*thenBranch), std::move(elseBranch));
}

Result<Stmt> Parser::parseWhileStatement(const Token& keyword) {
    auto condition = parseCondition("while 的条件");
    if (!condition) return std::unexpected(condition.error());
    auto body = parseStatement();
    if (!body) return std::unexpected(body.error());
    return makeWhile(keyword.pos, condition->second, std::move(condition->first), std::move(*body));
}

Result<Stmt> Parser::parseForStatement(const Token& keyword) {
    auto open = expect(TokenType::LParen, "for 的 '('");
    if (!open) return std::unexpected(open.error());

    auto init = parseForClause();
    if (!init) return std::unexpected(init.error());
    // for 的子句不是列表成员，所以这里要自己登记（parseStatementList 只管列表里那些）。
    if (*init != nullptr) {
        if (auto ok = registerSubStatements(**init); !ok) return std::unexpected(ok.error());
    }
    auto firstSeparator = expect(TokenType::Semicolon, "for 里第一个 ';'");
    if (!firstSeparator) return std::unexpected(firstSeparator.error());

    std::unique_ptr<Expr> condition;
    int conditionPos = keyword.pos; // 没有条件子句时退回关键字位置
    if (!check(TokenType::Semicolon)) {
        conditionPos = peek().pos;
        auto parsed = parseExpr(0);
        if (!parsed) return std::unexpected(parsed.error());
        condition = std::make_unique<Expr>(std::move(*parsed));
    }
    auto secondSeparator = expect(TokenType::Semicolon, "for 里第二个 ';'");
    if (!secondSeparator) return std::unexpected(secondSeparator.error());

    auto post = parseForClause();
    if (!post) return std::unexpected(post.error());
    if (*post != nullptr) {
        if (auto ok = registerSubStatements(**post); !ok) return std::unexpected(ok.error());
    }
    auto close = expect(TokenType::RParen, "for 的 ')'");
    if (!close) return std::unexpected(close.error());

    auto body = parseStatement();
    if (!body) return std::unexpected(body.error());
    return makeFor(keyword.pos, conditionPos, std::move(*init), std::move(condition),
                   std::move(*post), std::move(*body));
}

Result<Stmt> Parser::parseBreakStatement(const Token& keyword) { return makeBreak(keyword.pos); }

Result<Stmt> Parser::parseContinueStatement(const Token& keyword) {
    return makeContinue(keyword.pos);
}

} // namespace sc
