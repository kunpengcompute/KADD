# ==================== include 目录 CMakeLists.txt ====================

# 安装所有公共头文件
set(PUBLIC_HEADERS
    kadd_feature_engine.h
    kadd_ml_engine.h
    kadd_dfa_engine.h
    kadd_sqli_engine.h
)

# 如果需要，可以添加头文件检测
foreach(header ${PUBLIC_HEADERS})
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${header}")
        message(WARNING "Header file not found: ${header}")
    endif()
endforeach()

# 安装头文件
install(FILES ${PUBLIC_HEADERS} DESTINATION include)
