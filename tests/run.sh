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
gen_blocks() {
    local n=$1
    { printf '{%.0s' $(seq 1 "$n"); printf 'print 1'; printf '}%.0s' $(seq 1 "$n"); } >"$TMP/blocks.sc"
}
gen_ifs() {
    local n=$1
    { printf 'if (true) {%.0s' $(seq 1 "$n"); printf 'print 1'; printf '}%.0s' $(seq 1 "$n"); } >"$TMP/ifs.sc"
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
# 语句嵌套也必须被拦住：加语句深度防护之前这两条会在解析期打穿调用栈（段错误 139）。
gen_blocks 50000
check_file_err "5 万层嵌套块：报错而非段错误" 1 "语句嵌套过深" "$TMP/blocks.sc"
gen_ifs 50000
check_file_err "5 万层嵌套 if：报错而非段错误" 1 "语句嵌套过深" "$TMP/ifs.sc"
gen_blocks 200
check_file_out "200 层嵌套块仍可正常求值" 0 "1" "$TMP/blocks.sc"

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
check_err "单字符 ! 给出提示"            1 "不等号要写成" -e 'print 1 ! 2'

echo "== 布尔类型与比较运算符 =="
check_out "true 字面量"                  0 "true" -e 'print true'
check_out "false 字面量"                 0 "false" -e 'print false'
check_out "布尔字面量可以放进变量"       0 "true" -e 'x = true; print x'
check_out "布尔字面量做表达式语句"       0 "false" -e 'false'
check_err "true 是关键字，不能被赋值"     1 "期望 ';'" -e 'true = 1'
check_out "整数相等"                     0 "true" -e 'print 1 == 1'
check_out "整数不等"                     0 "false" -e 'print 1 == 2'
check_out "!= 取反"                      0 "true" -e 'print 1 != 2'
check_out "小于 / 大于"                  0 $'true\nfalse' -e 'print 1 < 2; print 1 > 2'
check_out "<= 与 >= 的边界"              0 $'true\ntrue' -e 'print 2 <= 2; print 2 >= 2'
check_out "负数比较"                     0 "true" -e 'print -3 < -1'
check_out "大整数比较（GMP）"            0 $'true\nfalse' -e 'print 2**100 > 2**99; print 2**100 < 2**99'
check_out "整数与小数比较（提升为小数）" 0 "true" -e 'print 1 < 1.5'
check_out "1 == 1.0（跨数值塔相等）"      0 "true" -e 'print 1 == 1.0'
# 0.1 与 0.2 在二进制里是无限循环小数，256 位精度下舍入方向一致，所以和恰好等于 0.3。
# 换成 1.0/3 这类就未必了；== 是精确比较，不做任何容差。
check_out "小数相等（无容差）"            0 "true" -e 'print 0.1 + 0.2 == 0.3'
check_out "小数比较"                     0 "false" -e 'print 1.5 > 2'
check_out "字符串相等"                   0 "true" -e 'print "ab" == "ab"'
check_out "字符串不等（按内容）"         0 "true" -e 'print "ab" != "ba"'
check_out "中文字符串相等"               0 "true" -e 'print "中文" == "中文"'
check_out "布尔相等 / 不等"              0 $'true\ntrue' -e 'print true == true; print true != false'
check_out "布尔相等可以和比较结果互比"   0 "true" -e 'print (1 < 2) == true'
check_err "布尔不参与算术（+）"          1 "布尔值不参与算术运算" -e 'print true + 1'
check_err "布尔不参与算术（-）"          1 "布尔值不参与算术运算" -e 'print 1 - true'
check_err "布尔不参与乘法"               1 "布尔值不参与算术运算" -e 'print true * 2'
check_err "布尔不参与取余"               1 "布尔值不参与算术运算" -e 'print true % 2'
check_err "布尔不参与移位"               1 "布尔值不参与算术运算" -e 'print true << 1'
check_err "布尔不参与幂"                 1 "布尔值不参与算术运算" -e 'print true ** 2'
check_err "布尔不支持一元负号"           1 "布尔值不支持一元负号" -e 'print -true'
check_err "布尔没有大小之分"             1 "不支持排序比较" -e 'print 1 < true'
check_err "字符串没有大小之分"           1 "字符串不支持排序比较" -e 'print "a" < "b"'
check_err "字符串 <= 也报类型错误"       1 "字符串不支持排序比较" -e 'print "a" <= "a"'
check_err "布尔与整数不能比较"           1 "不能比较" -e 'print true == 1'
check_err "整数与字符串不能比较"         1 "不能比较" -e 'print 1 == "1"'
check_err "字符串与布尔不能比较"         1 "不能比较" -e 'print "a" == true'
check_err "链式排序比较报错并给出改写提示" 1 "解析成 \`(1 < 2) < 3\`" -e 'print 1 < 2 < 3'
check_out "链式相等可以解析（(1==2)==false）" 0 "true" -e 'print 1 == 2 == false'
check_err "跨种类比较仍是类型错误"       1 "不能比较" -e 'print 1 == "1" == "x"'
check_out "比较比移位松"                 0 "true" -e 'print 1 << 2 < 5'
check_out "比较比移位松（右侧）"         0 "true" -e 'print 5 > 1 << 2'
check_out "比较比加减松"                 0 "true" -e 'print 1 + 1 == 2'
check_out "比较比乘除松"                 0 "false" -e 'print 2 * 3 < 6'
check_out "比较比幂松"                   0 "true" -e 'print 2 ** 3 >= 8'
check_out "比较比一元负号松"             0 "true" -e 'print -2 < -1'
check_out "== 与 < 同优先级（都低于移位）" 0 "true" -e 'print 1 < 2 == true'
check_out "括号里的比较可以嵌套"         0 "true" -e 'print (1 < 2) == (2 < 3)'
check_out "比较结果当条件表达式用"       0 "false" -e 'print (1 < 2) == (3 < 2)'

cmp_stdout=$("$BIN" -d -e 'print 1 < 2' 2>"$TMP/cmp"); cmp_rc=$?
if [[ $cmp_rc -eq 0 && "$(strip_trailing_newlines "$cmp_stdout")" == "true" \
      && "$(grep -cE '^ *[0-9]+: LT$' "$TMP/cmp")" -eq 1 \
      && "$(grep -c 'PUSH' "$TMP/cmp")" -eq 2 \
      && "$(grep -c '^  \[[0-9]*\]' "$TMP/cmp")" -eq 2 ]]; then
    ok "比较生成一条 LT 指令，不额外压常量（常量池只有两个操作数）"
else
    bad "比较生成一条 LT 指令，不额外压常量（常量池只有两个操作数）" \
        "exit=$cmp_rc stdout=[$cmp_stdout] dump=[$(cat "$TMP/cmp")]"
fi

bool_pool=$("$BIN" -d -e 'print true; print true; print 1 < 2' 2>"$TMP/bool"); bool_rc=$?
if [[ $bool_rc -eq 0 && "$(grep -c 'PUSH  \[0\] true' "$TMP/bool")" -eq 2 \
      && "$(grep -c '^  \[[0-9]*\] true$' "$TMP/bool")" -eq 1 \
      && "$(grep -c '^  \[[0-9]*\]' "$TMP/bool")" -eq 3 ]]; then
    ok "布尔字面量进常量池且按内容去重（两次 true 只占一个常量）"
else
    bad "布尔字面量进常量池且按内容去重（两次 true 只占一个常量）" \
        "exit=$bool_rc dump=[$(cat "$TMP/bool")]"
fi

check_stdin ":vars 里的布尔显示为 true"    0 $'x = 2 > 1\n:vars\n:quit\n' "x -> slot 0 = true" "-"
check_stdin ":consts 里的布尔不带引号"     0 $'x = true; print x\n:consts\n:quit\n' "[0] true" "-"
check_stdin "REPL：多行 if 块自动续行"      0 $'if (true) {\nprint 424242\n}\n:quit\n' "424242" "-"
check_stdin "REPL：多行 for 自动续行"       0 $'for (i = 0;\ni < 2;\ni = i + 1) {\nprint 424242\n}\n:quit\n' "424242" "-"
check_stdin "REPL：块未闭合提示续行"        0 $'if (true) {\nprint 1\n' ">>>" "丢弃未完成的输入"
check_stdin "REPL：循环里 break 可用"       0 $'i = 0\nwhile (true) {\ni = i + 1\nif (i > 1) { break }\n}\nprint i\n:quit\n' "2" "-"

echo "== if / else =="
check_out "if 真分支"                    0 "yes" -e 'if (1 < 2) { print "yes" } else { print "no" }'
check_out "if 假分支走 else"             0 "no"  -e 'if (1 > 2) { print "yes" } else { print "no" }'
check_out "没有 else 且条件为假"         0 ""    -e 'if (false) { print 1 }'
check_out "else if 链"                   0 "b"   -e 'if (1 > 2) print "a"; else if (2 > 1) print "b"; else print "c"'
check_out "单语句分支（无花括号）"       0 "1"   -e 'if (true) print 1'
check_out "then/else 各是一条语句"       0 "2"   -e 'if (false) print 1; else print 2'
check_out "空块合法"                     0 ""    -e 'if (true) { }'
check_out "if 可以嵌套"                  0 "42"  -e 'if (true) { if (true) { print 42 } }'
check_out "if 不成立时 else if 继续判断"  0 "2"  -e 'x = 2; if (x == 1) { print 1 } else if (x == 2) { print 2 }'
check_err "条件必须是布尔（整数）"       1 "条件必须是布尔值（这里是整数）" -e 'if (1) { print 1 }'
check_err "条件必须是布尔（字符串）"     1 "条件必须是布尔值（这里是字符串）" -e 'if ("a") { print 1 }'
check_err "缺条件括号"                   1 "期望" -e 'if true { print 1 }'
check_err "缺右花括号"                   1 "输入未结束" -e 'if (true) { print 1'

echo "== while =="
check_out "while 计数"                   0 $'0\n1\n2' -e 'i = 0; while (i < 3) { print i; i = i + 1 }'
check_out "while 条件一开始就为假"       0 ""    -e 'while (false) { print 1 }'
check_out "while + break"                0 "3"   -e 'i = 0; while (true) { i = i + 1; if (i > 2) break }; print i'
check_out "while + continue"             0 $'1\n3' -e 'i = 0; while (i < 3) { i = i + 1; if (i == 2) continue; print i }'
check_err "while 条件必须是布尔"         1 "条件必须是布尔值" -e 'while (1) { print 1 }'
check_err "while 里缺分号"               1 "期望 ';'" -e 'i = 0; while (i < 1) { i = i + 1 print 1 }'

echo "== for（C 风格）=="
check_out "for 计数"                     0 $'0\n1\n2' -e 'for (i = 0; i < 3; i = i + 1) { print i }'
check_out "for 单语句体"                 0 $'0\n1' -e 'for (i = 0; i < 2; i = i + 1) print i'
check_out "for 初始化可省略"             0 $'0\n1' -e 'i = 0; for (; i < 2; i = i + 1) { print i }'
check_out "for 后置可省略"               0 $'0\n1' -e 'for (i = 0; i < 2;) { print i; i = i + 1 }'
check_out "for 条件省略即死循环"         0 "4"   -e 'n = 0; for (;;) { n = n + 1; if (n > 3) break }; print n'
check_out "for + continue 仍走后置"      0 $'0\n2' -e 'for (i = 0; i < 3; i = i + 1) { if (i == 1) continue; print i }'
check_out "for + break"                  0 $'0\n1' -e 'for (i = 0; i < 10; i = i + 1) { if (i == 2) break; print i }'
check_out "for 初始化里定义变量"         0 "3"   -e 'for (n = 0; n < 3; n = n + 1) { }; print n'
check_err "for 缺右括号"                 1 "期望" -e 'for (i = 0; i < 2; i = i + 1 { print i }'
check_err "for 条件必须是布尔"           1 "条件必须是布尔值" -e 'for (i = 0; i; i = i + 1) { print 1 }'

echo "== break / continue 的位置校验 =="
check_err "break 必须在循环里"           1 "break 只能用在循环里" -e 'break'
check_err "continue 必须在循环里"        1 "continue 只能用在循环里" -e 'continue'
check_err "break 在 if 里也不行（不在循环）" 1 "break 只能用在循环里" -e 'if (true) { break }'
check_out "break 在块里（在循环内）"     0 ""    -e 'for (i = 0; i < 5; i = i + 1) { { break }; print i }'
check_out "continue 在块里（在循环内）"  0 $'0\n2' -e 'for (i = 0; i < 3; i = i + 1) { { if (i == 1) continue }; print i }'

echo "== 循环嵌套 =="
check_out "双层 for"                     0 $'0\n1\n10\n11' \
    -e 'for (i = 0; i < 2; i = i + 1) { for (j = 0; j < 2; j = j + 1) { print i * 10 + j } }'
check_out "break 只跳出内层"             0 $'0\n10' \
    -e 'for (i = 0; i < 2; i = i + 1) { for (j = 0; j < 5; j = j + 1) { if (j == 1) break; print i * 10 + j } }'
check_out "continue 只作用于内层"        0 $'0\n10' \
    -e 'for (i = 0; i < 2; i = i + 1) { for (j = 0; j < 5; j = j + 1) { if (j == 1) continue; if (j > 1) break; print i * 10 + j } }'
check_out "while 套 for"                 0 $'0\n1\n0\n1' \
    -e 'k = 0; while (k < 2) { for (j = 0; j < 2; j = j + 1) { print j }; k = k + 1 }'
check_out "循环里用比较结果当条件"       0 "3" -e 'i = 0; while (i < 3) { i = i + 1 }; print i'

echo "== 语句块与分号规则 =="
check_out "块作为语句"                   0 "1" -e '{ print 1 }'
check_out "块后写分号可接更多语句"       0 $'1\n2' -e '{ print 1 }; print 2'
check_out "块内最后一条可省分号"         0 $'1\n2' -e '{ print 1; print 2 }'
check_out "块内多余分号合法"             0 "1" -e '{ print 1;; }'
check_out "空块"                         0 "" -e '{ }'
check_err "块后漏分号"                   1 "期望 ';'" -e '{ print 1 } print 2'
check_err "块内漏分号"                   1 "期望 ';'" -e '{ print 1 print 2 }'
check_err "顶层漏分号"                   1 "期望 ';'" -e 'print 1 print 2'
check_err "循环体后漏分号"               1 "期望 ';'" -e 'for (i = 0; i < 1; i = i + 1) { print i } print 2'
check_out "循环体后写分号"               0 $'0\n2' -e 'for (i = 0; i < 1; i = i + 1) { print i }; print 2'
check_err "循环体后漏分号"               1 "期望 ';'" -e 'for (i = 0; i < 1; i = i + 1) { print i } break'
check_err "块语句后接赋值也要分号"       1 "期望 ';'" -e 'i = 0; { print i } i = i + 1'

echo "== Pratt 绑定力（左右结合 / 前缀与幂的相互作用）=="
check_out "混合链：+ - * / %"           0 "5" -e 'print 1 + 2 * 3 - 4 / 2 % 3'
check_out "% 左结合"                    0 "4" -e 'print 100 % 7 * 2'
check_out "幂右结合再取余"              0 "2" -e 'print 2 ** 3 ** 2 % 5'
check_out "一元负号比幂松"              0 "-4" -e 'print -2 ** 2 % 5'
check_out "一元负号比乘紧"              0 "-18" -e 'print 2 * -3 ** 2'
check_out "移位低于加号（两侧）"        0 "8" -e 'print 1 + 1 << 1 + 1'
check_out "移位低于加号（右侧）"        0 "1" -e 'print 7 >> 1 + 1'
check_out "同级移位左结合"              0 "32" -e 'print 1 << 2 << 3'
check_out "连续前缀负号"                0 "5" -e 'print - - 5'
check_out "括号压过一切"                0 "-9" -e 'print -(1 + 2) * 3'
check_out "语句混合分派"                0 $'2\n2' -e 'x = 1; print x + 1; x * 2'
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

echo "== 内建数学函数（vendor/neo-math.h）=="
# 小数函数：整数实参按 precision 提升，结果位数由值自身精度决定（256 位 → 77 位有效数字）。
check_out "sqrt(2)"                     0 "1.4142135623730950488016887242096980785696718753769480731766797379907324784621" -e 'print sqrt(2)'
check_out "整数实参提升为小数"          0 "2" -e 'print sqrt(4)'
check_out "pi() 常量"                   0 "3.1415926535897932384626433832795028841971693993751058209749445923078164062862" -e 'print pi()'
check_out "ln(e()) 与常量往返"          0 "1" -e 'print ln(e())'
check_out "log(x, base) 换底"           0 "3" -e 'print log(1000, 10)'
check_out "log10 / ln 分工"             0 "2" -e 'print log10(100)'
check_out "hypot"                       0 "5" -e 'print hypot(3, 4)'
check_out "sin(pi()/2)"                 0 "1" -e 'print sin(pi()/2)'
check_out "cos(pi())"                   0 "-1" -e 'print cos(pi())'
# 值种类保持：整数不该为了取整/取绝对值而变成小数（那会白白丢精度）。
check_out "floor(2.7)"                  0 "2" -e 'print floor(2.7)'
check_out "floor 对整数是恒等"          0 "1267650600228229401496703205376" -e 'print floor(2**100)'
check_out "abs 保持整数精确"            0 "1267650600228229401496703205376" -e 'print abs(-2**100)'
check_out "min 保持整数精确"            0 "1267650600228229401496703205376" -e 'print min(2**100, 2**101)'
# 整数函数（GMP）
check_out "gcd"                         0 "6" -e 'print gcd(12, 18)'
check_out "lcm"                         0 "36" -e 'print lcm(12, 18)'
check_out "factorial(20)"               0 "2432902008176640000" -e 'print factorial(20)'
check_out "binomial(20, 5)"             0 "15504" -e 'print binomial(20, 5)'
check_out "fibonacci(100)"              0 "354224848179261915075" -e 'print fibonacci(100)'
check_out "is_prime"                    0 "true" -e 'print is_prime(2**61 - 1)'
check_out "next_prime"                  0 "101" -e 'print next_prime(100)'
check_out "isqrt 是精确整数平方根"      0 "1125899906842624" -e 'print isqrt(2**100)'
check_out "powm 模幂"                   0 "24" -e 'print powm(2, 10, 1000)'
# 语法与绑定力：调用是最紧的后缀形式
check_out "sqrt(2)**2"                  0 "2" -e 'print sqrt(2)**2'
check_out "调用比一元负号紧"            0 "-1.4142135623730950488016887242096980785696718753769480731766797379907324784621" -e 'print -sqrt(2)'
check_out "逗号分隔的实参各算各的"      0 "3" -e 'print max(1 + 1, 2 + 1)'
check_out "实参里可以再调用"            0 "2" -e 'print sqrt(sqrt(16))'
# 常量按 Config::precision 求值，不是写死的 256
check_out "pi() 跟随 precision" 0 "3.141592653589793238462643383279502884197169399375105820974944592307816406286208998628034825342117067982148086513282306647093844609550582231725359408128481" --set precision=512 -e 'print pi()'
# 编译期就能拦下的错误
check_err "未知内建函数"                1 "未知的内建函数：nope" -e 'print nope(1)'
check_err "实参个数过多"                1 "需要 1 个参数，但给了 2 个" -e 'print sqrt(1, 2)'
check_err "实参个数过少"                1 "内建函数 log 需要 2 个参数，但给了 1 个" -e 'print log(1)'
check_err "实参类型不符"                1 "需要数值参数（这里是字符串）" -e 'print sqrt("a")'
check_err "整数函数拒绝小数"            1 "需要整数参数（这里是小数）" -e 'print gcd(2.5, 1)'
check_err "未闭合的调用是 incomplete"   1 "输入未结束：缺少 ')'" -e 'print sqrt(2'
# 运行期数值策略
check_err "定义域错误 sqrt(-1)"         1 "sqrt: 数学函数定义域错误" -e 'print sqrt(-1)'
check_err "溢出错误 exp(1e9)"           1 "exp: 数学函数结果溢出" -e 'print exp(1e9)'
check_err "阶乘规模受 CPU 预算约束"     1 "factorial: 参数超过 CPU 预算" -e 'print factorial(1000000000)'
check_err "整数结果受位宽预算约束"      1 "factorial: 结果预计约" --set max-integer-bits=64 -e 'print factorial(30)'
check_err "powm 拒绝非正模"             1 "模必须为正整数" -e 'print powm(2, 3, 0)'
check_out "allow-non-finite 时 ln(0) 得 -inf" 0 "-inf" --set allow-non-finite=true -e 'print ln(0)'
# 反汇编把表项下标显示成名字
check_err "-d 显示内建函数名"           0 "CALL  sqrt" -d -e 'print sqrt(2)'

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
