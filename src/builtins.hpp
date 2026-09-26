// 内建数学函数（来自 vendor/neo-math.h 的 GMP + MPFR 封装）。
//
// 表本身在 builtins.cpp：那是唯一包含 vendor/neo-math.h 的编译单元，
// 这样第三方头的编译开销与警告都被关在一个 TU 里（Makefile 用 -isystem vendor）。
//
// 这里只暴露查表接口，三处各取所需：
//   * 解析器：名字 + 实参个数在**编译期**校验（未知名字/个数不对都是编译错误）；
//   * 编译器：把表项下标写进 CALL 指令的操作数；
//   * VM / 反汇编：按下标取回名字与参数个数。
//
// 加一个内建函数 = 在 builtins.cpp 的表里加一行（需要时再写一个求值适配器）。
#pragma once

#include "error.hpp"
#include "value.hpp"

#include <span>
#include <string>
#include <string_view>

namespace sc {

/// 按名字与实参个数查表项下标；没有这一项返回 -1。
/// 同一个名字可以有多个参数个数（例如 log(x) 与 log(x, base)），所以必须一起查。
/// 编译期就能定下来，因此运行期不再做名字解析。
[[nodiscard]] int findBuiltin(std::string_view name, int arity);

/// 查表失败时的报错文本（未知名字 / 参数个数不对）。措辞归 builtins.cpp 管。
[[nodiscard]] std::string builtinLookupError(std::string_view name, int arity);

/// 表项的参数个数；下标越界返回 -1。
[[nodiscard]] int builtinArity(int index) noexcept;

/// 表项名字；下标越界返回空串。
[[nodiscard]] std::string_view builtinName(int index) noexcept;

/// 执行一个内建函数。越界下标、参数个数不符、以及数学库抛出的异常
/// 都在这里收口成 Error——C++ 异常绝不穿过 VM 主循环。
/// args 只在调用期间被读取，不持有引用。
[[nodiscard]] Result<Value> runBuiltin(int index, std::span<const Value> args,
                                       const ValueLimits& limits);

} // namespace sc
