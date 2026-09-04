# KADD Tools 工具集

本目录包含 KADD 项目相关的数据处理和模型训练工具。

## 目录结构

```
tools/
├── verify_sve.sh                  # SVE 行为矩阵一键验证脚本（编译期场景 1-5 + 产物检查）
├── dfa_compile/                   # DFA 编译器
│   └── dfa_compiler.py            # DFA 编译器脚本（规则 → 二进制状态表）
└── model_train/                   # 模型训练目录
    ├── model_train.py             # 统一训练脚本（支持 RF / DL_CNN 两种模型）
    ├── kadd_rf_format.py          # KADD-RF v1 跨平台模型格式导出与读取
    ├── kadd_cnn_format.py         # KADD-CNN v1 跨平台模型格式导出与读取
    ├── prepare_rf_benchmark.py    # RF 模型训练 + 严格留出基准数据集生成
    ├── prepare_cnn_benchmark.py   # CNN 模型导出 + token 评估数据集重建
    └── train_data/                # 训练数据 & C 工具子目录
        ├── CMakeLists.txt         # C 工具构建配置
        ├── csv_to_vector_tool.c   # CSV → 28 维特征向量转换工具
        └── train_data.csv         # 训练数据（Query + Label）
```

## 工作流程

```
┌─────────────────────────────────────────────────────────────┐
│  0. DFA 规则编译（可选，仅修改规则时需要）                      │
│     dfa_compiler.py - 将 SQL 模式规则编译为 DFA 状态表        │
│     输出: transition_table.bin, output_table.bin, eod_table.bin│
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│  1. 原始数据                                                 │
│     train_data.csv                                           │
│     (Query 列 + Label 列)                                     │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│  2. C 代码处理 (csv_to_vector_tool)                           │
│     - DFA tokeni`   (src/kadd_dfa_engine.c)                 │
│     - 特征提取       (src/kadd_feature_engine.c)             │
│     输出: training_vectors.csv                               │
│     (feature_0~feature_27 + Label)                            │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│  3. Python 训练 (model_train.py)                             │
│     ┌───────────────────┬────────────────────────────────┐  │
│     │ RF 模式           │ DL_CNN 模式                     │  │
│     │ 随机森林 28 维特征 │ TextCNN 300 维 token 序列       │  │
│     │ → .onnx + .krf   │ → .onnx + .kcn + 评估数据集     │  │
│     └───────────────────┴────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

## 快速开始

### 环境要求

- **编译环境**: Linux (x86_64, aarch64) / Windows (x64)
- **Python**: 3.8+
- **C 依赖库**:
  ```bash
  # 安装 cJSON（配置文件解析）
  sudo apt-get install libcjson-dev
  # 或使用项目内置 util/cJSON（自动回退）
  ```
- **Python 依赖库**:
  ```bash
  # 模型训练（RF + DL_CNN）
  pip install torch onnx onnxruntime pandas numpy scikit-learn skl2onnx
  # DFA 编译（可选，仅修改 DFA 规则时需要）
  pip install regex
  ```

### 步骤 1: 编译 C 工具

```bash
cd tools/model_train/train_data
mkdir build && cd build
cmake ..
make
```

编译成功后，会生成可执行文件 `csv_to_vector_tool`。

### 步骤 2: 生成训练向量

```bash
# 在 build 目录下直接运行（使用默认路径）
./csv_to_vector_tool

# 或指定参数
./csv_to_vector_tool \
    ../train_data.csv \
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

# 随机森林模型
python model_train.py --model-type rf

# TextCNN 深度学习模型
python model_train.py --model-type dl_cnn
```

### 步骤 4: 生成基准数据集（可选）

```bash
cd tools/model_train

# RF 基准：训练模型 + 生成严格留出测试集 (.krf + benchmark CSV)
python prepare_rf_benchmark.py

# CNN 基准：导出 .kcn + 重建 token 评估数据集
python prepare_cnn_benchmark.py
```

### 步骤 5: 验证 SVE 支持（可选，仅 aarch64）

```bash
# 从项目根目录运行
./tools/verify_sve.sh
```

验证 CMake 编译期 SVE 决策逻辑（场景 1-5），确保 aarch64 SVE 加速与 ONNX 回退路径均正确。

### 步骤 6: 修改 DFA 规则（可选）

仅在需要修改 SQL 模式规则时执行：

```bash
cd tools/dfa_compile
python dfa_compiler.py profile_sqli_fixed.txt
```

## 工具说明

### dfa_compiler.py

**功能**: 将 SQL 模式规则编译为 DFA 状态表

**主要作用**:
- 将输入字符串进行 token 化（分词）
- 将 SQL 模式规则转换为 DFA 状态转移表
- 生成用于 DFA 引擎的二进制表文件

**使用场景**:
- 仅在修改 DFA 规则时需要运行
- 预编译的表文件已包含在项目中（`assets/data/*.bin`）

**输出文件**:
- `transition_table.bin` — 状态转移表
- `output_table.bin` — 输出表
- `eod_table.bin` — 结束检测表

### csv_to_vector_tool

**功能**: 将 CSV 中的 Query 列转换为 28 维特征向量

**特性**:
- DFA 分词（支持 SQL 关键字、运算符、字符串等）
- 滑窗特征提取（窗口大小 = 5）
- 批处理支持
- 进度显示和错误统计

**命令行参数**:
```bash
csv_to_vector_tool [输入CSV] [输出CSV] [配置文件]
```

**注意事项**:
- 需要 `config.json` 和 DFA 表文件（`assets/data/*.bin`）
- 输入 CSV 必须包含 `Query` 和 `Label` 列
- 支持多种编码（UTF-8, GBK, Latin-1 等）

### model_train.py

**功能**: 统一训练脚本，支持 RF 和 DL_CNN 两种模型

**RF 模式**:
- 随机森林分类器（28 维特征向量输入）
- 同时输出 ONNX (`.onnxE) 和跨平台原生格式 (`.krf`)

**DL_CNN 模式**:
- TextCNN 模型（300 维 token 序列输入）
- 网络结构: Embedding(29,32) → Conv1d(k=2,3,4,5) → Concat → FC → Softmax
- 同时输出 ONNX (`.onnxE)、跨平台原生格式 (`.kcn`) 和独立测试集

**配置参数**:
```python
# RF 参数
RF_PARAMS = {
    'n_estimators': 10,       # 树的数量
    'max_depth': 8,           # 最大深度
    'random_state': 42,
    'class_weight': 'balanced'
}

# CNN 参数
SEQ_DIM = 300                # 序列长度
VOCAB_SI` = 29              # 词表大小
EMBED_DIM = 32               # 嵌入维度
CHANNELS = 64                # 卷积通道数
KERNELS = (2, 3, 4, 5)       # 卷积核大小
```

**使用方法**:
```bash
python model_train.py --model-type rf        # 随机森林
python model_train.py --model-type dl_cnn    # TextCNN
```

### kadd_rf_format.py

**功能**: KADD-RF v1 跨平台模型格式的导出与读取

**文件格式**:
- 固定宽度、小端序二进制格式
- Magic: `KADDRF1` + null byte
- 包含树节点结构、特征索引、阈值等信息

**主要 API**:
- `export_kadd_rf(model, output_path)` — 导出 sklearn RandomForestClassifier 到 .krf
- `load_kadd_rf(path)` — 读取 .krf 模型
- `export_kadd_dataset(...)` — 导出训练数据集格式
- `assert_export_parity(...)` — 验证导出一致性

**使用场景**:
- 在 x86 训练主机上导出模型，供 C 运行时在 x86-64 或 AArch64/SVE 上消费
- 由 `prepare_rf_benchmark.py` 和 `model_train.py --model-type rf` 自动调用

### kadd_cnn_format.py

**功能**: KADD-CNN v1 跨平台 TextCNN 模型格式的导出与读取

**文件格式**:
- 固定宽度、小端序二进制格式
- Magic: `KADDCN1` + null byte
- 严格限定 TextCNN 网络结构（Embedding → Conv1d → Concat → FC → Softmax）

**模型结构约束**:
```
float32[batch, 300]
  → Cast(int64) → Embedding(29, 32) → Transpose
  → {Conv1d(32, 64, k), ReLU, ReduceMax(axis=2)} for k=2,3,4,5
  → Concat(256) → Gemm(256, 64) → ReLU
  → Gemm(64, 2) → Softmax
```

**主要 API**:
- `export_kadd_cnn(onnx_path, output_path)` — 从 ONNX 导出到 .kcn
- `load_kadd_cnn(path)` — 读取 .kcn 模型
- `assert_export_parity(...)` — 验证导出一致性

**使用场景**:
- 由 `prepare_cnn_benchmark.py` 和 `model_train.py --model-type dl_cnn` 自动调用
- 仅接受项目固定的 TextCNN 图结构，防止不兼容模型被静默导出

### prepare_rf_benchmark.py

**功能**: 训练 KADD-RF 模型并生成严格留出基准测试数据集

**特性**:
- 消费预计算的 `training_vectors.csv`，无需 DFA/特征库
- 自动生成 train/test 划分（固定随机种子保证可重复）
- 导出 .krf 模型和基准数据集
- 输出分类报告和混淆矩阵

**使用方法**:
```bash
cd tools/model_train
python prepare_rf_benchmark.py
```

### prepare_cnn_benchmark.py

**功能**: 导出 TextCNN 为 KADD-CNN 格式并重建 token 评估数据集

**特性**:
- 绑定 DFA tokeni`r（ctypes 调用 C 动态库）
- 从原始 `Query, Label` CSV 重建 token 序列
- 导出 .kcn 模型和独立评估数据集
- 输出准确率、混淆矩阵等评估指标

**注意事项**:
- 需要编译好的 DFA 动态库（`libkadd_dfa_engine.so/dll`）
- 数据集为真实留出集仅当 ONNX 模型使用相同过滤行和分割参数训练

**使用方法**:
```bash
cd tools/model_train
python prepare_cnn_benchmark.py
```

### verify_sve.sh

**功能**: SVE 行为矩阵一键验证脚本

**验证场景**:
| 场景 | 条件 | 预期行为 |
|------|------|----------|
| 1 | SVE=ON + aarch64 + 编译器支持 | SVE 启用 |
| 2 | SVE=ON + aarch64 + 编译器不支持 | WARNING + ONNX 回退 |
| 3 | SVE=ON + 其他平台 (x86 等) | WARNING + ONNX 回退 |
| 4 | SVE=OFF + 任意平台 | ONNX 后端 |
| 5 | 任意 + ONNX Runtime 缺失 | FATAL_ERROR |

**使用方法**:
```bash
# 从项目根目录运行
./tools/verify_sve.sh

# 或指定 ONNX Runtime 路径
ONNXRUNTIME_HOME=/path/to/ort ./tools/verify_sve.sh
```

## 输出文件

| 文件 | 说明 | 生成工具 | 位置 |
|------|------|----------|------|
| `transition_table.bin` | DFA 状态转移表 | dfa_compiler.py | `assets/data/` |
| `output_table.bin` | DFA 输出表 | dfa_compiler.py | `assets/data/` |
| `eod_table.bin` | DFA 结束检测表 | dfa_compiler.py | `assets/data/` |
| `training_vectors.csv` | 28 维特征向量 | csv_to_vector_tool | `tools/model_train/train_data/` |
| `model_tokens_rf.onnx` | RF ONNX 模型 | model_train.py --model-type rf | `assets/model/` |
| `model_tokens_rf.krf` | RF 跨平台原生模型 | model_train.py --model-type rf | `assets/model/` |
| `model_tokens_dl_cnn.onnx` | CNN ONNX 模型 | model_train.py --model-type dl_cnn | `assets/model/` |
| `model_tokens_dl_cnn.kcn` | CNN 跨平台原生模型 | model_train.py --model-type dl_cnn | `assets/model/` |

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
- **问题**: 无法加载 DFA 表文件
- **解决**: 检查 `config.json` 中的路径是否正确
- **问题**: CSV 编码错误
- **解决**: 工具会自动尝试多种编码，如仍失败请手动转码为 UTF-8

### 训练错误
- **问题**: 向量 CSV 缺少特征列
- **解决**: 确保运行了 `csv_to_vector_tool` 生成正确的向量文件
- **问题**: sklearn 或 onnx 相关库缺失
- **解决**: `pip install torch scikit-learn skl2onnx onnx onnxruntime`
- **问题**: DL_CNN 训练失败
- **解决**: 确认已安装 PyTorch (`pip install torch`) 且 CUDA/CPU 环境正常

## 开发说明

### 添加新的特征维度

修改以下文件：
1. `config.json`: `feature_dim`
2. `tools/model_train/train_data/csv_to_vector_tool.c`: `FEATURE_DIM` 宏
3. `tools/model_train/model_train.py`: `Config.FEATURE_DIM`

### 修改 DFA 规则

1. 编辑规则文件：`tools/dfa_compile/profile_sqli_fixed.txt`
2. 运行 DFA 编译器：
   ```bash
   cd tools/dfa_compile
   python dfa_compiler.py profile_sqli_fixed.txt
   ```
3. 将生成的表文件复制到 `assets/data/` 目录

### 跨平台模型格式

项目定义了两种跨平台二进制模型格式，用于在 x86 训练主机和 AArch64/SVE 推理目标之间传递模型：

| 格式 | Magic | 模型类型 | 导出模块 |
|------|-------|----------|----------|
| KADD-RF v1 | `KADDRF1` + null | 随机森林 | `kadd_rf_format.py` |
| KADD-CNN v1 | `KADDCN1` + null | TextCNN | `kadd_cnn_format.py` |

两种格式均为固定宽度小端序，包含模型参数和 CRC32 校验，确保导出一致性。

## 许可证

源代码 **Private Repository Source Code License 1.0** — [LICENSE](../../LICENSE)。