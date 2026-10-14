# Installation Guide

<!-- md-trans-meta sourceCommit=bd143b7dd3a8d6940dbfa762540fd554e881848e translatedAt=2026-09-22T04:34:26.491Z pushedAt=2026-09-23T14:28:29.588Z -->

This document describes how to install the KADD SQL injection detection engine.

## About KADD SQL

KADD SQL is an SQL injection detection tool that can be built with one click through `SQLi/build.sh`.

## Environment Requirements

### Verified Environments

The processor is a new Kunpeng 920 processor model, and the OS is openEuler 24.03 LTS.

### Software Requirements

| Item | Requirement | Description |
|------|------|------|
| GCC | 10.3.1 or later | Required |
| CMake | 2.8.11 or later | Required |
| ONNX Runtime | 1.28.0 or later | Required |
| cJSON | 1.7.19 or later | Required |

## Environment Configuration and Compilation

1. Download the ONNX Runtime precompiled package.

    ```bash
    mkdir -p /opt/KADD/SQLi/util/ && cd /opt/KADD/SQLi/util/
    wget --no-check-certificate https://github.com/microsoft/onnxruntime/releases/download/v1.28.0/onnxruntime-linux-aarch64-1.28.0.tgz
    ```

2. Extract ONNX Runtime.

    ```bash
    tar -xzf onnxruntime-linux-aarch64-1.28.0.tgz
    ```

3. Download and install cJSON.

    ```bash
    mkdir -p /opt/KADD/SQLi/util/cJSON && cd /opt/KADD/SQLi/util/
    wget --no-check-certificate https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.19.zip -O cJSON-1.7.19.zip
    unzip cJSON-1.7.19.zip
    cp cJSON-1.7.19/cJSON.h cJSON-1.7.19/cJSON.c /opt/KADD/SQLi/util/cJSON/
    ```

4. Download and install GoogleTest.

    ```bash
    mkdir -p /opt/KADD/SQLi/util/gtest && cd /opt/KADD/SQLi/util/
    wget --no-check-certificate https://github.com/google/googletest/archive/refs/tags/release-1.12.1.tar.gz
    tar xzf release-1.12.1.tar.gz
    cp -rf googletest-release-1.12.1/googletest/include /opt/KADD/SQLi/util/gtest/
    cp -rf googletest-release-1.12.1/googletest/src /opt/KADD/SQLi/util/gtest/
    ```

5. Compile the demo. The demo program source code is located in the `/opt/KADD/SQLi/demo/` directory and is compiled using Makefile.

    ```bash
    mkdir -p /opt/KADD/SQLi/build && cd /opt/KADD/SQLi/build
    cmake ..
    cmake --build .
    export ONNXRUNTIME_HOME=/opt/KADD/SQLi/util/onnxruntime-linux-aarch64-1.28.0
    export LD_LIBRARY_PATH=$ONNXRUNTIME_HOME/lib:$LD_LIBRARY_PATH
    cd /opt/KADD/SQLi/build/demo
    ```

6. Run the demo.

    ```bash
    ./kadd_config_demo
    ```



## Build Guide

### One-Click Build from the Root Directory (Recommended)

**Applicable scenario**: Build all features (currently includes SQLi; PI support is planned).

```bash
cd /opt/KADD
chmod +x build.sh && ./build.sh
```

**Output location**:

- SQLi library files: `/opt/KADD/SQLi/build/src/`
- SQLi demos: `/opt/KADD/SQLi/build/demo/`
- SQLi tests: `/opt/KADD/SQLi/build/tests/`

**Running example**:

```bash
cd /opt/KADD/SQLi/build/demo
export LD_LIBRARY_PATH=../src:/opt/KADD/SQLi/util/onnxruntime-linux-aarch64-1.28.0/lib:$LD_LIBRARY_PATH
export KADD_MODEL_HMAC_KEY=74fab58d87dc235ee049b6faf21b44825dba1d9dc05bbc14aeded49cb211a1db   # Development key; when building with build.sh, you can directly source ../kadd_env.sh
./kadd_sqli_demo
```



### Standalone SQLi Feature Build (Production Deployment)

1. Enter the `SQLi` directory and perform a one-click build.

    ```bash
    cd /opt/KADD/SQLi
    chmod +x build.sh && ./build.sh
    ```

2. To build manually, configure and compile the project.

    ```bash
    mkdir -p /opt/KADD/SQLi/build && cd /opt/KADD/SQLi/build
    export ONNXRUNTIME_HOME=/opt/KADD/SQLi/util/onnxruntime-linux-aarch64-1.28.0
    cmake ..
    cmake --build .
    ```

### Unit Test Build (GCC Compilation)

1. Configure and compile the unit test project.

    ```bash
    cd /opt/KADD/SQLi
    mkdir -p build_ut && cd build_ut
    export ONNXRUNTIME_HOME=/opt/KADD/SQLi/util/onnxruntime-linux-aarch64-1.28.0
    cmake -DENABLE_CODE_COVERAGE=ON ..
    cmake --build .
    ```

2. Run the test.

    ```bash
    ctest --verbose
    ```

3. Generate the unit test coverage report.

    ```bash
    make coverage
    ```

### Fuzz Test Build (Clang + LibFuzzer)

#### One-Click Build

1. Build the fuzz test project separately for each backend. The build products are stored in backend-specific directories, so the two builds can coexist without overwriting each other.

    ```bash
    cd /opt/KADD/SQLi
    ./build.sh fuzz            # SVE backend  → build_fuzz_sve/tests/kadd_fuzzer
    SVE=OFF ./build.sh fuzz    # ONNX backend → build_fuzz_onnx/tests/kadd_fuzzer
    ```

2. Load the runtime environment and run the fuzz test.

    ```bash
    cd /opt/KADD/SQLi/build_fuzz_sve/tests    # For the ONNX backend, use build_fuzz_onnx/tests instead.
    source ../kadd_env.sh                     # Library search path + model HMAC key
    ./kadd_fuzzer -max_total_time=90          # For interactive verification, you are advised to add KADD_FUZZ_KEEP_STDOUT=1.
    ```

    ![](../../public_sys-resources/icon-note.gif) **NOTE:**

    - After initialization, the fuzzer redirects both stdout and stderr to `/dev/null` by default, so a long-duration stability run with 30 million iterations produces no log spam. Progress is written to a single-line status file, `kadd_fuzz_stat_<pid>.txt`, in the same directory (overwritten every 10 seconds or every 4096 iterations, and written with the final values when `-runs` completes).  Its content is `kadd_fuzz_stat: pid=.. iter=.. up_s=.. init_s=.. win_exec_s=.. all_exec_s=.. eq_ok=1 api_calls>=.. api=[independent counters for the 21 equal-frequency interfaces] api_min>=..`.
    - `api_min>=30000000` means the long-duration stability target is met (per-interface basis): `tools/fuzz_longrun.sh` sets `KADD_FUZZ_EQUAL_RANDOM=1`. On top of full random fuzzing, each iteration makes one real call to each of the 21 interfaces suitable for long-duration stability runs (valid handle + random input). `win_exec_s` is the steady-state throughput excluding initialization; libFuzzer's `stat::average_exec_per_sec` takes into account initialization in its denominator, so it cannot be used to judge speed.
    - Set `KADD_FUZZ_KEEP_STDOUT=1` when logs are needed. Use it as well when reproducing crash samples under the `-artifact_prefix` directory (crash inputs are still written to the drive even in quiet mode). `KADD_FUZZ_QUIET_STATS=1` also skips writing the status file. `KADD_FUZZ_STAT_FILE=<path>` can specify the status file location; when running multiple threads in parallel, give each one its own path.
    - For equal-frequency long-duration stability testing of these 21 interfaces with random inputs, use `tools/fuzz_longrun.sh` below. Do not replace it with normal fuzzing mode.

#### 30-Million-Iteration Long-Duration Stability Testing for Open Functions (with Coverage and Run Count Retained)

If you want the long-duration stability report to support clicking a function to view its run count (on the order of 30 million), use `tools/fuzz_longrun.sh` to complete it in one click. By default, it runs 32 instances in parallel, runs to the full `-runs` count (with a 15% margin and no time limit), and uses a coverage-instrumented build. After completion, it automatically merges the .profraw files from all instances and generates the HTML-format report. The script automatically sets `KADD_FUZZ_EQUAL_RANDOM=1` (equal-frequency long-duration stability mode with random inputs): all random fuzzing paths such as `selector`, `heavy`, and `fault-inject` are retained, and an equal-frequency layer of real calls with random inputs is added for the 21 interfaces suitable for long-duration stability runs (predict/batch/detect/tokenize/feature_dim classes). The 17 lifecycle interfaces (load/create/free) are explicitly exempted because file I/O, model parsing, and ONNX session creation/release semantics make them unsuitable for long-duration stability testing with tens of millions of iterations; their exemption reasons are listed separately in the report. The script also performs coverage threshold checks (100% for function coverage per C file and ≥ 80% for line coverage) and exits with an error if the thresholds are not met.

```bash
cd /opt/KADD/SQLi
./tools/fuzz_longrun.sh            # SVE backend -> build_longrun_sve/
./tools/fuzz_longrun.sh onnx       # ONNX backend -> build_longrun_onnx/
```

| Output | Content |
|------|------|
| `build_longrun_*/fuzz_report.txt` | Long-duration stability report (independent call counts of the 21 equal-frequency interfaces, exempted interface list, crash sample count, and per-instance status) |
| `build_longrun_*/lr.profdata` | Coverage data after merging the .profraw files of all instances (including run counts) |
| `build_longrun_*/lr_coverage.txt` | Coverage summary text |
| `build_longrun_*/lr_cov_html/index.html` | Coverage HTML file; clicking the line where an open function is located shows the run count (about 30 million) for the function |
| `build_longrun_*/longrun_*.tar.gz` | The above outputs packaged for return and archival |

You can use environment variables to adjust the scale by running `N=8 API_CALLS=1000000 ./tools/fuzz_longrun.sh`.

#### Manual Build (Coverage Instrumentation)

1. Configure and compile the fuzz test project.

    ```bash
    cd /opt/KADD/SQLi
    mkdir -p build_fuzz && cd build_fuzz
    export ONNXRUNTIME_HOME=/opt/KADD/SQLi/util/onnxruntime-linux-aarch64-1.28.0
    cmake -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang \
          -DENABLE_CODE_COVERAGE=ON ..
    cmake --build .
    ```

2. Run the fuzz test and generate coverage data. The test duration is three hours. The fuzz build does not enable `AddressSanitizer` by default (prioritizing long-duration stability throughput), so `-detect_leaks=0` is not needed.

    ```bash
    cd /opt/KADD/SQLi/build_fuzz/tests
    ./kadd_fuzzer -runs=1000 -max_total_time=10800 > /dev/null 2>&1
    ```

3. Merge the coverage data.

    ```bash
    cd /opt/KADD/SQLi/build_fuzz/tests
    llvm-profdata merge -sparse default.profraw -o kadd.profdata
    ```

4. View the coverage report.

    ```bash
    llvm-cov report ./kadd_fuzzer -instr-profile=kadd.profdata \
        -ignore-filename-regex="tests/.*|include/.*|util/.*|cjson*"
    ```

5. Generate an HTML-format coverage report.

    ```bash
    llvm-cov show ./kadd_fuzzer -instr-profile=kadd.profdata \
        -format=html -o coverage_html \
        -ignore-filename-regex="tests/.*|include/.*|util/.*|cjson*"
    ```

### Quick Reference for Compilation Options

| Option | Default Value | Description | Use Case |
|------|--------|------|---------|
| `-DCMAKE_BUILD_TYPE=Release` | Debug | Release build (`-O3` optimization) | Production deployment |
| `-DKADD_OPTIMIZE=ON` | OFF | Additional optimization option | Performance test |
| `-DENABLE_ARM_SVE=ON` | ON | Arm SVE acceleration (enabled by default) | Arm platforms |
| `-DKADD_SVE_VECTOR_BITS=256` | 256 | Fixed SVE vector width (which can be set to `128`/`256`/`512`; empty string means adaptive to any width) | Arm SVE platforms/coverage analysis |
| `-DENABLE_CODE_COVERAGE=ON` | OFF | Code coverage | Coverage analysis |
| `-DBUILD_TESTS=ON` | ON | Test program build | Test build |
| `-DCMAKE_CXX_COMPILER=clang++` | auto | Compiler specification | Fuzz/coverage |
| `-DCMAKE_C_COMPILER=clang` | auto | Compiler specification | Fuzz/coverage |
