#!/bin/bash
# ============================================================================
# KADD 一键构建脚本
# ============================================================================

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"

echo "==================== KADD 一键构建 ===================="
echo "项目根目录: ${PROJECT_ROOT}"
echo ""

# 构建 SQLi
if [ -d "${PROJECT_ROOT}/SQLi" ]; then
    echo "[1/1] 构建 SQLi 特性..."
    cd "${PROJECT_ROOT}/SQLi"
    chmod +x build.sh && ./build.sh
    echo "✅ SQLi 构建完成"
fi

echo ""
echo "==================== 构建完成 ===================="
echo ""
echo "输出："
echo "  库文件: ${PROJECT_ROOT}/SQLi/build/src/"
echo "  Demo:   ${PROJECT_ROOT}/SQLi/build/demo/"
echo ""
