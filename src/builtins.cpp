// 内建数学函数的实现：vendor/neo-math.h 与 sc 值层之间的适配。
//
// 这是**唯一**包含 vendor/neo-math.h 的编译单元（Makefile 用 -isystem vendor 引入，
// 第三方头的警告不会污染 make strict）。表驱动，加函数只需在 BUILTINS 里加一行。
//
// 三条边界规则（都在这里收口，别处不重复实现）：
//   1. 精度：实参是小数就保留它自己的精度，是整数就按 ValueLimits::promotePrecision
//      提升；二元函数取两者较高者。常量按 promotePrecision（= Config::precision）求值，
//      所以 --set precision=... 对内建函数同样生效。
//   2. 舍入：显式传 MPFR_RNDN，与 sc 的既有运算保持一致，不读 neo-math 的线程局部舍入。
//   3. 结果：小数走 inf/nan 策略，整数走位宽预算；需要"按 n 迭代"的函数
//      还要先按 max-int-exponent 卡住规模，绝不把超大参数交给 GMP。
#include "builtins.hpp"

#include <neo-math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <string>
#include <utility>

namespace sc {

namespace {

// ============================================================================
// 与 neo-math.h 的桥
//
// sc::Mpfr 与 gmp_mpfr::mpfr_class 都是 mpfr_t 的 RAII 封装，精度都按值携带，
// 所以双向转换只是"按同样的精度拷贝位模式"——无损，不做任何十进制往返。
// ============================================================================

[[nodiscard]] mpfr_class toNeo(const Mpfr& x) {
    mpfr_class r{with_prec(x.precision())};
    mpfr_set(r.get_mpfr_t(), x.get(), MPFR_RNDN);
    return r;
}

[[nodiscard]] Mpfr toSc(const mpfr_class& x) {
    Mpfr r(x.precision());
    mpfr_set(r.get(), x.get_mpfr_t(), MPFR_RNDN);
    return r;
}

/// 小数结果的统一收口。先按 nan / inf 给出比"超出范围"更具体的诊断，
/// 再交给值层策略（allow-non-finite 时原样放行）。
[[nodiscard]] Result<Value> finishFloat(Mpfr value, const ValueLimits& limits) {
    if (!limits.allowNonFinite) {
        if (value.isNaN()) return fail("数学函数定义域错误（结果是 nan）");
        if (value.isInf()) return fail("数学函数结果溢出（结果是 inf）");
    }
    return valueFromFloat(std::move(value), limits);
}

// ---------------------------------------------------------------- 实参取值

/// 实参 -> 浮点。整数按 promotePrecision 提升；小数保留自身精度。
[[nodiscard]] Result<mpfr_class> argAsFloat(const Value& v, const ValueLimits& limits) {
    if (v.isInt()) return toNeo(Mpfr(v.asInt(), limits.promotePrecision));
    if (v.isFloat()) return toNeo(v.toFloat(limits.promotePrecision));
    return fail(std::format("类型错误：需要数值参数（这里是{}）", v.typeName()));
}

/// 实参 -> 整数。不接受小数：静默截断会让 `gcd(2.5, 1)` 变成一个看不出错的答案。
[[nodiscard]] Result<const mpz_class*> argAsInt(const Value& v) {
    if (!v.isInt()) return fail(std::format("类型错误：需要整数参数（这里是{}）", v.typeName()));
    return &v.asInt();
}

// ---------------------------------------------------------------- 求值适配器
//
// mmath::* 的几种签名各配一个模板，表里直接写 &unaryFloat<mmath::sqrt> 这种形式。

/// 一元浮点：mpfr_class f(const mpfr_class&, mpfr_prec_t, mpfr_rnd_t)
template <mpfr_class (*Fn)(const mpfr_class&, mpfr_prec_t, mpfr_rnd_t)>
Result<Value> unaryFloat(std::span<const Value> args, const ValueLimits& limits) {
    auto x = argAsFloat(args[0], limits);
    if (!x) return std::unexpected(x.error());
    return finishFloat(toSc(Fn(*x, x->precision(), MPFR_RNDN)), limits);
}

/// 取整类：结果必为整数，MPFR 不提供舍入模式参数。
/// 对整数实参是恒等——返回原值而不是提升成小数，免得 floor(2**1000) 这类运算白白丢精度。
template <mpfr_class (*Fn)(const mpfr_class&, mpfr_prec_t)>
Result<Value> integerValued(std::span<const Value> args, const ValueLimits& limits) {
    if (args[0].isInt()) return valueFromInt(args[0].asInt(), limits);
    auto x = argAsFloat(args[0], limits);
    if (!x) return std::unexpected(x.error());
    return finishFloat(toSc(Fn(*x, x->precision())), limits);
}

/// 二元浮点：结果精度取两者较高者（与 sc 自己的二元运算规则一致）。
template <mpfr_class (*Fn)(const mpfr_class&, const mpfr_class&, mpfr_prec_t, mpfr_rnd_t)>
Result<Value> binaryFloat(std::span<const Value> args, const ValueLimits& limits) {
    auto a = argAsFloat(args[0], limits);
    if (!a) return std::unexpected(a.error());
    auto b = argAsFloat(args[1], limits);
    if (!b) return std::unexpected(b.error());
    const mpfr_prec_t prec = std::max(a->precision(), b->precision());
    return finishFloat(toSc(Fn(*a, *b, prec, MPFR_RNDN)), limits);
}

/// 零元常量：精度来自 Config::precision，因此 --set precision=512 会立刻反映到 pi()。
template <const mpfr_class& (*Fn)(mpfr_prec_t, mpfr_rnd_t)>
Result<Value> constantValue(std::span<const Value>, const ValueLimits& limits) {
    return finishFloat(toSc(Fn(limits.promotePrecision, MPFR_RNDN)), limits);
}

// ---------------------------------------------------------------- 特例

/// math::log 有一元（自然对数）与二元（换底）两个重载，取地址会歧义；
/// 用这个薄壳把二元那个固定下来。语言层刻意只暴露 log(x, base)，
/// 自然对数写 ln(x)、常用对数写 log10(x)——避免 C 的 log 与中学的 log 之争。
[[nodiscard]] mpfr_class logBase(const mpfr_class& x, const mpfr_class& base, mpfr_prec_t p,
                                 mpfr_rnd_t rnd) {
    return mmath::log(x, base, p, rnd);
}

/// abs 保持值种类：整数走 GMP（精确），小数走 MPFR。
Result<Value> builtinAbs(std::span<const Value> args, const ValueLimits& limits) {
    if (args[0].isInt()) {
        mpz_class r;
        mpz_abs(r.get_mpz_t(), args[0].asInt().get_mpz_t());
        return valueFromInt(std::move(r), limits);
    }
    return unaryFloat<mmath::abs>(args, limits);
}

/// min/max 也保持值种类：两个整数之间取最值必须是精确的。
Result<Value> builtinMinMax(std::span<const Value> args, const ValueLimits& limits, bool wantMax) {
    const Value& a = args[0];
    const Value& b = args[1];
    if (a.isInt() && b.isInt()) {
        const bool takeLeft = wantMax ? a.asInt() >= b.asInt() : a.asInt() <= b.asInt();
        return valueFromInt(takeLeft ? a.asInt() : b.asInt(), limits);
    }
    auto x = argAsFloat(a, limits);
    if (!x) return std::unexpected(x.error());
    auto y = argAsFloat(b, limits);
    if (!y) return std::unexpected(y.error());
    return finishFloat(toSc(wantMax ? mmath::max(*x, *y) : mmath::min(*x, *y)), limits);
}

Result<Value> builtinMin(std::span<const Value> args, const ValueLimits& limits) {
    return builtinMinMax(args, limits, false);
}

Result<Value> builtinMax(std::span<const Value> args, const ValueLimits& limits) {
    return builtinMinMax(args, limits, true);
}

// ---------------------------------------------------------------- 整数内建
//
// 预算哲学与整数幂一致：**先估算，再调用 GMP**。估算宁可高估——
// 高估只是拒绝一个本来能算的参数，漏估则是把内存和 CPU 交出去。
// 规模上限复用 max-int-exponent：它本来就是"CPU 预算"这个旋钮。

constexpr const char* STEP_BUDGET_MSG = "参数超过 CPU 预算（见 max-int-exponent）";

/// 参数是否在"按 n 迭代/累乘"的规模预算内（要求非负且 <= max-int-exponent）。
[[nodiscard]] bool stepBudgetOk(const mpz_class& n, const ValueLimits& limits) {
    if (n < 0) return false;
    if (mpz_fits_slong_p(n.get_mpz_t()) == 0) return false;
    return static_cast<long long>(mpz_get_si(n.get_mpz_t())) <= limits.maxIntExponent;
}

/// 已通过 stepBudgetOk 的参数（非负、能装进 unsigned long）。
[[nodiscard]] unsigned long asULong(const mpz_class& n) {
    return static_cast<unsigned long>(mpz_get_si(n.get_mpz_t()));
}

[[nodiscard]] Result<Value> budgetError(double estimatedBits, const ValueLimits& limits) {
    return fail(std::format("结果预计约 {} 位，超过位宽上限（上限 {} 位）",
                            static_cast<unsigned long long>(estimatedBits), limits.maxIntegerBits));
}

// 各自的位宽上界：log2(n!) <= n*log2(n)、θ(n) < 2n、log2(φ) < 1、C(n,k) <= 2^n。
[[nodiscard]] double factorialBits(const mpz_class& n) {
    const double v = mpz_get_d(n.get_mpz_t());
    return v < 2.0 ? 1.0 : v * std::log2(v);
}
[[nodiscard]] double primorialBits(const mpz_class& n) {
    const double v = mpz_get_d(n.get_mpz_t());
    return v < 2.0 ? 1.0 : 2.0 * v + 4.0;
}
[[nodiscard]] double fibonacciBits(const mpz_class& n) { return mpz_get_d(n.get_mpz_t()) + 4.0; }
[[nodiscard]] double binomialBits(const mpz_class& n) { return mpz_get_d(n.get_mpz_t()) + 1.0; }

Result<Value> builtinGcd(std::span<const Value> args, const ValueLimits& limits) {
    auto a = argAsInt(args[0]);
    if (!a) return std::unexpected(a.error());
    auto b = argAsInt(args[1]);
    if (!b) return std::unexpected(b.error());
    return valueFromInt(gext::gcd(**a, **b), limits);
}

Result<Value> builtinLcm(std::span<const Value> args, const ValueLimits& limits) {
    auto a = argAsInt(args[0]);
    if (!a) return std::unexpected(a.error());
    auto b = argAsInt(args[1]);
    if (!b) return std::unexpected(b.error());
    return valueFromInt(gext::lcm(**a, **b), limits);
}

Result<Value> builtinFactorial(std::span<const Value> args, const ValueLimits& limits) {
    auto n = argAsInt(args[0]);
    if (!n) return std::unexpected(n.error());
    if (!stepBudgetOk(**n, limits)) return fail(STEP_BUDGET_MSG);
    if (const double est = factorialBits(**n); est > static_cast<double>(limits.maxIntegerBits))
        return budgetError(est, limits);
    return valueFromInt(gext::factorial(asULong(**n)), limits);
}

Result<Value> builtinBinomial(std::span<const Value> args, const ValueLimits& limits) {
    auto n = argAsInt(args[0]);
    if (!n) return std::unexpected(n.error());
    auto k = argAsInt(args[1]);
    if (!k) return std::unexpected(k.error());
    if (!stepBudgetOk(**n, limits) || !stepBudgetOk(**k, limits)) return fail(STEP_BUDGET_MSG);
    if (const double est = binomialBits(**n); est > static_cast<double>(limits.maxIntegerBits))
        return budgetError(est, limits);
    return valueFromInt(gext::binomial(asULong(**n), asULong(**k)), limits);
}

Result<Value> builtinFibonacci(std::span<const Value> args, const ValueLimits& limits) {
    auto n = argAsInt(args[0]);
    if (!n) return std::unexpected(n.error());
    if (!stepBudgetOk(**n, limits)) return fail(STEP_BUDGET_MSG);
    if (const double est = fibonacciBits(**n); est > static_cast<double>(limits.maxIntegerBits))
        return budgetError(est, limits);
    return valueFromInt(gext::fibonacci(asULong(**n)), limits);
}

Result<Value> builtinLucnum(std::span<const Value> args, const ValueLimits& limits) {
    auto n = argAsInt(args[0]);
    if (!n) return std::unexpected(n.error());
    if (!stepBudgetOk(**n, limits)) return fail(STEP_BUDGET_MSG);
    if (const double est = fibonacciBits(**n); est > static_cast<double>(limits.maxIntegerBits))
        return budgetError(est, limits);
    return valueFromInt(gext::lucnum(asULong(**n)), limits);
}

Result<Value> builtinPrimorial(std::span<const Value> args, const ValueLimits& limits) {
    auto n = argAsInt(args[0]);
    if (!n) return std::unexpected(n.error());
    if (!stepBudgetOk(**n, limits)) return fail(STEP_BUDGET_MSG);
    if (const double est = primorialBits(**n); est > static_cast<double>(limits.maxIntegerBits))
        return budgetError(est, limits);
    return valueFromInt(gext::primorial(asULong(**n)), limits);
}

Result<Value> builtinIsqrt(std::span<const Value> args, const ValueLimits& limits) {
    auto n = argAsInt(args[0]);
    if (!n) return std::unexpected(n.error());
    if (**n < 0) return fail("参数必须非负");
    return valueFromInt(gext::sqrt(**n), limits);
}

Result<Value> builtinIsPrime(std::span<const Value> args, const ValueLimits&) {
    auto n = argAsInt(args[0]);
    if (!n) return std::unexpected(n.error());
    return Value(gext::is_prime(**n));
}

Result<Value> builtinNextPrime(std::span<const Value> args, const ValueLimits& limits) {
    auto n = argAsInt(args[0]);
    if (!n) return std::unexpected(n.error());
    if (**n < 0) return fail("参数必须非负");
    // next_prime 的结果与原值同量级，不需要预估；超预算由 valueFromInt 兜住。
    return valueFromInt(gext::next_prime(**n), limits);
}

Result<Value> builtinPowm(std::span<const Value> args, const ValueLimits& limits) {
    auto b = argAsInt(args[0]);
    if (!b) return std::unexpected(b.error());
    auto e = argAsInt(args[1]);
    if (!e) return std::unexpected(e.error());
    auto m = argAsInt(args[2]);
    if (!m) return std::unexpected(m.error());
    if (**e < 0) return fail("指数必须非负（负数指数需要模逆，本函数不提供）");
    if (**m <= 0) return fail("模必须为正整数");
    // 结果必小于模，所以位宽由模决定。
    return valueFromInt(gext::powm(**b, **e, **m), limits);
}

// ============================================================================
// 表
// ============================================================================

/// 内建函数的求值签名：实参（按书写顺序）+ 值层策略。
using BuiltinEval = Result<Value> (*)(std::span<const Value> args, const ValueLimits& limits);

/// 表项。用构造函数而不是聚合初始化，是为了让"忘了写求值函数"直接编译失败：
/// 聚合初始化允许少写一个成员（求值函数会静默变成 nullptr），构造函数不允许。
/// 这比事后再 static_assert 检查 eval 更可靠——顺带绕开了下面那个 GCC 陷阱。
struct BuiltinInfo {
    std::string_view name;
    int arity;
    BuiltinEval eval;

    constexpr BuiltinInfo(std::string_view n, int a, BuiltinEval e) : name(n), arity(a), eval(e) {}
};

// 名字 + 参数个数唯一；同一个名字可以有不同的参数个数。
constexpr auto BUILTINS = std::to_array<BuiltinInfo>({
    // ----- 常量（0 元）-----
    {"pi", 0, &constantValue<mmath::pi>},
    {"e", 0, &constantValue<mmath::e>},
    {"euler_gamma", 0, &constantValue<mmath::euler_gamma>},
    {"ln2", 0, &constantValue<mmath::ln2>},
    {"catalan", 0, &constantValue<mmath::catalan>},
    {"sqrt2", 0, &constantValue<mmath::sqrt2>},
    {"sqrt3", 0, &constantValue<mmath::sqrt3>},
    {"phi", 0, &constantValue<mmath::phi>},
    {"zeta3", 0, &constantValue<mmath::zeta3>},

    // ----- 幂 / 根 -----
    {"sqrt", 1, &unaryFloat<mmath::sqrt>},
    {"cbrt", 1, &unaryFloat<mmath::cbrt>},
    {"hypot", 2, &binaryFloat<mmath::hypot>},
    {"atan2", 2, &binaryFloat<mmath::atan2>},

    // ----- 指数 / 对数 -----
    {"exp", 1, &unaryFloat<mmath::exp>},
    {"exp2", 1, &unaryFloat<mmath::exp2>},
    {"exp10", 1, &unaryFloat<mmath::exp10>},
    {"expm1", 1, &unaryFloat<mmath::expm1>},
    {"ln", 1, &unaryFloat<mmath::ln>},
    {"log2", 1, &unaryFloat<mmath::log2>},
    {"log10", 1, &unaryFloat<mmath::log10>},
    {"log1p", 1, &unaryFloat<mmath::log1p>},
    {"log", 2, &binaryFloat<&logBase>},

    // ----- 三角 -----
    {"sin", 1, &unaryFloat<mmath::sin>},
    {"cos", 1, &unaryFloat<mmath::cos>},
    {"tan", 1, &unaryFloat<mmath::tan>},
    {"asin", 1, &unaryFloat<mmath::asin>},
    {"acos", 1, &unaryFloat<mmath::acos>},
    {"atan", 1, &unaryFloat<mmath::atan>},

    // ----- 双曲 -----
    {"sinh", 1, &unaryFloat<mmath::sinh>},
    {"cosh", 1, &unaryFloat<mmath::cosh>},
    {"tanh", 1, &unaryFloat<mmath::tanh>},
    {"asinh", 1, &unaryFloat<mmath::asinh>},
    {"acosh", 1, &unaryFloat<mmath::acosh>},
    {"atanh", 1, &unaryFloat<mmath::atanh>},

    // ----- 特殊函数 -----
    {"gamma", 1, &unaryFloat<mmath::gamma>},
    {"lgamma", 1, &unaryFloat<mmath::lgamma>},
    {"digamma", 1, &unaryFloat<mmath::digamma>},
    {"zeta", 1, &unaryFloat<mmath::zeta>},
    {"erf", 1, &unaryFloat<mmath::erf>},
    {"erfc", 1, &unaryFloat<mmath::erfc>},
    {"li2", 1, &unaryFloat<mmath::li2>},
    {"eint", 1, &unaryFloat<mmath::eint>},
    {"beta", 2, &binaryFloat<mmath::beta>},
    {"agm", 2, &binaryFloat<mmath::agm>},

    // ----- 贝塞尔 -----
    {"j0", 1, &unaryFloat<mmath::j0>},
    {"j1", 1, &unaryFloat<mmath::j1>},
    {"y0", 1, &unaryFloat<mmath::y0>},
    {"y1", 1, &unaryFloat<mmath::y1>},

    // ----- 取整 / 分解 -----
    {"floor", 1, &integerValued<mmath::floor>},
    {"ceil", 1, &integerValued<mmath::ceil>},
    {"trunc", 1, &integerValued<mmath::trunc>},
    {"round", 1, &integerValued<mmath::round>},
    {"frac", 1, &unaryFloat<mmath::frac>},
    {"abs", 1, &builtinAbs},
    {"fmod", 2, &binaryFloat<mmath::fmod>},
    {"remainder", 2, &binaryFloat<mmath::remainder>},
    {"copysign", 2, &binaryFloat<mmath::copysign>},
    {"dim", 2, &binaryFloat<mmath::dim>},
    {"min", 2, &builtinMin},
    {"max", 2, &builtinMax},

    // ----- 整数（GMP）-----
    {"gcd", 2, &builtinGcd},
    {"lcm", 2, &builtinLcm},
    {"factorial", 1, &builtinFactorial},
    {"binomial", 2, &builtinBinomial},
    {"fibonacci", 1, &builtinFibonacci},
    {"lucnum", 1, &builtinLucnum},
    {"primorial", 1, &builtinPrimorial},
    {"isqrt", 1, &builtinIsqrt},
    {"is_prime", 1, &builtinIsPrime},
    {"next_prime", 1, &builtinNextPrime},
    {"powm", 3, &builtinPowm},
});

/// (名字, 参数个数) 必须唯一：否则解析期查表命中哪个是不确定的。
constexpr bool builtinKeysUnique() {
    for (std::size_t i = 0; i < BUILTINS.size(); ++i) {
        for (std::size_t j = i + 1; j < BUILTINS.size(); ++j) {
            if (BUILTINS[i].name == BUILTINS[j].name && BUILTINS[i].arity == BUILTINS[j].arity)
                return false;
        }
    }
    return true;
}

/// 表项本身必须自洽：有名字、参数个数非负。求值函数非空由构造函数保证（见 BuiltinInfo）。
/// 注意：这里刻意**不**比较 eval != nullptr。在 -fsanitize=undefined 下（见 Makefile 的
/// ubsan 目标），GCC 不把"函数指针 == nullptr"当作常量表达式，写进去 static_assert
/// 会直接编译失败——operators.hpp 里记录过同一个陷阱。
constexpr bool builtinEntriesSane() {
    for (const BuiltinInfo& info : BUILTINS) {
        if (info.name.empty() || info.arity < 0) return false;
    }
    return true;
}

static_assert(builtinKeysUnique(), "内建函数表里有重复的 (名字, 参数个数)");
static_assert(builtinEntriesSane(), "内建函数表里有空名字 / 负数参数个数");

const BuiltinInfo* builtinAt(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= BUILTINS.size()) return nullptr;
    return &BUILTINS[static_cast<std::size_t>(index)];
}

} // namespace

// ---------------------------------------------------------------- 对外接口

int findBuiltin(std::string_view name, int arity) {
    for (std::size_t i = 0; i < BUILTINS.size(); ++i) {
        if (BUILTINS[i].name == name && BUILTINS[i].arity == arity) return static_cast<int>(i);
    }
    return -1;
}

std::string builtinLookupError(std::string_view name, int arity) {
    std::string accepted;
    for (const BuiltinInfo& info : BUILTINS) {
        if (info.name != name) continue;
        if (!accepted.empty()) accepted += " 或 ";
        accepted += std::to_string(info.arity);
    }
    if (accepted.empty()) return std::format("未知的内建函数：{}", name);
    return std::format("内建函数 {} 需要 {} 个参数，但给了 {} 个", name, accepted, arity);
}

int builtinArity(int index) noexcept {
    const BuiltinInfo* info = builtinAt(index);
    return info == nullptr ? -1 : info->arity;
}

std::string_view builtinName(int index) noexcept {
    const BuiltinInfo* info = builtinAt(index);
    return info == nullptr ? std::string_view{} : info->name;
}

Result<Value> runBuiltin(int index, std::span<const Value> args, const ValueLimits& limits) {
    const BuiltinInfo* info = builtinAt(index);
    if (info == nullptr) return fail(std::format("非法内建函数下标 {}", index));
    if (static_cast<int>(args.size()) != info->arity)
        return fail(std::format("内部错误：{} 期望 {} 个参数，收到 {} 个", info->name, info->arity,
                                args.size()));
    // 数学库用异常报错，sc 用 Result：在这里收口，异常绝不穿过 VM 主循环。
    try {
        return info->eval(args, limits);
    } catch (const std::exception& e) {
        return fail(std::format("数学函数内部失败：{}", e.what()));
    } catch (...) {
        return fail("数学函数内部失败：未知异常");
    }
}

} // namespace sc
