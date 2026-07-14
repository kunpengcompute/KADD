# API参考手册

## 概述

KADD SQL注入检测引擎提供以下模块：

| 模块 | 头文件 | 功能 |
|------|--------|------|
| SQLi引擎 | `kadd_sqli_engine.h` | SQL注入检测 |
| DFA引擎 | `kadd_dfa_engine.h` | 字符串分词 |
| Feature引擎 | `kadd_feature_engine.h` | 序列转特征向量 |
| ML引擎 | `kadd_ml_engine.h` | ONNX模型推理 |
| Config | `kadd_config_loader.h` | 配置加载 |

### 处理流程

``` bash
输入字符串 → DFA分词 → 特征提取 → ML推理 → 输出概率
```

## SQLi引擎API

### kadd_sqli_engine_create_from_file

**功能**

从配置文件创建SQL注入检测引擎。

**函数接口**

```c
int kadd_sqli_engine_create_from_file(kadd_sqli_engine_t **engine, const char *config_file);
```

**参数**

* `engine`：引擎句柄指针的指针
* `config_file`：JSON配置文件路径

**返回值**

* 状态码：`KADD_SUCCESS`表示成功，负值表示失败

---

### kadd_sqli_detect

**功能**

检测单个输入是否为SQL注入。

**函数接口**

```c
int kadd_sqli_detect(kadd_sqli_engine_t *engine, const char *input, float *probability);
```

**参数**

* `engine`：引擎句柄
* `input`：输入字符串
* `probability`：输出概率 [0, 1]，值越大越可能是SQL注入

**返回值**

* 状态码：`KADD_SUCCESS`表示成功，负值表示失败

---

### kadd_sqli_detect_batch

**功能**

批量检测多个输入。

**函数接口**

```c
int kadd_sqli_detect_batch(kadd_sqli_engine_t *engine, const char **inputs, int count,
                           float *probabilities, int batch_size);
```

**参数**

* `engine`：引擎句柄
* `inputs`：输入字符串数组
* `count`：输入数量
* `probabilities`：输出概率数组
* `batch_size`：ML推理批次大小（推荐32/64/128）

**返回值**

* 状态码：`KADD_SUCCESS`表示成功，负值表示失败

---

### kadd_sqli_engine_free

**功能**

释放引擎资源。传入NULL安全。

**函数接口**

```c
void kadd_sqli_engine_free(kadd_sqli_engine_t *engine);
```

---

## DFA引擎API

### kadd_dfa_engine_create

**功能**

创建DFA分词引擎。

**函数接口**

```c
int kadd_dfa_engine_create(kadd_dfa_engine_t **engine,
                           const kadd_dfa_table_paths_t *table_paths,
                           size_t max_input_length,
                           size_t max_output_dim);
```

**参数**

* `engine`：引擎句柄指针的指针
* `table_paths`：DFA三张表路径结构体
* `max_input_length`：最大输入字符长度
* `max_output_dim`：最大输出特征维度

**返回值**

* 状态码：`KADD_SUCCESS`表示成功，负值表示失败

---

### kadd_dfa_engine_tokenize

**功能**

对输入字符串进行分词。

**函数接口**

```c
int kadd_dfa_engine_tokenize(const kadd_dfa_engine_t *engine,
                             const char *input,
                             size_t input_length,
                             int *output,
                             int *token_count);
```

**参数**

* `engine`：引擎句柄
* `input`：输入字符串
* `input_length`：输入字符串长度
* `output`：输出token索引数组
* `token_count`：输出实际token数量

**返回值**

* 状态码：`KADD_SUCCESS`表示成功，负值表示失败

---

### kadd_dfa_engine_tokenize_batch

**功能**

批量分词多个字符串。

**函数接口**

```c
int kadd_dfa_engine_tokenize_batch(const kadd_dfa_engine_t *engine,
                                   kadd_dfa_batch_t *batch);
```

**参数**

* `engine`：引擎句柄
* `batch`：批处理上下文结构体，包含：
  * `inputs`：输入字符串数组
  * `lengths`：长度数组
  * `count`：输入数量
  * `outputs`：输出token数组
  * `dims`：每个的token数量
  * `batch_size`：批次大小（0或负数表示默认64）

**返回值**

* 状态码：`KADD_SUCCESS`表示成功，负值表示失败

**注意事项**

* 任何输入失败都会立即返回错误，不会部分成功

---

### kadd_dfa_engine_free

**功能**

释放DFA引擎资源。传入NULL安全。

**函数接口**

```c
void kadd_dfa_engine_free(kadd_dfa_engine_t *engine);
```

---

## ML引擎API

### kadd_model_load

**功能**

加载ONNX模型。

**函数接口**

```c
int kadd_model_load(kadd_model_handle_t **handle, const char *model_path, int feature_dim);
```

**参数**

* `handle`：模型句柄指针的指针
* `model_path`：ONNX模型文件路径
* `feature_dim`：特征维度

**返回值**

* 状态码：`KADD_SUCCESS`表示成功，负值表示失败

---

### kadd_model_predict

**功能**

单样本推理。

**函数接口**

```c
int kadd_model_predict(kadd_model_handle_t *handle, const float *features, float *probability);
```

**参数**

* `handle`：模型句柄
* `features`：特征向量
* `probability`：输出概率 [0, 1]

**返回值**

* 状态码：`KADD_SUCCESS`表示成功，负值表示失败

---

### kadd_model_predict_batch

**功能**

批量推理（分块处理）。

**函数接口**

```c
int kadd_model_predict_batch(kadd_model_handle_t *handle, const float *features,
                             int n, float *probabilities, int batch_size);
```

**参数**

* `handle`：模型句柄
* `features`：特征矩阵 (n x feature_dim)
* `n`：样本数（建议根据实际内存判断，建议10,000,000以下）
* `probabilities`：输出概率数组
* `batch_size`：批次大小（推荐值：32/64/128，范围：1-1024）

**返回值**

* 状态码：`KADD_SUCCESS`表示成功，负值表示失败

**注意事项**

* 如果 batch_size > 1024，将自动限制为 1024
* 任何批次处理失败会立即返回 KADD_ERROR_INFERENCE_FAILED，不会部分成功

---

### kadd_model_free

**功能**

释放模型资源。传入NULL安全。

**函数接口**

```c
void kadd_model_free(kadd_model_handle_t *handle);
```

---

## Feature引擎API

### kadd_seq_to_feature

**功能**

将Token序列转换为固定维度特征向量。

**函数接口**

```c
int kadd_seq_to_feature(const int *seq, int seq_len, int window_size,
                        int *feature, int feature_dim);
```

**参数**

* `seq`：输入序列
* `seq_len`：序列长度
* `window_size`：滑动窗口大小
* `feature`：输出特征向量
* `feature_dim`：输出特征维度

**返回值**

* 状态码：`KADD_SUCCESS`表示成功，负值表示失败

---

### kadd_seq_to_feature_batch

**功能**

批量将token序列转换为特征向量。

**函数接口**

```c
int kadd_seq_to_feature_batch(kadd_feature_batch_t *batch);
```

**参数**

* `batch`：批处理上下文结构体，包含：
  * `seqs`：token序列数组
  * `seq_lens`：序列长度数组
  * `count`：序列数量
  * `features`：输出特征
  * `feature_dim`：特征维度
  * `window_size`：滑动窗口大小
  * `batch_size`：批次大小（0或负数表示默认64）

**返回值**

* 状态码：`KADD_SUCCESS`表示成功，负值表示失败

**注意事项**

* 任何序列失败都会立即返回错误，不会部分成功

---

## 配置加载API

### kadd_config_load

**功能**

从配置文件加载所有引擎配置。

**函数接口**

```c
int kadd_config_load(const char *config_path, kadd_config_t *config);
```

**参数**

* `config_path`：配置文件路径
* `config`：输出配置结构体

**返回值**

* 状态码：`KADD_SUCCESS`表示成功，负值表示失败

---

### kadd_config_free

**功能**

释放配置资源。传入NULL安全。

**函数接口**

```c
void kadd_config_free(kadd_config_t *config);
```

---

## ⚠️ 重要提示

**详细类型定义和完整错误码列表请参考源码头文件**

| 头文件 | 路径 |
|--------|------|
| SQLi引擎 | `/opt/KADD/SQLi/include/kadd_sqli_engine.h` |
| DFA引擎 | `/opt/KADD/SQLi/include/kadd_dfa_engine.h` |
| Feature引擎 | `/opt/KADD/SQLi/include/kadd_feature_engine.h` |
| ML引擎 | `/opt/KADD/SQLi/include/kadd_ml_engine.h` |
| Config | `/opt/KADD/SQLi/include/kadd_config_loader.h` |
| 错误码 | `/opt/KADD/SQLi/include/kadd_errors.h` |
