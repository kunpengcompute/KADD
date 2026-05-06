# libinjection Demo 说明

libinjection 是一个轻量级的 SQL 注入检测库，基于模式匹配和指纹识别。

## 编译方式

### 方式 1: 使用 CMake（推荐）

如果 `third_party/libinjection-main/` 目录存在，CMake 会自动编译 libinjection demo：

```bash
cmake -B build
cmake --build build --target kadd_libinjection_demo

# 运行
./build/demo/kadd_libinjection_demo
```

### 方式 2: 使用编译脚本

Linux/macOS:
```bash
cd demo
chmod +x build_libinjection_demo.sh
./build_libinjection_demo.sh
./kadd_libinjection_demo
```

Windows:
```cmd
cd demo
build_libinjection_demo.bat
kadd_libinjection_demo.exe
```

### 方式 2: 手动编译

Linux/macOS:
```bash
cd demo
gcc -O2 -o kadd_libinjection_demo \
    kadd_libinjection_demo.c \
    tools.c \
    ../third_party/libinjection-main/src/libinjection_sqli.c \
    -I ../third_party/libinjection-main/src
```

Windows:
```cmd
cd demo
gcc -O2 -o kadd_libinjection_demo.exe ^
    kadd_libinjection_demo.c ^
    tools.c ^
    ..\third_party\libinjection-main\src\libinjection_sqli.c ^
    -I ..\third_party\libinjection-main\src
```

## 功能说明

该 demo 显示 libinjection 检测 SQL 注入的详细耗时分解：

1. **init+tokenize**: 初始化状态并进行词法分析
2. **fold**: 将 token 序列折叠成指纹
3. **lookup**: 查询指纹是否匹配黑名单

## 性能对比

运行 demo 后可以对比 KADD DFA 引擎和 libinjection 的性能：

```bash
# 运行 libinjection demo
./kadd_libinjection_demo

# 运行 KADD DFA demo
./build/demo/kadd_dfa_demo

# 输入相同的测试样本，对比耗时和检测结果
```

## 预期性能差异

| 引擎 | 预期耗时 (μs) | 特点 |
|------|----------------|------|
| libinjection | 5-20 | 轻量级，基于模式匹配 |
| KADD DFA | 20-100 | 基于 DFA 状态机，更复杂但更准确 |

实际性能取决于硬件平台和输入复杂度。
