#include "cli.hpp"
#include "config.hpp"
#include "diagnostics.hpp"
#include "engine.hpp"
#include "repl.hpp"

#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <print>
#include <sstream>
#include <string>
#include <string_view>

namespace {

/// 退出码集中一处，避免散落的字面量。
enum class ExitCode { Ok = 0, Failure = 1, Usage = 2 };

constexpr int code(ExitCode value) { return static_cast<int>(value); }

int runOnce(sc::Engine& engine, std::string_view source) {
    if (auto status = engine.runSource(source, std::cout, std::cerr); !status) {
        std::println(std::cerr, "{}", sc::formatError(status.error(), source));
        return code(ExitCode::Failure);
    }
    return code(ExitCode::Ok);
}

int runFile(sc::Engine& engine, const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::println(std::cerr, "无法打开文件: {}", path);
        return code(ExitCode::Failure);
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return runOnce(engine, buffer.str());
}

} // namespace

// 函数级 try：本项目不用异常，但标准库（内存不足、格式化失败）与 GMP 的一些路径会抛。
// 顶层兜住，保证进程以明确退出码结束，而不是 std::terminate。
int main(int argc, char** argv) try {
    const std::string program = (argc > 0 && argv[0] != nullptr) ? argv[0] : "sc";

    auto options = sc::parseArgs(argc, argv);
    if (!options) {
        std::println(std::cerr, "错误: {}", options.error().message);
        std::println(std::cerr, "用 {} -h 查看用法", program);
        return code(ExitCode::Usage);
    }

    sc::Config defaults;
    defaults.normalize();
    if (options->showHelp) {
        sc::printHelp(program, defaults, std::cout);
        return code(ExitCode::Ok);
    }

    sc::Engine engine;

    // 先应用 --set：配置错误按用法错误处理，且不会带着半套配置去执行程序。
    for (const auto& [name, value] : options->settings) {
        if (auto status = engine.setOption(name, value); !status) {
            std::println(std::cerr, "错误: {}", status.error().message);
            std::println(std::cerr, "用 {} --list-config 查看全部参数", program);
            return code(ExitCode::Usage);
        }
    }

    if (options->listConfig) {
        sc::printConfig(engine.config(), std::cout);
        return code(ExitCode::Ok);
    }

    engine.setDump(options->dump);

    if (options->expression) return runOnce(engine, *options->expression);
    if (options->filename) return runFile(engine, *options->filename);

    sc::runRepl(engine, std::cin, std::cout, std::cerr);
    return code(ExitCode::Ok);
} catch (const std::exception& error) {
    // 处理器里刻意用 C stdio：这时再分配内存可能再次失败。
    std::fputs("内部错误: ", stderr);
    std::fputs(error.what(), stderr);
    std::fputc('\n', stderr);
    return code(ExitCode::Failure);
} catch (...) {
    std::fputs("内部错误: 未知异常\n", stderr);
    return code(ExitCode::Failure);
}
