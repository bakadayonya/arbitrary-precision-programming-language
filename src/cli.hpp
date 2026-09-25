// 命令行解析与帮助。
//
// 可调参数不在这里逐个列举：--set 直接把名字/值交给 Config 的字段表，
// 因此新增参数不需要改本文件（只有 --set/--list-config 这两个入口是固定的）。
#pragma once

#include "config.hpp"
#include "error.hpp"

#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sc {

struct Options {
    bool showHelp = false;
    bool listConfig = false;
    bool dump = false;
    std::optional<std::string> expression;
    std::optional<std::string> filename;
    std::vector<std::pair<std::string, std::string>> settings; // --set name=value
};

[[nodiscard]] Result<Options> parseArgs(int argc, char** argv);
void printHelp(std::string_view program, const Config& defaults, std::ostream& os);

} // namespace sc
