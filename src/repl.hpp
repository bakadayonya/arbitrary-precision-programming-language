#pragma once

#include "engine.hpp"

#include <iosfwd>

namespace sc {

/// 交互式 REPL。遇到 incomplete 错误时继续读入下一行，其余错误立即报告。
void runRepl(Engine& engine, std::istream& in, std::ostream& out, std::ostream& err);

} // namespace sc
