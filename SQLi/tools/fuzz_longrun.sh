#!/bin/bash
# ============================================================================
# KADD 随机 fuzz 长稳：21 个可长跑开放接口「单接口」API_CALLS 次等频真实随机调用
# + 覆盖率运行计数留存（一键）
# 用法: cd /opt/KADD/SQLi && ./tools/fuzz_longrun.sh [sve|onnx]
#   sve  （默认）SVE 原生后端      → build_longrun_sve/
#   onnx         ONNX Runtime 后端 → build_longrun_onnx/
# 环境变量:
#   N=32                并行实例数（默认 32）
#   API_CALLS=30000000  单个等频接口的目标调用次数（默认 3000 万；单接口口径）
#
# 设计原则（随机测试优先，计数只是保底下限）：
#   1. 完整 selector / heavy / fault-inject 随机 fuzz 每次迭代全部保留，
#      覆盖率结论仍由随机测试主导（门槛：函数 100%，单 C 文件行 ≥80%）。
#   2. KADD_FUZZ_EQUAL_RANDOM=1 额外叠加等频随机层：21 个可长跑开放接口
#      每次迭代各做 1 次真实随机调用（有效持久句柄 + 随机输入，
#      输入由 libFuzzer 变异数据派生），绝不使用空指针/固定 case 凑数。
#   3. 17 个生命周期类接口（load/create/free）明确豁免 3000 万次：
#      文件 I/O、模型解析、ONNX Session 创建或释放语义不适合千万次长跑；
#      它们仍由 selector / heavy 路径正常覆盖并在报告单列。
#
# 与另外两个入口的分工：
#   build.sh fuzz     常规分层 fuzz（探索 / 崩溃定位，不承担等频计数）
#   coverage_all.sh   UT+Fuzz 覆盖率短跑（FUZZ_TIME 限时），产物 build_cov_*
#   本脚本            等频随机长稳 + 覆盖率插桩：跑满 -runs 的同时把每个
#                     函数/每行的执行次数留在 profraw 里，结束后合并生成
#                     HTML——点开函数所在行即可看到 21 个接口
#                     ~3000 万量级的运行次数（插桩有 10~30% 吞吐开销）。
#
# 口径：-runs 计「迭代数」。每次迭代 = 完整随机 fuzz + 21 个等频接口各 1 次。
# 长稳要求「单接口」达到 API_CALLS 次。libFuzzer 的 -runs 与回调次数存在
# 少量口径差，按 15% 加余量。状态文件 api_min 是 21 个接口独立计数的
# 最小值，达标判据即 api_min ≥ API_CALLS（selector 的额外调用不重复计入，
# 覆盖率 HTML 的原始计数只会更高）。不设 TIMEOUT 时限：按 -runs 跑满。
#
# 日志：fuzzer 初始化完成后 stdout/stderr 一并重定向到 /dev/null（3000 万次
# 不刷屏），进度看 tests/kadd_fuzz_stat_<pid>.txt（每 10s/4096 次迭代覆盖写）。
# 崩溃输入由 libFuzzer 落到 tests/fuzz_art/<w>/，复现：
#   KADD_FUZZ_KEEP_STDOUT=1 ./kadd_fuzzer <artifact_prefix>/crash-xxx -runs=1
#
# 产物（$BLD = build_longrun_{sve,onnx}）：
#   fuzz_report.txt          长稳报告（21 接口独立计数 / 豁免清单 / 崩溃数 / 实例状态）
#   lr.profdata              各实例 profraw 合并后的覆盖率数据（含运行次数）
#   lr_coverage.txt          覆盖率汇总文本（含逐 C 文件门槛判定）
#   lr_cov_html/index.html   覆盖率 HTML（点开函数所在行可见运行次数）
#   longrun_<dir>.tar.gz     以上产物打包（回传/留档）
# ============================================================================

set -uo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ONNXRT_VERSION="1.28.0"
ONNX_HOME="$PROJECT_ROOT/util/onnxruntime-linux-aarch64-${ONNXRT_VERSION}"
export ONNXRUNTIME_HOME="$ONNX_HOME"

MODE="${1:-sve}"
case "$MODE" in
    sve)  SVE_FLAG="ON";  BUILD_DIR="build_longrun_sve"  ;;
    onnx) SVE_FLAG="OFF"; BUILD_DIR="build_longrun_onnx" ;;
    *)    echo "[ERROR] 用法: $0 [sve|onnx]"; exit 2 ;;
esac

N="${N:-32}"
API_CALLS="${API_CALLS:-30000000}"
case "$N" in ''|*[!0-9]*) echo "[ERROR] N 必须是正整数（当前: $N）"; exit 2 ;; esac
case "$API_CALLS" in ''|*[!0-9]*) echo "[ERROR] API_CALLS 必须是正整数（当前: $API_CALLS）"; exit 2 ;; esac
[ "$N" -ge 1 ]        || { echo "[ERROR] N 必须 >= 1"; exit 2; }
[ "$API_CALLS" -ge 1 ] || { echo "[ERROR] API_CALLS 必须 >= 1"; exit 2; }
# 单接口口径：每迭代 21 个等频接口各调用 1 次；15% 余量吸收 libFuzzer
# 停止计数与回调次数的口径差，避免长跑结束时差一点而整轮重跑
RUNS=$(( (API_CALLS * 115 + N * 100 - 1) / (N * 100) + 16 ))

BLD="$PROJECT_ROOT/$BUILD_DIR"
FBLD="$BLD/tests"

command -v clang++ >/dev/null 2>&1 || {
    echo "[ERROR] 需要 clang++（覆盖率插桩 + libFuzzer 仅 Clang 可用）"; exit 3; }

echo "==================== $BUILD_DIR (SVE=$SVE_FLAG, N=$N, RUNS=$RUNS) ===================="

# ----------------------------------------------------------------------------
# [1/6] 配置：clang + 覆盖率插桩。ENABLE_CODE_COVERAGE=ON 会强制静态库，
#       引擎的计数随 kadd_fuzzer 二进制一起留在 profraw 里（与 coverage_all.sh
#       的 fuzz 覆盖率同一套配置，报告口径可直接对比）。
# ----------------------------------------------------------------------------
mkdir -p "$BLD" && cd "$BLD"
echo "[1/6] cmake configure（clang + 覆盖率插桩）"
cmake -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
      -DCMAKE_BUILD_TYPE=Release \
      -DENABLE_CODE_COVERAGE=ON \
      -DENABLE_ARM_SVE="$SVE_FLAG" \
      "$PROJECT_ROOT" || { echo "[ERROR] configure 失败"; exit 4; }

# ----------------------------------------------------------------------------
# [2/6] 构建（失败时与 coverage_all.sh 一致：关闭 ORT 故障注入重试一次）
# ----------------------------------------------------------------------------
echo "[2/6] cmake build"
if ! cmake --build . -j"$(nproc)"; then
    echo "[WARN] 构建失败，尝试 KADD_FUZZ_INFERENCE_INJECT=OFF 重新构建"
    cmake -DKADD_FUZZ_INFERENCE_INJECT=OFF "$PROJECT_ROOT" \
        || { echo "[ERROR] configure 失败"; exit 4; }
    cmake --build . -j"$(nproc)" || { echo "[ERROR] build 失败"; exit 4; }
fi
[ -x "$FBLD/kadd_fuzzer" ] || { echo "[ERROR] 未生成 $FBLD/kadd_fuzzer"; exit 4; }

# ----------------------------------------------------------------------------
# [3/6] 运行：N 实例并行，按 -runs 跑满（无时限）
#   · KADD_FUZZ_EQUAL_RANDOM=1：完整随机 fuzz + 21 接口等频随机层叠加执行
#   · -max_len=4096：等频层与 selector 都消费真实变异数据，不做 1 字节退化
#   · LLVM_PROFILE_FILE 用 %p：每实例一份 profraw，天然不冲突
#   · N>1 时给每实例私有 /tmp（mount namespace + tmpfs + private 传播）：
#     fuzzer 的合成资产是硬编码 /tmp 路径，裸并行会互相覆写；探测失败时
#     告警后继续并行（合成资产内容相同，撕裂读最坏只多走一次错误分支）
#   · 强制静音：KEEP_STDOUT 在 3000 万次下会产生 GB 级日志，这里必须 unset；
#     状态文件照常写（QUIET_STATS 一并 unset），崩溃样本落盘不受静音影响
# ----------------------------------------------------------------------------
echo "[3/6] run kadd_fuzzer（$N 实例 × -runs=$RUNS，无时限）"
cd "$FBLD"
export LD_LIBRARY_PATH="$ONNX_HOME/lib:${LD_LIBRARY_PATH:-}"
export LLVM_PROFILE_FILE="$BLD/lr_%p.profraw"
export KADD_FUZZ_EQUAL_RANDOM=1
unset KADD_FUZZ_KEEP_STDOUT KADD_FUZZ_QUIET_STATS KADD_FUZZ_STAT_FILE

tmpns=""
if [ "$N" -gt 1 ]; then
    for opts in "--mount --propagation private" \
                "--map-root-user --mount --propagation private"; do
        if unshare $opts sh -c 'mount -t tmpfs tmpfs /tmp' >/dev/null 2>&1; then
            tmpns="$opts"; break
        fi
    done
    if [ -z "$tmpns" ]; then
        echo "[WARN] 无法创建私有 /tmp（unshare/mount 不可用），将共享 /tmp 并行运行"
    fi
fi

rm -f "$BLD"/lr_*.profraw kadd_fuzz_stat_*.txt fz_*.log "$BLD/fuzz_report.txt"
rm -rf fuzz_art && mkdir -p fuzz_art

T0=$(date +%s)
pids=""
for w in $(seq 1 "$N"); do
    wart="$FBLD/fuzz_art/$w/"
    mkdir -p "$wart"
    seed=$((w * 7919))
    if [ -n "$tmpns" ]; then
        ( unshare $tmpns bash -c '
              mount -t tmpfs tmpfs /tmp || exit 91
              cd "$1" || exit 92
              exec ./kadd_fuzzer -runs="$2" -seed="$3" -max_len=4096 \
                   -artifact_prefix="$4" -reload=0 -verbosity=1 \
                   -abort_on_error=0
          ' _ "$FBLD" "$RUNS" "$seed" "$wart" ) > "$FBLD/fz_$w.log" 2>&1 &
    else
        ( cd "$FBLD" && exec ./kadd_fuzzer -runs="$RUNS" -seed="$seed" \
              -max_len=4096 -artifact_prefix="$wart" -reload=0 -verbosity=1 \
              -abort_on_error=0 ) > "$FBLD/fz_$w.log" 2>&1 &
    fi
    pids="$pids $!"
done
echo "   已启动 $N 个实例（日志静音），进度（另开终端）: cat $FBLD/kadd_fuzz_stat_*.txt"

rc=0
failinfo=""
for p in $pids; do
    wait "$p" || { ec=$?; failinfo="${failinfo} pid=$p rc=$ec"; rc=1; }
done
[ -z "$failinfo" ] || echo "[FAIL] 失败实例:${failinfo}"

# ----------------------------------------------------------------------------
# [4/6] 长稳报告：汇总每实例状态文件
#       （{ ... } > file 不进子 shell，total 在当前 shell 仍可用于最终判定）
# ----------------------------------------------------------------------------
echo "[4/6] 生成长稳报告 fuzz_report.txt"
total=0
iters=0
eqfail=0
API_NAMES="dfa_engine_tokenize dfa_engine_tokenize_batch \
seq_to_feature seq_to_feature_batch \
model_predict model_predict_batch model_get_feature_dim \
dl_model_predict dl_model_predict_batch dl_model_get_feature_dim \
sqli_detect sqli_detect_batch \
cnn_model_predict cnn_model_predict_batch cnn_model_get_feature_dim \
rf_model_predict rf_model_predict_batch rf_model_get_feature_dim \
inference_predict inference_predict_batch inference_engine_get_feature_dim"
declare -A api_sum
for name in $API_NAMES; do
    api_sum[$name]=0
done
{
    echo "================= KADD Fuzz 等频随机 ${API_CALLS} 长稳报告 ================="
    echo "模式             : $MODE（SVE=$SVE_FLAG）"
    echo "完成时间         : $(date '+%F %T')"
    echo "总耗时(秒)       : $(( $(date +%s) - T0 ))"
    echo "并行实例         : $N × -runs=$RUNS（每迭代 = 完整随机 fuzz + 21 接口等频随机层）"
    echo "状态文件         : $(ls kadd_fuzz_stat_*.txt 2>/dev/null | wc -l) / $N"
    echo "--------------------------------------------------------------------"
    for f in kadd_fuzz_stat_*.txt; do
        [ -f "$f" ] || continue
        line=$(tr -d '\n' < "$f")
        n=$(printf '%s' "$line" | sed 's/.*api_calls>=\([0-9]*\).*/\1/')
        case "$n" in ''|*[!0-9]*) n=0 ;; esac
        m=$(printf '%s' "$line" | sed 's/.*iter=\([0-9]*\).*/\1/')
        case "$m" in ''|*[!0-9]*) m=0 ;; esac
        for tok in $(printf '%s' "$line" | sed 's/.*api=\[//; s/\].*//'); do
            key=${tok%%=*}
            val=${tok#*=}
            case "$val" in ''|*[!0-9]*) continue ;; esac
            if [[ -n "${api_sum[$key]+x}" ]]; then
                api_sum[$key]=$((api_sum[$key] + val))
            fi
        done
        total=$((total + n))
        iters=$((iters + m))
        echo "$f: $line"
    done
    echo "--------------------------------------------------------------------"
    echo "迭代合计         : $iters / $((RUNS * N))"
    echo "等频随机接口调用次数（21 个，单接口口径，selector 额外调用未计入）:"
    for name in $API_NAMES; do
        printf '    %-34s %s\n' "$name" "${api_sum[$name]}"
    done
    minv=""
    allok=1
    for name in $API_NAMES; do
        v=${api_sum[$name]}
        if [[ -z "$minv" || "$v" -lt "$minv" ]]; then
            minv=$v
        fi
        if [ "$v" -lt "$API_CALLS" ]; then
            allok=0
        fi
    done
    echo "最小单接口       : $minv / $API_CALLS  $([ "$allok" -eq 1 ] && echo '[达标]' || echo '[未达标]')"
    echo "接口调用总和     : $total（21 个等频接口合计，参考值）"
    echo "--------------------------------------------------------------------"
    echo "生命周期豁免接口（17 个，不做 ${API_CALLS} 次长跑，仍由随机 fuzz 覆盖）:"
    echo "    config_load / config_free                        配置文件 I/O / 释放语义"
    echo "    dfa_engine_create / dfa_engine_free              真实 DFA 表加载（10.4MB）/ 释放"
    echo "    model_load / model_free                          模型文件 I/O + 解析或 ONNX 会话"
    echo "    dl_model_load / dl_model_free                    模型文件 I/O + 解析或 ONNX 会话"
    echo "    sqli_engine_create_from_file / sqli_engine_free  配置+DFA+双模型完整初始化 / 释放"
    echo "    cnn_model_load / cnn_model_free                  .kcn 文件 I/O + 权重预打包 / 释放"
    echo "    rf_model_load / rf_model_free                    .krf 文件 I/O + HMAC 解析 / 释放"
    echo "    inference_engine_load / inference_engine_load_from_path"
    echo "                                                     ONNX Session 创建（毫秒级，千万次不可行）"
    echo "    inference_engine_free                            释放语义（与 load 成对，单独刷次数无意义）"
    echo "--------------------------------------------------------------------"
    echo "崩溃样本数       : $(find fuzz_art -type f 2>/dev/null | wc -l)（0 = 无崩溃）"
    echo "失败实例         : ${failinfo:-无}"
    echo "实例退出码       : $([ "$rc" -eq 0 ] && echo '全部为 0' || echo '存在非 0，见上方失败实例与 fz_*.log')"
} > "$BLD/fuzz_report.txt"
cat "$BLD/fuzz_report.txt"

# 实例侧句柄健康检查：任一状态文件 eq_ok=0 视为长稳失败（缺资产不能静默降级）
for f in "$FBLD"/kadd_fuzz_stat_*.txt; do
    [ -f "$f" ] || continue
    e=$(tr -d '\n' < "$f" | sed 's/.*eq_ok=\([0-9]*\).*/\1/')
    [ "$e" = "1" ] || { eqfail=1; echo "[FAIL] $f eq_ok=${e:-missing}（等频层句柄缺失，见 fz_*.log 初始化日志）"; }
done

# ----------------------------------------------------------------------------
# [5/6] 覆盖率：合并各实例 profraw（计数相加）→ 文本汇总 + HTML + 门槛判定
#       HTML 中点开函数所在行即可看到 21 个接口 ~3000 万量级的执行次数
#       门槛：每个 C 文件函数覆盖率 100%、行覆盖率 ≥80%（随机 fuzz 主导）
# ----------------------------------------------------------------------------
echo "[5/6] 合并 profraw 并生成覆盖率（运行计数已留存）"
cov_ok=1
if command -v llvm-profdata >/dev/null 2>&1 && command -v llvm-cov >/dev/null 2>&1 \
   && ls "$BLD"/lr_*.profraw >/dev/null 2>&1; then
    llvm-profdata merge -sparse "$BLD"/lr_*.profraw -o "$BLD/lr.profdata" \
        || { echo "[ERROR] llvm-profdata merge 失败"; exit 6; }
    llvm-cov report "$FBLD/kadd_fuzzer" -instr-profile="$BLD/lr.profdata" \
        -ignore-filename-regex="tests/.*|include/.*|util/.*|demo/.*|tools/.*" \
        > "$BLD/lr_coverage.txt" || { echo "[ERROR] llvm-cov report 失败"; exit 6; }
    llvm-cov show "$FBLD/kadd_fuzzer" -instr-profile="$BLD/lr.profdata" \
        -format=html -o "$BLD/lr_cov_html" \
        -ignore-filename-regex="tests/.*|include/.*|util/.*|demo/.*|tools/.*" \
        || { echo "[ERROR] llvm-cov show 失败"; exit 6; }
    echo "   覆盖率汇总（TOTAL 行）:"
    grep -E '^TOTAL' "$BLD/lr_coverage.txt" | sed 's/^/     /' || true
    # 逐 C 文件判定门槛。不同版本 llvm-cov report 列数不同（新版在
    # Functions 与 Lines 之间多一组 Instantiations，共 13 列；旧版 10 列），
    # 但指标组顺序恒定：Regions → Functions → [Instantiations] → Lines。
    # 因此不按列号解析，而是收集所有带 % 后缀的列：
    #   第 2 个百分比恒为函数覆盖率，最后一个百分比恒为行覆盖率。
    cov_fail=$(awk '
        $1 ~ /\.c$/ && NF >= 8 {
            np = 0
            for (i = 2; i <= NF; i++) {
                if ($i ~ /%$/) { pct[++np] = $i }
            }
            if (np < 3) { print "(llvm-cov 输出格式异常: " $1 ")"; next }
            if (pct[2] + 0 < 100) { print $1 " 函数覆盖 " pct[2] " < 100%" }
            if (pct[np] + 0 < 80)  { print $1 " 行覆盖 " pct[np] " < 80%" }
        }
        $1 ~ /\.c$/ && NF < 8 { print "(llvm-cov 列数异常，需人工核对: " $1 ")" }
    ' "$BLD/lr_coverage.txt")
    if [ -n "$cov_fail" ]; then
        cov_ok=0
        echo "[FAIL] 覆盖率门槛未达标（要求：函数 100%，单 C 文件行 ≥80%）:"
        printf '%s\n' "$cov_fail" | sed 's/^/     /'
        printf '%s\n' "$cov_fail" > "$BLD/lr_cov_fail.txt"
    else
        echo "   覆盖率门槛: 全部 C 文件函数 100%、行 ≥80% [达标]"
    fi
    echo "   覆盖率 HTML : $BLD/lr_cov_html/index.html（点开函数所在行可见运行次数）"
else
    cov_ok=0
    echo "[WARN] 未找到 llvm-profdata/llvm-cov 或 lr_*.profraw，跳过覆盖率生成（视为未达标）"
fi

# ----------------------------------------------------------------------------
# [6/6] 打包（报告 + 覆盖率 + 日志/状态/崩溃样本，便于回传留档）
# ----------------------------------------------------------------------------
echo "[6/6] 打包 longrun_${BUILD_DIR}.tar.gz"
( cd "$BLD" && tar -czf "longrun_${BUILD_DIR}.tar.gz" \
    fuzz_report.txt lr.profdata lr_coverage.txt lr_cov_html \
    tests/fz_*.log tests/kadd_fuzz_stat_*.txt tests/fuzz_art 2>/dev/null ) || true
echo "   打包产物 : $BLD/longrun_${BUILD_DIR}.tar.gz"

crashes=$(find "$FBLD/fuzz_art" -type f 2>/dev/null | wc -l)
echo "==================== $BUILD_DIR 完成 ===================="
if [ "$rc" -eq 0 ] && [ "${allok:-0}" -eq 1 ] && [ "$eqfail" -eq 0 ] \
   && [ "$crashes" -eq 0 ] && [ "$cov_ok" -eq 1 ]; then
    echo "[DONE] 长稳达标：21 个等频接口均 ≥ $API_CALLS（最小 ${minv:-0}），覆盖率门槛通过，无崩溃，退出码全 0"
else
    echo "[WARN] 长稳未达标或存在异常（min_api=${minv:-0} / $API_CALLS, iter=$iters/$((RUNS*N)),"
    echo "       crashes=$crashes, rc=$rc, eq_ok_fail=$eqfail, coverage_ok=$cov_ok）"
    echo "       排查: 1) cat $FBLD/fz_*.log 看实例是否启动失败 / 等频句柄是否缺失"
    echo "             2) 状态文件 iter=0 = 未执行迭代；eq_ok=0 = 资产缺失（先 source kadd_env.sh）"
    echo "             3) api_min=0 但 eq_ok=1 = 计数问题（发我分析）"
    echo "             4) coverage_ok=0 = 看 $BLD/lr_cov_fail.txt 的逐文件明细"
    exit 7
fi
