# ==================== src 目录 CMakeLists.txt ====================

# 从父 CMakeLists.txt 传递的变量
# ONNXRUNTIME_INCLUDE_DIRS, ONNXRUNTIME_LIBRARIES
# CJSON_FOUND, CJSON_INCLUDE_DIRS, CJSON_LIBRARIES, CJSON_IS_THIRD_PARTY

# ============================================================================
# 通用函数：创建动态库
# ============================================================================
function(add_kadd_library NAME)
    set(options NEED_CJSON NEED_ONNX)
    set(one_value_args VERSION)
    set(multi_value_args SOURCES LINK_LIBRARIES)
    cmake_parse_arguments(ARG "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    # 动态库
    add_library(${NAME} SHARED ${ARG_SOURCES})

    # 显式应用安全编译选项（确保 SP 栈保护和 Ftrapv 整数溢出检查生效）
    target_compile_options(${NAME} PRIVATE
        -fPIC                    # 位置无关代码（PIC）
        -fstack-protector-strong # 栈保护（Stack Protection）
        -Wformat -Wformat-security # 格式化字符串检查（Format String）
        -ftrapv                  # 整数溢出检查
    )
    # 链接选项继承自 CMAKE_SHARED_LINKER_FLAGS: -Wl,-z,relro,-z,now

    set_target_properties(${NAME} PROPERTIES
        VERSION "${ARG_VERSION}"
        SOVERSION 1
    )

    target_include_directories(${NAME} PUBLIC ${CMAKE_SOURCE_DIR}/include)

    # cJSON 依赖处理
    if(ARG_NEED_CJSON AND CJSON_FOUND)
        target_include_directories(${NAME} PUBLIC ${CJSON_INCLUDE_DIRS})
        if(CJSON_IS_THIRD_PARTY)
            target_sources(${NAME} PRIVATE ${CJSON_LIBRARIES})
        else()
            target_link_libraries(${NAME} PUBLIC ${CJSON_LIBRARIES})
        endif()
    endif()

    # ONNX Runtime 依赖处理
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
# ============================================================================

# DFA 引擎
add_kadd_library(kadd_dfa_engine
    VERSION 1.0.0
    SOURCES kadd_dfa_engine.c
)

# 特征引擎
add_kadd_library(kadd_feature_engine
    VERSION 1.0.0
    SOURCES kadd_feature_engine.c
)

# ML 引擎
add_kadd_library(kadd_ml_engine
    VERSION 1.0.0
    SOURCES kadd_ml_engine.c
    NEED_ONNX
)

# ============================================================================
# 高级引擎库 (需要 cJSON)
# ============================================================================

if(CJSON_FOUND)
    # DFA Pro 引擎（暂时注释掉，源文件不存在）
    # add_kadd_library(kadd_dfa_engine_pro
    #     VERSION 1.0.0
    #     SOURCES kadd_dfa_engine_pro.c
    #     NEED_CJSON
    # )

    # SQL 注入检测引擎
    add_kadd_library(kadd_sqli_engine
        VERSION 1.0.0
        SOURCES kadd_sqli_engine.c
        NEED_CJSON
        LINK_LIBRARIES kadd_dfa_engine kadd_feature_engine kadd_ml_engine
    )

    # 配置加载器
    add_kadd_library(kadd_config_loader
        VERSION 1.0.0
        SOURCES kadd_config_loader.c
        NEED_CJSON
    )
endif()
