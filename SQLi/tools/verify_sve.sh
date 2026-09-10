#!/bin/bash
# ============================================================================
# SVE 行为矩阵一键验证脚本（编译期场景 1-5 + 产物检查）
#
# 用法:
#   ./tools/verify_sve.sh                    # 项目根目录自动推断
#   ONNXRUNTIME_HOME=/path ./tools/verify_sve.sh
#   ./tools/verify_sve.sh . -DCMAKE_TOOLCHAIN_FILE=xxx.cmake   # 额外参数透传给 cmake
#
# 验证的行为矩阵（SQLi/CMakeLists.txt）:
#   场景1  ON  + aarch64 + 编译器支持 SVE -> SVE 启用, "using SVE native engines"
#   场景2  ON  + aarch64 + 编译器不支持   -> WARNING + "uses the ONNX backend"
#   场景3  ON  + 其他平台(x86 等)         -> WARNING + "uses the ONNX backend"
#   场景4  OFF + 任意平台                 -> "using ONNX backend"
#   场景5  任意 + ONNX Runtime 缺失       -> FATAL_ERROR "ONNX Runtime not found"
#
# 说明:
#   - 本脚本只做 CMake 配置阶段验证（SVE 决策发生在 ONNX 检查之前，
#     因此 ONNX 缺失不影响场景 1-4 的判定，只有整体退出码受影响）
#   - 场景 2 需要不支持 SVE 的老编译器（GCC < 8），
#     与场景 3 共用同一回退分支（仅原因文本不同），可用容器构造
# ============================================================================
set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="${1:-$(dirname "${SCRIPT_DIR}")}"
shift 2>/dev/null || true
EXTRA_CMAKE_ARGS=("$@")

PASS=0; FAIL=0
log()  { printf '%s\n' "$*"; }
pass() { PASS=$((PASS + 1)); log "  [PASS] $*"; }
fail() { FAIL=$((FAIL + 1)); log "  [FAIL] $*"; }

# ---------- 1. 环境探测 ----------
log "==================== 环境探测 ===================="
ARCH="$(uname -m)"
log "  项目目录:          ${PROJECT_ROOT}"
log "  架构:              ${ARCH}"
log "  编译器:            $( (cc --version 2>/dev/null || gcc --version 2>/dev/null) | head -1 )"
log "  cmake:             $(cmake --version 2>/dev/null | head -1)"

SVE_HW=0
if [ "${ARCH}" = "aarch64" ] && [ -r /proc/cpuinfo ]; then
    if grep -qw sve /proc/cpuinfo 2>/dev/null; then
        SVE_HW=1
        log "  硬件 SVE:          支持 (/proc/cpuinfo)"
    else
        log "  硬件 SVE:          不支持 (无 SVE 的 ARM CPU —— 可验证运行期 R3 回退)"
    fi
else
    log "  硬件 SVE:          N/A (非 aarch64 Linux)"
fi

# 向量长度实测（仅 aarch64 + 硬件 SVE 时）
if [ "${SVE_HW}" = "1" ]; then
    VL_DIR="$(mktemp -d)"
    cat > "${VL_DIR}/vl.c" <<'EOF'
#include <stdio.h>
#include <arm_sve.h>
int main(void) { printf("%zu\n", (size_t)svcntb() * 8U); return 0; }
EOF
    if cc -march=armv8-a+sve "${VL_DIR}/vl.c" -o "${VL_DIR}/vl" 2>/dev/null \
       && "${VL_DIR}/vl" > "${VL_DIR}/out" 2>/dev/null; then
        log "  SVE 向量长度:      $(cat "${VL_DIR}/out") bits"
    fi
    rm -rf "${VL_DIR}"
fi
log ""

# ---------- 2. 编译期场景验证 ----------
BUILD_ROOT="$(mktemp -d)"
if [ "${DEBUG_KEEP:-0}" = "1" ]; then
    log "调试模式: 构建目录 ${BUILD_ROOT} 将保留"
    trap '' EXIT
else
    trap 'rm -rf "${BUILD_ROOT}"' EXIT
fi

run_configure() {  # $1=构建目录, 其余为 cmake 参数; 输出退出码, 日志在 <dir>/configure.log
    local dir="$1"; shift
    mkdir -p "${dir}"
    cmake -S "${PROJECT_ROOT}" -B "${dir}" "$@" \
        ${EXTRA_CMAKE_ARGS[@]+"${EXTRA_CMAKE_ARGS[@]}"} \
        > "${dir}/configure.log" 2>&1
    echo $?
}

log "==================== 编译期场景 ===================="

# --- 场景 1/2/3: ENABLE_ARM_SVE=ON（与默认值行为一致）---
DIR_ON="${BUILD_ROOT}/on"
log "--- [测试] ENABLE_ARM_SVE=ON ---"
CODE_ON="$(run_configure "${DIR_ON}" -DENABLE_ARM_SVE=ON)"
LOG_ON="${DIR_ON}/configure.log"

# Decide the target architecture from CMake's compiler-target check, not from
# the host machine. This keeps the script correct for cross compilation.
if grep -q "compiler target is not AArch64" "${LOG_ON}"; then
    TARGET_IS_AARCH64=0
else
    TARGET_IS_AARCH64=1
fi

if grep -q "using SVE native engines" "${LOG_ON}"; then
    if [ "${TARGET_IS_AARCH64}" = "1" ]; then
        pass "场景1 ON + aarch64 + 编译器支持: SVE 启用并明示"
    else
        fail "场景1 异常: 非 aarch64 平台却启用了 SVE（检测逻辑有误）"
    fi
    grep -q "Fixing SVE vector length to 256-bit" "${LOG_ON}" \
        && pass "场景1: 固定 256 位向量宽度" \
        || fail "场景1: 缺少固定位宽提示"
    if [ "${CODE_ON}" != "0" ]; then
        log "  [SKIP] 整体配置因依赖缺失中止（如 ONNX），编译参数检查需完整配置成功"
    elif grep -rq -- "-march=armv8-a+sve" "${DIR_ON}/CMakeFiles" 2>/dev/null; then
        pass "场景1: 编译参数包含 -march=armv8-a+sve"
    else
        fail "场景1: 编译参数缺少 -march=armv8-a+sve"
    fi
elif grep -q "ENABLE_ARM_SVE=ON but SVE is unavailable" "${LOG_ON}"; then
    if ! grep -q "uses the ONNX backend" "${LOG_ON}"; then
        fail "场景2/3 ON: WARNING 缺少 ONNX 后端明示"
    elif [ "${TARGET_IS_AARCH64}" = "1" ]; then
        pass "场景2 ON + aarch64(编译器不支持 SVE): WARNING + 明示 ONNX"
    elif grep -q "not AArch64" "${LOG_ON}"; then
        pass "场景3 ON + 非 aarch64: WARNING(含原因) + 明示 ONNX"
    else
        fail "场景3 ON: 回退原因不正确"
    fi
else
    fail "场景1/2/3 ON: 未出现任何预期的 SVE 决策信息"
fi
log ""

# --- 场景 4: OFF ---
DIR_OFF="${BUILD_ROOT}/off"
log "--- [测试] ENABLE_ARM_SVE=OFF ---"
run_configure "${DIR_OFF}" -DENABLE_ARM_SVE=OFF > /dev/null
LOG_OFF="${DIR_OFF}/configure.log"
grep -q "ARM SVE disabled (ENABLE_ARM_SVE=OFF): using ONNX backend" "${LOG_OFF}" \
    && pass "场景4 OFF: 明示 using ONNX backend" \
    || fail "场景4 OFF: 输出不符合预期"
if grep -rq -- "-march=armv8-a+sve" "${DIR_OFF}/CMakeFiles" 2>/dev/null; then
    fail "场景4 OFF: 不应出现 SVE 编译参数"
else
    pass "场景4 OFF: 无 SVE 编译参数（跳过检测）"
fi
log ""

# --- 场景 5: ONNX Runtime 缺失 ---
DIR_NOONNX="${BUILD_ROOT}/noonnx"
log "--- [测试] ONNX Runtime 缺失（无论 SVE 取值都应编译失败）---"
run_configure "${DIR_NOONNX}" -DENABLE_ARM_SVE=ON -DONNXRUNTIME_ROOT=/nonexistent > /dev/null
LOG_NO="${DIR_NOONNX}/configure.log"
if grep -q "ONNX Runtime not found" "${LOG_NO}" && grep -qi "CMake Error" "${LOG_NO}"; then
    pass "场景5 ONNX 缺失: FATAL_ERROR, 编译失败"
else
    fail "场景5 ONNX 缺失: 未按预期失败"
fi
log ""

# ---------- 3. 产物 SVE 指令检查（可选，ON 配置整体成功时）----------
log "==================== 产物检查（可选） ===================="
if grep -q "using SVE native engines" "${LOG_ON}"; then
    if cmake --build "${DIR_ON}" --target kadd_cnn_engine > "${DIR_ON}/build.log" 2>&1; then
        CNN_LIB="$(find "${DIR_ON}" -name "libkadd_cnn_engine*" -type f | head -1)"
        if [ -n "${CNN_LIB}" ] && objdump -d "${CNN_LIB}" 2>/dev/null \
           | grep -qE '\b(cntb|cntw|whilelt|ld1w|st1w)\b'; then
            pass "产物包含真实 SVE 指令 (${CNN_LIB})"
        else
            fail "未在产物中找到 SVE 指令（或 objdump 不可用）"
        fi
    else
        log "  [SKIP] 构建未完成（可能缺少 ONNX/cJSON 依赖），跳过产物指令检查"
        log "         构建日志: ${DIR_ON}/build.log"
    fi
else
    log "  [SKIP] 当前环境未启用 SVE，跳过产物指令检查"
fi
log ""

# ---------- 汇总 ----------
log "==================== 汇总: PASS=${PASS} FAIL=${FAIL} ===================="
if [ "${FAIL}" -eq 0 ]; then
    log "全部通过"
    exit 0
else
    log "存在失败项，构建日志保留于 ${BUILD_ROOT}（可用 DEBUG_KEEP=1 重新运行保留）"
    exit 1
fi
