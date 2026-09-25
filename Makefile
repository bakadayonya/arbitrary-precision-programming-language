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

.PHONY: all clean test strict ubsan clang clang-test

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

clean:
	rm -rf build $(BIN)

-include $(DEPS)
