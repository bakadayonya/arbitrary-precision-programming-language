// 把词法/语法/编译/执行串起来，并持有会话状态（Config + SymbolTable + VM）。
//
// 关键语义：一次 runSource 是事务性的——失败时符号表（含常量池）和 VM 变量
// 都会完整回滚，所以 REPL 里"出错的一行"不会留下任何痕迹。
//
// 可调性：所有策略都在 config_ 里，setOption() 是 CLI --set 与 REPL :set
// 的共同入口，改完立刻对后续语句生效。
#pragma once

#include "compiler.hpp"
#include "config.hpp"
#include "error.hpp"
#include "symbols.hpp"
#include "vm.hpp"

#include <iosfwd>
#include <string_view>
#include <utility>

namespace sc {

class Engine {
public:
    Engine() { config_.normalize(); }
    explicit Engine(Config config) : config_(std::move(config)) { config_.normalize(); }

    [[nodiscard]] Status runSource(std::string_view source, std::ostream& out, std::ostream& err);

    void setDump(bool on) { dump_ = on; }
    [[nodiscard]] bool dump() const { return dump_; }

    [[nodiscard]] const Config& config() const { return config_; }

    /// 设置一个可调参数。名字未知或取值非法时返回 Error，且不改变当前配置。
    [[nodiscard]] Status setOption(std::string_view name, std::string_view value) {
        auto updated = applyConfigOption(config_, name, value);
        if (!updated) return std::unexpected(updated.error());
        config_ = *updated;
        return {};
    }

    [[nodiscard]] const SymbolTable& symbols() const { return symbols_; }
    [[nodiscard]] VM& vm() { return vm_; }

    void reset() {
        symbols_.reset();
        vm_.reset();
    }

private:
    Config config_;
    SymbolTable symbols_;
    Compiler compiler_; // 无状态，仅为方便复用
    VM vm_;
    bool dump_ = false;
};

} // namespace sc
