// compiler.cpp — C++23 + GMP/MPFR
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <variant>
#include <memory>
#include <cctype>
#include <expected>
#include <unordered_map>
#include <sstream>
#include <fstream>
#include <format>
#include <print>
#include <optional>
#include <type_traits>
#include <cmath>

#include <gmpxx.h>
#include <mpfr.h>

// ==================== 全局 MPFR 精度 ====================
static constexpr mpfr_prec_t MPFR_PRECISION = 256;  // 约 77 位十进制

// ==================== MPFR C++ 封装 ====================
// 官方没有 mpfr_class，自己包一个 RAII 类
class Mpfr {
    mpfr_t v;
public:
    Mpfr() { mpfr_init2(v, MPFR_PRECISION); mpfr_set_zero(v, 1); }

    explicit Mpfr(double d) {
        mpfr_init2(v, MPFR_PRECISION);
        mpfr_set_d(v, d, MPFR_RNDN);
    }
    explicit Mpfr(long l) {
        mpfr_init2(v, MPFR_PRECISION);
        mpfr_set_si(v, l, MPFR_RNDN);
    }
    explicit Mpfr(const mpz_class& z) {
        mpfr_init2(v, MPFR_PRECISION);
        mpfr_set_z(v, z.get_mpz_t(), MPFR_RNDN);
    }
    // 从字符串解析，返回是否成功
    static std::optional<Mpfr> fromString(std::string_view s) {
        Mpfr r;
        if (mpfr_set_str(r.v, std::string(s).c_str(), 10, MPFR_RNDN) != 0)
            return std::nullopt;
        return r;
    }

    Mpfr(const Mpfr& o) {
        mpfr_init2(v, MPFR_PRECISION);
        mpfr_set(v, o.v, MPFR_RNDN);
    }
    Mpfr(Mpfr&& o) noexcept {
        mpfr_init2(v, MPFR_PRECISION);
        mpfr_swap(v, o.v);
    }
    Mpfr& operator=(const Mpfr& o) {
        if (this != &o) mpfr_set(v, o.v, MPFR_RNDN);
        return *this;
    }
    Mpfr& operator=(Mpfr&& o) noexcept {
        if (this != &o) mpfr_swap(v, o.v);
        return *this;
    }
    ~Mpfr() { mpfr_clear(v); }

    mpfr_ptr get() { return v; }
    mpfr_srcptr get() const { return v; }

    // 算术
    Mpfr operator+(const Mpfr& o) const { Mpfr r; mpfr_add(r.v, v, o.v, MPFR_RNDN); return r; }
    Mpfr operator-(const Mpfr& o) const { Mpfr r; mpfr_sub(r.v, v, o.v, MPFR_RNDN); return r; }
    Mpfr operator*(const Mpfr& o) const { Mpfr r; mpfr_mul(r.v, v, o.v, MPFR_RNDN); return r; }
    Mpfr operator/(const Mpfr& o) const { Mpfr r; mpfr_div(r.v, v, o.v, MPFR_RNDN); return r; }
    Mpfr operator-() const { Mpfr r; mpfr_neg(r.v, v, MPFR_RNDN); return r; }

    bool isZero() const { return mpfr_zero_p(v) != 0; }

    std::string to_string() const {
        // 优先用更紧凑的输出，最多 30 位有效数字
        char* buf = nullptr;
        mpfr_asprintf(&buf, "%.30Rg", v);
        std::string s(buf);
        mpfr_free_str(buf);
        return s;
    }
};

// ==================== 值类型 ====================
struct Value {
    std::variant<mpz_class, Mpfr> data;

    Value() : data(mpz_class(0)) {}
    Value(mpz_class z) : data(std::move(z)) {}
    Value(Mpfr f) : data(std::move(f)) {}

    bool isFloat() const { return std::holds_alternative<Mpfr>(data); }

    // 提升为 Mpfr
    Mpfr toFloat() const {
        if (auto* z = std::get_if<mpz_class>(&data))
            return Mpfr(*z);
        return std::get<Mpfr>(data);
    }

    mpz_class toInt() const {
        if (auto* z = std::get_if<mpz_class>(&data))
            return *z;
        // 浮点转整数（截断）
        mpz_class z;
        mpfr_get_z(z.get_mpz_t(), std::get<Mpfr>(data).get(), MPFR_RNDZ);
        return z;
    }

    std::string to_string() const {
        if (auto* z = std::get_if<mpz_class>(&data))
            return z->get_str();
        return std::get<Mpfr>(data).to_string();
    }

    // 混合类型算术
    static Value add(const Value& a, const Value& b) {
        if (!a.isFloat() && !b.isFloat())
            return Value(std::get<mpz_class>(a.data) + std::get<mpz_class>(b.data));
        return Value(a.toFloat() + b.toFloat());
    }
    static Value sub(const Value& a, const Value& b) {
        if (!a.isFloat() && !b.isFloat())
            return Value(std::get<mpz_class>(a.data) - std::get<mpz_class>(b.data));
        return Value(a.toFloat() - b.toFloat());
    }
    static Value mul(const Value& a, const Value& b) {
        if (!a.isFloat() && !b.isFloat())
            return Value(std::get<mpz_class>(a.data) * std::get<mpz_class>(b.data));
        return Value(a.toFloat() * b.toFloat());
    }
    static Value div(const Value& a, const Value& b, bool& divByZero) {
        divByZero = false;
        if (!a.isFloat() && !b.isFloat()) {
            const auto& zb = std::get<mpz_class>(b.data);
            if (zb == 0) { divByZero = true; return Value(); }
            // 整数除法：向零截断
            return Value(std::get<mpz_class>(a.data) / zb);
        }
        Mpfr fb = b.toFloat();
        if (fb.isZero()) { divByZero = true; return Value(); }
        return Value(a.toFloat() / fb);
    }

    bool isTruthy() const {
        if (auto* z = std::get_if<mpz_class>(&data)) return *z != 0;
        return !std::get<Mpfr>(data).isZero();
    }
};

// ==================== 错误类型 ====================
struct Error {
    std::string message;
    int pos = -1;

    std::string to_string() const {
        if (pos >= 0) return std::format("{} (位置 {})", message, pos);
        return message;
    }
};

using Result = std::expected<void, Error>;

// ==================== Token ====================
enum class TokenType {
    Number,       // 整数
    Float,        // 小数
    Plus, Minus, Star, Slash,
    LParen, RParen, Ident, Assign,
    Print, Semicolon, End
};

struct Token {
    TokenType type;
    std::string value;
    int pos;
};

constexpr std::string_view tokenName(TokenType t) {
    switch (t) {
        case TokenType::Number:    return "整数";
        case TokenType::Float:     return "小数";
        case TokenType::Plus:      return "+";
        case TokenType::Minus:     return "-";
        case TokenType::Star:      return "*";
        case TokenType::Slash:     return "/";
        case TokenType::LParen:    return "(";
        case TokenType::RParen:    return ")";
        case TokenType::Ident:     return "标识符";
        case TokenType::Assign:    return "=";
        case TokenType::Print:     return "print";
        case TokenType::Semicolon: return ";";
        case TokenType::End:       return "EOF";
    }
    return "?";
}

// ==================== Lexer ====================
class Lexer {
    std::string_view src;
    size_t pos = 0;
public:
    explicit Lexer(std::string_view s) : src(s) {}

    std::expected<std::vector<Token>, Error> tokenize() {
        std::vector<Token> tokens;
        pos = 0;
        while (pos < src.size()) {
            char c = src[pos];
            if (std::isspace(static_cast<unsigned char>(c))) { pos++; continue; }

            // 数字开头（含 . 起始的小数）
            if (std::isdigit(static_cast<unsigned char>(c)) ||
                (c == '.' && pos + 1 < src.size() &&
                 std::isdigit(static_cast<unsigned char>(src[pos + 1])))) {
                tokens.push_back(readNumber());
                continue;
            }
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                tokens.push_back(readIdentifier());
                continue;
            }

            auto push = [&](TokenType t, std::string v) {
                tokens.push_back({t, std::move(v), static_cast<int>(pos++)});
            };
            switch (c) {
                case '+': push(TokenType::Plus, "+"); break;
                case '-': push(TokenType::Minus, "-"); break;
                case '*': push(TokenType::Star, "*"); break;
                case '/': push(TokenType::Slash, "/"); break;
                case '(': push(TokenType::LParen, "("); break;
                case ')': push(TokenType::RParen, ")"); break;
                case '=': push(TokenType::Assign, "="); break;
                case ';': push(TokenType::Semicolon, ";"); break;
                default:
                    return std::unexpected(Error{
                        std::format("非法字符 '{}'", c), static_cast<int>(pos)});
            }
        }
        tokens.push_back({TokenType::End, "", static_cast<int>(pos)});
        return tokens;
    }
private:
    Token readNumber() {
        size_t start = pos;
        bool isFloat = false;

        while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) pos++;

        // 小数点
        if (pos < src.size() && src[pos] == '.') {
            // 避免和别的语法冲突：仅当后面是数字或非标识符时才当小数
            if (pos + 1 < src.size() && std::isdigit(static_cast<unsigned char>(src[pos + 1]))) {
                isFloat = true;
                pos++;
                while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) pos++;
            } else if (pos + 1 >= src.size() ||
                       !std::isalpha(static_cast<unsigned char>(src[pos + 1]))) {
                // "3." 也算小数
                isFloat = true;
                pos++;
            }
        }

        // 科学计数法 e/E
        if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E')) {
            size_t save = pos;
            pos++;
            if (pos < src.size() && (src[pos] == '+' || src[pos] == '-')) pos++;
            if (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) {
                isFloat = true;
                while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) pos++;
            } else {
                pos = save; // 不是科学计数法，回退
            }
        }

        return {isFloat ? TokenType::Float : TokenType::Number,
                std::string(src.substr(start, pos - start)),
                static_cast<int>(start)};
    }
    Token readIdentifier() {
        size_t start = pos;
        while (pos < src.size() &&
               (std::isalnum(static_cast<unsigned char>(src[pos])) || src[pos] == '_')) pos++;
        std::string name(src.substr(start, pos - start));
        TokenType t = (name == "print") ? TokenType::Print : TokenType::Ident;
        return {t, std::move(name), static_cast<int>(start)};
    }
};

// ==================== AST ====================
struct Expr;
struct Stmt;

struct IntLit   { mpz_class value; };
struct FloatLit { Mpfr value; };
struct VarExpr  { std::string name; };

struct BinaryExpr {
    TokenType op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
};

struct AssignExpr {
    std::string name;
    std::unique_ptr<Expr> value;
};

struct PrintStmt {
    std::unique_ptr<Expr> expr;
};

using ExprVariant = std::variant<IntLit, FloatLit, VarExpr, BinaryExpr>;
using StmtVariant = std::variant<AssignExpr, PrintStmt, Expr>;

struct Expr { ExprVariant v; };
struct Stmt { StmtVariant v; };

// 便捷构造
static Expr makeInt(mpz_class z)   { return Expr{IntLit{std::move(z)}}; }
static Expr makeFloat(Mpfr f)      { return Expr{FloatLit{std::move(f)}}; }
static Expr makeVar(std::string n) { return Expr{VarExpr{std::move(n)}}; }
static Expr makeBinary(TokenType op, Expr l, Expr r) {
    return Expr{BinaryExpr{op,
        std::make_unique<Expr>(std::move(l)),
        std::make_unique<Expr>(std::move(r))}};
}
static Stmt makeAssign(std::string n, Expr v) {
    return Stmt{AssignExpr{std::move(n), std::make_unique<Expr>(std::move(v))}};
}
static Stmt makePrint(Expr e) {
    return Stmt{PrintStmt{std::make_unique<Expr>(std::move(e))}};
}
static Stmt makeExprStmt(Expr e) { return Stmt{std::move(e)}; }

// ==================== Parser ====================
class Parser {
    std::vector<Token> tokens;
    size_t pos = 0;
public:
    explicit Parser(std::vector<Token> t) : tokens(std::move(t)) {}

    std::expected<std::vector<Stmt>, Error> parse() {
        std::vector<Stmt> stmts;
        while (peek().type != TokenType::End) {
            auto s = parseStatement();
            if (!s) return std::unexpected(s.error());
            stmts.push_back(std::move(*s));
        }
        return stmts;
    }
private:
    const Token& peek() const { return tokens[pos]; }
    const Token& advance() { return tokens[pos++]; }
    bool check(TokenType t) const { return peek().type == t; }
    bool checkNext(TokenType t) const {
        return pos + 1 < tokens.size() && tokens[pos + 1].type == t;
    }

    std::expected<Token, Error> expect(TokenType t, std::string_view msg) {
        if (!check(t))
            return std::unexpected(Error{
                std::format("期望 {}，但遇到 {}", msg, tokenName(peek().type)),
                peek().pos});
        return advance();
    }

    std::expected<Stmt, Error> parseStatement() {
        if (check(TokenType::Print)) {
            advance();
            auto expr = parseExpression();
            if (!expr) return std::unexpected(expr.error());
            auto semi = expect(TokenType::Semicolon, "';'");
            if (!semi) return std::unexpected(semi.error());
            return makePrint(std::move(*expr));
        }
        if (check(TokenType::Ident) && checkNext(TokenType::Assign)) {
            std::string name = advance().value;
            advance();
            auto expr = parseExpression();
            if (!expr) return std::unexpected(expr.error());
            auto semi = expect(TokenType::Semicolon, "';'");
            if (!semi) return std::unexpected(semi.error());
            return makeAssign(std::move(name), std::move(*expr));
        }
        auto expr = parseExpression();
        if (!expr) return std::unexpected(expr.error());
        if (check(TokenType::Semicolon)) advance();
        return makeExprStmt(std::move(*expr));
    }

    std::expected<Expr, Error> parseExpression() { return parseAddSub(); }

    std::expected<Expr, Error> parseAddSub() {
        auto leftExp = parseMulDiv();
        if (!leftExp) return std::unexpected(leftExp.error());
        Expr left = std::move(*leftExp);

        while (check(TokenType::Plus) || check(TokenType::Minus)) {
            TokenType op = advance().type;
            auto rightExp = parseMulDiv();
            if (!rightExp) return std::unexpected(rightExp.error());
            left = makeBinary(op, std::move(left), std::move(*rightExp));
        }
        return left;
    }

    std::expected<Expr, Error> parseMulDiv() {
        auto leftExp = parseUnary();
        if (!leftExp) return std::unexpected(leftExp.error());
        Expr left = std::move(*leftExp);

        while (check(TokenType::Star) || check(TokenType::Slash)) {
            TokenType op = advance().type;
            auto rightExp = parseUnary();
            if (!rightExp) return std::unexpected(rightExp.error());
            left = makeBinary(op, std::move(left), std::move(*rightExp));
        }
        return left;
    }

    std::expected<Expr, Error> parseUnary() {
        if (check(TokenType::Minus)) {
            advance();
            auto operandExp = parseUnary();
            if (!operandExp) return std::unexpected(operandExp.error());
            return makeBinary(TokenType::Minus, makeInt(mpz_class(0)), std::move(*operandExp));
        }
        return parsePrimary();
    }

    std::expected<Expr, Error> parsePrimary() {
        if (check(TokenType::Number)) {
            const Token& t = advance();
            mpz_class z;
            if (z.set_str(t.value, 10) != 0)
                return std::unexpected(Error{"整数解析失败", t.pos});
            return makeInt(std::move(z));
        }
        if (check(TokenType::Float)) {
            const Token& t = advance();
            auto f = Mpfr::fromString(t.value);
            if (!f)
                return std::unexpected(Error{"小数解析失败", t.pos});
            return makeFloat(std::move(*f));
        }
        if (check(TokenType::Ident)) {
            return makeVar(advance().value);
        }
        if (check(TokenType::LParen)) {
            advance();
            auto expr = parseExpression();
            if (!expr) return std::unexpected(expr.error());
            auto rp = expect(TokenType::RParen, "')'");
            if (!rp) return std::unexpected(rp.error());
            return expr;
        }
        return std::unexpected(Error{
            std::format("意外的 token: {}", tokenName(peek().type)),
            peek().pos});
    }
};

// ==================== 字节码 ====================
// 常量池 + 变量槽
enum class OpCode {
    PushConst,   // 压入常量池第 i 个值
    Load,        // 加载变量槽 i
    Store,       // 存储到变量槽 i
    Add, Sub, Mul, Div,
    Print,
    Halt
};

constexpr std::string_view opName(OpCode op) {
    switch (op) {
        case OpCode::PushConst: return "PUSH";
        case OpCode::Load:      return "LOAD";
        case OpCode::Store:     return "STORE";
        case OpCode::Add:       return "ADD";
        case OpCode::Sub:       return "SUB";
        case OpCode::Mul:       return "MUL";
        case OpCode::Div:       return "DIV";
        case OpCode::Print:     return "PRINT";
        case OpCode::Halt:      return "HALT";
    }
    return "?";
}

struct Instruction {
    OpCode op;
    int operand = 0;
};

// ==================== Compiler ====================
class Compiler {
    std::vector<Instruction> code;
    std::vector<Value> constPool;
    std::unordered_map<std::string, int> varTable;
    int nextVarIndex = 0;

    int addConst(Value v) {
        constPool.push_back(std::move(v));
        return static_cast<int>(constPool.size() - 1);
    }

public:
    const auto& varTableRef() const { return varTable; }
    const auto& constPoolRef() const { return constPool; }
    int numVars() const { return nextVarIndex; }

    std::expected<std::vector<Instruction>, Error>
    compile(const std::vector<Stmt>& stmts) {
        code.clear();
        constPool.clear();
        for (const auto& s : stmts) {
            if (auto r = genStmt(s); !r) return std::unexpected(r.error());
        }
        code.push_back({OpCode::Halt});
        return code;
    }

private:
    int varIndex(const std::string& name) {
        auto it = varTable.find(name);
        if (it != varTable.end()) return it->second;
        int idx = nextVarIndex++;
        varTable[name] = idx;
        return idx;
    }

    Result genExpr(const Expr& e) {
        return std::visit([this](const auto& node) -> Result {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, IntLit>) {
                code.push_back({OpCode::PushConst, addConst(Value(node.value))});
            } else if constexpr (std::is_same_v<T, FloatLit>) {
                code.push_back({OpCode::PushConst, addConst(Value(node.value))});
            } else if constexpr (std::is_same_v<T, VarExpr>) {
                code.push_back({OpCode::Load, varIndex(node.name)});
            } else if constexpr (std::is_same_v<T, BinaryExpr>) {
                if (auto r = genExpr(*node.left); !r) return r;
                if (auto r = genExpr(*node.right); !r) return r;
                switch (node.op) {
                    case TokenType::Plus:  code.push_back({OpCode::Add}); break;
                    case TokenType::Minus: code.push_back({OpCode::Sub}); break;
                    case TokenType::Star:  code.push_back({OpCode::Mul}); break;
                    case TokenType::Slash: code.push_back({OpCode::Div}); break;
                    default: return std::unexpected(Error{"无效的二元运算符"});
                }
            }
            return {};
        }, e.v);
    }

    Result genStmt(const Stmt& s) {
        return std::visit([this](const auto& node) -> Result {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, AssignExpr>) {
                if (auto r = genExpr(*node.value); !r) return r;
                code.push_back({OpCode::Store, varIndex(node.name)});
            } else if constexpr (std::is_same_v<T, PrintStmt>) {
                if (auto r = genExpr(*node.expr); !r) return r;
                code.push_back({OpCode::Print});
            } else if constexpr (std::is_same_v<T, Expr>) {
                if (auto r = genExpr(node); !r) return r;
                code.push_back({OpCode::Print});
            }
            return {};
        }, s.v);
    }
};

// ==================== VM ====================
class VM {
    std::vector<Value> stack;
    std::vector<Value> variables;
    std::vector<Value> constPool; // 拷贝一份，随代码一起运行

public:
    void ensureVars(int n) {
        if (static_cast<int>(variables.size()) < n)
            variables.resize(n);
    }

    std::expected<void, Error>
    run(const std::vector<Instruction>& code,
        const std::vector<Value>& constants,
        int numVars) {
        constPool = constants;
        ensureVars(numVars);
        stack.clear();

        size_t pc = 0;
        while (pc < code.size()) {
            const auto& inst = code[pc++];
            switch (inst.op) {
                case OpCode::PushConst:
                    stack.push_back(constPool[inst.operand]);
                    break;
                case OpCode::Load:
                    stack.push_back(variables[inst.operand]);
                    break;
                case OpCode::Store:
                    variables[inst.operand] = stack.back();
                    stack.pop_back();
                    break;
                case OpCode::Add: case OpCode::Sub:
                case OpCode::Mul: case OpCode::Div: {
                    if (stack.size() < 2) return std::unexpected(Error{"栈下溢"});
                    Value b = stack.back(); stack.pop_back();
                    Value a = stack.back(); stack.pop_back();
                    Value r;
                    bool divZero = false;
                    switch (inst.op) {
                        case OpCode::Add: r = Value::add(a, b); break;
                        case OpCode::Sub: r = Value::sub(a, b); break;
                        case OpCode::Mul: r = Value::mul(a, b); break;
                        case OpCode::Div: r = Value::div(a, b, divZero); break;
                        default: break;
                    }
                    if (divZero) return std::unexpected(Error{"除零错误"});
                    stack.push_back(std::move(r));
                    break;
                }
                case OpCode::Print:
                    if (stack.empty()) return std::unexpected(Error{"栈下溢"});
                    std::println("{}", stack.back().to_string());
                    stack.pop_back();
                    break;
                case OpCode::Halt:
                    return {};
            }
        }
        return {};
    }
};

// ==================== 反汇编 ====================
static void dumpBytecode(const std::vector<Instruction>& code,
                         const std::vector<Value>& constPool) {
    for (size_t i = 0; i < code.size(); ++i) {
        const auto& ins = code[i];
        if (ins.op == OpCode::PushConst)
            std::println("{:>3}: {:5} [{}] {}", i, opName(ins.op), ins.operand,
                         constPool[ins.operand].to_string());
        else if (ins.op == OpCode::Load || ins.op == OpCode::Store)
            std::println("{:>3}: {:5} {}", i, opName(ins.op), ins.operand);
        else
            std::println("{:>3}: {}", i, opName(ins.op));
    }
}

// ==================== Engine ====================
struct Engine {
    Compiler compiler;
    VM vm;
    bool dump = false;

    Result runSource(std::string_view source) {
        Lexer lexer(source);
        auto tokens = lexer.tokenize();
        if (!tokens) return std::unexpected(tokens.error());

        Parser parser(std::move(*tokens));
        auto ast = parser.parse();
        if (!ast) return std::unexpected(ast.error());

        auto bytecode = compiler.compile(*ast);
        if (!bytecode) return std::unexpected(bytecode.error());

        if (dump) {
            std::println("--- bytecode ---");
            dumpBytecode(*bytecode, compiler.constPoolRef());
            std::println("--- constants ---");
            for (size_t i = 0; i < compiler.constPoolRef().size(); ++i)
                std::println("  [{}] {}", i, compiler.constPoolRef()[i].to_string());
            std::println("----------------");
        }
        return vm.run(*bytecode, compiler.constPoolRef(), compiler.numVars());
    }
};

// ==================== 命令行参数 ====================
struct Options {
    bool showHelp = false;
    bool dump = false;
    std::optional<std::string> expression;
    std::optional<std::string> filename;
};

static void printHelp(std::string_view prog) {
    std::println(
        "用法: {} [选项]\n\n"
        "选项:\n"
        "  -e <expr>    直接执行表达式\n"
        "  -f <file>    执行源文件\n"
        "  -d, --dump   显示生成的字节码\n"
        "  -h, --help   显示帮助\n\n"
        "若不指定 -e 或 -f，则进入交互式 REPL 模式。\n\n"
        "数值类型:\n"
        "  整数       任意精度 (GMP)，如  123456789012345678901234567890\n"
        "  小数       高精度浮点 (MPFR, {} 位)，如  3.14159  1.5e10  .25\n"
        "  混合运算   整数自动提升为小数\n"
        "  整数除法   向零截断，要浮点结果写  1.0 / 3\n\n"
        "示例:\n"
        "  {} -e \"print 2.0 / 3\"\n"
        "  {} -e \"print 2**100\"\n"
        "  {} -f test.lang -d",
        prog, MPFR_PRECISION, prog, prog, prog);
}

static std::expected<Options, Error> parseArgs(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "-h" || a == "--help") opt.showHelp = true;
        else if (a == "-d" || a == "--dump") opt.dump = true;
        else if (a == "-e") {
            if (i + 1 >= argc) return std::unexpected(Error{"-e 需要一个参数"});
            opt.expression = argv[++i];
        } else if (a == "-f") {
            if (i + 1 >= argc) return std::unexpected(Error{"-f 需要一个参数"});
            opt.filename = argv[++i];
        } else {
            return std::unexpected(Error{std::format("未知选项: {}", a)});
        }
    }
    return opt;
}

// ==================== REPL ====================
static void repl() {
    Engine engine;
    std::println("简单编译器 REPL (C++23 + GMP/MPFR, 精度 {} 位)", MPFR_PRECISION);
    std::println(":help 查看帮助, :quit 退出");

    std::string line;
    std::string pending;

    while (true) {
        std::print("{}", pending.empty() ? ">>> " : "... ");
        std::cout.flush();
        if (!std::getline(std::cin, line)) break;

        if (pending.empty()) {
            if (line == ":quit" || line == ":q") break;
            if (line == ":help" || line == ":h") {
                std::println(
                    "  :help            显示帮助\n"
                    "  :quit            退出\n"
                    "  :dump on|off     开关字节码显示\n"
                    "  :vars            显示变量表\n"
                    "  :consts          显示常量池\n"
                    "  :prec            显示当前 MPFR 精度\n\n"
                    "  输入表达式直接求值，例如:\n"
                    "    x = 12345678901234567890\n"
                    "    2.0 / 3\n"
                    "    1.5e10 * 2\n"
                    "    print (1 + 2.5) * 3");
                continue;
            }
            if (line.starts_with(":dump")) {
                std::string_view arg = line.size() > 5 ? std::string_view(line).substr(5) : "";
                engine.dump = !(arg.find("off") != std::string_view::npos);
                std::println("dump = {}", engine.dump ? "on" : "off");
                continue;
            }
            if (line == ":vars") {
                if (engine.compiler.varTableRef().empty())
                    std::println("  (空)");
                else
                    for (const auto& [name, slot] : engine.compiler.varTableRef())
                        std::println("  {} -> slot {}", name, slot);
                continue;
            }
            if (line == ":consts") {
                const auto& cp = engine.compiler.constPoolRef();
                if (cp.empty()) std::println("  (空)");
                else for (size_t i = 0; i < cp.size(); ++i)
                    std::println("  [{}] {}", i, cp[i].to_string());
                continue;
            }
            if (line == ":prec") {
                std::println("  MPFR 精度 = {} 位 (约 {} 位十进制)",
                             MPFR_PRECISION,
                             static_cast<int>(MPFR_PRECISION * 0.30103));
                continue;
            }
        }

        if (line.empty()) continue;

        if (!pending.empty()) pending += "\n";
        pending += line;

        auto result = engine.runSource(pending);
        if (result) {
            pending.clear();
        } else {
            std::string trimmed = pending;
            while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back())))
                trimmed.pop_back();
            if (!trimmed.empty() && trimmed.back() == ';') {
                std::println(std::cerr, "错误: {}", result.error().to_string());
                pending.clear();
            }
        }
    }
}

// ==================== main ====================
int main(int argc, char** argv) {
    auto opt = parseArgs(argc, argv);
    if (!opt) {
        std::println(std::cerr, "错误: {}", opt.error().to_string());
        return 1;
    }

    if (opt->showHelp) {
        printHelp(argv[0]);
        return 0;
    }

    if (opt->expression) {
        Engine engine;
        engine.dump = opt->dump;
        if (auto r = engine.runSource(*opt->expression); !r) {
            std::println(std::cerr, "错误: {}", r.error().to_string());
            return 1;
        }
        return 0;
    }

    if (opt->filename) {
        std::ifstream in(*opt->filename);
        if (!in) {
            std::println(std::cerr, "无法打开文件: {}", *opt->filename);
            return 1;
        }
        std::stringstream ss;
        ss << in.rdbuf();
        Engine engine;
        engine.dump = opt->dump;
        if (auto r = engine.runSource(ss.str()); !r) {
            std::println(std::cerr, "错误: {}", r.error().to_string());
            return 1;
        }
        return 0;
    }

    repl();
    return 0;
}