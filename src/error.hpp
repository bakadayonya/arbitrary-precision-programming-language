// 错误类型：所有可恢复失败都用 std::expected<T, Error> 表达。
#pragma once

#include <expected>
#include <string>
#include <utility>

namespace sc {

struct Error {
    std::string message;
    int pos = -1;            // 源码字节偏移，-1 表示与具体位置无关
    bool incomplete = false; // 输入在语句结束前就用完了（REPL 可继续读入下一行）
};

template <class T> using Result = std::expected<T, Error>;
using Status = Result<void>;

/// 普通错误。
[[nodiscard]] inline std::unexpected<Error> fail(std::string message, int pos = -1) {
    return std::unexpected(Error{std::move(message), pos, false});
}

/// 输入被截断导致的错误：REPL 据此决定是否续行，而不是猜"最后一个字符是不是分号"。
[[nodiscard]] inline std::unexpected<Error> incomplete(std::string message, int pos = -1) {
    return std::unexpected(Error{std::move(message), pos, true});
}

namespace detail {

/// 依赖模板参数恒为假，用于让"忘了处理某个 variant 分支"变成编译错误。
template <class...> inline constexpr bool alwaysFalse = false;

} // namespace detail

} // namespace sc
