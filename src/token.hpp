#pragma once

#include <string>
#include <string_view>

namespace sc {

/// Token 类型。Count 是哨兵，不是真实 token。
enum class TokenType {
    Number, // 整数
    Float,  // 小数
    String, // 字符串字面量（value 里是已解转义的内容）
    Plus,
    Minus,
    Star,
    StarStar, // **
    Slash,
    Percent,    // %
    ShiftLeft,  // <<
    ShiftRight, // >>
    LParen,
    RParen,
    Ident,
    Assign,
    Print,
    Semicolon,
    End,
    Count // 哨兵：真实 token 类型数
};

/// 真实 token 类型数（不含 Count）。
inline constexpr int TOKEN_TYPE_COUNT = static_cast<int>(TokenType::Count);

struct Token {
    TokenType type;
    std::string value;
    int pos;
};

/// Token 名。漏登记的类型由 allTokenNamesDefined() 在编译期拦下。
constexpr std::string_view tokenName(TokenType type) {
    switch (type) {
        case TokenType::Number:    return "整数";
        case TokenType::Float:     return "小数";
        case TokenType::String:    return "字符串";
        case TokenType::Plus:      return "+";
        case TokenType::Minus:     return "-";
        case TokenType::Star:      return "*";
        case TokenType::StarStar:  return "**";
        case TokenType::Slash:     return "/";
        case TokenType::Percent:   return "%";
        case TokenType::ShiftLeft: return "<<";
        case TokenType::ShiftRight: return ">>";
        case TokenType::LParen:    return "(";
        case TokenType::RParen:    return ")";
        case TokenType::Ident:     return "标识符";
        case TokenType::Assign:    return "=";
        case TokenType::Print:     return "print";
        case TokenType::Semicolon: return ";";
        case TokenType::End:       return "输入结束";
        case TokenType::Count:     break;
    }
    return "?";
}

namespace detail {

constexpr bool allTokenNamesDefined() {
    for (int i = 0; i < TOKEN_TYPE_COUNT; ++i) {
        if (tokenName(static_cast<TokenType>(i)) == std::string_view{"?"}) return false;
    }
    return true;
}

} // namespace detail

static_assert(detail::allTokenNamesDefined(),
              "有 TokenType 没有在 tokenName() 里登记名字：请在 token.hpp 补上");

} // namespace sc
