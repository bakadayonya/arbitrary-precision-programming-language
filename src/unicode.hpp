// UTF-8 / Unicode 支持的唯一入口。
//
// 整个项目只有这里 include <utf8proc.h>：解码、合法性校验、显示宽度、
// 空白判定、标识符分类、NFC 归一化都收在这个模块里。
// 其他层（lexer / diagnostics / value）只调用这里的语义化接口，
// 因此换掉底层库（libunistring / ICU / 自生成表）只需改这一个文件。
//
// 分类语义：标识符用 ID_Start / ID_Continue 的**类别近似**
// （Lu Ll Lt Lm Lo Nl + Other_ID_Start；续字符再加 Mn Mc Nd Pc + Other_ID_Continue）。
// 与精确的 XID_* 只差少数 NFKC 不稳定字符，对本语言这种规模足够；
// 需要精确闭包时用生成表替换这两个函数即可。
#pragma once

#include "error.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace sc::unicode {

/// 解码一个码点的结果。
enum class DecodeStatus {
    Ok,        // 成功
    Invalid,   // 字节序列非法（overlong、代理区、>U+10FFFF、非法首字节…）
    Truncated, // 字节序列合法但输入在此处被截断（REPL 据此续行）
};

struct Decoded {
    DecodeStatus status = DecodeStatus::Invalid;
    char32_t codepoint = 0; // status == Ok 时有效
    int length = 0;         // status == Ok 时是消耗的字节数

    [[nodiscard]] bool ok() const { return status == DecodeStatus::Ok; }
};

/// 从 offset 处解码一个码点。
[[nodiscard]] Decoded decodeAt(std::string_view text, std::size_t offset);

/// 显示宽度：0 = 不可打印或组合符，2 = 东亚宽字符（终端列宽）。
[[nodiscard]] int displayWidth(char32_t codepoint);

/// Unicode White_Space（含 U+3000、U+00A0、U+0085 等）。
[[nodiscard]] bool isSpace(char32_t codepoint);

/// ASCII 空白快速判断，避免为 ASCII 走解码路径。
[[nodiscard]] bool isAsciiSpace(char byte);

/// 标识符首字符（ID_Start 近似，另含 Other_ID_Start）。'_' 由词法层单独处理。
[[nodiscard]] bool isIdentStart(char32_t codepoint);

/// 标识符续字符（ID_Continue 近似）。'_' 由词法层单独处理。
[[nodiscard]] bool isIdentContinue(char32_t codepoint);

/// NFC 归一化（标识符用）。纯 ASCII 输入原样返回，不分配。
[[nodiscard]] Result<std::string> normalizeNfc(std::string_view text);

/// 把一个码点编码成 UTF-8 追加到 out；码点非法时返回 false。
[[nodiscard]] bool encode(char32_t codepoint, std::string& out);

/// 供错误消息使用：可打印时返回 "'字'（U+XXXX）"，否则只返回 "U+XXXX"。
/// 绝不把原始字节塞进消息（那会产生非法 UTF-8 的输出）。
[[nodiscard]] std::string describe(char32_t codepoint);

} // namespace sc::unicode
