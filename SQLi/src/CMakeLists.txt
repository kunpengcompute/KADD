# ==================== src 目录 CMakeLists.txt ====================

# 从父 CMakeLists.txt 传递的变量
# ONNXRUNTIME_INCLUDE_DIRS, ONNXRUNTIME_LIBRARIES
# CJSON_FOUND, CJSON_INCLUDE_DIRS, CJSON_LIBRARIES, CJSON_IS_THIRD_PARTY

# ============================================================================
# 公共函数：为公开 .so 添加 linker version script（隐藏内部符号）
# ============================================================================
function(kadd_apply_version_script NAME EXPORT_PREFIXES)
    get_target_property(_type ${NAME} TYPE)
    if(NOT _type STREQUAL "SHARED_LIBRARY")
        return()
    endif()

    set(_map "${CMAKE_CURRENT_BINARY_DIR}/${NAME}.map")
    set(_content "KADD_1.0 {\n    global:\n")
    foreach(_pfx IN LISTS EXPORT_PREFIXES)
        set(_content "${_content}        ${_pfx}*;\n")
    endforeach()
    set(_content "${_content}    local: *;\n};\n")
    file(WRITE "${_map}" "${_content}")

    set_target_properties(${NAME} PROPERTIES
        LINK_DEPENDS "${_map}"
        LINK_FLAGS "-Wl,--version-script=${_map}"
    )
endfunction()

# ============================================================================
# 通用函数：创建库
# ============================================================================
# FORCE_STATIC: 内部实现库 → 静态库，代码直接编入上层 .so，不产生独立 .so
#               外部消费者无法访问其符号（物理不存在）
function(add_kadd_library NAME)
    set(options NEED_CJSON NEED_ONNX FORCE_STATIC)
    set(one_value_args VERSION)
    set(multi_value_args SOURCES LINK_LIBRARIES)
    cmake_parse_arguments(ARG "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    # 库类型：FORCE_STATIC 强制静态；否则根据 BUILD_SHARED_LIBS
    if(ARG_FORCE_STATIC)
        set(LIB_TYPE STATIC)
    elseif(BUILD_SHARED_LIBS)
        set(LIB_TYPE SHARED)
    else()
        set(LIB_TYPE STATIC)
    endif()

    add_library(${NAME} ${LIB_TYPE} ${ARG_SOURCES})

    # 编译选项（静态库也需要 -fPIC，因为会被链接进共享库）
    target_compile_options(${NAME} PRIVATE
        -fPIC                    # 位置无关代码（PIC，静态库被链入 .so 时必须）
        -fstack-protector-strong # 栈保护
        -Wformat -Wformat-security # 格式化字符串检查
        -ftrapv                  # 整数溢出检查
    )

    if(LIB_TYPE STREQUAL "SHARED")
        set_target_properties(${NAME} PROPERTIES
            VERSION "${ARG_VERSION}"
            SOVERSION 1
        )
    endif()

    # ── include 路径 ──
    # PUBLIC: 对外公开的头文件
    target_include_directories(${NAME} PUBLIC ${CMAKE_SOURCE_DIR}/include)
    # PRIVATE: 内部实现头文件（编译可见，不 install、不传播）
    target_include_directories(${NAME} PRIVATE ${CMAKE_SOURCE_DIR}/include/internal)

    # cJSON 依赖
    if(ARG_NEED_CJSON AND CJSON_FOUND)
        target_include_directories(${NAME} PUBLIC ${CJSON_INCLUDE_DIRS})
        if(CJSON_IS_THIRD_PARTY)
            target_sources(${NAME} PRIVATE ${CJSON_LIBRARIES})
        else()
            target_link_libraries(${NAME} PUBLIC ${CJSON_LIBRARIES})
        endif()
    endif()

    # ONNX Runtime 依赖
    if(ARG_NEED_ONNX)
        target_include_directories(${NAME} PUBLIC ${ONNXRUNTIME_INCLUDE_DIRS})
        target_link_libraries(${NAME} PUBLIC ${ONNXRUNTIME_LIBRARIES})
    endif()

    # 额外链接库
    if(ARG_LINK_LIBRARIES)
        target_link_libraries(${NAME} PUBLIC ${ARG_LINK_LIBRARIES})
    endif()
endfunction()

# ============================================================================
# 基础引擎库
#   DFA/特征引擎：公开 API → 动态库，仅导出 kadd_dfa_*/kadd_seq_*
#   CNN/RF 引擎：内部实现 FORCE_STATIC → 折叠进上层 .so，不产生独立 .so
# ============================================================================

# DFA 引擎（对外公开 API → 动态库，仅导出 kadd_dfa_*）
add_kadd_library(kadd_dfa_engine
    VERSION 1.0.0
    SOURCES kadd_dfa_engine.c
)
kadd_apply_version_script(kadd_dfa_engine "kadd_dfa_")

# 特征引擎（对外公开 API → 动态库，仅导出 kadd_seq_*）
add_kadd_library(kadd_feature_engine
    VERSION 1.0.0
    SOURCES kadd_feature_engine.c
)
kadd_apply_version_script(kadd_feature_engine "kadd_seq_")

# 便携 TextCNN 推理引擎（SVE 通道分块 + 查表融合）
add_kadd_library(kadd_cnn_engine FORCE_STATIC
    VERSION 1.0.0
    SOURCES kadd_cnn_engine.c
)

# 便携 sklearn RandomForest 推理引擎（加载 .krf）
add_kadd_library(kadd_rf_engine FORCE_STATIC
    VERSION 1.0.0
    SOURCES kadd_rf_engine.c
)

# ============================================================================
# 高级引擎库（需要 cJSON）
# ============================================================================

if(CJSON_FOUND)
    # 配置加载器（对外公开 API → 动态库，仅导出 kadd_config_*）
    add_kadd_library(kadd_config_loader
        VERSION 1.0.0
        SOURCES kadd_config_loader.c
        NEED_CJSON
    )
    kadd_apply_version_script(kadd_config_loader "kadd_config_")

    # 统一推理引擎（内部）
    add_kadd_library(kadd_inference_engine FORCE_STATIC
        VERSION 1.0.0
        SOURCES kadd_inference_engine.c
        NEED_CJSON
        NEED_ONNX
        LINK_LIBRARIES kadd_config_loader
    )

    # ── 对外公开的引擎库（SHARED + version script）──

    # ML 引擎：静态链接内部实现，仅导出 kadd_model_* 符号
    add_kadd_library(kadd_ml_engine
        VERSION 1.0.0
        SOURCES kadd_ml_engine.c
        LINK_LIBRARIES kadd_rf_engine kadd_inference_engine
    )
    kadd_apply_version_script(kadd_ml_engine "kadd_model_")

    # SQL 注入检测引擎：仅导出 kadd_sqli_* 符号
    add_kadd_library(kadd_sqli_engine
        VERSION 1.0.0
        SOURCES kadd_sqli_engine.c
        NEED_CJSON
        LINK_LIBRARIES kadd_dfa_engine kadd_feature_engine kadd_ml_engine kadd_dl_engine
    )
    kadd_apply_version_script(kadd_sqli_engine "kadd_sqli_")

    # DL/CNN 引擎：仅导出 kadd_dl_* 符号
    add_kadd_library(kadd_dl_engine
        VERSION 1.0.0
        SOURCES kadd_dl_engine.c
        LINK_LIBRARIES kadd_cnn_engine kadd_inference_engine
    )
    kadd_apply_version_script(kadd_dl_engine "kadd_dl_")
endif()
