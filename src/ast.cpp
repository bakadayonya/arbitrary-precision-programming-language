#include "ast.hpp"

#include <utility>

namespace sc {

Expr makeInt(mpz_class z) { return Expr{IntLit{std::move(z)}}; }

Expr makeFloat(Mpfr f) { return Expr{FloatLit{std::move(f)}}; }

Expr makeStr(std::string value) { return Expr{StrLit{std::move(value)}}; }

Expr makeVar(std::string name, int pos) { return Expr{VarExpr{std::move(name), pos}}; }

Expr makeBinary(TokenType op, int pos, Expr left, Expr right) {
    return Expr{BinaryExpr{op, pos, std::make_unique<Expr>(std::move(left)),
                           std::make_unique<Expr>(std::move(right))}};
}

Expr makeUnary(TokenType op, int pos, Expr operand) {
    return Expr{UnaryExpr{op, pos, std::make_unique<Expr>(std::move(operand))}};
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

} // namespace sc
