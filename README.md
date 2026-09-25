# sc — C++23 + GMP/MPFR/utf8proc 的小型语言编译器

`sc` 是一个玩具语言的编译器 + 栈式虚拟机：词法分析 → Pratt 解析（表驱动 parselet）→ AST →
字节码 → 执行。整数用 GMP（任意精度），小数用 MPFR（默认 256 位二进制 ≈ 77 位十进制，
精度可调且按值携带）；源码按 **UTF-8** 处理——标识符可以用中文等 Unicode 字符，
字符串字面量原生支持。

```console
$ make
$ ./sc -e "print 2**100"
1267650600228229401496703205376
$ ./sc -e "print 2.0 / 3"
0.66666666666666666666666666666666666666666666666666666666666666666666666666667
$ ./sc --set precision=512 -e "print 1.0 / 3"    # 精度是运行期可调的
$ ./sc          # 交互式 REPL
```

## 构建与测试

依赖：g++ 14+（需要 C++23 的 `std::expected` / `<print>`）、GMP、MPFR、
utf8proc（Debian/Ubuntu 上 `sudo apt install libutf8proc-dev`）。
可选：`clang++-23` + `libc++-23-dev`，用于第二套工具链/标准库的交叉验证（`make clang`）。

```console
make             # 生成 ./sc（GCC + libstdc++）
make test        # 运行 tests/run.sh（146 项回归测试）
make strict      # 用最严格的警告集重新构建，应当零警告
make ubsan       # UBSan 构建
make clang       # 用 clang + libc++ 构建出 ./sc-clang（不覆盖 ./sc）
make clang-test  # 上面两步 + 对 ./sc-clang 跑一遍测试
make clean
```

## 语言参考

| 语法 | 说明 |
| --- | --- |
| `;` | 语句分隔符；最后一条语句可省略分号；单独的分号是空语句 |
| 表达式语句 | 求值并输出结果，例如 `2.0 / 3`（方便当计算器用） |
| `print <表达式>` | 输出表达式结果 |
| `名字 = <表达式>` | 赋值。赋值即定义；**读取未赋值的变量是编译错误** |
| `+ - * / %` | 四则运算与取余，`*` `/` `%` 优先级高于 `+` `-` |
| `<< >>` | 移位，优先级低于 `+` `-`；只接受整数，`<<` 受 `max-integer-bits` 约束 |
| `**` | 幂运算，优先级高于一元负号，**右结合**：`2**3**2 == 512`，`-2**2 == -4` |
| `( )` | 分组 |
| `"..."` | 字符串字面量，支持 `\n \t \r \" \\ \u{XXXX}` 转义；允许裸换行 |
| `+`（字符串） | 连接两个字符串；字符串与数值混算报类型错误 |

标识符：字母/下划线开头，其后可跟字母、数字、下划线。非 ASCII 按 Unicode 的
`ID_Start`/`ID_Continue` 判定，并在词法阶段做 **NFC 归一化**——`é`（U+00E9）与
`e` + U+0301 是同一个名字。关键字仍是 ASCII（`print`）。

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

字符串规则：

* 只有 `+`（连接）一个运算；与数值混算、`*`、`**`、一元负号一律报类型错误。
* 转义：`\n` `\t` `\r` `\"` `\\` `\u{XXXX}`；未知转义、代理区码点、超出 U+10FFFF 都报错。
* 字面量里允许裸换行，所以忘写右引号在 REPL 里表现为 `incomplete`，下一行补上即可
  （补上的换行是字符串内容的一部分）。
* 长度按**字节**计，受 `max-string-bytes`（默认 1 MiB）约束；
  常量池按内容去重，`-d`/`:consts`/`:vars` 里以带引号转义的形式显示。

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

## 可调参数

所有策略参数集中在一处、运行期可改（CLI 用 `--set`，REPL 用 `:set`），
改完立刻对后续语句生效：

```console
$ ./sc --list-config
  precision          = 256            # 新建浮点值的二进制精度
  output-digits      = 0              # 输出有效数字位数；0 = 按每个值自身的精度
  allow-non-finite   = false          # 是否允许 inf/nan（默认拒绝）
  max-integer-bits   = 1073741824     # 单个整数的位宽预算（受 GMP 硬上限约束）
  max-int-exponent   = 10000000       # 整数幂的指数上限
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
| [src/number.hpp](src/number.hpp), [src/number.cpp](src/number.cpp) | MPFR 的 RAII 封装，精度按值携带 |
| [src/value.hpp](src/value.hpp), [src/value.cpp](src/value.cpp) | 值（整数/小数/字符串）、数值提升、运算语义、各类预算与策略 |
| [src/token.hpp](src/token.hpp), [src/lexer.hpp](src/lexer.hpp), [src/lexer.cpp](src/lexer.cpp) | 词法分析（UTF-8 解码、字符串字面量与转义、标识符 NFC） |
| [src/opcode.hpp](src/opcode.hpp) | 指令集 + 指令名（漏登记即编译失败） |
| [src/operators.hpp](src/operators.hpp) | **运算符表**：优先级/结合性/指令/求值函数的单一来源 |
| [src/ast.hpp](src/ast.hpp), [src/ast.cpp](src/ast.cpp) | AST 节点 |
| [src/parser.hpp](src/parser.hpp), [src/parser.cpp](src/parser.cpp) | **Pratt 解析器**：前缀/中缀/语句三层 parselet 分派，绑定力来自运算符表 |
| [src/bytecode.hpp](src/bytecode.hpp), [src/bytecode.cpp](src/bytecode.cpp) | `Instruction`、`CompilationUnit`、反汇编 |
| [src/symbols.hpp](src/symbols.hpp), [src/symbols.cpp](src/symbols.cpp) | **会话状态**：变量表 + 常量池，可完整快照/回滚 |
| [src/compiler.hpp](src/compiler.hpp), [src/compiler.cpp](src/compiler.cpp) | AST → 字节码（无状态，产物是 `CompilationUnit`） |
| [src/vm.hpp](src/vm.hpp), [src/vm.cpp](src/vm.cpp) | 栈式虚拟机（核心指令 + 表驱动求值） |
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
* **加一条语句**（为将来的 `if`/`while`/`block` 准备）：在 [src/parser.hpp](src/parser.hpp) 的
  `STMT_PARSELETS` 里加一行 + 写一个 `Result<Stmt> parseXxxStatement(const Token&)`；
  语句节点加进 [src/ast.hpp](src/ast.hpp) 的 `StmtVariant`；
  再在 [src/compiler.cpp](src/compiler.cpp) 的 `genStmt` 补一个分支。
  **漏了编译期就会报错**（`static_assert(detail::alwaysFalse<T>)`），不会静默生成空代码。
  词法层需要的话再加一个关键字 token。
* **加一种值类型**（1~2 个文件）：在 `Value` 的 variant 里加备选、在 `ValueKind` 里加种类、
  在 `KIND_PREFIX` 里加常量池前缀。三处数量不一致会被 `static_assert` 拦下；
  运算分派是 `switch (ValueKind)` 且没有 `default`，漏分支会被 `-Wswitch` 拦下。

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
  `Config::normalize()` 保证 `max-expr-depth` 不会小于节点预算隐含的深度——
  以前这两个上限散落在两个匿名命名空间里，单独调大其中一个会静默破坏"AST 深度安全"这个不变量。
* **数值预算（可调）**：整数幂除了指数上限（`max-int-exponent`，CPU 预算）还要检查**结果位宽**
  （`max-integer-bits`，默认 2^30 位）。只限指数是不够的：底数很大时（例如先算出 `2**10000000`），
  一个"合法"的指数就能让结果超过 GMP 的**硬上限**——`mpz_t` 的 limb 计数是 32 位 `int`
  （见 `gmp.h` 的 `__mpz_struct`），超过约 2^31 limb（≈2^37 位）就不再是"内存不够"，
  而是未定义行为。重构前这条路径会以 SIGFPE(136) 或 SIGABRT(134) 结束进程；
  现在在调用 GMP 之前就被拒绝，返回普通 `Error`。`max-integer-bits` 会被收敛到
  `GMP_HARD_MAX_BITS` 以内。
* **数值域策略**：默认拒绝 `inf`/`nan`（字面量溢出、运算溢出都报错），
  `allow-non-finite=true` 可以放开。以前 `print 1e999999999` 会静默打印 `inf`，
  与"不产生 inf"这句文档相矛盾。
* **事务边界**：一次 `runSource` 是完整事务——符号表（变量 + **常量池**）和 VM 变量一起回滚。
  重构前 `Snapshot` 不覆盖常量池，失败的一行会在 `:consts` 里留下痕迹。
* **VM 不信任字节码**：常量下标、变量下标、栈高度、栈上限（`max-stack`）全部检查，
  异常字节码只会得到 `Error`。
* **指令与运算符表的一致性**：每条指令必须恰好属于"VM 核心"或"运算符表"之一，
  且必须有名字；两条不变量都是 `static_assert`，漏做会编译失败。
* **常量池去重**：整数用十进制、小数用 `%Ra` 十六进制精确文本作为键，`print 1+1` 只占一个常量。
* **诊断位置**：位置是字节偏移换算的行列，制表符/宽字符不会做视觉对齐修正。
* **UTF-8 与 Unicode**：源码必须是合法 UTF-8（overlong、代理区、>U+10FFFF、截断的序列
  都会被拒绝并给出字节偏移）。标识符按 Unicode 类别近似 `ID_Start`/`ID_Continue` 判定，
  并在词法阶段做 NFC 归一化；Unicode 空白（含全角空格 U+3000）等价于空格。
  诊断消息里报的是**码点列**，插入符按**显示宽度**缩进（东亚宽字符算 2 列），
  因此含中文的源码也能对齐。非法字符一律以 `U+XXXX` 形式呈现——
  不会把半个字符的字节塞进消息（那会产生非法 UTF-8 输出，旧版本就有这个 bug）。
* **已知局限**：语句/表达式二分，赋值只能是语句（`x = y = 1` 不合法）；没有块作用域
  （变量表是扁平的、槽位只增不复用）；VM 没有调用栈，因此加函数/闭包是架构级改造；
  诊断只有一个字节偏移而不是 span，也没有错误恢复（遇到第一个错误就停）。
  字符串长度按字节而非码点/字素，且没有比较运算符（所以字符串无法比较、没有索引）；
  插入符对组合字符与 emoji ZWJ 字素簇不完美（按码点宽度求和），制表符仍按 1 列计。
* 没有比较/逻辑运算符、没有 `if`/循环、没有函数、不支持注释——这些都还没有实现。
