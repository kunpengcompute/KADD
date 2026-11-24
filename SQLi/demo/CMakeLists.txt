# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# All rights reserved.
#
# Licensed under the License;
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://opensource.org/license/bsd-3-clause
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ==================== demo 目录 CMakeLists.txt ====================
# 示例程序，链接 src 构建的引擎库（配合顶层 CMake 一键编译）

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/demo)

file(GLOB DEMO_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.c")

foreach(demo_source ${DEMO_SOURCES})
    get_filename_component(demo_name ${demo_source} NAME_WE)
    add_executable(${demo_name} ${demo_source})
    target_include_directories(${demo_name} PRIVATE ${CMAKE_SOURCE_DIR}/include)

    # 按各 demo 实际用到的引擎链接（避免引入不必要的依赖，如 dfa/feature 不依赖 ONNX Runtime）
    if(demo_name STREQUAL "kadd_sqli_demo")
        target_link_libraries(${demo_name} PRIVATE kadd_sqli_engine kadd_config_loader)
    elseif(demo_name STREQUAL "kadd_dfa_demo")
        target_link_libraries(${demo_name} PRIVATE kadd_dfa_engine kadd_config_loader)
    elseif(demo_name STREQUAL "kadd_ml_demo")
        target_link_libraries(${demo_name} PRIVATE kadd_ml_engine kadd_config_loader)
    elseif(demo_name STREQUAL "kadd_dl_demo")
        target_link_libraries(${demo_name} PRIVATE kadd_dl_engine kadd_config_loader)
    elseif(demo_name STREQUAL "kadd_inference_demo")
        target_link_libraries(${demo_name} PRIVATE kadd_inference_engine kadd_config_loader)
    elseif(demo_name STREQUAL "kadd_feature_demo")
        target_link_libraries(${demo_name} PRIVATE kadd_feature_engine kadd_config_loader)
    elseif(demo_name STREQUAL "kadd_config_demo")
        target_link_libraries(${demo_name} PRIVATE kadd_config_loader)
    endif()

    message(STATUS "Demo target: ${demo_name}")
endforeach()

# demo 代码以 "../config.json" 加载配置 → 复制到 build/（即 build/demo 的上级目录）
add_custom_target(copy_demo_config ALL
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_SOURCE_DIR}/config.json ${CMAKE_BINARY_DIR}/config.json
    COMMENT "Copying config.json for demos"
)
