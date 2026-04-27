# Demo 程序 CMakeLists.txt

# 获取工具函数源文件
set(TOOLS_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/tools.c)

# ========== kadd_feature_demo ==========
add_executable(kadd_feature_demo kadd_feature_demo.c ${TOOLS_SRCS})

target_include_directories(kadd_feature_demo PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(WIN32)
    # Windows: 链接 DLL
    target_link_libraries(kadd_feature_demo PRIVATE
        ${CMAKE_BINARY_DIR}/src/libkadd_feature_engine.dll
    )
    # 将 DLL 复制到输出目录
    add_custom_command(TARGET kadd_feature_demo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/src/libkadd_feature_engine.dll
        $<TARGET_FILE_DIR:kadd_feature_demo>
        COMMENT "Copying libkadd_feature_engine.dll to output directory"
    )
elseif(APPLE)
    target_link_libraries(kadd_feature_demo PRIVATE
        ${CMAKE_BINARY_DIR}/src/libkadd_feature_engine.dylib
    )
else()
    target_link_libraries(kadd_feature_demo PRIVATE
        ${CMAKE_BINARY_DIR}/src/libkadd_feature_engine.so
    )
endif()

# ========== kadd_ml_demo ==========
add_executable(kadd_ml_demo kadd_ml_demo.c ${TOOLS_SRCS})

target_include_directories(kadd_ml_demo PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 链接 ONNX Runtime
target_include_directories(kadd_ml_demo PRIVATE ${ONNXRUNTIME_INCLUDE_DIRS})
target_link_libraries(kadd_ml_demo PRIVATE ${ONNXRUNTIME_LIBRARIES})

if(WIN32)
    # Windows: 链接 DLL
    target_link_libraries(kadd_ml_demo PRIVATE
        ${CMAKE_BINARY_DIR}/src/libkadd_ml_engine.dll
    )
    # 将 DLL 复制到输出目录
    add_custom_command(TARGET kadd_ml_demo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/src/libkadd_ml_engine.dll
        $<TARGET_FILE_DIR:kadd_ml_demo>
        COMMENT "Copying libkadd_ml_engine.dll to output directory"
    )
    # 复制 ONNX Runtime DLL
    if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/lib/onnxruntime.dll")
        add_custom_command(TARGET kadd_ml_demo POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/third_party/lib/onnxruntime.dll
            $<TARGET_FILE_DIR:kadd_ml_demo>
            COMMENT "Copying onnxruntime.dll to output directory"
        )
    endif()
elseif(APPLE)
    target_link_libraries(kadd_ml_demo PRIVATE
        ${CMAKE_BINARY_DIR}/src/libkadd_ml_engine.dylib
    )
    # 设置 rpath 以便找到 dylib
    set_target_properties(kadd_ml_demo PROPERTIES
        INSTALL_RPATH "${CMAKE_BINARY_DIR}/src"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
 else()
    target_link_libraries(kadd_ml_demo PRIVATE
        ${CMAKE_BINARY_DIR}/src/libkadd_ml_engine.so
    )
    # 复制 ONNX Runtime .so 文件
    if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/lib/libonnxruntime.so")
        add_custom_command(TARGET kadd_ml_demo POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/third_party/lib/libonnxruntime.so
            $<TARGET_FILE_DIR:kadd_ml_demo>
            COMMENT "Copying libonnxruntime.so to output directory"
        )
    endif()
    # 设置 rpath 以便找到 so
    # 使用 $ORIGIN 让程序在当前目录查找库文件
    set_target_properties(kadd_ml_demo PROPERTIES
        INSTALL_RPATH "$ORIGIN;$ORIGIN/../src:${CMAKE_BINARY_DIR}/src"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# ========== kadd_ml_demo_debug (调试版本) ==========
add_executable(kadd_ml_demo_debug kadd_ml_demo_debug.c ${TOOLS_SRCS})

target_include_directories(kadd_ml_demo_debug PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 链接 ONNX Runtime
target_include_directories(kadd_ml_demo_debug PRIVATE ${ONNXRUNTIME_INCLUDE_DIRS})
target_link_libraries(kadd_ml_demo_debug PRIVATE ${ONNXRUNTIME_LIBRARIES})

if(WIN32)
    target_link_libraries(kadd_ml_demo_debug PRIVATE
        ${CMAKE_BINARY_DIR}/src/libkadd_ml_engine.dll
    )
elseif(APPLE)
    target_link_libraries(kadd_ml_demo_debug PRIVATE
        ${CMAKE_BINARY_DIR}/src/libkadd_ml_engine.dylib
    )
else()
    target_link_libraries(kadd_ml_demo_debug PRIVATE
        ${CMAKE_BINARY_DIR}/src/libkadd_ml_engine.so
    )
    # 复制 ONNX Runtime .so 文件
    if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/lib/libonnxruntime.so")
        add_custom_command(TARGET kadd_ml_demo_debug POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/third_party/lib/libonnxruntime.so
            $<TARGET_FILE_DIR:kadd_ml_demo_debug>
            COMMENT "Copying libonnxruntime.so to output directory"
        )
    endif()
    # 设置 rpath
    set_target_properties(kadd_ml_demo_debug PROPERTIES
        INSTALL_RPATH "$ORIGIN;$ORIGIN/../src:${CMAKE_BINARY_DIR}/src"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()
