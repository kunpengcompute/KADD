#!/bin/bash
# ============================================================================
# KADD 一键全量覆盖率脚本（UT + Fuzz × SVE ON/OFF）
# 用法: cd /opt/KADD/SQLi && ./coverage_all.sh [sve|onnx|all]
#   默认 all: 依次跑 SVE ON 和 SVE OFF 两套
# 环境变量:
#   FUZZ_TIME=<秒>  每个 fuzz worker 的限时（默认 90）
#   FUZZ_JOBS=<N>    并行 fuzz 进程数（默认 1 = 串行，行为与以前完全一致）
#                    N>1 时每个 worker 获得一份私有 /tmp（mount namespace
#                    + tmpfs + private 传播），彼此不干扰；内核不允许时
#                    自动回退串行并告警。总墙钟时间仍约为 FUZZ_TIME，
#                    迭代总量约为单路的 N 倍。
#                    注意：每个 worker 各自使用 KADD_ORT_INTRA_OP_THREADS 个
#                    ORT 线程，总线程数 ≈ N × intra，请按核数选 N。
#   例: FUZZ_JOBS=4 FUZZ_TIME=120 ./coverage_all.sh sve
#
# 21 个可长跑开放接口「单接口」3000 万等频随机长稳（SVE / ONNX 可并存）:
#   ./tools/fuzz_longrun.sh [sve|onnx]
#   环境变量:
#     N=32                并行实例数
#     API_CALLS=30000000  单个等频接口的目标调用次数
#   该脚本会自动设置 KADD_FUZZ_EQUAL_RANDOM=1：selector / heavy /
#   fault-inject 随机 fuzz 全部保留，另叠加 21 个可长跑接口的等频
#   真实随机调用层（有效句柄 + 随机输入）；17 个生命周期类接口
#   （文件 I/O / 模型解析 / ONNX 会话创建 / 释放语义）明确豁免，
#   并生成带运行计数的覆盖率 HTML 与覆盖率门槛判定。
# 产物（每套）:
#   build_cov_{sve,onnx}/coverage_html/    UT 覆盖率 HTML (llvm-cov)
#   build_cov_{sve,onnx}/fuzz_cov_html/    Fuzz 覆盖率 HTML (llvm-cov)
#   build_cov_{sve,onnx}/ut_coverage.txt   UT 汇总文本
#   build_cov_{sve,onnx}/fuzz_coverage.txt Fuzz 汇总文本
#   build_cov_{sve,onnx}/fuzz.log          Fuzz 完整日志（正常不回显，异常时自动打印）
#   build_cov_{sve,onnx}/coverage_${TAG}.tar.gz  打包产物（UT+Fuzz 合并）
# ============================================================================
set -uo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC="$PROJECT_ROOT/src"
ONNXRT_VERSION="1.28.0"
ONNX_HOME="$PROJECT_ROOT/util/onnxruntime-linux-aarch64-${ONNXRT_VERSION}"
export ONNXRUNTIME_HOME="$ONNX_HOME"
# ONNX Runtime 线程数（性能相关）：模型很小，线程过多反而被调度开销拖垮。
# 实测 intra=8 / inter=1 最优（CNN 吞吐 1.6k -> 13.6k+ samples/sec），可按机型覆盖。
export KADD_ORT_INTRA_OP_THREADS="${KADD_ORT_INTRA_OP_THREADS:-8}"
export KADD_ORT_INTER_OP_THREADS="${KADD_ORT_INTER_OP_THREADS:-1}"
FUZZ_TIME="${FUZZ_TIME:-90}"   # 每次 fuzz 限时秒数，FUZZ_TIME=60 ./coverage_all.sh 可覆盖
FUZZ_JOBS="${FUZZ_JOBS:-1}"   # 并行 fuzz 进程数，FUZZ_JOBS=4 ./coverage_all.sh 可覆盖

MODE="${1:-all}"

run_one() {
    local SVE_FLAG="$1" BUILD_DIR="$2"
    local TAG
    TAG=$(basename "$BUILD_DIR")
    echo "==================== $TAG (SVE=$SVE_FLAG) ===================="

    local BLD="$PROJECT_ROOT/$BUILD_DIR"
    mkdir -p "$BLD"
    cd "$BLD"

    echo "[1/6] cmake configure (clang + coverage)"
    cmake -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
          -DCMAKE_BUILD_TYPE=Release \
          -DENABLE_CODE_COVERAGE=ON \
          -DENABLE_ARM_SVE="$SVE_FLAG" \
          "$PROJECT_ROOT" || { echo "[ERROR] $TAG configure 失败"; return 1; }

    echo "[2/6] cmake build"
    if ! cmake --build . -j"$(nproc)"; then
        # kadd_fuzzer 默认开启 ORT 故障注入（内联 kadd_inference_engine.c），
        # 个别工具链组合下可能编译失败：关闭注入后重试一次（只损失 inference
        # 引擎约 25 行 ORT 错误清理分支的 fuzz 覆盖，UT 侧注入仍保留）。
        echo "[WARN] $TAG build 失败，尝试 KADD_FUZZ_INFERENCE_INJECT=OFF 重新构建"
        cmake -DKADD_FUZZ_INFERENCE_INJECT=OFF "$PROJECT_ROOT" \
            || { echo "[ERROR] $TAG configure 失败"; return 1; }
        cmake --build . -j"$(nproc)" || { echo "[ERROR] $TAG build 失败"; return 1; }
    fi

    export LD_LIBRARY_PATH="$ONNX_HOME/lib:${LD_LIBRARY_PATH:-}"
    local FBLD="$BLD/tests"

    # ---- UT 覆盖率 (clang -fprofile-instr-generate → profraw → llvm-cov) ----
    echo "[3/6] run gtest UTs + llvm-cov capture"
    rm -f "$FBLD"/*.profraw
    export LLVM_PROFILE_FILE="$FBLD/ut_%p.profraw"
    ( cd "$FBLD" && ctest --output-on-failure ) || echo "[WARN] $TAG ctest 有失败（继续出覆盖率）"
    export LLVM_PROFILE_FILE=""

    if ls "$FBLD"/ut_*.profraw >/dev/null 2>&1; then
        local UT_PROFDATA="$BLD/ut.profdata"
        llvm-profdata merge -sparse "$FBLD"/ut_*.profraw -o "$UT_PROFDATA" 2>/dev/null
        # [FIX] 此前只用 kadd_sqli_demo 单二进制做映射：6 个 `#include "*.c"` 型
        # gtest（config/dfa/feature/inference/ml/sqli）的 profiling 记录哈希与
        # demo 中库副本不一致，其覆盖数据被 llvm-cov 静默丢弃——表现为 ONNX UT
        # 报告缺少 cnn/rf 文件、各引擎错误分支覆盖率整体偏低。
        # 现改为把所有 UT 可执行二进制逐个以 -object 传入 llvm-cov，合并全部
        # TU 的覆盖数据（首个二进制仍作主映射参数，fuzzer 单独统计不混入）。
        local UT_FIRST="" UT_OBJECTS=()
        local bin
        for bin in "$BLD"/demo/kadd_sqli_demo "$FBLD"/*_test "$FBLD"/*_gtest; do
            [ -x "$bin" ] && [ ! -d "$bin" ] || continue
            case "$(basename "$bin")" in
                kadd_fuzzer) continue ;;
            esac
            if [ -z "$UT_FIRST" ]; then
                UT_FIRST="$bin"
            else
                UT_OBJECTS+=(-object "$bin")
            fi
        done
        if [ -z "$UT_FIRST" ]; then
            echo "[WARN] $TAG 未找到任何 UT 二进制，跳过 UT 报告"
        else
            llvm-cov report "$UT_FIRST" ${UT_OBJECTS[@]+"${UT_OBJECTS[@]}"} -instr-profile="$UT_PROFDATA" \
                -ignore-filename-regex="tests/.*|include/.*|util/.*|demo/.*|tools/.*" \
                > "$BLD/ut_coverage.txt" 2>/dev/null
            llvm-cov show "$UT_FIRST" ${UT_OBJECTS[@]+"${UT_OBJECTS[@]}"} -instr-profile="$UT_PROFDATA" \
                -format=html -o "$BLD/coverage_html" \
                -ignore-filename-regex="tests/.*|include/.*|util/.*|demo/.*|tools/.*" 2>/dev/null
            echo "   UT 汇总: $BLD/ut_coverage.txt（映射二进制 $(( ${#UT_OBJECTS[@]} / 2 + 1 )) 个）"
            echo "   UT 报告: $BLD/coverage_html/index.html"
        fi
    else
        echo "[WARN] $TAG 无 UT profraw 数据，跳过 UT 报告"
    fi

    # ---- Fuzz 覆盖率 (fuzzer 定期 flush profraw → llvm-cov) ----
    # 支持多进程并行（FUZZ_JOBS>1）。并行的前提是给每个 worker 一份私有 /tmp：
    # fuzzer 的合成资产是硬编码绝对路径（/tmp/kadd_fz_*、/tmp/fz_*、
    # /tmp/fz3_*、/tmp/fz4_*），多进程共用同一 /tmp 会互相覆写——
    #   · BuildSyntheticAssets() 在 init 阶段重写 /tmp/kadd_fz_*，而
    #     selector 的 DFA / Config 用例每次迭代都在读它们；
    #   · error_paths_cases.inc 在热循环里反复重写 /tmp/fz_*。
    # 撕裂读会把本该成功的加载路径随机翻成错误路径，既污染覆盖率又制造假崩溃。
    # 因此用 mount namespace + tmpfs 给每个 worker 独立 /tmp。/tmp 下全部文件都由
    # fuzzer 自己生成；真实模型资产走相对路径、profraw 与 artifact 走构建
    # 目录绝对路径，均不受 namespace 影响。--propagation private 必须带：
    # systemd 下 / 默认是 shared mount，不加会把 tmpfs 反向传播回宿主 /tmp。
    # 不用 libFuzzer 自带的 -jobs：它不隔离 /tmp，且其“跑完即重启、直到
    # 发现 bug”的语义会让 -max_total_time 失去总时长上界。
    # fuzzer 默认静音（初始化后 stdout+stderr → /dev/null，进度只写单行状态文件）。
    # 覆盖率跑需要 libFuzzer 的 Done/SUMMARY/stat:: 与崩溃诊断，这里显式保留输出：
    # worker 输出已全部重定向到 fuzz_w*.log，正常结束只回显摘要，不会刷屏。
    export KADD_FUZZ_KEEP_STDOUT=1
    rm -f "$FBLD"/fuzz_*.profraw "$FBLD"/kadd_fuzz_stat_*.txt
    export LLVM_PROFILE_FILE="$FBLD/fuzz_%p.profraw"   # %p=PID，多 worker 天然不冲突
    local FUZZ_LOG="$BLD/fuzz.log"
    if [ -x "$FBLD/kadd_fuzzer" ]; then
        local jobs opts tmpns=""
        case "$FUZZ_JOBS" in
            ''|*[!0-9]*) jobs=1 ;;
            *)           jobs="$FUZZ_JOBS" ;;
        esac
        [ "$jobs" -ge 1 ] || jobs=1
        if [ "$jobs" -gt 1 ]; then
            for opts in "--mount --propagation private" \
                        "--map-root-user --mount --propagation private"; do
                if unshare $opts sh -c 'mount -t tmpfs tmpfs /tmp' >/dev/null 2>&1; then
                    tmpns="$opts"; break
                fi
            done
            if [ -z "$tmpns" ]; then
                echo "[WARN] $TAG 无法为并行 worker 创建私有 /tmp（unshare/mount 不可用），回退串行"
                jobs=1
            fi
        fi
        echo "[4/6] run kadd_fuzzer (-max_total_time=$FUZZ_TIME, jobs=$jobs)"
        rm -rf "$BLD/fuzz_artifacts" "$BLD"/fuzz_w*.log
        mkdir -p "$BLD/fuzz_artifacts"
        # fuzz 单次运行会产生数万行日志，这里全部重定向到文件：
        # 退出码为 0 时只回显结论摘要，非 0（崩溃/超时）才打印全文便于排查。
        local pids=() w wlog wart seed
        for ((w = 1; w <= jobs; w++)); do
            wlog="$BLD/fuzz_w${w}.log"
            # artifact 目录必须在宿主路径：放 /tmp 会随私有 namespace 退出而丢失
            wart="$BLD/fuzz_artifacts/w${w}/"
            mkdir -p "$wart"
            seed=$((w * 7919))   # 各 worker 不同起点，避免重复探索同一空间
            if [ -n "$tmpns" ]; then
                ( timeout $((FUZZ_TIME + 30)) unshare $tmpns bash -c '
                      mount -t tmpfs tmpfs /tmp || exit 91
                      cd "$1" || exit 92
                      exec ./kadd_fuzzer -max_total_time="$2" -seed="$3" \
                           -artifact_prefix="$4" -abort_on_error=0 -print_final_stats=1
                  ' _ "$FBLD" "$FUZZ_TIME" "$seed" "$wart" ) > "$wlog" 2>&1 &
            else
                ( cd "$FBLD" && timeout $((FUZZ_TIME + 30)) ./kadd_fuzzer \
                      -max_total_time="$FUZZ_TIME" -seed="$seed" \
                      -artifact_prefix="$wart" -abort_on_error=0 -print_final_stats=1 ) > "$wlog" 2>&1 &
            fi
            pids+=("$!")
        done
        local fuzz_rc=0 i rc
        for i in "${!pids[@]}"; do
            rc=0; wait "${pids[$i]}" || rc=$?
            echo "   worker $((i + 1))/$jobs rc=$rc  log: $BLD/fuzz_w$((i + 1)).log"
            if [ "$rc" -ne 0 ]; then fuzz_rc="$rc"; fi
        done
        # 合并各 worker 日志为 fuzz.log（产物名与打包内容保持不变）
        : > "$FUZZ_LOG"
        for ((w = 1; w <= jobs; w++)); do
            [ -f "$BLD/fuzz_w${w}.log" ] || continue
            { echo "########## fuzz worker ${w}/${jobs} ##########"
              cat "$BLD/fuzz_w${w}.log"; } >> "$FUZZ_LOG"
        done
        if [ "$fuzz_rc" -eq 0 ]; then
            echo "   fuzz 正常结束 rc=0（$jobs 个 worker），完整日志: $FUZZ_LOG ($(grep -c '' "$FUZZ_LOG") 行)"
            grep -E '^(Done|SUMMARY:|INFO: seed corpus)' "$FUZZ_LOG" | tail -n $((3 * jobs)) | sed 's/^/     /'
            grep -E '^stat::(average_exec_per_sec|new_units_added|peak_rss_mb):' "$FUZZ_LOG" | tail -n $((3 * jobs)) | sed 's/^/     /'
        else
            echo "[WARN] $TAG fuzzer 退出码 $fuzz_rc（崩溃或超时），完整日志: $FUZZ_LOG"
            cat "$FUZZ_LOG"
        fi
    else
        echo "[WARN] $TAG 未找到 kadd_fuzzer，跳过 fuzz 覆盖"
    fi
    export LLVM_PROFILE_FILE=""

    # ---- llvm-cov 报告 ----
    echo "[5/6] llvm-cov fuzz report"
    if ls "$FBLD"/fuzz_*.profraw >/dev/null 2>&1; then
        local FUZZ_PROFDATA="$BLD/fuzz.profdata"
        llvm-profdata merge -sparse "$FBLD"/fuzz_*.profraw -o "$FUZZ_PROFDATA" 2>/dev/null
        llvm-cov report "$FBLD/kadd_fuzzer" -instr-profile="$FUZZ_PROFDATA" \
            -ignore-filename-regex="tests/.*|include/.*|util/.*|demo/.*|tools/.*" \
            > "$BLD/fuzz_coverage.txt" 2>/dev/null
        llvm-cov show "$FBLD/kadd_fuzzer" -instr-profile="$FUZZ_PROFDATA" \
            -format=html -o "$BLD/fuzz_cov_html" \
            -ignore-filename-regex="tests/.*|include/.*|util/.*|demo/.*|tools/.*" 2>/dev/null
        echo "   Fuzz 汇总: $BLD/fuzz_coverage.txt"
        echo "   Fuzz HTML : $BLD/fuzz_cov_html/index.html"
    else
        echo "[WARN] $TAG 无 fuzz profraw 数据，跳过 fuzz 报告"
    fi

    # ---- 打包（UT + Fuzz 合并为一个 tar） ----
    echo "[6/6] tar 打包"
    ( cd "$BLD" && tar -czf "coverage_${TAG}.tar.gz" \
        coverage_html ut_coverage.txt ut.profdata \
        fuzz_cov_html fuzz_coverage.txt fuzz.profdata fuzz.log \
        fuzz_artifacts 2>/dev/null )
    echo "   覆盖率包: $BLD/coverage_${TAG}.tar.gz"

    echo "==================== $TAG 完成 ===================="
    echo ""
}

case "$MODE" in
    sve)  run_one ON   build_cov_sve ;;
    onnx) run_one OFF  build_cov_onnx ;;
    all)  run_one ON   build_cov_sve
          run_one OFF  build_cov_onnx
          # 合并两份 fuzz_coverage.txt 便于对比
          echo "==================== 汇总对比 ===================="
          echo "--- SVE ON (build_cov_sve) ---"
          cat "$PROJECT_ROOT/build_cov_sve/fuzz_coverage.txt" 2>/dev/null
          echo ""
          echo "--- SVE OFF (build_cov_onnx) ---"
          cat "$PROJECT_ROOT/build_cov_onnx/fuzz_coverage.txt" 2>/dev/null ;;
    *) echo "用法: $0 [sve|onnx|all]"; exit 2 ;;
esac

echo ""
echo "[DONE] 全部覆盖率生成完成"
echo "   SVE ON  : build_cov_sve/coverage_build_cov_sve.tar.gz (UT+Fuzz)"
echo "   SVE OFF : build_cov_onnx/coverage_build_cov_onnx.tar.gz (UT+Fuzz)"
