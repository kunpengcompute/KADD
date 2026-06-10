cmake_minimum_required(VERSION 3.16)
project(KADD VERSION 1.0.0 LANGUAGES C CXX ASM)

# C/C++ 标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# 安全编译选项（ BIND_NOW; NX; PIC; PIE; RELRO; SP; NO Rpath/Runpath; FS; Ftrapv; Strip）
# ============================================================================
set(SECURE_COMPILE_FLAGS "")

# PIC/PIE（位置无关代码/可执行文件）
set(SECURE_COMPILE_FLAGS "${SECURE_COMPILE_FLAGS} -fPIC -fPIE")

# 栈保护（Stack Protection - SP）
set(SECURE_COMPILE_FLAGS "${SECURE_COMPILE_FLAGS} -fstack-protector-strong")

# 格式化字符串检查（Format String - FS）
set(SECURE_COMPILE_FLAGS "${SECURE_COMPILE_FLAGS} -Wformat -Wformat-security")

# 整数溢出检查（Ftrapv）
set(SECURE_COMPILE_FLAGS "${SECURE_COMPILE_FLAGS} -ftrapv")

# RELRO（Relocation Read-Only）和 BIND_NOW
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,relro,-z,now")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,relro,-z,now")

# NX（No-Execute，默认现代编译器已启用）
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,noexecstack")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,noexecstack")

# 禁用 Rpath 和 Runpath（防止暴露构建路径信息）
set(CMAKE_SKIP_RPATH TRUE)
set(CMAKE_SKIP_BUILD_RPATH TRUE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)

# 应用到全局编译选项
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${SECURE_COMPILE_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SECURE_COMPILE_FLAGS}")

# 版本信息
string(TIMESTAMP BUILDDATE "%Y.%m.%d")
set(KADD_VERSION_BUILDDATE "${BUILDDATE}")

# ============================================================================
# 选项配置
# ============================================================================
option(BUILD_TESTS "Build test programs" ON)
option(USE_SYSTEM_LIBS "Use system libraries instead of third_party" ON)
option(ENABLE_CODE_COVERAGE "Enable code coverage reporting" OFF)
option(KADD_OPTIMIZE "Enable optimizations" OFF)
option(ENABLE_ARM_SVE "Enable ARM SVE acceleration (auto-detects SVE2 support)" ON)

# 自动检测 assets 目录（优先级：项目根目录 > 构建目录 > 默认 assets）
if(EXISTS "${CMAKE_SOURCE_DIR}/assets")
    set(KADD_ASSETS_PATH "${CMAKE_SOURCE_DIR}/assets")
elseif(EXISTS "${CMAKE_BINARY_DIR}/assets")
    set(KADD_ASSETS_PATH "${CMAKE_BINARY_DIR}/assets")
else()
    set(KADD_ASSETS_PATH "assets")
endif()

# 自动检测 SVE2 支持（如果启用 SVE，尝试检测是否支持 SVE2）
set(ENABLE_ARM_SVE2 FALSE)
if(ENABLE_ARM_SVE)
    include(CheckCSourceCompiles)
    check_c_source_compiles("
        #if defined(__ARM_FEATURE_SVE2) && __ARM_FEATURE_SVE2
            int main(void) { return 0; }
        #else
            #error SVE2 not supported
        #endif
    " ARM_SVE2_SUPPORTED)

    if(ARM_SVE2_SUPPORTED)
        set(ENABLE_ARM_SVE2 TRUE)
        message(STATUS "SVE2 supported, enabling SVE2 acceleration")
    else()
        message(STATUS "SVE2 not supported, falling back to SVE")
    endif()
endif()

# 优化选项
if(KADD_OPTIMIZE)
    add_definitions(-DKADD_OPTIMIZE)
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
endif()

# Strip - Release 构建时剥离符号信息
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -s")
    message(STATUS "Strip symbols enabled for Release build")
endif()

# ARM SVE 加速选项
if(ENABLE_ARM_SVE AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64")
    message(WARNING "ENABLE_ARM_SVE is ON but system is not ARM64. SVE will be disabled.")
    set(ENABLE_ARM_SVE OFF)
elseif(ENABLE_ARM_SVE)
    # 检测编译器是否支持 SVE（尝试编译 SVE 代码）
    include(CheckCSourceCompiles)
    check_c_source_compiles("
        #if defined(__aarch64__) || defined(__ARM64)
            #if defined(__ARM_FEATURE_SVE)
                int main(void) { return 0; }
            #else
                #error SVE not supported
            #endif
        #else
            #error Not ARM64
        #endif
    " ARM_SVE_COMPILER_SUPPORTED)

    if(NOT ARM_SVE_COMPILER_SUPPORTED)
        message(WARNING "Compiler does not support ARM SVE. SVE acceleration will be disabled.")
        set(ENABLE_ARM_SVE OFF)
    endif()
endif()

if(ENABLE_ARM_SVE)
    if(ENABLE_ARM_SVE2)
        message(STATUS "Enabling ARM SVE2 acceleration (-march=armv9-a+sve2)")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv9-a+sve2")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv9-a+sve2")
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -march=armv9-a+sve2")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=armv9-a+sve2")
        add_definitions(-D__ARM_FEATURE_SVE)
        add_definitions(-D__ARM_FEATURE_SVE2)
    else()
        message(STATUS "Enabling ARM SVE acceleration (-march=armv9-a+sve)")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv9-a+sve")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv9-a+sve")
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -march=armv9-a+sve")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=armv9-a+sve")
        add_definitions(-D__ARM_FEATURE_SVE)
    endif()
endif()

# 代码覆盖率
if(ENABLE_CODE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # Clang 使用显式的 coverage 标志
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fprofile-instr-generate -fcoverage-mapping")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-instr-generate -fcoverage-mapping")
    else()
        # GCC 使用 --coverage
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --coverage")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage")
    endif()
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} --coverage")
    # Coverage 构建时强制使用静态库，避免 .so 分散 coverage 数据
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
endif()

# 依赖查找策略: 优先使用系统库，如果找不到则使用 third_party
message(STATUS "Build tests: ${BUILD_TESTS}")
message(STATUS "Use system libs: ${USE_SYSTEM_LIBS}")
message(STATUS "Enable coverage: ${ENABLE_CODE_COVERAGE}")
message(STATUS "Enable optimize: ${KADD_OPTIMIZE}")
message(STATUS "Enable ARM SVE: ${ENABLE_ARM_SVE}")
message(STATUS "Assets path: ${KADD_ASSETS_PATH} (auto-detected)")
if(ENABLE_ARM_SVE)
    message(STATUS "  SVE2 enabled: ${ENABLE_ARM_SVE2}")
endif()

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
    # 未指定，搜索 third_party 和系统路径
    set(SEARCH_PATHS "${CMAKE_SOURCE_DIR}/third_party/onnxruntime" "/usr/local" "/usr")
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
# 验证结果
if(NOT ONNXRUNTIME_LIBRARIES OR NOT ONNXRUNTIME_INCLUDE_DIRS)
    message(FATAL_ERROR "ONNX Runtime not found!"
        "\n  Solutions:"
        "\n    1. Set ONNXRUNTIME_HOME environment variable"
        "\n    2. Install: sudo apt-get install libonnxruntime-dev"
        "\n    3. Place in: third_party/lib/")
endif()

get_filename_component(ONNXRUNTIME_LIB_DIR ${ONNXRUNTIME_LIBRARIES} DIRECTORY)
message(STATUS "Found ONNX Runtime: ${ONNXRUNTIME_LIBRARIES}")

# ============================================================================
# cJSON 依赖
# ============================================================================
# cJSON 查找
set(CJSON_FOUND FALSE)
if(USE_SYSTEM_LIBS)
    find_path(CJSON_INCLUDE_DIR
        NAMES cJSON.h
        PATHS
            /usr/include /usr/local/include
            $ENV{CJSON_ROOT_DIR}
        PATH_SUFFIXES cjson
    )

    find_library(CJSON_LIBRARY
        NAMES cjson
        PATHS
            /usr/lib /usr/local/lib /usr/lib64
    )

    if(CJSON_INCLUDE_DIR AND CJSON_LIBRARY)
        set(CJSON_FOUND TRUE)
        set(CJSON_INCLUDE_DIRS "${CJSON_INCLUDE_DIR}")
        set(CJSON_LIBRARIES "${CJSON_LIBRARY}")
        message(STATUS "cJSON include: ${CJSON_INCLUDE_DIR}")
        message(STATUS "cJSON library: ${CJSON_LIBRARY}")
    endif()
endif()

if(NOT CJSON_FOUND)
    # 使用 third_party 中的 cJSON
    set(cjson_path "${CMAKE_SOURCE_DIR}/third_party/cJSON")
    set(cjson_header "${cjson_path}/cJSON.h")
    set(cjson_source "${cjson_path}/cJSON.c")

    if(EXISTS "${cjson_header}" AND EXISTS "${cjson_source}")
        set(CJSON_FOUND TRUE)
        set(CJSON_INCLUDE_DIRS "${cjson_path}")
        set(CJSON_LIBRARIES "${cjson_source}")
        set(CJSON_IS_THIRD_PARTY TRUE)
        message(STATUS "Using third_party cJSON")
    else()
        message(WARNING "cJSON not found! JSON-dependent features will be disabled.")
    endif()
endif()

# ============================================================================
# Google Test 依赖 (仅在 BUILD_TESTS 时)
# ============================================================================
set(GTest_FOUND FALSE)
if(BUILD_TESTS)
    if(USE_SYSTEM_LIBS)
        find_package(GTest QUIET)
        if(GTest_FOUND)
            message(STATUS "Found system Google Test")
        endif()
    endif()

    if(NOT GTest_FOUND)
        # 使用 third_party 中的 Google Test
        set(gtest_path "${CMAKE_SOURCE_DIR}/third_party/gtest")
        set(gtest_source "${gtest_path}/src/gtest-all.cc")

        if(EXISTS "${gtest_source}")
            message(STATUS "Using third_party Google Test")
            set(GTEST_INCLUDE_DIR "${gtest_path}/include")
            set(GTest_FOUND TRUE)

            # 创建 gtest 静态库
            # 使用 CMAKE_CURRENT_SOURCE_DIR 确保 gtest-all.cc 的 include 路径正确
            add_library(gtest STATIC "${gtest_source}")
            target_include_directories(gtest PUBLIC "${GTEST_INCLUDE_DIR}")
            target_include_directories(gtest PRIVATE "${gtest_path}")  # 为 #include "src/xxx.cc" 提供路径
            # gtest 自动继承全局 CMAKE_CXX_FLAGS 中的安全编译选项

            add_library(gtest_main STATIC "${gtest_path}/src/gtest_main.cc")
            target_include_directories(gtest_main PUBLIC "${GTEST_INCLUDE_DIR}")
            target_include_directories(gtest_main PRIVATE "${gtest_path}")  # 为 gtest_main.cc 的 include 提供
            target_link_libraries(gtest_main PUBLIC gtest)
            # gtest_main 自动继承全局 CMAKE_CXX_FLAGS 中的安全编译选项
        else()
            message(WARNING "Google Test not found! Tests will be disabled.")
            set(BUILD_TESTS OFF)
        endif()
    endif()
endif()

# ============================================================================
# 全局包含目录
# ============================================================================
include_directories("${CMAKE_SOURCE_DIR}/include")
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
elseif(ENABLE_ARM_FUZZ)
    # ARM环境强制启用FUZZ测试
    set(FUZZ_TESTS_AVAILABLE TRUE CACHE BOOL "LibFuzzer tests available (ARM)" FORCE)

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
# 子目录 (不包含 tools 目录)
# ============================================================================
add_subdirectory(src)

# 仅在启用测试时添加 tests 目录
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
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
message(STATUS "  C Standard:      ${C_STANDARD}")
message(STATUS "  CXX Standard:    ${CXX_STANDARD}")
message(STATUS "")
message(STATUS "  Dependencies:")
message(STATUS "    ONNX Runtime:  ${ONNXRUNTIME_LIBRARIES}")
message(STATUS "    cJSON:         ${CJSON_FOUND}")
message(STATUS "    Google Test:   ${GTest_FOUND}")
message(STATUS "")
message(STATUS "  Testing:")
message(STATUS "    Unit Tests:    ${BUILD_TESTS}")
message(STATUS "    FUZZ Tests:    ${FUZZ_TESTS_AVAILABLE}")
if(FUZZ_TESTS_AVAILABLE)
	if(ENABLE_ARM_SVE)
		message(STATUS "      ARM SVE: ${ENABLE_ARM_SVE}")
	endif()
endif()
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
