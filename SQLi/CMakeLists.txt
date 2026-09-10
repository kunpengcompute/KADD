cmake_minimum_required(VERSION 3.16)
project(KADD VERSION 1.0.0 LANGUAGES C CXX ASM)

# C/C++ 标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# 安全编译选项（BIND_NOW; NX; PIC; PIE; RELRO; SP; NO Rpath/Runpath; FS; Ftrapv; Strip）
# ============================================================================
add_compile_options(
    -fPIC -fPIE
    -fstack-protector-strong
    -Wformat -Wformat-security
    -ftrapv
)
add_link_options(
    -Wl,-z,relro,-z,now
    -Wl,-z,noexecstack
)
set(CMAKE_SKIP_RPATH TRUE)
set(CMAKE_SKIP_BUILD_RPATH TRUE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)

# 版本信息
string(TIMESTAMP BUILDDATE "%Y.%m.%d")
set(KADD_VERSION_BUILDDATE "${BUILDDATE}")

# ============================================================================
# 选项配置
# ============================================================================
option(BUILD_SHARED_LIBS "Build shared libraries (dynamic libraries)" ON)
option(BUILD_TESTS "Build test programs" ON)
option(ENABLE_CODE_COVERAGE "Enable code coverage reporting" OFF)
option(KADD_OPTIMIZE "Enable optimizations" ON)

# KADD_OPTIMIZE: 启用 -O2 + _FORTIFY_SOURCE
if(KADD_OPTIMIZE)
    add_compile_definitions(_FORTIFY_SOURCE=2)
    add_compile_options(-O2)
endif()
option(KADD_STRIP "Strip symbols from built binaries and libraries (-s)" ON)

# KADD_STRIP: 链接时剥离符号表（-s），减小产物体积（仅保留动态链接所需符号）
if(KADD_STRIP)
    add_link_options(-s)
endif()
# ARM SVE 加速选项（ON/OFF）：
#   ON（默认）—— 请求 SVE 加速：平台/编译器支持则启用；
#                不支持则禁用 SVE 并回退 ONNX 后端（WARNING 明示原因与后端，编译继续）
#   OFF       —— 完全禁用 SVE，直接使用 ONNX 后端
# 注意：ONNX Runtime 为强制依赖，缺失时无条件 FATAL_ERROR（即"无法回退则编译失败"）
option(ENABLE_ARM_SVE "Enable ARM SVE acceleration (falls back to ONNX backend when unsupported)" ON)
# 可选：固定 SVE 向量位宽（默认 256 位；设为空字符串则自适应任意宽度）。
# 固定 256 位时启用 SVE256 专用路径并排除不可用的 VLA 回退，使覆盖率报告不含其 0% 函数。
set(KADD_SVE_VECTOR_BITS "256" CACHE STRING "Fixed SVE vector length in bits (empty=adaptive; e.g. 128, 256, 512)")

# 自动检测 assets 目录（优先级：项目根目录 > 构建目录 > 默认 assets）
if(EXISTS "${CMAKE_SOURCE_DIR}/assets")
    set(KADD_ASSETS_PATH "${CMAKE_SOURCE_DIR}/assets")
elseif(EXISTS "${CMAKE_BINARY_DIR}/assets")
    set(KADD_ASSETS_PATH "${CMAKE_BINARY_DIR}/assets")
else()
    set(KADD_ASSETS_PATH "assets")
endif()

# ============================================================================
# ARM SVE 加速：能力检测与回退（行为矩阵）
#
#   | SVE  | 平台/编译器      | 行为                                             |
#   |------|------------------|--------------------------------------------------|
#   | ON   | aarch64 + 支持   | 加 -march=armv8-a+sve，明示"使用 SVE"             |
#   | ON   | aarch64 + 不支持 | 不加 SVE 参数，WARNING 明示原因 + "使用 ONNX"      |
#   | ON   | 其他平台（x86 等）| 不加 SVE 参数，WARNING 明示原因 + "使用 ONNX"      |
#   | OFF  | 任意             | 跳过检测，明示"使用 ONNX"                          |
#
# 检测原则：
#   1. 以编译器预定义宏（__aarch64__）判断目标架构，而非 CMAKE_SYSTEM_PROCESSOR——
#      交叉编译（CC=xxx-gcc 且未提供 toolchain 文件）时后者仍是宿主机架构；
#   2. 带 -march=armv8-a+sve 编译真实 SVE 代码来检测编译器能力——
#      __ARM_FEATURE_SVE 宏只有在 -march 启用 SVE 后才会被编译器定义，
#      裸查宏检测的是"当前参数是否已启用 SVE"，在标准工具链上永远判定
#      "不支持"，导致 SVE 被静默关闭；
#   3. 统一到 armv8-a+sve（纯 SVE1）：armv9-a 隐含 SVE2，GCC 会生成 SVE2 指令
#      （如 whilewr/whilerw），而部分鲲鹏 CPU 仅支持 SVE1，运行会 SIGILL；
#      当前实现只用 SVE1 intrinsic，无需 SVE2；SVE2 完全向下兼容 SVE1，
#      armv8-a+sve 产物可运行于所有支持 SVE 的 CPU（含全部 Armv9）；
#   4. 编译期无法检测目标硬件是否真有 SVE，由源码在运行时通过
#      getauxval(AT_HWCAP) 判断并回退 ONNX 后端。
# ============================================================================
set(KADD_SVE_ENABLED FALSE)
set(KADD_SVE_FALLBACK_REASON "")

if(ENABLE_ARM_SVE)
    include(CheckCSourceCompiles)

    # 1) 编译目标是否为 AArch64
    check_c_source_compiles("
        #if defined(__aarch64__) || defined(_M_ARM64)
            int main(void) { return 0; }
        #else
            #error Not ARM64
        #endif
    " KADD_COMPILER_TARGETS_ARM64)

    if(NOT KADD_COMPILER_TARGETS_ARM64)
        set(KADD_SVE_FALLBACK_REASON "compiler target is not AArch64 (host: ${CMAKE_SYSTEM_PROCESSOR})")
    else()
        # 2) 编译器是否支持 SVE（带 -march 编译真实 SVE 代码）
        set(CMAKE_REQUIRED_FLAGS "-march=armv8-a+sve")
        check_c_source_compiles("
            #include <arm_sve.h>
            int main(void) { return (int)svcntb(); }
        " KADD_COMPILER_SVE_SUPPORTED)
        unset(CMAKE_REQUIRED_FLAGS)

        if(KADD_COMPILER_SVE_SUPPORTED)
            set(KADD_SVE_ENABLED TRUE)
        else()
            set(KADD_SVE_FALLBACK_REASON "compiler cannot compile SVE code (${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION})")
        endif()
    endif()
endif()

if(KADD_SVE_ENABLED)
    message(STATUS "ARM SVE acceleration enabled (-march=armv8-a+sve): using SVE native engines")
    add_compile_options(-march=armv8-a+sve)
    # 注意：不要手动 add_definitions(-D__ARM_FEATURE_SVE)——该宏由编译器在
    # -march=armv8-a+sve 生效时自动定义，手动定义只会掩盖真实能力检测

    # 可选：固定 SVE 向量位宽（空=自适应任意宽度，如 128/256/512 均兼容）。
    # 固定后编译器将 VL 视为编译期常量（svcntw() 折叠），可让 256 位专用路径
    # （如 KADD_CNN_SVE256_ONLY）在编译期确定，从而把用不到的回退分支排除，
    # 避免其以 0% 覆盖出现在覆盖率报告中。固定宽度构建仅适用于硬件宽度固定
    # 且与该值一致的部署。
    if(NOT KADD_SVE_VECTOR_BITS STREQUAL "")
        message(STATUS "  Fixing SVE vector length to ${KADD_SVE_VECTOR_BITS}-bit")
        add_compile_options(-msve-vector-bits=${KADD_SVE_VECTOR_BITS})
        add_definitions(-DKADD_SVE_VECTOR_BITS=${KADD_SVE_VECTOR_BITS})
        # CNN 期望/校验的位宽与固定位宽一致
        add_definitions(-DKADD_CNN_EXPECTED_SVE_BITS=${KADD_SVE_VECTOR_BITS})
        # 恰好 256 位时启用 SVE256 专用快速路径，排除通用 VLA 回退
        if(KADD_SVE_VECTOR_BITS STREQUAL "256")
            add_definitions(-DKADD_CNN_SVE256_ONLY)
        endif()
    endif()
elseif(ENABLE_ARM_SVE)
    message(WARNING
        "ENABLE_ARM_SVE=ON but SVE is unavailable: ${KADD_SVE_FALLBACK_REASON}.\n"
        "SVE acceleration is disabled and this build uses the ONNX backend.")
else()
    message(STATUS "ARM SVE disabled (ENABLE_ARM_SVE=OFF): using ONNX backend")
endif()

# 代码覆盖率
if(ENABLE_CODE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
        add_link_options(-fprofile-instr-generate -fcoverage-mapping)
    else()
        add_compile_options(--coverage)
        add_link_options(--coverage)
    endif()
    # Coverage 构建时强制使用静态库，避免 .so 分散 coverage 数据
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
    # 静态链接模式下需要显式链接 libm
    add_link_options(-lm)
endif()

# 依赖查找策略: 优先使用系统库，如果找不到则使用 util/ 源码
message(STATUS "Build tests: ${BUILD_TESTS}")
message(STATUS "Build shared libs: ${BUILD_SHARED_LIBS}")
message(STATUS "Enable coverage: ${ENABLE_CODE_COVERAGE}")
message(STATUS "Enable optimize: ${KADD_OPTIMIZE}")
    message(STATUS "Enable strip:    ${KADD_STRIP}")
if(KADD_SVE_ENABLED)
    message(STATUS "Enable ARM SVE: ${ENABLE_ARM_SVE} -> enabled, using SVE native engines")
elseif(ENABLE_ARM_SVE)
    message(STATUS "Enable ARM SVE: ON -> unavailable (${KADD_SVE_FALLBACK_REASON}), using ONNX backend")
else()
    message(STATUS "Enable ARM SVE: OFF -> using ONNX backend")
endif()
message(STATUS "Assets path: ${KADD_ASSETS_PATH} (auto-detected)")

# ============================================================================
# ONNX   依赖
# ============================================================================

# 优先使用环境变量 ONNXRUNTIME_HOME
set(ONNXRUNTIME_ROOT "$ENV{ONNXRUNTIME_HOME}" CACHE PATH "Root directory of ONNX Runtime installation")

if(ONNXRUNTIME_ROOT)
    # 用户指定了路径
    message(STATUS "Using ONNXRUNTIME_HOME: ${ONNXRUNTIME_ROOT}")
    set(SEARCH_PATHS "${ONNXRUNTIME_ROOT}")
    set(SEARCH_MODE NO_DEFAULT_PATH)
else()
    # 未指定 ONNXRUNTIME_HOME，仅搜索系统路径
    set(SEARCH_PATHS "/usr/local" "/usr")
    set(SEARCH_MODE "")
endif()

find_library(ONNXRUNTIME_LIBRARIES
    NAMES onnxruntime
    PATHS ${SEARCH_PATHS}
    PATH_SUFFIXES lib lib64
    ${SEARCH_MODE}
)

find_path(ONNXRUNTIME_INCLUDE_DIRS
    NAMES onnxruntime_c_api.h
    PATHS ${SEARCH_PATHS}
    PATH_SUFFIXES include
    ${SEARCH_MODE}
)

set(KADD_ONNXRUNTIME_MIN_API_VERSION 28)

# 验证结果
if(NOT ONNXRUNTIME_LIBRARIES OR NOT ONNXRUNTIME_INCLUDE_DIRS)
    message(FATAL_ERROR "ONNX Runtime 1.28.0 or newer not found!"
        "\n  Solutions:"
        "\n    1. Set ONNXRUNTIME_HOME environment variable"
        "\n    2. Install: sudo apt-get install libonnxruntime-dev (1.28.0+)")
endif()

include(CheckCSourceCompiles)
set(CMAKE_REQUIRED_INCLUDES ${ONNXRUNTIME_INCLUDE_DIRS})
unset(ONNXRUNTIME_API_VERSION_SUPPORTED CACHE)
check_c_source_compiles("
#include <onnxruntime_c_api.h>
#if ORT_API_VERSION < ${KADD_ONNXRUNTIME_MIN_API_VERSION}
#error \"ONNX Runtime 1.28.0 or newer (C API v28+) is required\"
#endif
int main(void) { return 0; }
" ONNXRUNTIME_API_VERSION_SUPPORTED)
unset(CMAKE_REQUIRED_INCLUDES)

if(NOT ONNXRUNTIME_API_VERSION_SUPPORTED)
    message(FATAL_ERROR "ONNX Runtime 1.28.0 or newer (C API v28+) is required."
        "\n  Found header: ${ONNXRUNTIME_INCLUDE_DIRS}/onnxruntime_c_api.h")
endif()

get_filename_component(ONNXRUNTIME_LIB_DIR ${ONNXRUNTIME_LIBRARIES} DIRECTORY)
message(STATUS "Found ONNX Runtime 1.28.0+ (C API v${KADD_ONNXRUNTIME_MIN_API_VERSION}+): ${ONNXRUNTIME_LIBRARIES}")

# ============================================================================
# cJSON 依赖
# ============================================================================
# cJSON 查找：优先 util/ 源码（源码内嵌，无外部 .so 依赖），其次系统库
set(CJSON_FOUND FALSE)

# 1. 优先 util/cJSON 源码（源码内嵌方式）
set(cjson_path "${CMAKE_SOURCE_DIR}/util/cJSON")
set(cjson_header "${cjson_path}/cJSON.h")
set(cjson_source "${cjson_path}/cJSON.c")
if(EXISTS "${cjson_header}" AND EXISTS "${cjson_source}")
    set(CJSON_FOUND TRUE)
    set(CJSON_INCLUDE_DIRS "${cjson_path}")
    set(CJSON_LIBRARIES "${cjson_source}")
    set(CJSON_IS_THIRD_PARTY TRUE)
    message(STATUS "Using util/ cJSON (source): ${cjson_path}")
else()
    # 2. 备选：系统 cJSON 库
    #    可通过环境变量 CJSON_ROOT_DIR 手动指定安装路径
    find_path(CJSON_INCLUDE_DIR
        NAMES cJSON.h
        PATHS
            $ENV{CJSON_ROOT_DIR}
            /usr/include
            /usr/local/include
        PATH_SUFFIXES cjson
    )

    find_library(CJSON_LIBRARY
        NAMES cjson
        PATHS
            $ENV{CJSON_ROOT_DIR}
            /usr/lib
            /usr/local/lib
            /usr/lib64
            /usr/lib/aarch64-linux-gnu
            /usr/lib/x86_64-linux-gnu
        PATH_SUFFIXES lib lib64
    )

    if(CJSON_INCLUDE_DIR AND CJSON_LIBRARY)
        set(CJSON_FOUND TRUE)
        set(CJSON_INCLUDE_DIRS "${CJSON_INCLUDE_DIR}")
        set(CJSON_LIBRARIES "${CJSON_LIBRARY}")
        set(CJSON_IS_THIRD_PARTY FALSE)
        message(STATUS "Using system cJSON: ${CJSON_LIBRARY}")
    else()
        message(FATAL_ERROR
            "cJSON not found!\n"
            "  cJSON 是 KADD 的必选依赖，请选择以下任一方式安装：\n"
            "  方式1（推荐）: 将 cJSON 源码放入 util/cJSON/ 目录（源码内嵌，无外部 .so 依赖）\n"
            "    mkdir -p util/cJSON && cd util/\n"
            "    wget -q https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.19.zip -O cJSON-1.7.19.zip\n"
            "    unzip -q cJSON-1.7.19.zip && cp cJSON-1.7.19/cJSON.h cJSON-1.7.19/cJSON.c cJSON/\n"
            "  方式2: 安装系统 cJSON 开发包\n"
            "    openEuler/CentOS: dnf install cJSON-devel\n"
            "    Ubuntu/Debian:  apt install libcjson-dev\n"
            "  方式3: 手动指定路径\n"
            "    export CJSON_ROOT_DIR=/path/to/cjson\n"
        )
    endif()
endif()

# ============================================================================
# Google Test 依赖 (仅在 BUILD_TESTS 时）
# ============================================================================
set(GTest_FOUND FALSE)
set(GMock_FOUND FALSE)
if(BUILD_TESTS)
    # Google Test 查找：优先 util/gtest 源码（GTEST_HOME 可覆盖路径），其次系统库
    set(gtest_path "$ENV{GTEST_HOME}")
    if(NOT gtest_path OR NOT EXISTS "${gtest_path}/src/gtest-all.cc")
        set(gtest_path "${CMAKE_SOURCE_DIR}/util/gtest")
    endif()
    set(gtest_source "${gtest_path}/src/gtest-all.cc")

    if(EXISTS "${gtest_source}")
        message(STATUS "Using util/ Google Test: ${gtest_path}")
        set(GTEST_INCLUDE_DIR "${gtest_path}/include")
        set(GTest_FOUND TRUE)

        # GTest itself needs pthreads on Linux; keep the dependency on the
        # static library so every test target inherits it automatically.
        set(THREADS_PREFER_PTHREAD_FLAG ON)
        find_package(Threads REQUIRED)

        # 创建 gtest 静态库
        add_library(gtest STATIC "${gtest_source}")
        target_include_directories(gtest PUBLIC "${GTEST_INCLUDE_DIR}")
        target_include_directories(gtest PRIVATE "${gtest_path}")  # 给 #include "src/xxx.cc" 提供路径
        target_link_libraries(gtest PUBLIC Threads::Threads)

        add_library(gtest_main STATIC "${gtest_path}/src/gtest_main.cc")
        target_include_directories(gtest_main PUBLIC "${GTEST_INCLUDE_DIR}")
        target_include_directories(gtest_main PRIVATE "${gtest_path}")  # 给 gtest_main.cc 为 include 提供
        target_link_libraries(gtest_main PUBLIC gtest)

        # gmock（如果存在）
        set(gmock_source "${gtest_path}/src/gmock-all.cc")
        if(EXISTS "${gmock_source}")
            message(STATUS "Using util/ Google Mock: ${gtest_path}")
            set(GMock_FOUND TRUE)

            add_library(gmock STATIC "${gmock_source}")
            target_include_directories(gmock PUBLIC "${GTEST_INCLUDE_DIR}")
            target_include_directories(gmock PRIVATE "${gtest_path}")
            target_link_libraries(gmock PUBLIC gtest)

            add_library(gmock_main STATIC "${gtest_path}/src/gmock_main.cc")
            target_include_directories(gmock_main PUBLIC "${GTEST_INCLUDE_DIR}")
            target_include_directories(gmock_main PRIVATE "${gtest_path}")
            target_link_libraries(gmock_main PUBLIC gmock)
        endif()
        # gtest / gtest_main / gmock / gmock_main 自动继承全局 CMAKE_CXX_FLAGS 中的安全编译选项
    else()
        # 备选：系统 Google Test
        find_package(GTest QUIET)
        if(GTest_FOUND)
            message(STATUS "Using system Google Test")

            # 尝试查找系统 gmock
            find_package(GMock QUIET)
            if(GMock_FOUND)
                message(STATUS "Using system Google Mock")
                set(GMock_FOUND TRUE)
            endif()
        else()
            message(WARNING "Google Test not found! Place gtest source in util/gtest (or set GTEST_HOME), or install system gtest. Tests will be disabled.")
            set(BUILD_TESTS OFF)
        endif()
    endif()
endif()

# ============================================================================
# 全局包含目录
# ============================================================================
include_directories("${CMAKE_SOURCE_DIR}/include")
include_directories("${CMAKE_SOURCE_DIR}/include/internal")
if(CJSON_IS_THIRD_PARTY)
    include_directories("${CJSON_INCLUDE_DIRS}")
endif()

# ============================================================================
# FUZZ 测试支持检查（全局变量，供 tests/CMakeLists.txt 使用）
# ============================================================================
# FUZZ测试支持（需要Clang编译器）
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(FUZZ_TESTS_AVAILABLE TRUE CACHE BOOL "LibFuzzer tests available" FORCE)
    message(STATUS "Clang detected - FUZZ tests available (use Clang for best results)")
else()
    set(FUZZ_TESTS_AVAILABLE FALSE CACHE BOOL "LibFuzzer tests available" FORCE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        message(STATUS "GCC detected - FUZZ tests skipped")
        message(STATUS "  ARM platform: use Clang for best results")
    else()
        message(STATUS "FUZZ tests skipped - requires Clang")
    endif()
endif()

# ============================================================================
# 子目录（不包含 tools 目录)
# ============================================================================

add_subdirectory(src)

# 添加 demo 目录（CMake 一键编译示例程序）
if(EXISTS "${CMAKE_SOURCE_DIR}/demo/CMakeLists.txt")
    add_subdirectory(demo)
endif()

# 仅在启用测试时添加 tests 目录
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ============================================================================
# 安装规则
# ============================================================================
include(GNUInstallDirs)

# 公开头文件（不安装 internal/，仅对外暴露公共 API）
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/kadd
    FILES_MATCHING PATTERN "*.h"
    PATTERN "internal" EXCLUDE
)

# 内部头文件（只有编译时可见，不安装）
# internal/ 由 src/CMakeLists.txt target_include_directories PRIVATE 已处理

# 库文件（由 src/CMakeLists.txt 的 add_kadd_library 创建）
install(TARGETS
        kadd_ml_engine kadd_sqli_engine kadd_dl_engine
        kadd_dfa_engine kadd_feature_engine kadd_config_loader
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# DFA 模型资产（可选）
if(EXISTS "${CMAKE_SOURCE_DIR}/assets")
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/kadd/assets
    )
endif()

# ============================================================================
# 构建信息摘要
# ============================================================================
message(STATUS "")
message(STATUS "=================== KADD Build Summary ===================")
message(STATUS "  Version:         ${PROJECT_VERSION}")
message(STATUS "  Build date:      ${KADD_VERSION_BUILDDATE}")
message(STATUS "  Output path:     ${LIBRARY_OUTPUT_PATH}")
message(STATUS "  Build type:      ${CMAKE_BUILD_TYPE}")
message(STATUS "  C Standard:      ${CMAKE_C_STANDARD}")
message(STATUS "  CXX Standard:    ${CMAKE_CXX_STANDARD}")
message(STATUS "")
message(STATUS "  Dependencies:")
message(STATUS "    ONNX Runtime:  ${ONNXRUNTIME_LIBRARIES}")
message(STATUS "    cJSON:         ${CJSON_FOUND}")
message(STATUS "    Google Test:   ${GTest_FOUND}")
message(STATUS "")
message(STATUS "  Acceleration:")
if(KADD_SVE_ENABLED)
    message(STATUS "    ARM SVE:       enabled (-march=armv8-a+sve)")
else()
    message(STATUS "    ARM SVE:       disabled -> ONNX backend")
endif()
message(STATUS "")
message(STATUS "  Testing:")
message(STATUS "    Unit Tests:    ${BUILD_TESTS}")
message(STATUS "    FUZZ Tests:    ${FUZZ_TESTS_AVAILABLE}")
if(BUILD_TESTS)
    message(STATUS "")
    message(STATUS "  To run tests, set LD_LIBRARY_PATH:")
    message(STATUS "    export LD_LIBRARY_PATH=${ONNXRUNTIME_LIB_DIR}:$LD_LIBRARY_PATH")
    if(CJSON_FOUND AND NOT CJSON_IS_THIRD_PARTY)
		get_filename_component(CJSON_LIB_DIR ${CJSON_LIBRARIES} DIRECTORY)
		message(STATUS "    export LD_LIBRARY_PATH=${CJSON_LIB_DIR}:$LD_LIBRARY_PATH")
    endif()
endif()
message(STATUS "============================================================")
