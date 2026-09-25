// 语言值。
//
// 值是一个 variant，ValueKind 的枚举顺序 == variant 备选顺序 == 数值塔等级。
// 加一种值类型 = 加一个 variant 备选 + 一个 ValueKind + 一个 KIND_PREFIX，
// 三处数量不一致会被 static_assert 拦下；运算分派用 switch(ValueKind) 且无 default，
// 漏掉新种类会被 -Wswitch 抓住；to_string/to_literal/key 里的 visit 链
// 由 detail::alwaysFalse 兜底，漏分支直接编译失败。
#pragma once

#include "error.hpp"
#include "number.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace sc {

/// 值的种类。Int/Float 是数值塔（可互相提升），Str 不在塔里：
/// 字符串只支持 '+' 连接，其他运算一律类型错误。
enum class ValueKind { Int = 0, Float = 1, Str = 2, Count };

/// 真实种类数（不含 Count）。
inline constexpr int VALUE_KIND_COUNT = static_cast<int>(ValueKind::Count);

/// 值层策略：整数位宽预算、整数幂指数上限、是否接受 inf/nan、
/// 整数提升为浮点时的精度、单个字符串的字节预算。
/// 由 Config 提供，随调用链显式传递，值层不读任何全局状态。
struct ValueLimits {
    unsigned long long maxIntegerBits = 1ULL << 30;
    long long maxIntExponent = 10'000'000;
    bool allowNonFinite = false;
    mpfr_prec_t promotePrecision = DEFAULT_PRECISION;
    std::size_t maxStringBytes = 1U << 20;
};

class Value {
public:
    Value();
    Value(mpz_class z);
    Value(Mpfr f);
    Value(std::string s);

    [[nodiscard]] ValueKind kind() const { return static_cast<ValueKind>(data_.index()); }
    [[nodiscard]] int rank() const { return static_cast<int>(kind()); }
    [[nodiscard]] bool isInt() const { return kind() == ValueKind::Int; }
    [[nodiscard]] bool isFloat() const { return kind() == ValueKind::Float; }
    [[nodiscard]] bool isStr() const { return kind() == ValueKind::Str; }

    /// 整数载荷（仅在 isInt() 时有效）。
    [[nodiscard]] const mpz_class& asInt() const { return std::get<mpz_class>(data_); }
    /// 字符串载荷（仅在 isStr() 时有效）。
    [[nodiscard]] const std::string& asStr() const { return std::get<std::string>(data_); }

    /// 提升为浮点；整数按 precision 构造，浮点值原样返回。
    /// 前置条件：调用方已排除字符串（数值运算会先做类型检查）。
    [[nodiscard]] Mpfr toFloat(mpfr_prec_t precision) const;

    /// 程序输出：字符串是裸文本。
    /// digits <= 0 表示"由值自身的精度决定位数"（默认，保证打印的都是正确位）；
    /// digits > 0 时按指定位数输出（由 Config 的 output-digits 覆盖）。
    [[nodiscard]] std::string to_string(int digits) const;

    /// 调试/自省输出：字符串带引号与转义，供 -d / :consts / :vars 使用，
    /// 避免字符串常量与数字常量在输出里无法区分。
    [[nodiscard]] std::string to_literal(int digits) const;

    /// 常量池去重键（精确、类型前缀 + 无需转义的载荷）。
    [[nodiscard]] std::string key() const;

    /// 原始载荷的访问器，供热衷于自己 visit 的扩展使用。
    [[nodiscard]] const std::variant<mpz_class, Mpfr, std::string>& raw() const { return data_; }

private:
    std::variant<mpz_class, Mpfr, std::string> data_;

    static_assert(std::variant_size_v<decltype(data_)> ==
                      static_cast<std::size_t>(VALUE_KIND_COUNT),
                  "Value 的 variant 备选数与 ValueKind 数量不一致：加值类型时两边都要改");
};

/// 数值提升：取等级更高的种类。
/// 前置条件：两个参数都必须是数值种类（调用方已排除字符串）。
[[nodiscard]] ValueKind promote(ValueKind a, ValueKind b);

// 运算。实现放在 value.cpp，由 operators.hpp 的表引用；
// 想加新运算就在这里加一个自由函数 + 在表里加一行。
[[nodiscard]] Result<Value> valueAdd(const Value& a, const Value& b, const ValueLimits& limits);
[[nodiscard]] Result<Value> valueSub(const Value& a, const Value& b, const ValueLimits& limits);
[[nodiscard]] Result<Value> valueMul(const Value& a, const Value& b, const ValueLimits& limits);
[[nodiscard]] Result<Value> valueDiv(const Value& a, const Value& b, const ValueLimits& limits);
[[nodiscard]] Result<Value> valueRem(const Value& a, const Value& b, const ValueLimits& limits);
[[nodiscard]] Result<Value> valueShl(const Value& a, const Value& b, const ValueLimits& limits);
[[nodiscard]] Result<Value> valueShr(const Value& a, const Value& b, const ValueLimits& limits);
[[nodiscard]] Result<Value> valuePow(const Value& a, const Value& b, const ValueLimits& limits);
[[nodiscard]] Result<Value> valueNeg(const Value& a, const ValueLimits& limits);

} // namespace sc
