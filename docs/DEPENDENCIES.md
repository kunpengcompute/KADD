# 第三方库安装指南

本文档说明 KADD 项目所需第三方库的安装方法。

## third_party 目录结构

项目使用统一的目录结构管理第三方依赖：

```
third_party/
├── cJSON/              # JSON 解析库
│   ├── cJSON.h
│   └── cJSON.c
├── gtest/              # Google Test 测试框架
│   ├── include/gtest/
│   │   ├── gtest.h
│   │   └── internal/
│   └── src/
│       ├── gtest-all.cc
│       └── gtest_main.cc
└── onnxruntime/        # ONNX Runtime 推理引擎
    ├── include/onnxruntime/
    └── lib/
        └── libonnxruntime.so*
```

## 快速安装

使用系统包管理器一键安装：

```bash
# Ubuntu/Debian: apt-get install libonnxruntime-dev libcjson-dev libgtest-dev
# CentOS/RHEL: yum install onnxruntime-devel cjson-devel gtest-devel
# Arch Linux: pacman -S onnxruntime cjson gtest
```

---

## ONNX Runtime

**必需依赖**，用于 ML 模型推理。推荐版本：1.16.0

### 版本选择

| 版本 | 特点 | 适用场景 |
|------|------|----------|
| 1.16.0 | 稳定版，API 版本 15-17 | 生产环境推荐 |
| 1.20.x | 最新特性，性能优化 | 开发测试 |

### 手动安装（系统级）

```bash
# 检测系统架构
uname -m
# aarch64 → ARM64（树莓派、Jetson、ARM 服务器）
# x86_64 → Intel/AMD 64位

# 下载预编译包
wget https://github.com/microsoft/onnxruntime/releases/download/v1.16.0/onnxruntime-linux-x64-1.16.0.tgz
# 或 ARM64:
wget https://github.com/microsoft/onnxruntime/releases/download/v1.16.0/onnxruntime-linux-aarch64-1.16.0.tgz

# 解压
tar xzf onnxruntime-linux-*.tgz
cd onnxruntime-linux-*

# 安装到系统目录
sudo cp lib/libonnxruntime.so* /usr/local/lib/
sudo cp -r include/* /usr/local/include/onnxruntime/
sudo ldconfig

# 验证安装
pkg-config --modversion onnxruntime  # 如果有 .pc 文件
ldconfig -p | grep onnxruntime       # 检查库是否被找到
```

### third_party 方式

```bash
wget https://buildtools.obs.cn-north-4.myhuaweicloud.com/onnxruntime-linux-aarch64-1.16.0.tgz
tar xzf onnxruntime-linux-*.tgz

mkdir -p third_party/onnxruntime/lib third_party/onnxruntime/include/onnxruntime
cp onnxruntime-linux-*/lib/* third_party/onnxruntime/lib/
cp -r onnxruntime-linux-*/include/* third_party/onnxruntime/include/onnxruntime/

rm -rf onnxruntime-linux-*
```

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
mkdir -p third_party/cJSON
wget https://buildtools.obs.cn-north-4.myhuaweicloud.com/cJson-master.zip
unzip -q cJSON*.zip
cp cJSON-master/cJSON.h third_party/cJSON/
cp cJSON-master/cJSON.c third_party/cJSON/
rm -rf cJSON-master cJSON.zip
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
wget https://buildtools.obs.cn-north-4.myhuaweicloud.com/googletest-release-1.12.1.tar.gz
tar xzf googletest*.tar.gz
mkdir -p third_party/gtest
cp -r googletest-release-1.12.1/googletest/include third_party/gtest/
cp -r googletest-release-1.12.1/googletest/src third_party/gtest/
rm -rf googletest*
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
| ONNX Runtime | 1.16.0 | 稳定版，API 版本 15-17 |
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