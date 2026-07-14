# 快速入门

本文给出KADD的快速入门指导。提供了工具集和demo示例，通过运行demo程序可以快速体验SQL注入检测功能，通过使用工具可以进行DFA表生成和模型训练。详细使用细节请参考《[用户指南](user_guide.md)》。

## 前提条件

请参考[安装指南](installation_guide.md)完成安装。

---

## Demo程序使用

KADD提供了5个demo程序，分别展示各个引擎的使用方法。

### 运行demo程序

运行前需要设置库搜索路径：

```bash
export LD_LIBRARY_PATH=/opt/KADD/SQLi/build/src:/opt/KADD/SQLi/util/onnxruntime-linux-aarch64-1.16.0/lib:$LD_LIBRARY_PATH
```

#### 1. SQL注入检测 - kadd_sqli_demo

完整的SQL注入检测流程演示。

```bash
cd /opt/KADD/SQLi/build/demo
./kadd_sqli_demo
```

**输出示例**：

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

**输出示例**：

```bash
输入: SELECT * FROM users WHERE id = 1

分词结果 (8 个 Token):
  [1] Token 索引: 7
  [2] Token 索引: 11
  [3] Token 索引: 8
  ...
```

#### 3. ML推理引擎 - kadd_ml_demo

使用ONNX模型进行推理。

```bash
cd /opt/KADD/SQLi/build/demo
./kadd_ml_demo
```

**输出示例**：

```bash
输入特征: [0.10 0.10 0.10 0.10 0.10 ...]
注入概率: 8.45%
✅ 推理成功
```

#### 4. 特征提取引擎 - kadd_feature_demo

Token ID序列转换为特征向量。

```bash
cd /opt/KADD/SQLi/build/demo
./kadd_feature_demo
```

**输出示例**：

```bash
Token 序列 (7 个):
  [1] Token ID: 7
  [2] Token ID: 11
  ...

特征向量 (28 维):
[12 8 15 20 11 ...]
✅ 特征提取成功
```

#### 5. 配置加载器 - kadd_config_demo

从JSON文件加载配置。

```bash
cd /opt/KADD/SQLi/build/demo
./kadd_config_demo
```

**输出示例**：

```bash
DFA 引擎:
  转移表路径: assets/data/transition_table.bin
  最大输入长度: 512

ML 引擎:
  模型路径: assets/model/model_tokens_rf.onnx
  特征维度: 28
```

## DFA表编译

使用DFA编译器将SQL模式规则编译为DFA状态表。仅在需要修改SQL模式规则时执行。

```bash
cd /opt/KADD/SQLi/tools/dfa_compile
python dfa_compiler.py profile_sqli_fixed.txt
```

出现如下回显：

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

将生成的表文件复制到 `/opt/KADD/SQLi/assets/data/` 目录替换原有文件。

---

## 模型训练

使用训练工具训练随机森林分类器并导出ONNX模型。

### 步骤1：生成训练向量

`csv_to_vector_tool`将文本内容转化为特征向量；

**编译特征生成工具**

```bash
cd /opt/KADD/SQLi/tools/model_train/train_data
export LD_LIBRARY_PATH=/opt/KADD/SQLi/lib:$LD_LIBRARY_PATH
mkdir build && cd build
cmake ..
make
```

运行工具生成28维特征向量：

- 在 build 目录下运行

```bash
./csv_to_vector_tool
```

- 指定参数 分别对应 原始sql注入文本 转换向量后的目的地址  dfa feature 配置文件地址

```bash
./csv_to_vector_tool \
    ../total_training_dataset_v4_pinnacle.csv \
    ../training_vectors.csv \
    /opt/KADD/SQLi/config.json
```

**输出格式**：

```bash
========== 统计信息 ==========
总行数 
成功: 
失败: 
标签分布: Label 0 = , Label 1 = 
输出文件: 
==============================
```

```csv
Query,feature_0,feature_1,...,feature_27,Label
"SELECT * FROM users",0,5,2,...,1,0
"1' OR '1'='1",1,3,8,...,0,1
```

### 步骤2：训练模型

使用Python脚本训练模型：

- 默认CSV路径

```bash
cd /opt/KADD/SQLi/tools/model_train
python model_train.py
```

- 指定向量CSV路径

```bash
cd /opt/KADD/SQLi/tools/model_train
python model_train.py --csv /opt/KADD/SQLi/tools/model_train/train_data/training_vectors.csv
```

**训练输出**：

- `model_tokens_rf.onnx` - ONNX格式的模型文件
- 准确率、分类报告、混淆矩阵等评估指标

```bash
数据源: /opt/KADD/SQLi/tools/model_train/train_data/training_vectors.csv (向量CSV)
向量CSV 编码: latin-1
向量数据: 44074 样本, 特征维度: 28, 标签分布: 0=23012, 1=21062
X=(44074, 28), y=(44074,), 0=23012, 1=21062
Train: 35259, Test: 8815, Verify: 100
X_train: (35259, 28), 范围 0.0~9789.0, 均值 55.62
y_train: Counter({np.int32(0): 18409, np.int32(1): 16850})
============================================================
训练随机森林模型...
  Train Accuracy: 
  Test Accuracy: 
  Verify Accuracy: 

分类报告 (Test):
              precision    recall  f1-score   support

     Label 0       
     Label 1       

    accuracy       
   macro avg       
weighted avg       

混淆矩阵:
[[4596    7]
 [ 399 3813]]
```

---
