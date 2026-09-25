// 词法分析。
//
// UTF-8：ASCII 走快速路径（纯字节比较），非 ASCII 交给 unicode 模块解码后按
// Unicode 语义分类。标识符在词法阶段做 NFC 归一化，因此视觉相同的两种写法是同一个名字。
// config 必须在 Lexer 存活期间有效（用它取 max-string-bytes）。
#pragma once

#include "config.hpp"
#include "error.hpp"
#include "token.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace sc {

class Lexer {
public:
    Lexer(std::string_view source, const Config& config) : src_(source), config_(config) {}

    [[nodiscard]] Result<std::vector<Token>> tokenize();

private:
    [[nodiscard]] bool atEnd() const { return pos_ >= src_.size(); }
    [[nodiscard]] char byte() const { return src_[pos_]; }

    [[nodiscard]] Token readNumber();
    [[nodiscard]] Token readIdentifier();
    /// start 是开引号的位置，用作 token 位置与"字符串未结束"的报错位置。
    [[nodiscard]] Result<Token> readString(int start);
    /// 处理一个反斜杠转义，把结果追加到 out。escapePos 是反斜杠的位置。
    [[nodiscard]] Status readEscape(std::string& out, int escapePos);

    std::string_view src_;
    const Config& config_;
    std::size_t pos_ = 0;
};

} // namespace sc
