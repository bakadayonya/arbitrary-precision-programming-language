#include "cli.hpp"

#include <cstddef>
#include <format>
#include <ostream>
#include <print>
#include <string>

namespace sc {

Result<Options> parseArgs(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            options.showHelp = true;
        } else if (arg == "--list-config") {
            options.listConfig = true;
        } else if (arg == "-d" || arg == "--dump") {
            options.dump = true;
        } else if (arg == "-e") {
            if (i + 1 >= argc) return fail("-e 需要一个参数");
            options.expression = std::string(argv[++i]);
        } else if (arg == "-f") {
            if (i + 1 >= argc) return fail("-f 需要一个参数");
            options.filename = std::string(argv[++i]);
        } else if (arg == "--set") {
            if (i + 1 >= argc) return fail("--set 需要 name=value 参数");
            const std::string_view assignment = argv[++i];
            const std::size_t separator = assignment.find('=');
            if (separator == std::string_view::npos || separator == 0)
                return fail(std::format("--set 需要 name=value 形式: {}", assignment));
            options.settings.emplace_back(std::string(assignment.substr(0, separator)),
                                          std::string(assignment.substr(separator + 1)));
        } else {
            return fail(std::format("未知选项: {}", arg));
        }
    }
    if (options.expression && options.filename) return fail("不能同时使用 -e 和 -f");
    return options;
}

void printHelp(std::string_view program, const Config& defaults, std::ostream& os) {
    std::println(
        os,
        "用法: {} [选项]\n"
        "\n"
        "选项:\n"
        "  -e <expr>              直接执行一段源程序（结尾可省略分号）\n"
        "  -f <file>              执行源文件\n"
        "  -d, --dump             把字节码与常量池输出到 stderr\n"
        "  --set <name>=<value>   设置一个可调参数（可重复）\n"
        "  --list-config          列出全部可调参数及其当前值\n"
        "  -h, --help             显示本帮助\n"
        "\n"
        "不带 -e / -f 时进入交互式 REPL。\n"
        "\n"
        "语法:\n"
        "  语句           用 ';' 分隔，最后一条可省略分号；单独的分号是空语句\n"
        "  表达式语句     求值并输出结果，例如 2.0 / 3\n"
        "  print <表达式> 输出表达式结果\n"
        "  变量           赋值即定义；读取未定义的变量会报错\n"
        "  运算符         + - * / % << >> ** ( )，其中 ** 是幂运算（右结合）\n"
        "                 << >> 只接受整数；% 与 / 一样会做整数提升\n"
        "\n"
        "数值:\n"
        "  整数           任意精度 (GMP)，如 123456789012345678901234567890\n"
        "  小数           高精度浮点 (MPFR, 默认 {} 位 ≈ {} 位十进制)，如 3.14159 1.5e10 .25 3.\n"
        "  混合运算       整数自动提升为小数\n"
        "  整数除法       向零截断；要小数结果请写 1.0 / 3\n"
        "  除零           报错，不产生 inf\n"
        "  输出精度       默认最多 {} 位有效数字\n"
        "\n"
        "可调参数（--set / REPL 的 :set，详见 --list-config）:\n",
        program, defaults.precision, defaults.digits(), defaults.digits());
    printConfigHelp(os);
    std::println(os,
                 "\n"
                 "示例:\n"
                 "  {} -e \"print 2.0 / 3\"\n"
                 "  {} -e \"print 2**100\"\n"
                 "  {} -e \"x = 2**64; print x * x\"\n"
                 "  {} --set precision=512 -e \"print 1.0 / 3\"\n"
                 "  {} -f prog.sc -d\n"
                 "\n"
                 "退出码: 0 正常，1 编译/运行错误，2 命令行用法错误\n",
                 program, program, program, program, program);
}

} // namespace sc
