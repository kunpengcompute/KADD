# KADD CMake 构建指南

---

## 目录

- [1. 快速开始](#1-快速开始)
- [2. 构建选项](#2-构建选项)
- [3. 依赖管理](#3-依赖管理)
- [4. 构建目标](#4-构建目标)
- [5. 代码覆盖率](#5-代码覆盖率)

---

## 1. 快速开始

### 1.1 标准构建

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### 1.2 运行测试

```bash
cd build
ctest --verbose
```

---

## 2. 构建选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `BUILD_TESTS` | ON | 构建测试程序 |
| `USE_SYSTEM_LIBS` | ON | 优先使用系统库 |
| `ENABLE_CODE_COVERAGE` | OFF | 启用代码覆盖率 |
| `KADD_OPTIMIZE` | OFF | 启用 -O3 优化 |
| `ENABLE_ARM_SVE` | OFF | 启用 ARM SVE |
| `ENABLE_ARM_SVE2` | OFF | 启用 ARM SVE2 |

### ARM 加速

```bash
# SVE (ARMv9-A)
cmake -DENABLE_ARM_SVE=ON ..

# SVE2 (ARMv9.2-A+)
cmake -DENABLE_ARM_SVE2=ON ..
```

---

## 3. 依赖管理

### 3.1 ONNX Runtime

自动检测路径（优先级）：
1. 环境变量 `ONNXRUNTIME_HOME`
2. `third_party/` 目录
3. 系统路径

```bash
# 手动指定
export ONNXRUNTIME_HOME=/path/to/onnxruntime
cmake ..

# 系统安装
sudo apt-get install libonnxruntime-dev
```

### 3.2 cJSON

自动检测系统库，回退到 `third_party/`。

```bash
sudo apt-get install libcjson-dev
```

### 3.3 Google Test

测试时需要，可选。

```bash
sudo apt-get install libgtest-dev
```

---

## 4. 构建目标

### 演示程序

| 目标 | 说明 |
|------|------|
| `kadd_dfa_demo` | DFA 分词演示 |
| `kadd_feature_demo` | 特征转换演示 |
| `kadd_ml_demo` | ML 推理演示 |
| `kadd_sqli_demo` | SQL 注入检测演示 |

### 测试程序

| 目标 | 说明 |
|------|------|
| `dfa_engine_test` | DFA 引擎测试 |
| `feature_engine_test` | 特征引擎测试 |
| `ml_engine_test` | ML 引擎测试 |
| `sqli_engine_test` | SQLi 引擎测试 |

---

## 5. 代码覆盖率

```bash
cmake -DENABLE_CODE_COVERAGE=ON ..
make
ctest --verbose
make coverage
```

报告位置：`build/coverage_html/index.html`

---

*文档版本: 1.0.0*
