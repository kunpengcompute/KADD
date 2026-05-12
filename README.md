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

# 启用优化版本构建
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

## 📁 目录结构

```
KADD/
├── include/          # 头文件
├── src/              # 源代码
├── tests/            # 测试代码
├── demo/             # 示例代码
├── docs/             # 文档
├── tools/            # 工具脚本（⚠️ 见下方说明）
├── assets/           # 模型和数据文件
└── third_party/      # 第三方依赖
```

### ⚠️ 关于 tools 目录

`tools/` 目录包含**模型和数据生成工具**，用于：

- 训练和生成检测模型（`tools/train_tokens.py`）
- 生成 DFA 状态转移表

**重要声明：**

> 本目录代码**仅供学习和参考**，**不保证**：
> - 代码质量符合生产标准
> - 无安全漏洞
> - 与主项目代码规范一致
>
> 这些工具是**开发过程中的临时脚本**，可能未经过充分测试和审查。
> 如需用于生产环境，请自行评估安全性并进行必要的加固。

#### 运行机制说明

**正常使用场景：**

KADD 引擎运行时**仅使用** `tools/` 生成的二进制产物：

| 产物文件 | 说明 | 引擎使用位置 |
|---------|------|-------------|
| `assets/model/model_tokens_rf.onnx` | 训练好的 ML 模型 | ML 引擎加载推理 |
| `assets/data/dfa_states.json` | DFA 状态转移表 | DFA 引擎查表匹配 |

**主项目代码不会执行** `tools/` 中的任何 Python 脚本。

#### 何时需要使用 tools 工具

仅在以下情况才需要运行 `tools/` 中的脚本：

| 场景 | 需要运行的脚本 | 生成的产物 |
|------|--------------|-----------|
| 更新检测模型 | `tools/train_tokens.py` | 新的 `.onnx` 模型文件 |
| 修改分词规则 | `tools/generate_dfa.py`* | 新的 DFA 状态表 |
| 自定义训练数据 | `tools/train_tokens.py --dataset your_data.csv` | 定制化模型 |

**示例：重新训练模型**

```bash
# 1. 准备训练数据
cp your_custom_dataset.csv assets/data/

# 2. 运行训练脚本（⚠️ 谨慎执行）
python tools/train_tokens.py \
    --input assets/data/your_custom_dataset.csv \
    --output assets/model/model_tokens_rf.onnx

# 3. 验证新模型
./build/kadd_sqli_demo
```

#### 安全建议

1. **默认不运行**：使用预生成的产物即可满足大部分需求
2. **隔离环境**：如需定制，在独立的沙箱/容器中运行
3. **审查代码**：运行前务必检查脚本内容
4. **验证产物**：对生成的模型和状态表进行充分测试

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
cmake -DENABLE_ARM_SVE=ON -DCMAKE_BUILD_TYPE=Release -DKADD_OPTIMIZE=ON ..
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
