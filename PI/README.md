# 鲲鹏亲和提示词检测

基于LERT模型的针对鲲鹏芯片加速的提示词检测项目

## 目录结构

```
kunpeng_detection
├── algorithm
│   ├── api
│   ├── codes
│   │   ├── filter
│   │   └── utils
│   └── knowledge_base
│       ├── blackwhitelist
│       └── model_file
├── deploy
└── train
    ├── config
    ├── dataset
    ├── finetuned_model
    ├── log
    ├── loss_fn
    ├── model
    ├── onnx_model
    ├── pretrained_model
    │   └── chinese-lert-small
    └── utils
```

- algorithm：提示词检测主体代码
  - api：对外接口
  - codes：检测逻辑代码
  - knowledge_base：知识库，存放黑白名单、模型文件
- deploy：依赖列表
- train：模型训练代码
  - dataset：训练数据
  - finetuned_model：存放微调后的模型文件，.bin格式
  - onnx_model：存放模型转换后的文件，.onnx格式
  - pretrained_model：存放预训练模型

## 功能概述

- 提示词检测：判断输入的prompt是否违规，result 1 为JAILBREAK，result 0 为BENIGN
- 黑白名单机制：支持动态更新黑白名单，直接匹配拦截放行
- 模型训练量化：支持模型二分类训练，导出为ONNX格式并FP32 → INT8动态量化，便于部署

## 性能指标

在标准测试集上准确率已达到99%

## 鲲鹏亲和环境要求

| 鲲鹏芯片型号      | Python版本 |
| ----------- | -------- |
| 鲲鹏920处理器    | 3.9      |
| 鲲鹏920新型号处理器 | 3.9      |
| 鲲鹏950处理器    | 3.10     |
| 其他          | 不限       |

## 快速开始

### 1、部署

请参考[安装指南](../docs/zh/PI/installation_guide.md)完成环境部署，主要步骤如下：

1. 鲲鹏920处理器、鲲鹏920新型号处理器以及鲲鹏950处理器：按照安装指南编译对应版本的onnxruntime加速库（加速库20或加速库50），并使用pip安装编译得到的whl包，配置所需的环境变量
2. 其他处理器：直接使用pip安装非加速版三方库 `pip install onnxruntime==1.19.2`
3. 安装python三方库依赖

```bash
pip install -r deploy/requirements.txt
```

部署完成后可通过`python function_test.py`验证功能，或直接输入`python test_kp_detect.py`进行单条文本测试

### 2、使用API

```python
import logging
from algorithm.api.kp_detection import kp_detect, kp_detector_initialization, kp_detector_update_blackwhitelist

# 初始化检测实例
logger = logging.getLogger()
kp = kp_detector_initialization(logger)

# 更新黑白名单
blackwhitelist = {
    "blacklist": ["这是测试文本"],
    "whitelist": ["这是测试文本"]
}
kp_detector_update_blackwhitelist(kp, blackwhitelist)

# 检测文本
prompt = "这是测试文本"
res = kp_detect(kp, prompt)
```

使用下面的命令运行脚本，以达到最大性能

```bash
# 示例脚本
numactl --cpunodebind=0 --membind=0 python 脚本名称.py
```

### 3、输出格式

```json
{
    "result": 1,
    "detect_time": "10 ms"
}
```

## 模型训练复现

### 1、训练集

训练集来源为两个，均取自huggingface

黑样本来源于**thu-coai/Safety-Prompts**中的**instruction_attack_scenarios.json**

白样本来源于**COIG-CQIA**

黑白样本各3w条，存放于`/train/dataset/train_data.json`中

### 2、预训练模型

预训练模型为**Chinese-lert-small**，取自huggingface，参数量为15.08M

### 3、训练过程

将预训练模型文件使用git clone放置在 `train/pretrained_model/Chinese-lert-small`文件夹下，即可一键式训练

```bash
python train/train.py
```

训练参数均可在`/train/config/config.json`中查看

## 常见错误

### 1、缺少动态链接库

在鲲鹏920处理器下，若在环境部署好后新开了终端，运行程序会出现缺少动态链接库的错误，此时可以按照[安装指南](../docs/zh/PI/installation_guide.md)重新配置环境变量解决

### 2、缺少_ctypes

当出现缺少ctypes的提示时，是因为python安装不完整，先检查系统中是否存在libffi-devel，需要在python源码编译的时候添加`--with-system-ffi` 

### 3、缺少系统库

当缺少GLIBC_等系统库时，可能是由于系统的ldd版本低于加速库编译时的ldd版本，此时由于加速库的限制，无法启用鲲鹏加速，可手动下载onnxruntime并运行未加速的版本
