# 动态库构建脚本使用说明

## 概述

项目提供了两个一键构建脚本，用于在依赖已安装的前提下快速生成所有动态库。

## 依赖查找策略

构建脚本支持智能依赖查找：

1. **优先使用系统库** - 首先检查系统中是否已安装依赖
2. **回退到 third_party** - 如果系统库不存在，使用 third_party 目录中的库
3. **可配置** - 通过 `USE_SYSTEM_LIBS` 选项控制

### 优势

- ✅ 开发环境使用系统库，便于管理
- ✅ CI/CD 环境可使用 third_party，无需额外配置
- ✅ 灵活适配不同场景

## 脚本文件

| 平台 | 脚本文件 | 说明 |
|------|----------|------|
| Windows | `build_libs.bat` | Windows 批处理脚本 |
| Linux/macOS | `build_libs.sh` | Bash 脚本 |

## 使用方法

### Windows

```cmd
# 直接双击运行
build_libs.bat

# 或在命令行中运行
build_libs.bat
```

### Linux/macOS

```bash
# 添加执行权限
chmod +x build_libs.sh

# 运行脚本
./build_libs.sh
```

## 脚本功能

### 1. 依赖检查

- 检查构建目录是否存在
- 检查 ONNX Runtime 是否安装（必需）
- 检查 cJSON 是否安装（可选，用于 SQLi 引擎和 DFA Pro）
- 检查 Google Test 是否安装（可选，仅用于测试）
- 提示用户参考 INSTALL.md 安装缺失的依赖

### 2. 清理旧文件

- 删除之前生成的动态库文件

### 3. 配置 CMake

- 自动运行 CMake 配置

### 4. 构建动态库

依次构建以下动态库：
1. `libkadd_dfa_engine.dll/.so/.dylib` - DFA 分词引擎
2. `libkadd_feature_engine.dll/.so/.dylib` - 特征提取引擎
3. `libkadd_ml_engine.dll/.so/.dylib` - ML 推理引擎
4. `libkadd_sqli_engine.dll/.so/.dylib` - SQL 注入检测引擎

### 5. 统计结果

- 显示构建成功的动态库数量
- 列出所有生成的动态库文件

## 输出位置

所有动态库输出到 `build/src/` 目录：

### Windows
```
build/src/
├── libkadd_dfa_engine.dll
├── libkadd_feature_engine.dll
├── libkadd_ml_engine.dll
└── libkadd_sqli_engine.dll
```

### Linux
```
build/src/
├── libkadd_dfa_engine.so
├── libkadd_feature_engine.so
├── libkadd_ml_engine.so
└── libkadd_sqli_engine.so
```

### macOS
```
build/src/
├── libkadd_dfa_engine.dylib
├── libkadd_feature_engine.dylib
├── libkadd_ml_engine.dylib
└── libkadd_sqli_engine.dylib
```

## 前置条件

### 依赖检查

构建脚本会自动检查以下依赖：

#### 必需依赖
- **ONNX Runtime** - ML 引擎必需

#### 可选依赖
- **cJSON** - SQLi 检测引擎和 DFA Pro 需要
- **Google Test** - 运行单元测试需要

### 检查系统库

```bash
# Linux - 检查系统库
ldconfig -p | grep onnxruntime     # ONNX Runtime
pkg-config --exists cjson          # cJSON
pkg-config --exists gtest          # Google Test

# macOS - 检查系统库
brew list | grep onnxruntime       # ONNX Runtime
brew list | grep cjson             # cJSON
brew list | grep googletest        # Google Test

# Windows - 检查系统库
where onnxruntime.dll              # ONNX Runtime
vcpkg list | findstr cjson         # cJSON (vcpkg)
vcpkg list | findstr gtest         # Google Test (vcpkg)
```

### 检查 third_party 库（备用）

```bash
# 检查 third_party 目录
ls third_party/lib/onnxruntime.*   # ONNX Runtime
ls third_party/include/cJSON.h     # cJSON
ls third_party/gtest/src/gtest-all.cc  # Google Test
```

### 构建脚本输出说明

构建脚本会显示找到的依赖来源：

```
[SYSTEM] Dependency Lookup Strategy:
[INFO]   1. Try system libraries first
[INFO]   2. Fallback to third_party directory

[CHECK] Checking ONNX Runtime...
[OK]   System ONNX Runtime found        # 使用系统库
[OK]   third_party ONNX Runtime found   # 使用 third_party

[CHECK] Checking cJSON...
[OK]   System cJSON found               # 使用系统库
[WARN] Will use third_party cJSON       # 使用 third_party
```

### 2. CMake 已配置

如果首次运行，脚本会自动配置 CMake。

## 故障排除

### 脚本执行失败

**症状**: 脚本报错退出

**解决方案**:

1. 检查依赖是否安装
2. 检查 CMake 是否可用
3. 查看具体错误信息

### 动态库构建失败

**症状**: 某个动态库构建失败

**解决方案**:

1. 查看构建日志（移除 `> /dev/null 2>&1`）
2. 检查对应的源文件是否存在
3. 确认依赖库已正确链接

### 权限问题

**症状**: Permission denied

**解决方案**:

```bash
# Linux/macOS
chmod +x build_libs.sh
```

## 集成到其他项目

### Windows

```c
// 1. 复制头文件
copy include\*.h your_project\include\

// 2. 复制动态库
copy build\src\*.dll your_project\lib\

// 3. 复制依赖库
copy third_party\lib\onnxruntime.dll your_project\

// 4. 链接
// 在 Visual Studio 项目设置中添加:
// - 头文件目录: include
// - 库文件目录: lib
// - 附加依赖: kadd_dfa_engine; kadd_feature_engine; kadd_ml_engine
```

### Linux

```bash
# 1. 复制头文件
sudo cp include/*.h /usr/local/include/

# 2. 复制动态库
sudo cp build/src/libkadd_*.so /usr/local/lib/

# 3. 更新动态库缓存
sudo ldconfig

# 4. 链接示例
gcc -o your_app your_app.c -L/usr/local/lib -lkadd_dfa_engine -lkadd_feature_engine -lkadd_ml_engine
```

## 版本信息

- **脚本版本**: 1.0.0
- **支持平台**: Windows (MinGW), Linux, macOS
- **支持架构**: x86_64, ARM64
- **CMake 最低版本**: 3.15
- **编译器要求**: GCC 7+ (Linux), Clang 10+ (macOS), MinGW 8+ (Windows)

## 跨平台兼容性

### Windows
- 编译器：MinGW-w64
- 动态库：`.dll`
- 依赖：`onnxruntime.dll`

### Linux
- 编译器：GCC 7+
- 动态库：`.so`
- 依赖：`libonnxruntime.so`
- 安装：`ldconfig` 或 `LD_LIBRARY_PATH`

### macOS
- 编译器：Clang 10+
- 动态库：`.dylib`
- 依赖：`libonnxruntime.dylib`
- 安装：`update_dyld_shared_cache` 或 `DYLD_LIBRARY_PATH`
