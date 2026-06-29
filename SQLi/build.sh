#!/bin/bash
# ============================================================================
# KADD 一键构建脚本
# 自动准备 util/ 依赖（ONNX Runtime / cJSON / Google Test）+ CMake 编译 src/demo/tests
# 用法：cd /opt/KADD/SQLi && ./build.sh
# ============================================================================
set -e

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
UTIL_DIR="${PROJECT_ROOT}/util"
mkdir -p "${UTIL_DIR}"

# ----------------------------------------------------------------------------
# cJSON（源码内嵌方式：放置 cJSON.h / cJSON.c 到 util/cJSON）
# ----------------------------------------------------------------------------
if [ ! -f "${UTIL_DIR}/cJSON/cJSON.c" ]; then
    echo "[1/3] 准备 cJSON..."
    cd "${UTIL_DIR}"
    wget -q https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.19.zip -O cJSON-1.7.19.zip
    unzip -q cJSON-1.7.19.zip
    mkdir -p cJSON
    cp cJSON-1.7.19/cJSON.h cJSON-1.7.19/cJSON.c cJSON/
    rm -rf cJSON-1.7.19 cJSON-1.7.19.zip
fi

# ----------------------------------------------------------------------------
# ONNX Runtime（ML 推理必需）
# ----------------------------------------------------------------------------
ONNXRT_DIR="${UTIL_DIR}/onnxruntime-linux-aarch64-1.16.0"
if [ ! -d "${ONNXRT_DIR}" ]; then
    echo "[2/3] 准备 ONNX Runtime..."
    cd "${UTIL_DIR}"
    wget -q https://github.com/microsoft/onnxruntime/releases/download/v1.16.0/onnxruntime-linux-aarch64-1.16.0.tgz
    tar xzf onnxruntime-linux-aarch64-1.16.0.tgz
    rm -f onnxruntime-linux-aarch64-1.16.0.tgz
fi
export ONNXRUNTIME_HOME="${ONNXRT_DIR}"

# ----------------------------------------------------------------------------
# Google Test（可选，仅构建单元测试时需要）
# ----------------------------------------------------------------------------
if [ ! -d "${UTIL_DIR}/gtest" ]; then
    echo "[3/3] 准备 Google Test..."
    cd "${UTIL_DIR}"
    wget -q https://github.com/google/googletest/archive/refs/tags/release-1.12.1.tar.gz
    tar xzf release-1.12.1.tar.gz
    mkdir -p gtest
    cp -r googletest-release-1.12.1/googletest/include gtest/
    cp -r googletest-release-1.12.1/googletest/src gtest/
    rm -rf googletest-release-1.12.1 release-1.12.1.tar.gz
fi
export GTEST_HOME="${UTIL_DIR}/gtest"

# ----------------------------------------------------------------------------
# CMake 一键构建（src + demo + tests）
# ----------------------------------------------------------------------------
echo "开始 CMake 构建..."
cd "${PROJECT_ROOT}"
mkdir -p build && cd build
cmake -DKADD_OPTIMIZE=ON -DCMAKE_BUILD_TYPE=Release ..
cmake --build .

# 运行环境：kadd 动态库 + onnxruntime.so
export LD_LIBRARY_PATH="${PROJECT_ROOT}/build/src:${ONNXRUNTIME_HOME}/lib:${LD_LIBRARY_PATH}"

echo ""
echo "✅ 构建完成"
echo "   运行 demo : cd build/demo && ./kadd_sqli_demo"
echo "   运行测试 : cd build && ctest --verbose"
