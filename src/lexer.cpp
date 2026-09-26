#include "lexer.hpp"

#include "unicode.hpp"

#include <cstdint>
#include <format>
#include <string>
#include <utility>

namespace sc {

namespace {

bool isAsciiDigit(char c) { return c >= '0' && c <= '9'; }
bool isAsciiAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

/// 标识符首字符：ASCII 走快速路径，其余交给 Unicode 分类（ID_Start 近似）。
bool isIdentStartChar(char32_t codepoint) {
    if (codepoint < 0x80) {
        const char c = static_cast<char>(codepoint);
        return isAsciiAlpha(c) || c == '_';
    }
    return unicode::isIdentStart(codepoint);
}

/// 标识符续字符。
bool isIdentContinueChar(char32_t codepoint) {
    if (codepoint < 0x80) {
        const char c = static_cast<char>(codepoint);
        return isAsciiAlpha(c) || isAsciiDigit(c) || c == '_';
    }
    return unicode::isIdentContinue(codepoint);
}

/// 十六进制数字的值；不是十六进制返回 -1。
int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

Result<std::vector<Token>> Lexer::tokenize() {
    std::vector<Token> tokens;
    pos_ = 0;

    while (!atEnd()) {
        const unsigned char current = static_cast<unsigned char>(byte());

        // 非 ASCII 一律先解码：可能是 Unicode 空白、标识符起始，或非法字符/编码。
        if (current >= 0x80) {
            const auto decoded = unicode::decodeAt(src_, pos_);
            if (decoded.status == unicode::DecodeStatus::Truncated)
                return incomplete("输入在字符中间结束", static_cast<int>(pos_));
            if (decoded.status == unicode::DecodeStatus::Invalid)
                return fail("源码不是合法的 UTF-8 字节序列", static_cast<int>(pos_));
            if (unicode::isSpace(decoded.codepoint)) {
                pos_ += static_cast<std::size_t>(decoded.length);
                continue;
            }
            if (isIdentStartChar(decoded.codepoint)) {
                tokens.push_back(readIdentifier());
                continue;
            }
            return fail(std::format("非法字符 {}", unicode::describe(decoded.codepoint)),
                        static_cast<int>(pos_));
        }

        const char c = static_cast<char>(current);
        if (unicode::isAsciiSpace(c)) {
            ++pos_;
            continue;
        }

        // 数值字面量：'1'、'1.5'、'.5'、'3.'、'1e-3'（数字与标识符定义都是 ASCII）
        if (isAsciiDigit(c) ||
            (c == '.' && pos_ + 1 < src_.size() && isAsciiDigit(src_[pos_ + 1]))) {
            tokens.push_back(readNumber());
            continue;
        }
        if (isAsciiAlpha(c) || c == '_') {
            tokens.push_back(readIdentifier());
            continue;
        }

        const int start = static_cast<int>(pos_);
        // 紧凑的单行分支：拆成多行反而更难对照；下面这段保持原样。
        // clang-format off
        switch (c) {
            case '"': {
                auto token = readString(start);
                if (!token) return std::unexpected(token.error());
                tokens.push_back(std::move(*token));
                break;
            }
            case '+': tokens.push_back({TokenType::Plus, "+", start}); ++pos_; break;
            case '-': tokens.push_back({TokenType::Minus, "-", start}); ++pos_; break;
            case '/': tokens.push_back({TokenType::Slash, "/", start}); ++pos_; break;
            case '%': tokens.push_back({TokenType::Percent, "%", start}); ++pos_; break;
            case '<':
                if (pos_ + 1 < src_.size() && src_[pos_ + 1] == '<') {
                    tokens.push_back({TokenType::ShiftLeft, "<<", start});
                    pos_ += 2;
                    break;
                }
                if (pos_ + 1 < src_.size() && src_[pos_ + 1] == '=') {
                    tokens.push_back({TokenType::LessEqual, "<=", start});
                    pos_ += 2;
                    break;
                }
                tokens.push_back({TokenType::Less, "<", start});
                ++pos_;
                break;
            case '>':
                if (pos_ + 1 < src_.size() && src_[pos_ + 1] == '>') {
                    tokens.push_back({TokenType::ShiftRight, ">>", start});
                    pos_ += 2;
                    break;
                }
                if (pos_ + 1 < src_.size() && src_[pos_ + 1] == '=') {
                    tokens.push_back({TokenType::GreaterEqual, ">=", start});
                    pos_ += 2;
                    break;
                }
                tokens.push_back({TokenType::Greater, ">", start});
                ++pos_;
                break;
            case '!':
                if (pos_ + 1 < src_.size() && src_[pos_ + 1] == '=') {
                    tokens.push_back({TokenType::NotEqual, "!=", start});
                    pos_ += 2;
                    break;
                }
                return fail(std::format("非法字符 {}：不等号要写成 '!='",
                                        unicode::describe(static_cast<char32_t>('!'))),
                            start);
            case '(': tokens.push_back({TokenType::LParen, "(", start}); ++pos_; break;
            case ')': tokens.push_back({TokenType::RParen, ")", start}); ++pos_; break;
            case ',': tokens.push_back({TokenType::Comma, ",", start}); ++pos_; break;
            case '{': tokens.push_back({TokenType::LBrace, "{", start}); ++pos_; break;
            case '}': tokens.push_back({TokenType::RBrace, "}", start}); ++pos_; break;
            case '=':
                if (pos_ + 1 < src_.size() && src_[pos_ + 1] == '=') {
                    tokens.push_back({TokenType::Equal, "==", start});
                    pos_ += 2;
                    break;
                }
                tokens.push_back({TokenType::Assign, "=", start});
                ++pos_;
                break;
            case ';': tokens.push_back({TokenType::Semicolon, ";", start}); ++pos_; break;
            case '*':
                if (pos_ + 1 < src_.size() && src_[pos_ + 1] == '*') {
                    tokens.push_back({TokenType::StarStar, "**", start});
                    pos_ += 2;
                } else {
                    tokens.push_back({TokenType::Star, "*", start});
                    ++pos_;
                }
                break;
            default:
                // 到这里只可能是 ASCII 非法字符（非 ASCII 已在上面处理）。
                return fail(std::format("非法字符 {}",
                                        unicode::describe(static_cast<char32_t>(current))),
                            start);
        }
        // clang-format on
    }

    tokens.push_back({TokenType::End, "", static_cast<int>(pos_)});
    return tokens;
}

Token Lexer::readNumber() {
    const std::size_t start = pos_;
    bool isFloat = false;

    while (pos_ < src_.size() && isAsciiDigit(src_[pos_]))
        ++pos_;

    if (pos_ < src_.size() && src_[pos_] == '.') {
        // "3." 是小数；但 "3.foo" 里的点不是小数点的一部分，留给后面报"非法字符"
        const bool nextIsIdent =
            pos_ + 1 < src_.size() && (isAsciiAlpha(src_[pos_ + 1]) || src_[pos_ + 1] == '_');
        if (!nextIsIdent) {
            isFloat = true;
            ++pos_;
            while (pos_ < src_.size() && isAsciiDigit(src_[pos_]))
                ++pos_;
        }
    }

    // 科学计数法：只有后面真的跟着数字才吞掉 e/E，否则回退（"1e" 是 1 和标识符 e）
    if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
        const std::size_t saved = pos_;
        ++pos_;
        if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-')) ++pos_;
        if (pos_ < src_.size() && isAsciiDigit(src_[pos_])) {
            isFloat = true;
            while (pos_ < src_.size() && isAsciiDigit(src_[pos_]))
                ++pos_;
        } else {
            pos_ = saved;
        }
    }

    return {isFloat ? TokenType::Float : TokenType::Number,
            std::string(src_.substr(start, pos_ - start)), static_cast<int>(start)};
}

Token Lexer::readIdentifier() {
    const std::size_t start = pos_;
    bool asciiOnly = true;

    while (!atEnd()) {
        const unsigned char current = static_cast<unsigned char>(byte());
        if (current < 0x80) {
            if (!isIdentContinueChar(static_cast<char32_t>(current))) break;
            ++pos_;
            continue;
        }
        const auto decoded = unicode::decodeAt(src_, pos_);
        // 编码坏了就停在这里，交给主循环报出准确位置。
        if (!decoded.ok() || !isIdentContinueChar(decoded.codepoint)) break;
        asciiOnly = false;
        pos_ += static_cast<std::size_t>(decoded.length);
    }

    std::string name(src_.substr(start, pos_ - start));
    if (!asciiOnly) {
        // 标识符按 NFC 比较：é(U+00E9) 与 e+U+0301 是同一个名字。
        // 归一化失败（内存不足）时退化为原样，词法阶段不额外报错。
        if (auto normalized = unicode::normalizeNfc(name)) name = std::move(*normalized);
    }

    // 关键字表。标识符在这里退化成关键字 token，所以加关键字只需要加一行。
    // 关键字都是 ASCII；标识符在非 ASCII 时已做过 NFC，不会与这些名字混淆。
    if (name == "print") return {TokenType::Print, std::move(name), static_cast<int>(start)};
    if (name == "true") return {TokenType::True, std::move(name), static_cast<int>(start)};
    if (name == "false") return {TokenType::False, std::move(name), static_cast<int>(start)};
    if (name == "if") return {TokenType::If, std::move(name), static_cast<int>(start)};
    if (name == "else") return {TokenType::Else, std::move(name), static_cast<int>(start)};
    if (name == "while") return {TokenType::While, std::move(name), static_cast<int>(start)};
    if (name == "for") return {TokenType::For, std::move(name), static_cast<int>(start)};
    if (name == "break") return {TokenType::Break, std::move(name), static_cast<int>(start)};
    if (name == "continue") return {TokenType::Continue, std::move(name), static_cast<int>(start)};
    return {TokenType::Ident, std::move(name), static_cast<int>(start)};
}

Result<Token> Lexer::readString(int start) {
    ++pos_; // 跳过开引号
    std::string value;

    while (true) {
        // 引号没闭合：交给 REPL 续行（裸换行是允许的）。
        if (atEnd()) return incomplete("字符串未结束", start);

        const unsigned char current = static_cast<unsigned char>(byte());
        if (current == '"') {
            ++pos_;
            break;
        }
        if (current == '\\') {
            if (auto status = readEscape(value, static_cast<int>(pos_)); !status)
                return std::unexpected(status.error());
            continue;
        }
        if (current < 0x80) {
            value.push_back(static_cast<char>(current));
            ++pos_;
        } else {
            const auto decoded = unicode::decodeAt(src_, pos_);
            if (decoded.status == unicode::DecodeStatus::Truncated)
                return incomplete("字符串在字符中间结束", static_cast<int>(pos_));
            if (decoded.status == unicode::DecodeStatus::Invalid)
                return fail("源码不是合法的 UTF-8 字节序列", static_cast<int>(pos_));
            value.append(src_.substr(pos_, static_cast<std::size_t>(decoded.length)));
            pos_ += static_cast<std::size_t>(decoded.length);
        }

        if (value.size() > config_.maxStringBytes)
            return fail(
                std::format("字符串字面量超出长度上限（上限 {} 字节）", config_.maxStringBytes),
                start);
    }

    return Token{TokenType::String, std::move(value), start};
}

Status Lexer::readEscape(std::string& out, int escapePos) {
    ++pos_; // 跳过反斜杠
    if (atEnd()) return incomplete("转义序列未结束", escapePos);

    switch (byte()) {
        case 'n':
            out.push_back('\n');
            ++pos_;
            return {};
        case 't':
            out.push_back('\t');
            ++pos_;
            return {};
        case 'r':
            out.push_back('\r');
            ++pos_;
            return {};
        case '"':
            out.push_back('"');
            ++pos_;
            return {};
        case '\\':
            out.push_back('\\');
            ++pos_;
            return {};
        case 'u': break; // 下面处理 \u{XXXX}
        default: return fail(std::format("未知的转义序列 '\\{}'", byte()), escapePos);
    }

    ++pos_; // 跳过 'u'
    if (atEnd() || byte() != '{') return fail("\\u 转义需要写成 \\u{XXXX}", escapePos);
    ++pos_;

    std::uint32_t codepoint = 0;
    int digits = 0;
    while (!atEnd() && byte() != '}') {
        const int value = hexValue(byte());
        if (value < 0)
            return fail(std::format("\\u 转义里有非十六进制字符 '{}'", byte()),
                        static_cast<int>(pos_));
        codepoint = codepoint * 16 + static_cast<std::uint32_t>(value);
        if (codepoint > 0x10FFFF) return fail("\\u 转义超出 Unicode 范围", escapePos);
        ++digits;
        ++pos_;
    }
    if (atEnd()) return incomplete("\\u 转义未结束", escapePos);
    if (digits == 0 || digits > 6) return fail("\\u 转义需要 1~6 位十六进制数字", escapePos);
    ++pos_; // 跳过 '}'

    if (codepoint >= 0xD800 && codepoint <= 0xDFFF)
        return fail("\\u 转义不能是代理区码点", escapePos);
    if (!unicode::encode(static_cast<char32_t>(codepoint), out))
        return fail("\\u 转义无法编码", escapePos);
    return {};
}

} // namespace sc
