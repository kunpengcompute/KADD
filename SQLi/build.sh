#!/bin/bash
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# All rights reserved.
#
# Licensed under the License;
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://opensource.org/license/bsd-3-clause
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ============================================================================
# KADD 一键构建脚本
# 自动准备 util/ 依赖（ONNX Runtime / cJSON / Google Test）+ CMake 编译 src/demo/tests
#
# 用法：cd /opt/KADD/SQLi && ./build.sh [all|fuzz]
#   all   （默认）用默认编译器构建 src + demo + tests，产物在 build/
#         注意：kadd_fuzzer 只在 Clang 下才会被 CMake 配置（FUZZ_TESTS_AVAILABLE），
#         所以 gcc 环境下跑 all 不会产出 fuzzer，这是预期行为。
#   fuzz  用 clang + libFuzzer 构建长稳专用的 kadd_fuzzer，按后端分目录产出：
#           SVE=ON （默认）→ build_fuzz_sve/tests/kadd_fuzzer   原生 RF/CNN + SVE
#           SVE=OFF        → build_fuzz_onnx/tests/kadd_fuzzer  ONNX Runtime 后端
#         两套可同时存在、互不覆盖（共用一个目录切 SVE 会复用 CMake 缓存，
#         既拖慢构建又容易跑错后端）。
#         环境变量：
#           SVE=ON|OFF        ARM SVE 加速开关（默认 ON；OFF 走 ONNX 后端）
#                             若 ON 但当前 CPU 不支持 SVE，自动回退 ONNX 后端并提示
#           FUZZ_PROFILE=0|1  1=带覆盖率插桩；默认 0=不插桩（长稳吞吐更高）
#                             覆盖率报告请用 coverage_all.sh，不要用本模式
#         例：SVE=OFF ./build.sh fuzz
#
# fuzzer 初始化完成后默认静音（stdout/stderr → /dev/null），长稳进度看
# <build_fuzz_*>/tests/kadd_fuzz_stat_<pid>.txt；
# 21 个可长跑接口等频随机长稳请使用 tools/fuzz_longrun.sh
# （自动设置 KADD_FUZZ_EQUAL_RANDOM=1，随机 fuzz 全保留 + 等频层叠加）。
# ============================================================================
set -e

# ----------------------------------------------------------------------------
# SVE CPU 能力检测
# ----------------------------------------------------------------------------
sve_cpu_supported() {
    local cpu_arch
    cpu_arch="$(uname -m 2>/dev/null || true)"
    case "$cpu_arch" in
        aarch64|arm64) ;;
        *) return 1 ;;
    esac

    if command -v lscpu >/dev/null 2>&1; then
        lscpu 2>/dev/null | grep -qw sve
    else
        grep -m1 '^Flags' /proc/cpuinfo 2>/dev/null | grep -qw sve
    fi
}

# 注：构建环境可能缺少 CA 证书，所有 wget 统一加 --no-check-certificate；不使用静默参数，保留下载进度输出
PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
UTIL_DIR="${PROJECT_ROOT}/util"
mkdir -p "${UTIL_DIR}"

MODE="${1:-all}"
SVE_REQUESTED="${SVE:-ON}"
SVE="${SVE_REQUESTED}"
if [ "$SVE" = "ON" ] && ! sve_cpu_supported; then
    SVE="OFF"
    echo "[WARN] SVE=ON 但当前 CPU 不支持 SVE，已回退 ONNX 后端"
fi
case "$MODE" in
    all)  BUILD_DIR="build" ;;
    # fuzz 产物按后端分目录：SVE / ONNX 两套 fuzzer 可并存，互不覆盖 CMake 缓存
    fuzz) if [ "$SVE" = "OFF" ]; then BUILD_DIR="build_fuzz_onnx"; else BUILD_DIR="build_fuzz_sve"; fi ;;
    *)    echo "[ERROR] 未知模式：$MODE（可选 all | fuzz）"; exit 2 ;;
esac
FUZZ_PROFILE="${FUZZ_PROFILE:-0}"

# ----------------------------------------------------------------------------
# cJSON（源码内嵌方式：放置 cJSON.h / cJSON.c 到 util/cJSON）
# ----------------------------------------------------------------------------
if [ ! -f "${UTIL_DIR}/cJSON/cJSON.c" ]; then
    echo "[1/3] 准备 cJSON..."
    cd "${UTIL_DIR}"
    wget --no-check-certificate https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.19.zip -O cJSON-1.7.19.zip
    unzip cJSON-1.7.19.zip
    mkdir -p cJSON
    cp cJSON-1.7.19/cJSON.h cJSON-1.7.19/cJSON.c cJSON/
    rm -rf cJSON-1.7.19 cJSON-1.7.19.zip
fi

# ----------------------------------------------------------------------------
# ONNX Runtime（ML 推理必需）：先 GitHub release，失败自动回退 NuGet 官方发行包
# （均为发行包，不做源码编译；ONNXRT_URL 可覆盖 GitHub 地址，供内网镜像使用）
# ----------------------------------------------------------------------------
ONNXRT_VERSION="1.28.0"
ONNXRT_DIR="${UTIL_DIR}/onnxruntime-linux-aarch64-${ONNXRT_VERSION}"
if [ ! -d "${ONNXRT_DIR}" ]; then
    echo "[2/3] 准备 ONNX Runtime ${ONNXRT_VERSION}..."
    cd "${UTIL_DIR}"
    TGZ="onnxruntime-linux-aarch64-${ONNXRT_VERSION}.tgz"
    URL="${ONNXRT_URL:-https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRT_VERSION}/${TGZ}}"
    if wget --no-check-certificate "${URL}" -O "${TGZ}" && tar xzf "${TGZ}" 2>/dev/null; then
        rm -f "${TGZ}"
    else
        echo "[WARN] GitHub 通道不可用，改用 NuGet 官方发行包..."
        rm -f "${TGZ}"
        NUPKG="microsoft.ml.onnxruntime.${ONNXRT_VERSION}.nupkg"
        wget --no-check-certificate "https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/${ONNXRT_VERSION}/${NUPKG}" -O "${NUPKG}"
        unzip -q "${NUPKG}" -d ort_tmp
        mkdir -p "${ONNXRT_DIR}/include" "${ONNXRT_DIR}/lib"
        cp ort_tmp/build/native/include/* "${ONNXRT_DIR}/include/"
        cp ort_tmp/runtimes/linux-arm64/native/*.so "${ONNXRT_DIR}/lib/"
        # 包内 .so 的 SONAME 带版本号，补软链接
        (cd "${ONNXRT_DIR}/lib" && ln -sf libonnxruntime.so libonnxruntime.so.1 && ln -sf libonnxruntime.so "libonnxruntime.so.${ONNXRT_VERSION}")
        rm -rf ort_tmp "${NUPKG}"
    fi
fi
export ONNXRUNTIME_HOME="${ONNXRT_DIR}"

# ----------------------------------------------------------------------------
# Google Test（可选，仅构建单元测试时需要）
# ----------------------------------------------------------------------------
if [ ! -d "${UTIL_DIR}/gtest" ]; then
    echo "[3/3] 准备 Google Test..."
    cd "${UTIL_DIR}"
    wget --no-check-certificate https://github.com/google/googletest/archive/refs/tags/release-1.12.1.tar.gz
    tar xzf release-1.12.1.tar.gz
    mkdir -p gtest
    cp -r googletest-release-1.12.1/googletest/include gtest/
    cp -r googletest-release-1.12.1/googletest/src gtest/
    rm -rf googletest-release-1.12.1 release-1.12.1.tar.gz
fi
export GTEST_HOME="${UTIL_DIR}/gtest"

# ----------------------------------------------------------------------------
# CMake 一键构建（src + demo + tests；fuzz 模式额外产出 kadd_fuzzer）
# ----------------------------------------------------------------------------
cd "${PROJECT_ROOT}"
mkdir -p "${BUILD_DIR}" && cd "${BUILD_DIR}"

if [ "$MODE" = "fuzz" ]; then
    command -v clang++ >/dev/null 2>&1 || {
        echo "[ERROR] fuzz 模式需要 clang++（libFuzzer 只在 Clang 下可用）"
        echo "        安装后重试，或直接用 ./coverage_all.sh（它也是 clang 构建）"
        exit 3
    }
    if [ "$FUZZ_PROFILE" = "1" ]; then FUZZ_PROFILE_FLAG="ON"; else FUZZ_PROFILE_FLAG="OFF"; fi
    echo "开始 CMake 构建（fuzz 模式：clang + libFuzzer, SVE=$SVE, 插桩=$FUZZ_PROFILE_FLAG）..."
    cmake -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
          -DKADD_OPTIMIZE=ON -DCMAKE_BUILD_TYPE=Release \
          -DENABLE_ARM_SVE="$SVE" \
          -DENABLE_CODE_COVERAGE=OFF \
          -DKADD_FUZZ_PROFILE="$FUZZ_PROFILE_FLAG" ..
    if ! cmake --build . -j"$(nproc)"; then
        # 与 coverage_all.sh 一致的回退：关闭 ORT 故障注入后重试一次
        echo "[WARN] fuzz 构建失败，尝试 KADD_FUZZ_INFERENCE_INJECT=OFF 重新构建"
        cmake -DKADD_FUZZ_INFERENCE_INJECT=OFF ..
        cmake --build . -j"$(nproc)"
    fi
else
    echo "开始 CMake 构建..."
    cmake -DKADD_OPTIMIZE=ON -DCMAKE_BUILD_TYPE=Release -DENABLE_ARM_SVE="$SVE" ..
    cmake --build .
fi

# 运行环境：kadd 动态库 + onnxruntime.so
export LD_LIBRARY_PATH="${PROJECT_ROOT}/${BUILD_DIR}/src:${ONNXRUNTIME_HOME}/lib:${LD_LIBRARY_PATH}"

# ----------------------------------------------------------------------------
# 模型 HMAC 密钥（.krf/.kcn 强制 HMAC-SHA256 认证，密钥缺失/不匹配拒绝加载）
# 优先级：环境变量 KADD_MODEL_HMAC_KEY（或旧名 KADD_RF_HMAC_KEY）> config.json model_hmac_key
# 仓库不落明文密钥：未配置环境变量时每次构建随机生成密钥，用它重签 build/ 下
# 模型副本（rf_sign.py --resign，仓库 assets 不动），并写入 build/config.json
# （chmod 600，勿入库），构建完即可直接 demo。测试自带密钥与资产，不受影响。
# 生产：export KADD_MODEL_HMAC_KEY=$(openssl rand -hex 32) && ./build.sh
# 手动签名/校验：python3 tools/rf_sign/rf_sign.py --help
# ----------------------------------------------------------------------------
command -v python3 >/dev/null 2>&1 || { echo "❌ 需要 python3（模型签名 / 写入 config）"; exit 1; }
KADD_EFFECTIVE_KEY="${KADD_MODEL_HMAC_KEY:-${KADD_RF_HMAC_KEY:-}}"
if [ -z "${KADD_EFFECTIVE_KEY}" ]; then
    KADD_EFFECTIVE_KEY="$(python3 -c 'import secrets; print(secrets.token_hex(32))')"
    echo "未配置密钥，已为本次构建随机生成（仅供构建/测试）"
fi
export KADD_MODEL_HMAC_KEY="${KADD_EFFECTIVE_KEY}"

# 用生效密钥重签 build/ 模型副本（demo 从 build/assets 读取）
for M in "${PROJECT_ROOT}/${BUILD_DIR}/assets/model/"*.krf \
         "${PROJECT_ROOT}/${BUILD_DIR}/assets/model/"*.kcn; do
    [ -f "${M}" ] || continue
    python3 "${PROJECT_ROOT}/tools/rf_sign/rf_sign.py" --in-place --resign "${M}" || exit 1
done

# 密钥写入构建配置，原生模型指向 build/ 副本（demo 直接读取，无需手动 export）
CFG="${PROJECT_ROOT}/${BUILD_DIR}/config.json"
if [ -f "${CFG}" ]; then
    KADD_CFG="${CFG}" KADD_KEY="${KADD_EFFECTIVE_KEY}" python3 -c '
import json, os
p = os.environ["KADD_CFG"]
with open(p, encoding="utf-8") as f:
    cfg = json.load(f)
cfg["model_hmac_key"] = os.environ["KADD_KEY"]
base = os.path.join(os.path.dirname(p), "assets", "model")
cfg["model_rf"] = os.path.join(base, "model_tokens_rf.krf")
cfg["model_cnn"] = os.path.join(base, "model_tokens_dl_cnn.kcn")
with open(p, "w", encoding="utf-8") as f:
    json.dump(cfg, f, ensure_ascii=False, indent=2)
    f.write("\n")
'
    chmod 600 "${CFG}"
fi

# 运行环境文件：仅库搜索路径（密钥已在 config.json）
{
    echo "# 由 build.sh 生成：运行 demo 前先 source（仅库搜索路径；模型密钥在 config.json）"
    echo "export LD_LIBRARY_PATH=\"${PROJECT_ROOT}/${BUILD_DIR}/src:${ONNXRUNTIME_HOME}/lib:\${LD_LIBRARY_PATH:-}\""
} > "${PROJECT_ROOT}/${BUILD_DIR}/kadd_env.sh"
echo "   模型密钥 : 已写入 ${BUILD_DIR}/config.json（chmod 600，勿入库；生产请 export KADD_MODEL_HMAC_KEY）"

echo ""
if [ "$MODE" = "fuzz" ]; then
    echo "✅ fuzz 构建完成（SVE=$SVE → ${BUILD_DIR}，覆盖率插桩=$FUZZ_PROFILE_FLAG）"
    echo "   运行环境  : source ${BUILD_DIR}/kadd_env.sh   # 仅库搜索路径（模型密钥已在 config.json）"
    echo "   交互验证  : cd ${BUILD_DIR}/tests && KADD_FUZZ_KEEP_STDOUT=1 ./kadd_fuzzer -max_total_time=90"
    echo "                 看 kadd_fuzz_stat 行的 win_exec_s（稳态吞吐）"
    echo "   长稳运行  : 不加 KADD_FUZZ_KEEP_STDOUT —— 初始化后 stdout/stderr 静音，"
    echo "                 进度看 ${BUILD_DIR}/tests/kadd_fuzz_stat_<pid>.txt（每 10s/4096 次迭代覆盖写，-runs 跑满时写最终值），"
    echo "                 崩溃样本落在 -artifact_prefix 目录，"
    echo "                 复现：KADD_FUZZ_KEEP_STDOUT=1 ./kadd_fuzzer <crash 文件> -runs=1"
    echo "   ONNX 后端 : SVE=OFF ./build.sh fuzz → build_fuzz_onnx/tests/kadd_fuzzer"
    echo "   等频长稳  : ./tools/fuzz_longrun.sh [sve|onnx]  # 21个可长跑接口等频随机长稳"
    echo "   计数留存  : ./tools/fuzz_longrun.sh [sve|onnx]  # 插桩版单接口3000万长稳，"
    echo "                 覆盖率HTML点开函数所在行可见21个等频接口~3000万次运行计数"
else
    echo "✅ 构建完成"
    echo "   运行环境 : source ${BUILD_DIR}/kadd_env.sh   # 仅库搜索路径（模型密钥已在 config.json）"
    echo "   运行 demo : cd ${BUILD_DIR}/demo && ./kadd_sqli_demo"
    echo "   运行测试 : cd ${BUILD_DIR} && ctest --verbose   # ctest 自动注入库路径，密钥从 config 读取"
    echo "   构建 fuzz : ./build.sh fuzz   # kadd_fuzzer 需 clang，默认编译器下不会产出"
fi
