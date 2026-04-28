# Demo 程序 CMakeLists.txt (ARM Linux)

# 获取工具函数源文件
set(TOOLS_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/tools.c)

# ========== kadd_feature_demo ==========
add_executable(kadd_feature_demo kadd_feature_demo.c ${TOOLS_SRCS})

target_include_directories(kadd_feature_demo PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(kadd_feature_demo PRIVATE kadd_feature_engine)

# ========== kadd_ml_demo ==========
add_executable(kadd_ml_demo kadd_ml_demo.c ${TOOLS_SRCS})

target_include_directories(kadd_ml_demo PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ONNXRUNTIME_INCLUDE_DIRS}
)

target_link_libraries(kadd_ml_demo PRIVATE
    kadd_ml_engine
)

# 设置 RPATH 以便在当前目录和 src 目录查找库
set_target_properties(kadd_ml_demo PROPERTIES
    INSTALL_RPATH "$ORIGIN;$ORIGIN/../src:${CMAKE_BINARY_DIR}/src"
    BUILD_WITH_INSTALL_RPATH TRUE
)
