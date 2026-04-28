# Tests CMakeLists.txt

# JSON 解析器库（辅助工具）
add_library(json_parser STATIC
    ../tools/scripts/json_parser.c
)
target_include_directories(json_parser PUBLIC
    ${CMAKE_SOURCE_DIR}/tools/scripts
)

# URL解码器库（辅助工具）
add_library(url_decoder STATIC
    ../tools/scripts/url_decoder.c
)
target_include_directories(url_decoder PUBLIC
    ${CMAKE_SOURCE_DIR}/tools/scripts
)

# DFA 准确率测试 v2
add_executable(dfa_accuracy_test_v2
    dfa_accuracy_test_v2.c
)

target_link_libraries(dfa_accuracy_test_v2 PRIVATE
    kadd_dfa_engine
    json_parser
    url_decoder
)

target_include_directories(dfa_accuracy_test_v2 PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tools/scripts
)

# ML 推理测试
add_executable(test_ml_inference
    test_ml_inference.c
)

target_link_libraries(test_ml_inference PRIVATE
    onnxruntime
)

target_include_directories(test_ml_inference PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/include
)

# 简单 ML 测试
add_executable(test_ml_simple
    test_ml_simple.c
)

target_link_libraries(test_ml_simple PRIVATE
    kadd_other_engines
)

target_include_directories(test_ml_simple PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# 复制模型文件到构建目录
add_custom_command(TARGET test_ml_inference POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/tools/model_tokens_rf.onnx
        ${CMAKE_BINARY_DIR}/model_tokens_rf.onnx
    COMMENT "Copying model file for testing"
)

# 复制转移表文件到构建目录（用于测试）
add_custom_command(TARGET dfa_accuracy_test_v2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/tools/profile_data
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/tools/profile_data/transition_table.bin
        ${CMAKE_BINARY_DIR}/tools/profile_data/
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/tools/profile_data/states_meta.json
        ${CMAKE_BINARY_DIR}/tools/profile_data/
    COMMENT "Copying DFA table files for testing"
)
