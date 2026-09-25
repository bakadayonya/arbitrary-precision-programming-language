#include "config.hpp"

#include <charconv>
#include <format>
#include <limits>
#include <ostream>
#include <print>
#include <string>
#include <string_view>
#include <type_traits>

namespace sc {

// variant 靠类型区分字段指针；若 mpfr_prec_t 与 long long 同型，下面两个重载会重复。
static_assert(!std::is_same_v<mpfr_prec_t, long long>,
              "ConfigFieldPtr 需要互不相同的成员指针类型");

namespace {

/// 解析布尔字面量。
bool parseBool(std::string_view text, bool& out) {
    if (text == "true" || text == "on" || text == "yes" || text == "1") {
        out = true;
        return true;
    }
    if (text == "false" || text == "off" || text == "no" || text == "0") {
        out = false;
        return true;
    }
    return false;
}

/// 整串必须是合法整数，不允许尾随垃圾。
template <class T>
bool parseIntegral(std::string_view text, T& out) {
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const auto result = std::from_chars(first, last, out);
    return result.ec == std::errc{} && result.ptr == last;
}

/// 把文本写进 Config 的一个字段；类型由字段指针的类型决定。
struct SetVisitor {
    Config& config;
    std::string_view name;
    std::string_view value;

    [[nodiscard]] std::unexpected<Error> bad() const {
        return fail(std::format("可调参数 '{}' 的取值非法: {}", name, value));
    }

    Status operator()(mpfr_prec_t Config::* field) const {
        mpfr_prec_t parsed = 0;
        if (!parseIntegral(value, parsed) || parsed < MPFR_PREC_MIN) return bad();
        config.*field = parsed;
        return {};
    }

    Status operator()(int Config::* field) const {
        long long parsed = 0;
        if (!parseIntegral(value, parsed)) return bad();
        if (parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max())
            return bad();
        config.*field = static_cast<int>(parsed);
        return {};
    }

    Status operator()(long long Config::* field) const {
        long long parsed = 0;
        if (!parseIntegral(value, parsed)) return bad();
        config.*field = parsed;
        return {};
    }

    Status operator()(unsigned long long Config::* field) const {
        unsigned long long parsed = 0;
        if (!parseIntegral(value, parsed)) return bad();
        config.*field = parsed;
        return {};
    }

    Status operator()(bool Config::* field) const {
        bool parsed = false;
        if (!parseBool(value, parsed)) return bad();
        config.*field = parsed;
        return {};
    }
};

} // namespace

Result<Config> applyConfigOption(Config config, std::string_view name, std::string_view value) {
    const ConfigField* field = findConfigField(name);
    if (field == nullptr)
        return fail(std::format("未知的可调参数: {}（用 --list-config 查看全部参数）", name));

    if (auto status = std::visit(SetVisitor{config, name, value}, field->ptr); !status)
        return std::unexpected(status.error());

    config.normalize();
    return config;
}

std::string formatConfigField(const Config& config, const ConfigField& field) {
    return std::visit(
        [&config](auto member) -> std::string { return std::format("{}", config.*member); },
        field.ptr);
}

void printConfig(const Config& config, std::ostream& os) {
    for (const ConfigField& field : CONFIG_FIELDS) {
        std::print(os, "  {:<18} = {:<14} # {}\n", field.name, formatConfigField(config, field),
                   field.help);
    }
}

void printConfigHelp(std::ostream& os) {
    for (const ConfigField& field : CONFIG_FIELDS)
        std::print(os, "    {:<18} {}\n", field.name, field.help);
}

} // namespace sc
