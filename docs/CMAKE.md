# KADD CMake 构建指南

---

## 目录

- [1. 快速开始](#1-快速开始)
- [2. 构建选项](#2-构建选项)
- [3. 构建产物](#3-构建产物)
- [4. 代码覆盖率](#4-代码覆盖率)

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
| `ENABLE_ARM_SVE` | OFF | 启用 ARM SVE（自动检测 SVE2） |

### ARM 加速

```bash
# 启用 SVE（自动检测 SVE2 支持）
cmake -DENABLE_ARM_SVE=ON -DCMAKE_BUILD_TYPE=Release -DKADD_OPTIMIZE=ON ..
cmake --build .

# SVE2 支持的系统会自动启用 SVE2
# 非 SVE2 系统会回退到 SVE
```

**注意**：SVE/SVE2 仅在 ARM64 架构上可用。启用时会自动检测编译器是否支持 SVE2，如果支持则自动启用 SVE2，否则使用 SVE。

---

## 3. 构建产物

### 3.1 动态库

`src/` 目录下会生成以下动态库（`.so` / `.dll`）：

| 库名 | 说明 | 依赖 |
|------|------|------|
| `libkadd_dfa_engine` | DFA 分词引擎 | 无 |
| `libkadd_feature_engine` | 特征提取引擎 | 无 |
| `libkadd_ml_engine` | ML 推理引擎 | ONNX Runtime |
| `libkadd_sqli_engine` | SQL 注入检测引擎 | DFA + Feature + ML + cJSON |

**动态库版本管理**：
- 遵循 Semantic Versioning（主版本.次版本.修订版本）
- SOVERSION 为 1（主版本号）
- 示例：`libkadd_dfa_engine.so.1.0.0`

### 3.2 静态库

`src/` 目录下会生成综合静态库：

| 库名 | 说明 | 用途 |
|------|------|------|
| `libkadd_other_engines.a` | 所有引擎的静态链接版本 | Demo 程序链接 |

### 3.3 Demo 程序

`demo/` 目录生成的可执行文件：

| 程序 | 说明 |
|------|------|
| `kadd_dfa_demo` | DFA 分词演示 |
| `kadd_feature_demo` | 特征转换演示 |
| `kadd_ml_demo` | ML 推理演示 |
| `kadd_sqli_demo` | SQL 注入检测演示 |

### 3.4 测试程序

`tests/` 目录生成的测试程序：

| 程序 | 说明 |
|------|------|
| `dfa_engine_test` | DFA 引擎单元测试 |
| `feature_engine_test` | 特征引擎单元测试 |
| `ml_engine_test` | ML 引擎单元测试 |
| `sqli_engine_test` | SQLi 引擎单元测试 |

---

## 4. 代码覆盖率

```bash
cmake -DENABLE_CODE_COVERAGE=ON ..
make
ctest --verbose
make coverage
```

报告位置：`build/coverage_html/index.html`

---

*文档版本: 1.0.0*
