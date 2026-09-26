#include "number.hpp"

#include <algorithm>

namespace sc {

namespace {

/// 结果精度不能低于操作数，否则会凭空丢精度。
mpfr_prec_t combinePrec(mpfr_prec_t a, mpfr_prec_t b) { return a > b ? a : b; }

/// mpfr_asprintf 的失败兜底文本。
constexpr const char* FORMAT_FAILED = "<数值格式化失败>";

} // namespace

Mpfr::Mpfr(mpfr_prec_t precision) {
    mpfr_init2(v_, precision < MPFR_PREC_MIN ? MPFR_PREC_MIN : precision);
    mpfr_set_zero(v_, 1);
}

Mpfr::Mpfr(const mpz_class& z, mpfr_prec_t precision) {
    mpfr_init2(v_, precision < MPFR_PREC_MIN ? MPFR_PREC_MIN : precision);
    mpfr_set_z(v_, z.get_mpz_t(), MPFR_RNDN);
}

std::optional<Mpfr> Mpfr::fromString(std::string_view s, mpfr_prec_t precision) {
    Mpfr result(precision);
    const std::string text(s);
    if (mpfr_set_str(result.v_, text.c_str(), 10, MPFR_RNDN) != 0) return std::nullopt;
    return result;
}

Mpfr::Mpfr(const Mpfr& other) {
    mpfr_init2(v_, mpfr_get_prec(other.v_));
    mpfr_set(v_, other.v_, MPFR_RNDN);
}

Mpfr::Mpfr(Mpfr&& other) noexcept {
    mpfr_init2(v_, mpfr_get_prec(other.v_));
    mpfr_swap(v_, other.v_);
}

Mpfr& Mpfr::operator=(const Mpfr& other) {
    if (this != &other) {
        // 目标精度足够就原地拷贝，否则整体重建（mpfr_set 会按目标精度舍入）。
        mpfr_set_prec(v_, mpfr_get_prec(other.v_));
        mpfr_set(v_, other.v_, MPFR_RNDN);
    }
    return *this;
}

Mpfr& Mpfr::operator=(Mpfr&& other) noexcept {
    if (this != &other) mpfr_swap(v_, other.v_);
    return *this;
}

Mpfr::~Mpfr() { mpfr_clear(v_); }

mpfr_prec_t Mpfr::wider(const Mpfr& other) const {
    return combinePrec(mpfr_get_prec(v_), mpfr_get_prec(other.v_));
}

std::string Mpfr::to_string(int digits) const {
    char* buf = nullptr;
    const int significant = digits > 0 ? digits : 1;
    const int written = mpfr_asprintf(&buf, "%.*Rg", significant, v_);
    if (written < 0 || buf == nullptr) return FORMAT_FAILED;
    std::string text(buf);
    mpfr_free_str(buf);
    return text;
}

std::string Mpfr::to_key() const {
    char* buf = nullptr;
    const int written = mpfr_asprintf(&buf, "%Ra", v_);
    if (written < 0 || buf == nullptr) return FORMAT_FAILED;
    std::string text(buf);
    mpfr_free_str(buf);
    return text;
}

Mpfr Mpfr::operator-() const {
    Mpfr r(precision());
    mpfr_neg(r.v_, v_, MPFR_RNDN);
    return r;
}

Mpfr Mpfr::operator+(const Mpfr& other) const {
    Mpfr r(wider(other));
    mpfr_add(r.v_, v_, other.v_, MPFR_RNDN);
    return r;
}

Mpfr Mpfr::operator-(const Mpfr& other) const {
    Mpfr r(wider(other));
    mpfr_sub(r.v_, v_, other.v_, MPFR_RNDN);
    return r;
}

Mpfr Mpfr::operator*(const Mpfr& other) const {
    Mpfr r(wider(other));
    mpfr_mul(r.v_, v_, other.v_, MPFR_RNDN);
    return r;
}

Mpfr Mpfr::operator/(const Mpfr& other) const {
    Mpfr r(wider(other));
    mpfr_div(r.v_, v_, other.v_, MPFR_RNDN);
    return r;
}

Mpfr Mpfr::pow(const Mpfr& base, const Mpfr& exponent) {
    Mpfr r(combinePrec(base.precision(), exponent.precision()));
    mpfr_pow(r.v_, base.v_, exponent.v_, MPFR_RNDN);
    return r;
}

int Mpfr::compare(const Mpfr& other) const { return mpfr_cmp(v_, other.v_); }

bool Mpfr::equals(const Mpfr& other) const { return mpfr_equal_p(v_, other.v_) != 0; }

Mpfr Mpfr::fmod(const Mpfr& a, const Mpfr& b) {
    Mpfr r(combinePrec(a.precision(), b.precision()));
    // 与 C 的 fmod 一致：余数取被除数的符号，和"向零截断"的整数除法配套。
    mpfr_fmod(r.v_, a.v_, b.v_, MPFR_RNDN);
    return r;
}

} // namespace sc
