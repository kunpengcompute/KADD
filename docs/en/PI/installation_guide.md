# Installation Guide

<!-- md-trans-meta sourceCommit=e6b62e0b6301d6001b149147e843b5b6a34d9d54 translatedAt=2026-09-22T04:06:44.854Z pushedAt=2026-09-23T01:18:43.669Z -->

This guide describes the environment deployment and dependency installation steps of the KADD Prompt Injection (PI) feature for jailbreak text detection. It is divided into two chapters:

- **Chapter 1 Compiling the Acceleration Library**: describes how to compile the ONNX Runtime library optimized for Kunpeng processors. (**Required only for Kunpeng processor users**)
- **Chapter 2 Deploying the PI Feature**: describes how to install dependencies, configure environment variables, and verify functionality. (**Required for all users**)

## Before You Start: Choose Your Installation Path

**Step 1: Confirm the processor model.**

```bash
lscpu   # Check the BIOS Model name field in the output to confirm the processor model
```

**Step 2: Choose the installation path based on the following table.**

| Processor Model                | ONNX Runtime         | Installation Path                   |
| -------------------- | ----------------------- | ---------------------- |
| Kunpeng 920 processor or new Kunpeng 920 processor model | Acceleration library 20 (onnxruntime_acl)  | Chapter 1 (section 1.2) and then Chapter 2    |
| Kunpeng 950 processor             | Acceleration library 50 (onnxruntime)      | Chapter 1 (section 1.3) and then Chapter 2    |
| Other processors (including x86 architecture)      | Standard onnxruntime==1.19.2 | **Skip Chapter 1** and start directly from Chapter 2 |

### Terminology

| Terminology            | Meaning                                                                                  |
| --------------------- | ----------------------------------------------------------------------------------- |
| Acceleration library                   | An ONNX Runtime library optimized for Kunpeng processors, obtained as a .whl installation package (Python binary installation package) through source compilation.                           |
| Acceleration library 20                 | An acceleration library for Kunpeng 920 processors and new Kunpeng 920 processor models, compiled based on ONNX Runtime 1.19.2 and dependent on Arm Compute Library and KONNX. |
| Acceleration library 50                 | An acceleration library for Kunpeng 950 processors, compiled based on ONNX Runtime 1.22.2.                                            |
| Arm Compute Library   | Arm's official open-source compute library, a prerequisite dependency for compiling acceleration library 20.                                                            |
| KONNX/SRA_Inference | An inference acceleration component provided by the Kunpeng community, a prerequisite dependency for compiling acceleration library 20.                                                          |

### Path Conventions

In all commands in this document, **`/path` is a placeholder**. Replace it with the actual path when executing commands. This document assumes that:

- The compilation source code (Compute Library, ONNX Runtime, etc.) is downloaded and stored under the `/path` directory. For example, Arm Compute Library is located at `/path/ComputeLibrary-23.08`, and the ONNX Runtime source code is located at `/path/onnxruntime`.
- The code repository is also located under the `/path` directory, in which case the PI directory is `/path/PI` (that is, the directory where `function_test.py` is located).

For example, if the actual working directory is `/home/user/pi_build`, replace all `/path` in the commands with `/home/user/pi_build`. If your directory structure differs from the assumptions in this document, replace the relevant paths accordingly.

To avoid omissions, the first line of every command block containing `/path` in this document **includes a comment reminder**, which you can see when copying and executing it directly. If you encounter a `No such file or directory` error (a path related to `/path` cannot be found) during execution, it is usually caused by not replacing the placeholder. Replace it and try again.

## 1. Compiling the Acceleration Library

Users using **non-Kunpeng processors** (such as x86) do not need to compile the acceleration library. **In this case, go to Chapter 2 directly.**

This section describes the environment dependencies and compilation steps of the PI feature acceleration library. There are two versions of the acceleration library.

- **Acceleration library 20**: for Kunpeng 920 processors and new Kunpeng 920 processor models. See section 1.2 for the compilation steps.
- **Acceleration library 50**: for Kunpeng 950 processors. See section 1.3 for the compilation steps.

### 1.1 Recommended Compilation Environment Requirements

| Item                    | Verified Version (Acceleration Library 20)                                                                  | Verified Version (Acceleration Library 50)                                      |
| ------------------- | --------------------------------------------------------------------------- | ----------------------------------------------- |
| Processor                 | Kunpeng 920                                                                    | Kunpeng 950                                        |
| OS                | openEuler 24.03 LTS                                                         | openEuler 24.03 LTS                             |
| CMake               | 3.27.9                                                                      | 3.28.1                                          |
| Make                | 4.3                                                                         | 4.4.1                                           |
| GCC                 | 10.3.1                                                                      | 12.3.1                                          |
| Python              | 3.9.9                                                                       | 3.10.20                                         |
| Third-party Python libraries           | numpy==2.0.2, packaging==26.3, SCons==4.11.1, wheel==0.48.0, and setuptools==59.4.0 | numpy==2.2.6, packaging==26.2, and setuptools==79.0.1 |
| ONNX Runtime         | 1.19.2                                                                      | 1.22.2                                          |
| Arm Compute Library | 23.08                                                                       | -                                               |
| KONNX               | 1.1.0                                                                       | -                                               |

- In the table, the processors refer to the machine models recommended for **compiling** each acceleration library; the applicable models for the acceleration library are subject to the `Verified Environments` table in Chapter 2 (acceleration library 20 is used for Kunpeng 920 processors/new Kunpeng 920 processor models, and acceleration library 50 is used for Kunpeng 950 processors).
- In the table, the third-party Python libraries are **compilation dependencies** (used when compiling ONNX Runtime), and are independent of the third-party library dependencies in Chapter 2 (PI feature runtime dependencies). Do not confuse them.

### 1.2 Compiling the Acceleration Library 20 (Kunpeng 920 Processors/New Kunpeng 920 Processor Models)

The entire process consists of 4 steps. Perform them in order.

#### Step 1: Installing the Compilation Toolchain

Install CMake, Make, GCC, and Python required by the `Verified Version (Acceleration Library 20)` column in the table in section 1.1. The installation method is not limited (both the system package manager and source compilation are acceptable). The following uses openEuler as an example.

```bash
dnf install -y gcc gcc-c++ make cmake git wget unzip python3 python3-pip
```

If the installed versions are inconsistent with those in the table in section 1.1, you can specify the versions during installation:

```bash
# Check the versions available in the repository.
dnf list --showduplicates cmake
# Install the specified version (the version number is subject to the query result of the command above).
dnf install -y cmake-3.27.9
```

If the required versions are not available in the repository, download the source code of the corresponding versions from the official release page, and then compile and install them yourself.

After installation, run the following commands to check whether the versions are consistent with those in the table in section 1.1:

```bash
cmake --version    # Should be 3.27.9
make --version     # Should be 4.3
gcc --version      # Should be 10.3.1
python --version   # Should be 3.9.9
```

Install the third-party Python libraries required for compilation (versions consistent with those in the table in section 1.1).

```bash
pip install numpy==2.0.2 packaging==26.3 SCons==4.11.1 wheel==0.48.0 setuptools==59.4.0
```

#### Step 2: Compiling Arm Compute Library

Download the source code in the working directory, decompress it, and enter the folder.

```bash
# Note: /path is a placeholder. Replace it with the actual working directory first.
cd /path
wget -q https://github.com/ARM-software/ComputeLibrary/archive/refs/tags/v23.08.zip -O ComputeLibrary-23.08.zip
unzip ComputeLibrary-23.08.zip && cd ComputeLibrary-23.08
```

Run the following command for compilation:

```bash
scons Werror=1 -j8 debug=0 neon=1 opencl=0 os=linux arch=armv8a build=native
```

**Checkpoint**: After compilation is complete, the following three files should exist under the `/path/ComputeLibrary-23.08/build` directory (they need to be configured into environment variables during deployment in Chapter 2):

- `libarm_compute.so`
- `libarm_compute_core.so`
- `libarm_compute_graph.so`

#### Step 3: Installing KONNX

Follow the Kunpeng community guide to first install [SRA_Inference](https://www.hikunpeng.com/document/detail/en/boostsra/srainference/SRA_Inference/kunpengsra_inference_16_0006.html):

```bash
# Note: /path is a placeholder. Replace it with the actual working directory first.
cd /path
wget -q https://kunpeng-repo.obs.cn-north-4.myhuaweicloud.com/Kunpeng%20BoostKit/Kunpeng%20BoostKit%2026.1.RC1/BoostKit-boostsra-inference_1.2.0_RC1.zip -O sra_inference.zip
unzip sra_inference.zip
rpm -ivh boostsra-inference-1.2.0-RC1.aarch64.rpm
source /etc/profile
```

Then install the ONNX Runtime source code with the KONNX patch applied. For details, see the [reference document](https://www.hikunpeng.com/document/detail/en/boostsra/srainference/SRA_Inference/kunpengsra_inference_16_0031.html).

```bash
# Note: /path is a placeholder. Replace it with the actual working directory first.
cd /path
git clone -b v1.19.2 https://github.com/microsoft/onnxruntime.git
git clone https://gitcode.com/openeuler/sra_onnxruntime_adapter.git
cd onnxruntime
git apply ../sra_onnxruntime_adapter/konnx.patch
```

#### Step 4: Compiling ONNX Runtime

Enter the `onnxruntime` directory, and then start compiling ONNX Runtime from the source code. Run the following commands, where the `--acl_home` and `--acl_libs` parameters need to specify the Arm Compute Library path obtained from the compilation in step 2:

```bash
# Execute the commands under the /path/onnxruntime directory.
# Note: Replace /path in --acl_home and --acl_libs below with the actual working directory.
./build.sh --config Release --build_wheel --parallel --compile_no_warning_as_error \
  --skip_submodule_sync --allow_running_as_root --skip-keras-test --skip_onnx_tests \
  --skip_tests --build_dir build --use_acl "ACL_2308" \
  --acl_home=/path/ComputeLibrary-23.08 \
  --acl_libs=/path/ComputeLibrary-23.08/build
```

**Checkpoint**: After successful compilation, you can obtain **`onnxruntime_acl-xx.whl`** (*xx* is the version number; the actual file name is subject to the generated result in this directory) under the `/path/onnxruntime/build/Release/dist` directory. This file will be installed during deployment in Chapter 2.

If the Eigen library download fails, download [Eigen](https://gitlab.com/libeigen/eigen/-/archive/e7248b26a1ed53fa030c5c459f7ea095dfd276ac/eigen-e7248b26a1ed53fa030c5c459f7ea095dfd276ac.zip) in a browser, manually place it under the `/path/onnxruntime/build/Release/_deps/eigen-subbuild/eigen-populate-prefix/src/` directory, and execute the following commands. Note that `/path` is a placeholder; replace it with the actual working directory first.

```bash
# Execute the command under the /path/onnxruntime directory.
grep -r "be8be39fdbc6e60e94fa7870b280707069b5b81a" . | cut -d: -f1 | sort -u | xargs sed -i.bak 's/be8be39fdbc6e60e94fa7870b280707069b5b81a/32b145f525a8308d7ab1c09388b2e288312d8eba/g'
```

### 1.3 Compiling the Acceleration Library 50 (Kunpeng 950 Processors)

The entire process consists of 2 steps. Perform them in order.

#### Step 1: Installing the Compilation Toolchain

Install CMake, Make, GCC, and Python required by the `Verified Version (Acceleration Library 50)` column in the table in section 1.1. The installation method is not limited (both the system package manager and source compilation are acceptable). The following uses openEuler as an example.

```bash
dnf install -y gcc gcc-c++ make cmake git wget unzip python3 python3-pip
```

If the installed versions are inconsistent with those in the table in section 1.1, you can specify the versions during installation:

```bash
# Check the versions available in the repository.
dnf list --showduplicates cmake
# Install the specified version (use the version number from the query result of the command above).
dnf install -y cmake-3.28.1
```

If the required versions are not available in the repository (for example, Python 3.10.20 usually cannot be installed directly), download the source code of the corresponding versions from the official release page, and then compile and install it yourself.

After installation, run the following commands to verify whether the versions are consistent with those in the table in section 1.1:

```bash
cmake --version    # Should be 3.28.1.
make --version     # Should be 4.4.1.
gcc --version      # Should be 12.3.1.
python --version   # Should be 3.10.20
```

Install the third-party Python libraries required for compilation (versions consistent with those in the table in section 1.1).

```bash
pip install numpy==2.2.6 packaging==26.2 setuptools==79.0.1
```

#### Step 2: Compiling ONNX Runtime

Download the ONNX Runtime source code in the working directory, decompress it, and enter the folder.

```bash
# Note: /path is a placeholder. Replace it with the actual working directory first.
cd /path
wget -q https://codeload.github.com/microsoft/onnxruntime/zip/refs/tags/v1.22.2 -O onnxruntime-1.22.2.zip
unzip onnxruntime-1.22.2.zip && cd onnxruntime-1.22.2
```

Enter the `onnxruntime-1.22.2` directory, and then start compiling ONNX Runtime from the source code. Run the following command in the `/path/onnxruntime-1.22.2` directory:

```bash
# Run the command in the /path/onnxruntime-1.22.2 directory.
./build.sh --config Release --build_wheel --parallel --compile_no_warning_as_error --skip_submodule_sync --allow_running_as_root --skip-keras-test --skip_onnx_tests --skip_tests --build_dir build --cmake_extra_defines onnxruntime_USE_KLEIDIAI=ON
```

**Checkpoint**: After successful compilation, you can obtain **`onnxruntime-xx.whl`** (*xx* is the version number; the actual file name is subject to the generated result in this directory) under the `/path/onnxruntime-1.22.2/build/Release/dist` directory. This file will be installed during deployment in Chapter 2.

## 2. Deploying the PI Feature

### 2.1 Runtime Environment Requirements

#### Verified Environments

| Kunpeng Chip Model      | Python Version | ONNX Runtime           |
| ----------- | -------- | ---------------------- |
| Kunpeng 920 processor    | 3.9      | onnxruntime_acl-1.19.2 |
| New Kunpeng 920 processor model | 3.9      | onnxruntime_acl-1.19.2 |
| Kunpeng 950 processor    | 3.10     | onnxruntime-1.22.2     |
| Others          | Unlimited       | onnxruntime==1.19.2    |

The OS is openEuler 24.03 LTS for all.

#### Third-Party Library Dependencies

The PI feature depends on the following third-party Python libraries (corresponding to `PI/deploy/requirements.txt` in the code repository, which can be installed with one click in step 3 of section 2.2).

| Item           | Version     | Description               |
| ------------ | ------ | ---------------- |
| datasketch   | 2.0.0  | MinHash LSH approximate matching |
| jieba        | 0.42.1 | Chinese word segmentation             |
| numpy        | 2.0.1  | Numerical computation             |
| onnx         | 1.19.1 | ONNX model format support       |
| onnxscript   | 0.7.1  | ONNX script support         |
| scikit-learn | 1.6.1  | Machine learning tool           |
| torch        | 2.8.0  | Deep learning framework (for training)      |
| tqdm         | 4.69.0 | Progress bar              |
| transformers | 4.57.6 | Pretrained model tool          |

### 2.2 Environment Deployment

#### Step 1: Checking the Processor Model

```bash
lscpu   # Check the BIOS Model name field to confirm the processor model.
```

#### Step 2: Installing the ONNX Runtime Library (Selecting One from Three)

**Scenario A: Installing the acceleration library 20 (Kunpeng 920 processor/new Kunpeng 920 processor model)**

The .whl file is the compilation output of step 4 in section 1.2, located under the `/path/onnxruntime/build/Release/dist` directory on the compilation machine. If the deployment machine and the compilation machine are not the same, copy the .whl file to the deployment machine, and then perform the installation.

```bash
pip install onnxruntime_acl-xx.whl   # xx is the version number; replace it with the actual file name in the dist directory.
```

**Scenario B: Installing the acceleration library 50 (Kunpeng 950 processor)**

The .whl file is the compilation output of step 2 in section 1.3, located under the `/path/onnxruntime-1.22.2/build/Release/dist` directory on the compilation machine. If the deployment machine and the compilation machine are not the same, copy the .whl file to the deployment machine, and then perform the installation.

```bash
pip install onnxruntime-xx.whl       # xx is the version number; replace it with the actual file name in the dist directory.
```

**Scenario C: Installing the standard version directly (other processors)**

```bash
pip install onnxruntime==1.19.2
```

#### Step 3: Installing the Python Dependencies of the PI Feature (Required for All Processors)

Note for Kunpeng processor users: the Python version of the deployment environment must be consistent with the Python version used when compiling the .whl file in Chapter 1 (Kunpeng 920/new Kunpeng 920 processor model: 3.9; Kunpeng 950: 3.10); otherwise, the .whl installation will fail or errors will occur at runtime.

```bash
# Note: /path is a placeholder, which is the parent directory of the PI directory. Replace it with the actual path.
pip install -r /path/PI/deploy/requirements.txt
```

#### Step 4: Configuring Environment Variables (Required Only for Kunpeng Processors)

**When using Kunpeng 920 processors/New Kunpeng 920 processor models (corresponding to the acceleration library 20), configure the following two items:**

1. Install [SRA_Inference](https://www.hikunpeng.com/document/detail/en/boostsra/srainference/SRA_Inference/kunpengsra_inference_16_0006.html). If the deployment machine and the compilation machine are the same, no reinstallation is required.

2. Add the ACL library path. On the deployment machine, `libarm_compute.so`, `libarm_compute_core.so`, and `libarm_compute_graph.so` should exist under this directory (if the deployment machine and the compilation machine are not the same, copy the files under the `/path/ComputeLibrary-23.08/build` directory on the compilation machine to the deployment machine, and modify the following command according to the actual storage path).

   ```bash
   # Note: /path is a placeholder. Replace it with the actual working directory.
   export LD_LIBRARY_PATH=/path/ComputeLibrary-23.08/build:$LD_LIBRARY_PATH
   ```

**When using Kunpeng 950 processors (corresponding to the acceleration library 50), no environment variable configuration is required.**

Environment variables configured via `export` are **valid only in the current terminal**. They become invalid after a new terminal is opened and must be configured again. To make them take effect permanently, append the above `export` command to the end of the `~/.bashrc` file and run `source ~/.bashrc` to make the configuration take effect.

### 2.3 Function Verification

After the environment deployment is complete, you can verify whether the functions are normal through unit tests. Run the following command in the `PI` directory (that is, the directory where `function_test.py` is located in the code repository):

```bash
# Note: /path is a placeholder, which is the parent directory of the PI directory. Replace it with the actual path.
cd /path/PI
python function_test.py
```

The expected output is as follows (indicating that all 17 tests have passed):

```bash
.................
----------------------------------------------------------------------
Ran 17 tests in 1.703s

OK
```

If the output is inconsistent with the above, check whether all preceding steps have been completed, or refer to [FAQs](faq.md).

## 3. Performance Optimization Commands

After deployment is complete, you can run the script with the following command to achieve maximum performance:

```bash
# Running command example
numactl --cpunodebind=0 --membind=0 python script_name.py
```

`numactl` binds the process to the CPU and memory of NUMA node 0, reducing cross-node access latency.

If the `numactl` command is not found, install it first:

```bash
dnf install -y numactl
```

## Change History

| Version | Date       | Description    |
| ---- | ---------- | ------- |
| 01   | 2026-09-30 | This is the first official release. |