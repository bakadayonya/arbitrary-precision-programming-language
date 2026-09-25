// 全部可调参数，集中一处。
//
// 设计目标：加一个可调参数 = 在 Config 里加一个字段 + 在 CONFIG_FIELDS 里加一行。
// 加完之后 CLI 的 `--set 名字=值`、REPL 的 `:set 名字 值`、`--list-config`、
// `:config`、`--help` 里的参数列表都会自动包含它，不需要改任何解析代码。
//
// 参数是运行期可改的：Engine 持有 Config，每次 runSource 时按当前值执行。
#pragma once

#include "number.hpp"
#include "value.hpp"

#include <array>
#include <iosfwd>
#include <string>
#include <string_view>
#include <variant>

namespace sc {

/// GMP 的 mpz limb 计数是 32 位 int（见 gmp.h 的 __mpz_struct），
/// 因此单个整数的位宽存在一个硬上限；越过后是未定义行为而不是"内存不够"。
inline constexpr unsigned long long GMP_HARD_MAX_BITS =
    (1ULL << 31) * static_cast<unsigned long long>(GMP_NUMB_BITS);

/// 运行期可调参数。
struct Config {
    mpfr_prec_t precision = DEFAULT_PRECISION;      // 新建浮点值的二进制精度
    int outputDigits = 0;                           // 输出位数；0 = 按每个值自身的精度
    bool allowNonFinite = false;                    // 是否允许 inf/nan
    unsigned long long maxIntegerBits = 1ULL << 30; // 单个整数的位宽预算
    long long maxIntExponent = 10'000'000;          // 整数幂的指数上限（CPU 预算）
    int maxParseDepth = 256;                        // 语法嵌套深度上限
    int maxNodesPerStatement = 4096;                // 单条语句的表达式节点预算
    int maxExprDepth = 4096;                        // 代码生成的递归上限（最后防线）
    int maxStack = 1 << 20;                         // VM 操作数栈上限
    unsigned long long maxStringBytes = 1ULL << 20; // 单个字符串的字节上限

    /// 展示用的默认位数：output-digits 为 0 时由 precision 推导。
    /// 注意：格式化「值」时应当传 outputDigits（0 = 按值自身精度），
    /// 本方法只用于帮助/横幅这类"配置说明"文本。
    [[nodiscard]] int digits() const {
        return outputDigits > 0 ? outputDigits : decimalDigitsFor(precision);
    }

    /// 交给值层的策略视图。
    [[nodiscard]] ValueLimits valueLimits() const {
        return ValueLimits{maxIntegerBits, maxIntExponent, allowNonFinite, precision,
                           static_cast<std::size_t>(maxStringBytes)};
    }

    /// 把互相冲突的设置收敛到自洽状态。
    /// 不变量：maxExprDepth 至少要覆盖 maxNodesPerStatement 隐含的 AST 深度
    /// （节点预算允许的最坏情况是约 节点数/2 层的左倾树，析构/代码生成都是递归的）。
    void normalize() {
        if (precision < MPFR_PREC_MIN) precision = MPFR_PREC_MIN;
        if (outputDigits < 0) outputDigits = 0;
        if (maxIntegerBits < 64) maxIntegerBits = 64;
        if (maxIntegerBits > GMP_HARD_MAX_BITS) maxIntegerBits = GMP_HARD_MAX_BITS;
        if (maxIntExponent < 0) maxIntExponent = 0;
        if (maxParseDepth < 1) maxParseDepth = 1;
        if (maxNodesPerStatement < 1) maxNodesPerStatement = 1;
        const int impliedDepth = maxNodesPerStatement / 2 + 1;
        if (maxExprDepth < impliedDepth) maxExprDepth = impliedDepth;
        if (maxStack < 16) maxStack = 16;
        if (maxStringBytes < 16) maxStringBytes = 16;
        if (maxStringBytes > (1ULL << 32)) maxStringBytes = 1ULL << 32;
    }
};

/// 字段指针：指向 Config 的某个成员。
using ConfigFieldPtr = std::variant<mpfr_prec_t Config::*, int Config::*, long long Config::*,
                                    unsigned long long Config::*, bool Config::*>;

/// 一个可调参数的元信息。
struct ConfigField {
    std::string_view name;
    std::string_view help;
    ConfigFieldPtr ptr;
};

inline constexpr std::array<ConfigField, 10> CONFIG_FIELDS{{
    {"precision", "新建浮点值的二进制精度", &Config::precision},
    {"output-digits", "输出有效数字位数；0 = 按每个值自身的精度", &Config::outputDigits},
    {"allow-non-finite", "是否允许 inf/nan（默认拒绝）", &Config::allowNonFinite},
    {"max-integer-bits", "单个整数的位宽预算（受 GMP 硬上限约束）", &Config::maxIntegerBits},
    {"max-int-exponent", "整数幂的指数上限", &Config::maxIntExponent},
    {"max-string-bytes", "单个字符串的字节上限", &Config::maxStringBytes},
    {"max-parse-depth", "语法嵌套深度上限", &Config::maxParseDepth},
    {"max-nodes", "单条语句的表达式节点预算", &Config::maxNodesPerStatement},
    {"max-expr-depth", "代码生成的递归上限", &Config::maxExprDepth},
    {"max-stack", "VM 操作数栈上限", &Config::maxStack},
}};

/// 按名字查参数；找不到返回 nullptr。
[[nodiscard]] constexpr const ConfigField* findConfigField(std::string_view name) {
    for (const ConfigField& field : CONFIG_FIELDS) {
        if (field.name == name) return &field;
    }
    return nullptr;
}

/// 设置一个参数（值语义：返回改好的副本）。名字未知或取值非法时返回 Error。
[[nodiscard]] Result<Config> applyConfigOption(Config config, std::string_view name,
                                               std::string_view value);

/// 单个参数的当前取值文本。
[[nodiscard]] std::string formatConfigField(const Config& config, const ConfigField& field);

/// 打印全部参数（当前值 + 说明），供 --list-config / :config。
void printConfig(const Config& config, std::ostream& os);

/// 打印参数清单（名字 + 说明），供 --help。
void printConfigHelp(std::ostream& os);

} // namespace sc
