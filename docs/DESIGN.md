# ONNX 推理引擎设计文档

## 项目概述

### 项目简介

本项目是一个高性能的 ONNX 模型推理框架，专门用于处理序列数据的特征提取和模型预测。项目采用模块化设计，支持跨平台编译和部署，提供简洁的 C 语言 API 接口。

### 核心功能

- **文本解析**：基于 KADD DFA 的文本分词，将原始文本转换为 token 序列
- **序列特征转换**：将不定长序列转换为固定维度特征向量
- **ONNX 模型推理**：基于 ONNX Runtime 的高性能模型推理
- **SQL 注入检测**：整合 DFA + 特征 + ML 的完整检测流程
- **批量推理支持**：支持单样本和批量样本的高效推理
- **ARM SVE 加速**：支持 ARM SVE 指令集的向量化优化
- **跨平台支持**：支持 Windows (MinGW) 和 Linux 平台

### 技术栈

| 技术 | 版本 | 用途 |
|------|------|------|
| C 标准 | C11 | 核心实现语言 |
| CMake | ≥3.16 | 模块化构建系统 |
| Google Test | 1.12+ | 单元测试框架 |
| ONNX Runtime | 1.16+ | 模型推理引擎 |
| ARM SVE | ARMv9+ | SIMD 向量化加速 |

---

## 目录结构

```
c/
├── cmake/                      # CMake 模块
├── include/                    # 公共头文件
├── src/                        # 核心源代码
│   └── python/                 # Python 训练脚本代码
├── demo/                       # 示例程序
├── tests/                      # Google Test 测试
├── docs/                       # 文档目录
└── assets/                     # 资源文件
    └── model/                  # 模型文件
```

**生成目录**（编译后生成，不提交到版本控制）：
- `bin/` - 编译输出
- `build/` - CMake 构建目录

**用户目录**（需要用户自行准备）：
- `lib/` - ONNX Runtime 库文件（详见 [INSTALL.md](../INSTALL.md)）
- `third_party/` - 可选的第三方头文件（开发调试用）

---

## 系统架构

### 整体架构图

```
┌─────────────────────────────────────────────────────────────────────────┐
│                              用户应用层                                   │
│  ┌──────────────┐  ┌──────────────────┐  ┌──────────────────┐          │
│  │   dfa_demo   │  │ kadd_feature_    │  │ kadd_ml_demo     │          │
│  │              │  │     demo         │  │                  │          │
│  └──────┬───────┘  └────────┬─────────┘  └────────┬─────────┘          │
└─────────┼───────────────────┼─────────────────────┼──────────────────────┘
          │                   │                     │
┌─────────┼───────────────────┼─────────────────────┼──────────────────────┐
│         │      公共 API 层  │                     │                      │
│  ┌──────┴──────┐  ┌────────┴────────┐  ┌────────┴────────┐             │
│  │ kadd_dfa_    │  │ kadd_feature_   │  │ kadd_ml_         │             │
│  │ engine.h     │  │ engine.h        │  │ engine.h         │             │
│  └──────┬──────┘  └────────┬────────┘  └────────┬────────┘             │
└─────────┼───────────────────┼─────────────────────┼──────────────────────┘
          │                   │                     │
┌─────────┼───────────────────┼─────────────────────┼──────────────────────┐
│         │      核心库层 (STATIC)                  │                      │
│  ┌──────┴──────┐  ┌────────┴────────┐  ┌────────┴────────┐            │
│  │ kadd_dfa_    │  │ kadd_feature_   │  │ kadd_ml_          │            │
│  │ engine.c     │  │ engine.c        │  │ engine.c        │            │
│  │ (无三方依赖)  │  │  (无三方依赖)   │  │  (依赖 ONNX)    │            │
│  └──────────────┘  └─────────────────┘  └────────┬────────┘            │
└───────────────────────────────────────────────────┼──────────────────────┘
                                                          │
┌─────────────────────────────────────────────────────┼──────────────────────┐
│                                                     │   第三方依赖层      │
│                                              ┌──────┴──────┐               │
│                                              │ ONNX Runtime│               │
│                                              │  (系统库)    │               │
│                                              │ /usr/local/  │               │
│                                              └─────────────┘               │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 数据流图

```
原始文本
   │
   ▼
┌──────────────────┐
│ kadd_dfa_engine  │  DFA 解析器
│                  │
└────────┬─────────┘
         │ Token 序列 [7, 11, 8, 12, ...]
         ▼
┌──────────────────┐
│ kadd_feature_    │  特征转换器
│ engine           │
└────────┬─────────┘
         │ 特征向量 [0, 152, 0, ...]
         ▼
┌──────────────────┐
│ kadd_ml_engine   │  ML 推理引擎
│                  │
└────────┬─────────┘
         │ 预测概率 [0.95]
         ▼
     结果输出
```

### 模块依赖关系

```
config (无依赖)
    ↓
kadd_dfa_engine (无依赖)
    ↓
kadd_feature_engine (无依赖)
    ↓
kadd_ml_engine (依赖 ONNX Runtime)
    ↓
kadd_sqli_engine (整合引擎)
    ↓
Demo 程序 (依赖以上所有库)
```

### 依赖管理原则

**系统库优先**：项目使用系统包管理器安装的 ONNX Runtime，而非包含在仓库中

| 依赖 | 安装方式 | 位置 | 版本控制 |
|------|----------|------|----------|
| ONNX Runtime | 系统包管理器 | `/usr/local/lib` | ❌ 不提交 |
| ONNX 头文件 | 系统包管理器 | `/usr/local/include` | ❌ 不提交 |
| Google Test | CMake 自动下载 | `build/_deps/googletest` | ❌ 不提交 |

**不提交的目录**（见 `.gitignore`）：
- `lib/` - 第三方库文件
- `third_party/` - 第三方头文件（可选）
- `build/` - CMake 构建目录
- `bin/` - 编译输出

### 设计原则

1. **模块化设计**：每个模块职责单一，接口清晰
2. **依赖隔离**：核心模块独立，推理模块可选依赖 ONNX
3. **单向数据流**：文本 → Token → 特征 → 概率
4. **不透明指针**：隐藏内部实现细节，提供稳定 ABI
5. **现代 CMake**：使用 target-based 构建系统，自动传递依赖

---

## 核心模块设计

### 1. DFA 文本解析模块 (kadd_dfa_engine)

#### 模块概述

KADD DFA（确定有限状态自动机）解析模块负责将原始文本转换为 Token 序列，为后续的特征提取和模型推理做准备。本模块从 Python 版本转换而来，实现了基于转移表的高效 SQL 分词器。

#### 功能特性

- **输入**：原始文本字符串
- **输出**：Token 序列（包含类型、值、位置）
- **算法**：确定性有限状态自动机，基于预编译转移表
- **支持**：
  - SQL 关键字识别（SELECT, FROM, WHERE, etc.）
  - 数字、字符串、标识符
  - 运算符（单字符和双字符）
  - 注释（行注释和块注释）
  - 特殊字符串（二进制、十六进制、转义、引用字符串）
  - 变量和函数名
  - SQL 注入模式检测

#### 二进制表文件格式

转移表文件 `transition_table.bin` 格式：

```
┌─────────────────────────────────────────────┐
│ uint32: 状态数量 (num_states)                │
├─────────────────────────────────────────────┤
│ 状态名列表:                                  │
│   [uint8: 长度 + UTF-8字符串] × num_states   │
├─────────────────────────────────────────────┤
│ 转移表:                                      │
│   [uint16 × 256] × num_states                │
│   - 0xFFFE = 特殊指令                         │
│     后跟 [uint8: 长度 + 字符串]               │
│   - 其他值 = 目标状态 ID                      │
└─────────────────────────────────────────────┘
```

#### Token 类型

| 类型字符 | 名称 | 说明 |
|---------|------|------|
| `k` | KEYWORD | SQL 关键字 |
| `n` | BAREWORD | 标识符 |
| `1` | NUMBER | 数字 |
| `s` | STRING | 字符串 |
| `o` | OPERATOR | 运算符 |
| `&` | LOGIC_OP | 逻辑运算符 |
| `v` | VARIABLE | 变量 |
| `f` | FUNCTION | 函数 |
| `c` | COMMENT | 注释 |
| `t` | SQLTYPE | SQL 类型 |
| `U` | UNION | UNION 操作 |
| `E` | EXPRESSION | 表达式 |

#### 接口定义

```c
/**
 * @brief Token 结构
 */
typedef struct {
    char type;      /* Token 类型字符 */
    char *value;    /* Token 值 (动态分配) */
    size_t pos;     /* 在输入中的起始位置 */
} token_t;

/**
 * @brief Token 列表
 */
typedef struct {
    token_t *tokens;    /* Token 数组 */
    size_t count;       /* 当前数量 */
    size_t capacity;    /* 容量 */
} token_list_t;

/**
 * @brief DFA 引擎句柄
 */
typedef struct {
    char table_dir[256];              /* 表文件目录 */
    kadd_trans_entry_t **transitions; /* 转移表 [state][char] */
    char **state_names;               /* 状态名数组 */
    kadd_state_meta_t *meta;          /* 状态元数据 */
    uint32_t num_states;              /* 状态数量 */
    int id_word;                      /* WORD 状态 ID */
} kadd_dfa_engine_t;

/**
 * @brief 创建 DFA 引擎
 * @param table_dir 表文件目录，NULL 表示当前目录
 * @return 新引擎，失败返回 NULL
 */
kadd_dfa_engine_t *kadd_dfa_engine_create(const char *table_dir);

/**
 * @brief 销毁 DFA 引擎
 */
void kadd_dfa_engine_destroy(kadd_dfa_engine_t *engine);

/**
 * @brief 对输入字符串进行分词
 * @return token 列表，失败返回 NULL
 */
kadd_token_list_t *kadd_dfa_engine_tokenize(const kadd_dfa_engine_t *engine, const char *input_str);

/**
 * @brief 对输入字符串进行分词，返回类型索引数组
 */
int *kadd_dfa_engine_tokenize_to_indices(const kadd_dfa_engine_t *engine, const char *input_str, size_t *count);
```

---

### 2. 特征转换模块 (kadd_feature_engine)

#### 模块概述

KADD 特征转换模块负责将不定长的整数序列转换为固定维度的特征向量，基于滑动窗口算法实现。

#### 功能特性

- **输入**：不定长整数序列
- **输出**：固定维度特征向量（默认 28 维）
- **算法**：滑动窗口距离计算
- **优化**：
  - 循环展开优化（4路展开）
  - ARM SVE 向量化加速（可选）
  - SIMD 友好的内存访问模式

#### 核心算法

**滑动窗口距离计算：**

对于序列 `seq` 和窗口大小 `window_size`，计算每个位置的距离：

```
result[i] = Σ weight[d] * |seq[i] - seq[i+d]|
其中 weight[d] = window_size - d
```

#### 接口定义

```c
/**
 * @brief 将不定长序列转换为固定维度特征向量
 *
 * @param seq 输入序列（整数数组）
 * @param seq_len 序列长度
 * @param window_size 滑动窗口大小
 * @param feature 输出特征向量（需预先分配）
 * @param feature_dim 输出特征维度
 *
 * @return 0 成功, -1 失败
 */
int seq_to_feature(const int *seq, int seq_len, int window_size,
                   int *feature, int feature_dim);
```
---

### 3. ML 推理引擎模块 (kadd_ml_engine)

#### 模块概述

KADD ML 推理引擎模块封装了 ONNX Runtime 的核心功能，提供简洁的模型推理接口。

#### 功能特性

- **模型加载**：支持加载 ONNX 格式模型
- **单样本推理**：单个样本的快速推理
- **批量推理**：批量样本的高效处理
- **自动检测输出**：自动识别模型输出节点名称
- **资源管理**：自动管理模型生命周期

#### 接口定义

```c
/**
 * @brief 模型句柄（不透明指针）
 */
typedef struct kadd_model_handle_t kadd_model_handle_t;

/**
 * @brief 加载 ONNX 模型
 *
 * @param handle 模型句柄指针的指针
 * @param model_path 模型文件路径 (.onnx)
 * @param feature_dim 特征维度
 *
 * @return 0 成功, -1 失败
 */
int kadd_model_load(kadd_model_handle_t **handle, const char *model_path, int feature_dim);

/**
 * @brief 单样本推理
 *
 * @param handle 模型句柄
 * @param features 特征向量 (feature_dim 维 float 数组)
 * @param probability 输出概率 [0, 1]
 *
 * @return 0 成功, -1 失败
 */
int kadd_model_predict(kadd_model_handle_t *handle, const float *features, float *probability);

/**
 * @brief 批量推理
 *
 * @param handle 模型句柄
 * @param features 特征矩阵 (n x feature_dim)
 * @param n 样本数
 * @param probabilities 输出概率数组
 *
 * @return 成功预测的样本数，-1 失败
 */
int kadd_model_predict_batch(kadd_model_handle_t *handle, const float *features, int n, float *probabilities);

/**
 * @brief 释放模型
 *
 * @param handle 模型句柄
 */
void kadd_model_free(kadd_model_handle_t *handle);
```
---

## 完整处理流程

### 文本到推理的完整流程

```c
#include "dfa_engine.h"
#include "seq_to_feature.h"
#include "inference_engine.h"

#define FEATURE_DIM 28
#define MAX_TOKENS 1000

int main() {
    // 1. 文本解析
    dfa_handle_t *dfa = NULL;
    dfa_create(&dfa, "vocab.txt");

    const char *text = "The quick brown fox jumps";
    int tokens[MAX_TOKENS];
    int token_count = dfa_parse(dfa, text, tokens, MAX_TOKENS);

    // 2. 特征转换
    int feature_int[FEATURE_DIM] = {0};
    seq_to_feature(tokens, token_count, 5, feature_int, FEATURE_DIM);

    // 3. 转换为浮点数
    float features[FEATURE_DIM];
    for (int i = 0; i < FEATURE_DIM; i++) {
        features[i] = (float)feature_int[i];
    }

    // 4. 模型推理
    model_handle_t *model = NULL;
    model_load(&model, "assets/model/model_tokens_rf.onnx", FEATURE_DIM);

    float probability = 0.0f;
    model_predict(model, features, &probability);

    printf("预测概率: %.2f%%\n", probability * 100);

    // 5. 清理
    dfa_free(dfa);
    model_free(model);

    return 0;
}
```

---

## 第三方依赖管理

### 依赖策略

项目采用**系统库优先**策略，不将第三方库包含在源代码仓库中。

### 依赖清单

| 依赖 | 类型 | 安装方式 | 必需 |
|------|------|----------|------|
| **ONNX Runtime** | 系统库 | apt/brew/vcpkg | ✅ 推理功能需要 |
| **Google Test** | 自动下载 | CMake FetchContent | ✅ 测试需要 |
| **libgomp** | 系统库 | apt (默认安装) | ⚠️ Linux 需要 |

### ONNX Runtime 安装

详细安装指南请参阅 [INSTALL.md](../INSTALL.md)

**快速安装**：
```bash
# Linux (x86_64)
wget https://github.com/microsoft/onnxruntime/releases/download/v1.16.0/onnxruntime-linux-x64-1.16.0.tgz
tar -xzf onnxruntime-linux-x64-1.16.0.tgz
sudo cp onnxruntime-linux-x64-1.16.0/lib/* /usr/local/lib/
sudo cp -r onnxruntime-linux-x64-1.16.0/include/* /usr/local/include/
sudo ldconfig

# Linux (ARM64)
wget https://github.com/microsoft/onnxruntime/releases/download/v1.16.0/onnxruntime-linux-aarch64-1.16.0.tgz
# ... 同上

# macOS
brew install onnxruntime

# Windows (vcpkg)
vcpkg install onnxruntime:x64-windows
```

### 目录使用说明

**`lib/` 目录**（用户创建）：
```bash
# 开发者手动放置 ONNX Runtime 库文件的位置
# Linux: libonnxruntime.so, libonnxruntime.so.1
# Windows: onnxruntime.dll
```

**`third_party/` 目录**（可选）：
- 仅用于开发调试
- 不应提交到版本控制
- 如果系统库安装正确，此目录非必需

### CMake 查找顺序

CMake 按以下顺序查找 ONNX Runtime：

1. **环境变量** `ONNXRuntime_DIR`
2. **系统路径** `/usr/local/lib`, `/usr/lib`
3. **pkg-config** (如果可用)
4. **vcpkg** (Windows)
5. **自定义路径** 通过 `-DONNXRuntime_DIR=...`

### 验证安装

```bash
# 检查 ONNX Runtime
ldconfig -p | grep onnxruntime    # Linux
otool -L /usr/local/lib/libonnxruntime.dylib  # macOS

# 配置项目
cmake .. -DUSE_ONNX=ON

# 应该看到：
# -- Found ONNX Runtime: /usr/local/lib/libonnxruntime.so
```

---

## 构建系统设计

### CMake 模块化架构

#### 模块职责

| 模块 | 文件 | 职责 |
|------|------|------|
| 根配置 | CMakeLists.txt | 项目选项、子目录协调 |
| 编译选项 | cmake/CompilerOptions.cmake | 警告、优化、ARM SVE |
| 库查找 | cmake/FindONNXRuntime.cmake | 系统库标准查找 |
| 库定义 | cmake/Libraries.cmake | 所有静态库目标 |
| 测试 | cmake/Tests.cmake | Google Test 集成 |

#### 依赖可见性

**PUBLIC - 传递给使用者**
```cmake
target_include_directories(seq_to_feature PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
)
```

**PRIVATE - 仅库内部使用**
```cmake
target_include_directories(inference_engine PRIVATE
    ${ONNX_RUNTIME_INCLUDE_DIRS}
)
```

---

## 测试设计

### 测试框架

采用 **Google Test** 框架，支持参数化测试、测试夹具和死亡测试。

### 测试分层策略

```
端到端测试 (e2e)
    ↓ 依赖
集成测试 (integration)
    ↓ 依赖
单元测试 (unit)
```

#### 测试类型说明

- **单元测试**：测试单个函数/模块的功能正确性，使用 Mock 隔离依赖
- **集成测试**：测试多个模块间的接口和数据流
- **端到端测试**：从文本输入到推理结果的完整流程
- **异常测试**：错误处理、边界条件、安全验证

### 单元测试设计

#### 1. DFA 引擎单元测试 (DfaEngineTest)

##### 测试用例分类

**功能测试**
- `ParsePlainText` - 解析纯英文文本
- `ParseChineseText` - 解析中文文本
- `ParseMixedText` - 解析中英混合文本
- `ParseNumbers` - 解析包含数字的文本
- `ParseSymbols` - 解析包含特殊符号的文本
- `ParseWithCustomVocab` - 使用自定义词汇表
- `ParseLongText` - 解析超长文本（>10k 字符）
- `ParseEmptyText` - 解析空字符串
- `ParseWhitespace` - 解析仅含空白字符

**边界条件测试**
- `ParseSingleCharacter` - 单字符解析
- `ParseMaxTokens` - 达到最大 Token 数量
- `ParseExceedMaxTokens` - 超过最大 Token 数量（应截断）
- `ParseWithNULLHandle` - 传入 NULL 句柄（死亡测试）
- `ParseWithNULLTokens` - 传入 NULL 输出数组（死亡测试）

**状态机测试**
- `StateTransitionStartToWord` - START → WORD 转换
- `StateTransitionStartToNumber` - START → NUMBER 转换
- `StateTransitionStartToSymbol` - START → SYMBOL 转换
- `StateTransitionWordToWord` - WORD → WORD 持续
- `StateTransitionWordToSpace` - WORD → 空白状态转换
- `StateTransitionNumberToWord` - NUMBER → WORD 转换
- `AllStateTransitions` - 完整状态转换矩阵验证

**异常处理测试**
- `HandleUnknownToken` - 处理未知词汇（映射到 UNK）
- `HandleInvalidUTF8` - 处理无效 UTF-8 编码
- `HandleSpecialCharacters` - 处理特殊控制字符（\n, \t, \r）
- `HandleZeroWidthChars` - 处理零宽字符



---

#### 2. 序列特征转换单元测试 (SeqToFeatureTest)

##### 测试用例分类

**功能测试**
- `TransformSimpleSequence` - 简单序列转换（5 个元素）
- `TransformLongSequence` - 长序列转换（100 个元素）
- `TransformSingleElement` - 单元素序列
- `TransformWithWindow5` - 窗口大小 5
- `TransformWithWindow10` - 窗口大小 10
- `TransformWithWindow20` - 窗口大小 20

**滑动窗口算法测试**
- `SlidingWindowDistance` - 距离计算正确性
- `WeightedDistance` - 加权距离计算
- `WindowOverBoundary` - 窗口超出序列边界
- `MultipleWindowSizes` - 不同窗口大小参数化测试
- `FeatureVectorValues` - 特征向量值域验证

**边界条件测试**
- `TransformEmptySequence` - 空序列（seq_len=0）
- `TransformNegativeLen` - 负长度（死亡测试）
- `TransformNULLSequence` - NULL 输入序列（死亡测试）
- `TransformNULLFeature` - NULL 输出特征数组（死亡测试）
- `TransformZeroWindow` - 窗口大小 0
- `TransformNegativeWindow` - 负窗口大小（死亡测试）
- `TransformInsufficientFeatureDim` - 特征维度不足

**数学正确性测试**
- `DistanceSymmetry` - 距离对称性|a-b| = |b-a|
- `DistanceTriangleInequality` - 距离三角不等式
- `MaxDistanceValue` - 最大距离值验证
- `MinDistanceValue` - 最小距离值验证（0）
- `WeightSumCorrectness` - 权重总和正确性

**SVE 加速测试**
- `SVEEnabled` - SVE 启用时的加速比
- `SVEConsistency` - SVE 与标量结果一致性



---

#### 3. 推理引擎单元测试 (InferenceEngineTest)

##### 测试用例分类

**模型生命周期测试**
- `LoadValidModel` - 加载有效 ONNX 模型
- `LoadInvalidPath` - 加载无效路径（应失败）
- `LoadCorruptedModel` - 加载损坏的模型文件
- `LoadWrongFeatureDim` - 特征维度不匹配（应失败）
- `LoadNULLHandle` -  NULL 句柄（死亡测试）
- `FreeNULLHandle` - 释放 NULL 句柄
- `FreeMultipleTimes` - 多次释放同一句柄（应安全）
- `LoadThenFree` - 加载后立即释放

**推理功能测试**
- `PredictSingleSample` - 单样本推理
- `PredictWithHighProb` - 高概率（>0.9）样本
- `PredictWithLowProb` - 低概率（<0.1）样本
- `PredictWithMediumProb` - 中等概率（0.3-0.7）样本
- `PredictProbabilityRange` - 概率值域验证 [0, 1]

**批量推理测试**
- `PredictBatchSize1` - 批量大小 1（等价单样本）
- `PredictBatchSize10` - 批量大小 10
- `PredictBatchSize100` - 批量大小 100
- `PredictBatchSize1000` - 批量大小 1000
- `BatchProbabilityOrdering` - 批量结果顺序正确性
- `BatchWithNULLFeatures` - NULL 特征矩阵（死亡测试）
- `BatchWithNULLProbabilities` - NULL 输出数组（死亡测试）
- `BatchWithZeroCount` - 零样本批量（应成功返回 0）

**错误处理测试**
- `PredictBeforeLoad` - 加载前推理（应失败）
- `PredictAfterFree` - 释放后推理（应失败）
- `PredictWithModel_NULL` - NULL 句柄推理（死亡测试）
- `PredictWithNULLFeatures` - NULL 特征向量（死亡测试）
- `PredictWithNULLProbability` - NULL 输出概率（死亡测试）
- `PredictWithMismatchedDim` - 特征维度不匹配

**内存管理测试**
- `MemoryLeakLoadFreeCycle` - 加载/释放循环无内存泄漏
- `MemoryLeakManyPredicts` - 大量推理无内存泄漏
- `HandleValidAfterFree` - 句柄释放后失效



---

### 集成测试设计

#### 端到端测试套件 (EndToEndTest)

| 测试用例 | 描述 | 预期结果 |
|---------|------|----------|
| `CompletePipeline` | DFA → 特征转换 → 推理完整流程 | 成功输出概率 |
| `PipelineWithValidInput` | 有效文本输入 | 概率在 [0, 1] |
| `PipelineWithInvalidInput` | 无效文本（垃圾字符） | 优雅降级 |
| `PipelineWithLongInput` | 超长文本输入 | 正确截断处理 |
| `PipelineReusability` | 多次复用同一模型 | 结果一致 |

#### 模块集成测试套件 (IntegrationTest)

| 测试用例 | 模块组合 | 验证点 |
|---------|---------|-------|
| `DfaToFeatureIntegration` | dfa_engine + seq_to_feature | Token 序列→特征向量转换 |
| `FeatureToInferenceIntegration` | seq_to_feature + inference_engine | 特征向量→概率转换 |
| `ThreeModuleIntegration` | 三个模块串联 | 数据流完整性 |

### 测试数据策略

#### 数据集划分

| 数据集 | 大小 | 用途 | 维度 |
|--------|------|------|------|
| `unit_test_data` | ~100 条 | 单元测试 | 小规模 |
| `integration_test_data` | ~1k 条 | 集成测试 | 中规模 |
| `boundary_test_data` | 特殊构造 | 边界测试 | 极端值 |

#### 数据生成规则

**DFA 测试数据**
- 纯英文：随机 ASCII 英文字母组合
- 中文：常见中文字符覆盖
- 数字：不同格式（整数、浮点）
- 符号：ASCII 符号全集
- 混合：按比例随机混合

**特征转换测试数据**
- 长度范围：0, 1, 5, 10, 50, 100, 1000, 10000
- 数值范围：[-1000, 1000]
- 特殊值：0, MAX_INT, MIN_INT

**推理测试数据**
- 有效特征向量：从已知好样本生成
- 无效特征：随机、NaN、Inf、全零、超大值

### 测试覆盖目标

| 模块 | 函数覆盖率 | 分支覆盖率 | 行覆盖率 | 条件覆盖率 |
|------|-----------|-----------|----------|-----------|
| dfa_engine | 100% | 95% | 100% | 90% |
| seq_to_feature | 100% | 95% | 100% | 90% |
| inference_engine | 100% | 95% | 100% | 90% |



### 测试执行策略

#### 快速测试
```bash
# 运行所有单元测试（~5 分钟）
ctest -L unit --verbose
```

#### 完整测试
```bash
# 运行所有测试（~30 分钟）
ctest --verbose
```


## 部署指南

### 编译步骤

```bash
# 1. 安装 ONNX Runtime（系统库）
sudo apt-get install libonnxruntime-dev  # Linux
brew install onnxruntime                  # macOS
vcpkg install onnxruntime:x64-windows    # Windows

# 2. 配置项目
mkdir build && cd build
cmake -DUSE_ONNX=ON -DBUILD_TESTS=ON ..

# 3. 编译
cmake --build .

# 4. 运行测试
ctest --verbose

# 5. 运行演示
cd ../bin
./dfa_demo
./seq_to_feature_demo
./inference_demo
```

### 编译选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `-DUSE_ONNX=ON` | OFF | 启用 ONNX Runtime 支持 |
| `-DENABLE_SVE=ON` | OFF | 启用 ARM SVE 加速 |
| `-DBUILD_TESTS=ON` | OFF | 构建测试程序 |
| `-DBUILD_DEMOS=ON` | ON | 构建 Demo 程序 |

---

## 常见问题

### Q1: DFA 解析器如何处理未知词？

未知词映射到 `UNK_TOKEN` (ID=0)，可在词汇表中定义默认处理。

### Q2: 如何扩展 DFA 支持更多语言？

修改状态转换函数 `transition()`，添加新语言的字符范围判断。

### Q3: 特征维度如何确定？

根据模型训练时的输入维度确定，通过 `model_load()` 的 `feature_dim` 参数传入。

### Q4: 如何启用 ARM SVE 加速？

```bash
cmake -DENABLE_SVE=ON -DUSE_ONNX=ON ..
```

需要 ARMv9+ 处理器和 GCC 10+ 编译器。

### Q5: 批量推理比单次推理快吗？

批量推理主要减少函数调用开销，性能提升有限（~10%）。
