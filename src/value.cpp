#include "value.hpp"

#include "unicode.hpp"

#include <array>
#include <format>
#include <utility>

namespace sc {

namespace {

/// 每种 ValueKind 在常量池键里的前缀。长度必须与 VALUE_KIND_COUNT 一致。
constexpr std::array<std::string_view, static_cast<std::size_t>(VALUE_KIND_COUNT)> KIND_PREFIX{
    "i:", "f:", "s:"};

static_assert(KIND_PREFIX.size() == static_cast<std::size_t>(VALUE_KIND_COUNT),
              "新增 ValueKind 时要在 KIND_PREFIX 里补上前缀");

/// 整数预算：结果位宽超过上限就报错，而不是把内存吃光。
[[nodiscard]] Result<Value> checkInteger(mpz_class value, const ValueLimits& limits) {
    const std::size_t bits = mpz_sizeinbase(value.get_mpz_t(), 2);
    if (bits > limits.maxIntegerBits) {
        return fail(
            std::format("整数结果超出位宽上限（{} 位，上限 {} 位）", bits, limits.maxIntegerBits));
    }
    return Value(std::move(value));
}

/// 浮点策略：默认拒绝 inf/nan（README 承诺过"不产生 inf"）。
[[nodiscard]] Result<Value> checkFloat(Mpfr value, const ValueLimits& limits) {
    if (!limits.allowNonFinite && !value.isFinite())
        return fail("浮点结果超出可表示范围（inf/nan）");
    return Value(std::move(value));
}

/// 字符串预算：单个字符串的字节数上限（字面量与连接结果都受此约束）。
[[nodiscard]] Result<Value> checkString(std::string value, const ValueLimits& limits) {
    if (value.size() > limits.maxStringBytes)
        return fail(std::format("字符串超出长度上限（{} 字节，上限 {} 字节）", value.size(),
                                limits.maxStringBytes));
    return Value(std::move(value));
}

/// 字符串只参与 '+' 连接，其余二元运算一律在这里被拒绝。
constexpr const char* STRING_ONLY_ADD = "类型错误：字符串只支持 '+'（连接）";

[[nodiscard]] bool hasString(const Value& a, const Value& b) { return a.isStr() || b.isStr(); }

/// 同类型运算的公共骨架：先提升，再按提升后的种类分派。
/// switch 没有 default：新增 ValueKind 而忘了分支会被 -Wswitch 拦下。
/// 调用方必须已经排除字符串（字符串不是数值塔的一部分）。
template <class IntOp, class FloatOp>
[[nodiscard]] Result<Value> binaryOp(const Value& a, const Value& b, const ValueLimits& limits,
                                     IntOp intOp, FloatOp floatOp) {
    switch (promote(a.kind(), b.kind())) {
        case ValueKind::Int: return checkInteger(intOp(a.asInt(), b.asInt()), limits);
        case ValueKind::Float:
            return checkFloat(
                floatOp(a.toFloat(limits.promotePrecision), b.toFloat(limits.promotePrecision)),
                limits);
        case ValueKind::Str:
        case ValueKind::Count: break;
    }
    return fail("内部错误：未知的值种类");
}

/// 供 to_literal 使用：把字符串渲染成带引号、可安全回读的形式。
[[nodiscard]] std::string quote(const std::string& text) {
    std::string out = "\"";
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char byte = static_cast<unsigned char>(text[i]);
        if (byte == '"') {
            out += "\\\"";
            ++i;
        } else if (byte == '\\') {
            out += "\\\\";
            ++i;
        } else if (byte == '\n') {
            out += "\\n";
            ++i;
        } else if (byte == '\t') {
            out += "\\t";
            ++i;
        } else if (byte == '\r') {
            out += "\\r";
            ++i;
        } else if (byte < 0x20 || byte == 0x7F) {
            out += std::format("\\u{{{:X}}}", static_cast<unsigned int>(byte));
            ++i;
        } else {
            // 多字节序列整体拷贝；合法性已由词法层保证，非法时按 1 字节兜底。
            const auto decoded = unicode::decodeAt(text, i);
            const std::size_t length = decoded.ok() ? static_cast<std::size_t>(decoded.length) : 1;
            out.append(text, i, length);
            i += length;
        }
    }
    out += '"';
    return out;
}

} // namespace

Value::Value() : data_(mpz_class(0)) {}
Value::Value(mpz_class z) : data_(std::move(z)) {}
Value::Value(Mpfr f) : data_(std::move(f)) {}
Value::Value(std::string s) : data_(std::move(s)) {}

ValueKind promote(ValueKind a, ValueKind b) { return a > b ? a : b; }

Mpfr Value::toFloat(mpfr_prec_t precision) const {
    return std::visit(
        [precision](const auto& payload) -> Mpfr {
            using T = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<T, mpz_class>) {
                return Mpfr(payload, precision);
            } else if constexpr (std::is_same_v<T, Mpfr>) {
                (void)precision; // 已有浮点值保留自己的精度
                return payload;
            } else {
                // 字符串不会被提升为浮点：所有数值运算入口都先做类型检查。
                return Mpfr(precision);
            }
        },
        data_);
}

std::string Value::to_string(int digits) const {
    return std::visit(
        [digits](const auto& payload) -> std::string {
            using T = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<T, mpz_class>) {
                return payload.get_str();
            } else if constexpr (std::is_same_v<T, Mpfr>) {
                // digits <= 0：按值自身的精度决定位数。这是默认语义——
                // 精度可以运行期调整，若按"当前配置的精度"输出，
                // 提高精度后打印旧值就会暴露二进制舍入噪声。
                const int effective = digits > 0 ? digits : decimalDigitsFor(payload.precision());
                return payload.to_string(effective);
            } else if constexpr (std::is_same_v<T, std::string>) {
                return payload; // 程序输出：裸文本
            } else {
                static_assert(detail::alwaysFalse<T>, "Value::to_string 没有处理这个 variant 备选");
                return "<未知值>";
            }
        },
        data_);
}

std::string Value::to_literal(int digits) const {
    return std::visit(
        [digits](const auto& payload) -> std::string {
            using T = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<T, mpz_class>) {
                return payload.get_str();
            } else if constexpr (std::is_same_v<T, Mpfr>) {
                const int effective = digits > 0 ? digits : decimalDigitsFor(payload.precision());
                return payload.to_string(effective);
            } else if constexpr (std::is_same_v<T, std::string>) {
                return quote(payload);
            } else {
                static_assert(detail::alwaysFalse<T>,
                              "Value::to_literal 没有处理这个 variant 备选");
                return "<未知值>";
            }
        },
        data_);
}

std::string Value::key() const {
    const std::string_view prefix = KIND_PREFIX[static_cast<std::size_t>(rank())];
    return std::visit(
        [prefix](const auto& payload) -> std::string {
            using T = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<T, mpz_class>) {
                return std::string(prefix) + payload.get_str();
            } else if constexpr (std::is_same_v<T, Mpfr>) {
                return std::string(prefix) + payload.to_key();
            } else if constexpr (std::is_same_v<T, std::string>) {
                return std::string(prefix) + payload;
            } else {
                static_assert(detail::alwaysFalse<T>, "Value::key 没有处理这个 variant 备选");
                return std::string(prefix) + "?";
            }
        },
        data_);
}

Result<Value> valueAdd(const Value& a, const Value& b, const ValueLimits& limits) {
    // 字符串只支持连接；不与数值混算。
    if (a.isStr() || b.isStr()) {
        if (!a.isStr() || !b.isStr()) return fail("类型错误：字符串只能与字符串相加");
        std::string joined = a.asStr();
        if (joined.size() + b.asStr().size() > limits.maxStringBytes)
            return fail(std::format("字符串超出长度上限（上限 {} 字节）", limits.maxStringBytes));
        joined += b.asStr();
        return checkString(std::move(joined), limits);
    }
    return binaryOp(
        a, b, limits, [](const mpz_class& x, const mpz_class& y) { return x + y; },
        [](const Mpfr& x, const Mpfr& y) { return x + y; });
}

Result<Value> valueSub(const Value& a, const Value& b, const ValueLimits& limits) {
    if (hasString(a, b)) return fail(STRING_ONLY_ADD);
    return binaryOp(
        a, b, limits, [](const mpz_class& x, const mpz_class& y) { return x - y; },
        [](const Mpfr& x, const Mpfr& y) { return x - y; });
}

Result<Value> valueMul(const Value& a, const Value& b, const ValueLimits& limits) {
    if (hasString(a, b)) return fail(STRING_ONLY_ADD);
    return binaryOp(
        a, b, limits, [](const mpz_class& x, const mpz_class& y) { return x * y; },
        [](const Mpfr& x, const Mpfr& y) { return x * y; });
}

Result<Value> valueDiv(const Value& a, const Value& b, const ValueLimits& limits) {
    if (hasString(a, b)) return fail(STRING_ONLY_ADD);
    switch (promote(a.kind(), b.kind())) {
        case ValueKind::Int: {
            const mpz_class& divisor = b.asInt();
            if (divisor == 0) return fail("除零错误");
            // GMP 的 mpz_class::operator/ 向零截断，与语言文档一致。
            return checkInteger(a.asInt() / divisor, limits);
        }
        case ValueKind::Float: {
            const Mpfr divisor = b.toFloat(limits.promotePrecision);
            if (divisor.isZero()) return fail("除零错误");
            return checkFloat(a.toFloat(limits.promotePrecision) / divisor, limits);
        }
        case ValueKind::Str:
        case ValueKind::Count: break;
    }
    return fail("内部错误：未知的值种类");
}

Result<Value> valueRem(const Value& a, const Value& b, const ValueLimits& limits) {
    if (hasString(a, b)) return fail(STRING_ONLY_ADD);
    switch (promote(a.kind(), b.kind())) {
        case ValueKind::Int: {
            const mpz_class& divisor = b.asInt();
            if (divisor == 0) return fail("对零取余");
            // GMP 的 % 与向零截断的除法配套：余数取被除数的符号。(-7) % 2 == -1
            return checkInteger(a.asInt() % divisor, limits);
        }
        case ValueKind::Float: {
            const Mpfr divisor = b.toFloat(limits.promotePrecision);
            if (divisor.isZero()) return fail("对零取余");
            return checkFloat(Mpfr::fmod(a.toFloat(limits.promotePrecision), divisor), limits);
        }
        case ValueKind::Str:
        case ValueKind::Count: break;
    }
    return fail("内部错误：未知的值种类");
}

/// 移位只接受整数：位运算对小数没有意义，显式报类型错误而不是隐式提升。
Result<Value> valueShl(const Value& a, const Value& b, const ValueLimits& limits) {
    if (hasString(a, b)) return fail(STRING_ONLY_ADD);
    if (!a.isInt() || !b.isInt()) return fail("类型错误：移位只支持整数");

    const mpz_class& value = a.asInt();
    const mpz_class& amount = b.asInt();
    if (amount < 0) return fail("负数移位位数");

    // 与整数幂共用同一套位宽预算：先估结果位宽，再调用 GMP。
    // 否则 1 << 10^18 会直接撞上 GMP 的 2^31 limb 硬上限（信号级崩溃而不是 Error）。
    const unsigned long long bitLength = mpz_sizeinbase(value.get_mpz_t(), 2);
    const unsigned long long room =
        limits.maxIntegerBits > bitLength ? limits.maxIntegerBits - bitLength : 0;
    if (!amount.fits_ulong_p() || amount.get_ui() > room)
        return fail(std::format("左移结果超出位宽上限（上限 {} 位）", limits.maxIntegerBits));

    mpz_class result;
    mpz_mul_2exp(result.get_mpz_t(), value.get_mpz_t(), amount.get_ui());
    return checkInteger(std::move(result), limits);
}

Result<Value> valueShr(const Value& a, const Value& b, const ValueLimits& limits) {
    if (hasString(a, b)) return fail(STRING_ONLY_ADD);
    if (!a.isInt() || !b.isInt()) return fail("类型错误：移位只支持整数");

    const mpz_class& value = a.asInt();
    const mpz_class& amount = b.asInt();
    if (amount < 0) return fail("负数移位位数");

    // 定义成"向零截断的除以 2^n"，即 x >> n == x / 2**n（与本语言的整数除法一致）。
    // 移出全部有效位就是 0，位数大到装不进 ulong 也一样。
    if (!amount.fits_ulong_p()) return Value(mpz_class(0));

    mpz_class result;
    mpz_tdiv_q_2exp(result.get_mpz_t(), value.get_mpz_t(), amount.get_ui());
    return checkInteger(std::move(result), limits);
}

Result<Value> valuePow(const Value& a, const Value& b, const ValueLimits& limits) {
    if (hasString(a, b)) return fail(STRING_ONLY_ADD);

    if (a.isInt() && b.isInt()) {
        const mpz_class& base = a.asInt();
        const mpz_class& exponent = b.asInt();
        if (exponent < 0) return fail("负整数指数：请改用浮点底数，例如 2.0 ** -1");
        if (!exponent.fits_slong_p() || exponent.get_si() > limits.maxIntExponent)
            return fail(std::format("指数过大（上限 {}）", limits.maxIntExponent));
        if (exponent == 0) return Value(mpz_class(1));

        // 关键防护：先估算结果位宽，再决定是否调用 GMP。
        // 只限指数是不够的——底数很大时（例如先算出 2**10000000），
        // 一个"合法"的指数就能把结果推到 GMP 无法表示的范围（limb 计数是 32 位 int），
        // 那会是信号级崩溃而不是 Error。
        const unsigned long long baseBits = mpz_sizeinbase(base.get_mpz_t(), 2);
        const unsigned long long expValue = static_cast<unsigned long long>(exponent.get_si());
        if (expValue != 0 && baseBits > limits.maxIntegerBits / expValue)
            return fail(std::format("整数幂结果超出位宽上限（底数 {} 位 × 指数 {}，上限 {} 位）",
                                    baseBits, expValue, limits.maxIntegerBits));

        mpz_class result;
        mpz_pow_ui(result.get_mpz_t(), base.get_mpz_t(), exponent.get_ui());
        return checkInteger(std::move(result), limits);
    }

    const Mpfr base = a.toFloat(limits.promotePrecision);
    const Mpfr exponent = b.toFloat(limits.promotePrecision);
    return checkFloat(Mpfr::pow(base, exponent), limits);
}

Result<Value> valueNeg(const Value& a, const ValueLimits& limits) {
    switch (a.kind()) {
        case ValueKind::Int: return checkInteger(-a.asInt(), limits);
        case ValueKind::Float: return checkFloat(-a.toFloat(limits.promotePrecision), limits);
        case ValueKind::Str: return fail("类型错误：字符串不支持一元负号");
        case ValueKind::Count: break;
    }
    return fail("内部错误：未知的值种类");
}

} // namespace sc
