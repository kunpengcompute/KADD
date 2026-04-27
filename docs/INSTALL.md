# 安装指南

本文档介绍如何在不同平台上安装依赖、构建项目和安装动态库。

## 依赖管理策略

本项目支持两种依赖管理方式：

### 方式 1：系统库安装（推荐）
- 优点：符合系统管理规范，便于更新，多个项目共享
- 缺点：需要管理员权限
- 适合：生产环境、长期使用的开发环境

### 方式 2：third_party 目录
- 优点：无需管理员权限，项目自包含
- 缺点：占用更多磁盘空间，需手动管理更新
- 适合：临时测试、无管理员权限的环境

**默认行为**：CMake 会优先查找系统库，如果找不到则使用 third_party 目录中的库。

**强制模式**：
- `cmake -DUSE_SYSTEM_LIBS=ON` - 强制使用系统库（默认）
- `cmake -DUSE_SYSTEM_LIBS=OFF` - 强制使用 third_party

## 目录

- [系统要求](#系统要求)
- [依赖关系表](#依赖关系表)
- [安装 ONNX Runtime](#安装-onnx-runtime)
- [安装 Google Test](#安装-google-test)
- [安装 cJSON](#安装-cjson)
- [构建项目](#构建项目)
- [构建动态库](#构建动态库)
- [安装动态库](#安装动态库)
- [常见问题](#常见问题)

## 系统要求

| 平台 | 最低要求 | 推荐配置 |
|------|----------|----------|
| Linux | GCC 7+, CMake 3.15+ | GCC 10+, CMake 3.20+ |
| Windows | MinGW 8+, CMake 3.15+ | MinGW 11+, CMake 3.20+ |
| macOS | Clang 10+, CMake 3.15+ | Clang 13+, CMake 3.20+ |
| ARM (可选) | ARMv9-A, GCC 10+ | ARMv9-A, GCC 12+ |

## 安装方式选择

### 推荐安装路径

| 平台 | 系统库安装路径 | third_party 路径 |
|------|---------------|------------------|
| Linux | `/usr/local/lib`, `/usr/lib` | `third_party/lib/` |
| macOS | `/usr/local/lib` (Homebrew) | `third_party/lib/` |
| Windows | `C:\Program Files\`, `C:\msys64\mingw64\` | `third_party\lib\` |

### 依赖关系表

| 功能模块 | 需要的依赖 | 安装建议 |
|---------|-----------|----------|
| DFA 引擎 | 无外部依赖 | - |
| 特征引擎 | 无外部依赖 | - |
| ML 引擎 | ONNX Runtime | 推荐系统安装 |
| SQLi 检测引擎 | ONNX Runtime + cJSON | 推荐系统安装 |
| DFA Pro 引擎 | cJSON | 推荐系统安装 |
| 单元测试 | Google Test + ONNX Runtime + cJSON | 推荐系统安装 |

## 安装 ONNX Runtime

ONNX Runtime 是推理功能的必需依赖。

### Linux (x86_64)

```bash
# 下载
wget https://github.com/microsoft/onnxruntime/releases/download/v1.16.0/onnxruntime-linux-x64-1.16.0.tgz

# 解压
tar -xzf onnxruntime-linux-x64-1.16.0.tgz

# 安装到系统目录
sudo cp -r onnxruntime-linux-x64-1.16.0/include/* /usr/local/include/
sudo cp -r onnxruntime-linux-x64-1.16.0/lib/* /usr/local/lib/
sudo ldconfig

# 验证
ldconfig -p | grep onnxruntime
```

### Linux (ARM64)

```bash
# 下载 ARM64 版本
wget https://github.com/microsoft/onnxruntime/releases/download/v1.16.0/onnxruntime-linux-aarch64-1.16.0.tgz

# 解压并安装（同上）
tar -xzf onnxruntime-linux-aarch64-1.16.0.tgz
sudo cp -r onnxruntime-linux-aarch64-1.16.0/include/* /usr/local/include/
sudo cp -r onnxruntime-linux-aarch64-1.16.0/lib/* /usr/local/lib/
sudo ldconfig
```

### macOS

```bash
# 使用 Homebrew 安装
brew install onnxruntime

# 验证
otool -L /usr/local/lib/libonnxruntime.dylib
```

### Windows (vcpkg)

```bash
# 安装 vcpkg（如果还没有）
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.bat

# 安装 ONNX Runtime
./vcpkg install onnxruntime:x64-windows

# 集成到 CMake
./vcpkg integrate install
```

### Windows (手动安装)

```bash
# 1. 下载预构建版本
# 访问 https://github.com/microsoft/onnxruntime/releases
# 下载 onnxruntime-win-x64-1.16.0.zip

# 2. 创建目录并解压
mkdir -p third_party/lib
unzip onnxruntime-win-x64-1.16.0.zip -d third_party/lib/

# 3. 确保 DLL 在 PATH 中或与可执行文件在同一目录
```

## 安装 Google Test

Google Test 是可选的，仅在运行单元测试时需要。

### Linux

```bash
# 使用包管理器安装（推荐）
# Debian/Ubuntu
sudo apt-get install libgtest-dev

# CentOS/RHEL
sudo yum install gtest-devel

# Arch Linux
sudo pacman -S gtest

# 验证
pkg-config --modversion gtest
```

```bash
# 从源码安装到系统目录
git clone https://github.com/google/googletest.git
cd googletest
git checkout release-1.12.1

mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make -j$(nproc)
sudo make install
sudo ldconfig

# 验证
pkg-config --cflags gtest
pkg-config --libs gtest
ls /usr/local/lib/libgtest.*
```

### macOS

```bash
# 使用 Homebrew 安装
brew install googletest

# 验证
brew list googletest
pkg-config --cflags gtest
```

```bash
# 从源码安装到系统目录
git clone https://github.com/google/googletest.git
cd googletest
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make -j$(sysctl -n hw.ncpu)
sudo make install

# 验证
pkg-config --cflags gtest
```

### Windows (vcpkg)

```cmd
vcpkg install gtest:x64-windows
vcpkg integrate install

# 验证
vcpkg list gtest
```

### Windows (MSYS2)

```bash
pacman -S mingw-w64-x86_64-gtest

# 验证
pacman -Q mingw-w64-x86_64-gtest
```

### Windows (手动安装)

```cmd
REM 从源码构建并安装
git clone https://github.com/google/googletest.git
cd googletest
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=C:\msys64\mingw64 ..
cmake --build . --config Release
cmake --install .

REM 验证
dir C:\msys64\mingw64\include\gtest\gtest.h
dir C:\msys64\mingw64\lib\libgtest.*
```

## 安装 cJSON

cJSON 是一个轻量级的 JSON 解析库，用于 SQL 注入检测引擎的配置文件解析。

### Linux

```bash
# 使用包管理器安装（推荐）
# Debian/Ubuntu
sudo apt-get install libcjson-dev

# CentOS/RHEL
sudo yum install cjson-devel

# Arch Linux
sudo pacman -S cjson

# 验证
pkg-config --modversion cjson
```

```bash
# 从源码安装到系统目录
git clone https://github.com/DaveGamble/cJSON.git
cd cJSON
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make -j$(nproc)
sudo make install
sudo ldconfig

# 验证
pkg-config --cflags cjson
pkg-config --libs cjson
ls /usr/local/lib/libcjson.*
```

### macOS

```bash
# 使用 Homebrew 安装
brew install cjson

# 验证
brew list cjson
pkg-config --cflags cjson
```

```bash
# 从源码安装到系统目录
git clone https://github.com/DaveGamble/cJSON.git
cd cJSON
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make -j$(sysctl -n hw.ncpu)
sudo make install

# 验证
pkg-config --cflags cjson
```

### Windows (vcpkg)

```cmd
vcpkg install cjson:x64-windows

# 验证
vcpkg list cjson
```

### Windows (MSYS2)

```bash
pacman -S mingw-w64-x86_64-cjson

# 验证
pacman -Q mingw-w64-x86_64-cjson
```

### Windows (手动安装)

```cmd
REM 从源码构建并安装
git clone https://github.com/DaveGamble/cJSON.git
cd cJSON
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=C:\msys64\mingw64 ..
cmake --build . --config Release
cmake --install .

REM 验证
dir C:\msys64\mingw64\include\cJSON.h
dir C:\msys64\mingw64\lib\libcjson.*
```

## 验证系统安装

安装完成后，使用以下命令验证依赖是否正确安装到系统：

### Linux

```bash
# 检查 ONNX Runtime
ldconfig -p | grep onnxruntime
ls /usr/local/lib/libonnxruntime.*

# 检查 cJSON
pkg-config --exists cjson && echo "cJSON installed" || echo "cJSON not found"
pkg-config --cflags --libs cjson

# 检查 Google Test
pkg-config --exists gtest && echo "GTest installed" || echo "GTest not found"
pkg-config --cflags --libs gtest

# 检查所有已安装的库
ldconfig -p | grep -E "(onnx|cjson|gtest)"
```

### macOS

```bash
# 检查 ONNX Runtime
brew list | grep onnxruntime
otool -L /usr/local/lib/libonnxruntime.dylib

# 检查 cJSON
brew list | grep cjson
pkg-config --cflags --libs cjson

# 检查 Google Test
brew list | grep googletest
pkg-config --cflags --libs gtest

# 检查所有已安装的库
brew list | grep -E "(onnx|cjson|gtest)"
```

### Windows

```cmd
REM 检查 vcpkg 安装的包
vcpkg list | findstr /i "onnx cjson gtest"

REM 检查 MSYS2 安装的包
pacman -Q | findstr /i "onnx cjson gtest"

REM 检查系统目录
dir C:\msys64\mingw64\lib\onnxruntime.*
dir C:\msys64\mingw64\lib\libcjson.*
dir C:\msys64\mingw64\lib\libgtest.*

REM 检查头文件
dir C:\msys64\mingw64\include\cJSON.h
dir C:\msys64\mingw64\include\gtest\gtest.h
```

## 构建项目

### 基本构建

```bash
# 创建构建目录
mkdir build && cd build

# 配置 CMake
cmake .. -DUSE_ONNX=ON

# 编译
cmake --build .

# 运行测试
ctest --verbose
```

### Windows MinGW 构建

```bash
# 配置
cmake .. -G "MinGW Makefiles" -DUSE_ONNX=ON

# 编译
mingw32-make

# 运行测试
.\ml_engine_test.exe
.\feature_engine_test.exe
```

## 构建动态库

项目支持将各个引擎编译为动态库（.so/.dll/.dylib），供其他项目使用。

### 当前状态

✅ 已支持构建动态库：
- `libkadd_dfa_engine` - DFA 分词引擎
- `libkadd_feature_engine` - 特征提取引擎
- `libkadd_ml_engine` - ML 推理引擎
- `libkadd_sqli_engine` - SQL 注入检测引擎

### 一键构建

```bash
# Windows
build_libs.bat

# Linux/macOS
chmod +x build_libs.sh
./build_libs.sh
```

### 标准构建

```bash
# Windows (MinGW)
cmake -H. -Bbuild -G "MinGW Makefiles"
cmake --build build

# Linux/macOS
cmake -H. -Bbuild -G "Unix Makefiles"
cmake --build build
```

### 动态库输出

| 平台 | 输出目录 | 扩展名 | 文件名 |
|------|----------|--------|--------|
| Windows | `build/src/` | `.dll` | `libkadd_dfa_engine.dll`, `libkadd_feature_engine.dll`, `libkadd_ml_engine.dll`, `libkadd_sqli_engine.dll` |
| Linux | `build/src/` | `.so` | `libkadd_dfa_engine.so`, `libkadd_feature_engine.so`, `libkadd_ml_engine.so`, `libkadd_sqli_engine.so` |
| macOS | `build/src/` | `.dylib` | `libkadd_dfa_engine.dylib`, `libkadd_feature_engine.dylib`, `libkadd_ml_engine.dylib`, `libkadd_sqli_engine.dylib` |

### 依赖关系

- `libkadd_ml_engine` 依赖 ONNX Runtime
- `libkadd_sqli_engine` 依赖 DFA、特征、ML 引擎和 cJSON

## 安装动态库

### Linux

```bash
# 安装到系统目录
sudo cp build/src/libkadd_*.so /usr/local/lib/
sudo cp build/src/libkadd_*.so.* /usr/local/lib/  # 版本符号链接
sudo cp include/*.h /usr/local/include/
sudo ldconfig
```

### Windows

```bash
# 方法 1: 复制到系统目录
copy build\src\*.dll C:\Windows\System32\

# 方法 2: 添加到 PATH
# 将 build\src\ 目录添加到系统环境变量 PATH

# 方法 3: 复制到项目目录
# 将 DLL 复制到使用项目的根目录
```

### macOS

```bash
# 安装到系统目录
sudo cp build/src/libkadd_*.dylib /usr/local/lib/
sudo cp include/*.h /usr/local/include/
sudo update_dyld_shared_cache
```

### 验证安装

```bash
# Linux
ldconfig -p | grep kadd
ls -l /usr/local/lib/libkadd_*.so

# macOS
otool -L /usr/local/lib/libkadd_dfa_engine.dylib
ls -l /usr/local/lib/libkadd_*.dylib

# Windows
where libkadd_dfa_engine.dll
dir C:\Windows\System32\libkadd_*.dll
```

## 启用 ARM SVE 加速

```bash
cmake .. -DENABLE_SVE=ON -DUSE_ONNX=ON
cmake --build .
```

**要求**: ARMv9-A 处理器，GCC 10+ 或 Clang 12+

### 自定义 ONNX Runtime 路径

```bash
export ONNXRuntime_DIR=/custom/path/to/onnxruntime
cmake .. -DUSE_ONNX=ON
```

## 常见问题

### Q: 找不到 ONNX Runtime

**症状**: CMake 报错 "Could not find ONNXRuntime"

**解决方案**:

```bash
# 检查系统库
ldconfig -p | grep onnxruntime    # Linux
otool -L /usr/local/lib/libonnxruntime.dylib  # macOS

# 或设置自定义路径
export ONNXRuntime_DIR=/path/to/onnxruntime
cmake .. -DUSE_ONNX=ON
```

### Q: Windows 上找不到 DLL

**症状**: 运行时报错 "找不到 onnxruntime.dll"

**解决方案**:

```bash
# 方法 1: 复制 DLL 到构建目录
copy third_party\lib\onnxruntime.dll build\

# 方法 2: 添加到系统 PATH
set PATH=%PATH%;C:\path\to\onnxruntime\bin
```

### Q: Google Test 下载失败

**症状**: CMake 配置时找不到 gtest

**解决方案**:

```bash
# 检查 third_party/gtest 目录是否存在
ls third_party/gtest/

# 如果不存在，按照上面的安装指南安装 Google Test

# 或者在构建时跳过测试
cmake .. -DBUILD_TESTS=OFF
```

### Q: ARM SVE 编译错误

**症状**: 编译时报错 "error: 'arm_sve.h' file not found"

**解决方案**:

```bash
# 确认编译器版本
gcc --version  # 需要 GCC 10+

# 确认平台
uname -m       # 需要 aarch64

# 检查 SVE 支持
cat /proc/cpuinfo | grep sve
```

## 编译选项参考

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `-DUSE_ONNX=ON` | OFF | 启用 ONNX Runtime 支持（必需） |
| `-DENABLE_SVE=ON` | OFF | 启用 ARM SVE 加速 |
| `-DBUILD_TESTS=ON` | OFF | 构建测试程序（需要 Google Test） |
| `-DBUILD_DEMOS=ON` | ON | 构建 Demo 程序 |
| `-DENABLE_COVERAGE=ON` | OFF | 启用代码覆盖率 |

## 验证安装

```bash
# 运行测试
cd build

# Windows
./ml_engine_test.exe
./feature_engine_test.exe

# Linux/macOS
./ml_engine_test
./feature_engine_test

# 应该看到类似输出：
# ========================================
#   ML 引擎测试
# ========================================
# [==========] Running 33 tests from 1 test suite.
# [  PASSED  ] 33 tests.
```
