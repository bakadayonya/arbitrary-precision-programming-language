#include "ast.hpp"

#include <utility>

namespace sc {

Expr makeInt(mpz_class z) { return Expr{IntLit{std::move(z)}}; }

Expr makeFloat(Mpfr f) { return Expr{FloatLit{std::move(f)}}; }

Expr makeStr(std::string value) { return Expr{StrLit{std::move(value)}}; }

Expr makeBool(bool value) { return Expr{BoolLit{value}}; }

Expr makeVar(std::string name, int pos) { return Expr{VarExpr{std::move(name), pos}}; }

Expr makeBinary(TokenType op, int pos, Expr left, Expr right) {
    return Expr{BinaryExpr{op, pos, std::make_unique<Expr>(std::move(left)),
                           std::make_unique<Expr>(std::move(right))}};
}

Expr makeUnary(TokenType op, int pos, Expr operand) {
    return Expr{UnaryExpr{op, pos, std::make_unique<Expr>(std::move(operand))}};
}

Expr makeCall(std::string name, int pos, int builtin, std::vector<Expr> args) {
    return Expr{CallExpr{std::move(name), pos, builtin, std::move(args)}};
}

Stmt makeAssign(std::string name, int pos, Expr value) {
    return Stmt{AssignExpr{std::move(name), pos, std::make_unique<Expr>(std::move(value))}};
}

Stmt makePrint(int pos, Expr expr) {
    return Stmt{PrintStmt{pos, std::make_unique<Expr>(std::move(expr))}};
}

Stmt makeExprStmt(int pos, Expr expr) {
    return Stmt{ExprStmt{pos, std::make_unique<Expr>(std::move(expr))}};
}

Stmt makeBlock(int pos, std::vector<Stmt> body) { return Stmt{BlockStmt{pos, std::move(body)}}; }

Stmt makeIf(int pos, int conditionPos, Expr condition, Stmt thenBranch,
            std::unique_ptr<Stmt> elseBranch) {
    return Stmt{IfStmt{pos, conditionPos, std::make_unique<Expr>(std::move(condition)),
                       std::make_unique<Stmt>(std::move(thenBranch)), std::move(elseBranch)}};
}

Stmt makeWhile(int pos, int conditionPos, Expr condition, Stmt body) {
    return Stmt{WhileStmt{pos, conditionPos, std::make_unique<Expr>(std::move(condition)),
                          std::make_unique<Stmt>(std::move(body))}};
}

Stmt makeFor(int pos, int conditionPos, std::unique_ptr<Stmt> init, std::unique_ptr<Expr> condition,
             std::unique_ptr<Stmt> post, Stmt body) {
    return Stmt{ForStmt{pos, conditionPos, std::move(init), std::move(condition), std::move(post),
                        std::make_unique<Stmt>(std::move(body))}};
}

Stmt makeBreak(int pos) { return Stmt{BreakStmt{pos}}; }

Stmt makeContinue(int pos) { return Stmt{ContinueStmt{pos}}; }

} // namespace sc
