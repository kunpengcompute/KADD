# KADD API 参考文档

本文档详细说明 KADD SQL 注入检测引擎的 API 使用方法。

## 目录

- [概述](#概述)
- [DFA 引擎](#dfa-引擎)
- [特征引擎](#特征引擎)
- [ML 引擎](#ml-引擎)
- [SQLi 引擎](#sqli-引擎)
- [完整示例](#完整示例)

---

## 概述

KADD 提供模块化的 API，每个引擎可独立使用或组合使用：

```
输入字符串
    ↓
DFA 引擎 (分词)
    ↓
Token 序列
    ↓
特征引擎 (特征提取)
    ↓
特征向量
    ↓
ML 引擎 (推理)
    ↓
概率值
```

### 依赖关系

| 引擎 | 依赖 |
|------|------|
| DFA 引擎 | 无 |
| 特征引擎 | 无 |
| ML 引擎 | ONNX Runtime |
| SQLi 引擎 | DFA + 特征 + ML + cJSON |

### 链接库

```bash
# DFA 引擎
-lkadd_dfa_engine

# 特征引擎
-lkadd_feature_engine

# ML 引擎
-lkadd_ml_engine -lonnxruntime

# SQLi 引擎
-lkadd_sqli_engine -lkadd_dfa_engine -lkadd_feature_engine -lkadd_ml_engine -lonnxruntime -lcjson
```

---

## DFA 引擎

DFA（确定性有限自动机）引擎用于字符串分词，将输入转换为 Token 序列。

### 头文件

```c
#include "kadd_dfa_engine.h"
```

### 数据结构

```c
typedef struct kadd_dfa_engine_t kadd_dfa_engine_t;
```

### API 函数

#### kadd_dfa_engine_create

创建 DFA 引擎实例。

```c
int kadd_dfa_engine_create(kadd_dfa_engine_t **engine,
                           const char *table_path,
                           const char *state_path,
                           size_t max_input_length,
                           size_t max_output_dim);
```

**参数：**
- `engine` - 输出参数，返回创建的引擎指针
- `table_path` - 转移表文件路径
- `state_path` - 状态元数据文件路径
- `max_input_length` - 最大输入字符长度
- `max_output_dim` - 最大输出特征维度

**返回值：**
- `0` - 成功
- 非 `0` - 失败

**示例：**
```c
kadd_dfa_engine_t *engine = NULL;
int ret = kadd_dfa_engine_create(&engine,
                                  "data/transition_table.bin",
                                  "data/states_meta.json",
                                  1024,
                                  128);
if (ret != 0) {
    fprintf(stderr, "Failed to create DFA engine\n");
    return -1;
}
```

#### kadd_dfa_engine_tokenize

对输入字符串进行分词。

```c
int kadd_dfa_engine_tokenize(const kadd_dfa_engine_t *e,
                             const char *input,
                             size_t input_length,
                             int *output,
                             int *dim);
```

**参数：**
- `e` - DFA 引擎
- `input` - 输入字符串
- `input_length` - 输入字符串长度
- `output` - 输出数组（存储 token 索引）
- `dim` - 输出参数，返回实际的 token 数量

**返回值：**
- `0` - 成功
- 非 `0` - 失败

**示例：**
```c
const char *input = "id=1' OR '1'='1";
int tokens[128];
int token_count;

int ret = kadd_dfa_engine_tokenize(engine, input, strlen(input), tokens, &token_count);
if (ret == 0) {
    printf("Token count: %d\n", token_count);
    for (int i = 0; i < token_count; i++) {
        printf("Token[%d] = %d\n", i, tokens[i]);
    }
}
```

#### kadd_dfa_engine_free

释放 DFA 引擎。

```c
void kadd_dfa_engine_free(kadd_dfa_engine_t *e);
```

**示例：**
```c
kadd_dfa_engine_free(engine);
```

---

## 特征引擎

特征引擎将不定长 Token 序列转换为固定维度的特征向量。

### 头文件

```c
#include "kadd_feature_engine.h"
```

### API 函数

#### kadd_seq_to_feature

将序列转换为特征向量。

```c
int kadd_seq_to_feature(const int *seq, int seq_len, int window_size,
                        int *feature, int feature_dim);
```

**参数：**
- `seq` - 输入序列（整数数组）
- `seq_len` - 序列长度
- `window_size` - 滑动窗口大小
- `feature` - 输出特征向量（需预先分配）
- `feature_dim` - 输出特征维度

**返回值：**
- `0` - 成功
- `-1` - 失败

**说明：**
- 使用滑动窗口方法处理不定长序列
- 支持 ARM SVE/SVE2 加速（编译时通过 `-DENABLE_ARM_SVE=ON` 启用）
- 窗口大小通常为 5-7

**SVE 加速性能：**
```
标量实现:     1.0x (基准)
NEON 优化:    2-3x  (ARMv8-A)
SVE 优化:     3-5x  (ARMv9-A, -DENABLE_ARM_SVE=ON)
SVE2 优化:    4-6x  (ARMv9.2-A+, -DENABLE_ARM_SVE2=ON)
```

**启用 SVE 加速：**
```bash
# 编译时启用
cmake -DENABLE_ARM_SVE=ON -H. -Bbuild -G "Unix Makefiles"
cmake --build build

# 或启用 SVE2（更好性能）
cmake -DENABLE_ARM_SVE2=ON -H. -Bbuild -G "Unix Makefiles"
cmake --build build
```

**示例：**
```c
#define FEATURE_DIM 28
#define WINDOW_SIZE 5

int tokens[] = {7, 11, 8, 12, 8, 11, 8};
int token_count = 7;
int feature[FEATURE_DIM] = {0};

int ret = kadd_seq_to_feature(tokens, token_count, WINDOW_SIZE, feature, FEATURE_DIM);
if (ret == 0) {
    printf("Feature vector extracted\n");
    for (int i = 0; i < FEATURE_DIM; i++) {
        printf("feature[%d] = %d\n", i, feature[i]);
    }
}
```

---

## ML 引擎

ML 引擎基于 ONNX Runtime 进行模型推理。

### 头文件

```c
#include "kadd_ml_engine.h"
```

### 数据结构

```c
typedef struct kadd_model_handle_t kadd_model_handle_t;
```

### API 函数

#### kadd_model_load

加载 ONNX 模型。

```c
int kadd_model_load(kadd_model_handle_t **handle, 
                    const char *model_path, 
                    int feature_dim);
```

**参数：**
- `handle` - 模型句柄指针的指针
- `model_path` - 模型文件路径 (.onnx)
- `feature_dim` - 特征维度

**返回值：**
- `0` - 成功
- `-1` - 失败

**示例：**
```c
kadd_model_handle_t *model = NULL;
int ret = kadd_model_load(&model, "model/model_tokens_rf.onnx", 28);
if (ret != 0) {
    fprintf(stderr, "Failed to load model\n");
    return -1;
}
```

#### kadd_model_predict

单样本推理。

```c
int kadd_model_predict(kadd_model_handle_t *handle, 
                      const float *features, 
                      float *probability);
```

**参数：**
- `handle` - 模型句柄
- `features` - 特征向量 (feature_dim 维 float 数组)
- `probability` - 输出概率 [0, 1]

**返回值：**
- `0` - 成功
- `-1` - 失败

**示例：**
```c
float features[28] = { /* 特征值 */ };
float probability;

int ret = kadd_model_predict(model, features, &probability);
if (ret == 0) {
    printf("Probability: %.4f\n", probability);
    if (probability >= 0.5f) {
        printf("SQL Injection detected!\n");
    }
}
```

#### kadd_model_predict_batch

批量推理（分块处理）。

```c
int kadd_model_predict_batch(kadd_model_handle_t *handle, 
                             const float *features, 
                             int n, 
                             float *probabilities, 
                             int batch_size);
```

**参数：**
- `handle` - 模型句柄
- `features` - 特征矩阵 (n x feature_dim)
- `n` - 样本数
- `probabilities` - 输出概率数组
- `batch_size` - 批次大小（推荐值：32/64/128，0 表示自动选择）

**返回值：**
- 成功预测的样本数
- `-1` - 失败

**说明：**
- 使用分块批量推理，避免内存峰值过高
- 较大的 batch_size 性能更好，但需要更多内存
- 如果 batch_size <= 0，将使用默认值 64

**示例：**
```c
#define SAMPLE_COUNT 100
#define FEATURE_DIM 28

float features[SAMPLE_COUNT * FEATURE_DIM];
float probabilities[SAMPLE_COUNT];

// 填充特征数据...

int ret = kadd_model_predict_batch(model, features, SAMPLE_COUNT, probabilities, 64);
if (ret > 0) {
    printf("Predicted %d samples\n", ret);
}
```

#### kadd_model_free

释放模型。

```c
void kadd_model_free(kadd_model_handle_t *handle);
```

**示例：**
```c
kadd_model_free(model);
```

---

## SQLi 引擎

SQLi 引擎整合 DFA、特征和 ML 引擎，提供完整的 SQL 注入检测功能。

### 头文件

```c
#include "kadd_sqli_engine.h"
```

### 错误代码

```c
typedef enum {
    KADD_SQLI_OK = 0,                   /**< 成功 */
    KADD_SQLI_ERROR_MEMORY = -1,         /**< 内存分配失败 */
    KADD_SQLI_ERROR_INVALID_INPUT = -2,  /**< 输入参数无效 */
    KADD_SQLI_ERROR_DFA = -3,            /**< DFA引擎错误 */
    KADD_SQLI_ERROR_FEATURE = -4,        /**< 特征提取错误 */
    KADD_SQLI_ERROR_ML = -5,             /**< ML推理错误 */
    KADD_SQLI_ERROR_CONFIG = -6,         /**< 配置错误 */
} kadd_sqli_error_code_t;
```

### 数据结构

```c
typedef struct kadd_sqli_engine_t kadd_sqli_engine_t;
```

### API 函数

#### kadd_sqli_engine_create_from_file

从配置文件创建 SQL 注入检测引擎。

```c
int kadd_sqli_engine_create_from_file(kadd_sqli_engine_t **engine, 
                                      const char *config_file);
```

**参数：**
- `engine` - 引擎句柄指针的指针
- `config_file` - JSON 配置文件路径

**返回值：**
- `KADD_SQLI_OK` - 成功
- 其他值 - 失败

**配置文件格式：**
```json
{
  "kadd_dfa_engine": {
    "table_path": "data/transition_table.bin",
    "state_path": "data/states_meta.json",
    "max_input_length": 1024,
    "max_output_dim": 128
  },
  "kadd_ml_engine": {
    "model_path": "model/model_tokens_rf.onnx",
    "feature_dim": 28
  },
  "kadd_feature_engine": {
    "window_size": 5
  },
  "kadd_sqli_engine": {
    "threshold": 0.5
  }
}
```

**示例：**
```c
kadd_sqli_engine_t *engine = NULL;
int ret = kadd_sqli_engine_create_from_file(&engine, "config.json");
if (ret != KADD_SQLI_OK) {
    fprintf(stderr, "Failed to create engine: %d\n", ret);
    return -1;
}
```

#### kadd_sqli_detect

检测单个输入。

```c
int kadd_sqli_detect(kadd_sqli_engine_t *engine, 
                     const char *input, 
                     float *probability);
```

**参数：**
- `engine` - 引擎句柄
- `input` - 输入字符串
- `probability` - 输出概率值指针 [0, 1]

**返回值：**
- `KADD_SQLI_OK` - 成功
- 其他值 - 失败

**说明：**
- 概率值 >= 阈值（默认 0.5）时判定为 SQL 注入
- 阈值可通过配置文件调整

**示例：**
```c
const char *input = "id=1' OR '1'='1";
float probability;

int ret = kadd_sqli_detect(engine, input, &probability);
if (ret == KADD_SQLI_OK) {
    printf("Probability: %.4f\n", probability);
    if (probability >= 0.5f) {
        printf("SQL Injection detected!\n");
    }
}
```

#### kadd_sqli_detect_batch

批量检测。

```c
int kadd_sqli_detect_batch(kadd_sqli_engine_t *engine, 
                           const char **inputs, 
                           int count, 
                           float *probabilities, 
                           int batch_size);
```

**参数：**
- `engine` - 引擎句柄
- `inputs` - 输入字符串数组
- `count` - 输入数量
- `probabilities` - 输出概率数组（需预先分配）
- `batch_size` - ML 推理批次大小

**返回值：**
- 成功检测的数量
- 负值 - 失败

**说明：**
- 使用 ML 引擎的批量推理能力
- 即使某个输入失败也会继续处理其他输入
- 失败的输入对应概率值设为 -1.0

**示例：**
```c
const char *inputs[] = {
    "id=1",
    "id=1' OR '1'='1",
    "search=test"
};
int count = 3;
float probabilities[3];

int detected = kadd_sqli_detect_batch(engine, inputs, count, probabilities, 64);
printf("Successfully detected: %d/%d\n", detected, count);

for (int i = 0; i < count; i++) {
    if (probabilities[i] >= 0) {
        printf("Input %d: %.4f (%s)\n", i, probabilities[i],
               probabilities[i] >= 0.5f ? "SQLi" : "Normal");
    }
}
```

#### kadd_sqli_engine_free

释放引擎。

```c
void kadd_sqli_engine_free(kadd_sqli_engine_t *engine);
```

**示例：**
```c
kadd_sqli_engine_free(engine);
```

---

## 完整示例

### 示例 1: 使用 SQLi 引擎

```c
#include "kadd_sqli_engine.h"
#include <stdio.h>

int main() {
    // 创建引擎
    kadd_sqli_engine_t *engine = NULL;
    if (kadd_sqli_engine_create_from_file(&engine, "config.json") != KADD_SQLI_OK) {
        fprintf(stderr, "Failed to create engine\n");
        return 1;
    }

    // 检测 SQL 注入
    const char *test_cases[] = {
        "id=1",
        "id=1' OR '1'='1",
        "search=test",
        "admin'--"
    };

    for (int i = 0; i < 4; i++) {
        float probability;
        if (kadd_sqli_detect(engine, test_cases[i], &probability) == KADD_SQLI_OK) {
            printf("[%s] %.4f - %s\n",
                   test_cases[i],
                   probability,
                   probability >= 0.5f ? "SQL Injection" : "Normal");
        }
    }

    // 释放资源
    kadd_sqli_engine_free(engine);
    return 0;
}
```

### 示例 2: 分步使用各引擎

```c
#include "kadd_dfa_engine.h"
#include "kadd_feature_engine.h"
#include "kadd_ml_engine.h"
#include <stdio.h>

#define FEATURE_DIM 28
#define WINDOW_SIZE 5

int main() {
    // 1. 创建 DFA 引擎
    kadd_dfa_engine_t *dfa = NULL;
    kadd_dfa_engine_create(&dfa, "data/transition_table.bin", 
                            "data/states_meta.json", 1024, 128);

    // 2. 创建 ML 模型
    kadd_model_handle_t *model = NULL;
    kadd_model_load(&model, "model/model_tokens_rf.onnx", FEATURE_DIM);

    // 3. 处理输入
    const char *input = "id=1' OR '1'='1";
    int tokens[128];
    int token_count;

    // 分词
    kadd_dfa_engine_tokenize(dfa, input, strlen(input), tokens, &token_count);

    // 特征提取
    int feature[FEATURE_DIM] = {0};
    kadd_seq_to_feature(tokens, token_count, WINDOW_SIZE, feature, FEATURE_DIM);

    // 转换为 float
    float features[FEATURE_DIM];
    for (int i = 0; i < FEATURE_DIM; i++) {
        features[i] = (float)feature[i];
    }

    // 推理
    float probability;
    kadd_model_predict(model, features, &probability);

    printf("Input: %s\n", input);
    printf("Probability: %.4f\n", probability);
    printf("Result: %s\n", probability >= 0.5f ? "SQL Injection" : "Normal");

    // 释放资源
    kadd_dfa_engine_free(dfa);
    kadd_model_free(model);

    return 0;
}
```

### 示例 3: 批量处理

```c
#include "kadd_sqli_engine.h"
#include <stdio.h>

int main() {
    kadd_sqli_engine_t *engine = NULL;
    kadd_sqli_engine_create_from_file(&engine, "config.json");

    // 批量检测
    const char *inputs[] = {
        "id=1",
        "id=1' OR '1'='1",
        "name=John",
        "admin' OR '1'='1",
        "search=sql"
    };
    int count = 5;
    float probabilities[5];

    int detected = kadd_sqli_detect_batch(engine, inputs, count, probabilities, 64);
    printf("Detected: %d/%d\n", detected, count);

    // 统计结果
    int sqli_count = 0;
    for (int i = 0; i < count; i++) {
        if (probabilities[i] >= 0.5f) {
            sqli_count++;
        }
    }

    printf("SQL Injection count: %d\n", sqli_count);

    kadd_sqli_engine_free(engine);
    return 0;
}
```

---

## 编译示例

### 编译 SQLi 引擎示例

```bash
gcc -o sqli_example sqli_example.c \
    -I../include \
    -L../build/lib \
    -lkadd_sqli_engine \
    -lonnxruntime \
    -lcjson
```

### 运行

```bash
export LD_LIBRARY_PATH=../build/lib:$LD_LIBRARY_PATH
./sqli_example
```

---

## 性能建议

1. **批量处理** - 使用 `*_batch` 函数提高吞吐量
2. **线程安全** - 每个线程使用独立的引擎实例
3. **内存管理** - 及时释放不再使用的引擎
4. **模型复用** - 同一模型可用于多个检测任务

---

## 错误处理

所有 API 函数都返回状态码，建议检查返回值：

```c
int ret = kadd_sqli_detect(engine, input, &probability);
if (ret != KADD_SQLI_OK) {
    switch (ret) {
        case KADD_SQLI_ERROR_MEMORY:
            fprintf(stderr, "Memory allocation failed\n");
            break;
        case KADD_SQLI_ERROR_INVALID_INPUT:
            fprintf(stderr, "Invalid input parameter\n");
            break;
        case KADD_SQLI_ERROR_DFA:
            fprintf(stderr, "DFA engine error\n");
            break;
        case KADD_SQLI_ERROR_FEATURE:
            fprintf(stderr, "Feature extraction error\n");
            break;
        case KADD_SQLI_ERROR_ML:
            fprintf(stderr, "ML inference error\n");
            break;
        default:
            fprintf(stderr, "Unknown error: %d\n", ret);
    }
}
```

---

## 调试

启用 ML 引擎调试输出：

```c
// 在包含头文件之前定义
#define KADD_DEBUG_ML
#include "kadd_ml_engine.h"
```

或在编译时定义：

```bash
gcc -DKADD_DEBUG_ML -o example example.c -lkadd_ml_engine
```
