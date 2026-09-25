#!/usr/bin/env bash
# sc 回归测试
# 用法: bash tests/run.sh [可执行文件路径]      默认 ./sc
set -u

BIN=${1:-./sc}
if [[ ! -x "$BIN" ]]; then
    echo "找不到可执行文件: $BIN（先运行 make）" >&2
    exit 2
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

pass=0
fail=0
failures=()

ok()  { pass=$((pass + 1)); printf '  ok    %s\n' "$1"; }
bad() { fail=$((fail + 1)); failures+=("$1"); printf '  FAIL  %s\n        %s\n' "$1" "$2"; }

strip_trailing_newlines() {
    local s=$1
    while [[ "$s" == *$'\n' ]]; do s=${s%$'\n'}; done
    printf '%s' "$s"
}

# check_out <名字> <期望退出码> <期望 stdout> [参数...]
check_out() {
    local name=$1 want_rc=$2 want_out=$3; shift 3
    local out rc
    out=$("$BIN" "$@" 2>"$TMP/err"); rc=$?
    out=$(strip_trailing_newlines "$out")
    want_out=$(strip_trailing_newlines "$want_out")
    if [[ $rc -eq $want_rc && "$out" == "$want_out" ]]; then
        ok "$name"
    else
        bad "$name" "exit=$rc(期望 $want_rc) stdout=[$out] 期望=[$want_out] stderr=[$(head -c 200 "$TMP/err")]"
    fi
}

# check_err <名字> <期望退出码> <stderr 子串> [参数...]
check_err() {
    local name=$1 want_rc=$2 needle=$3; shift 3
    local out rc err
    out=$("$BIN" "$@" 2>"$TMP/err"); rc=$?
    err=$(cat "$TMP/err")
    if [[ $rc -eq $want_rc && "$err" == *"$needle"* ]]; then
        ok "$name"
    else
        bad "$name" "exit=$rc(期望 $want_rc) stderr=[$err] 期望含=[$needle] stdout=[$out]"
    fi
}

# check_file_err <名字> <期望退出码> <stderr 子串> <源文件>
check_file_err() {
    local name=$1 want_rc=$2 needle=$3 file=$4
    local rc
    "$BIN" -f "$file" >"$TMP/out" 2>"$TMP/err"; rc=$?
    if [[ $rc -eq $want_rc && "$(cat "$TMP/err")" == *"$needle"* ]]; then
        ok "$name"
    else
        bad "$name" "exit=$rc(期望 $want_rc) stderr=[$(head -c 200 "$TMP/err")] 期望含=[$needle]"
    fi
}

# check_file_out <名字> <期望退出码> <期望 stdout> <源文件>
check_file_out() {
    local name=$1 want_rc=$2 want_out=$3 file=$4
    local rc out
    out=$("$BIN" -f "$file" 2>"$TMP/err"); rc=$?
    out=$(strip_trailing_newlines "$out")
    if [[ $rc -eq $want_rc && "$out" == "$(strip_trailing_newlines "$want_out")" ]]; then
        ok "$name"
    else
        bad "$name" "exit=$rc(期望 $want_rc) stdout=[$out] 期望=[$want_out]"
    fi
}

# check_stdin <名字> <期望退出码> <stdin 内容> <stdout 子串> <stderr 子串或 -> [参数...]
check_stdin() {
    local name=$1 want_rc=$2 input=$3 stdout_needle=$4 stderr_needle=$5; shift 5
    local out rc err
    out=$("$BIN" "$@" 2>"$TMP/err" <<<"$input"); rc=$?
    err=$(cat "$TMP/err")
    local stdout_ok=1 stderr_ok=1
    [[ "$out" == *"$stdout_needle"* ]] || stdout_ok=0
    if [[ "$stderr_needle" != "-" ]]; then
        [[ "$err" == *"$stderr_needle"* ]] || stderr_ok=0
    fi
    if [[ $rc -eq $want_rc && $stdout_ok -eq 1 && $stderr_ok -eq 1 ]]; then
        ok "$name"
    else
        bad "$name" "exit=$rc(期望 $want_rc) stdout 期望含[$stdout_needle] stderr 期望含[$stderr_needle]；stdout=[$out] stderr=[$err]"
    fi
}

# 生成大输入
gen_parens() {
    local n=$1
    { printf 'print '; printf '(%.0s' $(seq 1 "$n"); printf '1'; printf ')%.0s' $(seq 1 "$n"); } >"$TMP/parens.sc"
}
gen_chain() {
    local n=$1
    { printf 'print '; printf '1+%.0s' $(seq 1 "$n"); printf '1'; } >"$TMP/chain.sc"
}
gen_unary() {
    local n=$1
    { printf 'print '; printf -- '-%.0s' $(seq 1 "$n"); printf '1'; } >"$TMP/unary.sc"
}

echo "== 基本求值与词法 =="
check_out "不写分号的 print"            0 "0.$(printf '6%.0s' $(seq 1 76))7" -e 'print 2.0 / 3'
check_out "运算符优先级"                0 "14" -e '2 + 3 * 4'
check_out "括号"                        0 "20" -e '(2 + 3) * 4'
check_out "一元负号"                    0 "5"  -e '2 - -3'
check_out "整数除法向零截断"            0 "-3" -e 'print -7 / 2'
check_out "整数除法丢小数"              0 "0"  -e 'print 1 / 3'
check_out "混合运算提升为浮点"          0 "1.5" -e 'print 1 + 0.5'
check_out "小数写法 .5 / 3."            0 "3.5" -e '.5 + 3.'
check_out "科学计数法"                  0 "1500" -e 'print 1.5e3'
check_out "负指数科学计数法"            0 "0.001" -e 'print 1e-3'
check_out "前导零"                      0 "7" -e 'print 007'
check_out "空语句"                      0 "" -e ';;;'
check_out "空程序"                      0 "" -e ''
check_out "1/3 打印 77 位"              0 "0.$(printf '3%.0s' $(seq 1 77))" -e 'print 1.0 / 3'
check_out "十进制浮点不引入噪声"        0 "0.3" -e 'print 0.1 + 0.2'

echo "== 幂运算 ** =="
check_out "2**100"                      0 "1267650600228229401496703205376" -e 'print 2**100'
check_out "** 右结合"                   0 "512" -e 'print 2**3**2'
check_out "** 优先级高于一元负号"       0 "-4" -e 'print -2**2'
check_out "浮点负指数"                  0 "0.5" -e 'print 2.0**-1'
check_out "大整数精确保留"              0 "340282366920938463463374607431768211456" \
    -e 'x = 2**64; print x * x'
check_err "整数负指数报错"              1 "负整数指数" -e 'print 2**-1'
check_err "指数过大报错"                1 "指数过大" -e 'print 2**100000000000'

echo "== 变量与语句 =="
check_out "赋值后使用"                  0 "10" -e 'x = 5; print x * 2'
check_out "自引用赋值"                  0 "2" -e 'x = 1; x = x + 1; print x'
check_out "常量池去重（1+1 只占一个常量）" 0 "2" -e 'print 1 + 1'
check_err "读取未定义变量"              1 "未定义" -e 'print nope'
check_err "先使用后赋值"                1 "未定义" -e 'print x; x = 1'
check_err "语句之间缺少分号"            1 "期望 ';'" -e 'print 1 print 2'
check_err "非法字符"                    1 "非法字符" -e 'print 1 @ 2'

echo "== 错误与 incomplete 标记 =="
check_err "除零"                        1 "除零错误" -e 'print 1 / 0'
check_err "浮点除零"                    1 "除零错误" -e 'print 1.0 / 0'
check_err "表达式未结束"                1 "未结束" -e 'print 1 +'
check_err "括号未闭合"                  1 "输入未结束" -e 'print (1 + 2'
check_err "缺少表达式"                  1 "意外的 token" -e 'print *'

echo "== 诊断信息（行列 + 插入符）=="
check_err "运行期错误指向运算符"        1 "第 1 行 第 9 列" -e 'print 1 / 0'
check_err "运行期错误带源码行与插入符"  1 "1 | print 1 / 0" -e 'print 1 / 0'
check_err "编译错误指向标识符"          1 "第 1 行 第 7 列" -e 'print nope'
printf 'x = 10;\ny = x * 2;\nprint y + z;\n' >"$TMP/three_lines.sc"
check_file_err "多行源文件定位到第 3 行" 1 "第 3 行 第 11 列" "$TMP/three_lines.sc"

echo "== 命令行 =="
check_err "-e 与 -f 互斥"               2 "不能同时" -e 'print 1' -f /dev/null
check_err "未知选项"                    2 "未知选项" --nope
check_err "-e 缺参数"                   2 "需要一个参数" -e
check_err "文件打不开"                  1 "无法打开文件" -f "$TMP/不存在.sc"
help_out=$("$BIN" -h 2>"$TMP/err"); help_rc=$?
if [[ $help_rc -eq 0 && "$help_out" == 用法:* && "$help_out" == *"退出码"* ]]; then
    ok "帮助可打印（含用法与退出码说明）"
else
    bad "帮助可打印（含用法与退出码说明）" "exit=$help_rc stdout 头部=[$(head -c 60 <<<"$help_out")]"
fi

echo "== -d：反汇编只写 stderr =="
dump_stdout=$("$BIN" -d -e 'print 1 + 1' 2>"$TMP/dump"); dump_rc=$?
if [[ $dump_rc -eq 0 && "$(strip_trailing_newlines "$dump_stdout")" == "2" \
      && "$(grep -c 'PUSH' "$TMP/dump")" -eq 2 && "$(grep -c '\[1\]' "$TMP/dump")" -eq 0 ]]; then
    ok "-d 不污染 stdout；常量池已去重"
else
    bad "-d 不污染 stdout；常量池已去重" "stdout=[$dump_stdout] dump=[$(cat "$TMP/dump")]"
fi

echo "== 递归/规模防护（不应崩溃）=="
gen_parens 50000
check_file_err "5 万层括号：报错而非段错误" 1 "嵌套过深" "$TMP/parens.sc"
gen_chain 50000
check_file_err "5 万项表达式：报错而非段错误" 1 "表达式过于复杂" "$TMP/chain.sc"
gen_unary 100000
check_file_err "10 万个一元负号：报错而非段错误" 1 "嵌套过深" "$TMP/unary.sc"
gen_chain 2047
check_file_out "逼近节点预算的表达式仍可求值" 0 "2048" "$TMP/chain.sc"
gen_parens 200
check_file_out "200 层括号仍可正常求值" 0 "1" "$TMP/parens.sc"

echo "== 可调参数：--set / --list-config / --help =="
check_out "precision=128 输出 38 位有效数字" 0 "0.$(printf '3%.0s' $(seq 1 38))" \
    --set precision=128 -e 'print 1.0/3'
check_out "output-digits 覆盖自动位数"    0 "0.33333" --set output-digits=5 -e 'print 1.0/3'
check_err "未知参数按用法错误"            2 "未知的可调参数" --set nope=1 -e 'print 1'
check_err "取值非法按用法错误"            2 "取值非法" --set precision=abc -e 'print 1'
check_err "--set 缺 name=value"           2 "name=value" --set precision
check_err "max-int-exponent 生效"         1 "指数过大（上限 100）" \
    --set max-int-exponent=100 -e 'print 2**200'
check_err "max-integer-bits 生效"         1 "上限 1000000 位" \
    --set max-integer-bits=1000000 -e 'print 2**2000000'
check_err "max-nodes 生效"                1 "超过 3 个节点" --set max-nodes=3 -e 'print 1+1+1+1'
check_err "max-parse-depth 生效"          1 "嵌套过深" --set max-parse-depth=4 -e 'print ((((1))))'
check_out "max-parse-depth 边界内仍可用"  0 "1" --set max-parse-depth=4 -e 'print (((1)))'

list_out=$("$BIN" --list-config 2>"$TMP/err"); list_rc=$?
if [[ $list_rc -eq 0 && "$list_out" == *precision* && "$list_out" == *max-stack* ]]; then
    ok "--list-config 列出全部参数"
else
    bad "--list-config 列出全部参数" "exit=$list_rc out=[$(head -c 120 <<<"$list_out")]"
fi

cfg_help=$("$BIN" --help 2>"$TMP/err"); cfg_rc=$?
if [[ $cfg_rc -eq 0 && "$cfg_help" == *"--set"* && "$cfg_help" == *"max-parse-depth"* ]]; then
    ok "--help 含参数清单（新增参数自动出现）"
else
    bad "--help 含参数清单（新增参数自动出现）" "exit=$cfg_rc"
fi

big=$("$BIN" -e 'print 2**100000' 2>"$TMP/err"); big_rc=$?
if [[ $big_rc -eq 0 && ${#big} -eq 30103 ]]; then
    ok "默认位宽预算不误伤大整数（2**100000 仍是 30103 位）"
else
    bad "默认位宽预算不误伤大整数（2**100000 仍是 30103 位）" "exit=$big_rc 长度=${#big}"
fi

echo "== 数值边界：必须是 Error，不是信号 =="
# 回归：这两条以前分别以 SIGFPE(136) / SIGABRT(134) 结束进程。
check_err "整数幂结果位宽上限（原先 SIGFPE/ABORT）" 1 "位宽上限" \
    -e 'x = 2**10000000; y = x**100000'
check_err "字面量溢出默认报错"            1 "超出可表示范围" -e 'print 1e999999999'
check_out "allow-non-finite=true 时给 inf" 0 "inf" \
    --set allow-non-finite=true -e 'print 1e999999999'

echo "== 一元负号是一条 NEG 指令 =="
neg_stdout=$("$BIN" -d -e 'print -1' 2>"$TMP/neg"); neg_rc=$?
if [[ $neg_rc -eq 0 && "$(strip_trailing_newlines "$neg_stdout")" == "-1" \
      && "$(grep -c 'NEG' "$TMP/neg")" -eq 1 && "$(grep -c 'PUSH' "$TMP/neg")" -eq 1 ]]; then
    ok "一元负号生成 NEG，不分摊成 0-x（常量池无多余 0）"
else
    bad "一元负号生成 NEG，不分摊成 0-x（常量池无多余 0）" \
        "exit=$neg_rc stdout=[$neg_stdout] dump=[$(cat "$TMP/neg")]"
fi

echo "== 取余 =="
check_out "整数取余"                    0 "1" -e 'print 7 % 3'
check_out "余数取被除数的符号"          0 "-1" -e 'print -7 % 2'
check_out "除数带符号"                  0 "1" -e 'print 7 % -2'
check_out "小数取余"                    0 "1.5" -e 'print 7.5 % 2'
check_out "小数取余负数"                0 "-1.5" -e 'print -7.5 % 2'
check_err "对零取余"                    1 "对零取余" -e 'print 7 % 0'
check_err "对零取余（小数）"            1 "对零取余" -e 'print 7.5 % 0'
check_out "** 比 % 紧"                   0 "2" -e 'print 2**10 % 7'
check_out "% 与 / 同优先级且左结合"      0 "3" -e 'print 100 / 10 % 7'

echo "== 移位 =="
check_out "左移"                        0 "1024" -e 'print 1 << 10'
check_out "右移"                        0 "1" -e 'print 1024 >> 10'
check_out "左移负数"                    0 "-8" -e 'print -1 << 3'
check_out "右移向零截断（与 / 一致）"    0 "-3" -e 'print -7 >> 1'
check_out "大整数左移"                  0 "1267650600228229401496703205376" -e 'print 1 << 100'
check_out "移位优先级低于 +"            0 "32" -e 'print 1 << 2 + 3'
check_out "+ 先于 <<"                   0 "24" -e 'print 1 + 2 << 3'
check_out "* 先于 <<"                   0 "64" -e 'print 1 << 2 * 3'
check_out "移位左结合"                  0 "2" -e 'print 8 >> 1 >> 1'
check_out "右移超过位宽得 0"            0 "0" -e 'print 1 >> 100000000000000000000000'
check_err "负数移位位数"                1 "负数移位位数" -e 'print 1 << -1'
check_err "移位只支持整数（左操作数）"  1 "移位只支持整数" -e 'print 1.5 << 2'
check_err "移位只支持整数（右操作数）"  1 "移位只支持整数" -e 'print 1 << 2.0'
check_err "左移位宽预算（与幂共用）"    1 "左移结果超出位宽上限" -e 'print 1 << 1073741825'
check_err "位宽预算可调"                1 "上限 100 位" --set max-integer-bits=100 -e 'print 1 << 200'
check_err "单字符 < 给出提示"           1 "移位要写成" -e 'print 3 < 4'
check_err "单字符 > 给出提示"           1 '移位要写成' -e 'print 3 > 4'
check_err "字符串不能移位"              1 "字符串只支持" -e 'print "a" << 1'

echo "== UTF-8：标识符 =="
check_out "中文标识符赋值与读取"        0 "42" -e '变量 = 42; print 变量'
check_err "中文未定义变量报错"          1 "变量 '速度' 未定义" -e 'print 速度'
# NFC：é 的两种写法（U+00E9 / e + U+0301）必须是同一个标识符
check_out "标识符 NFC（NFD 读取 NFC 名字）" 0 "7" -e "$(printf '\xc3\xa9 = 7; print e\xcc\x81')"
check_out "标识符 NFC（NFC 读取 NFD 名字）" 0 "8" -e "$(printf 'e\xcc\x81 = 8; print \xc3\xa9')"

echo "== UTF-8：字符串字面量 =="
check_out "字符串输出"                  0 "你好，世界" -e 'print "你好，世界"'
check_out "转义序列"                    0 "$(printf 'a\tb\nc"d\\e')" -e 'print "a\tb\nc\"d\\e"'
check_out "\u{...} 转义"                0 "你好" -e 'print "\u{4F60}\u{597D}"'
check_out "字符串拼接"                  0 "ab" -e 'print "a" + "b"'
check_out "字符串可以放进变量"          0 "xy" -e 's = "x"; s = s + "y"; print s'
check_err "字符串 * 数 是类型错误"      1 "类型错误" -e 'print "a" * 2'
check_err "字符串 + 数 是类型错误"      1 "类型错误" -e 'print "a" + 1'
check_err "未闭合字符串"                1 "字符串未结束" -e 'print "中'
check_err "\u 转义超范围"               1 "超出 Unicode 范围" -e 'print "\u{110000}"'
check_err "\u 转义代理区"               1 "代理区码点" -e 'print "\u{D800}"'
check_err "未知转义序列"                1 "未知的转义序列" -e 'print "\q"'
check_err "字符串长度预算可调"          1 "超出长度上限" \
    --set max-string-bytes=16 -e 'print "aaaaaaaaaaaaaaaaaaaaaaaa"'

echo "== UTF-8：空白、合法性与诊断 =="
check_out "全角空格也是空白"            0 "3" -e "$(printf 'print 1\xe3\x80\x80+\xe3\x80\x802')"
check_err "非法首字节 0xFF"             1 "不是合法的 UTF-8" -e "$(printf 'print \xff')"
check_err "截断的多字节序列"            1 "字符中间结束" -e "$(printf 'print \xe4\xb8')"
check_err "overlong 编码被拒绝"         1 "不是合法的 UTF-8" -e "$(printf 'print \xc0\x80')"
check_err "ASCII 非法字符带码点"        1 "U+0040" -e 'print @'
check_err "非 ASCII 非法字符带码点"     1 "U+20AC" -e 'print €'
# 宽字符之后的错误：消息报码点列（23），插入符按显示宽度缩进（28 = 6+12+6+4）
wide_err=$("$BIN" -e '变量名 = 1; print 变量名 + 1/0' 2>&1)
wide_pad=$(printf '%s\n' "$wide_err" | tail -1); wide_pad=${wide_pad#*| }; wide_pad=${wide_pad%^}
if [[ "$wide_err" == *"第 23 列"* && ${#wide_pad} -eq 28 ]]; then
    ok "宽字符诊断：码点列 + 显示宽度插入符"
else
    bad "宽字符诊断：码点列 + 显示宽度插入符" \
        "stderr=[$(printf '%s' "$wide_err" | tr '\n' '~')] 插入符缩进=${#wide_pad}(期望 28)"
fi

echo "== REPL =="
check_stdin "续行：不完整表达式自动等下一行" 0 $'print 1 +\n424241\n:quit\n' "424242" "-"
check_stdin "每条语句独立求值"            0 $'print 424242\n:quit\n' "424242" "-"
check_stdin "错误立即报告"                0 $'print nope\n:quit\n' ">>>" "未定义"
check_stdin "事务回滚：失败行不留下变量"  0 $'x = 1; print nope;\nprint x;\n:quit\n' ">>>" "变量 'x' 未定义"
check_stdin ":vars 显示变量与当前值"      0 $'a = 5\n:vars\n:quit\n' "a -> slot 0 = 5" "-"
check_stdin ":reset 清空变量"             0 $'a = 5\n:reset\n:vars\n:quit\n' "(空)" "-"
check_stdin "续行状态下 :quit 可用"       0 $'print 1 +\n:quit\n' ">>>" "已丢弃未完成的输入"
check_stdin "未知命令提示"                0 $':nope\n:quit\n' ">>>" "未知命令"
check_stdin "EOF 时丢弃未完成输入"        0 $'print 1 +\n' ">>>" "丢弃未完成的输入"
check_stdin ":set 立即生效"                0 $':set max-int-exponent 100\nprint 2**200\n:quit\n' ">>>" "指数过大（上限 100）"
check_stdin ":set 未知参数"                0 $':set nope 1\n:quit\n' ">>>" "未知的可调参数"
check_stdin ":set 取值非法"                0 $':set precision abc\n:quit\n' ">>>" "取值非法"
check_stdin ":config 列出参数"             0 $':config\n:quit\n' "max-stack" "-"
check_stdin "REPL：中文变量"                0 $'温度 = 424242\nprint 温度\n:quit\n' "424242" "-"
check_stdin "REPL：字符串跨行续行（保留裸换行）" 0 $'print "中\n文"\n:quit\n' $'中\n文' "-"
check_stdin "REPL：:consts 里字符串带引号"  0 $'"hi" + "yo"\n:consts\n:quit\n' '[0] "hi"' "-"
check_stdin ":prec 反映当前精度"           0 $':set precision 128\n:prec\n:quit\n' "≈ 38 位" "-"
check_stdin "失败行的常量池也回滚"         0 $'x = 1/0\n:consts\n:quit\n' "(空)" "除零错误"
check_stdin "提高精度不污染旧值"           0 $'x = 1.0/3\n:set precision 512\nprint x\n:quit\n' \
    "$(printf '0.%s' "$(printf '3%.0s' $(seq 1 77))")" "-"

echo
if [[ $fail -eq 0 ]]; then
    printf '全部通过：%d 项\n' "$pass"
    exit 0
fi
printf '通过 %d 项，失败 %d 项：\n' "$pass" "$fail"
for name in "${failures[@]}"; do printf '  - %s\n' "$name"; done
exit 1
