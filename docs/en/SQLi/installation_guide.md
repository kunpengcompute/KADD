# Installation Guide

This document describes how to install the KADD SQL injection detection engine.

## Environment Requirements

### Verified Environments

The processor is a new Kunpeng 920 processor model, and the OS is openEuler 24.03 LTS.

### Software Requirements

| Item| Requirement|Description|
|------|------|------|
| GCC | 10.3.1 or later| Mandatory|
| CMake | 2.8.11 or later| Mandatory|
| ONNX Runtime | 1.16.0 or later| Mandatory|
| cJSON | 1.7.19 or later| Mandatory|

## Environment Configuration and Compilation

Step 1: Obtain the project code.

```bash
cd /opt
git clone https://gitcode.com/boostkit/kadd-open.git
cd kadd-open
```

Step 2: Obtain the library files.

```bash
mkdir /opt/kadd-open/lib && cd /opt/kadd-open/lib
wget /path/to/KADD-1.0.0.zip
unzip KADD-1.0.0.zip
```

Step 3: Install ONNX Runtime.

```bash
# Download the precompiled package (AArch64).
mkdir -p /opt/kadd-open/util/
cd /opt/kadd-open/util/
wget https://github.com/microsoft/onnxruntime/releases/download/v1.16.0/onnxruntime-linux-aarch64-1.16.0.tgz

# Decompress the package.
tar -xzf onnxruntime-linux-aarch64-1.16.0.tgz
```

Step 4: Install cJSON.

```bash
# 1. Download the version 1.7.19.
mkdir -p /opt/kadd-open/util/
cd /opt/kadd-open/util/
wget https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.19.zip

# 2. Decompress the package and compile cJSON.
unzip cJSON-1.7.19.zip
cd cJSON-1.7.19
mkdir build && cd build
cmake ..
make

# 3. After the compilation is complete, view the library file.
ls -la libcjson.so*
```

The following command output is displayed:

```bash
lrwxrwxrwx 1 root root 13 ... libcjson.so -> libcjson.so.1
lrwxrwxrwx 1 root root 18 ... libcjson.so.1 -> libcjson.so.1.7.19
-rwxr-xr-x 1 root root 76096 ... libcjson.so.1.7.19
```

Step 5: Compile the demo programs.

The source code of the demo programs is stored in the `/opt/kadd-open/demo/` directory. Compile the code using Makefile.

```bash
cd /opt/kadd-open/demo
export LD_LIBRARY_PATH=/opt/kadd-open/lib:$LD_LIBRARY_PATH
export ONNXRUNTIME_HOME=/opt/kadd-open/util/onnxruntime-linux-aarch64-1.16.0
export LD_LIBRARY_PATH=$ONNXRUNTIME_HOME/lib:$LD_LIBRARY_PATH
export CJSON_BUILD_DIR=/opt/kadd-open/util/cJSON-1.7.19/build
export LD_LIBRARY_PATH=$CJSON_BUILD_DIR:$LD_LIBRARY_PATH
make
```

After the compilation is successful, the executable files `kadd_sqli_demo`, `kadd_dfa_demo`, `kadd_ml_demo`, `kadd_feature_demo`, and `kadd_config_demo` are generated.
