# KADD SQL 注入检测引擎

基于 ONNX Runtime 的轻量级 SQL 注入检测引擎，支持多平台部署。

## ✨ 特性

- 📦 **模块化设计** - DFA、特征、ML 引擎独立可用
- 🎯 **高性能** - 支持 ARM NEON/SVE/SVE2 向量化加速
- 🔧 **易于集成** - 简洁的 C API，仅依赖 ONNX Runtime
- 🌍 **跨平台** - 支持 Linux、Windows (MinGW/MSVC)

## 🚀 快速开始

### 前置要求

- **编译器**: GCC 10+ / Clang / MSVC
- **CMake**: 3.16+
- **依赖**: ONNX Runtime（必需）、cJSON（可选）、Google Test（可选）

### 依赖安装

```bash
# openEuler
sudo yum update
sudo yum install cmake gcc-c++ make

# 安装 ONNX Runtime
sudo yum install onnxruntime-devel

# 可选：安装 cJSON 和 Google Test
sudo yum install cjson-devel gtest-devel
```

> 详见 [第三方库安装指南](docs/DEPENDENCIES.md)

### 构建与测试

```bash
# 标准构建
mkdir build && cd build
cmake ..
cmake --build .

# 启用优化
cmake -DKADD_OPTIMIZE=ON -DCMAKE_BUILD_TYPE=Release ..
cmake --build .

# 运行测试
ctest --verbose
```

## 📦 模块说明

| 模块 | 说明 | 依赖 |
|------|------|------|
| DFA 引擎 | 字符串分词 | 无 |
| 特征引擎 | 序列转特征向量 | 无 |
| ML 引擎 | ONNX 模型推理 | ONNX Runtime |
| SQLi 引擎 | 完整检测 API | DFA + 特征 + ML + cJSON |

## 💡 快速示例

```c
#include "kadd_sqli_engine.h"

int main() {
    // 创建检测引擎
    kadd_sqli_engine_t *engine = NULL;
    if (kadd_sqli_engine_create_from_file(&engine, "config.json") != KADD_SQLI_OK) {
        return 1;
    }
    
    // 检测 SQL 注入
    const char *input = "id=1' OR '1'='1";
    float probability = 0.0f;
    
    if (kadd_sqli_detect(engine, input, &probability) == KADD_SQLI_OK) {
        if (probability >= 0.5f) {
            printf("SQL 注入! 概率: %.2f%%\n", probability * 100);
        } else {
            printf("安全输入\n");
        }
    }
    
    kadd_sqli_engine_free(engine);
    return 0;
}
```

## 📚 文档

- [API 参考手册](docs/API.md) - 完整的 API 使用手册
- [架构设计文档](docs/DESIGN.md) - 系统架构和模块设计
- [CMake 构建指南](docs/CMAKE.md) - 构建系统说明
- [第三方库安装指南](docs/DEPENDENCIES.md) - 依赖安装说明
- [演示程序说明](demo/README.md) - Demo 使用指南

## 🏗️ 架构

```
输入字符串 → DFA 分词 → Token 序列 → 特征提取 → 特征向量 → ML 推理 → 概率
```

## 🌍 平台支持

| 平台 | 架构 | 状态 |
|------|------|------|
| Linux | x86_64 | ✅ 支持 |
| Linux | aarch64 | ✅ 支持 (NEON) |
| Linux | ARMv9-A | ✅ 支持 (SVE/SVE2) |

## 🔧 编译选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `-DKADD_OPTIMIZE=ON` | OFF | 启用 -O3 优化 |
| `-DENABLE_ARM_SVE=ON` | OFF | 启用 ARM SVE（自动检测 SVE2） |
| `-DENABLE_CODE_COVERAGE=ON` | OFF | 启用代码覆盖率 |
| `-DBUILD_TESTS=ON` | ON | 构建测试程序 |
| `-DUSE_SYSTEM_LIBS=ON` | ON | 优先使用系统库 |

### ARM 加速

```bash
# 启用 SVE（自动检测 SVE2 支持）
cmake -DENABLE_ARM_SVE=ON -DCMAKE_BUILD_TYPE=Release -DKADD_OPTIMIZE=ON
cmake --build .

# SVE2 支持的系统会自动启用 SVE2
# 非 SVE2 系统会回退到 SVE
```

### Assets 目录自动检测

构建系统会自动检测 assets 目录位置：
1. **优先**：项目根目录的 `assets/`
2. **其次**：构建目录的 `assets/`
3. **默认**：相对路径 `assets`

```bash
# 标准目录结构（推荐）
KADD/
├── assets/          ← 模型文件在这里
│   └── model/
│       └── model_tokens_rf.onnx
├── build/           ← 构建目录
└── CMakeLists.txt
```

## 📄 许可证

Private Repository Source Code License 1.0
Copyright (c) 2026 Huawei Technologies Co., Ltd. All rights reserved.
