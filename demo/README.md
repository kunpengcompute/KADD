# KADD Demo 程序

本目录包含 KADD 项目的交互式演示程序。

## 编译方法

### 方法 1: 使用 CMake（推荐）

```bash
cd build
cmake .. -G "MinGW Makefiles"
cmake --build . --target kadd_dfa_demo
cmake --build . --target kadd_sqli_demo
```

### 方法 2: 手动编译

```bash
# 编译 tools.c
gcc -c -I. demo/tools.c -o /tmp/tools.o

# 编译 DFA demo
gcc -Iinclude -Ithird_party/include demo/kadd_dfa_demo.c /tmp/tools.o \
    build/src/libkadd_other_engines.a \
    third_party/lib/libonnxruntime.a \
    third_party/src/cJSON.c -lstdc++ \
    -o kadd_dfa_demo.exe

# 编译 SQLi demo
gcc -Iinclude -Ithird_party/include demo/kadd_sqli_demo.c /tmp/tools.o \
    build/src/libkadd_other_engines.a \
    third_party/lib/libonnxruntime.a \
    third_party/src/cJSON.c -lstdc++ \
    -o kadd_sqli_demo.exe
```

## 运行

Demo 程序需要在项目根目录下运行，因为它们依赖以下文件：
- `tools/profile_data/transition_table.bin`
- `tools/profile_data/states_meta.json`
- `config.json`
- `assets/model/model_tokens_rf.onnx`

```bash
# 在项目根目录运行
cd /path/to/KADD
./kadd_dfa_demo.exe
./kadd_sqli_demo.exe
```

## Demo 程序列表

| Demo 程序 | 描述 | 功能 |
|---------|------|------|
| kadd_dfa_demo | DFA 分词引擎演示 | 文本分词，token 向量化 |
| kadd_feature_demo | 特征提取演示 | 序列到特征向量转换 |
| kadd_ml_demo | ML 引擎演示 | ONNX 模型推理 |
| kadd_sqli_demo | SQL 注入检测演示 | 完整的 SQL 注入检测流程 |

## 使用示例

### DFA Demo
```
[1]> SELECT * FROM users
  ✓ 分词成功 (耗时: 12.34 μs)
  Token 向量 [长度=5]: [7, 11, 8, 7, 11]
```

### SQLi Demo
```
[1]> id=1' OR '1'='1
  ⚠ id=1' OR '1'='1                    | 概率:  98.46% | 耗时: 1234.56 μs
```
