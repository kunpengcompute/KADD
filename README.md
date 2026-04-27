# KADD SQL 注入检测引擎

基于 ONNX Runtime 的轻量级 SQL 注入检测引擎，支持 DFA 分词、特征转换和模型推理。

## ✨ 特性

- 🔍 **准确检测** - 99.77% 准确率，100% 精确率
- 🚀 **高性能** - 单样本推理 ~100,000 ops/sec
- 📦 **模块化设计** - DFA、特征、ML 引擎独立可用
- 🎯 **ARM SVE 加速** - 支持 ARMv9-A SVE 向量化
- 🔧 **易于集成** - 简洁的 C API，支持跨平台

## 🚀 快速开始

### 依赖管理

本项目支持灵活的依赖管理方式：

**方式 1：系统库安装（推荐）**
```bash
# Linux
sudo apt-get install libonnxruntime-dev libcjson-dev libgtest-dev

# macOS
brew install onnxruntime cjson googletest
```

**方式 2：third_party 目录**
- 下载依赖到 `third_party/` 目录
- 适合无管理员权限的环境

**默认行为**：优先使用系统库，回退到 third_party

### 依赖要求

| 模块 | 必需依赖 | 可选依赖 |
|------|---------|----------|
| 基础引擎 | CMake 3.15+, 编译器 | - |
| ML 引擎 | ONNX Runtime | - |
| SQLi 引擎 | ONNX Runtime | cJSON |
| 单元测试 | ONNX Runtime | Google Test, cJSON |

> 详细安装指南请参阅 [安装文档](docs/INSTALL.md)

### 一键构建动态库

```bash
# Windows (CMD/PowerShell)
build_libs.bat

# Linux/macOS
chmod +x build_libs.sh
./build_libs.sh
```

### 平台特定配置

#### Windows (MinGW)
```bash
cmake -H. -Bbuild -G "MinGW Makefiles"
cmake --build build
```

#### Linux
```bash
cmake -H. -Bbuild -G "Unix Makefiles"
cmake --build build
```

#### macOS
```bash
cmake -H. -Bbuild -G "Unix Makefiles"
cmake --build build
```

### 完整构建流程

```bash
# 1. 安装依赖
# 详见 docs/INSTALL.md

# 2. 配置项目
# Windows (MinGW)
cmake -H. -Bbuild -G "MinGW Makefiles"

# Linux/macOS
cmake -H. -Bbuild -G "Unix Makefiles"

# 3. 构建动态库
# Windows: build_libs.bat
# Linux/macOS: ./build_libs.sh

# 4. 运行测试
# Windows
./build/ml_engine_test.exe
./build/feature_engine_test.exe

# Linux/macOS
./build/ml_engine_test
./build/feature_engine_test
```

## 🧪 可用测试

| 测试 | 说明 | 命令 |
|------|------|------|
| ML 引擎测试 | 模型推理、准确率、性能测试 | `./build/ml_engine_test.exe` |
| 特征引擎测试 | 特征提取、滑动窗口测试 | `./build/feature_engine_test.exe` |
| DFA 引擎测试 | 分词器测试（待启用） | - |

## 📦 动态库

### 跨平台支持

| 平台 | 动态库扩展名 | 输出目录 |
|------|--------------|----------|
| Windows | `.dll` | `build/src/` |
| Linux | `.so` | `build/src/` |
| macOS | `.dylib` | `build/src/` |

### 可用库

| 库 | Windows | Linux/macOS | 说明 |
|------|---------|-------------|------|
| DFA 引擎 | `libkadd_dfa_engine.dll` | `libkadd_dfa_engine.so` | 字符串分词 |
| 特征引擎 | `libkadd_feature_engine.dll` | `libkadd_feature_engine.so` | 序列转特征向量 |
| ML 引擎 | `libkadd_ml_engine.dll` | `libkadd_ml_engine.so` | ONNX 模型推理 |
| SQL 注入检测引擎 | `libkadd_sqli_engine.dll` | `libkadd_sqli_engine.so` | 完整检测 API |

动态库位于 `build/src/` 目录，可用于其他项目。

运行特定测试：
```bash
# 只运行准确率测试
./build/ml_engine_test.exe --gtest_filter="*Accuracy*"

# 运行所有测试
ctest --verbose
```

## 📚 文档

- [安装指南](docs/INSTALL.md) - 详细的安装步骤和依赖说明
- [构建脚本](docs/BUILD_LIBS.md) - 一键构建动态库脚本使用说明
- [API 文档](docs/API.md) - 完整的 API 使用手册和示例代码
- [测试文档](docs/TEST.md) - 测试框架和运行说明
- [设计文档](docs/DESIGN.md) - 架构设计和模块说明

## 💡 使用示例

```c
#include "kadd_sqli_engine.h"

int main() {
    kadd_sqli_engine_t *engine = kadd_sqli_engine_create_from_file("config.json");
    
    kadd_sqli_result_t result;
    const char *input = "id=1' OR '1'='1";
    
    if (kadd_sqli_detect(engine, input, &result) == 0) {
        if (result.is_sqli) {
            printf("SQL 注入检测到! 概率: %.2f%%\n", result.probability * 100);
        }
        kadd_sqli_result_free(&result);
    }
    
    kadd_sqli_engine_free(engine);
    return 0;
}
```

更多示例请参阅 [API 文档](docs/API.md)。

## 🏗️ 架构

```
输入字符串 → DFA 分词器 → Token 索引 → 特征提取器 → 特征向量 → ML 模型 → 概率
```

### 核心组件

| 组件 | 功能 |
|------|------|
| DFA 引擎 | 字符串分词 |
| 特征引擎 | 序列转特征向量 |
| ML 引擎 | ONNX 模型推理 |
| 检测引擎 | 整合检测 API |

## 📊 性能

| 指标 | 数值 |
|------|------|
| 准确率 | 99.77% |
| 精确率 | 100% |
| 召回率 | 99.77% |
| 单样本推理 | ~100,000 ops/sec |

## 🔧 编译选项

| 选项 | 说明 |
|------|------|
| `-DUSE_ONNX=ON` | 启用 ONNX Runtime 支持 |
| `-DENABLE_SVE=ON` | 启用 ARM SVE 加速 |
| `-DBUILD_TESTS=ON` | 构建测试程序 |
| `-DBUILD_DEMOS=ON` | 构建 Demo 程序 |

## 🌍 跨平台支持

| 平台 | 编译器 | 生成器 | 动态库 | 状态 |
|------|--------|--------|--------|------|
| Windows | MinGW 8+ | "MinGW Makefiles" | .dll | ✅ 完全支持 |
| Linux | GCC 7+ | "Unix Makefiles" | .so | ✅ 完全支持 |
| macOS | Clang 10+ | "Unix Makefiles" | .dylib | ✅ 完全支持 |
| ARM Linux | GCC 10+ | "Unix Makefiles" | .so | ✅ 支持 +SVE |
| ARM macOS | Clang 12+ | "Unix Makefiles" | .dylib | ✅ 支持 |

## 📁 项目结构

```
KADD/
├── include/    # 公共头文件
├── src/        # 源代码
├── tests/      # 测试
├── demo/       # 示例程序
├── tools/      # 训练脚本
├── docs/       # 文档
└── assets/     # 模型和数据
```

## 🔗 参考资料

- [ONNX Runtime](https://github.com/microsoft/onnxruntime)
- [Google Test](https://google.github.io/googletest/)
- [ARM SVE](https://developer.arm.com/documentation/ddi0602/latest/)

## 📄 许可证

Private Repository Source Code License 1.0
Copyright (c) 2026 Huawei Technologies Co., Ltd. All rights reserved.
