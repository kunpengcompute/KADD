# Tests CMakeLists.txt (仅 gtest 测试)

# ============================================================================
# 通用测试函数
# ============================================================================
function(add_kadd_gtest TEST_NAME SOURCE_FILE)
    add_executable(${TEST_NAME} ${SOURCE_FILE})
    target_compile_features(${TEST_NAME} PRIVATE cxx_std_17)
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

    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
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

add_kadd_gtest(ml_engine_test test_ml_engine_gtest.cpp)
add_kadd_gtest(feature_engine_test test_feature_engine_gtest.cpp)

message(STATUS "Tests configured: ml_engine_test, feature_engine_test")
if(ENABLE_CODE_COVERAGE)
    message(STATUS "  Run 'make coverage' after tests to generate report")
endif()
