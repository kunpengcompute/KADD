# 安装指南

本指南说明 KADD SQL 注入检测引擎的安装步骤。

## 关于 KADD

KADD 是一个安全检测工具集，通过根目录的 `build.sh` 一键构建所有特性。

| 特性 | 说明 | 状态 |
|------|------|------|
| SQLi | SQL 注入检测 | ✅ 已实现 |
| PI | 密码注入检测 | 🚧 开发中 |

## 环境要求

### 已验证环境

处理器为鲲鹏920新型号处理器，操作系统为openEuler 24.03 LTS。

### 软件要求

| 项目 | 要求 |说明|
|------|------|------|
| GCC | 10.3.1及以上 | 必选 |
| CMake | 2.8.11及以上 | 必选 |
| ONNX Runtime | 1.16.0及以上 | 必选 |
| cJSON | 1.7.19及以上 | 必选 |

## 环境配置与编译

步骤1. ONNX Runtime

```bash
# 下载预编译包（ARM64）
mkdir -p /opt/KADD/SQLi/util/ && cd /opt/KADD/SQLi/util/
wget https://github.com/microsoft/onnxruntime/releases/download/v1.16.0/onnxruntime-linux-aarch64-1.16.0.tgz

# 解压
tar -xzf onnxruntime-linux-aarch64-1.16.0.tgz
```

步骤2. cJSON

```bash
mkdir -p /opt/KADD/SQLi/util/cJSON && cd /opt/KADD/SQLi/util/
wget -q https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.19.zip -O cJSON-1.7.19.zip
unzip -q cJSON-1.7.19.zip
cp cJSON-1.7.19/cJSON.h cJSON-1.7.19/cJSON.c /opt/KADD/SQLi/util/cJSON/
```

步骤3. Google Test

```bash
mkdir -p /opt/KADD/SQLi/util/gtest && cd /opt/KADD/SQLi/util/
wget https://buildtools.obs.cn-north-4.myhuaweicloud.com/googletest-release-1.12.1.tar.gz
tar xzf googletest*.tar.gz
cp -rf googletest-release-1.12.1/googletest/include /opt/KADD/SQLi/util/gtest/
cp -rf googletest-release-1.12.1/googletest/src /opt/KADD/SQLi/util/gtest/
```

步骤4. 编译 demo

demo 程序源码位于 `/opt/KADD/SQLi/demo/` 目录，使用 Makefile 编译：

```bash
mkdir -p /opt/KADD/SQLi/build && cd /opt/KADD/SQLi/build
cmake ..
cmake --build .
export ONNXRUNTIME_HOME=/opt/KADD/SQLi/util/onnxruntime-linux-aarch64-1.16.0
export LD_LIBRARY_PATH=$ONNXRUNTIME_HOME/lib:$LD_LIBRARY_PATH
cd /opt/KADD/SQLi/build/demo

# demo执行
./kadd_config_demo

```

---

## 构建指南

### 🏗️ 根目录一键构建（推荐）

**适用场景**：构建所有特性（当前包含 SQLi，未来支持 PI）

```bash
cd /opt/KADD
chmod +x build.sh && ./build.sh
```

**输出位置**：
- SQLi 库文件：`/opt/KADD/SQLi/build/src/`
- SQLi Demo：`/opt/KADD/SQLi/build/demo/`
- SQLi 测试：`/opt/KADD/SQLi/build/tests/`

**运行示例**：
```bash
cd /opt/KADD/SQLi/build/demo
export LD_LIBRARY_PATH=../src:/opt/KADD/SQLi/util/onnxruntime-linux-aarch64-1.16.0/lib:$LD_LIBRARY_PATH
./kadd_sqli_demo
```

---

### 📦 SQLi 特性单独构建（生产部署）

```bash
# 一键构建（下载依赖 + 编译）
cd /opt/KADD/SQLi
chmod +x build.sh && ./build.sh

# 或手动构建
mkdir -p /opt/KADD/SQLi/build && cd /opt/KADD/SQLi/build
export ONNXRUNTIME_HOME=/opt/KADD/SQLi/util/onnxruntime-linux-aarch64-1.16.0
cmake ..
cmake --build .
```

### 🧪 单元测试构建（GCC 编译）

```bash
cd /opt/KADD/SQLi
mkdir -p build_ut && cd build_ut
export ONNXRUNTIME_HOME=/opt/KADD/SQLi/util/onnxruntime-linux-aarch64-1.16.0
cmake -DENABLE_CODE_COVERAGE=ON ..
cmake --build .

# 运行测试
ctest --verbose

# 生成 ut 覆盖率报告
make coverage
```

### 🎯 Fuzz 测试构建（Clang + ASan）

```bash
cd /opt/KADD/SQLi
mkdir -p build_fuzz && cd build_fuzz
export ONNXRUNTIME_HOME=/opt/KADD/SQLi/util/onnxruntime-linux-aarch64-1.16.0
cmake -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang \
      -DENABLE_CODE_COVERAGE=ON ..
cmake --build .

# 运行 Fuzz 生成覆盖率数据（3小时，ONNX 内存泄漏故使用 -detect_leaks=0）
cd /opt/KADD/SQLi/build_fuzz/tests
export ASAN_OPTIONS=detect_leaks=0
./kadd_fuzzer -detect_leaks=0 -runs=3000 -max_total_time=10800 \
    /opt/KADD/SQLi/assets/fuzz_corpus > /dev/null 2>&1

# 展示覆盖率报告
cd /opt/KADD/SQLi/build_fuzz/tests
llvm-profdata merge -sparse default.profraw -o kadd.profdata
llvm-cov report ./kadd_fuzzer -instr-profile=kadd.profdata \
    -ignore-filename-regex="tests/.*|include/.*|util/.*"

# 生成 fuzz 覆盖率报告
llvm-cov show ./kadd_fuzzer -instr-profile=kadd.profdata \
    -format=html -o coverage_html \
    -ignore-filename-regex="tests/.*|include/.*|util/.*"
```

### 🔧 编译选项速查表

| 选项 | 默认值 | 说明 | 使用场景 |
|------|--------|------|---------|
| `-DCMAKE_BUILD_TYPE=Release` | Debug | 发布构建（-O3 优化） | 生产部署 |
| `-DKADD_OPTIMIZE=ON` | OFF | 额外优化选项 | 性能测试 |
| `-DENABLE_ARM_SVE=ON` | ON | ARM SVE 加速（默认开启） | ARM 平台 |
| `-DENABLE_CODE_COVERAGE=ON` | OFF | 代码覆盖率 | 覆盖率分析 |
| `-DBUILD_TESTS=ON` | ON | 构建测试程序 | 测试构建 |
| `-DCMAKE_CXX_COMPILER=clang++` | auto | 指定编译器 | Fuzz/覆盖 |
| `-DCMAKE_C_COMPILER=clang` | auto | 指定编译器 | Fuzz/覆盖 |
