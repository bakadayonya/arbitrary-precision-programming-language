#include "engine.hpp"

#include "bytecode.hpp"
#include "lexer.hpp"
#include "parser.hpp"

#include <cstddef>
#include <ostream>
#include <print>
#include <utility>

namespace sc {

Status Engine::runSource(std::string_view source, std::ostream& out, std::ostream& err) {
    Lexer lexer(source, config_);
    auto tokens = lexer.tokenize();
    if (!tokens) return std::unexpected(tokens.error());

    Parser parser(std::move(*tokens), config_);
    auto ast = parser.parse();
    if (!ast) return std::unexpected(ast.error());

    // 事务边界：编译和执行都从这两个快照开始；任何一步失败都完整回滚。
    const SymbolTable::Snapshot symbolsBefore = symbols_.snapshot();
    const VM::Snapshot vmBefore = vm_.snapshot();

    auto unit = compiler_.compile(*ast, symbols_, config_);
    if (!unit) {
        symbols_.restore(symbolsBefore); // 编译期失败也要回滚常量池/变量表
        return std::unexpected(unit.error());
    }

    if (dump_) {
        std::println(err, "--- 字节码 ---");
        dumpBytecode(err, *unit, config_.outputDigits);
        std::println(err, "--- 常量池 ---");
        for (std::size_t i = 0; i < unit->constants.size(); ++i)
            std::println(err, "  [{}] {}", i, unit->constants[i].to_literal(config_.outputDigits));
        std::println(err, "----------------");
    }

    if (auto status = vm_.run(*unit, config_, out); !status) {
        symbols_.restore(symbolsBefore);
        vm_.restore(vmBefore);
        return std::unexpected(status.error());
    }
    return {};
}

} // namespace sc
