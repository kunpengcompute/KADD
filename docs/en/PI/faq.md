# FAQs

This document describes the frequently asked questions (FAQs) and solutions related to the KADD PI engine.

---

## Installation & Deployment

### Missing Dynamic Link Library

**Question**

On Kunpeng 920 processor, after the environment is deployed, running the program in a newly opened terminal results in missing dynamic link library errors.

**Answer**

Environment variables do not take effect automatically in a new terminal. Reconfigure the environment variables according to the [Installation Guide](installation_guide.md).

---

### Missing _ctypes Module

**Question**

Running the program shows `ModuleNotFoundError: No module named '_ctypes'`.

**Answer**

This is because the Python installation is incomplete. Check if `libffi-devel` is installed on the system, and add the `--with-system-ffi` option when compiling Python from source:

```bash
# Install libffi-devel (openEuler)
sudo yum install -y libffi-devel

# Recompile Python with --with-system-ffi
./configure --with-system-ffi
make && make install
```

---

### Missing System Libraries (GLIBC)

**Question**

Running the program shows `version GLIBC_XXX not found` or similar library version mismatch errors.

**Answer**

The system's `ldd` version is lower than the `ldd` version used when compiling the acceleration library. Due to the acceleration library's limitations, Kunpeng acceleration cannot be enabled. You can manually download the standard `onnxruntime` and run the non-accelerated version:

```bash
pip install onnxruntime==1.19.2
```

---

### Python Version Mismatch

**Question**

Installing the acceleration library whl package reports a Python version incompatibility (for example, `is not a supported wheel on this platform`), or Python version related errors occur at runtime.

**Answer**

Different Kunpeng chip models require different Python versions, and the Python version of the runtime environment must be the same as the Python version used when compiling the acceleration library whl package:

| Kunpeng Chip Model              | Required Python Version |
| ------------------------------- | ----------------------- |
| Kunpeng 920 processor           | 3.9                     |
| Kunpeng 920 new model processor | 3.9                     |
| Kunpeng 950 processor           | 3.10                    |

See the compilation environment requirements in the [Installation Guide](installation_guide.md), and recompile and redeploy the environment using the corresponding Python version.

---

## Usage

### How to Interpret the Detection Result

**Question**

How do I read the result returned by `kp_detect`?

**Answer**

The result is a dictionary. The `result` field values mean:

| result value | Meaning                    | Constant                    |
| ------------ | -------------------------- | --------------------------- |
| 1            | Jailbreak text (JAILBREAK) | `KunpengConstant.JAILBREAK` |
| 0            | Normal text (BENIGN)       | `KunpengConstant.BENIGN`    |

```python
res = kp_detect(kp, prompt)
if res["result"] == 1:
    print("Jailbreak text detected")
else:
    print("Normal text")
```

---

### Limits on Black/White Lists and Input Text

**Question**

Are there length or count limits for black/white lists and input text?

**Answer**

| Limit                                | Value      | Behavior When Exceeded                     |
| ------------------------------------ | ---------- | ------------------------------------------ |
| Black/white list entry count         | 500 each   | New entries are skipped with a warning log |
| Black/white list single entry length | 8500 chars | Raises `KpUpdateException`                 |
| Input text to detect length          | 8500 chars | Raises `KpDetectionException`              |

---

### Black/White List Matching Method

**Question**

Is the black/white list matching exact or fuzzy?

**Answer**

The black/white list uses the **MinHash LSH** approximate matching algorithm with a similarity threshold of 0.8. Text is first segmented using jieba Chinese word segmentation, then MinHash signatures are computed for approximate matching. This means highly similar text will also be matched.

---

### Using Detector Instances Across Threads

**Question**

Can multiple threads share the same detector instance?

**Answer**

It is recommended that each thread create an independent detector instance to avoid concurrency issues:

```python
# Each thread creates an independent instance
logger = logging.getLogger()
kp = kp_detector_initialization(logger)
```

---

## Performance

### How to Achieve the Best Detection Performance

**Question**

How do I reduce detection latency?

**Answer**

Use `numactl` to bind the NUMA node:

```bash
numactl --cpunodebind=0 --membind=0 python script_name.py
```

Additionally, the detector instance performs model warmup during initialization. The first detection will be slightly slower than subsequent ones.

---

## Model Training

### How to Train a Custom Model

**Question**

How do I train a new detection model with my own dataset?

**Answer**

1. Prepare training data in JSON format and store it in `train/dataset/train_data.json`
2. Download the Chinese-lert-small pretrained model and place it in `train/pretrained_model/chinese-lert-small/`
3. Modify training parameters in `train/config/config.json`
4. Run the training script:

```bash
python train/train.py
```

After training, the ONNX model is automatically exported and quantized to INT8. Copy the generated `train/onnx_model/clf_model.onnx` to `algorithm/knowledge_base/model_file/` to replace the original model.

**Training parameters:**

| Parameter   | Default | Description               |
| ----------- | ------- | ------------------------- |
| epochs      | 15      | Number of training epochs |
| lr          | 2e-5    | Learning rate             |
| batch_size  | 8       | Batch size                |
| max_length  | 512     | Maximum sequence length   |
| warmup_rate | 0.03    | Warmup ratio              |
| dropout     | 0       | Dropout rate              |
