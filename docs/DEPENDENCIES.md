# 第三方库安装指南

本文档说明 KADD 项目所需第三方库的安装方法。

## 快速安装

使用系统包管理器一键安装：

```bash
# Ubuntu/Debian: apt-get install libonnxruntime-dev libcjson-dev libgtest-dev
# CentOS/RHEL: yum install onnxruntime-devel cjson-devel gtest-devel
# Arch Linux: pacman -S onnxruntime cjson gtest
```

---

## ONNX Runtime

**必需依赖**，用于 ML 模型推理。推荐版本：1.16.x 或 1.20.x

### 手动安装

```bash
# 下载预编译包（根据架构选择：linux-x64 / linux-aarch64）
wget https://github.com/microsoft/onnxruntime/releases/download/v1.16.0/onnxruntime-linux-aarch64-1.16.0.tgz
tar xzf onnxruntime-linux-*.tgz

# 安装到系统目录
sudo cp lib/libonnxruntime.so* /usr/local/lib/
sudo cp -r include/onnxruntime /usr/local/include/
sudo ldconfig
```

### third_party 方式

```bash
# 项目根目录执行
mkdir -p third_party/lib third_party/include
cp lib/libonnxruntime.so* third_party/lib/
cp -r include/onnxruntime third_party/include/
```

### 环境变量

```bash
export ONNXRUNTIME_HOME=/path/to/onnxruntime
export LD_LIBRARY_PATH=$ONNXRUNTIME_HOME/lib:$LD_LIBRARY_PATH
```

---

## cJSON

**可选依赖**，用于 DFA 引擎和 SQLi 引擎。

### 手动编译

```bash
git clone https://github.com/DaveGamble/cJSON.git
cd cJSON && mkdir build && cd build
cmake .. && make
sudo make install && sudo ldconfig
```

### third_party 方式

```bash
mkdir -p third_party/src third_party/include
wget https://github.com/DaveGamble/cJSON/archive/refs/heads/master.zip -O cJSON.zip
unzip cJSON.zip && cp cJSON-master/cJSON.* third_party/src/
cp cJSON-master/cJSON.h third_party/include/
```

---

## Google Test

**可选依赖**，仅用于单元测试。

### 手动编译

```bash
git clone https://github.com/google/googletest.git
cd googletest && git checkout release-1.12.1
mkdir build && cd build
cmake .. && make
sudo make install
```

### third_party 方式

```bash
git clone https://github.com/google/googletest.git third_party/gtest
cd third_party/gtest && git checkout release-1.12.1
```

---

## 依赖查找优先级

CMake 按以下顺序查找：环境变量 → 系统库 → third_party 目录

```bash
# 指定版本
export ONNXRUNTIME_HOME=/opt/onnxruntime-1.16.0

# 强制使用 third_party
cmake -DUSE_SYSTEM_LIBS=OFF ..
```

---

## 版本兼容性

| 库 | 推荐版本 | 说明 |
|---|---|---|
| ONNX Runtime | 1.16.x / 1.20.x | ARM 优化版 / 最新稳定版 |
| cJSON | master | 稳定 |
| Google Test | 1.12.1 | 稳定 |

---

## 常见问题

**Q: 不同架构如何选择版本？**

A: 使用 `uname -m` 检查：aarch64 → linux-aarch64，x86_64 → linux-x64

**Q: 如何切换 ONNX Runtime 版本？**

A: 修改 `ONNXRUNTIME_HOME` 环境变量指向不同版本目录

---

## 参考链接

- [ONNX Runtime](https://github.com/microsoft/onnxruntime)
- [cJSON](https://github.com/DaveGamble/cJSON)
- [Google Test](https://github.com/google/googletest)
