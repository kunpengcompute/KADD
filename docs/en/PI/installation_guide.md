# Installation Guide

This guide describes the environment deployment and dependency installation steps for the KADD PI (Prompt Injection, jailbreak text detection) feature. It is divided into two chapters:

- **Chapter 1: Compiling the Acceleration Libraries**: describes how to compile the onnxruntime libraries optimized for Kunpeng processors. **Required only for Kunpeng processor users**.
- **Chapter 2: Deploying the PI Feature**: describes how to install dependencies, configure environment variables, and verify the function. **Required for all users**.

## Before You Begin: Choosing Your Installation Path

**Step 1: Check the processor model**

```bash
lscpu   # Check the BIOS Model name field in the output to determine the processor model
```

**Step 2: Choose your installation path based on the following table**

| Processor Model | onnxruntime to Use | Installation Path |
| --------------- | ------------------ | ----------------- |
| Kunpeng 920 processor, Kunpeng 920 new model processor | Acceleration library 20 (onnxruntime_acl) | Chapter 1 (Section 1.2) → Chapter 2 |
| Kunpeng 950 processor | Acceleration library 50 (onnxruntime) | Chapter 1 (Section 1.3) → Chapter 2 |
| Other processors (including x86) | Standard onnxruntime==1.19.2 | **Skip Chapter 1** and start from Chapter 2 directly |

### Terminology

| Term | Description |
| ---- | ----------- |
| Acceleration library | The onnxruntime library optimized for Kunpeng processors, obtained by compiling source code into a whl installation package (Python binary installation package) |
| Acceleration library 20 | The acceleration library for Kunpeng 920 processors and Kunpeng 920 new model processors. Compiled based on onnxruntime 1.19.2, depending on Arm Compute Library and KONNX |
| Acceleration library 50 | The acceleration library for Kunpeng 950 processors. Compiled based on onnxruntime 1.22.2 |
| Arm Compute Library | Arm's official open-source compute library, a prerequisite for compiling acceleration library 20 |
| KONNX / SRA_Inference | Inference acceleration components provided by the Kunpeng community, prerequisites for compiling acceleration library 20 |

### Path Conventions

**`/path` in all commands in this document is a placeholder.** Replace it with the actual path when executing the commands. This document assumes that:

- Source code to be compiled (ComputeLibrary, onnxruntime, etc.) is downloaded and stored in the `/path` directory. For example, Arm Compute Library is located at `/path/ComputeLibrary-23.08`, and the onnxruntime source code is located at `/path/onnxruntime`.
- The code repository is also located in the `/path` directory. In this case, the `PI` directory is `/path/PI` (that is, the directory where `function_test.py` is located).

For example, if the actual working directory is `/home/user/pi_build`, replace all occurrences of `/path` in the commands with `/home/user/pi_build`. If your directory structure differs from what this document assumes, replace the related paths accordingly.

To avoid omissions, all command blocks containing `/path` in this document **have a reminder comment on the first line**, which you can see when copying the commands directly. If a `No such file or directory` error (a `/path`-related path cannot be found) occurs during execution, it is usually because the placeholder was not replaced. Replace it and try again.

## 1. Compiling the Acceleration Libraries

> Users of non-Kunpeng processors (such as x86) do not need to compile the acceleration libraries. Go to Chapter 2 directly.

This section describes the environment dependencies and compilation steps of the acceleration libraries for the PI feature. There are two versions of the acceleration library:

- **Acceleration library 20**: for Kunpeng 920 processors and Kunpeng 920 new model processors. See Section 1.2 for the compilation steps.
- **Acceleration library 50**: for Kunpeng 950 processors. See Section 1.3 for the compilation steps.

### 1.1 Recommended Compilation Environment Requirements

|                     | Acceleration Library 20 Verified Version                                                   | Acceleration Library 50 Verified Version                     |
| ------------------- | ------------------------------------------------------------------------------------------ | ------------------------------------------------------------ |
| Processor           | Kunpeng 920 processor                                                                       | Kunpeng 920 new model processor                              |
| OS                  | openEuler 24.03 LTS                                                                         | openEuler 24.03 LTS                                          |
| cmake               | 3.27.9                                                                                      | 3.28.1                                                       |
| make                | 4.3                                                                                         | 4.4.1                                                        |
| gcc                 | 10.3.1                                                                                      | 12.3.1                                                       |
| python              | 3.9.9                                                                                       | 3.10.20                                                      |
| Python third-party libraries | numpy==2.0.2,packaging==26.3,SCons==4.11.1,wheel==0.48.0,setuptools==59.4.0 | numpy==2.2.6,packaging==26.2,setuptools==79.0.1              |
| onnxruntime         | 1.19.2                                                                                      | 1.22.2                                                       |
| Arm Compute Library | 23.08                                                                                       | -                                                            |
| KONNX               | 1.1.0                                                                                       | -                                                            |

- The processor in the table is the recommended machine model **for compiling** each acceleration library. For the applicable machine models of the acceleration libraries, see the "Verified Environments" table in Chapter 2 (acceleration library 20 for Kunpeng 920/Kunpeng 920 new model processors, and acceleration library 50 for Kunpeng 950 processors).
- The Python third-party libraries in the table are **compilation dependencies** (used when compiling onnxruntime). They are independent of the "Third-Party Dependencies" in Chapter 2 (running dependencies of the PI feature). Do not confuse them.

### 1.2 Compiling Acceleration Library 20 (Kunpeng 920 Processor / Kunpeng 920 New Model Processor)

The process consists of 4 steps. Perform them in sequence.

#### Step 1: Install the compilation toolchain

Install cmake, make, gcc, and python as required in the "Acceleration Library 20 Verified Version" column of the table in Section 1.1. The installation method is not restricted (system package manager or source code compilation). Taking openEuler as an example:

```bash
dnf install -y gcc gcc-c++ make cmake git wget unzip python3 python3-pip
```

If the installed versions are inconsistent with the requirements in the table in Section 1.1, you can specify versions during installation:

```bash
# Check the installable versions in the repository
dnf list --showduplicates cmake
# Install a specified version (the version number is subject to the query result of the preceding command)
dnf install -y cmake-3.27.9
```

If the required versions are unavailable in the repository, download the source code of the corresponding versions from the official release page and compile and install them on your own.

After the installation is complete, run the following commands to check whether the versions are consistent with the table in Section 1.1:

```bash
cmake --version    # Should be 3.27.9
make --version     # Should be 4.3
gcc --version      # Should be 10.3.1
python --version   # Should be 3.9.9
```

Install the Python third-party libraries required for compilation (versions consistent with the table in Section 1.1):

```bash
pip install numpy==2.0.2 packaging==26.3 SCons==4.11.1 wheel==0.48.0 setuptools==59.4.0
```

#### Step 2: Compile Arm Compute Library

In the working directory, download the source code, decompress it, and enter the directory:

```bash
# Note: /path is a placeholder. Replace it with the actual working directory first
cd /path
wget -q https://github.com/ARM-software/ComputeLibrary/archive/refs/tags/v23.08.zip -O ComputeLibrary-23.08.zip
unzip ComputeLibrary-23.08.zip && cd ComputeLibrary-23.08
```

Compile using the following command:

```bash
scons Werror=1 -j8 debug=0 neon=1 opencl=0 os=linux arch=armv8a build=native
```

**Checkpoint**: After the compilation is complete, the following three files should exist in the `/path/ComputeLibrary-23.08/build` directory (they need to be configured in environment variables during deployment in Chapter 2):

- `libarm_compute.so`
- `libarm_compute_core.so`
- `libarm_compute_graph.so`

#### Step 3: Install KONNX

Follow the Kunpeng community instructions to install [SRA_Inference](https://www.hikunpeng.com/document/detail/zh/boostsra/srainference/SRA_Inference/kunpengsra_inference_16_0006.html) first:

```bash
# Note: /path is a placeholder. Replace it with the actual working directory first
cd /path
wget -q https://kunpeng-repo.obs.cn-north-4.myhuaweicloud.com/Kunpeng%20BoostKit/Kunpeng%20BoostKit%2026.1.RC1/BoostKit-boostsra-inference_1.2.0_RC1.zip -O sra_inference.zip
unzip sra_inference.zip
rpm -ivh boostsra-inference-1.2.0-RC1.aarch64.rpm
source /etc/profile
```

Then install the onnxruntime source code with the KONNX patch applied. For details, see the [reference document](https://www.hikunpeng.com/document/detail/zh/boostsra/srainference/SRA_Inference/kunpengsra_inference_16_0031.html):

```bash
# Note: /path is a placeholder. Replace it with the actual working directory first
cd /path
git clone -b v1.19.2 https://github.com/microsoft/onnxruntime.git
git clone https://gitcode.com/openeuler/sra_onnxruntime_adapter.git
cd onnxruntime
git apply ../sra_onnxruntime_adapter/konnx.patch
```

#### Step 4: Compile OnnxRuntime

Enter the `onnxruntime` directory and start compiling onnxruntime from source. Use the following command, where `--acl_home` and `--acl_libs` must specify the path of the Arm Compute Library compiled in Step 2:

```bash
# Run in the /path/onnxruntime directory
# Note: Replace /path in --acl_home and --acl_libs below with the actual working directory
./build.sh --config Release --build_wheel --parallel --compile_no_warning_as_error \
  --skip_submodule_sync --allow_running_as_root --skip-keras-test --skip_onnx_tests \
  --skip_tests --build_dir build --use_acl "ACL_2308" \
  --acl_home=/path/ComputeLibrary-23.08 \
  --acl_libs=/path/ComputeLibrary-23.08/build
```

**Checkpoint**: After a successful compilation, **`onnxruntime_acl-xx.whl`** (xx indicates the version number; the actual file name is subject to the build output in the directory) is generated in the `/path/onnxruntime/build/Release/dist` directory. This file will be installed during deployment in Chapter 2.

If the Eigen library download fails, download [Eigen](https://gitlab.com/libeigen/eigen/-/archive/e7248b26a1ed53fa030c5c459f7ea095dfd276ac/eigen-e7248b26a1ed53fa030c5c459f7ea095dfd276ac.zip) using a browser, manually place it in the `/path/onnxruntime/build/Release/_deps/eigen-subbuild/eigen-populate-prefix/src/` directory, and run the following command. Note: `/path` is a placeholder. Replace it with the actual working directory first.

```bash
# Run in the /path/onnxruntime directory
grep -r "be8be39fdbc6e60e94fa7870b280707069b5b81a" . | cut -d: -f1 | sort -u | xargs sed -i.bak 's/be8be39fdbc6e60e94fa7870b280707069b5b81a/32b145f525a8308d7ab1c09388b2e288312d8eba/g'
```

### 1.3 Compiling Acceleration Library 50 (Kunpeng 950 Processor)

The process consists of 2 steps. Perform them in sequence.

#### Step 1: Install the compilation toolchain

Install cmake, make, gcc, and python as required in the "Acceleration Library 50 Verified Version" column of the table in Section 1.1. The installation method is not restricted (system package manager or source code compilation). Taking openEuler as an example:

```bash
dnf install -y gcc gcc-c++ make cmake git wget unzip python3 python3-pip
```

If the installed versions are inconsistent with the requirements in the table in Section 1.1, you can specify versions during installation:

```bash
# Check the installable versions in the repository
dnf list --showduplicates cmake
# Install a specified version (the version number is subject to the query result of the preceding command)
dnf install -y cmake-3.28.1
```

If the required versions are unavailable in the repository (for example, python 3.10.20 usually cannot be installed directly), download the source code of the corresponding versions from the official release page and compile and install them on your own.

After the installation is complete, run the following commands to check whether the versions are consistent with the table in Section 1.1:

```bash
cmake --version    # Should be 3.28.1
make --version     # Should be 4.4.1
gcc --version      # Should be 12.3.1
python --version   # Should be 3.10.20
```

Install the Python third-party libraries required for compilation (versions consistent with the table in Section 1.1):

```bash
pip install numpy==2.2.6 packaging==26.2 setuptools==79.0.1
```

#### Step 2: Compile OnnxRuntime

In the working directory, download the onnxruntime source code, decompress it, and enter the directory:

```bash
# Note: /path is a placeholder. Replace it with the actual working directory first
cd /path
wget -q https://codeload.github.com/microsoft/onnxruntime/zip/refs/tags/v1.22.2 -O onnxruntime-1.22.2.zip
unzip onnxruntime-1.22.2.zip && cd onnxruntime-1.22.2
```

Enter the `onnxruntime-1.22.2` directory and start compiling onnxruntime from source. In the `/path/onnxruntime-1.22.2` directory, use the following command:

```bash
# In the /path/onnxruntime-1.22.2 directory
./build.sh --config Release --build_wheel --parallel --compile_no_warning_as_error --skip_submodule_sync --allow_running_as_root --skip-keras-test --skip_onnx_tests --skip_tests --build_dir build --cmake_extra_defines onnxruntime_USE_KLEIDIAI=ON
```

**Checkpoint**: After the compilation is complete, **`onnxruntime-xx.whl`** (xx indicates the version number; the actual file name is subject to the build output in the directory) is generated in the `/path/onnxruntime-1.22.2/build/Release/dist` directory. This file will be installed during deployment in Chapter 2.

## 2. Deploying the PI Feature

### 2.1 Runtime Environment Requirements

#### Verified Environments

| Kunpeng Chip Model              | Python Version | ONNX Runtime           |
| ------------------------------- | -------------- | ---------------------- |
| Kunpeng 920 processor           | 3.9            | onnxruntime_acl-1.19.2 |
| Kunpeng 920 new model processor | 3.9            | onnxruntime_acl-1.19.2 |
| Kunpeng 950 processor           | 3.10           | onnxruntime-1.22.2     |
| Other                           | No restriction | onnxruntime==1.19.2    |

The OS is openEuler 24.03 LTS for all.

#### Third-Party Dependencies

The running of the PI feature depends on the following Python third-party libraries (corresponding to `PI/deploy/requirements.txt` in the code repository, which can be installed with one click in Step 3 of Section 2.2):

| Item         | Version | Description                            |
| ------------ | ------- | -------------------------------------- |
| datasketch   | 2.0.0   | MinHash LSH approximate matching       |
| jieba        | 0.42.1  | Chinese word segmentation              |
| numpy        | 2.0.1   | Numerical computing                    |
| onnx         | 1.19.1  | ONNX model format support              |
| onnxscript   | 0.7.1   | ONNX script support                    |
| scikit-learn | 1.6.1   | Machine learning tools                 |
| torch        | 2.8.0   | Deep learning framework (for training) |
| tqdm         | 4.69.0  | Progress bar                           |
| transformers | 4.57.6  | Pretrained model tools                 |

### 2.2 Environment Deployment

#### Step 1: Check the processor model

```bash
lscpu   # Check the BIOS Model name field to determine the processor model
```

#### Step 2: Install the onnxruntime library (choose one of the three options)

**Option A: Kunpeng 920 processor / Kunpeng 920 new model processor → Install acceleration library 20**

The whl file is the build output of Step 4 in Section 1.2, located in the `/path/onnxruntime/build/Release/dist` directory of the compilation machine. If the deployment machine is not the same machine as the compilation machine, copy the whl file to the deployment machine first, and then install it:

```bash
pip install onnxruntime_acl-xx.whl   # xx indicates the version number. Replace it with the actual file name in the dist directory
```

**Option B: Kunpeng 950 processor → Install acceleration library 50**

The whl file is the build output of Step 2 in Section 1.3, located in the `/path/onnxruntime-1.22.2/build/Release/dist` directory of the compilation machine. If the deployment machine is not the same machine as the compilation machine, copy the whl file to the deployment machine first, and then install it:

```bash
pip install onnxruntime-xx.whl       # xx indicates the version number. Replace it with the actual file name in the dist directory
```

**Option C: Other processors → Install the standard version directly**

```bash
pip install onnxruntime==1.19.2
```

#### Step 3: Install the Python dependencies of the PI feature (required for all processors)

Kunpeng processor users: The Python version of the deployment environment must be the same as the Python version used to compile the whl package in Chapter 1 (Kunpeng 920/Kunpeng 920 new model: 3.9; Kunpeng 950: 3.10). Otherwise, the whl installation fails or a runtime error occurs.

```bash
# Note: /path is a placeholder, that is, the parent directory of the PI directory. Replace it with the actual path
pip install -r /path/PI/deploy/requirements.txt
```

#### Step 4: Configure environment variables (required only for Kunpeng processors)

**When using a Kunpeng 920 processor / Kunpeng 920 new model processor (acceleration library 20), configure the following items:**

1. Install [SRA_Inference](https://www.hikunpeng.com/document/detail/zh/boostsra/srainference/SRA_Inference/kunpengsra_inference_16_0006.html). If the deployment machine is the same machine as the one used to compile the acceleration library, you do not need to install it again.

2. Add the ACL library path. The `libarm_compute.so`, `libarm_compute_core.so`, and `libarm_compute_graph.so` files should exist in this directory on the deployment machine (if the deployment machine is not the same machine as the compilation machine, copy the files in the `/path/ComputeLibrary-23.08/build` directory on the compilation machine to the deployment machine, and modify the following command based on the actual storage path):
   
   ```bash
   # Note: /path is a placeholder. Replace it with the actual working directory
   export LD_LIBRARY_PATH=/path/ComputeLibrary-23.08/build:$LD_LIBRARY_PATH
   ```

**When using a Kunpeng 950 processor (acceleration library 50), you do not need to configure environment variables.**

> **NOTE**: Environment variables configured using `export` **take effect only in the current terminal** and become invalid after you open a new terminal. To make them permanent, append the preceding export commands to the end of the `~/.bashrc` file and run `source ~/.bashrc` to make them take effect.

### 2.3 Function Verification

After the environment is deployed, you can run unit tests to verify that the function works properly. In the `PI` directory (that is, the directory where `function_test.py` is located in the code repository), run the following command:

```bash
# Note: /path is a placeholder, that is, the parent directory of the PI directory. Replace it with the actual path
cd /path/PI
python function_test.py
```

The expected output is as follows (indicating that all 17 tests pass):

```bash
.................
----------------------------------------------------------------------
Ran 17 tests in 1.703s

OK
```

If the output is inconsistent with the preceding, check whether all preceding steps are complete, or see [FAQs](faq.md).

## 3. Performance Optimization Command

After the deployment is complete, use the following command to run scripts for maximum performance:

```bash
# Example command
numactl --cpunodebind=0 --membind=0 python script_name.py
```

`numactl` binds the process to the CPU and memory of NUMA node 0, reducing cross-node access latency.

If the `numactl` command does not exist, install it first:

```bash
dnf install -y numactl
```

# Revision History

| Document Version | Release Date | Description           |
| ---------------- | ------------ | --------------------- |
| 01               | 2026-09-30   | First official release |
