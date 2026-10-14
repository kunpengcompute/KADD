# ==================== include 目录 CMakeLists.txt ====================

# ── 对外公开的头文件（install 给外部消费者）──
set(KADD_PUBLIC_HEADERS
    kadd_sqli_engine.h
    kadd_ml_engine.h
    kadd_dl_engine.h
    kadd_errors.h
    kadd_dfa_engine.h
    kadd_feature_engine.h
    kadd_config_loader.h
    CACHE STRING "KADD public header files"
)

# ── 内部头文件（仅编译时使用，不安装）──
# 位于 include/internal/ 子目录，通过 CMake PRIVATE include 路径访问

# 可选：安装头文件（如果使用 make install）
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    install(FILES ${KADD_PUBLIC_HEADERS}
        DESTINATION include
        COMPONENT Development
    )
endif()
