# sc — C++23 + GMP/MPFR/utf8proc 的小型语言编译器

`sc` 是一个高精度的编译器 + 栈式虚拟机：词法分析 → Pratt 解析（表驱动 parselet）→ AST →
字节码 → 执行。整数用 GMP（任意精度），小数用 MPFR（默认 256 位二进制 ≈ 77 位十进制，
精度可调且按值携带）；值有整数/小数/字符串/布尔四种，六个比较运算符
（`== != < <= > >=`）返回布尔；语句有赋值/`print`/块，以及 `if`-`else`、`while`、
C 风格 `for` 与 `break`/`continue`；还有一整套**内建数学函数**
（`sqrt`/`sin`/`ln`/`pi` 到 `gcd`/`factorial`/`is_prime`，见
[内建数学函数](#内建数学函数)）；源码按 **UTF-8** 处理——标识符可以用中文等 Unicode 字符，
字符串字面量原生支持。

```console
$ make
$ ./sc -e "print 2**100"
1267650600228229401496703205376
$ ./sc -e "print 2.0 / 3"
0.66666666666666666666666666666666666666666666666666666666666666666666666666667
$ ./sc -e "print 2**100 > 2**99"
true
$ ./sc -e 'for (i = 0; i < 3; i = i + 1) { print i }'    # if/else、while、for 都有
0
1
2
$ ./sc -e "print sqrt(2)"                        # 内建数学函数
1.4142135623730950488016887242096980785696718753769480731766797379907324784621
$ ./sc -e "print gcd(12, 18); print factorial(20)"
6
2432902008176640000
$ ./sc --set precision=512 -e "print 1.0 / 3"    # 精度是运行期可调的
$ ./sc          # 交互式 REPL
```

## 构建与测试

依赖：g++ 14+（需要 C++23 的 `std::expected` / `<print>`）、GMP、MPFR、
utf8proc（Debian/Ubuntu 上 `sudo apt install libutf8proc-dev`）。
内建数学函数来自 [vendor/neo-math.h](vendor/neo-math.h)（GMP + MPFR 的 C++ 封装，
随仓库自带，不需要额外安装）；它用 `-isystem vendor` 引入，见 [Makefile](Makefile) 里的 `VENDOR`。
可选：`clang++-23` + `libc++-23-dev`（第二套工具链/标准库，`make clang`）、
`clang-tidy-23` + `clang-format-23`（静态分析与格式化，`make tidy` / `make format`）。

```console
make             # 生成 ./sc（GCC + libstdc++）
make test        # 运行 tests/run.sh（293 项回归测试）
make strict      # 用最严格的警告集重新构建，应当零警告
make ubsan       # UBSan 构建
make clang       # 用 clang + libc++ 构建出 ./sc-clang（不覆盖 ./sc）
make clang-test  # 上面两步 + 对 ./sc-clang 跑一遍测试
make tidy        # clang-tidy 静态分析（几分钟；配置见 .clang-tidy，应当零输出）
make format      # 按 .clang-format 格式化源码
make format-check# 检查格式偏差，有偏差即非零退出
make clean
```


## 代码风格与静态分析

* 风格由 [.clang-format](.clang-format) 固定，而且是**从现有代码反推**出来的，不是套 LLVM 默认：
  4 空格缩进、100 列、指针/引用靠左、`case` 标签缩进 4、单行 `if (...)`、
  不重排手写换行的注释、include 分组保持手工顺序。套 LLVM 默认会改 2900 处，这个配置下是 0 处。
* 少数手写对齐的表格（[src/opcode.hpp](src/opcode.hpp) 与 [src/token.hpp](src/token.hpp) 的
  `case ...: return "...";` 名表、[src/lexer.cpp](src/lexer.cpp) 的紧凑单行分支）
  用 `// clang-format off` / `// clang-format on` 包住。
  注意：这两行注释必须**恰好**是这句话——后面跟任何说明文字都会失配（clang-format 整行精确比较），
  说明要写在上一行。
* [.clang-tidy](.clang-tidy) 打开 clang-analyzer / bugprone / performance / portability 四个族，
  并逐条列出被忽略的误报及原因（`#pragma once`、无符号字面量的位运算、flag 枚举按位或、枚举基类型）。
  `make tidy` 应当零输出；它只喂 `.cpp`，头文件经由包含它的 TU 被分析。
* 第三方头放在 [vendor/](vendor/)，用 `-isystem vendor` 引入（见 Makefile 的 `VENDOR`）：
  它不参与本项目的警告集（否则 `make strict` 会被它报的 `-Wuseless-cast` 打破），
  也不在 `src/` 下，因此 `clang-format` 的 `SOURCES` 与 clang-tidy 的 `HeaderFilterRegex`
  都不会把它当成本项目代码。目前只有 [src/builtins.cpp](src/builtins.cpp) 包含它——
  这样第三方头的编译开销（约几秒）只落在一个 TU 上。

## 语言参考

| 语法 | 说明 |
| --- | --- |
| `;` | 语句分隔符（不是语句的一部分）；连续多个合法，最后一条可省略 |
| 表达式语句 | 求值并输出结果，例如 `2.0 / 3`（方便当计算器用） |
| `print <表达式>` | 输出表达式结果 |
| `名字 = <表达式>` | 赋值。赋值即定义；**读取未赋值的变量是编译错误** |
| `{ 语句* }` | 语句块，可空。块本身是一条语句 |
| `if (条件) 语句 [else 语句]` | 条件必须是 `bool`。`else if` 就是 else 里再放一个 if |
| `while (条件) 语句` | 条件为真时重复执行语句体 |
| `for (初始化; 条件; 后置) 语句` | C 风格；三段都可省略，`for (;;)` 是死循环 |
| `break` / `continue` | 只许出现在循环体内（编译期检查），否则报错 |
| `+ - * / %` | 四则运算与取余，`*` `/` `%` 优先级高于 `+` `-` |
| `<< >>` | 移位，优先级低于 `+` `-`；只接受整数，`<<` 受 `max-integer-bits` 约束 |
| `**` | 幂运算，优先级高于一元负号，**右结合**：`2**3**2 == 512`，`-2**2 == -4` |
| `< <= > >=` | 数值比较，结果是不在数值塔里的 `bool`。优先级比 `<<` 还低，同级左结合（见下） |
| `== !=` | 相等/不等，与 `<` 同级。数值塔内部可混比（`1 == 1.0` 为真）；还可比字符串与布尔（`1 == "1"` 报类型错误） |
| `( )` | 分组 |
| `名字(实参, ...)` | 内建数学函数调用，见[内建数学函数](#内建数学函数)。允许零个实参（`pi()`）；名字未知或实参个数不对是**编译错误**，且调用是最紧的后缀形式 |
| `"..."` | 字符串字面量，支持 `\n \t \r \" \\ \u{XXXX}` 转义；允许裸换行 |
| `true` `false` | 布尔字面量 |
| `+`（字符串） | 连接两个字符串；字符串与数值混算报类型错误 |

标识符：字母/下划线开头，其后可跟字母、数字、下划线。非 ASCII 按 Unicode 的
`ID_Start`/`ID_Continue` 判定，并在词法阶段做 **NFC 归一化**——`é`（U+00E9）与
`e` + U+0301 是同一个名字。关键字仍是 ASCII（`print`、`true`、`false`）。

比较运算规则：

* 六个运算符都产生 `bool`。`bool` 不是数值：`true + 1`、`-true`、`true ** 2` 全部是类型错误，
  它只参与 `==`/`!=` 和 `print`。
* `< <= > >=` 只接受数值，且整数与小数**混合时提升为小数**再比（所以 `1 < 1.5` 为真，
  而不是把 1.5 截断成整数）。字符串与布尔没有大小之分，报类型错误；
  字符串的 `==`/`!=` 是逐字节精确比较，没有容差。
* 六个比较运算符**同级、左结合**（和 C/C++ 一样，不是 Python 的链式比较）。
  于是 `1 < 2 < 3` 解析成 `(1 < 2) < 3`，运行期以"布尔值不支持排序比较"失败，
  消息会直接提示改写成 `(1 < 2) == (2 < 3)`；而 `1 == 2 == false` 是合法的
  （`(1 == 2) == false`），因为相等可以作用在布尔上。想要链式语义必须自己写括号。
* 跨种类相等（`1 == "1"`、`true == 1`、`"a" == true`）是类型错误，不做隐式转换。
* 与分数的关系：`==` 是精确比较，`0.1 + 0.2 == 0.3` 在本实现里为真，
  但那是因为 256 位二进制下两者舍入到同一个值；`1.0/3*3 == 1.0` 这类就未必成立。
* 优先级（由松到紧）：`< <= > >= == !=` < `<< >>` < `+ -` < `* / %` < `**` < 一元负号 < `f(...)` < `()`。
  于是 `1 << 2 < 5` 是 `(1 << 2) < 5`，`-sqrt(2)` 是 `-(sqrt(2))`，`sqrt(2)**2` 是 `(sqrt(2))**2`。

数值规则：

* 整数任意精度，但受 `max-integer-bits` 预算约束（默认 2^30 位，并硬性收敛到 GMP 上限内）。
* 小数默认 256 位二进制精度（`precision` 可调）；精度按值携带，运算取两者中较高者。
* 混合运算时整数自动提升为小数。
* 整数除法向零截断（`-7 / 2 == -3`）；要小数结果写 `1.0 / 3`。
* `%` 与 `/` 一样做整数提升、一样拒绝零，余数取**被除数**的符号
  （`-7 % 2 == -1`，与 `-7 / 2 == -3` 自洽）；小数取余走 `fmod` 语义（`7.5 % 2 == 1.5`）。
* `<< >>` 只接受整数（小数报类型错误），移位位数必须非负。
  `x >> n` 定义为向零截断的 `x / 2**n`，所以 `-7 >> 1 == -3`；移出全部有效位得 `0`。
  `x << n` 的结果位宽与整数幂共用 `max-integer-bits` 预算——
  先估位宽再调用 GMP，所以 `1 << 10^9` 是普通 `Error`，不是进程崩溃。
* 除零报错；默认也不产生 `inf`/`nan`（`allow-non-finite=true` 可放开）。
* 浮点输出默认按**每个值自身**的精度给出正确位数（256 位 → 最多 77 位有效数字），
  再多打印只会暴露二进制舍入噪声；`output-digits` 可覆盖为一个固定位数。
* 整数 `**` 的指数必须非负且不超过 `max-int-exponent`（默认 10^7），
  且结果位宽不超过 `max-integer-bits`；负数指数请用浮点底数（`2.0 ** -1`）。
  同一个 `max-int-exponent` 也是 `factorial`/`primorial`/`fibonacci` 这类
  "按 n 迭代"的内建函数的规模上限（CPU 预算），见[内建数学函数](#内建数学函数)。

字符串规则：

* 运算只有 `+`（连接）与 `==`/`!=`（逐字节精确比较，NaN 那种"不等于自己"的语义不存在）；
  与数值混算、`<`、`*`、`**`、一元负号一律报类型错误。
* 转义：`\n` `\t` `\r` `\"` `\\` `\u{XXXX}`；未知转义、代理区码点、超出 U+10FFFF 都报错。
* 字面量里允许裸换行，所以忘写右引号在 REPL 里表现为 `incomplete`，下一行补上即可
  （补上的换行是字符串内容的一部分）。
* 长度按**字节**计，受 `max-string-bytes`（默认 1 MiB）约束；
  常量池按内容去重，`-d`/`:consts`/`:vars` 里以带引号转义的形式显示。

布尔规则：

* 只有 `true`/`false` 两个字面量，`print` 输出裸文本 `true`/`false`，
  `-d`/`:consts`/`:vars` 里显示为不带引号的 `true`/`false`（与源码可回读一致）。
* 布尔字面量和其他字面量一样进**常量池**并按内容去重，所以 `print true; print true`
  只占一个常量条目。
* 布尔只支持 `==`/`!=`；算术、移位、幂、一元负号、排序比较全部报类型错误。
  它的主要用途是当 `if`/`while`/`for` 的条件——条件**必须**是布尔，
  没有"0 为假""空串为假"这类隐式转换，`if (1)` 是类型错误。
* 没有逻辑运算符（`and`/`or`/`not`），组合条件目前只能靠 `==`/`!=` 与括号，或嵌套 `if`。

控制流语句：

* 语句体可以是单条语句，也可以是块：`if (c) print 1; else print 2` 与
  `if (c) { print 1 } else { print 2 }` 都合法。块里最后一条语句的分号可以省略。
* **分号是语句分隔符，不是语句的一部分**：所以 `;` 不能出现在语句自己的"内部"。
  两个直接后果：`{ print 1;; }`（多余分号）合法，而 `{ print 1 } print 2`
  会因为块后面少了分隔符报错（写 `{ print 1 }; print 2`）。
* `if (c) print 1; else print 2` 里 then 分支后面的分号是可选的，
  所以 `x = 1; if (x > 0) print "pos"; else print "neg"` 按你期望的方式绑定。
* `break`/`continue` 在**编译期**检查是否位于循环内（不是运行期）。
  嵌套循环里只作用于最内层。
* 变量作用域是**扁平的**：块不引入新作用域，`for (i = 0; ...)` 里的 `i`
  在循环结束后依然可见（这也让"循环计数变量"不需要额外声明语法）。
* 条件的位置用于运行期报错：`if (1) { }` 指向 `1` 而不是 `if`。

```console
$ ./sc -e 'for (i = 0; i < 3; i = i + 1) { print i }'
0
1
2
$ ./sc -e 'n = 0; for (;;) { n = n + 1; if (n > 3) break }; print n'
4
```

命令行：

```console
sc -e "<程序>"            直接执行，例：sc -e "print 2**100"
sc -f <文件>              执行源文件
sc -d                     把字节码与常量池输出到 stderr（不污染程序输出）
sc --set <名>=<值>        设置一个可调参数（可重复）
sc --list-config          列出全部可调参数及当前值
sc -h                     帮助（含参数清单）
```

退出码：`0` 正常，`1` 编译/运行错误，`2` 命令行用法错误。

## 内建数学函数

调用语法是 `名字(实参, ...)`，名字与实参个数在**解析期**就对着 builtins 表查定，
所以未知名字、实参个数不对都是编译错误（报错位置指向函数名）。
实现来自 [vendor/neo-math.h](vendor/neo-math.h)：`mmath::*` 是 MPFR 的函数与常量，
`gext::*` 是 GMP 的整数数论函数。

```console
$ ./sc -e 'print sqrt(2)'
1.4142135623730950488016887242096980785696718753769480731766797379907324784621
$ ./sc -e 'print pi(); print gcd(12, 18); print factorial(20)'
3.1415926535897932384626433832795028841971693993751058209749445923078164062862
6
2432902008176640000
$ ./sc -e 'print is_prime(2**61 - 1); print powm(2, 10, 1000)'
true
24
```

**常量（0 个实参）**：`pi()` `e()` `euler_gamma()` `ln2()` `catalan()` `sqrt2()` `sqrt3()`
`phi()`（黄金比例）`zeta3()`。

**小数函数（返回值恒为小数；整数实参按 `precision` 提升）**：

| 类别 | 函数 |
| --- | --- |
| 幂 / 根 | `sqrt` `cbrt` `hypot` `atan2` |
| 指数 / 对数 | `exp` `exp2` `exp10` `expm1` `ln` `log2` `log10` `log1p` `log(x, base)` |
| 三角 | `sin` `cos` `tan` `asin` `acos` `atan` |
| 双曲 | `sinh` `cosh` `tanh` `asinh` `acosh` `atanh` |
| 特殊函数 | `gamma` `lgamma` `digamma` `zeta` `erf` `erfc` `li2` `eint` `beta` `agm` |
| 贝塞尔 | `j0` `j1` `y0` `y1` |
| 分解 | `frac` `fmod` `remainder` `copysign` `dim` |

`log` 刻意只提供二元形式（第二个实参是**底数**）：自然对数写 `ln(x)`、常用对数写 `log10(x)`，
免得踩到"C 的 `log` 与中学的 `log` 不是一回事"这个老坑。

**取整**：`floor` `ceil` `trunc` `round` 对小数返回小数，对**整数是恒等**——
不会为了取整把大整数提升成小数而白白丢精度。

**保持值种类**：`abs` `min` `max` 在两边都是整数时走 GMP，
所以 `abs(-2**100)`、`min(2**100, 2**101)` 都是精确的。

**整数函数（只接受整数实参，小数报类型错误）**：
`gcd` `lcm` `factorial` `binomial` `fibonacci` `lucnum` `primorial` `isqrt` `powm` `next_prime`，
以及返回布尔的 `is_prime`。

数值策略（调用方不需要额外处理）：

* **精度**：小数实参保留自身精度，二元函数取两者较高者；常量按 `precision` 求值，
  所以 `--set precision=512 -e 'print pi()'` 给出 512 位的结果。
* **舍入**：一律 `MPFR_RNDN`，与本项目既有运算一致。
* **定义域 / 溢出**：`sqrt(-1)`、`ln(0)`、`exp(1e9)` 默认报错
  （`数学函数定义域错误（结果是 nan）` / `数学函数结果溢出（结果是 inf）`）；
  `allow-non-finite=true` 时改为返回 `nan`/`inf`。
* **预算**：整数结果走 `max-integer-bits`；`factorial` 这类还要先按 `max-int-exponent`
  卡住规模——**先估算再调用 GMP**，所以 `factorial(10^9)` 是普通 `Error`，不是把内存吃光。
* **异常**：数学库用异常报错、本项目用 `Result`，[src/builtins.cpp](src/builtins.cpp)
  在入口处收口，C++ 异常不会穿过 VM 主循环。

加一个内建函数 = 在 [src/builtins.cpp](src/builtins.cpp) 的 `BUILTINS` 表里加一行
（签名不同时再写一个求值适配器）。表有"`(名字, 实参个数)` 唯一"和"参数个数非负"两道
编译期检查，`-d` 反汇编会把指令的操作数显示成函数名而不是下标。

## 可调参数

所有策略参数集中在一处、运行期可改（CLI 用 `--set`，REPL 用 `:set`），
改完立刻对后续语句生效：

```console
$ ./sc --list-config
  precision          = 256            # 新建浮点值的二进制精度
  output-digits      = 0              # 输出有效数字位数；0 = 按每个值自身的精度
  allow-non-finite   = false          # 是否允许 inf/nan（默认拒绝）
  max-integer-bits   = 1073741824     # 单个整数的位宽预算（受 GMP 硬上限约束）
  max-int-exponent   = 10000000       # 整数幂与大整数内建函数的规模上限（CPU 预算）
  max-string-bytes   = 1048576        # 单个字符串的字节上限
  max-parse-depth    = 256            # 语法嵌套深度上限
  max-nodes          = 4096           # 单条语句的表达式节点预算
  max-expr-depth     = 4096           # 代码生成的递归上限
  max-stack          = 1048576        # VM 操作数栈上限
$ ./sc --set precision=128 -e "print 1.0 / 3"
0.33333333333333333333333333333333333333
```

精度是**按值携带**的：`--set precision` 只影响此后新建的浮点值，已有值保留自己的精度，
二元运算取两者中更高的精度。因此提高精度不会让旧值打印出二进制舍入噪声
（输出位数默认由每个值自身的精度决定，`output-digits` 非 0 时才统一定长）。

## REPL

```console
:help             帮助
:quit, :q         退出
:dump on|off      开关字节码输出（写 stderr）
:vars             列出变量、槽位与当前值
:consts           列出常量池
:prec             显示 MPFR 精度与输出精度
:config           列出全部可调参数及当前值
:set <名> <值>    设置一个可调参数，例如 :set precision 512
:reset            清空所有变量
```

* 语句写不完整（例如 `print 1 +`）时提示符变成 `...`，继续输入即可；判定依据是
  错误里的 `incomplete` 标记，而不是猜"最后一个字符是不是分号"。
* 出错的一行是**事务性**的：新定义的变量不会留下，已发生的赋值也会回滚。
* 续行状态下依然可以用 `:` 开头的命令（会先丢弃未完成的输入）。

## 模块结构

| 文件 | 职责 |
| --- | --- |
| [src/error.hpp](src/error.hpp) | `Error`（含 `incomplete` 标记）与 `Result`/`Status` 别名 |
| [src/config.hpp](src/config.hpp), [src/config.cpp](src/config.cpp) | **全部可调参数 + 字段表**（`--set`/`:set`/`--list-config` 的数据源） |
| [src/unicode.hpp](src/unicode.hpp), [src/unicode.cpp](src/unicode.cpp) | **UTF-8/Unicode**：解码与合法性校验、显示宽度、空白、标识符分类、NFC 归一化（唯一 include utf8proc 的地方） |
| [src/diagnostics.hpp](src/diagnostics.hpp), [src/diagnostics.cpp](src/diagnostics.cpp) | 把错误渲染成"行列 + 源码行 + 插入符"（码点列 + 显示宽度插入符） |
| [src/number.hpp](src/number.hpp), [src/number.cpp](src/number.cpp) | MPFR 的 RAII 封装，精度按值携带；比较原语（`compare`/`equals`） |
| [src/value.hpp](src/value.hpp), [src/value.cpp](src/value.cpp) | 值（整数/小数/字符串/布尔）、数值提升、算术与比较语义、各类预算与策略 |
| [src/token.hpp](src/token.hpp), [src/lexer.hpp](src/lexer.hpp), [src/lexer.cpp](src/lexer.cpp) | 词法分析（UTF-8 解码、字符串字面量与转义、标识符 NFC、关键字表） |
| [src/opcode.hpp](src/opcode.hpp) | 指令集 + 指令名（漏登记即编译失败） |
| [src/operators.hpp](src/operators.hpp) | **运算符表**：优先级/结合性/指令/求值函数的单一来源 |
| [src/ast.hpp](src/ast.hpp), [src/ast.cpp](src/ast.cpp) | AST 节点（表达式用 `unique_ptr` 串联，语句体是 `vector<Stmt>`） |
| [src/parser.hpp](src/parser.hpp), [src/parser.cpp](src/parser.cpp) | **Pratt 解析器**：前缀/中缀/语句三层 parselet 分派 + 语句列表驱动（分号与深度预算） |
| [src/bytecode.hpp](src/bytecode.hpp), [src/bytecode.cpp](src/bytecode.cpp) | `Instruction`、`CompilationUnit`、反汇编 |
| [src/symbols.hpp](src/symbols.hpp), [src/symbols.cpp](src/symbols.cpp) | **会话状态**：变量表 + 常量池，可完整快照/回滚 |
| [src/compiler.hpp](src/compiler.hpp), [src/compiler.cpp](src/compiler.cpp) | AST → 字节码（无状态，产物是 `CompilationUnit`） |
| [src/vm.hpp](src/vm.hpp), [src/vm.cpp](src/vm.cpp) | 栈式虚拟机（核心指令 + 相对跳转 + 表驱动求值） |
| [src/engine.hpp](src/engine.hpp), [src/engine.cpp](src/engine.cpp) | 串联流水线、持有 Config、事务式执行 |
| [src/cli.hpp](src/cli.hpp), [src/cli.cpp](src/cli.cpp) | 命令行解析与帮助（`--set` 由字段表驱动） |
| [src/repl.hpp](src/repl.hpp), [src/repl.cpp](src/repl.cpp) | REPL |
| [src/main.cpp](src/main.cpp) | 入口与退出码 |
| [tests/run.sh](tests/run.sh) | 回归测试 |
| [legacy/ab.cpp](legacy/ab.cpp) | 重构前的单文件版本，仅作参考 |

## 如何扩展

四条已经被编译期断言保护起来的扩展路径：

* **加一个可调参数**（1 个文件）：在 [src/config.hpp](src/config.hpp) 的 `Config` 里加字段、
  在 `CONFIG_FIELDS` 里加一行。`--set`、`--list-config`、`--help`、`:set`、`:config`
  会自动包含它。
* **加一个运算符**（实测 6 个文件 / 20 行）：以新增 `%` 取余运算符为对照实验，
  改动是 [src/token.hpp](src/token.hpp)（枚举 + 名字，2 行）、[src/lexer.cpp](src/lexer.cpp)（1 行）、
  [src/opcode.hpp](src/opcode.hpp)（枚举 + 名字 + 归属，3 行）、
  [src/value.hpp](src/value.hpp)/[src/value.cpp](src/value.cpp)（求值函数，11 行）、
  [src/operators.hpp](src/operators.hpp)（表里加一行，3 行）。
  **解析器、代码生成、VM、CLI、REPL 一行都不用改**——优先级、结合性、指令、求值全部从表推导：
  前缀运算符由 `UNARY_OPS` 驱动，中缀运算符由 `BINARY_OPS` 驱动（[src/parser.cpp](src/parser.cpp)
  的中缀环直接扫这张表）。结合性写成显式的 `(leftBp, rightBp)` 对：
  左结合 = `(2p, 2p+1)`，右结合 = `(2p+1, 2p)`，要非结合只需换下界。
  （若该运算符对小数也有定义，还需要在 `Mpfr` 上加一个原语，多一个文件。）
  漏登记会在编译期被拦住：忘了 `opName` → `allOpNamesDefined()` 失败；
  忘了登记进任何一张表/登记两次 → `opcodeCoverageOk()` 失败；忘了 token 名 → `allTokenNamesDefined()` 失败。
* **加一条语句**（`if`/`while`/`for`/`break`/`continue` 就是这条路径做出来的）：
  在 [src/parser.hpp](src/parser.hpp) 的 `STMT_PARSELETS` 里加一行 +
  写一个 `Result<Stmt> parseXxxStatement(const Token&)`；
  语句节点加进 [src/ast.hpp](src/ast.hpp) 的 `StmtVariant`；
  再在 [src/compiler.cpp](src/compiler.cpp) 的 `genStmt` 补一个分支。
  **漏了编译期就会报错**（`static_assert(detail::alwaysFalse<T>)`），不会静默生成空代码。
  词法层需要的话再加一个关键字 token。三条容易踩的坑：
  1. **分号不由语句消费**——`parseXxxStatement` 在分号前停下，列表层统一收尾。
     否则 `if (c) print 1; else print 2` 里的 else 会被 then 分支吃掉。
  2. **循环体要登记 break/continue 的跳转**——`Codegen` 里有 `breakStack`/`continueStack`，
     忘了压栈就会让 `break` 报"只能用在循环里"。
  3. **语句也会递归**——新语句如果带语句体，别在子解析里重置深度计数器（见上文段错误那条）。
* **加一种值类型**（1~2 个文件）：在 `Value` 的 variant 里加备选、在 `ValueKind` 里加种类、
  在 `KIND_PREFIX` 里加常量池前缀。三处数量不一致会被 `static_assert` 拦下；
  运算分派是 `switch (ValueKind)` 且没有 `default`，漏分支会被 `-Wswitch` 拦下。
  实测（bool 这一次，5 个文件）：[src/value.hpp](src/value.hpp)/[src/value.cpp](src/value.cpp)
  （variant、种类、三个 visit 链、算术入口的类型检查）、[src/token.hpp](src/token.hpp) +
  [src/lexer.cpp](src/lexer.cpp)（`true`/`false` 关键字）、[src/ast.hpp](src/ast.hpp)/[src/ast.cpp](src/ast.cpp)
  （字面量节点）、[src/parser.hpp](src/parser.hpp)（前缀 parselet 表加一行）、
  [src/compiler.cpp](src/compiler.cpp)（`genExpr` 加一个分支）。
  编译期断言能兜住"枚举/前缀/分支数量"这类机械错误，但兜不住**语义**位：
  把 bool 放进 `promote()` 的数值塔会静默给出 `true + 1 == 2`，
  这类检查只能靠 `Value::isNumber()` 这样的显式入口守卫写出来。

## 设计说明与已知边界

* **错误处理**：不使用异常，全部用 `std::expected<T, Error>` 传播。错误带字节偏移，
  输出时换算成行列并画出源码行与插入符；运行期错误的定位来自指令上附带的位置
  （AST 的运算符/语句位置），例如 `print 1 / 0` 会指向 `/`。手写字节码没有位置时，
  退化为提示出错指令下标，便于对照 `-d` 的输出。

  ```console
  $ ./sc -e 'print 1 / 0'
  错误: 除零错误
    --> 第 1 行 第 9 列
    1 | print 1 / 0
      |         ^
  ```
* **规模防护（可调）**：解析器限制语法嵌套（`max-parse-depth`，默认 256）与单条语句的表达式
  节点数（`max-nodes`，默认 4096，最坏形成约 2048 层左倾树）。解析、代码生成与 AST 析构
  都是递归的，所以必须有此上限；超限时报错而不是段错误。`1+1+1+...` 超过约 2000 项会被拒绝。
  **表达式深度与语句深度分开计数**，因为两条递归路径不同：`print (((1)))` 走 `parseExpr`，
  `{ { { ... } } }` 走 `parseStatement`。两个计数器都受 `max-parse-depth` 约束，
  但互不占用——否则 `if (c) { print (((1))) }` 会被外层语句层数挤掉括号预算而误报。
  另一个更隐蔽的坑：深度计数器只能在 `parse()` 里按"顶层语句"重置一次。
  曾经它写在语句列表循环里，于是每进入一层块就把深度清零——嵌套深度永远涨不上去，
  5 万层嵌套块会在解析期打穿 C++ 调用栈，以段错误(139)结束而不是报"嵌套过深"。
  现在 `gen_blocks`/`gen_ifs` 两条回归测试专门盯这一点。
  `Config::normalize()` 保证 `max-expr-depth` 不会小于节点预算隐含的深度——
  以前这两个上限散落在两个匿名命名空间里，单独调大其中一个会静默破坏"AST 深度安全"这个不变量。
* **数值预算（可调）**：整数幂除了指数上限（`max-int-exponent`，CPU 预算）还要检查**结果位宽**
  （`max-integer-bits`，默认 2^30 位）。只限指数是不够的：底数很大时（例如先算出 `2**10000000`），
  一个"合法"的指数就能让结果超过 GMP 的**硬上限**——`mpz_t` 的 limb 计数是 32 位 `int`
  （见 `gmp.h` 的 `__mpz_struct`），超过约 2^31 limb（≈2^37 位）就不再是"内存不够"，
  而是未定义行为。重构前这条路径会以 SIGFPE(136) 或 SIGABRT(134) 结束进程；
  现在在调用 GMP 之前就被拒绝，返回普通 `Error`。`max-integer-bits` 会被收敛到
  `GMP_HARD_MAX_BITS` 以内。内建数学函数（[src/builtins.cpp](src/builtins.cpp)）走同一套预算：
  整数结果交给 `valueFromInt` 收口，`factorial`/`primorial`/`binomial` 这类还会先算
  位宽上界（`log2(n!) <= n·log2(n)`、`θ(n) < 2n`、`C(n,k) <= 2^n`）再决定要不要调用 GMP。
* **数值域策略**：默认拒绝 `inf`/`nan`（字面量溢出、运算溢出都报错），
  `allow-non-finite=true` 可以放开。以前 `print 1e999999999` 会静默打印 `inf`，
  与"不产生 inf"这句文档相矛盾。
* **事务边界**：一次 `runSource` 是完整事务——符号表（变量 + **常量池**）和 VM 变量一起回滚。
  重构前 `Snapshot` 不覆盖常量池，失败的一行会在 `:consts` 里留下痕迹。
* **VM 不信任字节码**：常量下标、变量下标、栈高度、栈上限（`max-stack`）全部检查，
  异常字节码只会得到 `Error`。
* **指令与运算符表的一致性**：每条指令必须恰好属于"VM 核心"或"运算符表"之一，
  且必须有名字；两条不变量都是 `static_assert`，漏做会编译失败。
* **常量池去重**：整数用十进制、小数用 `%Ra` 十六进制精确文本作为键，布尔用 `b:true`/`b:false`，
  `print 1+1` 只占一个常量、`print true; print true` 也只占一个。
* **诊断位置**：位置是字节偏移换算的行列，制表符/宽字符不会做视觉对齐修正。
* **UTF-8 与 Unicode**：源码必须是合法 UTF-8（overlong、代理区、>U+10FFFF、截断的序列
  都会被拒绝并给出字节偏移）。标识符按 Unicode 类别近似 `ID_Start`/`ID_Continue` 判定，
  并在词法阶段做 NFC 归一化；Unicode 空白（含全角空格 U+3000）等价于空格。
  诊断消息里报的是**码点列**，插入符按**显示宽度**缩进（东亚宽字符算 2 列），
  因此含中文的源码也能对齐。非法字符一律以 `U+XXXX` 形式呈现——
  不会把半个字符的字节塞进消息（那会产生非法 UTF-8 输出，旧版本就有这个 bug）。
* **已知局限**：语句/表达式二分，赋值只能是语句（`x = y = 1` 不合法）；没有块作用域
  （变量表是扁平的、槽位只增不复用，`for` 的计数变量循环后仍可见）；
  VM 没有调用栈，因此加函数/闭包是架构级改造（跳转指令只用 `pc` 相对偏移，够 `if`/循环用）；
  诊断只有一个字节偏移而不是 span，也没有错误恢复（遇到第一个错误就停）。
  字符串长度按字节而非码点/字素，且只能 `==`/`!=`（没有排序比较、没有索引）；
  插入符对组合字符与 emoji ZWJ 字素簇不完美（按码点宽度求和），制表符仍按 1 列计。
* 没有逻辑运算符（`and`/`or`/`not`，短路需要按跳转做短路求值）、没有函数、
  不支持注释、没有 `switch`/`do-while`、没有 `for x in ...`——这些都还没有实现。
* 比较运算符同级左结合（不是 Python 的链式比较）：`1 < 2 < 3` 解析成 `(1 < 2) < 3`
  并在运行期因"排序比较作用在布尔上"报错，消息里给出改写方式。
