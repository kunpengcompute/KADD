# Tests CMakeLists.txt (仅 gtest 测试)

# ============================================================================
# 资源文件复制
# ============================================================================
add_custom_target(copy_test_resources ALL
    COMMAND ${CMAKE_COMMAND} -E echo "Copying test resources..."
)

# 复制 config.json
add_custom_command(TARGET copy_test_resources POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_SOURCE_DIR}/config.json
        ${CMAKE_BINARY_DIR}/tests/config.json
    COMMENT "Copying config.json for tests"
)

# 复制 assets 目录到 build 根目录（config.json 的 ../assets 指向此处）
add_custom_command(TARGET copy_test_resources POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        ${CMAKE_BINARY_DIR}/assets
    COMMENT "Copying assets directory for tests and demos"
)

# 同时也复制到 tests/assets（用于 TEST_MODEL_PATH 宏）
add_custom_command(TARGET copy_test_resources POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        ${CMAKE_BINARY_DIR}/tests/assets
    COMMENT "Copying assets to tests directory"
)

# 创建 results 输出目录
add_custom_command(TARGET copy_test_resources POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        ${CMAKE_BINARY_DIR}/tests/results
    COMMENT "Creating tests/results output directory"
)

# ============================================================================
# 通用测试函数
# ============================================================================
function(add_kadd_gtest TEST_NAME SOURCE_FILE)
    # 额外参数作为排除列表
    set(EXCLUDE_LIBS ${ARGN})

    add_executable(${TEST_NAME} ${SOURCE_FILE})
    target_compile_features(${TEST_NAME} PRIVATE cxx_std_17)

    # 安全编译选项继承自父 CMakeLists.txt 的 CMAKE_C_FLAGS/CMAKE_CXX_FLAGS
    # 包含: -fPIC -fPIE -fstack-protector-strong -Wformat -Wformat-security -ftrapv
    # 链接选项继承自 CMAKE_EXE_LINKER_FLAGS: -Wl,-z,relro,-z,now

    # 链接必要的引擎库
    set(_engine_libs
        kadd_dfa_engine
        kadd_feature_engine
        kadd_ml_engine
        kadd_inference_engine
    )
    if(EXCLUDE_LIBS)
        list(REMOVE_ITEM _engine_libs ${EXCLUDE_LIBS})
    endif()
    target_link_libraries(${TEST_NAME} PRIVATE ${_engine_libs})
    if(CJSON_FOUND)
        set(_cjson_libs kadd_sqli_engine kadd_config_loader)
        if(EXCLUDE_LIBS)
            list(REMOVE_ITEM _cjson_libs ${EXCLUDE_LIBS})
        endif()
        if(_cjson_libs)
            target_link_libraries(${TEST_NAME} PRIVATE ${_cjson_libs})
        endif()
    endif()

    # 包含头文件和源文件目录（用于 #include "*.c"）
    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/internal
        ${CMAKE_SOURCE_DIR}/src
    )

    # Google Test
    if(TARGET gtest_main)
        target_link_libraries(${TEST_NAME} PRIVATE gtest_main)
    elseif(GTest_FOUND)
        target_link_libraries(${TEST_NAME} PRIVATE GTest::gtest_main)
    endif()

    # ONNX Runtime
    target_link_libraries(${TEST_NAME} PRIVATE ${ONNXRUNTIME_LIBRARIES})
    target_include_directories(${TEST_NAME} PRIVATE ${ONNXRUNTIME_INCLUDE_DIRS})

    # cJSON
    if(CJSON_FOUND)
        if(CJSON_IS_THIRD_PARTY)
            target_sources(${TEST_NAME} PRIVATE ${CJSON_LIBRARIES})
        else()
            target_link_libraries(${TEST_NAME} PRIVATE ${CJSON_LIBRARIES})
            target_include_directories(${TEST_NAME} PRIVATE ${CJSON_INCLUDE_DIRS})
        endif()
    endif()

    # 定义模型和数据路径（统一使用 build/tests/assets）
    target_compile_definitions(${TEST_NAME} PRIVATE
        TEST_MODEL_PATH="assets/model/model_tokens_rf.onnx"
        TEST_DATA_PATH="assets/data/parsed_results_fixed.json"
    )

    # 添加资源复制依赖
    add_dependencies(${TEST_NAME} copy_test_resources)

    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})

    # 构建未启用 RPATH（安全考虑），通过 ctest WORKING_DIRECTORY + ENVIRONMENT 注入
    set_tests_properties(${TEST_NAME} PROPERTIES
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests
        ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR}/src:${ONNXRUNTIME_LIB_DIR}")
endfunction()

# ============================================================================
# 代码覆盖率
# ============================================================================
if(ENABLE_CODE_COVERAGE)
    add_custom_target(coverage
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}
            lcov --capture --base-directory ${CMAKE_SOURCE_DIR} --directory ${CMAKE_BINARY_DIR} --output-file coverage.info
        COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}
            lcov --remove coverage.info
                '/usr/*'
                '*util/*'
                '*tests/*'
                '*include/*'
                '*tools/*'
                --output-file coverage.info
        COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}
            genhtml coverage.info --output-directory coverage_html
        COMMENT "Run tests and generate code coverage report"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    )

    add_custom_target(coverage-clean
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/coverage_html
        COMMAND ${CMAKE_COMMAND} -E remove -f ${CMAKE_BINARY_DIR}/coverage.info*
        COMMENT "Clean coverage data"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endif()

# ============================================================================
# 测试程序
# ============================================================================

if(FUZZ_TESTS_AVAILABLE)
    # ----------------------------------------------------------------------------
    # FUZZ 测试 (Clang 编译器时自动启用)
    # ----------------------------------------------------------------------------
    # 创建 kadd_fuzzer 可执行文件
    add_executable(kadd_fuzzer
        fuzz/kadd_fuzzer.cpp
        fuzz/fuzz_utils.cpp
    )

    target_compile_features(kadd_fuzzer PRIVATE cxx_std_17)

    # 覆盖率构建：clang -fprofile-instr-generate + libFuzzer
    if(ENABLE_CODE_COVERAGE)
        target_compile_options(kadd_fuzzer PRIVATE
            -fsanitize=fuzzer
            -fprofile-instr-generate
            -fcoverage-mapping
            -g
        )
        target_link_options(kadd_fuzzer PRIVATE -fsanitize=fuzzer -fprofile-instr-generate -fcoverage-mapping)
    else()
        target_compile_options(kadd_fuzzer PRIVATE
            -fsanitize=fuzzer,address
            -fprofile-instr-generate
            -fcoverage-mapping
            -g
        )
        target_link_options(kadd_fuzzer PRIVATE -fsanitize=fuzzer,address)
    endif()

    # RELRO 和 BIND_NOW 继承自 CMAKE_EXE_LINKER_FLAGS: -Wl,-z,relro,-z,now

    # 链接必要的引擎库（直接链接动态库，避免重复编译）
    target_link_libraries(kadd_fuzzer PRIVATE
        kadd_dfa_engine
        kadd_feature_engine
        kadd_ml_engine
    )
    if(CJSON_FOUND)
        target_link_libraries(kadd_fuzzer PRIVATE
            kadd_sqli_engine
            kadd_config_loader
        )
    endif()

    target_include_directories(kadd_fuzzer PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/include/internal)

    # ONNX Runtime
    target_link_libraries(kadd_fuzzer PRIVATE ${ONNXRUNTIME_LIBRARIES})
    target_include_directories(kadd_fuzzer PRIVATE ${ONNXRUNTIME_INCLUDE_DIRS})

    # cJSON
    if(CJSON_FOUND)
        if(CJSON_IS_THIRD_PARTY)
            target_sources(kadd_fuzzer PRIVATE ${CJSON_LIBRARIES})
        else()
            target_link_libraries(kadd_fuzzer PRIVATE ${CJSON_LIBRARIES})
            target_include_directories(kadd_fuzzer PRIVATE ${CJSON_INCLUDE_DIRS})
        endif()
    endif()

    # RPATH 已移除，运行测试前需要设置 LD_LIBRARY_PATH

    # 添加资源复制依赖
    add_dependencies(kadd_fuzzer copy_test_resources)

    message(STATUS "FUZZ tests configured: kadd_fuzzer")
    message(STATUS "  Run: cd tests && ./kadd_fuzzer assets/fuzz_corpus -runs=10000")
endif()

# ----------------------------------------------------------------------------
# GTest 单元测试（与 fuzzer 同时构建，不互斥）
# ----------------------------------------------------------------------------

# DFA 引擎测试（包含源文件以测试 static 函数）
add_kadd_gtest(dfa_engine_test test_dfa_engine_gtest.cpp kadd_dfa_engine)

# ML 引擎测试（包含源文件以测试 static 函数）
add_kadd_gtest(ml_engine_test test_ml_engine_gtest.cpp kadd_ml_engine)

# Feature 引擎测试
add_kadd_gtest(feature_engine_test test_feature_engine_gtest.cpp)

# SQLi 引擎测试（包含源文件以测试 static 函数）
add_kadd_gtest(sqli_engine_test test_sqli_engine_gtest.cpp kadd_sqli_engine)
# sqli_engine_test includes kadd_sqli_engine.c directly, needs kadd_dl_engine for DL functions
target_link_libraries(sqli_engine_test PRIVATE kadd_dl_engine kadd_cnn_engine)
# SQLi ML/DL mode test
add_executable(sqli_ml_dl_mode_test test_sqli_ml_dl_mode_gtest.cpp)
target_compile_features(sqli_ml_dl_mode_test PRIVATE cxx_std_17)
target_include_directories(sqli_ml_dl_mode_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/internal
)
target_link_libraries(sqli_ml_dl_mode_test PRIVATE
    kadd_sqli_engine kadd_dl_engine kadd_ml_engine kadd_dfa_engine kadd_config_loader
)
if(TARGET gtest_main)
    target_link_libraries(sqli_ml_dl_mode_test PRIVATE gtest_main)
elseif(GTest_FOUND)
    target_link_libraries(sqli_ml_dl_mode_test PRIVATE GTest::gtest_main)
endif()
target_link_libraries(sqli_ml_dl_mode_test PRIVATE pthread)
add_dependencies(sqli_ml_dl_mode_test copy_test_resources)
add_test(NAME sqli_ml_dl_mode_test COMMAND sqli_ml_dl_mode_test)
set_tests_properties(sqli_ml_dl_mode_test PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR}/src:${ONNXRUNTIME_LIB_DIR}:$ENV{LD_LIBRARY_PATH}"
)
message(STATUS "SQLi ML/DL mode test enabled")


# Config Loader 测试（包含源文件以测试 static 函数）
add_kadd_gtest(config_loader_test test_config_loader_gtest.cpp kadd_config_loader)

# RF 引擎测试（不依赖 ONNX，不使用 add_kadd_gtest 以避免引入 ONNX 链接）


# CNN 引擎测试（kadd_cnn_engine + 共享数据加载）
add_executable(cnn_engine_gtest test_cnn_engine_gtest.cpp test_model_data.cpp)
target_compile_features(cnn_engine_gtest PRIVATE cxx_std_17)
target_include_directories(cnn_engine_gtest PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/include/internal ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(cnn_engine_gtest PRIVATE kadd_cnn_engine kadd_dfa_engine)
if(TARGET gtest_main)
    target_link_libraries(cnn_engine_gtest PRIVATE gtest_main)
elseif(GTest_FOUND)
    target_link_libraries(cnn_engine_gtest PRIVATE GTest::gtest_main)
endif()
target_link_libraries(cnn_engine_gtest PRIVATE pthread)
add_dependencies(cnn_engine_gtest copy_test_resources)
add_test(NAME cnn_engine_gtest COMMAND cnn_engine_gtest)
set_tests_properties(cnn_engine_gtest PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR}/src:$ENV{LD_LIBRARY_PATH}"
)

# DL 引擎综合测试（CNN/RF/ONNX 多后端，含模型校验链、CRC 防篡改等）
add_executable(dl_engine_gtest test_dl_engine_gtest.cpp)
target_compile_features(dl_engine_gtest PRIVATE cxx_std_17)
target_include_directories(dl_engine_gtest PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/internal
)
target_link_libraries(dl_engine_gtest PRIVATE
    kadd_sqli_engine kadd_dl_engine kadd_cnn_engine kadd_ml_engine
    kadd_dfa_engine kadd_config_loader
)
if(TARGET gtest_main)
    target_link_libraries(dl_engine_gtest PRIVATE gtest_main)
elseif(GTest_FOUND)
    target_link_libraries(dl_engine_gtest PRIVATE GTest::gtest_main)
endif()
target_link_libraries(dl_engine_gtest PRIVATE pthread)
add_dependencies(dl_engine_gtest copy_test_resources)
add_test(NAME dl_engine_gtest COMMAND dl_engine_gtest)
set_tests_properties(dl_engine_gtest PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR}/src:${ONNXRUNTIME_LIB_DIR}")
message(STATUS "DL engine comprehensive test enabled")

# RF 引擎测试（kadd_rf_engine + 共享数据加载）
add_executable(rf_engine_gtest test_rf_engine_gtest.cpp test_model_data.cpp)
target_compile_features(rf_engine_gtest PRIVATE cxx_std_17)
target_include_directories(rf_engine_gtest PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/include/internal ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(rf_engine_gtest PRIVATE
    kadd_rf_engine kadd_dfa_engine kadd_feature_engine)
if(TARGET gtest_main)
    target_link_libraries(rf_engine_gtest PRIVATE gtest_main)
elseif(GTest_FOUND)
    target_link_libraries(rf_engine_gtest PRIVATE GTest::gtest_main)
endif()
target_link_libraries(rf_engine_gtest PRIVATE pthread)
add_dependencies(rf_engine_gtest copy_test_resources)
add_test(NAME rf_engine_gtest COMMAND rf_engine_gtest)
set_tests_properties(rf_engine_gtest PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR}/src:$ENV{LD_LIBRARY_PATH}"
)

# 统一推理引擎测试（包含源文件以测试 static 函数）
add_kadd_gtest(inference_engine_test test_inference_engine_gtest.cpp kadd_inference_engine)
# 链接 gmock（如果可用）
if(TARGET gmock_main)
    target_link_libraries(inference_engine_test PRIVATE gmock_main)
endif()

message(STATUS "Tests configured: dfa_engine_test, ml_engine_test, feature_engine_test, sqli_engine_test, config_loader_test, inference_engine_test, cnn_engine_gtest, rf_engine_gtest")
message(STATUS "  To run tests: cd tests && ctest --output-on-failure")
if(ENABLE_CODE_COVERAGE)
    message(STATUS "  Run 'make coverage' after tests to generate report")
endif()
