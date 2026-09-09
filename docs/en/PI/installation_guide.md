# Installation Guide

This guide describes the environment deployment and dependency installation steps for the KADD PI (jailbreak text detection) feature.

It is divided into two chapters. Chapter 1 describes how to compile the onnxruntime libraries, and Chapter 2 describes how to deploy the PI feature.

# 1. Compiling the Acceleration Libraries

## Compilation Environment Requirements

This section describes the environment dependencies and compilation steps for the PI feature acceleration libraries. There are two versions of the acceleration library: one for the Kunpeng 920 processor and Kunpeng 920 new model processor, and one for the Kunpeng 950 processor. They are hereinafter referred to as acceleration library 20 and acceleration library 50.

### Compilation Environment

|                         | Acceleration Library 20 Verified Version                                                   | Acceleration Library 50 Verified Version                     |
| ----------------------- | ------------------------------------------------------------------------------------------ | ------------------------------------------------------------ |
| Processor               | Kunpeng 920 processor                                                                       | Kunpeng 920 new model processor                              |
| OS                      | openEuler 24.03 LTS                                                                         | openEuler 24.03 LTS                                          |
| cmake                   | 3.27.9                                                                                      | 3.28.1                                                       |
| make                    | 4.3                                                                                         | 4.4.1                                                        |
| gcc                     | 10.3.1                                                                                      | 12.3.1                                                       |
| python                  | 3.9.9                                                                                       | 3.10.20                                                      |
| Python third-party libs | numpy==2.0.2,packaging==26.3,SCons==4.11.1,wheel==0.48.0,setuptools==59.4.0                 | numpy==2.2.6,packaging==26.2,setuptools==79.0.1              |
| onnxruntime             | 1.19.2                                                                                      | 1.22.2                                                       |
| Arm Compute Library     | 23.08                                                                                       | \                                                            |
| KONNX                   | 1.1.0                                                                                       | \                                                            |

### Compiling the Acceleration Libraries

Note: The default current path is **/path**

#### Acceleration Library 20

**Install the Environment**

1. Confirm the processor in use
   
   ```bash
   lscpu #Check the BIOS Model name field
   ```

2. Install cmake, make, gcc, python, and the Python third-party libraries required by the compilation environment for the corresponding processor

**Install Arm Compute Library**

Download the Arm Compute Library source code. After the download is complete, decompress it and enter the /ComputeLibrary-23.08 directory

```bash
wget -q https://github.com/ARM-software/ComputeLibrary/archive/refs/tags/v23.08.zip -O ComputeLibrary-23.08.zip
unzip ComputeLibrary-23.08.zip && cd ComputeLibrary-23.08
```

Compile with the following command

```bash
scons Werror=1 -j8 debug=0 neon=1 opencl=0 os=linux arch=armv8a build=native \
    extra_cxx_flags="-D_FORTIFY_SOURCE=2 -ftrapv -fstack-protector-all" \
    extra_link_flags="-Wl,-z,relro -Wl,-z,now -Wl,--strip-all"
```

After the compilation is complete, /ComputeLibrary-23.08/build should contain libarm_compute.so, libarm_compute_core.so, and libarm_compute_graph.so. libarm_compute_core.so is an empty file without stack protection and can be replaced with libarm_compute.so.
``` bash
# In the /ComputeLibrary-23.08 directory
cd build
rm -rf libarm_compute_core.so
cp libarm_compute.so libarm_compute_core.so
```


**Install KONNX**
Follow the Kunpeng community instructions to install [SRA_Inference](https://www.hikunpeng.com/document/detail/zh/boostsra/srainference/SRA_Inference/kunpengsra_inference_16_0006.html) first, and then install the onnxruntime source code with the KONNX patch applied. See the [reference document](https://www.hikunpeng.com/document/detail/zh/boostsra/srainference/SRA_Inference/kunpengsra_inference_16_0031.html)

**Compile OnnxRuntime**
Before compiling onnxruntime, some cmake files in the source code need to be modified to disable the dynamic library search path. The steps are as follows:
Enter the onnxruntime directory and run the following command to find all lines that use rpath in the cmake files, then comment them out with '#'

```bash
# In the /onnxruntime directory
grep -rn 'ORIGIN' cmake/ | grep -i rpath
```

Comment example:

```bash
cmake/onnxruntime.cmake:114        # set_property(TARGET onnxruntime APPEND_STRING PROPERTY LINK_FLAGS " -Xlinker -rpath=\\$ORIGIN")
cmake/onnxruntime.cmake:151        # set(CMAKE_SHARED_LINKER_FLAGS "... -Wl,-rpath='$ORIGIN'")
```

Then start compiling onnxruntime from source using the following command, where the two acl parameters need to specify the path of the compiled Arm Compute Library

```bash
export CFLAGS="-D_FORTIFY_SOURCE=2 -fwrapv -fstack-protector-all"
export CXXFLAGS="-D_FORTIFY_SOURCE=2 -fwrapv -fstack-protector-all"
export LDFLAGS="-Wl,-z,relro -Wl,-z,now -Wl,--strip-all"

./build.sh --config Release --build_wheel --parallel --compile_no_warning_as_error \
  --skip_submodule_sync --allow_running_as_root --skip-keras-test --skip_onnx_tests \
  --skip_tests --build_dir build --use_acl "ACL_2308" \
  --acl_home=/path/ComputeLibrary-23.08 \
  --acl_libs=/path/ComputeLibrary-23.08/build \
  --cmake_extra_defines CMAKE_SKIP_RPATH=ON \
  --cmake_extra_defines CMAKE_SKIP_BUILD_RPATH=ON \
  --cmake_extra_defines CMAKE_SKIP_INSTALL_RPATH=ON
```

After a successful compilation, **onnxruntime_acl-xx.whl** can be obtained in /onnxruntime/build/Release/dist

#### Acceleration Library 50

**Install the Environment**

1. Confirm the processor in use
   
   ```bash
   lscpu #Check the BIOS Model name field
   ```

2. Install cmake, make, gcc, python, and the Python third-party libraries required by the compilation environment for the corresponding processor

**Compile OnnxRuntime**

Download the onnxruntime source code, decompress it, and enter the directory
``` bash
wget -q https://codeload.github.com/microsoft/onnxruntime/zip/refs/tags/v1.22.2 -O onnxruntime-1.22.2.zip
unzip onnxruntime-1.22.2.zip && cd onnxruntime-1.22.2
```
Before compiling onnxruntime, some cmake files in the source code need to be modified to disable the dynamic library search path. The steps are as follows:
Enter the onnxruntime-1.22.2 directory and run the following command to find all lines that use rpath in the xx.cmake files, then comment them out with '#'

```bash
# In the /onnxruntime-1.22.2 directory
grep -rn 'ORIGIN' cmake/ | grep -i rpath
```

Comment example:

```bash
cmake/onnxruntime.cmake:114        # set_property(TARGET onnxruntime APPEND_STRING PROPERTY LINK_FLAGS " -Xlinker -rpath=\\$ORIGIN")
cmake/onnxruntime.cmake:151        # set(CMAKE_SHARED_LINKER_FLAGS "... -Wl,-rpath='$ORIGIN'")
```

Then start compiling onnxruntime from source. In the /onnxruntime-1.22.2 directory, use the following command

```bash
# In the /onnxruntime-1.22.2 directory
export CFLAGS="-D_FORTIFY_SOURCE=2 -fwrapv -fstack-protector-all"
export CXXFLAGS="-D_FORTIFY_SOURCE=2 -fwrapv -fstack-protector-all"
export LDFLAGS="-Wl,-z,relro -Wl,-z,now -Wl,--strip-all"

 ./build.sh --config Release --build_wheel --parallel --compile_no_warning_as_error --skip_submodule_sync --allow_running_as_root --skip-keras-test --skip_onnx_tests --skip_tests --build_dir build --cmake_extra_defines onnxruntime_USE_KLEIDIAI=ON \
  --cmake_extra_defines CMAKE_SKIP_RPATH=ON \
  --cmake_extra_defines CMAKE_SKIP_BUILD_RPATH=ON \
  --cmake_extra_defines CMAKE_SKIP_INSTALL_RPATH=ON
```

After the compilation is complete, **onnxruntime-xx.whl** can be obtained in /onnxruntime/build/Release/dist

# 2. Deploying the PI Feature

## Runtime Environment Requirements

This section describes the environment deployment and dependency installation steps for running the PI feature

### Verified Environments

| Kunpeng Chip Model              | Python Version | ONNX Runtime           |
| ------------------------------- | -------------- | ---------------------- |
| Kunpeng 920 processor           | 3.9            | onnxruntime_acl-1.19.2 |
| Kunpeng 920 new model processor | 3.9            | onnxruntime_acl-1.19.2 |
| Kunpeng 950 processor           | 3.10           | onnxruntime-1.22.2     |
| Other                           | No restriction | onnxruntime==1.19.2    |

The OS is openEuler 24.03 LTS for all

### Third-Party Dependencies

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

---

## Environment Deployment

**Install the Environment**

1. Confirm the processor in use
```bash
lscpu #Check the BIOS Model name field
```

2. For Kunpeng 920 processors, Kunpeng 920 new model processors, and Kunpeng 950 processors, install python and the Python third-party libraries. The python version must be the same as the python version used when compiling the .whl in Chapter 1

3. For other processors, the non-accelerated third-party library can be installed directly using pip install onnxruntime==1.19.2

**Install the onnxruntime library**

When using a **Kunpeng 920 processor** or a **Kunpeng 920 new model processor**, install **acceleration library 20** with pip

```bash
pip install onnxruntime_acl-xx.whl
```

The environment variables to be configured are as follows:

- Install [SRA_Inference](https://www.hikunpeng.com/document/detail/zh/boostsra/srainference/SRA_Inference/kunpengsra_inference_16_0006.html). If it is on the same machine as the one used for compiling the acceleration library, no installation is required

- Add the ACL library. The /path/ComputeLibrary-23.08/build directory should contain libarm_compute.so, libarm_compute_core.so, and libarm_compute_graph.so
  
  ```bash
  # Replace /path with the actual path
  export LD_LIBRARY_PATH=/path/ComputeLibrary-23.08/build:$LD_LIBRARY_PATH
  ```

- Add the onnxruntime library link itself
  
  ```bash
  export LD_LIBRARY_PATH=$(python -c 'import sysconfig,os;print(os.path.join(sysconfig.get_paths()["purelib"],"onnxruntime","capi"))'):$LD_LIBRARY_PATH
  ```

When using a **Kunpeng 950 processor**, install **acceleration library 50** with pip

```bash
pip install onnxruntime-xx.whl
```

The environment variables to be configured are as follows:

- Add the onnxruntime library link itself
  
  ```bash
  export LD_LIBRARY_PATH=$(python -c 'import sysconfig,os;print(os.path.join(sysconfig.get_paths()["purelib"],"onnxruntime","capi"))'):$LD_LIBRARY_PATH
  ```

**Function Verification**
After the environment is deployed, you can run the unit tests to verify that the functions work properly. In the /PI path, run the command

```bash
python function_test.py
```

Expected output

```bash
.................
----------------------------------------------------------------------
Ran 17 tests in 1.703s

OK
```

---

## Performance Optimization Command

After deployment, use the following command to run scripts for maximum performance:

```bash
# example command
numactl --cpunodebind=0 --membind=0 python script_name.py
```

`numactl` binds the process to the CPU and memory of NUMA node 0, reducing cross-node access latency.
