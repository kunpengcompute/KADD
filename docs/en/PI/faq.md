# FAQs

<!-- md-trans-meta sourceCommit=e6b62e0b6301d6001b149147e843b5b6a34d9d54 translatedAt=2026-09-22T04:00:16.431Z pushedAt=2026-09-22T11:39:04.327Z -->

This document describes frequently asked questions (FAQs) and solutions related to the KADD Prompt Injection (PI) feature for jailbreak text detection.



## Installation and Deployment

### Missing Dynamic Link Library

**Question**

On a Kunpeng 920 processor, after the environment is deployed, running the program in a newly opened terminal reports an error, indicating that the dynamic link library is missing.

**Answer**

Environment variables do not take effect automatically in a new terminal. Reconfigure the environment variables as described in the [Installation Guide](installation_guide.md).



### Missing _ctypes Module

**Question**

When the program runs, the following error is reported: `ModuleNotFoundError: No module named '_ctypes'`.

**Answer**

This occurs because the Python installation is incomplete. You need to check whether `libffi-devel` exists in the system, and add the `--with-system-ffi` option when compiling the Python source code.

```bash
# Install libffi-devel (openEuler).
sudo yum install -y libffi-devel

# Add --with-system-ffi when recompiling Python.
./configure --with-system-ffi
make && make install
```



### Missing System Library (glibc)

**Question**

When the program runs, errors such as `version GLIBC_XXX not found` are reported, indicating a system library version mismatch.

**Answer**

The system's `ldd` version is lower than the `ldd` version used when the acceleration library was compiled. Due to the limitations of the acceleration library, Kunpeng acceleration cannot be enabled. You can manually download the standard ONNX Runtime and run the non-accelerated version.

```bash
pip install onnxruntime==1.19.2
```



### Error When Compiling Arm Compute Library

**Question**

An error occurs when you compile Arm Compute Library using the `scons` command.

**Answer**

This may be caused by a static check error due to a high GCC version. You can use the following command:

```bash
scons Werror=0 -j8 debug=0 neon=1 opencl=0 os=linux arch=armv8a build=native
```



### Error When Compiling ONNX Runtime Source Code

**Question**

An error occurs when you compile the ONNX Runtime source code.

**Answer**

- If `Python.h` is missing, the python-devel package may not be installed on the machine or the Python environment variables may not be set.
- If libraries such as numpy are missing, the required third-party Python libraries may not be downloaded.
- If the Eigen download fails, download [Eigen](https://gitlab.com/libeigen/eigen/-/archive/e7248b26a1ed53fa030c5c459f7ea095dfd276ac/eigen-e7248b26a1ed53fa030c5c459f7ea095dfd276ac.zip) from a browser, manually place it in the `/path/onnxruntime/build/Release/_deps/eigen-subbuild/eigen-populate-prefix/src/` directory, and run the following command. Note that `/path` is a placeholder; replace it with the actual working directory first.

```bash
# Run the command in the /path/onnxruntime directory.
grep -r "be8be39fdbc6e60e94fa7870b280707069b5b81a" . | cut -d: -f1 | sort -u | xargs sed -i.bak 's/be8be39fdbc6e60e94fa7870b280707069b5b81a/32b145f525a8308d7ab1c09388b2e288312d8eba/g'
```



### Python Version Mismatch

**Question**

When installing the .whl package for the acceleration library, a Python version incompatibility error (for example, `is not a supported wheel on this platform`) is reported, or a Python version-related error occurs at runtime.

**Answer**

Different Kunpeng chip models require different Python versions, and the Python version of the runtime environment must be consistent with the Python version used for compiling the acceleration library whl package.

| Kunpeng Chip Model      | Required Python Version |
| ----------- | ---------- |
| Kunpeng 920 processor    | 3.9        |
| New Kunpeng 920 processor model | 3.9        |
| Kunpeng 950 processor    | 3.10       |

Refer to the compilation environment requirements in [Installation Guide](installation_guide.md), and use the corresponding Python version to recompile and deploy the environment.



## Usage

### How to Interpret the Detection Result

**Question**

How should the result returned by `kp_detect` be interpreted?

**Answer**

The detection result is a dictionary. The meaning of the `result` field values is as follows:

| Result Value | Meaning              | Constant                     |
| ------- | --------------- | --------------------------- |
| 1       | Jailbreak text (JAILBREAK) | `KunpengConstant.JAILBREAK` |
| 0       | Normal text (BENIGN)    | `KunpengConstant.BENIGN`    |

```python
res = kp_detect(kp, prompt)
if res["result"] == 1:
    print("Jailbreak text detected")
else:
    print("Normal text")
```



### Restrictions on the Blocklist/Trustlist and Input Text

**Question**

Are there any length or quantity restrictions on the blocklist/trustlist and input text?

**Answer**

| Restriction Item         | Limit Value     | Over-Limit Behavior                         |
| ----------- | ------- | ---------------------------- |
| Number of blocklist/trustlist entries   | 500 each | Skips new records and outputs a warning log.               |
| Length of a single blocklist/trustlist record | 8500 characters | Throws a `KpUpdateException` exception.    |
| Length of text to be detected     | 8500 characters | Throws a `KpDetectionException` exception. |



### Blocklist/Trustlist Matching Mode

**Question**

Do the blocklist and trustlist use exact matching or fuzzy matching?

**Answer**

The blocklist and trustlist use the **MinHash LSH** approximate matching algorithm with a similarity threshold of 0.8. The text is first segmented with `jieba`, and then MinHash signatures are computed for approximate matching. This means that highly similar text will also be matched.



### Using Detection Instances Across Threads

**Question**

Can multiple threads share the same detection instance?

**Answer**

It is recommended that each thread create an independent detection instance to avoid concurrency issues.

```python
# Create an independent instance for each thread.
logger = logging.getLogger()
kp = kp_detector_initialization(logger)
```



## Performance

### How to Obtain Optimal Detection Performance

**Question**

How can detection latency be reduced?

**Answer**

Use `numactl` to bind NUMA nodes.

```bash
numactl --cpunodebind=0 --membind=0 python script_name.py
```

In addition, the detection instance performs model warmup during initialization, so the first detection is slightly slower than subsequent detections.



## Model Training

### How to Customize Model Training

**Question**

How do I train a new detection model with my own dataset?

**Answer**

1. Prepare the training data in JSON format and store it in `train/dataset/train_data.json`.
2. Download the Chinese-lert-small pretrained model and place it in `train/pretrained_model/chinese-lert-small/`.
3. Modify the training parameters in `train/config/config.json`.
4. Run the training script.

```bash
python train/train.py
```

After training is complete, the ONNX model is automatically exported and INT8 quantization is performed. Copy the generated `train/onnx_model/clf_model.onnx` to `algorithm/knowledge_base/model_file/` to replace the original model.

**Training parameter description**:

| Parameter    | Default Value | Description |
| ----------- | ---- | -------- |
| epochs      | 15   | Number of training epochs |
| lr          | 2e-5 | Learning rate |
| batch_size  | 8    | Batch size |
| max_length  | 512  | Maximum sequence length |
| warmup_rate | 0.03 | Warmup ratio |
| dropout     | 0    | Dropout rate |

## Change History

| Version | Date       | Description    |
| ---- | ---------- | ------- |
| 01   | 2026-09-30 | This is the first official release. |