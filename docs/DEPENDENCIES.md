# 第三方库安装指南

本文档详细说明 KADD 项目所需第三方库的安装方法。

## 目录

- [系统要求](#系统要求)
- [编译器要求](#编译器要求)
- [ONNX Runtime](#onnx-runtime)
- [cJSON](#cjson)
- [Google Test](#google-test)

---

## 系统要求

### 硬件要求

| 组件 | 最低要求 | 推荐配置 |
|------|----------|----------|
| CPU | ARMv8-A | ARMv9-A (SVE 支持) |
| 架构 | aarch64/armv7l | aarch64 |
| 内存 | 512MB | 1GB+ |
| 存储 | 100MB | 500MB+ |

### 操作系统

- **推荐**: ARM Linux (Ubuntu 20.04+, Debian 11+, CentOS 8+)
- **支持**: 所有主流 ARM Linux 发行版

---

## 编译器要求

### GCC 版本

| 功能 | 最低 GCC 版本 | 推荐版本 |
|------|---------------|----------|
| 基础编译 | GCC 7+ | GCC 10+ |
| ARM NEON | GCC 8+ | GCC 10+ |
| ARM SVE | GCC 10+ | GCC 11+ |
| ARM SVE2 | GCC 12+ | GCC 13+ |

### 验证编译器版本

```bash
# 检查 GCC 版本
gcc --version

# 检查支持的架构
gcc -march=native -Q --help=target | grep march

# 检查 SVE 支持
echo | gcc -march=armv9-a+sve -E - | grep SVE
```

### 安装编译器

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install build-essential gcc g++

# CentOS/RHEL
sudo yum groupinstall "Development Tools"
sudo yum install gcc gcc-c++

# Arch Linux
sudo pacman -S base-devel gcc
```

---

---

---

## ONNX Runtime

ONNX Runtime 是 KADD 的必需依赖，用于 ML 模型推理。

### 系统要求

- ARM Linux (aarch64/armv7l)
- 最小内存: 512MB
- 推荐内存: 1GB+

### 方式 1: 包管理器安装（推荐）

#### Ubuntu/Debian

```bash
# 更新包列表
sudo apt-get update

# 安装 ONNX Runtime
sudo apt-get install libonnxruntime-dev

# 验证安装
ldconfig -p | grep onnxruntime
```

#### CentOS/RHEL

```bash
# CentOS/RHEL 需要启用 EPEL
sudo yum install epel-release
sudo yum install onnxruntime-devel
```

#### Arch Linux

```bash
sudo pacman -S onnxruntime
```

### 方式 2: 手动安装

如果系统没有预编译包，可以手动安装：

#### 下载预编译版本

```bash
# ARM64 (aarch64) 版本
wget https://github.com/microsoft/onnxruntime/releases/download/v1.16.0/onnxruntime-linux-aarch64-1.16.0.tgz

# 解压
tar xzf onnxruntime-linux-aarch64-1.16.0.tgz
cd onnxruntime-linux-aarch64-1.16.0
```

#### 安装到系统目录

```bash
# 复制库文件
sudo cp lib/libonnxruntime.so* /usr/local/lib/

# 复制头文件
sudo cp -r include/onnxruntime /usr/local/include/

# 更新动态库缓存
sudo ldconfig
```

#### 验证安装

```bash
# 检查库文件
ls -l /usr/local/lib/libonnxruntime.so*

# 检查头文件
ls /usr/local/include/onnxruntime/

# 测试链接
gcc -l:libonnxruntime.so -o /dev/null -xc - <<< 'int main(){return 0;}'
```

### 方式 3: 使用环境变量

适合需要特定版本或多版本共存的情况：

```bash
# 下载并解压到指定目录
mkdir -p ~/onnxruntime
tar xzf onnxruntime-linux-aarch64-1.16.0.tgz -C ~/onnxruntime

# 设置环境变量
export ONNXRUNTIME_HOME=~/onnxruntime/onnxruntime-linux-aarch64-1.16.0
export LD_LIBRARY_PATH=$ONNXRUNTIME_HOME/lib:$LD_LIBRARY_PATH

# 添加到 ~/.bashrc 永久生效
echo 'export ONNXRUNTIME_HOME=~/onnxruntime/onnxruntime-linux-aarch64-1.16.0' >> ~/.bashrc
echo 'export LD_LIBRARY_PATH=$ONNXRUNTIME_HOME/lib:$LD_LIBRARY_PATH' >> ~/.bashrc
```

### 方式 4: third_party 目录

适合无管理员权限的环境：

```bash
# 在项目根目录
mkdir -p third_party/lib third_party/include

# 复制库文件
cp libonnxruntime.so* third_party/lib/

# 复制头文件
cp -r include/onnxruntime third_party/include/
```

CMake 会自动查找 `third_party/` 目录。

### 版本兼容性

| ONNX Runtime 版本 | 状态 | 说明 |
|-------------------|------|------|
| 1.20.x | ✅ 推荐 | 最新稳定版 |
| 1.18.x | ✅ 支持 | 稳定版 |
| 1.16.x | ✅ 支持 | ARM 优化版 |
| 1.17.x | ⚠️ 部分支持 | API 变化较多 |

### 故障排除

#### 问题: 找不到 libonnxruntime.so

```bash
# 检查库是否存在
ldconfig -p | grep onnxruntime

# 手动更新缓存
sudo ldconfig

# 检查环境变量
echo $LD_LIBRARY_PATH
```

#### 问题: 版本不兼容

```bash
# 检查版本
strings /usr/lib/libonnxruntime.so | grep "1\."

# 重新安装正确版本
sudo apt-get remove libonnxruntime-dev
sudo apt-get install libonnxruntime-dev=1.16.0-1
```

---

## cJSON

cJSON 是可选依赖，用于 DFA Pro 引擎和 SQLi 引擎。

### 方式 1: 包管理器安装

```bash
# Ubuntu/Debian
sudo apt-get install libcjson-dev

# CentOS/RHEL
sudo yum install cjson-devel

# Arch Linux
sudo pacman -S cjson
```

### 方式 2: 手动编译

```bash
# 下载源码
git clone https://github.com/DaveGamble/cJSON.git
cd cJSON

# 创建构建目录
mkdir build && cd build
cmake ..
make

# 安装
sudo make install

# 更新动态库缓存
sudo ldconfig
```

### 方式 3: third_party 目录

```bash
# 下载源码
wget https://github.com/DaveGamble/cJSON/archive/refs/heads/master.zip -O cJSON.zip
unzip cJSON.zip

# 复制到项目
mkdir -p third_party/src third_party/include
cp cJSON-master/cJSON.c third_party/src/
cp cJSON-master/cJSON.h third_party/include/
```

### 验证安装

```bash
# 检查头文件
ls /usr/include/cjson.h

# 检查库文件
ldconfig -p | grep cjson

# 测试程序
echo '#include <cJSON.h>' | gcc -xc - -o /dev/null
```

---

## Google Test

Google Test 是可选依赖，仅用于单元测试。

### 方式 1: 包管理器安装

```bash
# Ubuntu/Debian
sudo apt-get install libgtest-dev

# CentOS/RHEL
sudo yum install gtest-devel

# Arch Linux
sudo pacman -S gtest
```

### 方式 2: 手动编译

```bash
# 下载源码
git clone https://github.com/google/googletest.git
cd googletest
git checkout release-1.12.1

# 创建构建目录
mkdir build && cd build
cmake ..
make

# 安装
sudo make install
```

### 方式 3: third_party 目录

```bash
# 下载源码
git clone https://github.com/google/googletest.git third_party/gtest
cd third_party/gtest
git checkout release-1.12.1
```

CMake 会自动编译 `third_party/gtest/` 中的源码。

### 验证安装

```bash
# 检查头文件
ls /usr/include/gtest/gtest.h

# 检查库文件
ldconfig -p | grep gtest
```

---

## 依赖查找优先级

CMake 按以下顺序查找依赖：

1. **环境变量** - `ONNXRUNTIME_HOME`
2. **系统库** - `/usr/lib`, `/usr/local/lib`
3. **third_party** - 项目内 `third_party/` 目录

### 使用环境变量

```bash
# ONNX Runtime
export ONNXRUNTIME_HOME=/opt/onnxruntime-1.16.0

# 验证
cmake -H. -Bbuild
```

### 强制使用 third_party

```bash
cmake -DUSE_SYSTEM_LIBS=OFF ..
```

---

## 完整安装脚本

以下脚本安装所有依赖：

```bash
#!/bin/bash
set -e

echo "安装 KADD 依赖..."

# Ubuntu/Debian
if command -v apt-get &> /dev/null; then
    sudo apt-get update
    sudo apt-get install -y \
        cmake \
        build-essential \
        libonnxruntime-dev \
        libcjson-dev \
        libgtest-dev

# CentOS/RHEL
elif command -v yum &> /dev/null; then
    sudo yum install -y \
        cmake \
        gcc-c++ \
        onnxruntime-devel \
        cjson-devel \
        gtest-devel

# Arch Linux
elif command -v pacman &> /dev/null; then
    sudo pacman -S --needed \
        cmake \
        base-devel \
        onnxruntime \
        cjson \
        gtest
else
    echo "不支持的包管理器"
    exit 1
fi

# 更新动态库缓存
sudo ldconfig

# 验证安装
echo "验证安装..."
ldconfig -p | grep onnxruntime && echo "✓ ONNX Runtime"
ldconfig -p | grep cjson && echo "✓ cJSON"
ldconfig -p | grep gtest && echo "✓ Google Test"

echo "依赖安装完成！"
```

保存为 `install_deps.sh`，然后：

```bash
chmod +x install_deps.sh
./install_deps.sh
```

---

## 卸载依赖

### Ubuntu/Debian

```bash
sudo apt-get remove libonnxruntime-dev libcjson-dev libgtest-dev
sudo apt-get autoremove
```

### CentOS/RHEL

```bash
sudo yum remove onnxruntime-devel cjson-devel gtest-devel
```

### 手动安装的版本

```bash
# 删除库文件
sudo rm -f /usr/local/lib/libonnxruntime.so*
sudo rm -f /usr/local/lib/libcjson.so*
sudo rm -f /usr/local/lib/libgtest*.so*

# 删除头文件
sudo rm -rf /usr/local/include/onnxruntime
sudo rm -f /usr/local/include/cjson.h
sudo rm -rf /usr/local/include/gtest

# 更新缓存
sudo ldconfig
```

---

## 常见问题

### Q: 不同架构如何选择版本？

A: 使用 `uname -m` 检查架构：
- `aarch64` → 下载 `linux-aarch64` 版本
- `armv7l` → 下载 `linux-arm` 版本
- `x86_64` → 下载 `linux-x64` 版本

### Q: 可以使用静态链接吗？

A: ONNX Runtime 不推荐静态链接，但可以：

```bash
# 下载静态库版本
wget https://github.com/microsoft/onnxruntime/releases/download/v1.16.0/onnxruntime-linux-aarch64-static-1.16.0.tgz
```

### Q: 如何切换不同版本的 ONNX Runtime？

A: 使用环境变量：

```bash
# 切换到 1.16.0
export ONNXRUNTIME_HOME=~/onnxruntime-1.16.0
cmake ..

# 切换到 1.20.0
export ONNXRUNTIME_HOME=~/onnxruntime-1.20.0
cmake ..
```

---

## 参考链接

- [ONNX Runtime GitHub](https://github.com/microsoft/onnxruntime)
- [ONNX Runtime 官方文档](https://onnxruntime.ai/docs/)
- [cJSON GitHub](https://github.com/DaveGamble/cJSON)
- [Google Test GitHub](https://github.com/google/googletest)
