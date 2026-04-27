# ==================== src 目录 CMakeLists.txt ====================

# 从父 CMakeLists.txt 传递的变量
# ONNXRUNTIME_INCLUDE_DIRS, ONNXRUNTIME_LIBRARIES
# CJSON_FOUND, CJSON_INCLUDE_DIRS, CJSON_LIBRARIES, CJSON_IS_THIRD_PARTY

# DFA 引擎源文件
set(DFA_ENGINE_SOURCES
    kadd_dfa_engine.c
)

# DFA 引擎静态库
add_library(kadd_dfa_engine STATIC ${DFA_ENGINE_SOURCES})

# DFA 引擎头文件
target_include_directories(kadd_dfa_engine PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# DFA 引擎动态库（使用不同名称避免冲突）
add_library(kadd_dfa_engine_dll SHARED ${DFA_ENGINE_SOURCES})

set_target_properties(kadd_dfa_engine_dll PROPERTIES
    OUTPUT_NAME "kadd_dfa_engine"
    VERSION 1.0.0
    SOVERSION 1
)

target_include_directories(kadd_dfa_engine_dll PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# DFA 引擎 Pro 版本源文件
if(CJSON_FOUND)
    set(DFA_ENGINE_PRO_SOURCES
        kadd_dfa_engine_pro.c
    )

    # DFA 引擎 Pro 版本静态库
    add_library(kadd_dfa_engine_pro STATIC ${DFA_ENGINE_PRO_SOURCES})

    # DFA 引擎 Pro 版本动态库
    add_library(kadd_dfa_engine_pro_dll SHARED ${DFA_ENGINE_PRO_SOURCES})

    set_target_properties(kadd_dfa_engine_pro_dll PROPERTIES
        OUTPUT_NAME "kadd_dfa_engine_pro"
        VERSION 1.0.0
        SOVERSION 1
    )

    target_include_directories(kadd_dfa_engine_pro PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CJSON_INCLUDE_DIRS}
    )

    target_include_directories(kadd_dfa_engine_pro_dll PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CJSON_INCLUDE_DIRS}
    )

    # 链接 cJSON
    if(CJSON_IS_THIRD_PARTY)
        target_sources(kadd_dfa_engine_pro_dll PRIVATE ${CJSON_LIBRARIES})
    else()
        target_link_libraries(kadd_dfa_engine_pro_dll PUBLIC ${CJSON_LIBRARIES})
        target_link_libraries(kadd_dfa_engine_pro PUBLIC ${CJSON_LIBRARIES})
    endif()
endif()

# 特征引擎源文件
set(FEATURE_ENGINE_SOURCES
    kadd_feature_engine.c
)

# 特征引擎静态库
add_library(kadd_feature_engine STATIC ${FEATURE_ENGINE_SOURCES})

# 特征引擎动态库
add_library(kadd_feature_engine_dll SHARED ${FEATURE_ENGINE_SOURCES})

set_target_properties(kadd_feature_engine_dll PROPERTIES
    OUTPUT_NAME "kadd_feature_engine"
    VERSION 1.0.0
    SOVERSION 1
)

target_include_directories(kadd_feature_engine PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_include_directories(kadd_feature_engine_dll PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# ML 引擎源文件
set(ML_ENGINE_SOURCES
    kadd_ml_engine.c
)

# ML 引擎静态库
add_library(kadd_ml_engine STATIC ${ML_ENGINE_SOURCES})

# ML 引擎动态库
add_library(kadd_ml_engine_dll SHARED ${ML_ENGINE_SOURCES})

set_target_properties(kadd_ml_engine_dll PROPERTIES
    OUTPUT_NAME "kadd_ml_engine"
    VERSION 1.0.0
    SOVERSION 1
)

target_include_directories(kadd_ml_engine PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${ONNXRUNTIME_INCLUDE_DIRS}
)

target_include_directories(kadd_ml_engine_dll PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${ONNXRUNTIME_INCLUDE_DIRS}
)

# 链接 ONNX Runtime
target_link_libraries(kadd_ml_engine PUBLIC ${ONNXRUNTIME_LIBRARIES})
target_link_libraries(kadd_ml_engine_dll PUBLIC ${ONNXRUNTIME_LIBRARIES})

# SQL 注入检测引擎源文件
if(CJSON_FOUND)
    set(SQLI_ENGINE_SOURCES
        kadd_sqli_engine.c
    )

    # SQL 注入检测引擎静态库
    add_library(kadd_sqli_engine STATIC ${SQLI_ENGINE_SOURCES})

    # SQL 注入检测引擎动态库
    add_library(kadd_sqli_engine_dll SHARED ${SQLI_ENGINE_SOURCES})

    set_target_properties(kadd_sqli_engine_dll PROPERTIES
        OUTPUT_NAME "kadd_sqli_engine"
        VERSION 1.0.0
        SOVERSION 1
    )

    target_include_directories(kadd_sqli_engine PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CJSON_INCLUDE_DIRS}
    )

    target_link_libraries(kadd_sqli_engine PUBLIC
        kadd_dfa_engine
        kadd_feature_engine
        kadd_ml_engine
    )

    # 链接 cJSON
    if(CJSON_IS_THIRD_PARTY)
        target_link_libraries(kadd_sqli_engine PUBLIC)
    else()
        target_link_libraries(kadd_sqli_engine PUBLIC ${CJSON_LIBRARIES})
    endif()

    target_include_directories(kadd_sqli_engine_dll PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CJSON_INCLUDE_DIRS}
    )

    target_link_libraries(kadd_sqli_engine_dll PUBLIC
        kadd_dfa_engine
        kadd_feature_engine
        kadd_ml_engine
    )

    # 链接 cJSON
    if(CJSON_IS_THIRD_PARTY)
        target_sources(kadd_sqli_engine_dll PRIVATE ${CJSON_LIBRARIES})
    else()
        target_link_libraries(kadd_sqli_engine_dll PUBLIC ${CJSON_LIBRARIES})
    endif()
endif()

# 保留原有的 kadd_other_engines 静态库用于测试
set(OTHER_ENGINE_SOURCES
    kadd_feature_engine.c
    kadd_ml_engine.c
)

if(CJSON_FOUND)
    list(APPEND OTHER_ENGINE_SOURCES kadd_sqli_engine.c)
endif()

add_library(kadd_other_engines STATIC ${OTHER_ENGINE_SOURCES})

target_include_directories(kadd_other_engines PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${ONNXRUNTIME_INCLUDE_DIRS}
)

if(CJSON_FOUND)
    target_include_directories(kadd_other_engines PUBLIC
        ${CJSON_INCLUDE_DIRS}
    )
endif()

target_link_libraries(kadd_other_engines PUBLIC ${ONNXRUNTIME_LIBRARIES})

if(CJSON_FOUND AND NOT CJSON_IS_THIRD_PARTY)
    target_link_libraries(kadd_other_engines PUBLIC ${CJSON_LIBRARIES})
endif()
