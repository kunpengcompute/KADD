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
    target_link_libraries(kadd_feature_demo PRIVATE
        ${CMAKE_BINARY_DIR}/src/libkadd_feature_engine.dll
    )
    add_custom_command(TARGET kadd_feature_demo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/src/libkadd_feature_engine.dll
        $<TARGET_FILE_DIR:kadd_feature_demo>
        COMMENT "Copying libkadd_feature_engine.dll"
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

target_include_directories(kadd_ml_demo PRIVATE ${ONNXRUNTIME_INCLUDE_DIRS})
target_link_libraries(kadd_ml_demo PRIVATE ${ONNXRUNTIME_LIBRARIES})

if(WIN32)
    target_link_libraries(kadd_ml_demo PRIVATE
        ${CMAKE_BINARY_DIR}/src/libkadd_ml_engine.dll
    )
    add_custom_command(TARGET kadd_ml_demo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/src/libkadd_ml_engine.dll
        $<TARGET_FILE_DIR:kadd_ml_demo>
        COMMENT "Copying libkadd_ml_engine.dll"
    )
    if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/lib/onnxruntime.dll")
        add_custom_command(TARGET kadd_ml_demo POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/third_party/lib/onnxruntime.dll
            $<TARGET_FILE_DIR:kadd_ml_demo>
            COMMENT "Copying onnxruntime.dll"
        )
    endif()
elseif(APPLE)
    target_link_libraries(kadd_ml_demo PRIVATE
        ${CMAKE_BINARY_DIR}/src/libkadd_ml_engine.dylib
    )
    set_target_properties(kadd_ml_demo PROPERTIES
        INSTALL_RPATH "${CMAKE_BINARY_DIR}/src"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
else()
    # Linux
    target_link_libraries(kadd_ml_demo PRIVATE
        ${CMAKE_BINARY_DIR}/src/libkadd_ml_engine.so
    )
    # 复制 ONNX Runtime .so 到输出目录
    if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/lib/libonnxruntime.so")
        add_custom_command(TARGET kadd_ml_demo POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/third_party/lib/libonnxruntime.so
            $<TARGET_FILE_DIR:kadd_ml_demo>
            COMMENT "Copying libonnxruntime.so"
        )
    endif()
    # 设置 RPATH 以便在当前目录和 src 目录查找库
    set_target_properties(kadd_ml_demo PROPERTIES
        INSTALL_RPATH "$ORIGIN;$ORIGIN/../src:${CMAKE_BINARY_DIR}/src"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# ========== test_onnx_20 (ONNX Runtime 1.20+ 兼容性测试工具) ==========
add_executable(test_onnx_20 test_onnx_20.c)

target_include_directories(test_onnx_20 PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(test_onnx_20 PRIVATE ${ONNXRUNTIME_INCLUDE_DIRS})
target_link_libraries(test_onnx_20 PRIVATE ${ONNXRUNTIME_LIBRARIES})

if(WIN32)
    if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/lib/onnxruntime.dll")
        add_custom_command(TARGET test_onnx_20 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/third_party/lib/onnxruntime.dll
            $<TARGET_FILE_DIR:test_onnx_20>
            COMMENT "Copying onnxruntime.dll"
        )
    endif()
else()
    if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/lib/libonnxruntime.so")
        add_custom_command(TTARGET test_onnx_20 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/third_party/lib/libonnxruntime.so
            $<TARGET_FILE_DIR:test_onnx_20>
            COMMENT "Copying libonnxruntime.so"
        )
    endif()
    set_target_properties(test_onnx_20 PROPERTIES
        INSTALL_RPATH "$ORIGIN;$ORIGIN/../src:${CMAKE_BINARY_DIR}/src"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# ========== test_onnx_arm (ARM + ONNX Runtime 1.16 诊断工具) ==========
add_executable(test_onnx_arm test_onnx_arm.c)

target_include_directories(test_onnx_arm PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(test_onnx_arm PRIVATE ${ONNXRUNTIME_INCLUDE_DIRS})
target_link_libraries(test_onnx_arm PRIVATE ${ONNXRUNTIME_LIBRARIES})

if(WIN32)
    if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/lib/onnxruntime.dll")
        add_custom_command(TARGET test_onnx_arm POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/third_party/lib/onnxruntime.dll
            $<TARGET_FILE_DIR:test_onnx_arm>
            COMMENT "Copying onnxruntime.dll"
        )
    endif()
else()
    if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/lib/libonnxruntime.so")
        add_custom_command(TARGET test_onnx_arm POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/third_party/lib/libonnxruntime.so
            $<TARGET_FILE_DIR:test_onnx_arm>
            COMMENT "Copying libonnxruntime.so"
        )
    endif()
    set_target_properties(test_onnx_arm PROPERTIES
        INSTALL_RPATH "$ORIGIN;$ORIGIN/../src:${CMAKE_BINARY_DIR}/src"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()
