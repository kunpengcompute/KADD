# Quick Start

<!-- md-trans-meta sourceCommit=e6b62e0b6301d6001b149147e843b5b6a34d9d54 translatedAt=2026-09-22T04:14:05.828Z pushedAt=2026-09-23T01:53:59.384Z -->

This document provides a quick start guide for the KADD Prompt Injection (PI) feature. You can quickly experience the jailbreak text detection capability by running the test scripts, and integrate it into your existing system by using APIs. For detailed usage, see [User Guide](user_guide.md).

## Prerequisites

You have completed the deployment by following instructions in [Installation Guide](installation_guide.md).



## Test Script Usage

### 1. Interactive Text Detection (`test_kp_detect.py`)

This script provides an interactive command-line interface. Enter text to detect in real time whether it is jailbreak text.

```bash
cd /opt/KADD/PI
python test_kp_detect.py
```

**Output example:**

```bash
Please enter the text to be detected (enter q to exit):
I do not want to cause public panic. How should I avoid publishing false news?
{'result': 1, 'detect_time': '10.234 ms'}

Please enter the text to be detected (enter q to exit):
How should I take Yinqi Ganfu Granules? What precautions should I take during medication?
{'result': 0, 'detect_time': '8.567 ms'}

Please enter the text to be detected (enter q to exit):
q
```

- `result: 1` indicates jailbreak text detected (JAILBREAK).
- `result: 0` indicates normal text (BENIGN).

### 2. Function Tests (`function_test.py`)

This script uses the `unittest` framework to perform functional verification on APIs.

```bash
cd /opt/KADD/PI
python -m unittest function_test.py
```

**Output example:**

```bash
.................
----------------------------------------------------------------------
Ran 17 tests in 2.456s

OK
```

### 3. Quick Integration Using APIs

```python
import logging
from algorithm.api.kp_detection import (
    kp_detect,
    kp_detector_initialization,
    kp_detector_update_blackwhitelist,
)

# Initialize the detection instance.
logger = logging.getLogger()
kp = kp_detector_initialization(logger)

# Detect text.
prompt = "I do not want to cause public panic. How should I avoid publishing false news?"
res = kp_detect(kp, prompt)
print(res)
```

Expected output: `{'result': 1, 'detect_time': '10.234 ms'}`



## Model Training Reproduction

### 1. Training Set

The training set comes from two sources, both taken from Hugging Face.

- Blocklist samples come from `instruction_attack_scenarios.json` in **thu-coai/Safety-Prompts**.
- Trustlist samples come from **COIG-CQIA**.

There are 30,000 blocklist and 30,000 trustlist samples, stored in `train/dataset/train_data.json`.

### 2. Pretrained Model

The pretrained model is **Chinese-lert-small**, obtained from Hugging Face, with 15.08 million parameters.

Use `git clone` to place the pretrained model files under the `train/pretrained_model/Chinese-lert-small` folder.

### 3. Training Process

One-click training is performed, with automatic ONNX export and INT8 quantization upon training completion.

```bash
cd /opt/KADD/PI
python train/train.py
```

All training parameters can be viewed and modified in `train/config/config.json`.

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

### 4. Model Quantization

After training is complete, the model is automatically quantized from FP32 to INT8 (dynamic quantization, per_channel), and finally `train/onnx_model/clf_model.onnx` is generated.

Copy the quantized ONNX model to the `algorithm/knowledge_base/model_file/` directory to replace the original file, and the new model can then be used.



## Performance Optimization

Use `numactl` to bind NUMA nodes for maximum performance.

```bash
# Example command
numactl --cpunodebind=0 --membind=0 python test_script.py
```

## Change History

| Version | Date | Description |
| -------- | -------- | -------- |
| 01 | 2026-09-30 | This is the first official release. |
