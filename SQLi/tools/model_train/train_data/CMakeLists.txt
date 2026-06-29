cmake_minimum_required(VERSION 3.10)
project(csv_to_vector_tool C)

set(CMAKE_C_STANDARD 11)

# 设置编译选项
if(CMAKE_C_COMPILER_ID MATCHES "GNU" OR CMAKE_C_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wall -Wextra -O2)
endif()

# ============================================================================
# 计算项目根目录（关键修复）
# ============================================================================
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../" ABSOLUTE)
message(STATUS "Project Root: ${PROJECT_ROOT}")

# ============================================================================
# cJSON 依赖查找：优先 util/ 源码（源码内嵌），其次系统库
# ============================================================================
set(CJSON_FOUND FALSE)

# 1. 优先 util/cJSON 源码
set(cjson_path "${PROJECT_ROOT}/util/cJSON")
set(cjson_header "${cjson_path}/cJSON.h")
set(cjson_source "${cjson_path}/cJSON.c")
if(EXISTS "${cjson_header}" AND EXISTS "${cjson_source}")
    set(CJSON_FOUND TRUE)
    set(CJSON_INCLUDE_DIRS "${cjson_path}")
    set(CJSON_LIBRARIES "${cjson_source}")
    set(CJSON_IS_THIRD_PARTY TRUE)
    message(STATUS "Using util/ cJSON: ${cjson_path}")
endif()

# 2. 备选：系统 cJSON 库
if(NOT CJSON_FOUND)
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
            /usr/lib/x86_64-linux-gnu
            /usr/lib/aarch64-linux-gnu
    )

    if(CJSON_INCLUDE_DIR AND CJSON_LIBRARY)
        set(CJSON_FOUND TRUE)
        set(CJSON_INCLUDE_DIRS "${CJSON_INCLUDE_DIR}")
        set(CJSON_LIBRARIES "${CJSON_LIBRARY}")
        set(CJSON_IS_THIRD_PARTY FALSE)
        message(STATUS "Found system cJSON: ${CJSON_LIBRARY}")
    endif()
endif()

if(NOT CJSON_FOUND)
    message(FATAL_ERROR "cJSON not found! Please install:"
        "\n  sudo apt-get install libcjson-dev"
        "\nOr download cJSON source to: util/cJSON/")
endif()

# ============================================================================
# 包含头文件目录
# ============================================================================
include_directories(
    ${PROJECT_ROOT}/include
    ${CJSON_INCLUDE_DIRS}
)

# ============================================================================
# 源文件定义
# ============================================================================
set(KADD_SOURCES
    ${PROJECT_ROOT}/src/kadd_dfa_engine.c
    ${PROJECT_ROOT}/src/kadd_feature_engine.c
    ${PROJECT_ROOT}/src/kadd_config_loader.c
)

# 如果使用第三方cJSON，将其作为源文件添加
if(CJSON_IS_THIRD_PARTY)
    list(APPEND KADD_SOURCES ${cjson_source})
endif()

set(TOOL_SOURCES
    csv_to_vector_tool.c
    ${KADD_SOURCES}
)

# ============================================================================
# 可执行文件
# ============================================================================
add_executable(csv_to_vector_tool ${TOOL_SOURCES})

# 如果使用系统cJSON，需要链接库
if(NOT CJSON_IS_THIRD_PARTY)
    target_link_libraries(csv_to_vector_tool ${CJSON_LIBRARIES})
endif()

# ============================================================================
# 安装规则
# ============================================================================
# 安装可执行文件到当前目录
install(TARGETS csv_to_vector_tool
    RUNTIME DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/bin
)

# ============================================================================
# 开发便利设置
# ============================================================================

# 设置输出目录到 build/bin/
set_target_properties(csv_to_vector_tool PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# ============================================================================
# 配置信息输出
# ============================================================================
message(STATUS "========== CSV to Vector Tool Configuration ==========")
message(STATUS "  C Standard: ${CMAKE_C_STANDARD}")
message(STATUS "  Compiler: ${CMAKE_C_COMPILER_ID}")
message(STATUS "  Project Root: ${PROJECT_ROOT}")
message(STATUS "  Build Output: ${CMAKE_BINARY_DIR}/csv_to_vector_tool")
message(STATUS "  Training Data: ${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "  cJSON: ${CJSON_FOUND}")
if(CJSON_IS_THIRD_PARTY)
    message(STATUS "    Using: util/ source")
else()
    message(STATUS "    Library: ${CJSON_LIBRARIES}")
endif()
message(STATUS "")
message(STATUS "运行方式:")
message(STATUS "  在 build 目录下运行:")
message(STATUS "    ./csv_to_vector_tool")
message(STATUS "")
message(STATUS "  默认路径:")
message(STATUS "    输入: ../total_training_dataset_v4_pinnacle.csv")
message(STATUS "    输出: ../training_vectors.csv")
message(STATUS "    配置: ../../../../config.json")
message(STATUS "========================================================")
