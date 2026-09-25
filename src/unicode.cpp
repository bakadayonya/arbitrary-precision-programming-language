#include "unicode.hpp"

#include <utf8proc.h>

#include <cstdlib>
#include <format>
#include <memory>
#include <utility>

namespace sc::unicode {

namespace {

using Utf8Byte = utf8proc_uint8_t;
using CodePoint = utf8proc_int32_t;

/// utf8proc_map 返回的是 malloc 的缓冲区，必须 free。
struct FreeDeleter {
    void operator()(Utf8Byte* pointer) const { std::free(pointer); }
};
using MallocString = std::unique_ptr<Utf8Byte, FreeDeleter>;

/// 由首字节推断序列长度；非法首字节返回 0（含 overlong 的 C0/C1 与 0xF5 以上）。
constexpr int sequenceLength(unsigned char lead) {
    if (lead < 0x80) return 1;
    if (lead >= 0xC2 && lead <= 0xDF) return 2;
    if (lead >= 0xE0 && lead <= 0xEF) return 3;
    if (lead >= 0xF0 && lead <= 0xF4) return 4;
    return 0;
}

/// 判断失败是否只是"输入被截断"：首字节合法、长度不够、已有的续字节都合规。
bool isTruncatedPrefix(std::string_view text, std::size_t offset) {
    const int needed = sequenceLength(static_cast<unsigned char>(text[offset]));
    if (needed == 0) return false; // 首字节本身非法，不是截断
    const std::size_t available = text.size() - offset;
    if (available >= static_cast<std::size_t>(needed)) return false; // 够长却非法 → 真非法
    for (std::size_t i = 1; i < available; ++i) {
        if ((static_cast<unsigned char>(text[offset + i]) & 0xC0) != 0x80) return false;
    }
    return true;
}

/// Other_ID_Start：少数不属于 L*/Nl 但可作为标识符首字符的码点。
bool isOtherIdStart(char32_t codepoint) {
    switch (codepoint) {
        case 0x1885: // MONGOLIAN LETTER ALI GALI BALUDA
        case 0x1886: // MONGOLIAN LETTER ALI GALI THREE BALUDA
        case 0x2118: // SCRIPT CAPITAL P
        case 0x212E: // ESTIMATED SYMBOL
        case 0x309B: // KATAKANA-HIRAGANA VOICED SOUND MARK
        case 0x309C: // KATAKANA-HIRAGANA SEMI-VOICED SOUND MARK
            return true;
        default: return false;
    }
}

/// Other_ID_Continue：少数可作为续字符的码点（含 ZWNJ/ZWJ）。
bool isOtherIdContinue(char32_t codepoint) {
    if (codepoint >= 0x1369 && codepoint <= 0x1371) return true; // ETHIOPIC DIGIT ONE..NINE
    switch (codepoint) {
        case 0x00B7: // MIDDLE DOT
        case 0x0387: // GREEK ANO TELEIA
        case 0x19DA: // NEW TAI LUE THAM DIGIT ONE
        case 0x200C: // ZERO WIDTH NON-JOINER
        case 0x200D: // ZERO WIDTH JOINER
        case 0x30FB: // KATAKANA MIDDLE DOT
            return true;
        default: return false;
    }
}

bool isLetterCategory(utf8proc_category_t category) {
    switch (category) {
        case UTF8PROC_CATEGORY_LU:
        case UTF8PROC_CATEGORY_LL:
        case UTF8PROC_CATEGORY_LT:
        case UTF8PROC_CATEGORY_LM:
        case UTF8PROC_CATEGORY_LO:
        case UTF8PROC_CATEGORY_NL: return true;
        default: return false;
    }
}

} // namespace

Decoded decodeAt(std::string_view text, std::size_t offset) {
    if (offset >= text.size()) return Decoded{DecodeStatus::Truncated, 0, 0};

    CodePoint codepoint = 0;
    const auto* data = reinterpret_cast<const Utf8Byte*>(text.data() + offset);
    const auto remaining = static_cast<utf8proc_ssize_t>(text.size() - offset);
    const utf8proc_ssize_t consumed = utf8proc_iterate(data, remaining, &codepoint);

    if (consumed > 0)
        return Decoded{DecodeStatus::Ok, static_cast<char32_t>(codepoint),
                       static_cast<int>(consumed)};

    // utf8proc 只报"非法"，这里再区分出"被截断"，以便 REPL 续行。
    if (isTruncatedPrefix(text, offset)) return Decoded{DecodeStatus::Truncated, 0, 0};
    return Decoded{DecodeStatus::Invalid, 0, 0};
}

int displayWidth(char32_t codepoint) {
    return utf8proc_charwidth(static_cast<CodePoint>(codepoint));
}

bool isSpace(char32_t codepoint) {
    // Unicode White_Space = Zs | Zl | Zp | TAB..CR | SPACE | NEL
    if (codepoint == 0x20 || codepoint == 0x85) return true;
    if (codepoint >= 0x09 && codepoint <= 0x0D) return true;
    switch (utf8proc_category(static_cast<CodePoint>(codepoint))) {
        case UTF8PROC_CATEGORY_ZS:
        case UTF8PROC_CATEGORY_ZL:
        case UTF8PROC_CATEGORY_ZP: return true;
        default: return false;
    }
}

bool isAsciiSpace(char byte) {
    return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\v' || byte == '\f' ||
           byte == '\r';
}

bool isIdentStart(char32_t codepoint) {
    if (codepoint < 0x80) {
        return (codepoint >= 'A' && codepoint <= 'Z') || (codepoint >= 'a' && codepoint <= 'z');
    }
    if (isLetterCategory(utf8proc_category(static_cast<CodePoint>(codepoint)))) return true;
    return isOtherIdStart(codepoint);
}

bool isIdentContinue(char32_t codepoint) {
    if (codepoint < 0x80) {
        return (codepoint >= 'A' && codepoint <= 'Z') || (codepoint >= 'a' && codepoint <= 'z') ||
               (codepoint >= '0' && codepoint <= '9');
    }
    switch (utf8proc_category(static_cast<CodePoint>(codepoint))) {
        case UTF8PROC_CATEGORY_MN:
        case UTF8PROC_CATEGORY_MC:
        case UTF8PROC_CATEGORY_ND:
        case UTF8PROC_CATEGORY_PC: return true;
        default: break;
    }
    if (isLetterCategory(utf8proc_category(static_cast<CodePoint>(codepoint)))) return true;
    return isOtherIdStart(codepoint) || isOtherIdContinue(codepoint);
}

Result<std::string> normalizeNfc(std::string_view text) {
    bool asciiOnly = true;
    for (const char byte : text) {
        if (static_cast<unsigned char>(byte) >= 0x80) {
            asciiOnly = false;
            break;
        }
    }
    if (asciiOnly) return std::string(text);
    if (text.empty()) return std::string();

    Utf8Byte* mapped = nullptr;
    const auto options = static_cast<utf8proc_option_t>(UTF8PROC_STABLE | UTF8PROC_COMPOSE);
    const utf8proc_ssize_t written =
        utf8proc_map(reinterpret_cast<const Utf8Byte*>(text.data()),
                     static_cast<utf8proc_ssize_t>(text.size()), &mapped, options);
    const MallocString holder(mapped);
    if (written < 0 || mapped == nullptr) return fail("内存不足：NFC 归一化失败");

    return std::string(reinterpret_cast<const char*>(mapped), static_cast<std::size_t>(written));
}

bool encode(char32_t codepoint, std::string& out) {
    Utf8Byte buffer[4] = {0, 0, 0, 0};
    const utf8proc_ssize_t written =
        utf8proc_encode_char(static_cast<CodePoint>(codepoint), buffer);
    if (written <= 0) return false;
    out.append(reinterpret_cast<const char*>(buffer), static_cast<std::size_t>(written));
    return true;
}

std::string describe(char32_t codepoint) {
    const auto value = static_cast<std::uint32_t>(codepoint);
    std::string text;
    if (displayWidth(codepoint) > 0 && encode(codepoint, text))
        return std::format("'{}'（U+{:04X}）", text, value);
    return std::format("U+{:04X}", value);
}

} // namespace sc::unicode
