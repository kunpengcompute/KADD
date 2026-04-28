#!/bin/bash
# KADD 动态库一键构建脚本
# 用途：在依赖已安装的前提下，一键生成所有动态库
#
# 依赖查找策略：
#   1. 优先使用系统安装的库（/usr/local/lib, /usr/lib 等）
#   2. 如果系统库不存在，则使用 third_party 目录中的库
#   3. 可以通过 -DUSE_SYSTEM_LIBS=OFF 强制使用 third_party
#
# 构建选项：
#   OPTIMIZE=1   启用 -O3 优化
#   COVERAGE=1   启用代码覆盖率
#
# 安装依赖请参考: docs/INSTALL.md

set -e  # 遇到错误立即退出

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

echo_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

echo_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

echo_system() {
    echo -e "${BLUE}[SYSTEM]${NC} $1"
}

# 获取脚本目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR"
BUILD_DIR="$PROJECT_DIR/build"
OUTPUT_DIR="$PROJECT_DIR/output"

echo_info "=========================================="
echo_info "  KADD 动态库一键构建脚本"
echo_info "=========================================="
echo_info ""
echo_system "依赖查找策略:"
echo_info "  1. 优先使用系统库"
echo_info "  2. 回退到 third_party 目录"
echo_info "  3. 详见: cmake -DUSE_SYSTEM_LIBS=ON/OFF"
echo_info ""
echo_system "依赖说明:"
echo_info "  构建基础引擎 (DFA/Feature): 无外部依赖"
echo_info "  构建 ML 引擎: ONNX Runtime"
echo_info "  构建 DFA Pro 引擎: ONNX Runtime + cJSON"
echo_info "  构建 SQLi 检测引擎: ONNX Runtime + cJSON + DFA/Feature/ML"
echo_info "  运行测试: ONNX Runtime + cJSON + Google Test"
echo_info "  详细安装指南: docs/INSTALL.md"
echo_info ""
echo_system "构建选项:"
echo_info "  OPTIMIZE=1  启用 -O3 优化编译"
echo_info "  COVERAGE=1  启用代码覆盖率"
echo_info ""

# ========== 依赖检查函数 ==========
check_system_onnx() {
    if [ "$(uname)" = "Linux" ]; then
        if ldconfig -p | grep -q onnxruntime; then
            echo_info "  ✓ 系统已安装 ONNX Runtime"
            return 0
        fi
    elif [ "$(uname)" = "Darwin" ]; then
        if brew list | grep -q onnxruntime 2>/dev/null; then
            echo_info "  ✓ 系统已安装 ONNX Runtime"
            return 0
        fi
    fi
    return 1
}

check_third_party_onnx() {
    if [ -f "$PROJECT_DIR/third_party/lib/onnxruntime.dll" ] || \
       [ -f "$PROJECT_DIR/third_party/lib/libonnxruntime.so"* ] || \
       [ -f "$PROJECT_DIR/third_party/lib/libonnxruntime.dylib" ]; then
        echo_info "  ✓ third_party 中有 ONNX Runtime"
        return 0
    fi
    return 1
}

check_system_cjson() {
    if pkg-config --exists cjson 2>/dev/null; then
        echo_info "  ✓ 系统已安装 cJSON"
        return 0
    elif [ -f "/usr/local/include/cJSON.h" ] || [ -f "/usr/include/cJSON.h" ]; then
        echo_info "  ✓ 系统已安装 cJSON"
        return 0
    fi
    return 1
}

check_third_party_cjson() {
    if [ -f "$PROJECT_DIR/third_party/include/cJSON.h" ] && \
       [ -f "$PROJECT_DIR/third_party/src/cJSON.c" ]; then
        echo_info "  ✓ third_party 中有 cJSON"
        return 0
    fi
    return 1
}

check_system_gtest() {
    if pkg-config --exists gtest 2>/dev/null; then
        echo_info "  ✓ 系统已安装 Google Test"
        return 0
    fi
    return 1
}

check_third_party_gtest() {
    if [ -f "$PROJECT_DIR/third_party/gtest/src/gtest-all.cc" ]; then
        echo_info "  ✓ third_party 中有 Google Test"
        return 0
    fi
    return 1
}

# ========== 执行依赖检查 ==========
echo_system "1. 检查依赖..."
echo ""

# 检查构建目录
if [ ! -d "$BUILD_DIR" ]; then
    echo_info "构建目录不存在: $BUILD_DIR"
    echo_info "将创建构建目录并运行 CMake 配置"
fi

# 检查 ONNX Runtime
echo -n "${BLUE}检查 ONNX Runtime:${NC} "
if check_system_onnx; then
    USE_SYSTEM_ONNX=1
elif check_third_party_onnx; then
    USE_SYSTEM_ONNX=0
    echo_warn "将使用 third_party 中的 ONNX Runtime"
else
    echo_error "ONNX Runtime 未找到！"
    echo_info "请参考 docs/INSTALL.md 安装 ONNX Runtime"
    exit 1
fi

# 检查 cJSON
echo -n "${BLUE}检查 cJSON:${NC} "
if check_system_cjson; then
    USE_SYSTEM_CJSON=1
elif check_third_party_cjson; then
    USE_SYSTEM_CJSON=0
    echo_warn "将使用 third_party 中的 cJSON"
else
    USE_SYSTEM_CJSON=0
    echo_warn "cJSON 未找到！DFA Pro 和 SQLi 检测引擎将无法构建"
fi

# 检查 Google Test
echo -n "${BLUE}检查 Google Test:${NC} "
if check_system_gtest; then
    USE_SYSTEM_GTEST=1
elif check_third_party_gtest; then
    USE_SYSTEM_GTEST=0
    echo_warn "将使用 third_party 中的 Google Test"
else
    USE_SYSTEM_GTEST=0
    echo_warn "Google Test 未找到！测试将无法运行"
fi

echo ""
echo_info "✓ 依赖检查完成"
echo ""

# ========== 清理旧的动态库 ==========
echo_system "2. 清理旧的构建产物..."
cd "$PROJECT_DIR"
rm -f "$OUTPUT_DIR"/*.dll "$OUTPUT_DIR"/*.so "$OUTPUT_DIR"/*.dylib "$OUTPUT_DIR"/*.a 2>/dev/null || true
echo_info "✓ 清理完成"
echo ""

# ========== 配置 CMake ==========
echo_system "3. 配置 CMake..."

# 根据操作系统选择生成器
if [ "$(uname)" = "Darwin" ]; then
    GENERATOR="Unix Makefiles"
else
    GENERATOR="Unix Makefiles"
fi

# CMake 配置
CMAKE_OPTS="-G \"$GENERATOR\" -DBUILD_TESTS=OFF"

# 解析构建选项
if [ "$OPTIMIZE" = "1" ]; then
    CMAKE_OPTS="$CMAKE_OPTS -DKADD_OPTIMIZE=ON"
    echo_info "  优化编译已启用 (-O3)"
fi

if [ "$COVERAGE" = "1" ]; then
    CMAKE_OPTS="$CMAKE_OPTS -DENABLE_CODE_COVERAGE=ON"
    echo_info "  代码覆盖率已启用"
fi

# 如果用户想强制使用 third_party
if [ "$1" = "--use-third-party" ]; then
    CMAKE_OPTS="$CMAKE_OPTS -DUSE_SYSTEM_LIBS=OFF"
    echo_info "使用强制模式: 仅使用 third_party 依赖"
fi

CMAKE_CMD="cmake -H. -B\"$BUILD_DIR\" $CMAKE_OPTS"

eval $CMAKE_CMD
if [ $? -eq 0 ]; then
    echo_info "✓ CMake 配置完成 ($GENERATOR)"
else
    echo_error "CMake 配置失败"
    exit 1
fi
echo ""

# ========== 构建动态库 ==========
echo_system "4. 构建动态库..."
cd "$BUILD_DIR"

# 构建 DFA 引擎
echo_info "  [1/5] 构建 DFA 引擎动态库..."
cmake --build . --target kadd_dfa_engine_dll > /dev/null 2>&1
if [ -f "$OUTPUT_DIR/libkadd_dfa_engine.so" ] || [ -f "$OUTPUT_DIR/libkadd_dfa_engine.dylib" ] || [ -f "$OUTPUT_DIR/libkadd_dfa_engine.dll" ]; then
    echo_info "  ✓ libkadd_dfa_engine 构建成功"
else
    echo_error "  ✗ libkadd_dfa_engine 构建失败"
fi
echo ""

# 构建特征引擎
echo_info "  [2/5] 构建特征引擎动态库..."
cmake --build . --target kadd_feature_engine_dll > /dev/null 2>&1
if [ -f "$OUTPUT_DIR/libkadd_feature_engine.so" ] || [ -f "$OUTPUT_DIR/libkadd_feature_engine.dylib" ] || [ -f "$OUTPUT_DIR/libkadd_feature_engine.dll" ]; then
    echo_info "  ✓ libkadd_feature_engine 构建成功"
else
    echo_error "  ✗ libkadd_feature_engine 构建失败"
fi
echo ""

# 构建 ML 引擎
echo_info "  [3/5] 构建 ML 引擎动态库..."
cmake --build . --target kadd_ml_engine_dll > /dev/null 2>&1
if [ -f "$OUTPUT_DIR/libkadd_ml_engine.so" ] || [ -f "$OUTPUT_DIR/libkadd_ml_engine.dylib" ] || [ -f "$OUTPUT_DIR/libkadd_ml_engine.dll" ]; then
    echo_info "  ✓ libkadd_ml_engine 构建成功"
else
    echo_error "  ✗ libkadd_ml_engine 构建失败"
fi
echo ""

# 构建 DFA Pro 引擎和 SQLi 引擎（如果 cJSON 可用）
if [ "$USE_SYSTEM_CJSON" = "1" ] || [ -f "$PROJECT_DIR/third_party/include/cJSON.h" ]; then
    echo_info "  [4/5] 构建 DFA Pro 引擎动态库..."
    cmake --build . --target kadd_dfa_engine_pro_dll > /dev/null 2>&1
    if [ -f "$OUTPUT_DIR/libkadd_dfa_engine_pro.so" ] || [ -f "$OUTPUT_DIR/libkadd_dfa_engine_pro.dylib" ] || [ -f "$OUTPUT_DIR/libkadd_dfa_engine_pro.dll" ]; then
        echo_info "  ✓ libkadd_dfa_engine_pro 构建成功"
    else
        echo_warn "  ✗ libkadd_dfa_engine_pro 构建失败"
    fi
    echo ""

    echo_info "  [5/5] 构建 SQL 注入检测引擎动态库..."
    cmake --build . --target kadd_sqli_engine_dll > /dev/null 2>&1
    if [ -f "$OUTPUT_DIR/libkadd_sqli_engine.so" ] || [ -f "$OUTPUT_DIR/libkadd_sqli_engine.dylib" ] || [ -f "$OUTPUT_DIR/libkadd_sqli_engine.dll" ]; then
        echo_info "  ✓ libkadd_sqli_engine 构建成功"
    else
        echo_warn "  ✗ libkadd_sqli_engine 构建失败"
    fi
else
    echo_warn "  [4/5] 跳过 DFA Pro 引擎 (缺少 cJSON)"
    echo ""
    echo_warn "  [5/5] 跳过 SQL 注入检测引擎 (缺少 cJSON)"
fi
echo ""

# ========== 统计构建结果 ==========
echo_system "5. 构建结果统计..."
cd "$OUTPUT_DIR"

DLL_COUNT=0
OS_TYPE="$(uname)"
LIB_EXT=""

if [ "$OS_TYPE" = "Linux" ]; then
    LIB_EXT=".so"
elif [ "$OS_TYPE" = "Darwin" ]; then
    LIB_EXT=".dylib"
else
    LIB_EXT=".dll"
fi

for lib in kadd_dfa_engine kadd_feature_engine kadd_ml_engine kadd_dfa_engine_pro kadd_sqli_engine; do
    if [ -f "lib${lib}${LIB_EXT}" ]; then
        DLL_COUNT=$((DLL_COUNT + 1))
        echo_info "  ✓ lib${lib}${LIB_EXT}"
    fi
done

echo ""
echo_info "=========================================="
echo_info "  构建完成！"
echo_info "=========================================="
echo_system "系统: $OS_TYPE"
echo_info "动态库总数: $DLL_COUNT"
echo_info "输出目录: $OUTPUT_DIR"
echo ""

# ========== 使用说明 ==========
echo_info "使用方法:"
echo_info "  1. 系统库安装: sudo make install (Linux/macOS)"
echo_info "  2. 或复制动态库到项目目录使用"
echo_info ""
echo_system "集成说明:"

if [ "$USE_SYSTEM_ONNX" = "1" ]; then
    echo_info "  ONNX Runtime: 系统库"
else
    echo_info "  ONNX Runtime: third_party (需复制 DLL)"
fi

if [ "$USE_SYSTEM_CJSON" = "1" ]; then
    echo_info "  cJSON: 系统库"
elif [ -f "$PROJECT_DIR/third_party/include/cJSON.h" ]; then
    echo_info "  cJSON: third_party (已静态链接)"
else
    echo_info "  cJSON: 未使用"
fi

if [ "$OS_TYPE" = "Linux" ]; then
    echo_info "  Linux:   运行 sudo ldconfig 或设置 LD_LIBRARY_PATH"
elif [ "$OS_TYPE" = "Darwin" ]; then
    echo_info "  macOS:   设置 DYLD_LIBRARY_PATH 或安装到 /usr/local/lib"
else
    echo_info "  Windows: 需要 onnxruntime.dll 在同目录"
fi

echo_info "=========================================="
