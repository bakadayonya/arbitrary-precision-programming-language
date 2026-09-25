#include "symbols.hpp"

#include <format>
#include <utility>

namespace sc {

Result<int> SymbolTable::lookup(const std::string& name, int pos) const {
    const auto it = varTable_.find(name);
    if (it == varTable_.end()) return fail(std::format("变量 '{}' 未定义", name), pos);
    return it->second;
}

int SymbolTable::define(const std::string& name) {
    if (const auto it = varTable_.find(name); it != varTable_.end()) return it->second;
    const int index = nextVarIndex_++;
    varTable_[name] = index;
    return index;
}

int SymbolTable::intern(const Value& value) {
    std::string key = value.key();
    if (const auto it = constIndex_.find(key); it != constIndex_.end()) return it->second;

    const int index = static_cast<int>(constPool_.size());
    constPool_.push_back(value);
    constIndex_.emplace(std::move(key), index);
    return index;
}

void SymbolTable::reset() {
    varTable_.clear();
    nextVarIndex_ = 0;
    constPool_.clear();
    constIndex_.clear();
}

} // namespace sc
