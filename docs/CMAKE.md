# KADD CMake 构建系统指南

本文档详细说明 KADD 项目的 CMake 构建系统架构和配置方法。

## 目录结构

```
KADD/
├── CMakeLists.txt              # 主 CMake 配置文件
├── include/CMakeLists.txt      # 头文件安装配置
├── src/CMakeLists.txt          # 源代码和库构建
├── demo/CMakeLists.txt         # 示例程序构建
└── tests/CMakeLists.txt        # 测试程序构建
```

## 架构设计原则

### 1. 模块化设计
每个目录独立管理自己的构建逻辑，通过函数封装减少重复代码。

### 2. 依赖查找策略
```
环境变量 → 系统库 → third_party → 错误
```

### 3. 条件构建
支持可选组件（如测试、cJSON 相关功能）。

## 主 CMakeLists.txt 配置

### 项目基本信息

```cmake
cmake_minimum_required(VERSION 3.16)
project(KADD VERSION 1.0.0 LANGUAGES C CXX ASM)

# C/C++ 标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
```

### 构建选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `BUILD_TESTS` | ON | 构建测试程序 |
| `USE_SYSTEM_LIBS` | ON | 优先使用系统库 |
| `ENABLE_CODE_COVERAGE` | OFF | 启用代码覆盖率 |
| `KADD_OPTIMIZE` | OFF | 启用 -O3 优化 |
| `ENABLE_ARM_SVE` | OFF | 启用 ARM SVE 加速（ARMv9-A） |
| `ENABLE_ARM_SVE2` | OFF | 启用 ARM SVE2 加速（ARMv9.2-A+） |

**使用示例：**
```bash
# 启用优化
cmake -DKADD_OPTIMIZE=ON ..

# 禁用测试
cmake -DBUILD_TESTS=OFF ..

# 强制使用 third_party
cmake -DUSE_SYSTEM_LIBS=OFF ..

# 启用 ARM SVE 加速（ARMv9-A）
cmake -DENABLE_ARM_SVE=ON ..

# 启用 ARM SVE2 加速（ARMv9.2-A+，性能更好）
cmake -DENABLE_ARM_SVE2=ON ..

# 组合选项：最佳性能
cmake -DKADD_OPTIMIZE=ON -DENABLE_ARM_SVE2=ON -DCMAKE_BUILD_TYPE=Release ..
```

#### ARM SVE 加速

**SVE（Scalable Vector Extension）** 是 ARMv9-A 架构引入的向量指令集。

| 选项 | 说明 | 架构要求 |
|------|------|----------|
| `ENABLE_ARM_SVE=OFF` | 禁用 SVE，使用标量实现 | 任意 |
| `ENABLE_ARM_SVE=ON` | 启用 SVE（128-bit 向量） | ARMv9-A |
| `ENABLE_ARM_SVE2=ON` | 启用 SVE2（可变长度向量） | ARMv9.2-A+ |

**性能对比：**
```
标量实现:     1.0x (基准)
NEON 优化:    2-3x
SVE 优化:     3-5x
SVE2 优化:    4-6x
```

**自动检测：**
- 如果不是 ARM64 平台，启用 SVE 选项会自动禁用并警告
- 启用 SVE2 会自动启用 SVE

**验证 SVE 已启用：**
```bash
# 检查 CMake 配置
cmake -LA | grep ENABLE_ARM_SVE

# 检查编译标志
make VERBOSE=1 | grep "march=armv9-a+sve"

# 检查生成的二进制
objdump -d build/lib/libkadd_feature_engine.so | grep -E "sve|whilelo|ld1w"
```

### 依赖查找

#### ONNX Runtime

**查找优先级：**
1. 环境变量 `ONNXRUNTIME_HOME`
2. `third_party/` 目录
3. 系统路径 (`/usr/local`, `/usr`)

**配置方式：**
```bash
# 方式 1：环境变量（推荐）
export ONNXRUNTIME_HOME=/opt/onnxruntime
cmake ..

# 方式 2：系统安装
sudo apt-get install libonnxruntime-dev
cmake ..

# 方式 3：third_party
# 将 libonnxruntime.so 放入 third_party/lib/
cmake ..
```

#### cJSON

**可选依赖**，用于 DFA Pro 和 SQLi 引擎。

```bash
# 系统安装
sudo apt-get install libcjson-dev

# 或使用 third_party
# third_party/include/cJSON.h
# third_party/src/cJSON.c
```

#### Google Test

**仅在构建测试时需要**。

```bash
# 系统安装
sudo apt-get install libgtest-dev

# 或使用 third_party
# third_party/gtest/src/gtest-all.cc
```

## src/CMakeLists.txt - 库构建

### 通用函数：add_kadd_library

封装动态库创建逻辑，统一处理依赖。

**函数签名：**
```cmake
add_kadd_library(NAME
    VERSION version
    SOURCES source_files
    [NEED_CJSON]
    [NEED_ONNX]
    [LINK_LIBRARIES libs...]
)
```

**使用示例：**

#### 1. 基础库（无外部依赖）
```cmake
add_kadd_library(kadd_dfa_engine
    VERSION 1.0.0
    SOURCES kadd_dfa_engine.c
)
```

#### 2. 需要 ONNX Runtime
```cmake
add_kadd_library(kadd_ml_engine
    VERSION 1.0.0
    SOURCES kadd_ml_engine.c
    NEED_ONNX
)
```

#### 3. 需要 cJSON
```cmake
add_kadd_library(kadd_dfa_engine_pro
    VERSION 1.0.0
    SOURCES kadd_dfa_engine_pro.c
    NEED_CJSON
)
```

#### 4. 复杂依赖
```cmake
add_kadd_library(kadd_sqli_engine
    VERSION 1.0.0
    SOURCES kadd_sqli_engine.c
    NEED_CJSON
    LINK_LIBRARIES kadd_dfa_engine kadd_feature_engine kadd_ml_engine
)
```

### 输出库

| 库名 | 说明 | 依赖 |
|------|------|------|
| `libkadd_dfa_engine.so` | DFA 分词引擎 | 无 |
| `libkadd_feature_engine.so` | 特征提取引擎 | 无 |
| `libkadd_ml_engine.so` | ML 推理引擎 | ONNX Runtime |
| `libkadd_dfa_engine_pro.so` | 增强型 DFA | cJSON |
| `libkadd_sqli_engine.so` | SQL 注入检测引擎 | cJSON + 其他引擎 |

所有库输出到 `build/lib/` 目录。

## demo/CMakeLists.txt - 示例程序

### 示例程序列表

| 程序 | 说明 | 依赖 |
|------|------|------|
| `kadd_feature_demo` | 特征提取演示 | libkadd_feature_engine.so |
| `kadd_ml_demo` | ML 推理演示 | libkadd_ml_engine.so + ONNX Runtime |
| `test_onnx_20` | ONNX Runtime 兼容性测试 | ONNX Runtime |
| `test_onnx_arm` | ARM 平台诊断工具 | ONNX Runtime |

### RPATH 配置

所有程序配置了 RPATH，运行时自动查找依赖库：

```cmake
set_target_properties(kadd_ml_demo PROPERTIES
    INSTALL_RPATH "$ORIGIN;$ORIGIN/../src:${CMAKE_BINARY_DIR}/src"
    BUILD_WITH_INSTALL_RPATH TRUE
)
```

**含义：**
- `$ORIGIN` - 可执行文件所在目录
- `$ORIGIN/../src` - 上级 src 目录
- 构建目录的 src 路径

## tests/CMakeLists.txt - 测试构建

### 通用函数：add_kadd_gtest

封装 Google Test 配置逻辑。

**函数签名：**
```cmake
add_kadd_gtest(TEST_NAME source_file)
```

**自动处理：**
- C++17 标准
- 链接 `kadd_other_engines` 静态库
- 链接 Google Test（支持系统库或 third_party）
- 链接 ONNX Runtime 和 cJSON（如果可用）
- 注册到 CTest

### 测试列表

| 测试 | 源文件 | 说明 |
|------|--------|------|
| `ml_engine_test` | test_ml_engine_gtest.cpp | ML 引擎单元测试 |
| `feature_engine_test` | test_feature_engine_gtest.cpp | 特征引擎单元测试 |

### 运行测试

```bash
# 构建测试
cmake --build build

# 运行所有测试
cd build
ctest --verbose

# 运行单个测试
./ml_engine_test

# 运行特定测试用例
./ml_engine_test --gtest_filter="*Accuracy*"
```

## include/CMakeLists.txt - 头文件管理

### 公共头文件列表

```cmake
set(KADD_PUBLIC_HEADERS
    kadd_dfa_engine.h
    kadd_dfa_engine_pro.h
    kadd_feature_engine.h
    kadd_ml_engine.h
    kadd_sqli_engine.h
)
```

### 安装头文件

```bash
# 安装到系统
cmake --build build --target install

# 默认安装路径
/usr/local/include/
```

## 常见使用场景

### 场景 1：开发构建

```bash
# 标准构建
mkdir build && cd build
cmake -G "Unix Makefiles" ..
cmake --build .

# 输出到 build/lib/
ls build/lib/*.so
```

### 场景 2：优化构建

```bash
# 标准优化
cmake -DKADD_OPTIMIZE=ON -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

### 场景 3：ARM SVE 优化构建（ARMv9-A）

```bash
# SVE 加速（ARMv9-A）
cmake -DENABLE_ARM_SVE=ON -DCMAKE_BUILD_TYPE=Release ..
cmake --build .

# SVE2 加速（ARMv9.2-A+，最佳性能）
cmake -DENABLE_ARM_SVE2=ON -DCMAKE_BUILD_TYPE=Release ..
cmake --build .

# 组合优化 + SVE2
cmake -DKADD_OPTIMIZE=ON \
      -DENABLE_ARM_SVE2=ON \
      -DCMAKE_BUILD_TYPE=Release \
      -H. -Bbuild -G "Unix Makefiles"
cmake --build build
```

### 场景 4：调试构建

```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
cmake --build .
```

### 场景 5：代码覆盖率测试

```bash
# 配置项目（启用覆盖率）
cmake -DENABLE_CODE_COVERAGE=ON -H. -Bbuild -G "Unix Makefiles"

# 构建项目
cmake --build build

# 运行测试
cd build
ctest --verbose

# 生成覆盖率报告
make coverage

# 查看报告（HTML 格式）
# 报告位置: build/coverage_html/index.html
xdg-open coverage_html/index.html  # Linux
# 或用浏览器打开: file:///path/to/build/coverage_html/index.html

# 清理覆盖率数据
make coverage-clean
```

**覆盖率报告内容：**
- 行覆盖率（Line Coverage）
- 分支覆盖率（Branch Coverage）
- 函数覆盖率（Function Coverage）
- HTML 可视化报告

**注意事项：**
- 覆盖率编译会降低性能，仅用于测试环境
- 需要安装 `lcov` 工具：`sudo apt-get install lcov`
- 测试必须完整执行才能获得准确的覆盖率数据

### 场景 6：使用特定 ONNX Runtime

```bash
export ONNXRUNTIME_HOME=/opt/onnxruntime-1.16.0
cmake ..
cmake --build .
```

## 构建输出

### 目录结构

```
build/
├── lib/
│   ├── libkadd_dfa_engine.so
│   ├── libkadd_feature_engine.so
│   ├── libkadd_ml_engine.so
│   ├── libkadd_dfa_engine_pro.so     # 如果 cJSON 可用
│   └── libkadd_sqli_engine.so        # 如果 cJSON 可用
└── bin/
    ├── kadd_feature_demo
    ├── kadd_ml_demo
    ├── test_onnx_20
    └── test_onnx_arm
```

### 版本信息

所有动态库设置版本属性：
- `VERSION`: 1.0.0
- `SOVERSION`: 1

实际文件名：
```
libkadd_dfa_engine.so -> libkadd_dfa_engine.so.1 -> libkadd_dfa_engine.so.1.0.0
```

## 故障排查

### 问题 1：ONNX Runtime 未找到

```bash
# 检查环境变量
echo $ONNXRUNTIME_HOME

# 检查系统库
ldconfig -p | grep onnxruntime

# 检查 third_party
ls third_party/lib/libonnxruntime*
```

### 问题 2：运行时找不到库

```bash
# 检查 RPATH
readelf -d build/bin/kadd_ml_demo | grep RPATH

# 设置 LD_LIBRARY_PATH
export LD_LIBRARY_PATH=$ONNXRUNTIME_HOME/lib:$LD_LIBRARY_PATH
```

### 问题 3：测试失败

```bash
# 详细输出
ctest --verbose

# 单独运行测试
./build/ml_engine_test --gtest_verbose
```

## 扩展指南

### 添加新引擎

1. 在 `src/` 添加源文件
2. 在 `src/CMakeLists.txt` 调用 `add_kadd_library`
3. 在 `include/` 添加头文件
4. 更新 `include/CMakeLists.txt`

**示例：**
```cmake
# src/CMakeLists.txt
add_kadd_library(kadd_new_engine
    VERSION 1.0.0
    SOURCES kadd_new_engine.c
    NEED_ONNX
)
```

### 添加新测试

1. 在 `tests/` 添加 `test_*.cpp` 文件
2. 在 `tests/CMakeLists.txt` 调用 `add_kadd_gtest`

**示例：**
```cmake
# tests/CMakeLists.txt
add_kadd_gtest(new_engine_test test_new_engine_gtest.cpp)
```

### 添加新示例

1. 在 `demo/` 添加源文件
2. 在 `demo/CMakeLists.txt` 配置可执行文件

**示例：**
```cmake
# demo/CMakeLists.txt
add_executable(new_demo new_demo.c)
target_link_libraries(new_demo PRIVATE kadd_new_engine)
```

## 最佳实践

1. **使用环境变量** 指定依赖路径，便于 CI/CD
2. **优先系统库** 便于依赖管理和更新
3. **使用函数封装** 减少重复代码
4. **设置 RPATH** 简化运行时库查找
5. **版本化管理** 库文件使用版本号
6. **条件构建** 可选组件使用条件判断
