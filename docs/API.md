# API 使用文档

本文档详细介绍 KADD SQL 注入检测引擎的 API 使用方法。

## 目录

- [概述](#概述)
- [DFA 分词引擎 API](#dfa-分词引擎-api)
- [特征提取 API](#特征提取-api)
- [ML 推理 API](#ml-推理-api)
- [SQL 注入检测 API](#sql-注入检测-api)
- [完整示例](#完整示例)

## 概述

KADD 引擎提供 4 个核心 API：

1. **DFA 分词引擎** - 将输入字符串转换为 token 序列
2. **特征提取引擎** - 将 token 序列转换为固定维度的特征向量
3. **ML 推理引擎** - 基于 ONNX Runtime 的模型推理
4. **SQL 注入检测引擎** - 整合以上功能的完整检测 API

## DFA 分词引擎 API

### 头文件

```c
#include "kadd_dfa_engine.h"
```

### 创建引擎

```c
kadd_dfa_engine_t *kadd_dfa_engine_create(const char *table_dir);
```

**参数**:
- `table_dir`: 转移表文件目录路径，NULL 表示当前目录

**返回**:
- 成功: 返回引擎句柄
- 失败: 返回 NULL

**示例**:
```c
kadd_dfa_engine_t *engine = kadd_dfa_engine_create("src/py");
if (!engine) {
    fprintf(stderr, "引擎创建失败\n");
    return -1;
}
```

### 分词

```c
kadd_token_list_t *kadd_dfa_engine_tokenize(
    const kadd_dfa_engine_t *engine,
    const char *input_str
);
```

**参数**:
- `engine`: DFA 引擎句柄
- `input_str`: 输入字符串

**返回**:
- 成功: 返回 token 列表
- 失败: 返回 NULL

**示例**:
```c
const char *input = "SELECT * FROM users WHERE id=1";
kadd_token_list_t *tokens = kadd_dfa_engine_tokenize(engine, input);

if (tokens) {
    size_t count = kadd_token_list_count(tokens);
    for (size_t i = 0; i < count; i++) {
        kadd_token_t *tok = kadd_token_list_get(tokens, i);
        printf("Token(%c, '%s', pos=%zu)\n", tok->type, tok->value, tok->pos);
    }
    kadd_token_list_destroy(tokens);
}
```

### 获取 Token 索引数组

```c
int *kadd_dfa_engine_tokenize_to_indices(
    const kadd_dfa_engine_t *engine,
    const char *input_str,
    size_t *count
);
```

**参数**:
- `engine`: DFA 引擎句柄
- `input_str`: 输入字符串
- `count`: 输出 token 数量

**返回**:
- 成功: 返回 token 索引数组（需调用者 free）
- 失败: 返回 NULL

**示例**:
```c
size_t count;
int *indices = kadd_dfa_engine_tokenize_to_indices(engine, input, &count);
if (indices) {
    // 使用 indices
    for (size_t i = 0; i < count; i++) {
        printf("%d ", indices[i]);
    }
    free(indices);
}
```

### 销毁引擎

```c
void kadd_dfa_engine_destroy(kadd_dfa_engine_t *engine);
```

## 特征提取 API

### 头文件

```c
#include "kadd_feature_engine.h"
```

### 序列到特征转换

```c
int kadd_seq_to_feature(
    const int *seq,
    int seq_len,
    int window_size,
    int *feature,
    int feature_dim
);
```

**参数**:
- `seq`: 输入序列（整数数组）
- `seq_len`: 序列长度
- `window_size`: 滑动窗口大小（推荐 5）
- `feature`: 输出特征向量（需预先分配）
- `feature_dim`: 特征维度（固定 28）

**返回**:
- 成功: 0
- 失败: -1

**示例**:
```c
#define FEATURE_DIM 28

int seq[] = {7, 11, 8, 12, 8, 11, 8};
int feature[FEATURE_DIM] = {0};

int ret = kadd_seq_to_feature(seq, 7, 5, feature, FEATURE_DIM);
if (ret != 0) {
    fprintf(stderr, "特征转换失败\n");
    return -1;
}

// 特征现在存储在 feature 数组中
```

## ML 推理 API

### 头文件

```c
#include "kadd_ml_engine.h"
```

### 加载模型

```c
int kadd_model_load(
    kadd_model_handle_t **handle,
    const char *model_path,
    int feature_dim
);
```

**参数**:
- `handle`: 模型句柄指针的指针
- `model_path`: ONNX 模型文件路径
- `feature_dim`: 特征维度（必须与训练时一致，28）

**返回**:
- 成功: 0
- 失败: -1

**示例**:
```c
kadd_model_handle_t *model = NULL;
int ret = kadd_model_load(&model, "assets/model/model_tokens_rf.onnx", 28);
if (ret != 0) {
    fprintf(stderr, "模型加载失败\n");
    return -1;
}
```

### 单样本推理

```c
int kadd_model_predict(
    kadd_model_handle_t *handle,
    const float *features,
    float *probability
);
```

**参数**:
- `handle`: 模型句柄
- `features`: 特征向量（28 维 float 数组）
- `probability`: 输出概率 [0, 1]

**返回**:
- 成功: 0
- 失败: -1

**示例**:
```c
float features[28] = {/* 特征数据 */};
float probability = 0.0f;

int ret = kadd_model_predict(model, features, &probability);
if (ret == 0) {
    printf("攻击概率: %.2f%%\n", probability * 100);
}
```

### 批量推理

```c
int kadd_model_predict_batch(
    kadd_model_handle_t *handle,
    const float *features,
    int n,
    float *probabilities,
    int batch_size
);
```

**参数**:
- `handle`: 模型句柄
- `features`: 特征矩阵 (n x 28)
- `n`: 样本数量
- `probabilities`: 输出概率数组
- `batch_size`: 批次大小（推荐 64，0 表示自动选择）

**返回**:
- 成功: 返回预测的样本数
- 失败: -1

**示例**:
```c
#define N 100
#define FEATURE_DIM 28

float features[N * FEATURE_DIM] = {/* 特征数据 */};
float probabilities[N] = {0};

int ret = kadd_model_predict_batch(model, features, N, probabilities, 64);
if (ret == N) {
    // 所有样本预测成功
    for (int i = 0; i < N; i++) {
        printf("样本 %d: %.2f%%\n", i, probabilities[i] * 100);
    }
}
```

### 释放模型

```c
void kadd_model_free(kadd_model_handle_t *handle);
```

## SQL 注入检测 API

### 头文件

```c
#include "kadd_sqli_engine.h"
```

### 从配置文件创建引擎

```c
kadd_sqli_engine_t *kadd_sqli_engine_create_from_file(const char *config_path);
```

**配置文件格式** (JSON):
```json
{
  "dfa_table_dir": "src/py",
  "model_path": "assets/model/model_tokens_rf.onnx",
  "feature_dim": 28,
  "window_size": 5,
  "threshold": 0.5
}
```

**示例**:
```c
kadd_sqli_engine_t *engine = kadd_sqli_engine_create_from_file("config.json");
if (!engine) {
    fprintf(stderr, "引擎创建失败\n");
    return -1;
}
```

### 手动配置创建引擎

```c
kadd_sqli_config_t config = {
    .table_dir = "src/py",
    .model_path = "assets/model/model_tokens_rf.onnx",
    .feature_dim = 28,
    .window_size = 5,
    .threshold = 0.5f
};

kadd_sqli_engine_t *engine = kadd_sqli_engine_create(&config);
```

### SQL 注入检测

```c
int kadd_sqli_detect(
    kadd_sqli_engine_t *engine,
    const char *input,
    kadd_sqli_result_t *result
);
```

**参数**:
- `engine`: 检测引擎句柄
- `input`: 输入字符串
- `result`: 输出检测结果

**结果结构**:
```c
typedef struct {
    int is_sqli;        /* 是否为 SQL 注入 (0/1) */
    float probability;  /* 攻击概率 [0, 1] */
    char *tokens;       /* Token 序列（JSON 格式）*/
    char *features;     /* 特征向量（JSON 格式）*/
} kadd_sqli_result_t;
```

**示例**:
```c
kadd_sqli_result_t result;
const char *input = "id=1' OR '1'='1";

int ret = kadd_sqli_detect(engine, input, &result);
if (ret == 0) {
    if (result.is_sqli) {
        printf("SQL 注入检测到! 概率: %.2f%%\n", result.probability * 100);
    } else {
        printf("安全. 概率: %.2f%%\n", result.probability * 100);
    }
    kadd_sqli_result_free(&result);
}
```

### 释放引擎

```c
void kadd_sqli_engine_free(kadd_sqli_engine_t *engine);
```

## 完整示例

### 示例 1: 使用分体 API

```c
#include "kadd_dfa_engine.h"
#include "kadd_feature_engine.h"
#include "kadd_ml_engine.h"
#include <stdio.h>

int main() {
    // 1. 创建 DFA 引擎
    kadd_dfa_engine_t *dfa = kadd_dfa_engine_create("src/py");
    if (!dfa) return -1;

    // 2. 分词
    const char *input = "SELECT * FROM users WHERE id=1";
    size_t token_count;
    int *tokens = kadd_dfa_engine_tokenize_to_indices(dfa, input, &token_count);
    if (!tokens) return -1;

    // 3. 特征提取
    #define FEATURE_DIM 28
    int feature_int[FEATURE_DIM] = {0};
    kadd_seq_to_feature(tokens, token_count, 5, feature_int, FEATURE_DIM);

    // 转换为浮点数
    float features[FEATURE_DIM];
    for (int i = 0; i < FEATURE_DIM; i++) {
        features[i] = (float)feature_int[i];
    }

    // 4. 模型推理
    kadd_model_handle_t *model = NULL;
    kadd_model_load(&model, "assets/model/model_tokens_rf.onnx", FEATURE_DIM);

    float probability = 0.0f;
    kadd_model_predict(model, features, &probability);

    printf("攻击概率: %.2f%%\n", probability * 100);

    // 5. 清理
    free(tokens);
    kadd_dfa_engine_destroy(dfa);
    kadd_model_free(model);

    return 0;
}
```

### 示例 2: 使用整合 API

```c
#include "kadd_sqli_engine.h"
#include <stdio.h>

int main() {
    // 1. 创建引擎（从配置文件）
    kadd_sqli_engine_t *engine = kadd_sqli_engine_create_from_file("config.json");
    if (!engine) return -1;

    // 2. 检测
    const char *input = "id=1' OR '1'='1";
    kadd_sqli_result_t result;

    if (kadd_sqli_detect(engine, input, &result) == 0) {
        printf("输入: %s\n", input);
        printf("检测结果: %s\n", result.is_sqli ? "SQL 注入" : "安全");
        printf("概率: %.2f%%\n", result.probability * 100);

        kadd_sqli_result_free(&result);
    }

    // 3. 清理
    kadd_sqli_engine_free(engine);

    return 0;
}
```

## 错误处理

所有 API 函数都遵循以下错误处理约定：

1. **创建函数**: 失败返回 NULL
2. **处理函数**: 失败返回 -1 或非零值
3. **成功**: 返回 0

**建议的错误处理模式**:
```c
// 创建引擎
kadd_dfa_engine_t *engine = kadd_dfa_engine_create("src/py");
if (!engine) {
    fprintf(stderr, "引擎创建失败\n");
    goto cleanup;
}

// 处理数据
int ret = kadd_seq_to_feature(seq, len, 5, feature, 28);
if (ret != 0) {
    fprintf(stderr, "特征转换失败: %d\n", ret);
    goto cleanup;
}

cleanup:
    if (engine) kadd_dfa_engine_destroy(engine);
```

## 内存管理

1. **引擎句柄**: 使用完毕后必须调用对应的 `*_destroy()` 或 `*_free()` 函数
2. **Token 列表**: 使用 `kadd_token_list_destroy()` 释放
3. **Token 索引数组**: 使用 `free()` 释放
4. **检测结果**: 使用 `kadd_sqli_result_free()` 释放

## 线程安全

- **引擎句柄**: 不是线程安全的，每个线程应创建独立的句柄
- **模型推理**: 同一个模型句柄可以在多个线程中并发使用
- **特征提取**: 纯函数，完全线程安全

## 性能建议

1. **复用引擎句柄**: 创建一次，多次使用
2. **使用批量推理**: 对于多个样本，使用 `kadd_model_predict_batch()`
3. **预分配特征数组**: 避免在循环中重复分配
4. **设置合适的批次大小**: 推荐 64-128

## 更多示例

参考 `demo/` 目录中的完整示例程序：

- `kadd_dfa_demo.c` - DFA 分词示例
- `kadd_feature_demo.c` - 特征提取示例
- `kadd_ml_demo.c` - ML 推理示例
- `kadd_sqli_demo.c` - 完整检测示例
