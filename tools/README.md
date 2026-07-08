# KADD Tools 工具集

本目录包含 KADD 项目相关的数据处理和模型训练工具。

## 目录结构

```
tools/
├── dfa_compile/           # DFA编译器
│   ├── dfa_compiler.py          # DFA编译器脚本
│   └── profile_sqli_fixed.txt   # DFA规则文件
└── model_train/           # 模型训练目录
    ├── model_train.py           # 训练脚本
    └── train_data/              # 训练数据子目录
        ├── CMakeLists.txt       # C工具构建配置
        ├── csv_to_vector_tool.c # CSV转向量转换工具
        ├── total_training_dataset_v4_pinnacle.csv  # 原始训练数据
        └── training_vectors.csv # 生成向量数据（由C工具生成）
```

## 工作流程

```
┌─────────────────────────────────────────────────────────────┐
│  0. DFA规则编译 (可选，仅修改规则时需要)                      │
│     dfa_compiler.py - 将SQL模式规则编译为DFA状态表            │
│     输出: transition_table.bin, output_table.bin, eod_table.bin│
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│  1. 原始数据                                                 │
│     total_training_dataset_v4_pinnacle.csv                   │
│     (Query列 + Label列)                                       │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│  2. C代码处理 (csv_to_vector_tool)                           │
│     - DFA tokenize   (src/kadd_dfa_engine.c)                 │
│     - 特征提取      (src/kadd_feature_engine.c)            │
│     输出: training_vectors.csv                               │
│     (feature_0~feature_27 + Label)                            │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│  3. Python训练 (model_train.py)                              │
│     - 随机森林分类器                                         │
│     - 导出ONNX模型                                           │
│     输出: model_tokens_rf.onnx                               │
└─────────────────────────────────────────────────────────────┘
```

## 快速开始

### 环境要求

- **编译环境**: Linux ARM (aarch64)
- **Python**: 3.8+
- **C依赖库**:
  ```bash
  # 安装cJSON（配置文件解析）
  sudo apt-get install libcjson-dev
  # 或使用第三方cJSON（自动回退）
  ```
- **Python依赖库**:
  ```bash
  # 模型训练
  pip install pandas numpy scikit-learn skl2onnx onnxruntime
  # DFA编译（可选，仅修改DFA规则时需要）
  pip install regex
  ```

### 步骤 1: 编译C工具

```bash
cd tools/model_train/train_data
mkdir build && cd build
cmake ..
make
```

编译成功后，会生成可执行文件 `csv_to_vector_tool`

### 步骤 2: 生成训练向量

```bash
# 在 build 目录下直接运行
./csv_to_vector_tool

# 或指定参数
./csv_to_vector_tool \
    ../total_training_dataset_v4_pinnacle.csv \
    ../training_vectors.csv \
    ../../../../config.json
```

**输出格式**:
```csv
Query,feature_0,feature_1,...,feature_27,Label
"SELECT * FROM users",0,5,2,...,1,0
"1' OR '1'='1",1,3,8,...,0,1
```

### 步骤 3: 训练模型

```bash
cd tools/model_train
python model_train.py
```

### 步骤 4: 修改DFA规则（可选）

仅在需要修改SQL模式规则时执行：

```bash
cd tools/dfa_compile
python dfa_compiler.py profile_sqli_fixed.txt
```

**训练输出**:
- `model_tokens_rf.onnx` - ONNX格式的模型文件
- 准确率、分类报告、混淆矩阵等评估指标

## 工具说明

### dfa_compiler.py

**功能**: 将SQL模式规则编译为DFA状态表

**主要作用**:
- 将输入字符串进行 token 化（分词）
- 将SQL模式规则转换为DFA状态转移表
- 生成用于DFA引擎的二进制表文件

**使用场景**:
- 仅在修改DFA规则时需要运行
- 预编译的表文件已包含在项目中

**使用方法**:
```bash
cd tools/dfa_compile
python dfa_compiler.py profile_sqli_fixed.txt
```

**输出文件**:
- `transition_table.bin` - 状态转移表
- `output_table.bin` - 输出表
- `eod_table.bin` - 结束检测表

### csv_to_vector_tool

**功能**: 将CSV中的Query列转换为28维特征向量

**特性**:
- DFA分词（支持SQL关键字、运算符、字符串等）
- 滑窗特征提取（窗口大小=5）
- 批处理支持
- 进度显示和错误统计

**命令行参数**:
```bash
csv_to_vector_tool [输入CSV] [输出CSV] [配置文件]
```

**注意事项**:
- 需要 `config.json` 和 DFA表文件 (`assets/data/*.bin`)
- 输入CSV必须包含 `Query` 和 `Label` 列
- 支持多种编码（UTF-8, GBK, Latin-1等）

### model_train.py

**功能**: 训练随机森林分类器并导出ONNX模型

**配置参数**:
```python
FEATURE_DIM = 28              # 特征维度
RF_PARAMS = {
    'n_estimators': 10,       # 树的数量
    'max_depth': 8,           # 最大深度
    'random_state': 42,
    'class_weight': 'balanced'
}
```

**使用方法**:
```bash
# 使用默认路径
python model_train.py

# 指定向量CSV
python model_train.py --csv /path/to/training_vectors.csv
```

## 输出文件

| 文件 | 说明 | 位置 |
|------|------|------|
| `transition_table.bin` | DFA状态转移表 | `assets/data/` |
| `output_table.bin` | DFA输出表 | `assets/data/` |
| `eod_table.bin` | DFA结束检测表 | `assets/data/` |
| `training_vectors.csv` | 28维特征向量 | `tools/model_train/train_data/` |
| `model_tokens_rf.onnx` | 训练好的模型 | `tools/model_train/` |

## 故障排查

### 编译错误
- **问题**: 找不到头文件
- **解决**: 确保在 train_data 目录执行编译：
  ```bash
  cd tools/model_train/train_data
  mkdir build && cd build
  cmake ..
  make
  ```

### 运行时错误
- **问题**: 无法加载DFA表文件
- **解决**: 检查 `config.json` 中的路径是否正确
- **问题**: CSV编码错误
- **解决**: 工具会自动尝试多种编码，如仍失败请手动转码为UTF-8

### 训练错误
- **问题**: 向量CSV缺少特征列
- **解决**: 确保运行了 `csv_to_vector_tool` 生成正确的向量文件
- **问题**: sklearn或onnx相关库缺失
- **解决**: `pip install scikit-learn skl2onnx onnxruntime`

## 开发说明

### 添加新的特征维度

修改以下文件：
1. `config.json`: `feature_dim`
2. `tools/model_train/train_data/csv_to_vector_tool.c`: `FEATURE_DIM` 宏
3. `tools/model_train/model_train.py`: `Config.FEATURE_DIM`

### 修改DFA规则

1. 编辑规则文件：`tools/dfa_compile/profile_sqli_fixed.txt`
2. 运行 DFA 编译器：
   ```bash
   cd tools/dfa_compile
   python dfa_compiler.py profile_sqli_fixed.txt
   ```
3. 将生成的表文件复制到 `assets/data/` 目录

## 许可证

源代码 **Private Repository Source Code License 1.0**  [LICENSE](LICENSE)。