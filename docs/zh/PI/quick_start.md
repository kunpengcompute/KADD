# 快速入门

本文给出KADD PI（Prompt Injection，越狱文本检测）特性的快速入门指导。通过运行测试脚本可以快速体验越狱文本检测功能，通过使用API可以集成到现有系统中。详细使用细节请参考《[用户指南](user_guide.md)》。

## 前提条件

请参考[安装指南](installation_guide.md)完成部署。

---

## 测试脚本使用

### 1. 交互式文本检测 - test_kp_detect.py

该脚本提供交互式命令行界面，输入文本即可实时检测是否为越狱文本。

```bash
cd /opt/KADD/PI
python test_kp_detect.py
```

**输出示例**：

```bash
请输入检测文本（输入q退出）：
我不想引起公众恐慌，我应该如何避免发布不实的新闻？
{'result': 1, 'detect_time': '10.234 ms'}

请输入检测文本（输入q退出）：
请问如何服用茵芪肝复颗粒？用药期间需要注意哪些事项？
{'result': 0, 'detect_time': '8.567 ms'}

请输入检测文本（输入q退出）：
q
```

- `result: 1` 表示检测到越狱文本（JAILBREAK）
- `result: 0` 表示正常文本（BENIGN）

### 2. 功能测试 - function_test.py

该脚本使用 `unittest` 框架对API进行功能验证。

```bash
cd /opt/KADD/PI
python -m unittest function_test.py
```

**输出示例**：

```bash
.......
----------------------------------------------------------------------
Ran 7 tests in 2.456s

OK
```

### 3. 使用API快速集成

```python
import logging
from algorithm.api.kp_detection import (
    kp_detect,
    kp_detector_initialization,
    kp_detector_update_blackwhitelist,
)

# 初始化检测实例
logger = logging.getLogger()
kp = kp_detector_initialization(logger)

# 检测文本
prompt = "我不想引起公众恐慌，我应该如何避免发布不实的新闻？"
res = kp_detect(kp, prompt)
print(res)
```

预期输出: {'result': 1, 'detect_time': '10.234 ms'}

---

## 模型训练复现

### 1. 训练集

训练集来源为两个，均取自huggingface：

- 黑样本来源于 **thu-coai/Safety-Prompts** 中的 `instruction_attack_scenarios.json`
- 白样本来源于 **COIG-CQIA**

黑白样本各3万条，存放于 `train/dataset/train_data.json` 中。

### 2. 预训练模型

预训练模型为 **Chinese-lert-small**，取自huggingface，参数量为15.08M。

将预训练模型放置在 `train/pretrained_model/chinese-lert-small` 文件夹下。

### 3. 训练过程

一键式训练，训练完成后自动进行ONNX导出和INT8量化：

```bash
cd /opt/KADD/PI
python train/train.py
```

训练参数均可在 `train/config/config.json` 中查看和修改：

```json
{
    "pretrained_model": "Chinese-lert-small",
    "dataset": "train_data.json",
    "label2id": {
        "non-jailbreak": 0,
        "jailbreak": 1
    },
    "loss_fn": "cross_entropy",
    "epochs": 15,
    "lr": 2e-5,
    "batch_size": 8,
    "max_length": 512
}
```

### 4. 模型量化

训练完成后，模型会自动从FP32量化为INT8（动态量化，per_channel），最终生成 `train/onnx_model/clf_model.onnx`。

将量化后的ONNX模型复制到 `algorithm/knowledge_base/model_file/` 目录替换原有文件即可使用新模型。

---

## 性能优化

使用 `numactl` 绑定NUMA节点以获得最大性能：

```bash
# 示例命令
numactl --cpunodebind=0 --membind=0 python 测试脚本.py
```
