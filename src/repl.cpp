#include "repl.hpp"

#include "config.hpp"
#include "diagnostics.hpp"
#include "unicode.hpp"

#include <algorithm>
#include <cstddef>
#include <istream>
#include <ostream>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sc {

namespace {

/// 从 offset 起的字符是不是空白；返回它占的字节数（0 = 非空白）。
std::size_t spaceLengthAt(std::string_view text, std::size_t offset) {
    const unsigned char byte = static_cast<unsigned char>(text[offset]);
    if (byte < 0x80) return unicode::isAsciiSpace(static_cast<char>(byte)) ? 1 : 0;
    const auto decoded = unicode::decodeAt(text, offset);
    if (!decoded.ok() || !unicode::isSpace(decoded.codepoint)) return 0;
    return static_cast<std::size_t>(decoded.length);
}

/// 去掉首尾空白，含 Unicode 空白（例如全角空格 U+3000）。
std::string trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size()) {
        const std::size_t length = spaceLengthAt(text, begin);
        if (length == 0) break;
        begin += length;
    }

    // 结尾要按码点回退，不能按字节砍。
    std::size_t end = begin;
    std::size_t cursor = begin;
    while (cursor < text.size()) {
        const std::size_t length = spaceLengthAt(text, cursor);
        if (length != 0) {
            cursor += length;
            continue;
        }
        const auto decoded = unicode::decodeAt(text, cursor);
        cursor += decoded.ok() ? static_cast<std::size_t>(decoded.length) : 1;
        end = cursor;
    }
    return std::string(text.substr(begin, end - begin));
}

void printCommands(std::ostream& os) {
    std::println(os, "  :help             显示本帮助\n"
                     "  :quit, :q         退出\n"
                     "  :dump on|off      开关字节码输出（写 stderr）\n"
                     "  :vars             列出变量、槽位与当前值\n"
                     "  :consts           列出常量池\n"
                     "  :prec             显示 MPFR 精度与输出精度\n"
                     "  :config           列出全部可调参数及当前值\n"
                     "  :set <名> <值>    设置一个可调参数，例如 :set precision 512\n"
                     "  :reset            清空所有变量\n"
                     "\n"
                     "  直接输入表达式即可求值，例如:\n"
                     "    x = 12345678901234567890\n"
                     "    2.0 / 3\n"
                     "    2**100\n"
                     "    print (1 + 2.5) * 3");
}

/// 处理 `:set <name> <value>`。返回 true 表示命令已被识别。
bool handleSet(const std::string& line, Engine& engine, std::ostream& out, std::ostream& err) {
    if (line != ":set" && !line.starts_with(":set ")) return false;

    std::string_view rest(line);
    rest.remove_prefix(4);
    while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
        rest.remove_prefix(1);

    const std::size_t separator = rest.find_first_of(" \t");
    if (rest.empty() || separator == std::string_view::npos) {
        std::println(err, "用法: :set <参数> <值>（用 :config 查看全部参数）");
        return true;
    }

    std::string_view name = rest.substr(0, separator);
    std::string_view value = rest.substr(separator + 1);
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        value.remove_prefix(1);

    auto status = engine.setOption(name, value);
    if (!status) {
        std::println(err, "{}", status.error().message);
        return true;
    }

    const ConfigField* field = findConfigField(name);
    if (field != nullptr)
        std::println(out, "{} = {}", field->name, formatConfigField(engine.config(), *field));
    else
        std::println(out, "{} = {}", name, value);
    return true;
}

/// 返回 true 表示要退出 REPL。
bool handleCommand(const std::string& line, Engine& engine, std::ostream& out, std::ostream& err) {
    if (line == ":quit" || line == ":q") return true;

    if (line == ":help" || line == ":h") {
        printCommands(out);
        return false;
    }

    if (line == ":dump" || line == ":dump on") {
        engine.setDump(true);
        std::println(out, "dump = on");
        return false;
    }
    if (line == ":dump off") {
        engine.setDump(false);
        std::println(out, "dump = off");
        return false;
    }

    if (handleSet(line, engine, out, err)) return false;

    if (line == ":config") {
        printConfig(engine.config(), out);
        return false;
    }

    if (line == ":vars") {
        const auto& vars = engine.symbols().vars();
        if (vars.empty()) {
            std::println(out, "  (空)");
            return false;
        }
        std::vector<std::pair<std::string, int>> items(vars.begin(), vars.end());
        std::sort(items.begin(), items.end(),
                  [](const auto& a, const auto& b) { return a.second < b.second; });
        for (const auto& [name, slot] : items)
            std::println(out, "  {} -> slot {} = {}", name, slot,
                         engine.vm().variable(slot).to_literal(engine.config().outputDigits));
        return false;
    }

    if (line == ":consts") {
        const auto& pool = engine.symbols().constants();
        if (pool.empty()) {
            std::println(out, "  (空)");
            return false;
        }
        for (std::size_t i = 0; i < pool.size(); ++i)
            std::println(out, "  [{}] {}", i, pool[i].to_literal(engine.config().outputDigits));
        return false;
    }

    if (line == ":prec") {
        const Config& config = engine.config();
        std::println(out, "  MPFR 精度 = {} 位 ≈ {} 位十进制有效数字，输出最多 {} 位",
                     config.precision, config.digits(), config.digits());
        return false;
    }

    if (line == ":reset") {
        engine.reset();
        std::println(out, "已重置：变量与常量池已清空");
        return false;
    }

    std::println(err, "未知命令: {}（输入 :help 查看帮助）", line);
    return false;
}

} // namespace

void runRepl(Engine& engine, std::istream& in, std::ostream& out, std::ostream& err) {
    std::println(out, "sc REPL  (C++23 + GMP/MPFR, 精度 {} 位 ≈ {} 位十进制有效数字)",
                 engine.config().precision, engine.config().digits());
    std::println(out, "输入 :help 查看命令，:quit 退出；语句用 ';' 分隔");

    std::string pending;
    std::string line;
    while (true) {
        std::print(out, "{}", pending.empty() ? ">>> " : "... ");
        out.flush();

        if (!std::getline(in, line)) {
            if (!pending.empty()) std::println(err, "输入结束，丢弃未完成的输入");
            std::println(out, "");
            break;
        }

        // 命令在续行状态下也能用：先丢弃未完成的输入，再执行命令。
        if (line.starts_with(':')) {
            if (!pending.empty()) {
                pending.clear();
                std::println(err, "已丢弃未完成的输入");
            }
            if (handleCommand(trim(line), engine, out, err)) break;
            continue;
        }

        if (pending.empty() && trim(line).empty()) continue;

        if (!pending.empty()) pending.push_back('\n');
        pending += line;

        auto status = engine.runSource(pending, out, err);
        if (status) {
            pending.clear();
            continue;
        }
        if (status.error().incomplete) continue; // 语句还没写完

        std::println(err, "{}", formatError(status.error(), pending));
        pending.clear();
    }
}

} // namespace sc
