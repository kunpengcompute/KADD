# 快速入门

本文给出KADD的快速入门指导。提供了工具集和demo示例，通过运行demo程序可以快速体验SQL注入检测功能，通过使用工具可以进行DFA表生成和模型训练。详细使用细节请参考《[用户指南](user_guide.md)》。

## 前提条件

请参考[安装指南](installation_guide.md)完成安装。

---

## Demo程序使用

KADD提供了6个demo程序，分别展示各个引擎的使用方法。

### 运行demo程序

运行前需要设置库搜索路径，并注入模型HMAC密钥（原生`.krf`/`.kcn`模型强制HMAC-SHA256认证，未注入密钥将拒绝加载原生模型）：

```bash
export LD_LIBRARY_PATH=/opt/KADD/SQLi/build/src:/opt/KADD/SQLi/util/onnxruntime-linux-aarch64-1.28.0/lib:$LD_LIBRARY_PATH
export KADD_MODEL_HMAC_KEY=74fab58d87dc235ee049b6faf21b44825dba1d9dc05bbc14aeded49cb211a1db
```

也可以直接source `build.sh`生成的环境文件，等价于上面两条：

```bash
source /opt/KADD/SQLi/build/kadd_env.sh
```

![](../../public_sys-resources/icon-note.gif) **说明：**

- 上面的密钥是仓库自带的**公开开发密钥**，仓库内模型正是用它签名的，仅用于构建、测试与功能体验，不具备保密性。
- 未注入密钥时，原生模型会被拒绝加载并在stderr打印ERROR，随后回退ONNX（丢失SVE加速）；若`config.json`只配置了原生模型，或程序直接调用`kadd_rf_model_load()`/`kadd_cnn_model_load()`，则加载失败。
- 生产部署必须用自有密钥重新签名模型（见下文《模型训练》的签名步骤），并通过KMS或受限权限的env文件注入`KADD_MODEL_HMAC_KEY`。

#### 1. SQL注入检测 - kadd_sqli_demo

完整的SQL注入检测流程演示，支持ML（随机森林）和DL（CNN）两种推理模式。

```bash
cd /opt/KADD/SQLi/build/demo
./kadd_sqli_demo
```

##### 预期输出

```bash
=== KADD SQL注入检测引擎演示 ===

测试1：正常输入
输入: id=1
结果: 安全输入 (概率: 0.02%)

测试2：SQL注入攻击
输入: id=1' OR '1'='1
结果: 检测到SQL注入! (概率: 98.54%)

演示完成!
```

#### 2. DFA分词引擎 - kadd_dfa_demo

将字符串分词为Token索引序列。

```bash
cd /opt/KADD/SQLi/build/demo
./kadd_dfa_demo
```

##### 预期输出

```bash
输入: SELECT * FROM users WHERE id = 1

分词结果 (8 个 Token):
  [1] Token 索引: 7
  [2] Token 索引: 11
  [3] Token 索引: 8
  ...
```

#### 3. ML推理引擎 - kadd_ml_demo

使用ML模型进行推理。SVE可用时优先加载原生RF二进制格式，失败则回退ONNX Runtime。

```bash
cd /opt/KADD/SQLi/build/demo
./kadd_ml_demo
```

##### 预期输出

```bash
输入特征: [0.10 0.10 0.10 0.10 0.10 ...]
注入概率: 8.45%
推理成功
```

#### 4. 特征提取引擎 - kadd_feature_demo

Token ID序列转换为特征向量。

```bash
cd /opt/KADD/SQLi/build/demo
./kadd_feature_demo
```

##### 预期输出

```bash
Token 序列 (7 个):
  [1] Token ID: 7
  [2] Token ID: 11
  ...

特征向量 (28 维):
[12 8 15 20 11 ...]
特征提取成功
```

#### 5. 统一推理引擎 - kadd_inference_demo

使用ONNX Runtime进行模型推理，支持RF和CNN两种模型类型。

```bash
cd /opt/KADD/SQLi/build/demo
./kadd_inference_demo
```

##### 预期输出

```bash
加载模型: assets/model/model_tokens_rf.onnx
输入特征: [0.10 0.10 0.10 0.10 0.10 ...]
注入概率: 8.45%
推理成功
```

#### 6. 配置加载器 - kadd_config_demo

从JSON文件加载配置。

```bash
cd /opt/KADD/SQLi/build/demo
./kadd_config_demo
```

##### 预期输出

```bash
引擎模式: ml
DFA 表路径:
  transition_table: assets/data/transition_table.bin
  output_table: assets/data/output_table.bin
  eod_table: assets/data/eod_table.bin

模型路径:
  model_rf (native): assets/model/model_tokens_rf.krf
  model_rf (ONNX):   assets/model/model_tokens_rf.onnx
  model_cnn (native): assets/model/model_tokens_dl_cnn.kcn
  model_cnn (ONNX):   assets/model/model_tokens_dl_cnn.onnx
```

## DFA表编译

使用DFA编译器将SQL模式规则编译为DFA状态表。仅在需要修改SQL模式规则时执行。

```bash
cd /opt/KADD/SQLi/tools/dfa_compile
python dfa_compiler.py profile_sqli_fixed.txt
```

### 预期输出

```bash
================================================================================
DFA 编译器
================================================================================

[1/5] 加载 DFA 文件...
      加载了 8023 个状态

[2/5] 解析转移规则...

[3/5] 解析特殊指令...
      展开了 39941 处特殊指令，新增 1641 个 synthetic state，2 个普通终止态

[4/5] 构建转移表...

[5/5] 保存编译结果...
      转移表: /opt/KADD/SQLi/assets/data/transition_table.bin
      输出表: /opt/KADD/SQLi/assets/data/output_table.bin
      EOD表 : /opt/KADD/SQLi/assets/data/eod_table.bin

================================================================================
编译完成！
  状态数量: 10100
  转移表大小: 10100 x 256
  输出目录: /opt/KADD/SQLi/assets/data

生成的文件:
  - transition_table.bin    二进制转移表
  - output_table.bin        边界输出表
  - eod_table.bin           EOD 输出表
================================================================================
```

**输出文件**：

- `transition_table.bin` - DFA状态转移表
- `output_table.bin` - DFA输出表
- `eod_table.bin` - DFA结束检测表

将生成的表文件复制替换到 `/opt/KADD/SQLi/assets/data/` 目录下的DFA表，完成更新。

---

## 模型训练

KADD使用 `model_train.py` 统一训练脚本，支持两种模型类型：

| 模型类型 | 参数值 | 输入特征 | 输出文件 | 说明 |
|---------|--------|---------|---------|------|
| 随机森林 | `rf` | 28维特征向量 | `.onnx` + `.krf` | SVE优先加载原生格式 |
| TextCNN | `dl_cnn` | 300维token序列 | `.onnx` + `.kcn` | SVE优先加载原生格式 |

### 前提条件

#### 1. 安装Python依赖

```bash
pip install torch onnx onnxruntime pandas numpy scikit-learn skl2onnx
```

#### 2. 准备C动态库（DFA分词 + 特征提取）

```bash
cp /opt/KADD/SQLi/build/src/libkadd_dfa_engine.so /opt/KADD/SQLi/tools/model_train/
cp /opt/KADD/SQLi/build/src/libkadd_feature_engine.so /opt/KADD/SQLi/tools/model_train/
```

### 训练随机森林模型（RF）

最常用的模型类型，适用于低延迟场景。

1. 使用默认配置和内置数据路径训练模型。

    ```bash
    cd /opt/KADD/SQLi/tools/model_train
    python model_train.py --model-type rf
    ```

2. 指定自定义数据文件训练模型。

    ```bash
    python model_train.py --model-type rf \
        --data /path/to/your/training_data.csv \
        --data-format raw
    ```

3. 指定模型输出路径训练模型。

    ```bash
    python model_train.py --model-type rf \
        --model-output /opt/KADD/SQLi/assets/model/model_tokens_rf.onnx
    ```

4. 不使用 `config.json` 默认路径时，指定DFA表路径训练模型。

    ```bash
    python model_train.py --model-type rf \
        --transition /opt/KADD/SQLi/assets/data/transition_table.bin \
        --output-table /opt/KADD/SQLi/assets/data/output_table.bin \
        --eod /opt/KADD/SQLi/assets/data/eod_table.bin
    ```

5. 为新生成的原生模型签名（必需）。

    训练产出的`.krf`**未签名**，加载器会直接拒绝（fail-closed），必须用`rf_sign.py`追加HMAC-SHA256尾部：

    ```bash
    export KADD_MODEL_HMAC_KEY=$(openssl rand -hex 32)   # 生产环境自行生成并妥善保管
    cd /opt/KADD/SQLi/tools/rf_sign
    python rf_sign.py --in-place /opt/KADD/SQLi/assets/model/model_tokens_rf.krf
    python rf_sign.py --verify /opt/KADD/SQLi/assets/model/model_tokens_rf.krf
    ```

    > **注意**
    >
    > 签名密钥必须与运行期的`KADD_MODEL_HMAC_KEY`一致，更换密钥后需重新签名；ONNX格式（`.onnx`）不参与HMAC认证，无需签名。

#### 预期输出

- `model_tokens_rf.onnx` - ONNX格式模型（回退使用）
- `model_tokens_rf.krf` - 原生二进制格式（SVE可用时优先加载，**须按步骤5签名后才能被加载**）
- 准确率、分类报告、混淆矩阵等评估指标

```bash
Model type: rf
Data format: raw
Data file: train_data/train_data.csv
Model output: /opt/KADD/SQLi/assets/model/model_tokens_rf.onnx
Transition: /opt/KADD/SQLi/assets/data/transition_table.bin
Output table: /opt/KADD/SQLi/assets/data/output_table.bin
EOD: /opt/KADD/SQLi/assets/data/eod_table.bin

Loading data...
  Total samples: 44074
  Tokenized: 44074 (100.0%)
  Failed: 0
  Label distribution: {0: 23012, 1: 21062}

Splitting data...
  Train: 35259, Test: 8815

Training Random Forest...
  n_estimators: 10
  max_depth: 8

Train Accuracy: 0.9847
Test Accuracy: 0.9623

Classification Report (Test):
              precision    recall  f1-score   support

           0       0.96      0.96      0.96      4596
           1       0.96      0.96      0.96      4219

    accuracy                           0.96      8815
   macro avg       0.96      0.96      0.96      8815
weighted avg       0.96      0.96      0.96      8815

Confusion Matrix:
[[4418  178]
 [ 154 4065]]

Exporting KADD-RF format...
  model_tokens_rf.krf: 12345 bytes
  Parity check: PASS (100/100 samples match)

Training completed!
```

### 训练TextCNN模型（DL_CNN）

适用于追求更高检测准确率的场景。

1. 使用默认配置训练模型。

    ```bash
    cd /opt/KADD/SQLi/tools/model_train
    python model_train.py --model-type dl_cnn
    ```

2. 指定训练轮数。

    ```bash
    python model_train.py --model-type dl_cnn --epochs 30
    ```

3. 指定训练数据和模型输出。

    ```bash
    python model_train.py --model-type dl_cnn \
        --data /path/to/training_data.csv \
        --model-output /opt/KADD/SQLi/assets/model/model_tokens_dl_cnn.onnx
    ```

4. 为新生成的原生模型签名（必需）。

    训练产出的`.kcn`**未签名**，加载器会直接拒绝（fail-closed）：

    ```bash
    export KADD_MODEL_HMAC_KEY=$(openssl rand -hex 32)   # 需与运行环境保持一致
    cd /opt/KADD/SQLi/tools/rf_sign
    python rf_sign.py --in-place /opt/KADD/SQLi/assets/model/model_tokens_dl_cnn.kcn
    python rf_sign.py --verify /opt/KADD/SQLi/assets/model/model_tokens_dl_cnn.kcn
    ```

#### 预期输出

- `model_tokens_dl_cnn.onnx` - ONNX格式模型
- `model_tokens_dl_cnn.kcn` - 原生二进制格式（SVE可用时优先加载，**须按步骤4签名后才能被加载**）

```bash
Model type: dl_cnn
Data format: raw

Loading data...
  Total samples: 44074
  Tokenized: 44074 (100.0%)

Training TextCNN...
  Epochs: 20
  Batch size: 256
  Learning rate: 0.001

Epoch 1/20, Loss: 0.4523, Train Acc: 0.7891
Epoch 2/20, Loss: 0.3210, Train Acc: 0.8612
...
Epoch 20/20, Loss: 0.1234, Train Acc: 0.9567

Test Accuracy: 0.9712
Test F1 Score: 0.9708

Exporting KADD-CNN format...
  model_tokens_dl_cnn.kcn: 234567 bytes
  Parity check: PASS (100/100 samples match)

Training completed!
```

### 数据格式说明

训练脚本支持两种数据格式：

**1. Raw CSV格式（推荐）**

包含 `Query` 和 `Label` 两列，脚本自动调用C库进行DFA分词和特征提取。

```csv
Query,Label
"SELECT * FROM users WHERE id=1",0
"id=1' OR '1'='1",1
"admin'--",1
"hello world",0
```

**2. JSON格式**

预分词的结构化数据，适用于已分词的大规模数据集。

```json
[
  {"tokens": [7, 11, 8, 12, 8], "label": 0},
  {"tokens": [3, 15, 22, 7, 11], "label": 1}
]
```

### 完整参数列表

```bash
python model_train.py --help
```

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--model-type` | `rf` | 模型类型：`rf`、`dl_cnn` |
| `--data` | 内置CSV | 训练数据路径 |
| `--data-format` | `raw` | 数据格式：`raw`（原始CSV）或 `json`（预分词） |
| `--config` | `../../config.json` | 配置文件路径 |
| `--model-output` | 从config读取 | 模型输出路径 |
| `--transition` | 从config读取 | DFA转移表路径 |
| `--output-table` | 从config读取 | DFA输出表路径 |
| `--eod` | 从config读取 | DFA EOD表路径 |
| `--dfa-so` | 自动检测 | DFA动态库路径 |
| `--feature-so` | 自动检测 | Feature动态库路径 |
| `--epochs` | `20` | 训练轮数（仅DL模型） |

### 基准测试数据集生成

如需创建独立测试集用于后端性能对比，可使用辅助脚本（无需C动态库和PyTorch）：

1. 生成RF基准测试数据集。该数据集使用预计算的特征向量。

    ```bash
    cd /opt/KADD/SQLi/tools/model_train
    python prepare_rf_benchmark.py
    ```

2. 生成CNN基准测试数据集。该数据集需要DFA库进行分词。

    ```bash
    python prepare_cnn_benchmark.py
    ```

---
