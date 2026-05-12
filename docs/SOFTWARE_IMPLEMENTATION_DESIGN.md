# KADD 软件实现设计

## 1. 文档目标

本文档面向参与 KADD SQL 注入检测引擎开发、维护、集成和测试的同学，说明当前项目的实现边界、模块职责、核心数据结构、关键算法、构建产物、测试验收标准和后续开发规则。

本文档以当前源码为准，主要参考：

- `include/`：对外 C API 和错误码定义
- `src/`：DFA、Feature、ML、SQLi、Config Loader 实现
- `tools/dfa_compiler/`：DFA profile 到二进制表的编译链路
- `tools/train_tokens.py`：Token 特征模型训练和 ONNX 导出链路
- `tests/`：Google Test 单元、集成、性能与一致性测试
- `config.json`：当前默认运行配置

## 2. 系统范围

KADD 当前实现是一个轻量级 SQL 注入检测库，核心功能是把输入字符串转换为 token 序列，再转换为固定维度特征向量，最后通过 ONNX 模型输出 SQL 注入概率。

### 2.1 核心能力

| 能力 | 说明 |
| --- | --- |
| DFA 分词 | 使用预编译三张 DFA 表把输入字节流转换为 token id 序列 |
| 特征提取 | 将变长 token 序列转换为固定维度整数特征向量 |
| ML 推理 | 通过 ONNX Runtime 执行随机森林模型推理 |
| SQLi 检测 | 封装 DFA + Feature + ML，提供单样本和批量检测 API |
| DFA 编译 | 将 `profile_sqli_fixed.txt` 编译成运行期加载的二进制表 |
| 模型训练 | 从数据集生成特征，训练随机森林并导出 `model_tokens_rf.onnx` |

### 2.2 非目标

当前库不负责：

- URL 解码、Unicode 规范化、大小写标准化等输入预处理。
- 根据概率做最终业务拦截。阈值由调用方决定。
- 解释模型命中原因或输出可读规则链。
- 在一个 `kadd_sqli_engine_t` 实例上提供无锁并发单样本检测。

## 3. 总体架构

### 3.1 运行链路

```text
input string
    |
    v
kadd_sqli_detect / kadd_sqli_detect_batch
    |
    v
kadd_dfa_engine_tokenize
    |
    v
token ids: int[]
    |
    v
kadd_seq_to_feature
    |
    v
feature vector: int[feature_dim]
    |
    v
float conversion
    |
    v
kadd_model_predict / kadd_model_predict_batch
    |
    v
probability: float in [0, 1]
```

### 3.2 模块依赖

```text
kadd_sqli_engine
    |-- kadd_dfa_engine
    |-- kadd_feature_engine
    |-- kadd_ml_engine
    |       `-- ONNX Runtime
    `-- cJSON

kadd_config_loader
    |-- cJSON
    |-- public config structs

tools/dfa_compiler/dfa_compiler.py
    `-- profile_sqli_fixed.txt -> assets/data/*.bin

tools/train_tokens.py
    |-- scikit-learn
    |-- skl2onnx
    `-- assets/model/model_tokens_rf.onnx
```

### 3.3 核心约束

| 约束 | 当前值/策略 | 说明 |
| --- | --- | --- |
| 默认输入长度 | `config.json` 为 300 | DFA 超过 `max_input_length` 返回错误 |
| 默认 token 输出上限 | `config.json` 为 300 | 需与 Feature 的 `MAX_SEQ_LEN` 对齐 |
| Feature 维度 | 28 | token id 范围必须 `< feature_dim` |
| Feature 序列上限 | `MAX_SEQ_LEN=300` | 当前源码中固定 |
| Feature 维度上限 | `MAX_FEATURE_DIM=300` | 当前源码中固定 |
| 默认窗口 | 5 | 计算距离 1 到 `window_size - 1` |
| ML 输入名 | `float_input` | 在代码中硬编码 |
| ML 输出 | `[batch, 2]` | 取第二列作为正类概率 |
| 批大小上限 | 1024 | DFA/Feature/SQLi/ML 多处有上限保护 |

## 4. 目录与文件职责

| 路径 | 职责 |
| --- | --- |
| `include/kadd_errors.h` | 统一错误码；约定 0 成功、正数部分成功、负数失败 |
| `include/kadd_dfa_engine.h` | DFA 对外 API、三表路径结构、批处理结构 |
| `src/kadd_dfa_engine.c` | 加载 DFA 三张表，执行单样本/批量分词 |
| `include/kadd_feature_engine.h` | Feature 对外 API、批处理结构和上限宏 |
| `src/kadd_feature_engine.c` | 滑动窗口特征提取，含循环展开与 ARM SVE 分支 |
| `include/kadd_ml_engine.h` | ONNX 模型加载、单样本推理、批量推理 API |
| `src/kadd_ml_engine.c` | ONNX Runtime session 管理、推理和 warmup |
| `include/kadd_sqli_engine.h` | SQLi 检测统一 API |
| `src/kadd_sqli_engine.c` | 配置解析、子引擎初始化、检测流程编排 |
| `include/kadd_config_loader.h` | 通用配置结构 |
| `src/kadd_config_loader.c` | 从 `config.json` 读取 DFA、ML、SQLi、Feature 配置 |
| `tools/dfa_compiler/profile_sqli_fixed.txt` | DFA 源 profile |
| `tools/dfa_compiler/dfa_compiler.py` | DFA profile 编译器 |
| `tools/train_tokens.py` | 训练随机森林并导出 ONNX |
| `assets/data/*.bin` | DFA 运行期三表 |
| `assets/model/model_tokens_rf.onnx` | ML 推理模型 |
| `demo/` | 各模块交互式 demo |
| `tests/` | gtest 测试 |

## 5. 构建设计

### 5.1 CMake 选项

| 选项 | 默认 | 说明 |
| --- | --- | --- |
| `BUILD_TESTS` | ON | 构建 gtest 测试 |
| `USE_SYSTEM_LIBS` | ON | 优先查找系统 cJSON、GTest |
| `ENABLE_CODE_COVERAGE` | OFF | 追加 coverage 编译和链接选项 |
| `KADD_OPTIMIZE` | OFF | Release 下追加 `-O3 -DNDEBUG` |
| `ENABLE_ARM_SVE` | OFF | ARM64 下启用 SVE，自动检测 SVE2 |

### 5.2 依赖查找

ONNX Runtime 是必需依赖：

1. 优先使用环境变量 `ONNXRUNTIME_HOME`。
2. 未指定时查找 `third_party/onnxruntime`、`/usr/local`、`/usr`。
3. 未找到时 CMake 直接失败。

cJSON 是 SQLi 引擎和 Config Loader 的依赖：

1. 优先查找系统库。
2. 找不到时尝试 `third_party/cJSON`。
3. 仍找不到时跳过 JSON 相关库目标。

Google Test 仅在 `BUILD_TESTS=ON` 时需要：

1. 优先查找系统 GTest。
2. 找不到时尝试 `third_party/gtest`。
3. 仍找不到时关闭测试构建。

### 5.3 构建产物

动态库：

| 目标 | 源文件 | 依赖 |
| --- | --- | --- |
| `kadd_dfa_engine` | `src/kadd_dfa_engine.c` | 无外部运行库 |
| `kadd_feature_engine` | `src/kadd_feature_engine.c` | 无外部运行库 |
| `kadd_ml_engine` | `src/kadd_ml_engine.c` | ONNX Runtime |
| `kadd_sqli_engine` | `src/kadd_sqli_engine.c` | DFA + Feature + ML + cJSON |
| `kadd_config_loader` | `src/kadd_config_loader.c` | cJSON |

静态测试库：

- `kadd_other_engines`：用于 gtest 链接，包含可用的核心引擎源码。

可执行文件：

- `demo/kadd_dfa_demo`
- `demo/kadd_feature_demo`
- `demo/kadd_ml_demo`
- `demo/kadd_sqli_demo`
- `tests/dfa_engine_test`
- `tests/feature_engine_test`
- `tests/ml_engine_test`
- `tests/sqli_engine_test`

## 6. 配置设计

当前 `config.json`：

```json
{
  "kadd_dfa_engine": {
    "transition_table_path": "assets/data/transition_table.bin",
    "output_table_path": "assets/data/output_table.bin",
    "eod_table_path": "assets/data/eod_table.bin",
    "max_input_length": 300,
    "max_output_dim": 300
  },
  "kadd_ml_engine": {
    "model_path": "assets/model/model_tokens_rf.onnx",
    "feature_dim": 28,
    "window_size": 5
  }
}
```

### 6.1 必填字段

`kadd_sqli_engine_create_from_file` 要求以下字段存在：

- `kadd_dfa_engine.transition_table_path`
- `kadd_dfa_engine.output_table_path`
- `kadd_dfa_engine.eod_table_path`
- `kadd_ml_engine.model_path`

### 6.2 可选字段和默认值

| 字段 | SQLi 初始化默认值 | Config Loader 默认值 | 建议 |
| --- | --- | --- | --- |
| `feature_dim` | 28 | 28 | 保持 28，除非同步更新模型 |
| `window_size` | 5 | 5 | 保持 5，除非同步重训模型 |
| `max_input_length` | 8192 | 300 | 建议统一为 300 或统一提升 Feature 上限 |
| `max_output_dim` | 8192 | 300 | 建议不超过 `MAX_SEQ_LEN` |

设计要求：`max_output_dim <= MAX_SEQ_LEN`，否则 DFA 可能输出超过 Feature 引擎可处理的 token 数量，导致端到端检测失败。

## 7. 数据与资产格式

### 7.1 DFA 三表格式

三张表均为 little-endian 二进制。

#### `transition_table.bin`

```text
uint32 state_count
uint16 transition_table[state_count][256]
```

每个单元表示 `当前状态 + 输入字节 -> 下一状态`。特殊值：

- `0`：ENTRY 状态。
- `65535`：ERROR，不存在合法转移。

#### `output_table.bin`

```text
uint32 state_count
uint16 output_table[state_count][256]
```

每个单元是边输出编码，按 bit 拆分：

| bits | 字段 | 说明 |
| --- | --- | --- |
| 0..4 | `accept_info` | 目标状态的 token id，0 表示非接受 |
| 5..9 | `emit_token_id` | 需要立即输出的 token id |
| 10..11 | `scan_delta` | 输出后扫描位置增量 |
| 12..13 | `start_delta + 1` | token 起始修正，运行期解码后减 1 |
| 14..15 | `end_delta + 1` | 当前编译器写入，运行期暂未使用 |

#### `eod_table.bin`

```text
uint32 state_count
uint16 eod_table[state_count]
```

每个单元是输入结束动作：

| bits | 字段 | 说明 |
| --- | --- | --- |
| 0..1 | action | 0 无动作，1 输出 token，2 清空 token |
| 2..7 | token id | action=1 时输出的 token id |

### 7.2 Token id 映射

运行期 Feature 和模型使用整数 token id。`0` 保留为无 token，当前 DFA 编译器输出 `1..27`。

| id | token | 含义 |
| --- | --- | --- |
| 0 | `TYPE_NONE` | 保留 |
| 1 | `TYPE_KEYWORD` | SQL 关键字 |
| 2 | `TYPE_UNION` | UNION |
| 3 | `TYPE_GROUP` | 分组 |
| 4 | `TYPE_EXPRESSION` | 表达式 |
| 5 | `TYPE_SQLTYPE` | SQL 类型 |
| 6 | `TYPE_FUNCTION` | 函数 |
| 7 | `TYPE_BAREWORD` | 标识符/普通词 |
| 8 | `TYPE_NUMBER` | 数字 |
| 9 | `TYPE_VARIABLE` | 变量 |
| 10 | `TYPE_STRING` | 字符串 |
| 11 | `TYPE_OPERATOR` | 操作符 |
| 12 | `TYPE_LOGIC_OPERATOR` | 逻辑操作符 |
| 13 | `TYPE_COMMENT` | 注释 |
| 14 | `TYPE_COLLATE` | COLLATE |
| 15 | `TYPE_LEFTPARENS` | 左括号 |
| 16 | `TYPE_RIGHTPARENS` | 右括号 |
| 17 | `TYPE_LEFTBRACE` | 左花括号 |
| 18 | `TYPE_RIGHTBRACE` | 右花括号 |
| 19 | `TYPE_DOT` | 点 |
| 20 | `TYPE_COMMA` | 逗号 |
| 21 | `TYPE_COLON` | 冒号 |
| 22 | `TYPE_SEMICOLON` | 分号 |
| 23 | `TYPE_STATEMENT` | 语句 |
| 24 | `TYPE_UNKNOWN` | 未知 |
| 25 | `TYPE_EVIL` | 恶意模式 |
| 26 | `TYPE_FINGERPRINT` | 指纹 |
| 27 | `TYPE_BACKSLASH` | 反斜杠 |

## 8. 模块详细设计

### 8.1 DFA 引擎

#### 8.1.1 对外 API

头文件：`include/kadd_dfa_engine.h`

```c
int kadd_dfa_engine_create(kadd_dfa_engine_t **engine,
                           const kadd_dfa_table_paths_t *table_paths,
                           size_t max_input_length,
                           size_t max_output_dim);

int kadd_dfa_engine_tokenize(const kadd_dfa_engine_t *e,
                             const char *input,
                             size_t input_length,
                             int *output,
                             int *dim);

int kadd_dfa_engine_tokenize_batch(const kadd_dfa_engine_t *e,
                                   kadd_dfa_batch_t *batch);

void kadd_dfa_engine_free(kadd_dfa_engine_t *e);
```

#### 8.1.2 内部结构

```c
struct kadd_dfa_engine_t {
    uint16_t *transition_table;
    uint16_t *output_table;
    uint16_t *eod_table;
    size_t max_input_length;
    size_t max_output_dim;
};
```

设计要点：

- 三张表在创建阶段一次性加载到内存。
- 加载时校验三张表 `state_count` 一致。
- 表内容使用 `uint16_t`，节省内存并便于 cache 命中。
- 引擎本身不持有 profile 文本，不需要运行期解析状态名。

#### 8.1.3 单样本分词流程

1. 校验参数、表指针和输入长度。
2. 跳过 ASCII 控制字符、空格和 DEL。
3. 从 ENTRY 状态开始扫描一个 token。
4. 对每个字节读取：
   - `candidate_state = transition_table[state][byte]`
   - `output_code = output_table[state][byte]`
5. 如果 `candidate_state == 65535`，当前 token 扫描结束。
6. 如果 `output_code` 携带 `emit_token_id`：
   - 立即输出 token。
   - 根据 `scan_delta` 调整扫描位置。
   - 根据 `start_delta` 调整当前 token 起点。
   - 切换到候选状态继续扫描。
7. 如果没有立即输出：
   - 记录边上的 `accept_info`。
   - 前进一个字节并切换状态。
8. 到输入末尾时读取 `eod_table[state]`：
   - `CLEAR`：清空接受信息。
   - `EMIT`：将 token id 作为接受信息。
9. 如果 `accept_info != 0`，写入输出数组。
10. 写入 `*dim` 并返回。

#### 8.1.4 批量分词

`kadd_dfa_engine_tokenize_batch` 负责：

- 初始化每个 `outputs[i]=NULL`、`dims[i]=-1`。
- 支持调用方提供 lengths；未提供时使用 `strlen`。
- 每个输入分配 `max_output_dim` 大小的 `int` 输出缓冲区。
- 单条失败不影响其他输入。
- 完全成功返回 `KADD_SUCCESS`，部分成功返回成功数量，完全失败返回负数。

调用方必须释放成功项的 `outputs[i]`。

#### 8.1.5 开发注意

- 修改 token 映射必须同步更新 DFA 编译器、Feature 维度、模型训练和 API 文档。
- 修改 profile 后必须重新运行 DFA 编译器生成三张表。
- 二进制表格式是运行期 ABI，变更需要显式版本化。

### 8.2 Feature 引擎

#### 8.2.1 对外 API

头文件：`include/kadd_feature_engine.h`

```c
int kadd_seq_to_feature(const int *seq, int seq_len, int window_size,
                        int *feature, int feature_dim);

int kadd_seq_to_feature_batch(kadd_feature_batch_t *batch);
```

#### 8.2.2 算法定义

输入：

- `seq`：token id 序列，长度 `seq_len`。
- `window_size`：滑动窗口大小。
- `feature_dim`：输出特征维度，当前模型使用 28。

中间结果：

```text
result[i] =
    sum(seq[i-d] * (window_size - d), d=1..window_size-1, i-d >= 0)
  + sum(seq[i+d] * (window_size - d), d=1..window_size-1, i+d < seq_len)
```

最终特征：

```text
feature[token_id] += result[position]
```

其中 `token_id = seq[position]`。

#### 8.2.3 实现策略

- 默认实现：`sliding_window_distance_shift_fast`
  - 外层按距离 `d` 遍历。
  - 预计算 `weighted[i] = seq[i] * weight`。
  - 右侧贡献写入 `result[i] += weighted[i+d]`。
  - 左侧贡献写入 `result[i] += weighted[i-d]`。
  - 4 路循环展开减少循环开销。
- ARM64 + SVE：`sliding_window_distance_sve`
  - 使用 SVE 向量 load、multiply、add、store。
  - 尾部仍使用标量循环处理。

#### 8.2.4 参数约束

设计上应满足：

- `seq != NULL`
- `feature != NULL`
- `seq_len > 0`
- `window_size > 0`
- `0 < feature_dim <= MAX_FEATURE_DIM`
- `seq_len <= MAX_SEQ_LEN`
- 对所有 `i`，`0 <= seq[i] < feature_dim`

失败时返回统一错误码：

- 空指针：`KADD_ERROR_NULL_PTR`
- 参数非法：`KADD_ERROR_INVALID_PARAM`
- token 越界：`KADD_FEATURE_ERROR_VALUE_OUT_OF_RANGE`
- 内存不足：`KADD_ERROR_OUT_OF_MEMORY`

#### 8.2.5 批量特征

`kadd_seq_to_feature_batch` 使用展平二维数组：

```text
features[count][feature_dim]
```

逻辑：

- 先将输出全部置零。
- 分批遍历。
- 对每个序列调用单样本特征提取。
- 单条失败时保持该条特征全零。
- 全部成功返回 0，部分成功返回成功数量。

### 8.3 ML 引擎

#### 8.3.1 对外 API

头文件：`include/kadd_ml_engine.h`

```c
int kadd_model_load(kadd_model_handle_t **handle,
                    const char *model_path,
                    int feature_dim);

int kadd_model_predict(kadd_model_handle_t *handle,
                       const float *features,
                       float *probability);

int kadd_model_predict_batch(kadd_model_handle_t *handle,
                             const float *features,
                             int n,
                             float *probabilities,
                             int batch_size);

void kadd_model_free(kadd_model_handle_t *handle);
```

#### 8.3.2 内部结构

```c
struct kadd_model_handle_t {
    OrtSession *session;
    OrtEnv *env;
    OrtMemoryInfo *memory_info;
    const OrtApi *api;
    char *output_name;
    int feature_dim;
};
```

全局变量：

- `g_ort_env`：进程级 ONNX Runtime 环境，多个模型共享。
- `g_ort_api`：ONNX Runtime C API 指针，支持 API 版本回退。

#### 8.3.3 模型加载流程

1. 校验参数。
2. 分配 `kadd_model_handle_t`。
3. 初始化 `g_ort_api`：
   - 从 `ORT_API_VERSION` 向下回退到 15。
4. 初始化 `g_ort_env`。
5. 创建 SessionOptions：
   - `ORT_SEQUENTIAL`
   - `ORT_ENABLE_ALL`
6. 设置环境变量：
   - `ORT_UNRELEASED_OPS=1`
   - `ORT_ENABLE_OPTIONAL_OPS=1`
7. 创建 ONNX session。
8. 创建 CPU memory info。
9. 自动探测输出节点：
   - 优先选择名称包含 `probability` 的输出。
   - 否则使用默认 `probabilities`。
10. 使用全零特征执行一次 warmup。

#### 8.3.4 推理流程

单样本：

- 输入 shape：`[1, feature_dim]`
- 输入名：`float_input`
- 输出名：`handle->output_name`
- 输出 shape 约定：`[1, 2]`
- `probability = data[1]`

批量：

- 输入 shape：`[current_batch, feature_dim]`
- 输出 shape 约定：`[current_batch, 2]`
- 第 `j` 个样本概率：`data[j * 2 + 1]`
- 大批量按 `batch_size` 分块。

#### 8.3.5 开发注意

- 训练脚本和 C 推理约定必须一致：
  - 输入名固定为 `float_input`。
  - 输出需为二维概率矩阵。
  - 正类概率在第二列。
- 如果替换为非 sklearn 导出的模型，需要补充输入/输出节点名配置，避免硬编码。
- ONNX Runtime 环境当前按进程生命周期持有，不在 `kadd_model_free` 中释放。

### 8.4 SQLi 引擎

#### 8.4.1 对外 API

头文件：`include/kadd_sqli_engine.h`

```c
int kadd_sqli_engine_create_from_file(kadd_sqli_engine_t **engine,
                                      const char *config_file);

int kadd_sqli_detect(kadd_sqli_engine_t *engine,
                     const char *input,
                     float *probability);

int kadd_sqli_detect_batch(kadd_sqli_engine_t *engine,
                           const char **inputs,
                           int count,
                           float *probabilities,
                           int batch_size);

void kadd_sqli_engine_free(kadd_sqli_engine_t *engine);
```

#### 8.4.2 内部结构

```c
struct kadd_sqli_engine_t {
    kadd_dfa_engine_t *dfa_engine;
    kadd_model_handle_t *ml_model;
    int feature_dim;
    int window_size;
    int max_output_dim;
    int max_input_length;

    int *token_buffer;
    int *feature_buffer;
    float *float_feature_buffer;
};
```

#### 8.4.3 初始化流程

1. 读取 JSON 配置文件。
2. 解析 DFA 三张表路径和最大输入/输出长度。
3. 解析 ML 模型路径、Feature 维度和窗口大小。
4. 分配 SQLi 引擎结构。
5. 分配单样本检测复用缓冲：
   - `token_buffer[max_output_dim]`
   - `feature_buffer[feature_dim]`
   - `float_feature_buffer[feature_dim]`
6. 创建 DFA 引擎。
7. 加载 ONNX 模型。
8. 任一步失败时释放已分配资源。

#### 8.4.4 单样本检测流程

1. 参数校验。
2. `probability` 初始化为 0。
3. 调用 DFA 分词：
   - 输入长度通过 `strlen(input)` 计算。
   - 输出写入 `token_buffer`。
4. 调用 Feature 提取：
   - 输入 `token_buffer/token_count`。
   - 输出写入 `feature_buffer`。
5. 将 `int` 特征转换为 `float`。
6. 调用 ML 单样本推理。
7. 输出概率。

#### 8.4.5 批量检测流程

1. 校验参数。
2. 规范化 `batch_size`，上限 1024。
3. 分配批量临时缓冲：
   - `dfa_outputs[batch_size]`
   - `dfa_dims[batch_size]`
   - `features[batch_size * feature_dim]`
   - `float_features[batch_size * feature_dim]`
4. 按批处理：
   - DFA 批量分词。
   - Feature 批量提取。
   - int feature 转 float feature。
   - ML 批量推理。
   - 写回 probabilities。
   - 释放 DFA 每条输出。
5. 释放批量临时缓冲。
6. 全部成功返回 0，部分成功返回成功数量。

#### 8.4.6 线程安全

`kadd_sqli_detect` 使用引擎内复用缓冲区，因此同一个 `kadd_sqli_engine_t` 不能被多个线程并发调用单样本检测。

建议集成方式：

- 每个工作线程持有独立 `kadd_sqli_engine_t`。
- 或调用方用互斥锁保护同一实例。
- 对高并发服务，优先使用线程本地引擎池。

底层 DFA 表只读；ONNX session 通常支持并发 Run，但当前 SQLi 封装层存在共享缓冲，所以并发边界应放在 SQLi 引擎实例外。

### 8.5 Config Loader

`kadd_config_loader` 是通用配置读取模块，提供 `kadd_config_load` 和 `kadd_config_free`。

职责：

- 从配置文件读取 DFA、ML、SQLi、Feature 配置。
- 对缺省值填充默认参数。
- 使用固定长度字符数组保存路径，避免动态字符串生命周期问题。

设计建议：

- 后续应将返回码统一到 `kadd_errors.h`。
- 与 SQLi 引擎内部配置解析的默认值保持一致。
- 如引入更多配置项，优先复用 Config Loader，避免 SQLi 引擎中重复解析 JSON。

## 9. 工具链设计

### 9.1 DFA 编译链路

输入：

- `tools/dfa_compiler/profile_sqli_fixed.txt`
- `config.json` 中的三张表路径

输出：

- `assets/data/transition_table.bin`
- `assets/data/output_table.bin`
- `assets/data/eod_table.bin`

执行：

```bash
python3 tools/dfa_compiler/dfa_compiler.py
```

编译步骤：

1. 解析 `state NAME[: TYPE]` 状态定义。
2. 解析字符码转移、`default` 和 `eod` 规则。
3. 规范化特殊 SQL token 场景：
   - 空格复合词拆分。
   - keyword trie 的 word continuation。
   - q-string 动态分隔符。
   - 接受态边界。
4. 展开特殊动作：
   - `copy TARGET`
   - `dup TARGET prepend TYPE_*`
   - `prepend TYPE_*`
   - `emit_prefix N rewind M prepend TYPE_*`
5. 合成或折叠 EOD 规则。
6. 生成三张表并写入二进制文件。

开发规则：

- profile 修改后必须重新生成三张表。
- 生成后的三张表必须一起提交，不能只更新其中一张。
- 编译器中的 token 映射是运行期 token id 的源头之一，变更需评审。

### 9.2 模型训练链路

输入数据：

- 默认 CSV：`assets/data/total_training_dataset_v4_pinnacle.csv`
- 或 JSON：`assets/data/full_dfa_tokenize_results.json`

输出：

- `tools/model_tokens_rf.onnx`，按项目约定应放入 `assets/model/model_tokens_rf.onnx`。

执行：

```bash
python3 tools/train_tokens.py --csv path/to/dataset.csv
python3 tools/train_tokens.py --json path/to/dataset.json
```

训练步骤：

1. 加载 query 和 label。
2. 对 query 进行 DFA 分词。
3. 使用与 C 代码一致的滑窗特征逻辑生成 28 维特征。
4. 划分训练集、测试集和验证集。
5. 训练 `RandomForestClassifier`。
6. 输出准确率、分类报告、混淆矩阵。
7. 使用 `skl2onnx` 导出 ONNX。
8. 用 ONNX Runtime 做导出后验证。

开发规则：

- `FEATURE_DIM`、`WINDOW_SIZE` 与 `config.json` 必须一致。
- C 与 Python 的特征算法必须保持等价。
- 任何模型替换都必须跑：
  - ML 单元测试。
  - SQLi 端到端测试。
  - 一致性测试。
  - 代表性误报/漏报样本集。

## 10. 错误处理设计

全项目返回值约定：

| 返回值 | 语义 |
| --- | --- |
| `0` | 完全成功 |
| `>0` | 部分成功，数值表示成功处理的条数 |
| `<0` | 失败，值为错误码 |

核心错误码：

| 错误码 | 含义 |
| --- | --- |
| `KADD_ERROR_INVALID_INPUT` | 输入整体非法 |
| `KADD_ERROR_NULL_PTR` | 空指针 |
| `KADD_ERROR_OUT_OF_MEMORY` | 内存不足 |
| `KADD_ERROR_INVALID_PARAM` | 参数值非法 |
| `KADD_ERROR_CONFIG` | 配置错误 |
| `KADD_DFA_ERROR_FILE_NOT_FOUND` | DFA 表文件不存在 |
| `KADD_DFA_ERROR_PARSE` | DFA 表解析失败 |
| `KADD_DFA_ERROR_BUFFER_OVERFLOW` | DFA 输出超过上限 |
| `KADD_DFA_ERROR_INVALID_LENGTH` | 输入长度超过上限 |
| `KADD_FEATURE_ERROR_VALUE_OUT_OF_RANGE` | token id 超出 feature 维度 |
| `KADD_SQLI_ERROR_DFA` | SQLi 封装层 DFA 子流程失败 |
| `KADD_SQLI_ERROR_FEATURE` | SQLi 封装层 Feature 子流程失败 |
| `KADD_SQLI_ERROR_ML` | SQLi 封装层 ML 子流程失败 |

设计规则：

- public API 先做参数校验，再访问内部字段。
- 初始化流程中任一步失败必须释放已创建资源。
- 批处理 API 不因单条失败中断整体流程，除非全局资源分配失败。
- SQLi 高层 API 对子模块错误做归类，不暴露过细内部错误。

## 11. 内存与资源管理

### 11.1 所有权

| 资源 | 分配者 | 释放者 |
| --- | --- | --- |
| `kadd_dfa_engine_t` | `kadd_dfa_engine_create` | `kadd_dfa_engine_free` |
| DFA 三张表 | DFA 引擎内部 | `kadd_dfa_engine_free` |
| 批量 DFA 输出 `outputs[i]` | `kadd_dfa_engine_tokenize_batch` | 调用方 |
| `kadd_model_handle_t` | `kadd_model_load` | `kadd_model_free` |
| ONNX session | ML 引擎内部 | `kadd_model_free` |
| `kadd_sqli_engine_t` | `kadd_sqli_engine_create_from_file` | `kadd_sqli_engine_free` |
| SQLi 单样本缓冲 | SQLi 引擎内部 | `kadd_sqli_engine_free` |
| SQLi 批处理临时缓冲 | `kadd_sqli_detect_batch` | 函数返回前释放 |

### 11.2 对齐

SQLi 单样本 `float_feature_buffer`：

- Windows：`malloc`
- 非 Windows：`posix_memalign(..., 16, ...)`

这是为了让 ONNX 输入和潜在 SIMD 访问更友好。

### 11.3 失败清理

开发要求：

- 任何新增初始化步骤都必须加入失败清理路径。
- 多资源函数应按“先分配，失败跳到统一清理”的模式重构，降低遗漏概率。
- 批量流程中单条输出释放要在每个批次结束时保证执行。

## 12. 测试设计

### 12.1 DFA 测试

覆盖：

- 基础 token：关键字、数字、字符串、操作符、括号。
- 空白处理：空格、换行、制表符。
- SQL 注入样本：OR、UNION、堆叠、盲注、复杂注入。
- 正常流量：邮箱、正常 SQL、Web 参数。
- 异常输入：空指针、空路径、超长输入。
- 边界：最大输入长度、空输入、单字符。

### 12.2 Feature 测试

覆盖：

- 简单序列、单元素、窗口大小变化。
- 特征维度上下限。
- token id 越界和负数。
- 序列长度边界。
- 批处理成功/部分成功。
- 性能：小、中、大序列吞吐。

### 12.3 ML 测试

覆盖：

- 模型加载。
- 单样本推理。
- 批量推理和不同 batch size。
- 单样本与批量一致性。
- 空指针、非法模型路径、非法 feature_dim。
- 性能：单样本、批量、模型加载、大批量内存压力。

### 12.4 SQLi 测试

覆盖：

- 经典 OR、UNION、注释、堆叠、盲注、时间注入、错误注入。
- 正常邮箱、正常 SQL、Web 参数。
- Unicode、特殊字符、大小写混合。
- 单样本性能、批量性能。
- 批量与单样本结果一致性。
- 阈值敏感性。

### 12.5 验收命令

标准构建：

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
ctest --verbose
```

Release 优化构建：

```bash
mkdir -p build-release
cd build-release
cmake -DCMAKE_BUILD_TYPE=Release -DKADD_OPTIMIZE=ON ..
cmake --build .
ctest --verbose
```

ARM SVE 构建：

```bash
cmake -DCMAKE_BUILD_TYPE=Release -DKADD_OPTIMIZE=ON -DENABLE_ARM_SVE=ON ..
cmake --build .
```

## 13. 典型开发任务指导

### 13.1 修改 DFA 规则

开发步骤：

1. 修改 `tools/dfa_compiler/profile_sqli_fixed.txt`。
2. 运行 `python3 tools/dfa_compiler/dfa_compiler.py`。
3. 确认三张表同时更新。
4. 运行 `dfa_engine_test`。
5. 运行 `sqli_engine_test`，确认端到端行为未回退。
6. 如果 token 分布明显变化，重训模型。

验收：

- 新样本 token 序列符合预期。
- 原有核心注入样本仍能识别。
- 正常流量误报不明显上升。

### 13.2 新增 token 类型

开发步骤：

1. 在 `dfa_compiler.py` 中更新：
   - `TOKEN_TYPE_MAP`
   - `TOKEN_CHAR_ORDER`
   - `_token_char_name`
2. 更新 `docs/API.md` 和本文档 token 表。
3. 调整 `feature_dim` 和 `MAX_FEATURE_DIM`。
4. 重新生成 DFA 表。
5. 重新训练并导出 ONNX 模型。
6. 更新 `config.json`。
7. 补充 DFA、Feature、SQLi 测试。

注意：

- token id 是模型输入语义的一部分，不能只改 DFA。
- `feature_dim` 必须大于最大 token id。

### 13.3 替换或升级模型

开发步骤：

1. 确认新模型输入 shape 为 `[None, feature_dim]`。
2. 确认输入节点名为 `float_input`，或先改造 ML 引擎支持配置化输入名。
3. 确认输出为 `[None, 2]` 概率矩阵，正类概率在第二列。
4. 替换 `assets/model/model_tokens_rf.onnx`。
5. 运行 ML 和 SQLi 测试。
6. 输出模型评测报告，包括准确率、召回率、误报率、代表样本。

验收：

- `kadd_model_predict` 和 `kadd_model_predict_batch` 输出一致。
- 概率范围在 `[0, 1]`。
- SQLi 端到端测试通过。

### 13.4 增加新的 public API

开发步骤：

1. 在 `include/` 添加声明。
2. 在对应 `src/` 添加实现。
3. 保持 C ABI 友好，不暴露内部结构。
4. 错误码使用 `kadd_errors.h`。
5. 补充 demo 或测试。
6. 更新 API 文档。

设计规则：

- public API 不接管调用方传入字符串的所有权。
- 输出缓冲区由调用方分配时，必须明确容量参数。
- 由库分配的内存必须提供对应 free API。

### 13.5 服务集成

建议流程：

1. 服务启动时创建引擎，不要每个请求加载模型。
2. 每个 worker 持有独立引擎实例，或使用引擎池。
3. 请求侧先做业务需要的 URL decode、字符集归一化和长度限制。
4. 调用 `kadd_sqli_detect` 或 `kadd_sqli_detect_batch` 获取概率。
5. 业务侧按场景阈值拦截、告警或放行。
6. 记录输入摘要、概率、阈值、动作，不记录敏感原文或做脱敏。

建议阈值：

| 场景 | 阈值 |
| --- | --- |
| 高安全 | 0.3 |
| 平衡 | 0.5 |
| 低误报 | 0.7 |

最终阈值必须基于业务样本集校准，不应只依赖测试集。

## 14. 性能设计

### 14.1 DFA

性能来源：

- 二进制表一次加载，运行期 O(n) 查表。
- `state * 256 + byte` 直接定位，不做字符串状态查找。
- 跳过空白字符减少 token 扫描开销。

关注指标：

- 单输入分词耗时。
- 每字节处理成本。
- 长输入下输出 token 上限保护。

### 14.2 Feature

性能来源：

- 复杂度约为 `O(seq_len * window_size)`。
- 窗口通常为 5，实际常数较小。
- 循环展开和 SVE 分支提升吞吐。

关注指标：

- 小序列高 QPS。
- `window_size` 变大时的线性开销。
- `seq_len` 靠近 300 时的尾部表现。

### 14.3 ML

性能来源：

- 模型加载时 warmup，减少首包延迟。
- 批量推理复用一次 ONNX Runtime Run。
- SessionOptions 启用全图优化。

关注指标：

- 模型加载耗时。
- 单样本 P50/P99。
- 批量 samples/sec。
- 大 batch 内存峰值。

### 14.4 SQLi

性能来源：

- 单样本检测复用引擎内缓冲区，避免频繁 malloc/free。
- 批量检测在循环外分配批缓冲。

关注指标：

- 端到端单样本耗时。
- 批量吞吐。
- 不同 batch size 下的收益拐点。

## 15. 当前实现差异与待统一点

以下是当前源码、头文件、旧文档和测试之间需要开发同学优先对齐的点。

| 项 | 当前情况 | 建议处理 |
| --- | --- | --- |
| 旧 `docs/DESIGN.md` 的 DFA 配置 | 仍描述 `table_path/state_path` | 更新为三张表：`transition/output/eod` |
| Feature 参数校验 | 头文件/测试倾向 `seq_len>0`、`window_size>0`，源码单样本对 0 值校验不完全一致 | 统一源码、测试和文档 |
| `max_output_dim` 默认值 | SQLi 解析默认 8192，Config Loader 默认 300，Feature 上限 300 | 统一默认值，或提升 Feature 上限 |
| `kadd_sqli_detect_batch` 失败概率 | 头文件说明失败为 `-1.0`，源码初始化为 `0.0` | 改源码或改接口说明，建议用 `-1.0` 区分安全概率 |
| `kadd_model_predict_batch` 自动 batch | 头文件说明 `<=0` 默认 64，源码中 `<=1` 变为 1 | 建议改源码为 `<=0` 默认 64，`1` 保持 1 |
| ML 输入节点名 | 硬编码 `float_input` | 模型固定时可接受；后续建议配置化 |
| ML 输出名 | 自动找包含 `probability`，否则 fallback `probabilities` | 替换模型时需验证输出名 |
| SQLi 单样本输入长度 | 使用 `strlen` | 如果需处理二进制或含 NUL 输入，应新增带长度 API |
| Config Loader 返回码 | 多处返回 `-1` | 建议统一使用 `kadd_errors.h` |

## 16. 开发验收清单

代码变更提交前至少确认：

- public API 声明、实现、文档一致。
- 错误码符合 `kadd_errors.h` 约定。
- 初始化失败路径释放所有已分配资源。
- 批处理 API 能表达部分成功。
- `config.json` 与模型、DFA 表、Feature 参数一致。
- 修改 DFA profile 后重新生成三张表。
- 修改特征算法或 token 映射后重新训练模型。
- 单元测试、集成测试、批量一致性测试通过。
- Release 构建通过。
- 如果涉及 ARM 加速，至少完成 ARM64 构建验证。

## 17. 推荐开发顺序

对新同学建议按以下顺序熟悉和开发：

1. 先跑通标准构建和 `ctest`。
2. 阅读 `include/kadd_sqli_engine.h`，理解对外入口。
3. 阅读 `src/kadd_sqli_engine.c`，理解端到端编排。
4. 阅读 `src/kadd_dfa_engine.c`，理解三张表运行期语义。
5. 阅读 `tools/dfa_compiler/dfa_compiler.py`，理解三张表来源。
6. 阅读 `src/kadd_feature_engine.c` 和 `tools/train_tokens.py`，确认 C/Python 特征一致。
7. 阅读 `src/kadd_ml_engine.c`，确认 ONNX 输入输出约定。
8. 最后根据任务修改对应模块，并补齐测试。

