cmake_minimum_required(VERSION 3.15)
project(KADD VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ========== 依赖查找策略 ==========
# 优先使用系统库，如果找不到则使用 third_party
# 可以通过 -DUSE_SYSTEM_LIBS=OFF 强制使用 third_party

option(USE_SYSTEM_LIBS "Use system libraries instead of third_party" ON)

# ========== ONNX Runtime ==========
if(USE_SYSTEM_LIBS)
    # 尝试查找系统安装的 ONNX Runtime
    find_package(onnxruntime QUIET)
    if(onnxruntime_FOUND)
        message(STATUS "Using system ONNX Runtime")
        set(ONNXRUNTIME_INCLUDE_DIRS ${onnxruntime_INCLUDE_DIRS})
        set(ONNXRUNTIME_LIBRARIES ${onnxruntime_LIBRARIES})
    else()
        message(STATUS "System ONNX Runtime not found, checking third_party...")
        set(USE_SYSTEM_LIBS_OFF ON)
    endif()
endif()

if(NOT onnxruntime_FOUND)
    # 使用 third_party 中的 ONNX Runtime
    if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/lib/onnxruntime.dll" OR
       EXISTS "${CMAKE_SOURCE_DIR}/third_party/lib/libonnxruntime.so")
        message(STATUS "Using third_party ONNX Runtime")
        set(ONNXRUNTIME_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/third_party/include)
        if(WIN32)
            set(ONNXRUNTIME_LIBRARIES ${CMAKE_SOURCE_DIR}/third_party/lib/onnxruntime.dll)
        elseif(APPLE)
            set(ONNXRUNTIME_LIBRARIES ${CMAKE_SOURCE_DIR}/third_party/lib/libonnxruntime.dylib)
        else()
            set(ONNXRUNTIME_LIBRARIES ${CMAKE_SOURCE_DIR}/third_party/lib/libonnxruntime.so)
        endif()
    else()
        message(FATAL_ERROR "ONNX Runtime not found! Please install it or place it in third_party/lib/")
    endif()
endif()

# ========== cJSON ==========
if(USE_SYSTEM_LIBS)
    # 尝试查找系统安装的 cJSON
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(CJSON QUIET cjson)
    endif()

    if(NOT CJSON_FOUND)
        find_path(CJSON_INCLUDE_DIR cJSON.h)
        find_library(CJSON_LIBRARY cjson)
        if(CJSON_INCLUDE_DIR AND CJSON_LIBRARY)
            set(CJSON_FOUND TRUE)
            set(CJSON_INCLUDE_DIRS ${CJSON_INCLUDE_DIR})
            set(CJSON_LIBRARIES ${CJSON_LIBRARY})
            message(STATUS "Using system cJSON")
        endif()
    endif()

    if(NOT CJSON_FOUND)
        message(STATUS "System cJSON not found, checking third_party...")
    endif()
endif()

if(NOT CJSON_FOUND)
    # 使用 third_party 中的 cJSON
    if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/include/cJSON.h" AND
       EXISTS "${CMAKE_SOURCE_DIR}/third_party/src/cJSON.c")
        message(STATUS "Using third_party cJSON")
        set(CJSON_FOUND TRUE)
        set(CJSON_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/third_party/include)
        set(CJSON_LIBRARIES ${CMAKE_SOURCE_DIR}/third_party/src/cJSON.c)
        set(CJSON_IS_THIRD_PARTY TRUE)
    else()
        message(WARNING "cJSON not found! SQLi engine and DFA Pro will not be available.")
        set(CJSON_FOUND FALSE)
    endif()
endif()

# ========== Google Test ==========
if(BUILD_TESTS)
    if(USE_SYSTEM_LIBS)
        # 尝试查找系统安装的 Google Test
        find_package(GTest QUIET)
        if(GTest_FOUND)
            message(STATUS "Using system Google Test")
        else()
            message(STATUS "System Google Test not found, checking third_party...")
        endif()
    endif()

    if(NOT GTest_FOUND)
        # 使用 third_party 中的 Google Test
        if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/gtest/src/gtest-all.cc")
            message(STATUS "Using third_party Google Test")
            set(GTEST_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/third_party/gtest)
            set(GTEST_ROOT_DIR ${CMAKE_SOURCE_DIR}/third_party/gtest)

            # 添加 gtest 库
            add_library(gtest STATIC
                ${GTEST_ROOT_DIR}/src/gtest-all.cc
            )
            target_include_directories(gtest PUBLIC ${GTEST_INCLUDE_DIR})

            add_library(gtest_main STATIC
                ${GTEST_ROOT_DIR}/src/gtest_main.cc
            )
            target_include_directories(gtest_main PUBLIC ${GTEST_INCLUDE_DIR})
            target_link_libraries(gtest_main PUBLIC gtest)

            set(GTest_FOUND TRUE)
        else()
            message(WARNING "Google Test not found! Tests will not be available.")
            set(BUILD_TESTS OFF)
        endif()
    endif()
endif()

# ========== 全局包含目录 ==========
# 只添加必要的包含目录，避免污染
include_directories(${CMAKE_SOURCE_DIR}/include)
if(CJSON_FOUND AND CJSON_IS_THIRD_PARTY)
    include_directories(${CJSON_INCLUDE_DIRS})
endif()

# ========== 添加子目录 ==========
add_subdirectory(src)

# ========== Demo 程序 ==========
add_subdirectory(demo)

# ========== 测试程序 ==========
add_subdirectory(tests)

# ========== 简单测试程序 ==========
# add_executable(test_dfa_basic tools/scripts/test_dfa_basic.c)
# target_link_libraries(test_dfa_basic PRIVATE kadd_dfa_engine)

# ========== DFA准确率测试 ==========
# add_executable(dfa_accuracy_test tests/dfa_accuracy_test.c)
# target_link_libraries(dfa_accuracy_test PRIVATE kadd_dfa_engine)
# if(CJSON_FOUND AND CJSON_IS_THIRD_PARTY)
#     target_include_directories(dfa_accuracy_test PRIVATE ${CMAKE_SOURCE_DIR}/third_party/include)
#     target_sources(dfa_accuracy_test PRIVATE ${CJSON_LIBRARIES})
# endif()

# ========== DFA引擎Pro版本测试 ==========
# add_executable(dfa_engine_pro_test tests/dfa_engine_pro_test.c)
# target_link_libraries(dfa_engine_pro_test PRIVATE kadd_dfa_engine_pro libinjection)
# target_include_directories(dfa_engine_pro_test PRIVATE ${CMAKE_SOURCE_DIR}/third_party/include)

# ========== DFA引擎Pro版本准确率测试 ==========
# add_executable(dfa_accuracy_pro_test tests/dfa_accuracy_pro_test.c)
# target_link_libraries(dfa_accuracy_pro_test PRIVATE kadd_dfa_engine_pro libinjection)
# target_include_directories(dfa_accuracy_pro_test PRIVATE ${CMAKE_SOURCE_DIR}/third_party/include)
# if(CJSON_FOUND AND CJSON_IS_THIRD_PARTY)
#     target_sources(dfa_accuracy_pro_test PRIVATE ${CJSON_LIBRARIES})
# endif()

# ========== libinjection 支持 ==========
# message(STATUS "Fetching libinjection...")
# FetchContent_Declare(
#     libinjection
#     GIT_REPOSITORY https://github.com/client9/libinjection.git
#     GIT_TAG        master
#     GIT_SHALLOW    TRUE
# )
# FetchContent_MakeAvailable(libinjection)

# add_library(libinjection STATIC
#     ${libinjection_SOURCE_DIR}/src/libinjection_sqli.c
#     ${libinjection_SOURCE_DIR}/src/libinjection_xss.c
#     ${libinjection_SOURCE_DIR}/src/libinjection_html5.c
# )

# target_include_directories(libinjection PUBLIC
#     ${libinjection_SOURCE_DIR}/src
# )

# ========== DFA 引擎单元测试 ==========
# add_executable(dfa_engine_test tests/dfa_engine_test.cpp)
#
# target_compile_features(dfa_engine_test PRIVATE cxx_std_17)
#
# target_link_libraries(dfa_engine_test PRIVATE
#     kadd_dfa_engine
#     libinjection
#     gtest_main
# )
#
# target_include_directories(dfa_engine_test PRIVATE
#     ${CMAKE_SOURCE_DIR}/include
#     ${libinjection_SOURCE_DIR}/src
# )

# ========== ML 引擎单元测试 ==========
if(BUILD_TESTS AND GTest_FOUND)
    add_executable(ml_engine_test tests/test_ml_engine_gtest.cpp)

    target_compile_features(ml_engine_test PRIVATE cxx_std_17)

    target_link_libraries(ml_engine_test PRIVATE
        kadd_other_engines
    )

    # 链接 ONNX Runtime
    target_link_libraries(ml_engine_test PRIVATE ${ONNXRUNTIME_LIBRARIES})
    target_include_directories(ml_engine_test PRIVATE ${ONNXRUNTIME_INCLUDE_DIRS})

    # 链接 Google Test
    if(TARGET gtest_main)
        target_link_libraries(ml_engine_test PRIVATE gtest_main)
    elseif(GTest_FOUND)
        target_link_libraries(ml_engine_test PRIVATE GTest::gtest GTest::gtest_main)
    endif()

    target_include_directories(ml_engine_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )

    # 链接 cJSON (如果使用 third_party)
    if(CJSON_FOUND AND CJSON_IS_THIRD_PARTY)
        target_sources(ml_engine_test PRIVATE ${CJSON_LIBRARIES})
    elseif(CJSON_FOUND)
        target_link_libraries(ml_engine_test PRIVATE ${CJSON_LIBRARIES})
        target_include_directories(ml_engine_test PRIVATE ${CJSON_INCLUDE_DIRS})
    endif()

    # ========== 特征引擎单元测试 ==========
    add_executable(feature_engine_test tests/test_feature_engine_gtest.cpp)

    target_compile_features(feature_engine_test PRIVATE cxx_std_17)

    target_link_libraries(feature_engine_test PRIVATE
        kadd_other_engines
    )

    # 链接 Google Test
    if(TARGET gtest_main)
        target_link_libraries(feature_engine_test PRIVATE gtest_main)
    elseif(GTest_FOUND)
        target_link_libraries(feature_engine_test PRIVATE GTest::gtest GTest::gtest_main)
    endif()

    target_include_directories(feature_engine_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )

    # 启用测试
    enable_testing()
    # add_test(NAME DFAEngineTest COMMAND dfa_engine_test)
    add_test(NAME MLEngineTest COMMAND ml_engine_test)
    add_test(NAME FeatureEngineTest COMMAND feature_engine_test)
endif()
