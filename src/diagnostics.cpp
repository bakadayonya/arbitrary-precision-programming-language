#include "diagnostics.hpp"

#include "unicode.hpp"

#include <cstddef>
#include <format>
#include <string>

namespace sc {

namespace {

struct LineColumn {
    int line = 1;
    int column = 1;        // 码点列（1 基）
    int displayColumn = 1; // 显示列（1 基），用于插入符缩进
};

/// 把字节偏移换算成行号 + 码点列 + 显示列。
/// 本项目的 Error.pos 都落在码点边界上；万一落在多字节序列中间，
/// 按"1 列 + 1 宽度"兜底前进，既不越界也不会死循环。
LineColumn locate(std::string_view source, std::size_t pos) {
    LineColumn result;
    const std::size_t limit = pos < source.size() ? pos : source.size();

    std::size_t i = 0;
    while (i < limit) {
        const unsigned char byte = static_cast<unsigned char>(source[i]);
        if (byte < 0x80) {
            if (byte == '\n') {
                ++result.line;
                result.column = 1;
                result.displayColumn = 1;
            } else {
                ++result.column;
                ++result.displayColumn;
            }
            ++i;
            continue;
        }

        const auto decoded = unicode::decodeAt(source, i);
        if (!decoded.ok()) {
            ++result.column;
            ++result.displayColumn;
            ++i;
            continue;
        }
        ++result.column;
        result.displayColumn += unicode::displayWidth(decoded.codepoint);
        i += static_cast<std::size_t>(decoded.length);
    }
    return result;
}

} // namespace

std::string formatError(const Error& error, std::string_view source) {
    std::string out = std::format("错误: {}", error.message);
    if (error.pos < 0 || static_cast<std::size_t>(error.pos) > source.size()) return out;

    const auto pos = static_cast<std::size_t>(error.pos);
    const LineColumn at = locate(source, pos);

    // 定位到出错所在的整行
    std::size_t begin = pos;
    while (begin > 0 && source[begin - 1] != '\n') --begin;
    std::size_t end = pos;
    while (end < source.size() && source[end] != '\n') ++end;

    const std::string number = std::to_string(at.line);
    out += std::format("\n  --> 第 {} 行 第 {} 列", at.line, at.column);
    out += std::format("\n  {} | {}", number, source.substr(begin, end - begin));
    out += std::format("\n  {} | {}^", std::string(number.size(), ' '),
                       std::string(static_cast<std::size_t>(at.displayColumn - 1), ' '));
    return out;
}

} // namespace sc
