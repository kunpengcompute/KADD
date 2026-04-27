cmake_minimum_required(VERSION 3.15)
project(KADD VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 包含目录
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/third_party/include)

# 添加子目录
add_subdirectory(src)

# ========== 简单测试程序 ==========
add_executable(test_dfa_basic tools/scripts/test_dfa_basic.c)
target_link_libraries(test_dfa_basic PRIVATE kadd_dfa_engine)

# ========== DFA准确率测试 ==========
add_executable(dfa_accuracy_test tests/dfa_accuracy_test.c)
target_link_libraries(dfa_accuracy_test PRIVATE kadd_dfa_engine)
target_include_directories(dfa_accuracy_test PRIVATE ${CMAKE_SOURCE_DIR}/third_party/include)
target_sources(dfa_accuracy_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/libinjection_sqli.c
    ${CMAKE_SOURCE_DIR}/third_party/src/cJSON.c
)

# ========== DFA引擎Pro版本测试 ==========
add_executable(dfa_engine_pro_test tests/dfa_engine_pro_test.c)
target_link_libraries(dfa_engine_pro_test PRIVATE kadd_dfa_engine_pro libinjection)
target_include_directories(dfa_engine_pro_test PRIVATE ${CMAKE_SOURCE_DIR}/third_party/include)

# ========== DFA引擎Pro版本准确率测试 ==========
add_executable(dfa_accuracy_pro_test tests/dfa_accuracy_pro_test.c)
target_link_libraries(dfa_accuracy_pro_test PRIVATE kadd_dfa_engine_pro libinjection)
target_include_directories(dfa_accuracy_pro_test PRIVATE ${CMAKE_SOURCE_DIR}/third_party/include)
target_sources(dfa_accuracy_pro_test PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/src/cJSON.c
)

# ========== GoogleTest 配置 ==========
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        release-1.12.1
    GIT_SHALLOW    TRUE
)

if (CMAKE_VERSION VERSION_LESS 3.24)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
endif()

FetchContent_MakeAvailable(googletest)

# 禁用 gtest 的编译警告
if (MSVC)
    target_compile_options(gtest PUBLIC /W0 /wd4267)
    target_compile_options(gmock PUBLIC /W0 /wd4267)
    target_compile_options(gtest_main PUBLIC /W0 /wd4267)
    target_compile_options(gmock_main PUBLIC /W0 /wd4267)
endif()

# ========== libinjection 支持 ==========
message(STATUS "Fetching libinjection...")
FetchContent_Declare(
    libinjection
    GIT_REPOSITORY https://github.com/client9/libinjection.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(libinjection)

add_library(libinjection STATIC
    ${libinjection_SOURCE_DIR}/src/libinjection_sqli.c
    ${libinjection_SOURCE_DIR}/src/libinjection_xss.c
    ${libinjection_SOURCE_DIR}/src/libinjection_html5.c
)

target_include_directories(libinjection PUBLIC
    ${libinjection_SOURCE_DIR}/src
)

# ========== DFA 引擎单元测试 ==========
add_executable(dfa_engine_test tests/dfa_engine_test.cpp)

target_compile_features(dfa_engine_test PRIVATE cxx_std_17)

target_link_libraries(dfa_engine_test PRIVATE
    kadd_dfa_engine
    libinjection
    gtest_main
)

target_include_directories(dfa_engine_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${libinjection_SOURCE_DIR}/src
)

# ========== ML 引擎单元测试 ==========
add_executable(ml_engine_test tests/test_ml_engine_gtest.cpp)

target_compile_features(ml_engine_test PRIVATE cxx_std_17)

target_link_libraries(ml_engine_test PRIVATE
    kadd_other_engines
    gtest_main
    ${CMAKE_SOURCE_DIR}/third_party/lib/libonnxruntime.so
)

target_include_directories(ml_engine_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party/include
)

# ========== 特征引擎单元测试 ==========
add_executable(feature_engine_test tests/test_feature_engine_gtest.cpp)

target_compile_features(feature_engine_test PRIVATE cxx_std_17)

target_link_libraries(feature_engine_test PRIVATE
    kadd_other_engines
    gtest_main
)

target_include_directories(feature_engine_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# 启用测试
enable_testing()
add_test(NAME DFAEngineTest COMMAND dfa_engine_test)
add_test(NAME MLEngineTest COMMAND ml_engine_test)
add_test(NAME FeatureEngineTest COMMAND feature_engine_test)
