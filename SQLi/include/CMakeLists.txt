# ==================== include 目录 CMakeLists.txt ====================

# 公共头文件列表
set(KADD_PUBLIC_HEADERS
    kadd_dfa_engine.h
    kadd_dfa_engine_pro.h
    kadd_feature_engine.h
    kadd_ml_engine.h
    kadd_sqli_engine.h
    CACHE STRING "KADD public header files"
)

# 可选：安装头文件（如果使用 make install）
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    install(FILES ${KADD_PUBLIC_HEADERS}
        DESTINATION include
        COMPONENT Development
    )
endif()
