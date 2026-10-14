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
| ONNX Runtime | 1.28.0及以上 | 必选 |
| cJSON | 1.7.19及以上 | 必选 |

## 环境配置与编译

1. 下载ONNX Runtime预编译包。

    ```bash
    mkdir -p /opt/KADD/SQLi/util/ && cd /opt/KADD/SQLi/util/
    wget --no-check-certificate https://github.com/microsoft/onnxruntime/releases/download/v1.28.0/onnxruntime-linux-aarch64-1.28.0.tgz
    ```

2. 解压ONNX Runtime。

    ```bash
    tar -xzf onnxruntime-linux-aarch64-1.28.0.tgz
    ```

3. 下载并安装cJSON。

    ```bash
    mkdir -p /opt/KADD/SQLi/util/cJSON && cd /opt/KADD/SQLi/util/
    wget --no-check-certificate https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.19.zip -O cJSON-1.7.19.zip
    unzip cJSON-1.7.19.zip
    cp cJSON-1.7.19/cJSON.h cJSON-1.7.19/cJSON.c /opt/KADD/SQLi/util/cJSON/
    ```

4. 下载并安装Google Test。

    ```bash
    mkdir -p /opt/KADD/SQLi/util/gtest && cd /opt/KADD/SQLi/util/
    wget --no-check-certificate https://github.com/google/googletest/archive/refs/tags/release-1.12.1.tar.gz
    tar xzf release-1.12.1.tar.gz
    cp -rf googletest-release-1.12.1/googletest/include /opt/KADD/SQLi/util/gtest/
    cp -rf googletest-release-1.12.1/googletest/src /opt/KADD/SQLi/util/gtest/
    ```

5. 编译demo。demo程序源码位于 `/opt/KADD/SQLi/demo/` 目录，使用Makefile编译。

    ```bash
    mkdir -p /opt/KADD/SQLi/build && cd /opt/KADD/SQLi/build
    cmake ..
    cmake --build .
    export ONNXRUNTIME_HOME=/opt/KADD/SQLi/util/onnxruntime-linux-aarch64-1.28.0
    export LD_LIBRARY_PATH=$ONNXRUNTIME_HOME/lib:$LD_LIBRARY_PATH
    cd /opt/KADD/SQLi/build/demo
    ```

6. 运行demo。

    ```bash
    ./kadd_config_demo
    ```

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
export LD_LIBRARY_PATH=../src:/opt/KADD/SQLi/util/onnxruntime-linux-aarch64-1.28.0/lib:$LD_LIBRARY_PATH
export KADD_MODEL_HMAC_KEY=74fab58d87dc235ee049b6faf21b44825dba1d9dc05bbc14aeded49cb211a1db   # 开发密钥；用 build.sh 构建时可直接 source ../kadd_env.sh
./kadd_sqli_demo
```

### SQLi特性单独构建（生产部署）

1. 进入SQLi目录并执行一键构建。

    ```bash
    cd /opt/KADD/SQLi
    chmod +x build.sh && ./build.sh
    ```

2. 如需手动构建，配置并编译工程。

    ```bash
    mkdir -p /opt/KADD/SQLi/build && cd /opt/KADD/SQLi/build
    export ONNXRUNTIME_HOME=/opt/KADD/SQLi/util/onnxruntime-linux-aarch64-1.28.0
    cmake ..
    cmake --build .
    ```

### 单元测试构建（GCC编译）

1. 配置并编译单元测试工程。

    ```bash
    cd /opt/KADD/SQLi
    mkdir -p build_ut && cd build_ut
    export ONNXRUNTIME_HOME=/opt/KADD/SQLi/util/onnxruntime-linux-aarch64-1.28.0
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

#### 一键构建（长稳吞吐，推荐）

1. 按后端分别构建Fuzz测试工程。产物按后端分目录存放，两套可同时存在、互不覆盖；共用一个目录切换`SVE`会复用CMake缓存，既拖慢构建又容易跑错后端。

    ```bash
    cd /opt/KADD/SQLi
    ./build.sh fuzz            # SVE后端  → build_fuzz_sve/tests/kadd_fuzzer
    SVE=OFF ./build.sh fuzz    # ONNX后端 → build_fuzz_onnx/tests/kadd_fuzzer
    ```

2. 加载运行环境并运行Fuzz测试。

    ```bash
    cd /opt/KADD/SQLi/build_fuzz_sve/tests    # ONNX后端换成build_fuzz_onnx/tests
    source ../kadd_env.sh                     # 库搜索路径 + 模型HMAC密钥
    ./kadd_fuzzer -max_total_time=90          # 交互式验证建议加KADD_FUZZ_KEEP_STDOUT=1
    ```

    ![](../../public_sys-resources/icon-note.gif) **说明：**

    - Fuzzer初始化完成后默认把stdout/stderr一并重定向到`/dev/null`，3000万次长稳运行不产生刷屏日志；进度写入同目录的单行状态文件`kadd_fuzz_stat_<pid>.txt`（每10秒或每4096次迭代覆盖写，`-runs`跑满时写最终值），内容为`kadd_fuzz_stat: pid=.. iter=.. up_s=.. init_s=.. win_exec_s=.. all_exec_s=.. eq_ok=1 api_calls>=.. api=[21个等频接口独立计数] api_min>=..`。
    - `api_min>=30000000`即长稳达标（单接口口径）：`tools/fuzz_longrun.sh`会设置`KADD_FUZZ_EQUAL_RANDOM=1`，在完整随机fuzz之上每次迭代对21个可长跑开放接口各做1次真实随机调用（有效句柄+随机输入）；`win_exec_s`是不含初始化的稳态吞吐，libFuzzer的`stat::average_exec_per_sec`分母含初始化，不能用来判断快慢。
    - 需要日志时设置`KADD_FUZZ_KEEP_STDOUT=1`，复现`-artifact_prefix`目录下的崩溃样本同样用它（崩溃输入落盘不受静音影响）；`KADD_FUZZ_QUIET_STATS=1`连状态文件也不写；`KADD_FUZZ_STAT_FILE=<路径>`可指定状态文件位置，多路并行时给每路一个独立路径。
    - 21个可长跑接口的等频随机长稳请使用下方`tools/fuzz_longrun.sh`，不要手工用普通fuzz模式替代。

#### 开放函数3000万次长稳（带覆盖率运行计数留存）

若要求长稳报告可点开函数查看每个开放函数的运行次数（约3000万次量级），使用`tools/fuzz_longrun.sh`一键完成：默认32实例并行、按`-runs`跑满（含15%余量，不设时限）、覆盖率插桩构建，结束后自动合并各实例profraw并生成HTML。该脚本会自动设置`KADD_FUZZ_EQUAL_RANDOM=1`（等频随机长稳模式）：selector、heavy与fault-inject等随机fuzz路径全部保留，另叠加21个可长跑开放接口（predict/batch/detect/tokenize/feature_dim类）的等频真实随机调用层；17个load/create/free生命周期接口因文件I/O、模型解析、ONNX会话创建或释放语义不适合千万次长跑而明确豁免，并在报告中单列原因。脚本同时执行覆盖率门槛判定（每个C文件函数100%、行≥80%），未达标即报错退出。

```bash
cd /opt/KADD/SQLi
./tools/fuzz_longrun.sh            # SVE后端 → build_longrun_sve/
./tools/fuzz_longrun.sh onnx       # ONNX后端 → build_longrun_onnx/
```

| 产物 | 内容 |
|------|------|
| `build_longrun_*/fuzz_report.txt` | 长稳报告（21个等频接口独立调用次数、豁免接口清单、崩溃样本数、每实例状态） |
| `build_longrun_*/lr.profdata` | 各实例profraw合并后的覆盖率数据（含运行次数） |
| `build_longrun_*/lr_coverage.txt` | 覆盖率汇总文本 |
| `build_longrun_*/lr_cov_html/index.html` | 覆盖率HTML，点开函数所在行可见每个开放函数约3000万次运行计数 |
| `build_longrun_*/longrun_*.tar.gz` | 以上产物打包，便于回传留档 |

可用环境变量调整规模：`N=8 API_CALLS=1000000 ./tools/fuzz_longrun.sh`。插桩构建比纯吞吐版（`./build.sh fuzz`）慢约10%~30%，但运行计数可留档。

#### 手工构建（覆盖率插桩）

1. 配置并编译Fuzz测试工程。

    ```bash
    cd /opt/KADD/SQLi
    mkdir -p build_fuzz && cd build_fuzz
    export ONNXRUNTIME_HOME=/opt/KADD/SQLi/util/onnxruntime-linux-aarch64-1.28.0
    cmake -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang \
          -DENABLE_CODE_COVERAGE=ON ..
    cmake --build .
    ```

2. 运行Fuzz测试并生成覆盖率数据。测试时长为3小时。Fuzz构建默认不叠加AddressSanitizer（长稳吞吐优先），因此无需`-detect_leaks=0`；若按`tests/CMakeLists.txt`中的注释手工改回`-fsanitize=fuzzer,address`排查内存问题，因ONNX Runtime存在已知泄漏，需再加`-detect_leaks=0`。不建议传语料库目录：`assets/fuzz_corpus/*.txt`首字节均为`#`(35)，会把分层selector聚集到35，使公共API、内部API、自由输入约84.8/12.1/3.1的比重失效。

    ```bash
    cd /opt/KADD/SQLi/build_fuzz/tests
    ./kadd_fuzzer -runs=1000 -max_total_time=10800 > /dev/null 2>&1
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
