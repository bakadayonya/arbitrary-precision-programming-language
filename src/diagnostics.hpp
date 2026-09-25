// 把 Error 渲染成人类可读的诊断信息。
//
// 列号语义：消息里报的是**码点列**（编辑器/日志友好）；插入符的缩进按
// **显示宽度**（东亚宽字符算 2 列），这样含中文的源码也能对齐。
// 制表符仍按 1 列算（既有局限，未做视觉对齐修正）。
#pragma once

#include "error.hpp"

#include <string>
#include <string_view>

namespace sc {

/// 有源码位置时输出 "第 L 行 第 C 列" + 源码行 + 插入符；否则只输出错误消息。
[[nodiscard]] std::string formatError(const Error& error, std::string_view source);

} // namespace sc
