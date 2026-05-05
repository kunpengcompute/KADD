# KADD API 参考手册

本文档详细描述 KADD SQL 注入检测引擎的 C API 接口。

---

## 目录

- [1. DFA 引擎 API](#1-dfa-引擎-api)
- [2. 特征引擎 API](#2-特征引擎-api)
- [3. ML 引擎 API](#3-ml-引擎-api)
- [4. SQLi 引擎 API](#4-sqli-引擎-api)
- [5. 错误代码参考](#5-错误代码参考)
- [6. Token 类型映射](#6-token-类型映射)

---

## 1. DFA 引擎 API

DFA 引擎提供基于确定性有限自动机的 SQL 分词功能，将输入字符串转换为 token 序列。

### 1.1 类型定义

```c
typedef struct kadd_dfa_engine_t kadd_dfa_engine_t;
```

不透明的 DFA 引擎句柄，用于后续所有 DFA 操作。

### 1.2 kadd_dfa_engine_create

创建 DFA 引擎实例。

```c
int kadd_dfa_engine_create(kadd_dfa_engine_t **engine,
                           const char *table_path,
                           const char *state_path,
                           size_t max_input_length,
                           size_t max_output_dim);
```

**参数：**

| 参数 | 类型 | 说明 |
|------|------|------|
| engine | `kadd_dfa_engine_t**` | 输出参数，返回创建的引擎指针 |
| table_path | `const char*` | 转移表文件路径 (.bin) |
| state_path | `const char*` | 状态元数据文件路径 (.json) |
| max_input_length | `size_t` | 最大输入字符长度 |
| max_output_dim | `size_t` | 最大输出特征维度 |

**返回值：**

| 值 | 说明 |
|----|------|
| `0` | 成功 |
| 非 `0` | 失败 |

---

### 1.3 kadd_dfa_engine_tokenize

对输入字符串进行分词，输出 token 索引序列。

```c
int kadd_dfa_engine_tokenize(const kadd_dfa_engine_t *e,
                             const char *input,
                             size_t input_length,
                             int *output,
                             int *dim);
```

**参数：**

| 参数 | 类型 | 说明 |
|------|------|------|
| e | `const kadd_dfa_engine_t*` | DFA 引擎句柄 |
| input | `const char*` | 输入字符串 |
| input_length | `size_t` | 输入字符串长度 |
| output | `int*` | 输出数组（存储 token 索引） |
| dim | `int*` | 输出参数，返回实际的 token 数量 |

### 1.4 kadd_dfa_engine_free

释放 DFA 引擎资源。

```c
void kadd_dfa_engine_free(kadd_dfa_engine_t *e);
```

---

## 2. 特征引擎 API

### 2.1 kadd_seq_to_feature

将不定长序列转换为固定维度特征向量。

```c
int kadd_seq_to_feature(const int *seq, int seq_len, int window_size,
                        int *feature, int feature_dim);
```

**参数：**

| 参数 | 类型 | 说明 |
|------|------|------|
| seq | `const int*` | 输入序列（整数数组） |
| seq_len | `int` | 序列长度 |
| window_size | `int` | 滑动窗口大小（通常为 5） |
| feature | `int*` | 输出特征向量（需预先分配） |
| feature_dim | `int` | 输出特征维度（通常为 28） |

**性能优化：**
- ARM SVE 加速（编译时需启用 `-march=armv9-a+sve`）
- 循环展开优化

---

## 3. ML 引擎 API

### 3.1 类型定义

```c
typedef struct kadd_model_handle_t kadd_model_handle_t;
```

### 3.2 kadd_model_load

加载 ONNX 模型。

```c
int kadd_model_load(kadd_model_handle_t **handle, const char *model_path, int feature_dim);
```

### 3.3 kadd_model_predict

单样本推理。

```c
int kadd_model_predict(kadd_model_handle_t *handle, const float *features, float *probability);
```

**输出说明：**
- 返回的是 SQL 注入概率值 [0, 1]
- 调用方根据业务需求判断阈值（通常为 0.5）

### 3.4 kadd_model_predict_batch

批量推理（分块处理）。

```c
int kadd_model_predict_batch(kadd_model_handle_t *handle, const float *features, int n, float *probabilities, int batch_size);
```

### 3.5 kadd_model_free

释放模型资源。

```c
void kadd_model_free(kadd_model_handle_t *handle);
```

---

## 4. SQLi 引擎 API

### 4.1 类型定义

```c
typedef struct kadd_sqli_engine_t kadd_sqli_engine_t;
```

### 4.2 错误代码

```c
typedef enum {
    KADD_SQLI_OK = 0,
    KADD_SQLI_ERROR_MEMORY = -1,
    KADD_SQLI_ERROR_INVALID_INPUT = -2,
    KADD_SQLI_ERROR_DFA = -3,
    KADD_SQLI_ERROR_FEATURE = -4,
    KADD_SQLI_ERROR_ML = -5,
    KADD_SQLI_ERROR_CONFIG = -6,
} kadd_sqli_error_code_t;
```

### 4.3 kadd_sqli_engine_create_from_file

从配置文件创建引擎。

```c
int kadd_sqli_engine_create_from_file(kadd_sqli_engine_t **engine, const char *config_file);
```

**配置文件格式：**

```json
{
  "kadd_dfa_engine": {
    "table_path": "path/to/transition_table.bin",
    "state_path": "path/to/states_meta.json",
    "max_input_length": 8192,
    "max_output_dim": 8192
  },
  "kadd_ml_engine": {
    "model_path": "path/to/model.onnx",
    "feature_dim": 28,
    "window_size": 5
  }
}
```

### 4.4 kadd_sqli_detect

检测单个输入。

```c
int kadd_sqli_detect(kadd_sqli_engine_t *engine, const char *input, float *probability);
```

### 4.5 kadd_sqli_detect_batch

批量检测。

```c
int kadd_sqli_detect_batch(kadd_sqli_engine_t *engine, const char **inputs, int count, float *probabilities, int batch_size);
```

### 4.6 kadd_sqli_engine_free

释放引擎。

```c
void kadd_sqli_engine_free(kadd_sqli_engine_t *engine);
```

---

## 5. 错误代码参考

### 5.1 SQLi 引擎

| 代码 | 名称 |
|------|------|
| 0 | KADD_SQLI_OK |
| -1 | KADD_SQLI_ERROR_MEMORY |
| -2 | KADD_SQLI_ERROR_INVALID_INPUT |
| -3 | KADD_SQLI_ERROR_DFA |
| -4 | KADD_SQLI_ERROR_FEATURE |
| -5 | KADD_SQLI_ERROR_ML |
| -6 | KADD_SQLI_ERROR_CONFIG |

### 5.2 DFA 引擎

| 代码 | 名称 |
|------|------|
| 0 | KADD_DFA_OK |
| -1 | KADD_DFA_ERROR_MEMORY |
| -2 | KADD_DFA_ERROR_INVALID_INPUT |
| -3 | KADD_DFA_ERROR_FILE_NOT_FOUND |
| -4 | KADD_DFA_ERROR_PARSE |
| -5 | KADD_DFA_ERROR_BUFFER_OVERFLOW |
| -6 | KADD_DFA_ERROR_INVALID_LENGTH |

---

## 6. Token 类型映射

| 索引 | 类型名称 | 说明 |
|------|----------|------|
| 0 | TYPE_NONE | 无类型 |
| 1 | TYPE_KEYWORD | SQL 关键字 |
| 2 | TYPE_UNION | UNION |
| 3 | TYPE_GROUP | 分组 |
| 4 | TYPE_EXPRESSION | 表达式 |
| 5 | TYPE_SQLTYPE | 数据类型 |
| 6 | TYPE_FUNCTION | 函数 |
| 7 | TYPE_BAREWORD | 标识符 |
| 8 | TYPE_NUMBER | 数字 |
| 9 | TYPE_VARIABLE | 变量 |
| 10 | TYPE_STRING | 字符串 |
| 11 | TYPE_OPERATOR | 操作符 |
| 12 | TYPE_LOGIC_OPERATOR | 逻辑操作符 |
| 13 | TYPE_COMMENT | 注释 |
| 14 | TYPE_COLLATE | COLLATE |
| 15 | TYPE_LEFTPARENS | 左括号 |
| 16 | TYPE_RIGHTPARENS | 右括号 |
| 17 | TYPE_LEFTBRACE | 左花括号 |
| 18 | TYPE_RIGHTBRACE | 右花括号 |
| 19 | TYPE_DOT | 点号 |
| 20 | TYPE_COMMA | 逗号 |
| 21 | TYPE_COLON | 冒号 |
| 22 | TYPE_SEMICOLON | 分号 |
| 23 | TYPE_STATEMENT | 语句 |
| 24 | TYPE_UNKNOWN | 未知 |
| 25 | TYPE_EVIL | 恶意模式 |
| 26 | TYPE_FINGERPRINT | 指纹 |
| 27 | TYPE_BACKSLASH | 反斜杠 |

---

*文档版本: 1.0.0*
