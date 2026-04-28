# KADD SQL 注入检测引擎

基于 ONNX Runtime 的轻量级 SQL 注入检测引擎，专为 ARM Linux 平台优化。

## ✨ 特性

- 🔍 **准确检测** - 99.77% 准确率，100% 精确率
- 🚀 **高性能** - 单样本推理 ~100,000 ops/sec
- 📦 **模块化设计** - DFA、特征、ML 引擎独立可用
- 🎯 **ARM 优化** - 支持 NEON 和 ARMv9-A SVE 向量化
- 🔧 **易于集成** - 简洁的 C API，仅依赖 ONNX Runtime

## 🚀 快速开始

### 前置要求

- **系统**: ARM Linux (aarch64/armv7l)
- **编译器**: GCC 10+ 
- **CMake**: 3.16+
- **依赖**: ONNX Runtime（必需）、cJSON（可选）、Google Test（可选）

### 依赖安装

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install cmake build-essential

# 安装 ONNX Runtime
sudo apt-get install libonnxruntime-dev

# 可选：安装 cJSON 和 Google Test
sudo apt-get install libcjson-dev libgtest-dev
```

> 详见 [第三方库安装指南](docs/DEPENDENCIES.md)

### 构建与测试

```bash
# 标准构建
mkdir build && cd build
cmake -G "Unix Makefiles" ..
cmake --build .

# 启用优化和 SVE 加速
cmake -DKADD_OPTIMIZE=ON -DENABLE_ARM_SVE2=ON -DCMAKE_BUILD_TYPE=Release ..
cmake --build .

# 运行测试
ctest --verbose

# 代码覆盖率报告（需要重新编译）
rm -rf build
mkdir build && cd build
cmake -DENABLE_CODE_COVERAGE=ON -G "Unix Makefiles" ..
make
ctest --verbose
make coverage
```

## 📦 模块说明

| 模块 | 库文件 | 说明 | 依赖 |
|------|--------|------|------|
| DFA 引擎 | `libkadd_dfa_engine.so` | 字符串分词 | 无 |
| 特征引擎 | `libkadd_feature_engine.so` | 序列转特征向量 | 无 |
| ML 引擎 | `libkadd_ml_engine.so` | ONNX 模型推理 | ONNX Runtime |
| DFA Pro 引擎 | `libkadd_dfa_engine_pro.so` | 增强型 DFA 分词 | cJSON |
| SQLi 引擎 | `libkadd_sqli_engine.so` | 完整检测 API | ONNX Runtime + cJSON |

## 💡 快速示例

```c
#include "kadd_sqli_engine.h"

int main() {
    // 创建检测引擎
    kadd_sqli_engine_t *engine = kadd_sqli_engine_create_from_file("config.json");
    
    // 检测 SQL 注入
    kadd_sqli_result_t result;
    const char *input = "id=1' OR '1'='1";
    
    if (kadd_sqli_detect(engine, input, &result) == 0) {
        if (result.is_sqli) {
            printf("SQL 注入! 概率: %.2f%%\n", result.probability * 100);
        }
        kadd_sqli_result_free(&result);
    }
    
    kadd_sqli_engine_free(engine);
    return 0;
}
```

## 📚 文档

- [第三方库安装指南](docs/DEPENDENCIES.md) - ONNX Runtime、cJSON、Google Test 安装
- [KADD API 参考](docs/API.md) - 完整的 API 使用手册
- [CMake 构建指南](docs/CMAKE.md) - 构建系统和覆盖率测试说明
- [ARM 故障排除](demo/README_ARM_TROUBLESHOOTING.md) - ARM 特定问题

## 🏗️ 架构

```
输入字符串 → DFA 分词 → Token 序列 → 特征提取 → 特征向量 → ML 推理 → 概率
```

## 📊 性能指标

| 指标 | 数值 |
|------|------|
| 准确率 | 99.77% |
| 精确率 | 100% |
| 召回率 | 99.77% |
| 单样本推理 | ~100,000 ops/sec |

## 🔧 编译选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `-DKADD_OPTIMIZE=ON` | OFF | 启用 -O3 优化 |
| `-DENABLE_ARM_SVE=ON` | OFF | 启用 ARM SVE 加速（ARMv9-A） |
| `-DENABLE_ARM_SVE2=ON` | OFF | 启用 ARM SVE2 加速（ARMv9.2-A+） |
| `-DENABLE_CODE_COVERAGE=ON` | OFF | 启用代码覆盖率 |
| `-DBUILD_TESTS=ON` | ON | 构建测试程序 |
| `-DUSE_SYSTEM_LIBS=OFF` | ON | 强制使用 third_party |

### Assets 目录自动检测

构建系统会自动检测 assets 目录位置：
1. **优先**：项目根目录的 `assets/`（与 `CMakeLists.txt` 同级）
2. **其次**：构建目录的 `assets/`（与 `build/` 同级）
3. **默认**：相对路径 `assets`

```bash
# 标准目录结构（推荐）
KADD/
├── assets/          ← 模型文件在这里
│   └── model/
│       └── model_tokens_rf.onnx
├── build/           ← 构建目录
└── CMakeLists.txt

# 替代结构
KADD/
├── build/
│   └── assets/      ← 模型文件也可以在这里
│       └── model/
│           └── model_tokens_rf.onnx
└── CMakeLists.txt
```

## 🌍 平台支持

| 平台 | 架构 | 状态 | 优化 |
|------|------|------|------|
| ARM Linux | aarch64 | ✅ 完全支持 | NEON |
| ARM Linux | armv7l | ✅ 支持 | - |
| ARMv9-A Linux | aarch64 | ✅ 完全支持 | SVE |
| ARMv9.2-A+ Linux | aarch64 | ✅ 完全支持 | SVE2 |

### ARM SVE 加速

**SVE（Scalable Vector Extension）** 是 ARMv9-A 架构的向量指令集，可显著提升性能。

| 配置 | 性能提升 | 架构要求 |
|------|----------|----------|
| 标量实现 | 1.0x（基准） | 任意 |
| NEON 优化 | 2-3x | ARMv8-A |
| SVE 优化 | 3-5x | ARMv9-A |
| SVE2 优化 | 4-6x | ARMv9.2-A+ |

**启用方法：**
```bash
# SVE
cmake -DENABLE_ARM_SVE=ON ..

# SVE2（推荐）
cmake -DENABLE_ARM_SVE2=ON ..
```

## 📄 许可证

Private Repository Source Code License 1.0
Copyright (c) 2026 Huawei Technologies Co., Ltd. All rights reserved.
