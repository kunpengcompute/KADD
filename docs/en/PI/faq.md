# FAQs

This document describes the frequently asked questions (FAQs) and solutions related to the KADD PI engine.

---

## Installation & Deployment

### Q1: What Should I Do If a Dynamic Link Library Is Missing?

**Question**: On Kunpeng 920 processor, after deployment, opening a new terminal and manually activating the virtual environment results in missing dynamic link library errors when running the program.

**Solution**:

Add the acceleration library to the environment variable:

```bash
export LD_LIBRARY_PATH=project_path/acceleration_library:$LD_LIBRARY_PATH
```

Or use the deployment script with `source` (it automatically configures environment variables):

```bash
source ./deploy/deploy.sh
```

---

### Q2: What Should I Do If the _ctypes Module Is Missing?

**Question**: Running the program shows `ModuleNotFoundError: No module named '_ctypes'`.

**Solution**:

This is because the Python installation is incomplete. Check if `libffi-devel` is installed on the system, and add the `--with-system-ffi` option when compiling Python from source:

```bash
# Install libffi-devel (openEuler)
sudo yum install -y libffi-devel

# Recompile Python with --with-system-ffi
./configure --with-system-ffi
make && make install
```

---

### Q3: What Should I Do If System Libraries (GLIBC) Are Missing?

**Question**: Running the program shows `version GLIBC_XXX not found` or similar library version mismatch errors.

**Solution**:

The system's `ldd` version is lower than the `ldd` version used when compiling the acceleration library. Due to the acceleration library's limitations, Kunpeng acceleration cannot be enabled. You can manually download the standard `onnxruntime` and run the non-accelerated version:

```bash
pip install onnxruntime==1.19.2
```

---

### Q4: What Should I Do If the Python Version Does Not Match?

**Question**: The deployment script reports a Python version mismatch.

**Solution**:

Different Kunpeng chip models require different Python versions:

| Kunpeng Chip Model              | Required Python Version |
| ------------------------------- | ----------------------- |
| Kunpeng 920 processor           | 3.9                     |
| Kunpeng 920 new model processor | 3.9                     |
| Kunpeng 950 processor           | 3.10                    |

Use the `-p` parameter to specify the correct Python version:

```bash
source ./deploy/deploy.sh -p /opt/python3.9/bin/python3.9
```

---

## Usage

### Q5: How Do I Interpret the Detection Result?

**Question**: How do I read the result returned by `kp_detect`?

**Solution**:

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

### Q6: What Are the Limits for Black/White Lists and Input Text?

**Question**: Are there length or count limits for black/white lists and input text?

**Solution**:

| Limit                                | Value      | Behavior When Exceeded                     |
| ------------------------------------ | ---------- | ------------------------------------------ |
| Black/white list entry count         | 500 each   | New entries are skipped with a warning log |
| Black/white list single entry length | 8500 chars | Raises `KpUpdateException`                 |
| Input text to detect length          | 8500 chars | Raises `KpDetectionException`              |

---

### Q7: What Matching Method Does the Black/White List Use?

**Question**: Is the black/white list matching exact or fuzzy?

**Solution**:

The black/white list uses the **MinHash LSH** approximate matching algorithm with a similarity threshold of 0.8. Text is first segmented using jieba Chinese word segmentation, then MinHash signatures are computed for approximate matching. This means highly similar text will also be matched.

---

### Q8: Can a Detector Instance Be Used Across Threads?

**Question**: Can multiple threads share the same detector instance?

**Solution**:

It is recommended that each thread create an independent detector instance to avoid concurrency issues:

```python
# Each thread creates an independent instance
logger = logging.getLogger()
kp = kp_detector_initialization(logger)
```

## Performance

### Q9: How Do I Achieve the Best Detection Performance?

**Question**: How do I reduce detection latency?

**Solution**:

Use `numactl` to bind the NUMA node:

```bash
numactl --cpunodebind=0 --membind=0 python script_name.py
```

Additionally, the detector instance performs model warmup during initialization. The first detection will be slightly slower than subsequent ones.

---

## Model Training

### Q10: How Do I Train a Custom Model?

**Question**: How do I train a new detection model with my own dataset?

**Solution**:

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
