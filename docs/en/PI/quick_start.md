# Quick Start

This document provides a quick start guide for KADD PI (Prompt Injection, jailbreak text detection). By running the test scripts, you can quickly experience the jailbreak text detection function. By using the API, you can integrate it into existing systems. For details, see [User Guide](user_guide.md).

## Prerequisites

You have completed deployment by following instructions in [Installation Guide](installation_guide.md).

---

## Test Script Usage

### 1. Interactive Text Detection - test_kp_detect.py

This script provides an interactive command-line interface for real-time text detection.

```bash
cd /opt/KADD/PI
python test_kp_detect.py
```

**Output example:**

```bash
Enter text to detect (enter q to quit):
I don't want to cause public panic. How should I avoid publishing false news?
{'result': 1, 'detect_time': '10.234 ms'}

Enter text to detect (enter q to quit):
How should I take Yinqi Ganfu Granules? What precautions should I be aware of?
{'result': 0, 'detect_time': '8.567 ms'}

Enter text to detect (enter q to quit):
q
```

- `result: 1` indicates jailbreak text (JAILBREAK)
- `result: 0` indicates normal text (BENIGN)

### 2. Functional Test - function_test.py

This script uses the `unittest` framework to verify API functionality.

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

### 3. Quick API Integration

```python
import logging
from algorithm.api.kp_detection import (
    kp_detect,
    kp_detector_initialization,
    kp_detector_update_blackwhitelist,
)

# Initialize detector instance
logger = logging.getLogger()
kp = kp_detector_initialization(logger)

# Detect text
prompt = "I don't want to cause public panic. How should I avoid publishing false news?"
res = kp_detect(kp, prompt)
print(res)
```

Output: {'result': 1, 'detect_time': '10.234 ms'}

---

## Model Training Reproduction

### 1. Training Dataset

The training data comes from two sources, both from Hugging Face:

- Black samples (jailbreak) from **thu-coai/Safety-Prompts**, specifically `instruction_attack_scenarios.json`
- White samples (benign) from **COIG-CQIA**

Approximately 30,000 samples each, stored in `train/dataset/train_data.json`.

### 2. Pretrained Model

The pretrained model is **Chinese-lert-small**, obtained from Hugging Face, with 15.08M parameters.

Place the pretrained model in the `train/pretrained_model/chinese-lert-small/` directory.

### 3. Training Process

One-click training with automatic ONNX export and INT8 quantization:

```bash
cd /opt/KADD/PI
python train/train.py
```

Training parameters can be viewed and modified in `train/config/config.json`:

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

After training, the model is automatically quantized from FP32 to INT8 (dynamic quantization, per_channel), generating `train/onnx_model/clf_model.onnx`.

Copy the quantized ONNX model to `algorithm/knowledge_base/model_file/` to replace the original file.

---

## Performance Optimization

Use `numactl` to bind the NUMA node for maximum performance:

```bash
# example command
numactl --cpunodebind=0 --membind=0 python example.py
```
