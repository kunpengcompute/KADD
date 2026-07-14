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

# 复制 assets 目录
add_custom_command(TARGET copy_test_resources POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        ${CMAKE_BINARY_DIR}/tests/assets
    COMMENT "Copying assets directory for tests"
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
    add_executable(${TEST_NAME} ${SOURCE_FILE})
    target_compile_features(${TEST_NAME} PRIVATE cxx_std_17)

    # 安全编译选项继承自父 CMakeLists.txt 的 CMAKE_C_FLAGS/CMAKE_CXX_FLAGS
    # 包含: -fPIC -fPIE -fstack-protector-strong -Wformat -Wformat-security -ftrapv
    # 链接选项继承自 CMAKE_EXE_LINKER_FLAGS: -Wl,-z,relro,-z,now

    # 链接必要的引擎库（直接链接动态库，避免重复编译）
    target_link_libraries(${TEST_NAME} PRIVATE
        kadd_dfa_engine
        kadd_feature_engine
        kadd_ml_engine
    )
    if(CJSON_FOUND)
        target_link_libraries(${TEST_NAME} PRIVATE
            kadd_sqli_engine
            kadd_config_loader
        )
    endif()

    target_include_directories(${TEST_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/include)

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

    # 构建未启用 RPATH（安全考虑），通过 ctest ENVIRONMENT 自动注入库搜索路径
    set_tests_properties(${TEST_NAME} PROPERTIES
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
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
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

    # LibFuzzer 编译和链接选项（ASan + Coverage + Fuzzer 三合一）
    target_compile_options(kadd_fuzzer PRIVATE
        -fsanitize=fuzzer,address
        -fprofile-instr-generate
        -fcoverage-mapping
        -g
    )
    target_link_options(kadd_fuzzer PRIVATE
        -fsanitize=fuzzer,address
    )

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

    target_include_directories(kadd_fuzzer PRIVATE ${CMAKE_SOURCE_DIR}/include)

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
else()
    # ----------------------------------------------------------------------------
    # GTest 单元测试 (GCC 编译器时自动启用)
    # ----------------------------------------------------------------------------

    # DFA 引擎测试
    add_kadd_gtest(dfa_engine_test test_dfa_engine_gtest.cpp)

    # ML 引擎测试
    add_kadd_gtest(ml_engine_test test_ml_engine_gtest.cpp)

    # Feature 引擎测试
    add_kadd_gtest(feature_engine_test test_feature_engine_gtest.cpp)

    # SQLi 引擎测试
    add_kadd_gtest(sqli_engine_test test_sqli_engine_gtest.cpp)

    # Config Loader 测试
    add_kadd_gtest(config_loader_test test_config_loader_gtest.cpp)

    message(STATUS "Tests configured: dfa_engine_test, ml_engine_test, feature_engine_test, sqli_engine_test, config_loader_test")
    message(STATUS "  To run tests: cd tests && ctest --output-on-failure")
    message(STATUS "  Or run individual tests: ./tests/dfa_engine_test")
    if(ENABLE_CODE_COVERAGE)
        message(STATUS "  Run 'make coverage' after tests to generate report")
    endif()
endif()
