# Installation Guide

This document describes how to deploy the KADD PI (jailbreak text detection) engine.

## About KADD

KADD is a security detection toolset. All features can be built using the `build.sh` script in the root directory.

| Feature | Description | Status |
|---------|-------------|--------|
| SQLi | SQL injection detection | ✅ Implemented |
| PI | Jailbreak text detection | ✅ Implemented |

## Environment Requirements

### Verified Environments

The processor is a new Kunpeng 920 processor model, and the OS is openEuler 24.03 LTS.

### Kunpeng-Affinity Requirements

| Kunpeng Chip Model | Python Version | ONNX Runtime |
| ----------------- | --------------- | ------------- |
| Kunpeng 920       | 3.9             | onnxruntime_acl-1.19.2 |
| Kunpeng 920B      | 3.9             | onnxruntime_acl-1.19.2 |
| Kunpeng 950       | 3.10            | onnxruntime-1.22.2 |
| Other             | No restriction  | onnxruntime==1.19.2 |

### Software Dependencies

| Item | Version | Description |
|------|---------|-------------|
| datasketch | 2.0.0 | MinHash LSH approximate matching |
| jieba | 0.42.1 | Chinese word segmentation |
| numpy | 2.0.1 | Numerical computing |
| onnx | 1.19.1 | ONNX model format support |
| onnxscript | 0.7.1 | ONNX script support |
| scikit-learn | 1.6.1 | Machine learning tools |
| torch | 2.8.0 | Deep learning framework (for training) |
| tqdm | 4.69.0 | Progress bar |
| transformers | 4.57.6 | Pretrained model tools |

---

## Deployment

### 🚀 One-Click Deployment (Recommended)

PI supports one-click environment deployment. Run the deployment script on the target machine:

```bash
cd /opt/KADD/PI
source ./deploy/deploy.sh
```

The deployment script automatically completes the following:
1. Detects the Kunpeng CPU model and determines the required Python version
2. Creates a Python virtual environment
3. Installs Python dependencies
4. Installs the ONNX Runtime acceleration package for the corresponding CPU
5. Configures the acceleration library dynamic link path
6. Runs functional tests to verify the deployment

After deployment, you can run `python test_kp_detect.py` to test single text detection.

### Deployment Options

```bash
# View help
source ./deploy/deploy.sh -h

# Specify Python executable path or command
source ./deploy/deploy.sh -p /opt/python3.9/bin/python3.9

# Specify an existing virtual environment path
source ./deploy/deploy.sh -e /opt/python_venv

# Specify the virtual environment creation directory
source ./deploy/deploy.sh -d /data/envs

# If the target virtual environment already exists, delete and recreate it
source ./deploy/deploy.sh --recreate-venv

# Specify pip index URL (default: Aliyun)
source ./deploy/deploy.sh --pip-index-url https://pypi.tuna.tsinghua.edu.cn/simple

# Set a unified proxy for downloading dependencies
source ./deploy/deploy.sh --proxy http://127.0.0.1:7890

# Skip functional test after deployment
source ./deploy/deploy.sh -s
```

### Deployment Options Reference

| Option | Description | Use Case |
|--------|-------------|----------|
| `-p, --python` | Specify Python executable path or command | Multiple Python versions on system |
| `-d, --venv-dir` | Specify virtual environment creation directory | Custom venv location |
| `-e, --venv` | Specify an existing virtual environment path | Reuse existing venv |
| `-r, --requirements` | Specify requirements.txt path | Custom requirements file |
| `--recreate-venv` | Delete and recreate existing virtual environment | Fresh environment needed |
| `--pip-index-url` | Specify pip index URL | Accelerate dependency download |
| `--pip-trusted-host` | Specify pip trusted-host | Mirror source requires trusted host |
| `--proxy` | Set unified proxy | Download via proxy |
| `-s, --skip-function-test` | Skip functional test | Batch deployment |
| `-h, --help` | View help | View available options |

---

## Performance Optimization

After deployment, use the following command to run scripts for maximum performance:

```bash
numactl --cpunodebind=0 --membind=0 python script_name.py
```

`numactl` binds the process to NUMA node 0 CPU and memory, reducing cross-node access latency.
