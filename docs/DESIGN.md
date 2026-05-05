# KADD 架构设计文档

---

## 1. 系统概述

KADD (Kernel-level Anomaly Detection & Defense) 是一个轻量级 SQL 注入检测引擎，专为高性能场景设计。

### 1.1 设计目标

- **轻量级**：最小化资源占用，适合嵌入式和边缘设备部署
- **高性能**：支持 ARM 向量化加速（NEON/SVE/SVE2）
- **模块化**：各引擎独立可用，灵活组合
- **跨平台**：支持 ARM Linux、Windows (MinGW/MSVC)

### 1.2 技术栈

| 组件 | 技术选择 |
|------|----------|
| 语言 | C (C11) / C++ (C++17) |
| 推理引擎 | ONNX Runtime |
| 构建系统 | CMake 3.16+ |
| 测试框架 | Google Test |

---

## 2. 架构设计

### 2.1 系统架构图

```
┌─────────────────────────────────────────────────────────────────┐
│                         KADD SQLi Engine                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│   输入字符串                                                      │
│       │                                                          │
│       ▼                                                          │
│   ┌─────────────────┐                                           │
│   │  DFA 引擎        │  ◄── transition_table.bin               │
│   │  (SQL 分词)      │      states_meta.json                    │
│   └────────┬─────────┘                                           │
│            │                                                     │
│            ▼                                                     │
│        Token 序列                                                │
│            │                                                     │
│            ▼                                                     │
│   ┌─────────────────┐                                           │
│   │  特征引擎        │  (滑动窗口距离 + 向量化)                   │
│   └────────┬─────────┘                                           │
│            │                                                     │
│            ▼                                                     │
│       特征向量 (28维)                                            │
│            │                                                     │
│            ▼                                                     │
│   ┌─────────────────┐                                           │
│   │  ML 引擎         │  ◄── model_tokens_rf.onnx                │
│   │  (ONNX 推理)     │                                           │
│   └────────┬─────────┘                                           │
│            │                                                     │
│            ▼                                                     │
│       概率值 [0, 1]                                              │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 模块依赖关系

```
kadd_sqli_engine
    ├── kadd_dfa_engine     (无依赖)
    ├── kadd_feature_engine (无依赖)
    ├── kadd_ml_engine      (依赖: ONNX Runtime)
    └── cJSON               (可选，用于配置解析)
```

---

## 3. 模块详细设计

### 3.1 DFA 引擎

#### 功能
基于确定性有限自动机（DFA）的 SQL 分词器，将输入字符串转换为 token 索引序列。

#### 核心算法
- 预编译的状态转移表（二进制格式）
- 支持指令系统（`copy ENTRY`, `dup ENTRY prepend TYPE_*`）
- 状态类型分类优化（哈希快速查找）

#### Token 类型
- 28 种 token 类型（参考 libinjection）
- 支持特殊语法：q-string、B'、X'、E'、N'、U&' 前缀字符串
- 多字符操作符：`||`、`&&`、`==`、`!=`、`<=`、`>=` 等

#### 文件结构
```
include/kadd_dfa_engine.h    - 公共 API
src/kadd_dfa_engine.c        - 实现 (1898 行)
```

---

### 3.2 特征引擎

#### 功能
将不定长 token 序列转换为固定 28 维特征向量。

#### 核心算法
- **滑动窗口距离**：计算序列中每个位置的上下文相似度
- **映射策略**：按 token 值累加到对应维度

#### 性能优化
| 优化技术 | 说明 |
|----------|------|
| 循环展开 | 4 路 SIMD 友好展开 |
| ARM SVE | 可变长向量加速（ARMv9-A） |
| ARM NEON | 128 位向量加速 |

#### 公式

对于序列 `seq`，窗口大小 `window_size`：

```
result[i] = Σ( window_size - |i - j| ) * seq[j ]
           j∈[i-window_size+1, i+window_size-1]
```

#### 文件结构
```
include/kadd_feature_engine.h    - 公共 API
src/kadd_feature_engine.c        - 实现 (165 行)
```

---

### 3.3 ML 引擎

#### 功能
基于 ONNX Runtime 的模型推理引擎。

#### 模型信息
- **输入**：`[batch_size, 28]` float32 特征向量
- **输出**：`[batch_size, 2]` float32 (负类概率, 正类概率)
- **格式**：ONNX 1.x

#### 批量推理策略
- 分块处理避免内存峰值
- 默认批次大小：64
- 支持动态批次大小

#### 版本兼容
- 支持 ONNX Runtime 1.16+ (API 版本 15+)
- 自动版本回退机制

#### 文件结构
```
include/kadd_ml_engine.h    - 公共 API
src/kadd_ml_engine.c        - 实现 (378 行)
```

---

### 3.4 SQLi 引擎

#### 功能
整合 DFA、特征、ML 引擎，提供完整的 SQL 注入检测 API。

#### 检测流程
```
1. 读取 JSON 配置文件
2. 初始化 DFA 引擎 (转移表 + 状态元数据)
3. 加载 ONNX 模型
4. 对每个输入：
   a. DFA 分词 → Token 序列
   b. 特征提取 → 28 维向量
   c. ML 推理 → 注入概率
```

#### 配置驱动
```json
{
  "kadd_dfa_engine": {
    "table_path": "...",
    "state_path": "...",
    "max_input_length": 8192,
    "max_output_dim": 8192
  },
  "kadd_ml_engine": {
    "model_path": "...",
    "feature_dim": 28,
    "window_size": 5
  }
}
```

#### 文件结构
```
include/kadd_sqli_engine.h    - 公共 API
src/kadd_sqli_engine.c        - 实现 (349 行)
```

---

## 4. 数据结构

### 4.1 DFA 引擎内部结构

```c
struct kadd_dfa_engine_t {
    char table_path[256];                // 转移表路径
    char state_path[256];                // 状态元数据路径
    uint32_t num_states;                 // 状态数量
    char **state_names;                  // 状态名称数组
    kadd_trans_entry_t **transitions;    // 状态转移表 [num_states][256]
    kadd_state_meta_t *meta;             // 状态元数据
    uint8_t *state_types;                // 状态类型数组
    uint32_t *state_hashes;              // 状态名哈希 (DJB2)
    int id_word;                         // WORD 状态 ID
    size_t max_input_length;             // 最大输入长度
    size_t max_output_dim;               // 最大输出维度
};
```

### 4.2 ML 引擎内部结构

```c
struct kadd_model_handle_t {
    OrtSession *session;      // ONNX 会话
    OrtEnv *env;              // ONNX 环境
    OrtMemoryInfo *memory_info;  // 内存信息
    const OrtApi *api;        // ONNX API
    char *output_name;        // 输出节点名称
    int feature_dim;          // 特征维度
};
```

### 4.3 SQLi 引擎内部结构

```c
struct kadd_sqli_engine_t {
    kadd_dfa_engine_t *dfa_engine;  // DFA 引擎
    kadd_model_handle_t *ml_model;  // ML 模型
    int feature_dim;                // 特征维度
    int window_size;                // 窗口大小
    int max_output_dim;             // 最大输出维度
    int max_input_length;           // 最大输入长度
};
```

---

## 5. 性能优化

### 5.1 编译优化

| 选项 | 效果 |
|------|------|
| `-O3` | 最高优化级别 |
| `-march=armv9-a+sve` | 启用 SVE |
| `-march=armv9-a+sve2` | 启用 SVE2 |
| `--coverage` | 代码覆盖率 |

### 5.2 算法优化

| 模块 | 优化技术 |
|------|----------|
| DFA | 状态哈希快速查找、字符分类表 |
| 特征 | 循环展开、ARM SVE 向量化 |
| ML | 分块批量推理、内存对齐 |

### 5.3 性能指标

| 指标 | 数值 |
|------|------|
| 单次推理延迟 | ~0.1 ms |
| 批量吞吐量 | ~100,000 ops/sec |

---

## 6. 错误处理

### 6.1 错误传播策略

```
SQLi Engine
    ├── DFA 错误 → KADD_SQLI_ERROR_DFA
    ├── 特征错误 → KADD_SQLI_ERROR_FEATURE
    └── ML 错误 → KADD_SQLI_ERROR_ML
```

### 6.2 资源管理

- **失败即释放**：任何步骤失败，立即释放已分配资源
- **句柄验证**：所有公共 API 首先验证句柄有效性
- **内存对齐**：POSIX `posix_memalign` / Windows `_aligned_malloc`

---

## 7. 测试策略

### 7.1 测试覆盖

| 测试类型 | 覆盖范围 |
|----------|----------|
| 单元测试 | 每个引擎独立测试 |
| 集成测试 | 端到端 SQL 注入检测 |
| 边界测试 | 空输入、超长输入、特殊字符 |
| 性能测试 | 延迟、吞吐量基准 |

### 7.2 测试用例示例

```cpp
// 经典 OR 注入
"id=1' OR '1'='1"
"admin' OR 1=1--"

// UNION 注入
"1' UNION SELECT NULL--"

// 正常输入
"user@example.com"
"SELECT * FROM users"
```

---

## 8. 部署建议

### 8.1 推荐配置

```json
{
  "kadd_dfa_engine": {
    "max_input_length": 8192,
    "max_output_dim": 8192
  },
  "kadd_ml_engine": {
    "feature_dim": 28,
    "window_size": 5
  }
}
```

### 8.2 阈值建议

| 场景 | 建议阈值 |
|------|----------|
| 高安全要求 | 0.3 |
| 平衡模式 | 0.5 |
| 低误报要求 | 0.7 |

---

*文档版本: 1.0.0*
