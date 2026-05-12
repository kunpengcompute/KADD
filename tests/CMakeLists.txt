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

    # 链接必要的引擎库
    target_link_libraries(${TEST_NAME} PRIVATE kadd_other_engines)

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

    # 设置 RPATH 以便运行时找到 libonnxruntime.so 和 libcjson.so
    if(UNIX)
        # 获取 cJSON 库目录
        get_filename_component(CJSON_LIB_DIR ${CJSON_LIBRARIES} DIRECTORY)
        set_target_properties(${TEST_NAME} PROPERTIES
            INSTALL_RPATH "$ORIGIN:${ONNXRUNTIME_LIB_DIR}:${CJSON_LIB_DIR}:/usr/lib:/usr/lib64"
            BUILD_WITH_INSTALL_RPATH TRUE
        )
    endif()
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
            lcov --remove coverage.info '/usr/*' '${CMAKE_BINARY_DIR}/*' --output-file coverage.info
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
if(ENABLE_CODE_COVERAGE)
    message(STATUS "  Run 'make coverage' after tests to generate report")
endif()
