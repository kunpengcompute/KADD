cmake_minimum_required(VERSION 3.16)
project(KADD VERSION 1.0.0 LANGUAGES C CXX ASM)

# C/C++ 标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 版本信息
string(TIMESTAMP BUILDDATE "%Y.%m.%d")
set(KADD_VERSION_BUILDDATE "${BUILDDATE}")

# 输出路径配置
set(LIBRARY_OUTPUT_PATH "${PROJECT_SOURCE_DIR}/output")
set(EXECUTABLE_OUTPUT_PATH "${PROJECT_SOURCE_DIR}/output")
set(CMAKE_INSTALL_PREFIX "${PROJECT_SOURCE_DIR}/output")
message("-- Library output path: ${LIBRARY_OUTPUT_PATH}")
message("-- Executable output path: ${EXECUTABLE_OUTPUT_PATH}")

# ============================================================================
# 选项配置
# ============================================================================
option(BUILD_TESTS "Build test programs" ON)
option(USE_SYSTEM_LIBS "Use system libraries instead of third_party" ON)
option(ENABLE_CODE_COVERAGE "Enable code coverage reporting" OFF)
option(KADD_OPTIMIZE "Enable optimizations" OFF)

# 优化选项
if(KADD_OPTIMIZE)
    add_definitions(-DKADD_OPTIMIZE)
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
endif()

# 代码覆盖率
if(ENABLE_CODE_COVERAGE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --coverage")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} --coverage")
endif()

# 依赖查找策略: 优先使用系统库，如果找不到则使用 third_party
message(STATUS "Build tests: ${BUILD_TESTS}")
message(STATUS "Use system libs: ${USE_SYSTEM_LIBS}")
message(STATUS "Enable coverage: ${ENABLE_CODE_COVERAGE}")
message(STATUS "Enable optimize: ${KADD_OPTIMIZE}")

# ============================================================================
# ONNX Runtime 依赖
# ============================================================================
# ONNX Runtime 查找
if(USE_SYSTEM_LIBS)
    find_package(onnxruntime QUIET)
    if(onnxruntime_FOUND)
        message(STATUS "Found system ONNX Runtime")
        set(ONNXRUNTIME_INCLUDE_DIRS ${onnxruntime_INCLUDE_DIRS})
        set(ONNXRUNTIME_LIBRARIES ${onnxruntime_LIBRARIES})
    endif()
endif()

if(NOT onnxruntime_FOUND)
    # 使用 third_party 中的 ONNX Runtime
    set(ONNX_LIB_FOUND FALSE)
    set(ONNX_LIB_NAMES "onnxruntime.dll" "libonnxruntime.dylib" "libonnxruntime.so")

    # 检查版本化的库文件 (Linux)
    if(UNIX AND NOT APPLE)
        list(APPEND ONNX_LIB_NAMES
            "libonnxruntime.so.1.16.0"
            "libonnxruntime.so.1.17.0"
            "libonnxruntime.so.1.18.0"
            "libonnxruntime.so.1.20.0"
        )
    endif()

    # 搜索库文件
    foreach(lib_name ${ONNX_LIB_NAMES})
        set(lib_path "${CMAKE_SOURCE_DIR}/third_party/lib/${lib_name}")
        if(EXISTS "${lib_path}")
            set(ONNXRUNTIME_LIBRARIES "${lib_path}")
            set(ONNXRUNTIME_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/third_party/include")
            set(ONNX_LIB_FOUND TRUE)
            message(STATUS "Using third_party ONNX Runtime: ${lib_name}")
            break()
        endif()
    endforeach()

    if(NOT ONNX_LIB_FOUND)
        message(FATAL_ERROR "ONNX Runtime not found in third_party/lib/")
        message("  Looking for: ${ONNX_LIB_NAMES}")
        message("  Search path: ${CMAKE_SOURCE_DIR}/third_party/lib/")
    endif()
endif()

# ============================================================================
# cJSON 依赖
# ============================================================================
# cJSON 查找
set(CJSON_FOUND FALSE)
if(USE_SYSTEM_LIBS)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(CJSON IMPORTED_TARGET cjson)
    endif()

    if(NOT CJSON_FOUND)
        find_path(CJSON_INCLUDE_DIR cJSON.h)
        find_library(CJSON_LIBRARY cjson)
        if(CJSON_INCLUDE_DIR AND CJSON_LIBRARY)
            set(CJSON_FOUND TRUE)
            set(CJSON_INCLUDE_DIRS "${CJSON_INCLUDE_DIR}")
            set(CJSON_LIBRARIES "${CJSON_LIBRARY}")
            message(STATUS "Found system cJSON")
        endif()
    endif()
endif()

if(NOT CJSON_FOUND)
    # 使用 third_party 中的 cJSON
    set(cjson_header "${CMAKE_SOURCE_DIR}/third_party/include/cJSON.h")
    set(cjson_source "${CMAKE_SOURCE_DIR}/third_party/src/cJSON.c")

    if(EXISTS "${cjson_header}" AND EXISTS "${cjson_source}")
        set(CJSON_FOUND TRUE)
        set(CJSON_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/third_party/include")
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
            set(GTEST_INCLUDE_DIR "${gtest_path}")
            set(GTest_FOUND TRUE)

            # 创建 gtest 静态库
            add_library(gtest STATIC "${gtest_source}")
            target_include_directories(gtest PUBLIC "${GTEST_INCLUDE_DIR}")

            add_library(gtest_main STATIC "${gtest_path}/src/gtest_main.cc")
            target_include_directories(gtest_main PUBLIC "${GTEST_INCLUDE_DIR}")
            target_link_libraries(gtest_main PUBLIC gtest)
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
# 子目录
# ============================================================================
add_subdirectory(src)
add_subdirectory(demo)
add_subdirectory(tests)

# ============================================================================
# 测试程序配置
# ============================================================================
if(BUILD_TESTS AND GTest_FOUND)
    enable_testing()

    # ML 引擎单元测试
    add_executable(ml_engine_test tests/test_ml_engine_gtest.cpp)
    target_compile_features(ml_engine_test PRIVATE cxx_std_17)
    target_link_libraries(ml_engine_test PRIVATE kadd_other_engines)
    target_link_libraries(ml_engine_test PRIVATE "${ONNXRUNTIME_LIBRARIES}")
    target_include_directories(ml_engine_test PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
        "${ONNXRUNTIME_INCLUDE_DIRS}"
    )

    # 链接 Google Test
    if(TARGET gtest_main)
        target_link_libraries(ml_engine_test PRIVATE gtest_main)
    elseif(GTest_FOUND)
        target_link_libraries(ml_engine_test PRIVATE GTest::gtest GTest::gtest_main)
    endif()

    # 链接 cJSON
    if(CJSON_FOUND)
        if(CJSON_IS_THIRD_PARTY)
            target_sources(ml_engine_test PRIVATE "${CJSON_LIBRARIES}")
        else()
            target_link_libraries(ml_engine_test PRIVATE "${CJSON_LIBRARIES}")
            target_include_directories(ml_engine_test PRIVATE "${CJSON_INCLUDE_DIRS}")
        endif()
    endif()

    # 特征引擎单元测试
    add_executable(feature_engine_test tests/test_feature_engine_gtest.cpp)
    target_compile_features(feature_engine_test PRIVATE cxx_std_17)
    target_link_libraries(feature_engine_test PRIVATE kadd_other_engines)
    target_include_directories(feature_engine_test PRIVATE "${CMAKE_SOURCE_DIR}/include")

    # 链接 Google Test
    if(TARGET gtest_main)
        target_link_libraries(feature_engine_test PRIVATE gtest_main)
    elseif(GTest_FOUND)
        target_link_libraries(feature_engine_test PRIVATE GTest::gtest GTest::gtest_main)
    endif()

    # 注册测试
    add_test(NAME MLEngineTest COMMAND ml_engine_test)
    add_test(NAME FeatureEngineTest COMMAND feature_engine_test)

    message(STATUS "Tests configured: MLEngineTest, FeatureEngineTest")
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
message(STATUS "============================================================")
