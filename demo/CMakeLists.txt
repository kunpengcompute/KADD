# Demo 程序 CMakeLists.txt

# 工具函数源文件
set(TOOLS_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/tools.c)

# ============================================================================
# 通用 Demo 函数
# ============================================================================
function(add_kadd_demo DEMO_NAME SOURCE_FILE NEEDS_ML NEEDS_CJSON)
    add_executable(${DEMO_NAME} ${SOURCE_FILE} ${TOOLS_SRCS})

    target_include_directories(${DEMO_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # ONNX Runtime（ML demo 和 SQLi demo 需要）
    if(${NEEDS_ML})
        target_include_directories(${DEMO_NAME} PRIVATE ${ONNXRUNTIME_INCLUDE_DIRS})
        target_link_libraries(${DEMO_NAME} PRIVATE ${ONNXRUNTIME_LIBRARIES})
    endif()

    # cJSON（DFA demo 和 SQLi demo 需要）
    if(${NEEDS_CJSON} AND CJSON_FOUND)
        if(CJSON_IS_THIRD_PARTY)
            target_sources(${DEMO_NAME} PRIVATE ${CJSON_LIBRARIES})
        else()
            target_link_libraries(${DEMO_NAME} PRIVATE ${CJSON_LIBRARIES})
        endif()
        target_include_directories(${DEMO_NAME} PRIVATE ${CJSON_INCLUDE_DIRS})
    endif()

    # 链接引擎库
    if(${DEMO_NAME} STREQUAL "kadd_dfa_demo")
        target_link_libraries(${DEMO_NAME} PRIVATE kadd_dfa_engine)
    elseif(${DEMO_NAME} STREQUAL "kadd_feature_demo")
        target_link_libraries(${DEMO_NAME} PRIVATE kadd_feature_engine)
    elseif(${DEMO_NAME} STREQUAL "kadd_ml_demo")
        target_link_libraries(${DEMO_NAME} PRIVATE kadd_ml_engine)
    elseif(${DEMO_NAME} STREQUAL "kadd_sqli_demo")
        target_link_libraries(${DEMO_NAME} PRIVATE kadd_sqli_engine)
    endif()

    # C++ 标准库（ML 和 SQLi demo 需要）
    if(${NEEDS_ML})
        target_link_libraries(${DEMO_NAME} PRIVATE stdc++)
    endif()
endfunction()

# ============================================================================
# Demo 程序
# ============================================================================

# DFA 引擎演示
add_kadd_demo(kadd_dfa_demo kadd_dfa_demo.c FALSE TRUE FALSE)

# 特征引擎演示
add_kadd_demo(kadd_feature_demo kadd_feature_demo.c FALSE FALSE FALSE)

# ML 引擎演示
add_kadd_demo(kadd_ml_demo kadd_ml_demo.c TRUE FALSE FALSE)

# SQL 注入检测演示
add_kadd_demo(kadd_sqli_demo kadd_sqli_demo.c TRUE TRUE FALSE)

# libinjection 对比演示
set(LIBINJECTION_SRC_DIR ${CMAKE_SOURCE_DIR}/third_party/libinjection-main/src)

# 为需要 libinjection 的 demo 添加源文件和头文件路径
if(EXISTS "${LIBINJECTION_SRC_DIR}/libinjection_sqli.c")
    set(LIBINJECTION_FOUND TRUE)
    set(LIBINJECTION_LIB ${LIBINJECTION_SRC_DIR}/libinjection_sqli.c)
    set(LIBINJECTION_INCLUDE_DIR ${LIBINJECTION_SRC_DIR})
    message(STATUS "  libinjection: enabled (for comparison in dfa_demo and sqli_demo)")
else()
    set(LIBINJECTION_FOUND FALSE)
    set(LIBINJECTION_LIB "")
    set(LIBINJECTION_INCLUDE_DIR "")
    message(STATUS "  libinjection: disabled (source not found)")
endif()

# 为 dfa_demo 添加 libinjection 支持
if(LIBINJECTION_FOUND)
    target_sources(kadd_dfa_demo PRIVATE ${LIBINJECTION_LIB})
    target_include_directories(kadd_dfa_demo PRIVATE ${LIBINJECTION_INCLUDE_DIR})
endif()

# 为 sqli_demo 添加 libinjection 支持
if(LIBINJECTION_FOUND)
    target_sources(kadd_sqli_demo PRIVATE ${LIBINJECTION_LIB})
    target_include_directories(kadd_sqli_demo PRIVATE ${LIBINJECTION_INCLUDE_DIR})
endif()

# 独立的 libinjection demo (可选)
set(LIBINJECTION_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/kadd_libinjection_demo.c
    ${TOOLS_SRCS}
    ${LIBINJECTION_LIB}
)
if(LIBINJECTION_FOUND)
    add_executable(kadd_libinjection_demo ${LIBINJECTION_SOURCES})
    target_include_directories(kadd_libinjection_demo PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${LIBINJECTION_INCLUDE_DIR}
    )
    add_dependencies(kadd_libinjection_demo copy_demo_resources)
endif()

# ============================================================================
# 资源文件复制
# ============================================================================
add_custom_target(copy_demo_resources ALL
    COMMAND ${CMAKE_COMMAND} -E echo "Copying demo resources..."
)

# 复制 config.json
add_custom_command(TARGET copy_demo_resources POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_SOURCE_DIR}/config.json
        ${CMAKE_BINARY_DIR}/demo/config.json
    COMMENT "Copying config.json for demos"
)

# 复制 assets 目录
add_custom_command(TARGET copy_demo_resources POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        ${CMAKE_BINARY_DIR}/demo/assets
    COMMENT "Copying assets directory for demos"
)

# 添加依赖关系
add_dependencies(kadd_dfa_demo copy_demo_resources)
add_dependencies(kadd_feature_demo copy_demo_resources)
add_dependencies(kadd_ml_demo copy_demo_resources)
add_dependencies(kadd_sqli_demo copy_demo_resources)

message(STATUS "Demos configured: kadd_dfa_demo, kadd_feature_demo, kadd_ml_demo, kadd_sqli_demo")

# 设置 RPATH 以便在当前目录和 src 目录查找库（仅对需要动态库的程序）
if(UNIX AND NOT APPLE)
    set_target_properties(kadd_ml_demo PROPERTIES
        INSTALL_RPATH "$ORIGIN:$ORIGIN/../src:${CMAKE_BINARY_DIR}/src"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
    set_target_properties(kadd_sqli_demo PROPERTIES
        INSTALL_RPATH "$ORIGIN:$ORIGIN/../src:${CMAKE_BINARY_DIR}/src"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()
