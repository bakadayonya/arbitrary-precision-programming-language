// neo-math.h
#ifndef GMP_MPFR_MATH_H
#define GMP_MPFR_MATH_H

/*
 * 高精度数学库 C++ 封装（GMP + MPFR）
 *
 * 依赖：
 *   - GMP  (libgmp-dev)
 *   - MPFR (libmpfr-dev)
 *   - GCC 16 / libstdc++-16 (C++23 起；std::expected/std::span 等)
 *
 * 编译：
 *   g++-16 -std=c++26 -O2 -o math-test math-test.cpp -lgmpxx -lgmp -lmpfr
 *
 * 舍入模式：
 *   默认 MPFR_RNDN，可用 gmp_mpfr::set_rounding() 设置当前线程的模式，
 *   用 gmp_mpfr::scoped_rounding 做作用域 RAII 切换；所有运算、转换、
 *   常量求值与 math::* 默认跟随该模式。需要单次覆盖时，在函数最后一个
 *   参数显式传入 mpfr_rnd_t，例如 math::sqrt(x, 0, MPFR_RNDD)。
 *   可读别名：RND_NEAREST / RND_ZERO / RND_UP / RND_DOWN / RND_AWAY / RND_FAITHFUL。
 *
 *   注：ceil/floor/trunc/round 的结果必为整数，MPFR 不提供舍入模式参数；
 *   整数转换 to_long/to_ulong 默认向零取整，可显式指定或改用 *_checked()。
 */

#include <gmpxx.h>
#include <mpfr.h>

#include <algorithm>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <limits>
#include <mutex>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

// 可选：std::format / std::print 支持（需要 C++23 <format>）。
// 定义 NEO_MATH_NO_FORMAT 可关闭，避免不必要的编译开销。
#if !defined(NEO_MATH_NO_FORMAT) && defined(__has_include)
#  if __has_include(<format>)
#    include <format>
#    define NEO_MATH_HAS_FORMAT 1
#  endif
#endif

namespace gmp_mpfr {

// ============================================================================
// 常量
// ============================================================================

constexpr mpfr_prec_t DEFAULT_PREC = 256;
constexpr mpfr_rnd_t  DEFAULT_RND  = MPFR_RNDN;

// ============================================================================
// 舍入模式选择
//
// 每个线程维护一个“当前舍入模式”（初始值 rounding() = MPFR_RNDN）。所有运算、
// 转换、比较用的转换以及常量求值默认跟随它；需要单次覆盖时，可以在函数最后一
// 个参数显式传入 mpfr_rnd_t，或用 scoped_rounding 在作用域内临时切换：
//
//   set_rounding(MPFR_RNDU);                 // 本线程之后全部向上舍入
//   {
//       scoped_rounding g(MPFR_RNDD);        // 仅此作用域向下舍入
//       mpfr_class y = a / b;
//   }
//   mpfr_class z = math::sqrt(a, 0, MPFR_RNDZ);   // 只影响这一次调用
//
// 注意：伪随机数发生器（ancillary-prng.h）刻意固定使用 rounding()，
// 以保证输出流不受当前舍入模式影响、可复现。
// ============================================================================

inline mpfr_rnd_t& rounding_slot() noexcept {
    static thread_local mpfr_rnd_t r = DEFAULT_RND;
    return r;
}
// 当前线程的舍入模式
inline mpfr_rnd_t rounding() noexcept { return rounding_slot(); }
// 设置当前线程的舍入模式
inline void set_rounding(mpfr_rnd_t r) noexcept { rounding_slot() = r; }

// RAII 守卫：构造时切换，析构时恢复（异常路径同样恢复）
class scoped_rounding {
public:
    explicit scoped_rounding(mpfr_rnd_t r) noexcept : m_prev(rounding()) { set_rounding(r); }
    ~scoped_rounding() { set_rounding(m_prev); }
    scoped_rounding(const scoped_rounding&)            = delete;
    scoped_rounding& operator=(const scoped_rounding&) = delete;
private:
    mpfr_rnd_t m_prev;
};

// 可读别名（避免直接拼 MPFR_RND*）
inline constexpr mpfr_rnd_t RND_NEAREST  = MPFR_RNDN;   // 最近，平局取偶
inline constexpr mpfr_rnd_t RND_ZERO     = MPFR_RNDZ;   // 向零
inline constexpr mpfr_rnd_t RND_UP       = MPFR_RNDU;   // 向 +∞
inline constexpr mpfr_rnd_t RND_DOWN     = MPFR_RNDD;   // 向 -∞
inline constexpr mpfr_rnd_t RND_AWAY     = MPFR_RNDA;   // 远离零
inline constexpr mpfr_rnd_t RND_FAITHFUL = MPFR_RNDF;   // 忠实舍入

// ============================================================================
// 默认精度选择
//
// 每个线程维护一个"默认精度"（初始值 DEFAULT_PREC = 256）。凡是省略精度、
// 或显式传 0/负数的地方（mpfr_class 构造、math::*、常量、with_prec(0) 等）
// 都使用它：
//
//   set_default_precision(1024);
//   mpfr_class x;                       // 1024 位
//   {
//       scoped_precision g(128);        // 仅此作用域生效
//       mpfr_class y = math::sqrt(x);
//   }
//
// 注意：它只影响"新建对象/新计算"的精度，不改变既有对象的精度。
// ============================================================================
inline mpfr_prec_t& default_precision_slot() noexcept {
    static thread_local mpfr_prec_t p = DEFAULT_PREC;
    return p;
}
// 当前线程的默认精度
inline mpfr_prec_t default_precision() noexcept { return default_precision_slot(); }
// 设置当前线程的默认精度（非正值被忽略）
inline void set_default_precision(mpfr_prec_t p) noexcept {
    if (p > 0) default_precision_slot() = p;
}

// RAII 守卫：构造时切换默认精度，析构时恢复
class scoped_precision {
public:
    explicit scoped_precision(mpfr_prec_t p) noexcept
        : m_prev(default_precision()) { set_default_precision(p); }
    ~scoped_precision() { set_default_precision(m_prev); }
    scoped_precision(const scoped_precision&)            = delete;
    scoped_precision& operator=(const scoped_precision&) = delete;
private:
    mpfr_prec_t m_prev;
};

// ============================================================================
// 默认输出格式
//
// 每个线程维护一套默认输出格式（位数/进制/科学计数），供 mpfr_class::format()
// 与 operator<< 使用；to_string() 仍按显式参数工作、不受影响。
//
//   set_default_format(30, 10, false);      // 30 位有效数字、十进制、定点
//   mpfr_class x = math::pi();
//   std::cout << x << "\n";                 // 跟随上面的设置
//   {
//       scoped_format f(format_options{0, 16, false});   // 16 进制
//       std::cout << x << "\n";
//   }
// ============================================================================
struct format_options {
    std::size_t digits = 0;    // 0 = 按对象精度自动推导
    int         base   = 10;   // 2 ~ 62
    bool        sci    = false;
};

inline format_options& default_format_slot() noexcept {
    static thread_local format_options o{};
    return o;
}
inline const format_options& default_format() noexcept { return default_format_slot(); }
inline void set_default_format(const format_options& o) noexcept { default_format_slot() = o; }
inline void set_default_format(std::size_t digits, int base = 10, bool sci = false) noexcept {
    default_format_slot() = format_options{digits, base, sci};
}
inline void reset_default_format() noexcept { default_format_slot() = format_options{}; }

class scoped_format {
public:
    explicit scoped_format(const format_options& o) noexcept
        : m_prev(default_format()) { set_default_format(o); }
    ~scoped_format() { set_default_format(m_prev); }
    scoped_format(const scoped_format&)            = delete;
    scoped_format& operator=(const scoped_format&) = delete;
private:
    format_options m_prev;
};

struct with_prec_t { mpfr_prec_t prec; };
inline constexpr with_prec_t with_prec(mpfr_prec_t p) noexcept { return {p}; }

enum class mpfr_error {
    invalid_string,
    division_by_zero,
    domain_error,
    range_error,
    invalid_argument,
    unknown
};

namespace detail {
[[noreturn]] inline void throw_err(mpfr_error e, std::string_view msg) {
    switch (e) {
        case mpfr_error::invalid_string:   throw std::invalid_argument(std::string(msg));
        case mpfr_error::division_by_zero: throw std::domain_error  (std::string(msg));
        case mpfr_error::domain_error:     throw std::domain_error  (std::string(msg));
        case mpfr_error::range_error:      throw std::out_of_range  (std::string(msg));
        case mpfr_error::invalid_argument: throw std::invalid_argument(std::string(msg));
        default:                           throw std::runtime_error (std::string(msg));
    }
}

// 概念：可参与 MPFR 标量运算的内置类型
template <class T>
concept mpfr_scalar =
    (std::integral<std::remove_cvref_t<T>> ||
     std::floating_point<std::remove_cvref_t<T>>) &&
    !std::is_same_v<std::remove_cvref_t<T>, bool>;

// 精度归一化：每条构造路径都必须保证精度 >= 1，
// 否则 mpfr_init2 会触发 MPFR 断言（标 noexcept 也拦不住）。
// 传 0（或负数）表示"使用当前线程的默认精度"。
inline mpfr_prec_t norm_prec(mpfr_prec_t p) noexcept {
    return p > 0 ? p : default_precision();
}

}  // namespace detail

// ============================================================================
// mpfr_class
// ============================================================================

class mpfr_class {
public:
    // ---------------- 构造 ----------------

    mpfr_class() noexcept {
        mpfr_init2(m_val, default_precision());
        mpfr_set_zero(m_val, 0);
    }

    explicit mpfr_class(with_prec_t w) noexcept {
        mpfr_init2(m_val, w.prec > 0 ? w.prec : default_precision());
        mpfr_set_zero(m_val, 0);
    }

    template <detail::mpfr_scalar S>
    mpfr_class(S op, mpfr_prec_t prec = default_precision()) noexcept {
        mpfr_prec_t p = prec;
        if constexpr (std::floating_point<S>) {
            if constexpr (std::is_same_v<S, long double>) p = std::max<mpfr_prec_t>(p, 64);
            else                                          p = std::max<mpfr_prec_t>(p, 53);
        } else {
            p = detail::norm_prec(p);
        }
        mpfr_init2(m_val, p);
        if constexpr (std::is_same_v<S, long double>)        mpfr_set_ld(m_val, op, rounding());
        else if constexpr (std::floating_point<S>)           mpfr_set_d (m_val, static_cast<double>(op), rounding());
        else if constexpr (std::signed_integral<S>)          mpfr_set_si(m_val, static_cast<long>(op), rounding());
        else                                                 mpfr_set_ui(m_val, static_cast<unsigned long>(op), rounding());
    }

    mpfr_class(const char* str, mpfr_prec_t prec = default_precision(), int base = 10) {
        mpfr_init2(m_val, detail::norm_prec(prec));
        if (mpfr_set_str(m_val, str, base, rounding()) != 0) {
            mpfr_clear(m_val);
            detail::throw_err(mpfr_error::invalid_string,
                              std::string("mpfr_class: invalid string '") + str + "'");
        }
    }

    mpfr_class(std::string_view str, mpfr_prec_t prec = default_precision(), int base = 10) {
        std::string s(str);
        mpfr_init2(m_val, detail::norm_prec(prec));
        if (mpfr_set_str(m_val, s.c_str(), base, rounding()) != 0) {
            mpfr_clear(m_val);
            detail::throw_err(mpfr_error::invalid_string,
                              std::string("mpfr_class: invalid string '") + s + "'");
        }
    }

    mpfr_class(const mpz_class& op, mpfr_prec_t prec = default_precision()) noexcept {
        mpfr_init2(m_val, detail::norm_prec(prec));
        mpfr_set_z(m_val, op.get_mpz_t(), rounding());
    }

    mpfr_class(const mpq_class& op, mpfr_prec_t prec = default_precision()) noexcept {
        mpfr_init2(m_val, detail::norm_prec(prec));
        mpfr_set_q(m_val, op.get_mpq_t(), rounding());
    }

    // 拷贝 / 移动
    mpfr_class(const mpfr_class& o) {
        mpfr_init2(m_val, mpfr_get_prec(o.m_val));
        mpfr_set(m_val, o.m_val, rounding());
    }

    mpfr_class(mpfr_class&& o) noexcept {
        mpfr_init2(m_val, DEFAULT_PREC);
        mpfr_swap(m_val, o.m_val);
    }

    ~mpfr_class() { mpfr_clear(m_val); }

    // ---------------- 赋值 ----------------

    mpfr_class& operator=(const mpfr_class& o) {
        if (this != &o) {
            mpfr_set_prec(m_val, mpfr_get_prec(o.m_val));
            mpfr_set(m_val, o.m_val, rounding());
        }
        return *this;
    }

    mpfr_class& operator=(mpfr_class&& o) noexcept {
        if (this != &o) mpfr_swap(m_val, o.m_val);
        return *this;
    }

    template <detail::mpfr_scalar S>
    mpfr_class& operator=(S op) noexcept {
        if constexpr (std::is_same_v<S, long double>) {
            if (mpfr_get_prec(m_val) < 64) mpfr_set_prec(m_val, 64);
            mpfr_set_ld(m_val, op, rounding());
        } else if constexpr (std::floating_point<S>) {
            if (mpfr_get_prec(m_val) < 53) mpfr_set_prec(m_val, 53);
            mpfr_set_d(m_val, static_cast<double>(op), rounding());
        } else if constexpr (std::signed_integral<S>) {
            mpfr_set_si(m_val, static_cast<long>(op), rounding());
        } else {
            mpfr_set_ui(m_val, static_cast<unsigned long>(op), rounding());
        }
        return *this;
    }

    mpfr_class& operator=(std::string_view s) {
        mpfr_class tmp(s, mpfr_get_prec(m_val));
        mpfr_swap(m_val, tmp.m_val);
        return *this;
    }
    mpfr_class& operator=(const char* s)              { return operator=(std::string_view(s)); }
    mpfr_class& operator=(const std::string& s)       { return operator=(std::string_view(s)); }

    mpfr_class& operator=(const mpz_class& op) noexcept { mpfr_set_z(m_val, op.get_mpz_t(), rounding()); return *this; }
    mpfr_class& operator=(const mpq_class& op) noexcept { mpfr_set_q(m_val, op.get_mpq_t(), rounding()); return *this; }

    // ---------------- 访问器 ----------------

    mpfr_ptr    get_mpfr_t()       noexcept { return m_val; }
    mpfr_srcptr get_mpfr_t() const noexcept { return m_val; }

    mpfr_prec_t precision() const noexcept { return mpfr_get_prec(m_val); }
    void        set_precision(mpfr_prec_t p) noexcept { if (p > 0) mpfr_set_prec(m_val, p); }

    // ---------------- 类型转换 ----------------

    double        to_double(mpfr_rnd_t rnd = rounding())      const noexcept { return mpfr_get_d (m_val, rnd); }
    long double   to_long_double(mpfr_rnd_t rnd = rounding()) const noexcept { return mpfr_get_ld(m_val, rnd); }
    // 整数转换默认向零取整（与 C++ 内建转换 / std::trunc 一致），可显式指定舍入模式。
    // 注意：结果超出目标类型可表示范围时（例如负值转无符号、或绝对值过大），
    // MPFR 规定结果未指定，不要对不可信输入使用下面两个函数；
    // 需要可检查行为请用 to_long_checked() / to_ulong_checked()。
    long          to_long(mpfr_rnd_t rnd = MPFR_RNDZ)  const noexcept { return mpfr_get_si(m_val, rnd); }
    unsigned long to_ulong(mpfr_rnd_t rnd = MPFR_RNDZ) const noexcept { return mpfr_get_ui(m_val, rnd); }

    // NaN / ±Inf、溢出、负值转无符号一律返回 std::unexpected。
    std::expected<long, mpfr_error> to_long_checked(mpfr_rnd_t rnd = MPFR_RNDZ) const noexcept {
        if (!mpfr_number_p(m_val)) return std::unexpected(mpfr_error::domain_error);
        mpz_class z;
        mpfr_get_z(z.get_mpz_t(), m_val, rnd);
        if (!mpz_fits_slong_p(z.get_mpz_t())) return std::unexpected(mpfr_error::range_error);
        return static_cast<long>(mpz_get_si(z.get_mpz_t()));
    }

    std::expected<unsigned long, mpfr_error> to_ulong_checked(mpfr_rnd_t rnd = MPFR_RNDZ) const noexcept {
        if (!mpfr_number_p(m_val)) return std::unexpected(mpfr_error::domain_error);
        if (mpfr_sgn(m_val) < 0) return std::unexpected(mpfr_error::range_error);
        mpz_class z;
        mpfr_get_z(z.get_mpz_t(), m_val, rnd);
        if (!mpz_fits_ulong_p(z.get_mpz_t())) return std::unexpected(mpfr_error::range_error);
        return static_cast<unsigned long>(mpz_get_ui(z.get_mpz_t()));
    }

    mpz_class to_mpz(mpfr_rnd_t rnd = MPFR_RNDZ) const {
        mpz_class z;
        mpfr_get_z(z.get_mpz_t(), m_val, rnd);
        return z;
    }

    // MPFR 的每个有限值本身都是二进制有理数（分母为 2 的幂），mpfr_get_q 的
    // 转换总是精确的。因此这里只在分母足够"小"（默认分母 < 2^64，即
    // mpz_sizeinbase(den, 2) <= 64）时返回该精确有理数，
    // 否则认为该值无法用实用的有理数表示（例如 pi）。
    std::expected<mpq_class, mpfr_error>
    to_mpq(std::size_t max_denom_bits = 64) const noexcept {
        if (!mpfr_number_p(m_val)) return std::unexpected(mpfr_error::domain_error);
        mpq_class q;
        mpfr_get_q(q.get_mpq_t(), m_val);   // 精确（dyadic）
        mpq_canonicalize(q.get_mpq_t());
        if (mpz_sizeinbase(mpq_denref(q.get_mpq_t()), 2) > max_denom_bits)
            return std::unexpected(mpfr_error::domain_error);
        return q;
    }

    std::string to_string(std::size_t digits = 0, int base = 10,
                          bool sci = false, mpfr_rnd_t rnd = rounding()) const {
        // 自动位数：由 MPFR 按进制计算"足以无损读回"的位数
        // （旧实现固定按十进制 0.30103 估算，十六进制等会输出过多位）
        if (digits == 0)
            digits = mpfr_get_str_ndigits(base, precision());

        mpfr_exp_t exp = 0;

        // 0 / ±Inf / NaN 不能走下面的定点/科学计数拼接：
        // mpfr_get_str 对它们返回全 0 串或 "@Inf@"、"@NaN@"，且 exp 恒为 0，
        // 拼接后会得到 "0.000…"、"0.@Inf@" 这类错误结果。
        if (mpfr_zero_p(m_val)) return mpfr_signbit(m_val) ? "-0" : "0";
        if (!mpfr_number_p(m_val)) {
            char* raw = mpfr_get_str(nullptr, &exp, base, 0, m_val, rnd);
            if (!raw) detail::throw_err(mpfr_error::unknown, "mpfr_get_str failed");
            std::string out(raw);
            mpfr_free_str(raw);
            return out;
        }

        char* raw = mpfr_get_str(nullptr, &exp, base, digits, m_val, rnd);
        if (!raw) detail::throw_err(mpfr_error::unknown, "mpfr_get_str failed");

        std::string body(raw);
        mpfr_free_str(raw);

        bool neg = !body.empty() && body[0] == '-';
        std::string_view mag = neg ? std::string_view(body).substr(1)
                                   : std::string_view(body);
        std::string out;
        if (sci) {
            out.reserve(mag.size() + 16);
            if (neg) out.push_back('-');
            out.push_back(mag.empty() ? '0' : mag[0]);
            if (mag.size() > 1) { out.push_back('.'); out.append(mag.substr(1)); }
            out += 'e';
            out += std::to_string(exp - 1);
        } else {
            std::string intpart, frac;
            if (exp <= 0) {
                intpart = "0";
                frac.assign(static_cast<std::size_t>(-exp), '0');
                frac.append(mag);
            } else if (static_cast<std::size_t>(exp) >= mag.size()) {
                intpart = mag;
                intpart.append(static_cast<std::size_t>(exp) - mag.size(), '0');
            } else {
                intpart = mag.substr(0, static_cast<std::size_t>(exp));
                frac    = mag.substr(static_cast<std::size_t>(exp));
            }
            out.reserve(intpart.size() + frac.size() + 3);
            if (neg) out.push_back('-');
            out += intpart;
            if (!frac.empty()) { out.push_back('.'); out += frac; }
        }
        return out;
    }

    // ---------------- 默认格式 / 精确十六进制 ----------------

    // 用当前线程的默认输出格式（set_default_format / scoped_format）
    std::string format() const { return format(default_format()); }
    std::string format(const format_options& o) const {
        return to_string(o.digits, o.base, o.sci);
    }

    // 无损十六进制往返：格式为 "<十六进制数字>p<二进制指数>"，表示 0.<数字> × 2^指数。
    // 与 to_string 不同，它不做十进制舍入，配合 from_string_hex() 可精确复原。
    // 例：0.5 -> "8p-4"；-3 -> "-cp2"；0 -> "0p0"；±Inf/NaN -> "@Inf@"/"@NaN@"。
    std::string to_string_hex() const {
        if (mpfr_nan_p(m_val))  return "@NaN@";
        if (mpfr_inf_p(m_val))  return mpfr_signbit(m_val) ? "-@Inf@" : "@Inf@";
        if (mpfr_zero_p(m_val)) return mpfr_signbit(m_val) ? "-0p0" : "0p0";

        mpfr_exp_t e = 0;
        char* raw = mpfr_get_str(nullptr, &e, 16, 0, m_val, MPFR_RNDN);
        if (!raw) detail::throw_err(mpfr_error::unknown, "mpfr_get_str failed");
        std::string out(raw);
        mpfr_free_str(raw);

        // 去掉尾随的 '0'（不改变 0.<mant>×2^exp 的值），表示更紧凑。
        std::size_t last = out.size();
        while (last > 1 && out[last - 1] == '0') --last;
        if (last > 1 && out[last - 1] == '-') --last;   // 形如 "-00…" 的极端情形
        out.resize(last);
        if (out.empty() || out == "-") out = "0";

        out.push_back('p');
        out += std::to_string(static_cast<long long>(e) * 4);  // 16^e = 2^(4e)
        return out;
    }

    // prec <= 0 时按数字位数自动选择足够精度；显式给出 prec 时必须 >= 有效位数，
    // 否则会像其他构造方式一样舍入。
    static mpfr_class from_string_hex(std::string_view s, mpfr_prec_t prec = 0) {
        if (s == "@NaN@")  return mpfr_class("nan",  detail::norm_prec(prec));
        if (s == "@Inf@")  return mpfr_class("inf",  detail::norm_prec(prec));
        if (s == "-@Inf@") return mpfr_class("-inf", detail::norm_prec(prec));
        if (s == "0p0" || s == "-0p0") {
            mpfr_class z(0L, detail::norm_prec(prec));
            if (s[0] == '-') mpfr_set_zero(z.get_mpfr_t(), -1);
            return z;
        }

        const auto ppos = s.find('p');
        if (ppos == std::string_view::npos)
            detail::throw_err(mpfr_error::invalid_string, "mpfr_class: bad hex float");

        std::string_view mant = s.substr(0, ppos);
        std::string_view exps = s.substr(ppos + 1);
        const bool neg = !mant.empty() && mant[0] == '-';
        std::string_view digs = neg ? mant.substr(1) : mant;
        if (digs.empty() || exps.empty())
            detail::throw_err(mpfr_error::invalid_string, "mpfr_class: bad hex float");
        for (char c : digs) {
            const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                             (c >= 'A' && c <= 'F');
            if (!hex) detail::throw_err(mpfr_error::invalid_string, "mpfr_class: bad hex digit");
        }

        long long exp2 = 0;
        const auto [ptr, ec] = std::from_chars(exps.data(), exps.data() + exps.size(), exp2);
        if (ec != std::errc{} || ptr != exps.data() + exps.size())
            detail::throw_err(mpfr_error::invalid_string, "mpfr_class: bad hex exponent");
        if (exp2 < static_cast<long long>(std::numeric_limits<long>::min()) ||
            exp2 > static_cast<long long>(std::numeric_limits<long>::max()))
            detail::throw_err(mpfr_error::range_error, "mpfr_class: hex exponent out of range");

        const mpfr_prec_t needed = static_cast<mpfr_prec_t>(4 * digs.size());
        mpfr_class r(0L, prec > 0 ? prec : std::max<mpfr_prec_t>(needed, 2));

        std::string frac;
        frac.reserve(digs.size() + 3);
        if (neg) frac.push_back('-');
        frac += "0.";
        frac.append(digs);
        if (mpfr_set_str(r.get_mpfr_t(), frac.c_str(), 16, MPFR_RNDN) != 0)
            detail::throw_err(mpfr_error::invalid_string, "mpfr_class: bad hex float");
        if (exp2 != 0)
            mpfr_mul_2si(r.get_mpfr_t(), r.get_mpfr_t(), static_cast<long>(exp2), MPFR_RNDN);
        return r;
    }

    // ---------------- 谓词 ----------------

    bool is_nan()     const noexcept { return mpfr_nan_p(m_val)     != 0; }
    bool is_inf()     const noexcept { return mpfr_inf_p(m_val)     != 0; }
    bool is_zero()    const noexcept { return mpfr_zero_p(m_val)    != 0; }
    bool is_finite()  const noexcept { return mpfr_number_p(m_val)  != 0; }
    bool is_integer() const noexcept { return mpfr_integer_p(m_val) != 0; }
    int  sign()       const noexcept { return mpfr_sgn(m_val); }

    // ---------------- 一元 ----------------

    mpfr_class operator-() const {
        mpfr_class r{with_prec(precision())};
        mpfr_neg(r.m_val, m_val, rounding());
        return r;
    }
    const mpfr_class& operator+() const noexcept { return *this; }

    // ---------------- swap ----------------

    void swap(mpfr_class& o) noexcept { mpfr_swap(m_val, o.m_val); }
    friend void swap(mpfr_class& a, mpfr_class& b) noexcept { a.swap(b); }

    // ---------------- 复合赋值 ----------------
    // mpfr_class
    mpfr_class& operator+=(const mpfr_class& o) noexcept { mpfr_add(m_val, m_val, o.m_val, rounding()); return *this; }
    mpfr_class& operator-=(const mpfr_class& o) noexcept { mpfr_sub(m_val, m_val, o.m_val, rounding()); return *this; }
    mpfr_class& operator*=(const mpfr_class& o) noexcept { mpfr_mul(m_val, m_val, o.m_val, rounding()); return *this; }
    mpfr_class& operator/=(const mpfr_class& o) noexcept { mpfr_div(m_val, m_val, o.m_val, rounding()); return *this; }
    // mpz_class
    mpfr_class& operator+=(const mpz_class& o) noexcept { mpfr_add_z(m_val, m_val, o.get_mpz_t(), rounding()); return *this; }
    mpfr_class& operator-=(const mpz_class& o) noexcept { mpfr_sub_z(m_val, m_val, o.get_mpz_t(), rounding()); return *this; }
    mpfr_class& operator*=(const mpz_class& o) noexcept { mpfr_mul_z(m_val, m_val, o.get_mpz_t(), rounding()); return *this; }
    mpfr_class& operator/=(const mpz_class& o) noexcept { mpfr_div_z(m_val, m_val, o.get_mpz_t(), rounding()); return *this; }

    // 内置标量：模板，避免 int 歧义
#define GMP_MPFR_DEFINE_COMPOUND(OP, FN)                                              \
    template <detail::mpfr_scalar S>                                                  \
    mpfr_class& operator OP(S o) noexcept {                                           \
        if constexpr (std::is_same_v<std::remove_cvref_t<S>, long double>) {          \
            mpfr_class tmp{with_prec(precision())};                                   \
            mpfr_set_ld(tmp.get_mpfr_t(), static_cast<long double>(o), rounding());  \
            FN(m_val, m_val, tmp.get_mpfr_t(), rounding());                          \
        } else if constexpr (std::floating_point<S>) {                                \
            FN##_d(m_val, m_val, static_cast<double>(o), rounding());                \
        } else if constexpr (std::signed_integral<S>) {                               \
            FN##_si(m_val, m_val, static_cast<long>(o), rounding());                 \
        } else {                                                                      \
            FN##_ui(m_val, m_val, static_cast<unsigned long>(o), rounding());        \
        }                                                                             \
        return *this;                                                                 \
    }

    GMP_MPFR_DEFINE_COMPOUND(+=, mpfr_add)
    GMP_MPFR_DEFINE_COMPOUND(-=, mpfr_sub)
    GMP_MPFR_DEFINE_COMPOUND(*=, mpfr_mul)
    GMP_MPFR_DEFINE_COMPOUND(/=, mpfr_div)

#undef GMP_MPFR_DEFINE_COMPOUND

    // ---------------- 比较 ----------------

    friend bool operator==(const mpfr_class& a, const mpfr_class& b) noexcept { return mpfr_equal_p       (a.m_val, b.m_val) != 0; }
    friend bool operator!=(const mpfr_class& a, const mpfr_class& b) noexcept { return !(a == b); }
    friend bool operator< (const mpfr_class& a, const mpfr_class& b) noexcept { return mpfr_less_p        (a.m_val, b.m_val) != 0; }
    friend bool operator> (const mpfr_class& a, const mpfr_class& b) noexcept { return mpfr_greater_p     (a.m_val, b.m_val) != 0; }
    friend bool operator<=(const mpfr_class& a, const mpfr_class& b) noexcept { return mpfr_lessequal_p   (a.m_val, b.m_val) != 0; }
    friend bool operator>=(const mpfr_class& a, const mpfr_class& b) noexcept { return mpfr_greaterequal_p(a.m_val, b.m_val) != 0; }

    // ---------------- 流 ----------------

    friend std::ostream& operator<<(std::ostream& os, const mpfr_class& x) {
        // 默认格式可由 set_default_format 配置；流的 precision() 若被显式设置
        // （>0，默认即 6）则覆盖位数，与旧行为保持一致。
        format_options o = default_format();
        const auto d = os.precision();
        if (d > 0) o.digits = static_cast<std::size_t>(d);
        os << x.format(o);
        return os;
    }

    friend std::istream& operator>>(std::istream& is, mpfr_class& x) {
        std::string s; is >> s; x = s; return is;
    }

private:
    mpfr_t m_val;
};

// ============================================================================
// 二元运算符
// ============================================================================

// mpfr OP mpfr
#define GMP_MPFR_BINOP_MPFR_MPFR(OP, FN)                                        \
    inline mpfr_class operator OP(const mpfr_class& a, const mpfr_class& b) {   \
        mpfr_class r{with_prec(std::max(a.precision(), b.precision()))};        \
        FN(r.get_mpfr_t(), a.get_mpfr_t(), b.get_mpfr_t(), rounding());        \
        return r;                                                               \
    }

// mpfr OP 标量
#define GMP_MPFR_BINOP_MPFR_SCALAR(OP, FN)                                      \
    template <detail::mpfr_scalar S>                                            \
    inline mpfr_class operator OP(const mpfr_class& a, S b) {                   \
        mpfr_class r{with_prec(a.precision())};                                 \
        if constexpr (std::is_same_v<std::remove_cvref_t<S>, long double>) {    \
            mpfr_class t{with_prec(a.precision())};                             \
            mpfr_set_ld(t.get_mpfr_t(), static_cast<long double>(b), rounding()); \
            FN(r.get_mpfr_t(), a.get_mpfr_t(), t.get_mpfr_t(), rounding());    \
        } else if constexpr (std::floating_point<S>) {                          \
            FN##_d(r.get_mpfr_t(), a.get_mpfr_t(), static_cast<double>(b), rounding()); \
        } else if constexpr (std::signed_integral<S>) {                         \
            FN##_si(r.get_mpfr_t(), a.get_mpfr_t(), static_cast<long>(b), rounding());  \
        } else {                                                                \
            FN##_ui(r.get_mpfr_t(), a.get_mpfr_t(), static_cast<unsigned long>(b), rounding()); \
        }                                                                       \
        return r;                                                               \
    }

// 标量 OP mpfr（非交换）
#define GMP_MPFR_BINOP_SCALAR_MPFR(OP, FN_REV, FN_REV_SI, FN_REV_UI, FN_REV_D)  \
    template <detail::mpfr_scalar S>                                            \
    inline mpfr_class operator OP(S a, const mpfr_class& b) {                   \
        mpfr_class r{with_prec(b.precision())};                                 \
        if constexpr (std::is_same_v<std::remove_cvref_t<S>, long double>) {    \
            mpfr_class t{with_prec(std::max<mpfr_prec_t>(b.precision(), 64))};  \
            mpfr_set_ld(t.get_mpfr_t(), static_cast<long double>(a), rounding()); \
            FN_REV(r.get_mpfr_t(), t.get_mpfr_t(), b.get_mpfr_t(), rounding()); \
        } else if constexpr (std::floating_point<S>) {                          \
            FN_REV_D(r.get_mpfr_t(), static_cast<double>(a), b.get_mpfr_t(), rounding()); \
        } else if constexpr (std::signed_integral<S>) {                         \
            FN_REV_SI(r.get_mpfr_t(), static_cast<long>(a), b.get_mpfr_t(), rounding());  \
        } else {                                                                \
            FN_REV_UI(r.get_mpfr_t(), static_cast<unsigned long>(a), b.get_mpfr_t(), rounding()); \
        }                                                                       \
        return r;                                                               \
    }

// 加 / 乘：交换律
#define GMP_MPFR_BINOP_COMMUTATIVE(OP, FN)                                      \
    GMP_MPFR_BINOP_MPFR_MPFR(OP, FN)                                            \
    GMP_MPFR_BINOP_MPFR_SCALAR(OP, FN)                                          \
    template <detail::mpfr_scalar S>                                            \
    inline mpfr_class operator OP(S a, const mpfr_class& b) { return b OP a; }

GMP_MPFR_BINOP_COMMUTATIVE(+, mpfr_add)
GMP_MPFR_BINOP_COMMUTATIVE(*, mpfr_mul)

// 减 / 除
GMP_MPFR_BINOP_MPFR_MPFR(-, mpfr_sub)
GMP_MPFR_BINOP_MPFR_SCALAR(-, mpfr_sub)
GMP_MPFR_BINOP_SCALAR_MPFR(-, mpfr_sub, mpfr_si_sub, mpfr_ui_sub, mpfr_d_sub)

GMP_MPFR_BINOP_MPFR_MPFR(/, mpfr_div)
GMP_MPFR_BINOP_MPFR_SCALAR(/, mpfr_div)
GMP_MPFR_BINOP_SCALAR_MPFR(/, mpfr_div, mpfr_si_div, mpfr_ui_div, mpfr_d_div)

// mpfr OP mpz
inline mpfr_class operator+(const mpfr_class& a, const mpz_class& b) {
    mpfr_class r{with_prec(a.precision())};
    mpfr_add_z(r.get_mpfr_t(), a.get_mpfr_t(), b.get_mpz_t(), rounding());
    return r;
}
inline mpfr_class operator-(const mpfr_class& a, const mpz_class& b) {
    mpfr_class r{with_prec(a.precision())};
    mpfr_sub_z(r.get_mpfr_t(), a.get_mpfr_t(), b.get_mpz_t(), rounding());
    return r;
}
inline mpfr_class operator*(const mpfr_class& a, const mpz_class& b) {
    mpfr_class r{with_prec(a.precision())};
    mpfr_mul_z(r.get_mpfr_t(), a.get_mpfr_t(), b.get_mpz_t(), rounding());
    return r;
}
inline mpfr_class operator/(const mpfr_class& a, const mpz_class& b) {
    mpfr_class r{with_prec(a.precision())};
    mpfr_div_z(r.get_mpfr_t(), a.get_mpfr_t(), b.get_mpz_t(), rounding());
    return r;
}

// mpz OP mpfr
inline mpfr_class operator+(const mpz_class& a, const mpfr_class& b) { return b + a; }
inline mpfr_class operator*(const mpz_class& a, const mpfr_class& b) { return b * a; }
inline mpfr_class operator-(const mpz_class& a, const mpfr_class& b) {
    mpfr_class r{with_prec(b.precision())};
    mpfr_z_sub(r.get_mpfr_t(), a.get_mpz_t(), b.get_mpfr_t(), rounding());
    return r;
}
inline mpfr_class operator/(const mpz_class& a, const mpfr_class& b) {
    mpfr_class t(a, b.precision());
    mpfr_class r{with_prec(b.precision())};
    mpfr_div(r.get_mpfr_t(), t.get_mpfr_t(), b.get_mpfr_t(), rounding());
    return r;
}

#undef GMP_MPFR_BINOP_MPFR_MPFR
#undef GMP_MPFR_BINOP_MPFR_SCALAR
#undef GMP_MPFR_BINOP_SCALAR_MPFR
#undef GMP_MPFR_BINOP_COMMUTATIVE

// ============================================================================
// 字面量后缀
// ============================================================================

inline mpfr_class operator""_mpfr(const char* s, std::size_t) { return mpfr_class(s); }
inline mpfr_class operator""_mpfr(long double v)               { return mpfr_class(v); }
inline mpfr_class operator""_mpfr(unsigned long long v)        { return mpfr_class(static_cast<unsigned long>(v)); }

// ============================================================================
// math 命名空间
// ============================================================================

namespace math {

namespace detail {
inline mpfr_prec_t pick(mpfr_prec_t a, mpfr_prec_t b, mpfr_prec_t explicit_p) noexcept {
    return explicit_p > 0 ? explicit_p : std::max(a, b);
}

template <class Fn>
const mpfr_class& cached_constant(mpfr_prec_t requested, mpfr_rnd_t rnd, Fn&& fn) {
    static std::mutex mtx;
    static std::unordered_map<std::uint64_t, mpfr_class> cache;

    mpfr_prec_t prec_key = ((requested + 63) / 64) * 64;
    if (prec_key < 64) prec_key = 64;

    // 必须按 (精度槽, 舍入模式) 分键：同一精度下不同舍入模式得到的最低位不同，
    // 否则先请求 RNDN 的 pi 会被 RNDU 的调用错误复用。
    std::uint64_t key = (static_cast<std::uint64_t>(prec_key) << 3)
                      | static_cast<std::uint64_t>(rnd & 7);

    std::lock_guard lk(mtx);
    auto [it, inserted] = cache.try_emplace(key);
    if (inserted) {
        it->second = mpfr_class{with_prec(prec_key)};
        fn(it->second.get_mpfr_t());
    }
    return it->second;
}
}  // namespace detail

// ----- 幂 / 根 -----
inline mpfr_class pow(const mpfr_class& b, const mpfr_class& e, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) {
    mpfr_class r{with_prec(detail::pick(b.precision(), e.precision(), p))};
    mpfr_pow(r.get_mpfr_t(), b.get_mpfr_t(), e.get_mpfr_t(), rnd);
    return r;
}
// 整型指数统一走模板：早先同时提供 (long) 与 (unsigned long) 两个重载，
// 使得 pow(x, 2) / pow(x, n)（n 为 int/unsigned）产生二义性而无法编译。
template <class S>
    requires (std::integral<std::remove_cvref_t<S>> &&
              !std::is_same_v<std::remove_cvref_t<S>, bool>)
inline mpfr_class pow(const mpfr_class& b, S e, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) {
    mpfr_class r{with_prec(detail::pick(b.precision(), 0, p))};
    if constexpr (std::signed_integral<std::remove_cvref_t<S>>)
        mpfr_pow_si(r.get_mpfr_t(), b.get_mpfr_t(), static_cast<long>(e), rnd);
    else
        mpfr_pow_ui(r.get_mpfr_t(), b.get_mpfr_t(), static_cast<unsigned long>(e), rnd);
    return r;
}
inline mpfr_class sqrt(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) {
    mpfr_class r{with_prec(detail::pick(x.precision(), 0, p))};
    mpfr_sqrt(r.get_mpfr_t(), x.get_mpfr_t(), rnd);
    return r;
}
inline mpfr_class rec_sqrt(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) {
    mpfr_class r{with_prec(detail::pick(x.precision(), 0, p))};
    mpfr_rec_sqrt(r.get_mpfr_t(), x.get_mpfr_t(), rnd);
    return r;
}
inline mpfr_class cbrt(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) {
    mpfr_class r{with_prec(detail::pick(x.precision(), 0, p))};
    mpfr_cbrt(r.get_mpfr_t(), x.get_mpfr_t(), rnd);
    return r;
}
inline mpfr_class root(const mpfr_class& x, unsigned long n, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) {
    mpfr_class r{with_prec(detail::pick(x.precision(), 0, p))};
    mpfr_rootn_ui(r.get_mpfr_t(), x.get_mpfr_t(), n, rnd);
    return r;
}
inline mpfr_class hypot(const mpfr_class& x, const mpfr_class& y, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) {
    mpfr_class r{with_prec(detail::pick(x.precision(), y.precision(), p))};
    mpfr_hypot(r.get_mpfr_t(), x.get_mpfr_t(), y.get_mpfr_t(), rnd);
    return r;
}

// ----- 指数 / 对数 -----
inline mpfr_class exp  (const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_exp  (r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class exp2 (const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_exp2 (r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class exp10(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_exp10(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
// exp(x)-1 / ln(1+x)，在 x 接近 0 时比 exp(x)-1、ln(1+x) 精确得多
inline mpfr_class expm1(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_expm1(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class log1p(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_log1p(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
// 自然对数。log(x)（单参数）等价于 ln(x)；需要指定精度时用 ln(x, p[, rnd])。
inline mpfr_class ln   (const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_log  (r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class log2 (const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_log2 (r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class log10(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_log10(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
// log 的第二个参数一律是“底数”（单参数形式与 std::log 一致，为自然对数）。
// 旧版把第二个参数当精度，导致 mmath::log(x, 10) 被静默算成“10 位精度的自然对数”；
// 该精度重载已删除，换底请用 log(x, base[, p[, rnd]])，自然对数精度用 ln(x, p[, rnd])。
inline mpfr_class log(const mpfr_class& x) { return ln(x); }
inline mpfr_class log(const mpfr_class& x, const mpfr_class& base, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) {
    mpfr_prec_t pr = detail::pick(x.precision(), base.precision(), p);
    mpfr_class r{with_prec(pr)}, lb{with_prec(pr)};
    mpfr_log(r.get_mpfr_t(), x.get_mpfr_t(), rnd);
    mpfr_log(lb.get_mpfr_t(), base.get_mpfr_t(), rnd);
    mpfr_div(r.get_mpfr_t(), r.get_mpfr_t(), lb.get_mpfr_t(), rnd);
    return r;
}

// ----- 三角 -----
inline mpfr_class sin(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_sin(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class cos(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_cos(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class tan(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_tan(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class sec(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_sec(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class csc(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_csc(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class cot(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_cot(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }

inline std::pair<mpfr_class, mpfr_class> sin_cos(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) {
    mpfr_prec_t pr = detail::pick(x.precision(), 0, p);
    mpfr_class s{with_prec(pr)}, c{with_prec(pr)};
    mpfr_sin_cos(s.get_mpfr_t(), c.get_mpfr_t(), x.get_mpfr_t(), rnd);
    return {std::move(s), std::move(c)};
}

inline mpfr_class asin(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_asin(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class acos(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_acos(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class atan(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_atan(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class atan2(const mpfr_class& y, const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) {
    mpfr_class r{with_prec(detail::pick(x.precision(), y.precision(), p))};
    mpfr_atan2(r.get_mpfr_t(), y.get_mpfr_t(), x.get_mpfr_t(), rnd);
    return r;
}

// ----- 双曲 -----
inline mpfr_class sinh(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_sinh(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class cosh(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_cosh(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class tanh(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_tanh(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class sech(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_sech(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class csch(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_csch(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class coth(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_coth(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class asinh(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_asinh(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class acosh(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_acosh(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class atanh(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_atanh(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }

// ----- 特殊函数 -----
inline mpfr_class gamma (const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_gamma (r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class lgamma(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_lngamma(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class digamma(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()){ mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_digamma(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class zeta  (const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_zeta  (r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class erf   (const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_erf   (r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class erfc  (const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_erfc  (r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class beta(const mpfr_class& a, const mpfr_class& b, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) {
    mpfr_class r{with_prec(detail::pick(a.precision(), b.precision(), p))};
    mpfr_beta(r.get_mpfr_t(), a.get_mpfr_t(), b.get_mpfr_t(), rnd);
    return r;
}
inline mpfr_class agm(const mpfr_class& a, const mpfr_class& b, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) {
    mpfr_class r{with_prec(detail::pick(a.precision(), b.precision(), p))};
    mpfr_agm(r.get_mpfr_t(), a.get_mpfr_t(), b.get_mpfr_t(), rnd);
    return r;
}
inline mpfr_class li2(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) {
    mpfr_class r{with_prec(detail::pick(x.precision(), 0, p))};
    mpfr_li2(r.get_mpfr_t(), x.get_mpfr_t(), rnd);
    return r;
}
inline mpfr_class eint(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) {
    mpfr_class r{with_prec(detail::pick(x.precision(), 0, p))};
    mpfr_eint(r.get_mpfr_t(), x.get_mpfr_t(), rnd);
    return r;
}

// ----- 贝塞尔 -----
inline mpfr_class j0(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_j0(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class j1(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_j1(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class jn(long n, const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_jn(r.get_mpfr_t(), n, x.get_mpfr_t(), rnd); return r; }
inline mpfr_class y0(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_y0(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class y1(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_y1(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class yn(long n, const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_yn(r.get_mpfr_t(), n, x.get_mpfr_t(), rnd); return r; }

// ----- 取整 / 分解 -----
// ceil/floor/trunc/round 的结果必然是整数，MPFR 不提供舍入模式参数（结果不可表示时
// 一律按最近舍入），因此这四个函数不接受 rnd；rint 会遵循舍入模式。
inline mpfr_class ceil (const mpfr_class& x, mpfr_prec_t p = 0) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_ceil (r.get_mpfr_t(), x.get_mpfr_t()); return r; }
inline mpfr_class floor(const mpfr_class& x, mpfr_prec_t p = 0) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_floor(r.get_mpfr_t(), x.get_mpfr_t()); return r; }
inline mpfr_class trunc(const mpfr_class& x, mpfr_prec_t p = 0) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_trunc(r.get_mpfr_t(), x.get_mpfr_t()); return r; }
inline mpfr_class round(const mpfr_class& x, mpfr_prec_t p = 0) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_round(r.get_mpfr_t(), x.get_mpfr_t()); return r; }
inline mpfr_class rint (const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_rint (r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
// 指定取整方向的整数化（等价于 rint + 对应舍入模式，但接口更直观）
inline mpfr_class rint_ceil (const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_rint_ceil (r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class rint_floor(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_rint_floor(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class rint_round(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_rint_round(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class rint_trunc(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_rint_trunc(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class frac(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_frac(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class abs(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) { mpfr_class r{with_prec(detail::pick(x.precision(),0,p))}; mpfr_abs(r.get_mpfr_t(), x.get_mpfr_t(), rnd); return r; }
inline mpfr_class fmod(const mpfr_class& x, const mpfr_class& y, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) {
    mpfr_class r{with_prec(detail::pick(x.precision(), y.precision(), p))};
    mpfr_fmod(r.get_mpfr_t(), x.get_mpfr_t(), y.get_mpfr_t(), rnd);
    return r;
}
// IEEE 754 remainder：x - n*y，n 为最接近 x/y 的整数（平局取偶）
inline mpfr_class remainder(const mpfr_class& x, const mpfr_class& y, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) {
    mpfr_class r{with_prec(detail::pick(x.precision(), y.precision(), p))};
    mpfr_remainder(r.get_mpfr_t(), x.get_mpfr_t(), y.get_mpfr_t(), rnd);
    return r;
}
inline mpfr_class min(const mpfr_class& a, const mpfr_class& b) { return a < b ? a : b; }
inline mpfr_class max(const mpfr_class& a, const mpfr_class& b) { return a > b ? a : b; }
// 把 x 夹到 [lo, hi]；lo > hi 时抛 std::invalid_argument
inline mpfr_class clamp(const mpfr_class& x, const mpfr_class& lo, const mpfr_class& hi) {
    if (lo > hi) gmp_mpfr::detail::throw_err(mpfr_error::invalid_argument, "math::clamp: lo > hi");
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}
// 近似相等判定：|a-b| <= max(abs_eps, rel_eps * max(|a|,|b|))
inline bool approximately_equal(const mpfr_class& a, const mpfr_class& b,
                                const mpfr_class& rel_eps = mpfr_class("1e-30", 256),
                                const mpfr_class& abs_eps = mpfr_class("0", 256)) {
    if (a.is_nan() || b.is_nan()) return false;
    if (a == b) return true;
    const mpfr_class diff = abs(a - b);
    const mpfr_class scale = max(abs(a), abs(b));
    return diff <= max(abs_eps, rel_eps * scale);
}
inline mpfr_class copysign(const mpfr_class& x, const mpfr_class& y, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) {
    mpfr_class r{with_prec(detail::pick(x.precision(), y.precision(), p))};
    mpfr_copysign(r.get_mpfr_t(), x.get_mpfr_t(), y.get_mpfr_t(), rnd);
    return r;
}
inline mpfr_class dim(const mpfr_class& a, const mpfr_class& b, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) {
    mpfr_class r{with_prec(detail::pick(a.precision(), b.precision(), p))};
    mpfr_dim(r.get_mpfr_t(), a.get_mpfr_t(), b.get_mpfr_t(), rnd);
    return r;
}
inline mpfr_class nextafter(const mpfr_class& x, const mpfr_class& y, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) {
    mpfr_class r{with_prec(detail::pick(x.precision(), y.precision(), p))};
    mpfr_set(r.get_mpfr_t(), x.get_mpfr_t(), rnd);
    mpfr_nexttoward(r.get_mpfr_t(), y.get_mpfr_t());
    return r;
}
inline std::pair<mpfr_class, mpfr_class> modf(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) {
    mpfr_prec_t pr = detail::pick(x.precision(), 0, p);
    mpfr_class ip{with_prec(pr)}, fp{with_prec(pr)};
    mpfr_modf(ip.get_mpfr_t(), fp.get_mpfr_t(), x.get_mpfr_t(), rnd);
    return {std::move(ip), std::move(fp)};
}
inline std::pair<mpfr_class, mpfr_exp_t> frexp(const mpfr_class& x, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) {
    mpfr_exp_t e = 0;
    mpfr_class f{with_prec(detail::pick(x.precision(), 0, p))};
    mpfr_frexp(&e, f.get_mpfr_t(), x.get_mpfr_t(), rnd);
    return {std::move(f), e};
}
inline mpfr_class fma(const mpfr_class& a, const mpfr_class& b, const mpfr_class& c, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) {
    mpfr_class r{with_prec(detail::pick(std::max(a.precision(), b.precision()), c.precision(), p))};
    mpfr_fma(r.get_mpfr_t(), a.get_mpfr_t(), b.get_mpfr_t(), c.get_mpfr_t(), rnd);
    return r;
}
inline mpfr_class fms(const mpfr_class& a, const mpfr_class& b, const mpfr_class& c, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) {
    mpfr_class r{with_prec(detail::pick(std::max(a.precision(), b.precision()), c.precision(), p))};
    mpfr_fms(r.get_mpfr_t(), a.get_mpfr_t(), b.get_mpfr_t(), c.get_mpfr_t(), rnd);
    return r;
}

// ----- 数组运算（比逐个相加/相乘更精确：MPFR 内部用扩展精度累加） -----
inline mpfr_class sum(std::span<const mpfr_class> v, mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) {
    mpfr_prec_t pr = p > 0 ? p : default_precision();
    for (const auto& e : v) pr = std::max(pr, e.precision());
    mpfr_class r{with_prec(pr)};
    if (v.empty()) { mpfr_set_zero(r.get_mpfr_t(), 1); return r; }

    // mpfr_sum 的参数类型是 const mpfr_ptr*（元素为非 const 指针），但它不会
    // 修改输入，这里去掉 const 只是适配 MPFR 的接口。
    std::vector<mpfr_ptr> tab;
    tab.reserve(v.size());
    for (const auto& e : v) tab.push_back(const_cast<mpfr_ptr>(e.get_mpfr_t()));
    mpfr_sum(r.get_mpfr_t(), tab.data(), static_cast<unsigned long>(tab.size()), rnd);
    return r;
}

inline mpfr_class dot(std::span<const mpfr_class> a, std::span<const mpfr_class> b,
                      mpfr_prec_t p = 0, mpfr_rnd_t rnd = rounding()) {
    if (a.size() != b.size())
        gmp_mpfr::detail::throw_err(mpfr_error::invalid_argument, "math::dot: size mismatch");
    mpfr_prec_t pr = p > 0 ? p : default_precision();
    for (const auto& e : a) pr = std::max(pr, e.precision());
    for (const auto& e : b) pr = std::max(pr, e.precision());
    mpfr_class r{with_prec(pr)};
    if (a.empty()) { mpfr_set_zero(r.get_mpfr_t(), 1); return r; }

    std::vector<mpfr_ptr> ta, tb;
    ta.reserve(a.size());
    tb.reserve(b.size());
    for (const auto& e : a) ta.push_back(const_cast<mpfr_ptr>(e.get_mpfr_t()));
    for (const auto& e : b) tb.push_back(const_cast<mpfr_ptr>(e.get_mpfr_t()));
    mpfr_dot(r.get_mpfr_t(), ta.data(), tb.data(),
             static_cast<unsigned long>(ta.size()), rnd);
    return r;
}

// ----- 常量 -----
// 常量的舍入模式参与缓存键；显式传入 rnd 只会命中该模式的缓存槽。
inline const mpfr_class& pi(mpfr_prec_t prec = default_precision(), mpfr_rnd_t rnd = rounding()) {
    return detail::cached_constant(prec, rnd, [rnd](mpfr_t v) { mpfr_const_pi(v, rnd); });
}
inline const mpfr_class& e(mpfr_prec_t prec = default_precision(), mpfr_rnd_t rnd = rounding()) {
    return detail::cached_constant(prec, rnd, [rnd](mpfr_t v) {
        mpfr_set_ui(v, 1, rnd);
        mpfr_exp(v, v, rnd);
    });
}
inline const mpfr_class& euler_gamma(mpfr_prec_t prec = default_precision(), mpfr_rnd_t rnd = rounding()) {
    return detail::cached_constant(prec, rnd, [rnd](mpfr_t v) { mpfr_const_euler(v, rnd); });
}
inline const mpfr_class& ln2(mpfr_prec_t prec = default_precision(), mpfr_rnd_t rnd = rounding()) {
    return detail::cached_constant(prec, rnd, [rnd](mpfr_t v) { mpfr_const_log2(v, rnd); });
}
inline const mpfr_class& catalan(mpfr_prec_t prec = default_precision(), mpfr_rnd_t rnd = rounding()) {
    return detail::cached_constant(prec, rnd, [rnd](mpfr_t v) { mpfr_const_catalan(v, rnd); });
}
inline const mpfr_class& sqrt2(mpfr_prec_t prec = default_precision(), mpfr_rnd_t rnd = rounding()) {
    return detail::cached_constant(prec, rnd, [rnd](mpfr_t v) { mpfr_sqrt_ui(v, 2, rnd); });
}
inline const mpfr_class& sqrt3(mpfr_prec_t prec = default_precision(), mpfr_rnd_t rnd = rounding()) {
    return detail::cached_constant(prec, rnd, [rnd](mpfr_t v) { mpfr_sqrt_ui(v, 3, rnd); });
}
// 黄金比例 φ = (1+√5)/2
inline const mpfr_class& phi(mpfr_prec_t prec = default_precision(), mpfr_rnd_t rnd = rounding()) {
    return detail::cached_constant(prec, rnd, [rnd](mpfr_t v) {
        mpfr_sqrt_ui(v, 5, rnd);
        mpfr_add_ui(v, v, 1, rnd);
        mpfr_div_ui(v, v, 2, rnd);
    });
}
// Apéry 常数 ζ(3)
inline const mpfr_class& zeta3(mpfr_prec_t prec = default_precision(), mpfr_rnd_t rnd = rounding()) {
    return detail::cached_constant(prec, rnd, [rnd](mpfr_t v) { mpfr_zeta_ui(v, 3, rnd); });
}

}  // namespace math

// ============================================================================
// gmp_ext
// ============================================================================

namespace gmp_ext {

inline mpz_class pow(const mpz_class& b, unsigned long e) {
    mpz_class r; mpz_pow_ui(r.get_mpz_t(), b.get_mpz_t(), e); return r;
}
inline mpz_class powm(const mpz_class& b, const mpz_class& e, const mpz_class& m) {
    mpz_class r; mpz_powm(r.get_mpz_t(), b.get_mpz_t(), e.get_mpz_t(), m.get_mpz_t()); return r;
}
inline mpz_class factorial(unsigned long n)             { mpz_class r; mpz_fac_ui(r.get_mpz_t(), n); return r; }
inline mpz_class binomial(unsigned long n, unsigned long k) { mpz_class r; mpz_bin_uiui(r.get_mpz_t(), n, k); return r; }
inline mpz_class fibonacci(unsigned long n)             { mpz_class r; mpz_fib_ui(r.get_mpz_t(), n); return r; }
inline mpz_class lucnum(unsigned long n)                { mpz_class r; mpz_lucnum_ui(r.get_mpz_t(), n); return r; }
inline mpz_class primorial(unsigned long n)             { mpz_class r; mpz_primorial_ui(r.get_mpz_t(), n); return r; }
inline mpz_class gcd(const mpz_class& a, const mpz_class& b) { mpz_class r; mpz_gcd(r.get_mpz_t(), a.get_mpz_t(), b.get_mpz_t()); return r; }
inline mpz_class lcm(const mpz_class& a, const mpz_class& b) { mpz_class r; mpz_lcm(r.get_mpz_t(), a.get_mpz_t(), b.get_mpz_t()); return r; }

inline std::tuple<mpz_class, mpz_class, mpz_class> gcdext(const mpz_class& a, const mpz_class& b) {
    mpz_class g, s, t;
    mpz_gcdext(g.get_mpz_t(), s.get_mpz_t(), t.get_mpz_t(), a.get_mpz_t(), b.get_mpz_t());
    return {std::move(g), std::move(s), std::move(t)};
}

inline std::expected<mpz_class, mpfr_error> invert(const mpz_class& a, const mpz_class& m) {
    mpz_class r;
    if (mpz_invert(r.get_mpz_t(), a.get_mpz_t(), m.get_mpz_t()) == 0)
        return std::unexpected(mpfr_error::domain_error);
    return r;
}

inline int jacobi(const mpz_class& a, const mpz_class& n)  { return mpz_jacobi  (a.get_mpz_t(), n.get_mpz_t()); }
inline int legendre(const mpz_class& a, const mpz_class& p){ return mpz_legendre(a.get_mpz_t(), p.get_mpz_t()); }

inline bool is_prime(const mpz_class& n, int reps = 25) { return mpz_probab_prime_p(n.get_mpz_t(), reps) > 0; }
inline mpz_class next_prime(const mpz_class& n) { mpz_class r; mpz_nextprime(r.get_mpz_t(), n.get_mpz_t()); return r; }
inline mpz_class sqrt(const mpz_class& n)        { mpz_class r; mpz_sqrt(r.get_mpz_t(), n.get_mpz_t()); return r; }

inline std::pair<mpz_class, mpz_class> rootrem(const mpz_class& a, unsigned long n) {
    mpz_class root, rem;
    mpz_rootrem(root.get_mpz_t(), rem.get_mpz_t(), a.get_mpz_t(), n);
    return {std::move(root), std::move(rem)};
}

inline mpfr_class to_mpfr(const mpq_class& q, mpfr_prec_t prec = default_precision()) {
    return mpfr_class(q, prec);
}

}  // namespace gmp_ext

}  // namespace gmp_mpfr

// 全局便捷别名
using gmp_mpfr::DEFAULT_PREC;
using gmp_mpfr::mpfr_class;
using gmp_mpfr::with_prec;
using gmp_mpfr::with_prec_t;
using gmp_mpfr::mpfr_error;
using gmp_mpfr::rounding;
using gmp_mpfr::set_rounding;
using gmp_mpfr::scoped_rounding;
using gmp_mpfr::default_precision;
using gmp_mpfr::set_default_precision;
using gmp_mpfr::scoped_precision;
using gmp_mpfr::format_options;
using gmp_mpfr::default_format;
using gmp_mpfr::set_default_format;
using gmp_mpfr::reset_default_format;
using gmp_mpfr::scoped_format;

namespace mmath = gmp_mpfr::math;
namespace gext   = gmp_mpfr::gmp_ext;

// ============================================================================
// std::format 支持（需要 C++23 <format>；定义 NEO_MATH_NO_FORMAT 可关闭）
//
//   std::print("{}\n", x);          // 默认十进制
//   std::format("{:.20f}", x);      // 20 位有效数字、定点
//   std::format("{:.20e}", x);      // 科学计数
//   std::format("{:x}", x);         // 十六进制（定点）
//   std::format("{:p}", x);         // 精确十六进制往返形式（to_string_hex）
//
// 只支持 ".digits" 与类型字符 f/e/x/X/b/p，不支持宽度/对齐等字符串格式项。
// ============================================================================
#if defined(NEO_MATH_HAS_FORMAT) && defined(__cpp_lib_format) && __cpp_lib_format >= 201907L
namespace std {

template <>
struct formatter<gmp_mpfr::mpfr_class, char> {
    std::size_t digits = 0;
    int         base   = 10;
    bool        sci    = false;
    bool        exact  = false;

    constexpr auto parse(std::format_parse_context& ctx) {
        auto it  = ctx.begin();
        auto end = ctx.end();

        if (it != end && *it == '.') {
            ++it;
            if (it == end || *it < '0' || *it > '9')
                throw std::format_error("mpfr_class: '.' must be followed by digits");
            digits = 0;
            while (it != end && *it >= '0' && *it <= '9')
                digits = digits * 10 + static_cast<std::size_t>(*it++ - '0');
        }
        if (it != end && *it != '}') {
            switch (*it) {
                case 'f': sci = false; base = 10; break;
                case 'e': sci = true;  base = 10; break;
                case 'x': sci = false; base = 16; break;
                case 'X': sci = false; base = 16; break;
                case 'b': sci = false; base = 2;  break;
                case 'p': exact = true;           break;
                default:  throw std::format_error("mpfr_class: invalid format specifier");
            }
            ++it;
        }
        if (it != end && *it != '}')
            throw std::format_error("mpfr_class: invalid format specifier");
        return it;
    }

    template <class FormatContext>
    auto format(const gmp_mpfr::mpfr_class& v, FormatContext& ctx) const {
        const std::string s = exact ? v.to_string_hex() : v.to_string(digits, base, sci);
        return std::format_to(ctx.out(), "{}", s);
    }
};

}  // namespace std
#endif

#endif  // GMP_MPFR_MATH_H