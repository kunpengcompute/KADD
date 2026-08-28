# 安装指南

本指南说明 KADD SQL 注入检测引擎的安装步骤。

## 关于 KADD SQL

KADD SQL 是一个SQL注入检测工具，通过目 `SQLi/build.sh` 一键构建。

## 环境要求

### 已验证环境

处理器为鲲鹏920新型号处理器，操作系统为openEuler 24.03 LTS。

### 软件要求

| 项目 | 要求 |说明|
|------|------|------|
| GCC | 10.3.1及以上 | 必选 |
| CMake | 2.8.11及以上 | 必选 |
| ONNX Runtime | 1.18.0及以上 | 必选 |
| cJSON | 1.7.19及以上 | 必选 |

## 环境配置与编译

1. 下载ONNX Runtime预编译包。

    ```bash
    mkdir -p /opt/KADD/SQLi/util/ && cd /opt/KADD/SQLi/util/
    wget https://github.com/microsoft/onnxruntime/releases/download/v1.18.0/onnxruntime-linux-aarch64-1.18.0.tgz
    ```

2. 解压ONNX Runtime。

    ```bash
    tar -xzf onnxruntime-linux-aarch64-1.18.0.tgz
    ```

3. 下载并安装cJSON。

    ```bash
    mkdir -p /opt/KADD/SQLi/util/cJSON && cd /opt/KADD/SQLi/util/
    wget -q https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.19.zip -O cJSON-1.7.19.zip
    unzip -q cJSON-1.7.19.zip
    cp cJSON-1.7.19/cJSON.h cJSON-1.7.19/cJSON.c /opt/KADD/SQLi/util/cJSON/
    ```

4. 下载并安装Google Test。

    ```bash
    mkdir -p /opt/KADD/SQLi/util/gtest && cd /opt/KADD/SQLi/util/
    wget https://buildtools.obs.cn-north-4.myhuaweicloud.com/googletest-release-1.12.1.tar.gz
    tar xzf googletest*.tar.gz
    cp -rf googletest-release-1.12.1/googletest/include /opt/KADD/SQLi/util/gtest/
    cp -rf googletest-release-1.12.1/googletest/src /opt/KADD/SQLi/util/gtest/
    ```

5. 编译demo。demo程序源码位于 `/opt/KADD/SQLi/demo/` 目录，使用Makefile编译。

    ```bash
    mkdir -p /opt/KADD/SQLi/build && cd /opt/KADD/SQLi/build
    cmake ..
    cmake --build .
    export ONNXRUNTIME_HOME=/opt/KADD/SQLi/util/onnxruntime-linux-aarch64-1.18.0
    export LD_LIBRARY_PATH=$ONNXRUNTIME_HOME/lib:$LD_LIBRARY_PATH
    cd /opt/KADD/SQLi/build/demo
    ```

6. 运行demo。

    ```bash
    ./kadd_config_demo
    ```

---

## 构建指南

### 根目录一键构建（推荐）

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
export LD_LIBRARY_PATH=../src:/opt/KADD/SQLi/util/onnxruntime-linux-aarch64-1.18.0/lib:$LD_LIBRARY_PATH
./kadd_sqli_demo
```

---

### SQLi特性单独构建（生产部署）

1. 进入SQLi目录并执行一键构建。

    ```bash
    cd /opt/KADD/SQLi
    chmod +x build.sh && ./build.sh
    ```

2. 如需手动构建，配置并编译工程。

    ```bash
    mkdir -p /opt/KADD/SQLi/build && cd /opt/KADD/SQLi/build
    export ONNXRUNTIME_HOME=/opt/KADD/SQLi/util/onnxruntime-linux-aarch64-1.18.0
    cmake ..
    cmake --build .
    ```

### 单元测试构建（GCC编译）

1. 配置并编译单元测试工程。

    ```bash
    cd /opt/KADD/SQLi
    mkdir -p build_ut && cd build_ut
    export ONNXRUNTIME_HOME=/opt/KADD/SQLi/util/onnxruntime-linux-aarch64-1.18.0
    cmake -DENABLE_CODE_COVERAGE=ON ..
    cmake --build .
    ```

2. 运行测试。

    ```bash
    ctest --verbose
    ```

3. 生成单元测试覆盖率报告。

    ```bash
    make coverage
    ```

### Fuzz测试构建（Clang + LibFuzzer）

1. 配置并编译Fuzz测试工程。

    ```bash
    cd /opt/KADD/SQLi
    mkdir -p build_fuzz && cd build_fuzz
    export ONNXRUNTIME_HOME=/opt/KADD/SQLi/util/onnxruntime-linux-aarch64-1.18.0
    cmake -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang \
          -DENABLE_CODE_COVERAGE=ON ..
    cmake --build .
    ```

2. 运行Fuzz测试并生成覆盖率数据。测试时长为3小时；因ONNX内存泄漏问题，需设置 `-detect_leaks=0`。

    ```bash
    cd /opt/KADD/SQLi/build_fuzz/tests
    export ASAN_OPTIONS=detect_leaks=0
    ./kadd_fuzzer -detect_leaks=0 -runs=1000 -max_total_time=10800 \
        /opt/KADD/SQLi/assets/fuzz_corpus > /dev/null 2>&1
    ```

3. 合并覆盖率数据。

    ```bash
    cd /opt/KADD/SQLi/build_fuzz/tests
    llvm-profdata merge -sparse default.profraw -o kadd.profdata
    ```

4. 查看覆盖率报告。

    ```bash
    llvm-cov report ./kadd_fuzzer -instr-profile=kadd.profdata \
        -ignore-filename-regex="tests/.*|include/.*|util/.*|cjson*"
    ```

5. 生成HTML格式覆盖率报告。

    ```bash
    llvm-cov show ./kadd_fuzzer -instr-profile=kadd.profdata \
        -format=html -o coverage_html \
        -ignore-filename-regex="tests/.*|include/.*|util/.*|cjson*"
    ```

### 编译选项速查表

| 选项 | 默认值 | 说明 | 使用场景 |
|------|--------|------|---------|
| `-DCMAKE_BUILD_TYPE=Release` | Debug | 发布构建（-O3 优化） | 生产部署 |
| `-DKADD_OPTIMIZE=ON` | OFF | 额外优化选项 | 性能测试 |
| `-DENABLE_ARM_SVE=ON` | ON | ARM SVE 加速（默认开启） | ARM 平台 |
| `-DKADD_SVE_VECTOR_BITS=256` | 256 | 固定 SVE 向量位宽（可设 128/256/512；空字符串=自适应任意宽度）。固定 256 时启用 SVE256 专用路径并把不可达的 VLA 回退从编译中排除，使覆盖率报告中不出现其 0% 函数 | ARM SVE 平台/覆盖率分析 |
| `-DENABLE_CODE_COVERAGE=ON` | OFF | 代码覆盖率 | 覆盖率分析 |
| `-DBUILD_TESTS=ON` | ON | 构建测试程序 | 测试构建 |
| `-DCMAKE_CXX_COMPILER=clang++` | auto | 指定编译器 | Fuzz/覆盖 |
| `-DCMAKE_C_COMPILER=clang` | auto | 指定编译器 | Fuzz/覆盖 |
