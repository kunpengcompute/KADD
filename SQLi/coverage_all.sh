#!/bin/bash
# ============================================================================
# KADD 一键全量覆盖率脚本（UT + Fuzz × SVE ON/OFF）
# 用法: cd /opt/KADD/SQLi && ./coverage_all.sh [sve|onnx|all]
#   默认 all: 依次跑 SVE ON 和 SVE OFF 两套
# 产物（每套）:
#   build_cov_{sve,onnx}/coverage_html/    UT 覆盖率 HTML (llvm-cov)
#   build_cov_{sve,onnx}/fuzz_cov_html/    Fuzz 覆盖率 HTML (llvm-cov)
#   build_cov_{sve,onnx}/ut_coverage.txt   UT 汇总文本
#   build_cov_{sve,onnx}/fuzz_coverage.txt Fuzz 汇总文本
#   build_cov_{sve,onnx}/coverage_${TAG}.tar.gz  打包产物（UT+Fuzz 合并）
# ============================================================================
set -uo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC="$PROJECT_ROOT/src"
ONNX_HOME="$PROJECT_ROOT/util/onnxruntime-linux-aarch64-1.18.0"
export ONNXRUNTIME_HOME="$ONNX_HOME"
FUZZ_TIME="${FUZZ_TIME:-90}"   # 每次 fuzz 限时秒数，FUZZ_TIME=60 ./coverage_all.sh 可覆盖

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
    cmake --build . -j"$(nproc)" || { echo "[ERROR] $TAG build 失败"; return 1; }

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
        # 用 kadd_sqli_demo 做 llvm-cov report（它链接了所有引擎库，含完整覆盖率映射）
        local GTEST_BIN="$BLD/demo/kadd_sqli_demo"
        [ -x "$GTEST_BIN" ] || GTEST_BIN="$FBLD/dfa_engine_test"
        llvm-cov report "$GTEST_BIN" -instr-profile="$UT_PROFDATA" \
            -ignore-filename-regex="tests/.*|include/.*|util/.*|demo/.*|tools/.*" \
            > "$BLD/ut_coverage.txt" 2>/dev/null
        llvm-cov show "$GTEST_BIN" -instr-profile="$UT_PROFDATA" \
            -format=html -o "$BLD/coverage_html" \
            -ignore-filename-regex="tests/.*|include/.*|util/.*|demo/.*|tools/.*" 2>/dev/null
        echo "   UT 汇总: $BLD/ut_coverage.txt"
        echo "   UT 报告: $BLD/coverage_html/index.html"
    else
        echo "[WARN] $TAG 无 UT profraw 数据，跳过 UT 报告"
    fi

    # ---- Fuzz 覆盖率 (fuzzer 定期 flush profraw → llvm-cov) ----
    echo "[4/6] run kadd_fuzzer (-max_total_time=$FUZZ_TIME)"
    rm -f "$FBLD"/fuzz_*.profraw
    export LLVM_PROFILE_FILE="$FBLD/fuzz_%p.profraw"
    if [ -x "$FBLD/kadd_fuzzer" ]; then
        ( cd "$FBLD" && timeout $((FUZZ_TIME + 30)) ./kadd_fuzzer \
              -max_total_time="$FUZZ_TIME" \
              -abort_on_error=0 -print_final_stats=1 ) \
            || echo "[WARN] $TAG fuzzer 退出码非 0 或超时（继续）"
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
        fuzz_cov_html fuzz_coverage.txt fuzz.profdata 2>/dev/null )
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
