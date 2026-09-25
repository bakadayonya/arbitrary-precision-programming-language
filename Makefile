# sc — C++23 + GMP/MPFR 玩具语言编译器
CXX      ?= g++
CXXFLAGS ?= -std=c++23 -O2 -Wall -Wextra -Wpedantic
LDLIBS   ?= -lgmpxx -lgmp -lmpfr -lutf8proc

BIN      := sc
SRCDIR   := src
# 编译选项不同就用不同的对象目录，避免在 release/strict/ubsan 之间切换时
# 把上一套选项编译出的 .o 链进来。
BUILDDIR := build/$(shell printf '%s' '$(CXXFLAGS)' | cksum | cut -d' ' -f1)

SRCS := $(wildcard $(SRCDIR)/*.cpp)
OBJS := $(patsubst $(SRCDIR)/%.cpp,$(BUILDDIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

.PHONY: all clean test strict ubsan clang clang-test tidy format format-check

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@

$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILDDIR):
	mkdir -p $@

# 回归测试（见 tests/run.sh）
test: $(BIN)
	bash tests/run.sh ./$(BIN)

# 最严格的警告集，应当零警告
strict:
	$(MAKE) CXXFLAGS="-std=c++23 -O2 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast -Wuseless-cast"

# UBSan 构建
ubsan:
	$(MAKE) CXXFLAGS="-std=c++23 -O1 -g -fsanitize=undefined -fno-sanitize-recover=all -Wall -Wextra -Wpedantic"

# 第二套工具链 + 第二套标准库（clang + libc++），产物是 sc-clang，不覆盖 ./sc。
# 注意：-Wuseless-cast 是 GCC 专有，这里不能用；依赖 libc++-<版本>-dev。
CLANGXX   ?= clang++-23
CLANGFLAGS ?= -std=c++23 -O2 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast -stdlib=libc++
clang:
	$(MAKE) CXX="$(CLANGXX)" CXXFLAGS="$(CLANGFLAGS)" BIN=sc-clang

clang-test: clang
	bash tests/run.sh ./sc-clang

# 静态分析与格式化。检查项与忽略项见 .clang-tidy，风格见 .clang-format。
CLANG_TIDY   ?= clang-tidy-23
CLANG_FORMAT ?= clang-format-23
SOURCES      := $(SRCS) $(wildcard $(SRCDIR)/*.hpp)

# 静态分析：只报告本项目的发现（系统头噪音按 .clang-tidy 的 HeaderFilterRegex 过滤）。
# 只喂 .cpp：头文件会经由包含它的 TU 被分析，单独当 TU 跑只会白白翻倍耗时。
# 带 clang-analyzer 全量跑一轮大约要几分钟。
tidy:
	$(CLANG_TIDY) -quiet $(SRCS) -- -std=c++23 -Isrc

# 应用项目风格
format:
	$(CLANG_FORMAT) -i $(SOURCES)

# 检查格式：有偏差就非零退出
format-check:
	$(CLANG_FORMAT) --dry-run --Werror $(SOURCES)

clean:
	rm -rf build $(BIN)

-include $(DEPS)
