# KADD 离线依赖库引入指南

本文档介绍如何在 ARM Linux 系统和 Windows 系统上离线引入第三方 C 库依赖。

## 目录

- [概述](#概述)
- [依赖库列表](#依赖库列表)
- [方案一：在线 FetchContent（推荐）](#方案一在线-fetchcontent推荐)
- [方案二：离线本地引入](#方案二离线本地引入)
  - [Windows 系统](#windows-系统)
  - [ARM Linux 系统](#arm-linux-系统)
- [验证安装](#验证安装)
- [常见问题](#常见问题)

---

## 概述

KADD 项目依赖以下第三方 C/C++ 库：

| 库名称 | 用途 | 许可证 |
|--------|------|--------|
| libinjection | SQL 注入检测 | BSD-3-Clause |
| cJSON | JSON 解析 | MIT |
| onnxruntime | ONNX 模型推理 | MIT |

---

## 依赖库列表

### 1. libinjection

- **官网**: https://github.com/client9/libinjection
- **版本**: master (或指定 release tag)
- **用途**: SQL 注入和 XSS 攻击检测
- **组件**: 
  - `libinjection_sqli.c/h` - SQL 注入检测
  - `libinjection_xss.c/h` - XSS 检测

### 2. cJSON

- **官网**: https://github.com/DaveGamble/cJSON
- **版本**: v1.7.17 或更高
- **用途**: JSON 数据解析和生成
- **组件**: `cJSON.c/h`

### 3. onnxruntime

- **官网**: https://github.com/microsoft/onnxruntime
- **版本**: v1.17.0 或更高
- **用途**: ONNX 格式机器学习模型推理
- **组件**: 
  - `libonnxruntime.so` (Linux)
  - `onnxruntime.dll` (Windows)

---

## 方案一：在线 FetchContent（推荐）

适用于有网络访问的环境，CMake 会自动下载并配置依赖库。

### CMakeLists.txt 配置

```cmake
cmake_minimum_required(VERSION 3.15)
project(KADD VERSION 1.0.0 LANGUAGES C CXX)

include(FetchContent)

# ========== libinjection ==========
FetchContent_Declare(
    libinjection
    GIT_REPOSITORY https://github.com/client9/libinjection.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
set(LIBINJECTION_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(LIBINJECTION_BUILD_STATIC ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(libinjection)

# ========== cJSON ==========
FetchContent_Declare(
    cjson
    GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
    GIT_TAG        v1.7.17
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(cjson)

# ========== onnxruntime (预编译二进制) ==========
# Windows
if(WIN32)
    # 手动下载预编译包或使用 vcpkg
    find_package(onnxruntime REQUIRED)
    # 或使用本地路径
    # set(ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/third_party/onnxruntime")
    # include_directories(${ONNXRUNTIME_ROOT}/include)
    # link_directories(${ONNXRUNTIME_ROOT}/lib)
endif()

# Linux
if(UNIX)
    # ARM Linux 需要预编译或交叉编译
    find_package(onnxruntime REQUIRED)
endif()

# 链接依赖
target_link_libraries(your_target PRIVATE
    libinjection_static
    cjson
    onnxruntime
)
```

---

## 方案二：离线本地引入

适用于无网络访问或需要离线部署的环境。

### Windows 系统

#### 目录结构

```
KADD/
├── third_party/
│   ├── include/
│   │   ├── libinjection/
│   │   │   ├── libinjection.h
│   │   │   ├── libinjection_sqli.h
│   │   │   └── libinjection_xss.h
│   │   ├── cJSON.h
│   │   └── onnxruntime/
│   │       └── onnxruntime_cxx_api.h
│   └── lib/
│       ├── libinjection_static.lib
│       ├── cJSON.lib
│       └── onnxruntime.lib
└── src/
```

#### 获取离线包

##### 1. libinjection

```bash
# 克隆仓库
git clone https://github.com/client9/libinjection.git third_party/libinjection

# 或下载发布版本
# https://github.com/client9/libinjection/releases
```

编译静态库：

```bash
cd third_party/libinjection/src
# 使用 MinGW 或 MSVC 编译
gcc -c libinjection_sqli.c -o libinjection_sqli.o
ar rcs libinjection_static.a libinjection_sqli.o
```

##### 2. cJSON

```bash
# 克隆仓库
git clone https://github.com/DaveGamble/cJSON.git third_party/cjson

# 编译
cd third_party/cjson
gcc -c cJSON.c -o cJSON.o
ar rcs libcJSON.a cJSON.o
```

##### 3. onnxruntime

下载预编译包：

- 访问 https://github.com/microsoft/onnxruntime/releases
- 下载 `onnxruntime-win-x64-<version>.zip`
- 解压到 `third_party/onnxruntime/`

#### CMakeLists.txt 配置（Windows 离线）

```cmake
cmake_minimum_required(VERSION 3.15)
project(KADD VERSION 1.0.0 LANGUAGES C CXX)

# ========== 设置第三方库路径 ==========
set(THIRD_PARTY_ROOT "${CMAKE_SOURCE_DIR}/third_party")
set(THIRD_PARTY_INCLUDE "${THIRD_PARTY_ROOT}/include")
set(THIRD_PARTY_LIB "${THIRD_PARTY_ROOT}/lib")

# ========== libinjection ==========
set(LIBINJECTION_ROOT "${THIRD_PARTY_ROOT}/libinjection")
include_directories(${LIBINJECTION_ROOT}/src)

add_library(libinjection_static STATIC
    ${LIBINJECTION_ROOT}/src/libinjection_sqli.c
    ${LIBINJECTION_ROOT}/src/libinjection_xss.c
    ${LIBINJECTION_ROOT}/src/libinjection_html5.c
)

# ========== cJSON ==========
set(CJSON_ROOT "${THIRD_PARTY_ROOT}/cjson")
include_directories(${CJSON_ROOT})

add_library(cjson STATIC
    ${CJSON_ROOT}/cJSON.c
)

# ========== onnxruntime ==========
set(ONNXRUNTIME_ROOT "${THIRD_PARTY_ROOT}/onnxruntime")
include_directories(${ONNXRUNTIME_ROOT}/include)
link_directories(${ONNXRUNTIME_ROOT}/lib)

# ========== 主目标 ==========
add_executable(kadd_app src/main.c)

target_link_libraries(kadd_app PRIVATE
    libinjection_static
    cjson
    onnxruntime
)

# Windows 特定设置
if(WIN32)
    # 复制 DLL 到输出目录
    add_custom_command(TARGET kadd_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ONNXRUNTIME_ROOT}/bin/onnxruntime.dll"
        $<TARGET_FILE_DIR:kadd_app>
    )
endif()
```

---

### ARM Linux 系统

#### 目录结构

```
KADD/
├── third_party/
│   ├── include/
│   │   ├── libinjection/
│   │   ├── cJSON.h
│   │   └── onnxruntime/
│   └── lib/
│       ├── liblibinjection.a
│       ├── libcjson.a
│       └── libonnxruntime.so
└── src/
```

#### 获取离线包

##### 1. libinjection

```bash
# 在 ARM Linux 设备上或交叉编译环境
git clone https://github.com/client9/libinjection.git third_party/libinjection

cd third_party/libinjection
mkdir build && cd build
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DLIBINJECTION_BUILD_SHARED=OFF \
    -DLIBINJECTION_BUILD_STATIC=ON \
    -DLIBINJECTION_BUILD_TESTS=OFF
make

# 复制产物
cp src/libinjection_static.a ../../lib/
cp ../src/*.h ../../include/libinjection/
```

##### 2. cJSON

```bash
git clone https://github.com/DaveGamble/cJSON.git third_party/cjson

cd third_party/cjson
mkdir build && cd build
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DENABLE_CJSON_TEST=OFF
make

# 复制产物
cp libcJSON.a ../../lib/
cp ../cJSON.h ../../include/
```

##### 3. onnxruntime（ARM Linux）

**选项 A：使用预编译包**

```bash
# 下载 ARM64 预编译版本
wget https://github.com/microsoft/onnxruntime/releases/download/v1.17.0/onnxruntime-linux-aarch64-1.17.0.tgz

tar -xzf onnxruntime-linux-aarch64-1.17.0.tgz
mv onnxruntime-linux-aarch64-1.17.0 third_party/onnxruntime

# 头文件
cp third_party/onnxruntime/include/* third_party/include/onnxruntime/

# 库文件
cp third_party/onnxruntime/lib/libonnxruntime.so* third_party/lib/
```

**选项 B：交叉编译**

```bash
# 安装交叉编译工具链
sudo apt-get install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu

# 克隆并构建
git clone --recursive https://github.com/microsoft/onnxruntime.git
cd onnxruntime

# 交叉编译配置
./build.sh \
    --config Release \
    --build_shared_lib \
    --skip_submodule_sync \
    --cmake_extra_defines CMAKE_TOOLCHAIN_FILE=path/to/toolchain.cmake

# 复制产物
cp build/Linux/Release/libonruntime.so* third_party/lib/
cp include/onnxruntime/* third_party/include/onnxruntime/
```

#### CMakeLists.txt 配置（ARM Linux 离线）

```cmake
cmake_minimum_required(VERSION 3.15)
project(KADD VERSION 1.0.0 LANGUAGES C CXX)

# ========== 检测架构 ==========
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(ARCH "aarch64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
    set(ARCH "arm")
else()
    set(ARCH "x86_64")
endif()

message(STATUS "Target architecture: ${ARCH}")

# ========== 设置第三方库路径 ==========
set(THIRD_PARTY_ROOT "${CMAKE_SOURCE_DIR}/third_party")
set(THIRD_PARTY_INCLUDE "${THIRD_PARTY_ROOT}/include")
set(THIRD_PARTY_LIB "${THIRD_PARTY_ROOT}/lib")

include_directories(${THIRD_PARTY_INCLUDE})
link_directories(${THIRD_PARTY_LIB})

# ========== libinjection ==========
set(LIBINJECTION_ROOT "${THIRD_PARTY_ROOT}/libinjection")

add_library(libinjection_static STATIC
    ${LIBINJECTION_ROOT}/src/libinjection_sqli.c
    ${LIBINJECTION_ROOT}/src/libinjection_xss.c
    ${LIBINJECTION_ROOT}/src/libinjection_html5.c
)

target_include_directories(libinjection_static PUBLIC
    ${LIBINJECTION_ROOT}/src
)

# ========== cJSON ==========
set(CJSON_ROOT "${THIRD_PARTY_ROOT}/cjson")

add_library(cjson STATIC
    ${CJSON_ROOT}/cJSON.c
)

target_include_directories(cjson PUBLIC ${CJSON_ROOT})

# ========== onnxruntime ==========
# 使用预编译共享库
add_library(onnxruntime SHARED IMPORTED)
set_target_properties(onnxruntime PROPERTIES
    IMPORTED_LOCATION "${THIRD_PARTY_LIB}/libonnxruntime.so"
    IMPORTED_SONAME "libonnxruntime.so"
)

# ========== 主目标 ==========
add_executable(kadd_app src/main.c)

target_link_libraries(kadd_app PRIVATE
    libinjection_static
    cjson
    onnxruntime
    dl  # Linux 动态链接库
    m   # 数学库
)

# 安装规则
install(TARGETS kadd_app
    RUNTIME DESTINATION bin
)

install(FILES ${THIRD_PARTY_LIB}/libonnxruntime.so*
    DESTINATION lib
)
```

---

## 验证安装

### 编译测试程序

```c
/* test_deps.c */
#include <stdio.h>
#include "libinjection_sqli.h"
#include "cJSON.h"
#include "onnxruntime_cxx_api.h"

int main() {
    /* 测试 libinjection */
    const char *s = "1' OR '1'='1";
    struct libinjection_sqli_state state;
    libinjection_sqli_init(&state, s, strlen(s), FLAG_NONE);
    printf("libinjection: %s\n", 
           libinjection_is_sqli(&state) ? "SQLi detected" : "Clean");

    /* 测试 cJSON */
    cJSON *json = cJSON_Parse("{\"test\": 123}");
    printf("cJSON: %s\n", cJSON_Print(json));
    cJSON_Delete(json);

    /* 测试 onnxruntime */
    printf("onnxruntime: %s\n", OrtGetApiBase()->GetVersionString());

    return 0;
}
```

### 编译命令

**Windows (MSVC)**:
```cmd
cl test_deps.c /I third_party\include third_party\lib\*.lib
```

**Linux (GCC)**:
```bash
gcc test_deps.c -o test_deps \
    -I third_party/include \
    -L third_party/lib \
    -linjection -lcjson -lonnxruntime -lm -ldl
```

---

## 常见问题

### Q1: Windows 上找不到 DLL

**A**: 确保 DLL 在系统 PATH 或可执行文件同目录下：

```cmake
add_custom_command(TARGET your_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${THIRD_PARTY_ROOT}/onnxruntime/bin/onnxruntime.dll"
    $<TARGET_FILE_DIR:your_app>
)
```

### Q2: ARM Linux 上找不到 libonnxruntime.so

**A**: 设置 LD_LIBRARY_PATH 或使用 RPATH：

```cmake
target_link_options(your_app PRIVATE
    "-Wl,-rpath,${CMAKE_INSTALL_PREFIX}/lib"
)
```

### Q3: 交叉编译 onnxruntime 失败

**A**: 使用官方预编译包或 Docker 容器：

```bash
docker run --rm -v $(pwd):/work -w /work \
    ubuntu:22.04 \
    bash -c "apt-get update && apt-get install -y cmake gcc && ./build.sh"
```

### Q4: 版本兼容性

**A**: 固定依赖库版本：

```cmake
# 使用 Git tag 而非 branch
set(LIBINJECTION_VERSION "v3.10.0")
FetchContent_Declare(
    libinjection
    GIT_REPOSITORY https://github.com/client9/libinjection.git
    GIT_TAG ${LIBINJECTION_VERSION}
)
```

---

## 参考资料

- [CMake FetchContent 文档](https://cmake.org/cmake/help/latest/module/FetchContent.html)
- [libinjection GitHub](https://github.com/client9/libinjection)
- [cJSON GitHub](https://github.com/DaveGamble/cJSON)
- [ONNX Runtime GitHub](https://github.com/microsoft/onnxruntime)
- [ONNX Runtime 构建](https://onnxruntime.ai/docs/build/)

---

**最后更新**: 2026-04-25
**维护者**: KADD 开发团队
