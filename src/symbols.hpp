// 会话级符号状态：变量名 → 槽位，以及去重的常量池。
//
// 这份状态跨多次编译保留（REPL 需要），因此它必须能被完整地快照/回滚：
// 「出错的一行不留痕迹」这句承诺的边界就是 Snapshot 覆盖的字段。
// 以前常量池不在快照里，失败的编译会留下常量；现在整个类只有这一处状态，
// 回滚是完整的。
#pragma once

#include "error.hpp"
#include "value.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace sc {

class SymbolTable {
public:
    struct Snapshot {
        std::unordered_map<std::string, int> vars;
        int nextVar = 0;
        std::vector<Value> constants;
        std::unordered_map<std::string, int> constIndex;
    };

    /// 读取变量槽；未定义返回 Error（位置用于报错）。
    [[nodiscard]] Result<int> lookup(const std::string& name, int pos) const;

    /// 定义变量；已存在则复用槽位。
    int define(const std::string& name);

    /// 常量池去重：命中则复用已有下标。
    int intern(const Value& value);

    [[nodiscard]] const std::unordered_map<std::string, int>& vars() const { return varTable_; }
    [[nodiscard]] const std::vector<Value>& constants() const { return constPool_; }
    [[nodiscard]] int numVars() const { return nextVarIndex_; }

    [[nodiscard]] Snapshot snapshot() const {
        return Snapshot{varTable_, nextVarIndex_, constPool_, constIndex_};
    }
    void restore(const Snapshot& snapshot) {
        varTable_ = snapshot.vars;
        nextVarIndex_ = snapshot.nextVar;
        constPool_ = snapshot.constants;
        constIndex_ = snapshot.constIndex;
    }
    void reset();

private:
    std::unordered_map<std::string, int> varTable_;
    int nextVarIndex_ = 0;
    std::vector<Value> constPool_;
    std::unordered_map<std::string, int> constIndex_;
};

} // namespace sc
