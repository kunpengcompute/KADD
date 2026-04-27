# ==================== src 目录 CMakeLists.txt ====================

# DFA 引擎源文件
set(DFA_ENGINE_SOURCES
    kadd_dfa_engine.c
)

# DFA 引擎静态库
add_library(kadd_dfa_engine STATIC ${DFA_ENGINE_SOURCES})

# DFA 引擎头文件
target_include_directories(kadd_dfa_engine PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party/include
)

# DFA 引擎 Pro 版本源文件
set(DFA_ENGINE_PRO_SOURCES
    kadd_dfa_engine_pro.c
)

# DFA 引擎 Pro 版本静态库
add_library(kadd_dfa_engine_pro STATIC ${DFA_ENGINE_PRO_SOURCES})

# DFA 引擎 Pro 版本头文件
target_include_directories(kadd_dfa_engine_pro PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party/include
)

# 其他引擎源文件
set(OTHER_ENGINE_SOURCES
    kadd_feature_engine.c
    kadd_ml_engine.c
    kadd_sqli_engine.c
)

# 其他引擎静态库
add_library(kadd_other_engines STATIC ${OTHER_ENGINE_SOURCES})

target_include_directories(kadd_other_engines PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party/include
)
