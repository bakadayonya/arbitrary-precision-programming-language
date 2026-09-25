// MPFR 高精度浮点的 C++ RAII 封装。
//
// 精度是「按值携带」的（mpfr_t 本身就记录精度）：每个值记得自己是多少位的，
// 二元运算取两者中更高的精度。好处是没有隐藏的全局精度状态，
// 运行期可以随时提高精度而不影响已经存在的值。
#pragma once

#include <gmpxx.h>
#include <mpfr.h>

#include <optional>
#include <string>
#include <string_view>

namespace sc {

/// 新建浮点值的默认二进制精度。
inline constexpr mpfr_prec_t DEFAULT_PRECISION = 256;

/// 二进制精度对应的十进制有效位数：floor(precision * log10(2))。
/// 这些位都是正确的；再多打印只会暴露出二进制舍入的尾部噪声。
/// 需要精确往返时用 Mpfr::to_key()（%Ra 十六进制），而不是这个十进制文本。
constexpr int decimalDigitsFor(mpfr_prec_t precision) {
    return static_cast<int>(static_cast<double>(precision) * 0.30102999566398120);
}

class Mpfr {
public:
    explicit Mpfr(mpfr_prec_t precision = DEFAULT_PRECISION);
    Mpfr(const mpz_class& z, mpfr_prec_t precision);

    /// 按十进制解析；语法错误返回 nullopt。
    /// 注意：MPFR 对「语法合法但超出指数范围」的串返回成功并置 ±inf，
    /// 是否接受由调用方的数值策略决定（见 ValueLimits::allowNonFinite）。
    [[nodiscard]] static std::optional<Mpfr> fromString(std::string_view s, mpfr_prec_t precision);

    Mpfr(const Mpfr& other);
    Mpfr(Mpfr&& other) noexcept;
    Mpfr& operator=(const Mpfr& other);
    Mpfr& operator=(Mpfr&& other) noexcept;
    ~Mpfr();

    [[nodiscard]] mpfr_prec_t precision() const { return mpfr_get_prec(v_); }
    [[nodiscard]] bool isZero() const { return mpfr_zero_p(v_) != 0; }
    [[nodiscard]] bool isFinite() const { return mpfr_number_p(v_) != 0; }

    /// 面向用户的十进制文本，最多 digits 位有效数字（%g 会去掉多余的 0）。
    [[nodiscard]] std::string to_string(int digits) const;

    /// 精确（可往返）的十六进制浮点文本，用于常量池去重。
    [[nodiscard]] std::string to_key() const;

    [[nodiscard]] Mpfr operator-() const;
    [[nodiscard]] Mpfr operator+(const Mpfr& other) const;
    [[nodiscard]] Mpfr operator-(const Mpfr& other) const;
    [[nodiscard]] Mpfr operator*(const Mpfr& other) const;
    [[nodiscard]] Mpfr operator/(const Mpfr& other) const;

    [[nodiscard]] static Mpfr pow(const Mpfr& base, const Mpfr& exponent);
    /// 取余（同 C 的 fmod：余数取被除数的符号，与"向零截断"的除法配套）。
    [[nodiscard]] static Mpfr fmod(const Mpfr& a, const Mpfr& b);

private:
    /// 二元运算的结果精度：取两者较高者。
    [[nodiscard]] mpfr_prec_t wider(const Mpfr& other) const;

    mpfr_t v_;
};

} // namespace sc
