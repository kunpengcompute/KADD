# KADD SQL 注入检测引擎

## 🚀 快速开始

### 前置要求

- **编译器**: Clang 12+ / GCC 11+
- **CMake**: 3.16+
- **依赖**: ONNX Runtime、cJSON、Google Test（可选）

### 依赖安装

> 详见 [第三方库安装指南](docs/DEPENDENCIES.md)

### 构建指南

#### 📦 基础构建（生产部署）

```bash
# 一键构建（下载依赖 + 编译）
chmod +x build.sh && ./build.sh

# 或手动构建
mkdir -p build && cd build
cmake ..
cmake --build .
```

#### 🧪 单元测试构建（GCC 编译）

```bash
mkdir -p build_ut && cd build_ut
cmake -DUSE_SYSTEM_LIBS=OFF -DENABLE_CODE_COVERAGE=ON ..
cmake --build .

# 运行测试
export LD_LIBRARY_PATH=$(pwd)/src:../third_party/onnxruntime/lib:/usr/local/lib64:/usr/local/lib:$LD_LIBRARY_PATH
ctest --verbose

# 生成 ut 覆盖率报告
make coverage
```

#### 🎯 Fuzz 测试构建（Clang + ASan）

```bash
mkdir -p build_fuzz && cd build_fuzz
cmake -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang \
      -DUSE_SYSTEM_LIBS=OFF -DENABLE_CODE_COVERAGE=ON ..
cmake --build .

# 运行 Fuzz 生成覆盖率数据（3小时，ONNX 内存泄漏故使用 -detect_leaks=0）
cd tests && export ASAN_OPTIONS=detect_leaks=0 && \
    ./kadd_fuzzer -detect_leaks=0 -runs=30000000 -max_total_time=10800 \
    assets/fuzz_corpus > /dev/null 2>&1

# 展示覆盖率报告
llvm-profdata merge -sparse default.profraw -o kadd.profdata
llvm-cov report ./kadd_fuzzer -instr-profile=kadd.profdata \
    -ignore-filename-regex="tests/.*|third_party/.*|include/*"

# 生成 fuzz 覆盖率报告
llvm-cov show ./kadd_fuzzer -instr-profile=kadd.profdata \
    -format=html -o coverage_html \
    -ignore-filename-regex="tests/.*|third_party/.*|include/*"
```

#### 🔧 编译选项速查表

| 选项 | 默认值 | 说明 | 使用场景 |
|------|--------|------|---------|
| `-DCMAKE_BUILD_TYPE=Release` | Debug | 发布构建（-O3 优化） | 生产部署 |
| `-DKADD_OPTIMIZE=ON` | OFF | 额外优化选项 | 性能测试 |
| `-DENABLE_ARM_SVE=ON` | ON | ARM SVE 加速（默认开启） | ARM 平台 |
| `-DENABLE_CODE_COVERAGE=ON` | OFF | 代码覆盖率 | 覆盖率分析 |
| `-DBUILD_TESTS=ON` | ON | 构建测试程序 | 测试构建 |
| `-DUSE_SYSTEM_LIBS=OFF` | ON | 使用第三方库 | 独立构建 |
| `-DCMAKE_CXX_COMPILER=clang++` | auto | 指定编译器 | Fuzz/覆盖 |
| `-DCMAKE_C_COMPILER=clang` | auto | 指定编译器 | Fuzz/覆盖 |

## 📦 模块说明

| 模块 | 说明 | 依赖 |
|------|------|------|
| DFA 引擎 | 字符串分词 | 无 |
| 特征引擎 | 序列转特征向量 | 无 |
| ML 引擎 | ONNX 模型推理 | ONNX Runtime |
| SQLi 引擎 | 完整检测 API | DFA + 特征 + ML + cJSON |

## 💡 快速示例

```c
#include "kadd_sqli_engine.h"

int main() {
    // 创建检测引擎
    kadd_sqli_engine_t *engine = NULL;
    if (kadd_sqli_engine_create_from_file(&engine, "config.json") != KADD_SQLI_OK) {
        return 1;
    }
    
    // 检测 SQL 注入
    const char *input = "id=1' OR '1'='1";
    float probability = 0.0f;
    
    if (kadd_sqli_detect(engine, input, &probability) == KADD_SQLI_OK) {
        if (probability >= 0.5f) {
            printf("SQL 注入! 概率: %.2f%%\n", probability * 100);
        } else {
            printf("安全输入\n");
        }
    }
    
    kadd_sqli_engine_free(engine);
    return 0;
}
```

## 📚 文档

- [第三方库安装指南](docs/DEPENDENCIES.md) - 依赖安装说明


## 📁 目录结构

```
KADD/
├── include/          # 头文件
├── src/              # 源代码
├── tests/            # 测试代码
├── docs/             # 文档
├── tools/            # 工具脚本（⚠️ 见下方说明）
├── assets/           # 模型和数据文件
└── third_party/      # 第三方依赖
```

### ⚠️ 关于 tools 目录

`tools/` 目录包含**模型和数据生成工具**，用于：

- 训练和生成检测模型（`tools/train_tokens.py`）
- 生成 DFA 状态转移表（`tools/dfa_compile/dfa_compiler.py`）

**重要声明：**

> 本目录代码**仅供学习和参考**，**不保证**：
> - 代码质量符合生产标准
> - 无安全漏洞
> - 与主项目代码规范一致
>
> 这些工具是**开发过程中的临时脚本**，可能未经过充分测试和审查。
> 如需用于生产环境，请自行评估安全性并进行必要的加固。

**示例：重新训练模型**

```bash
# 1. 准备训练数据
cp your_custom_dataset.csv assets/data/

# 2. 运行训练脚本（⚠️ 谨慎执行）
python tools/train_tokens.py \
    --input assets/data/your_custom_dataset.csv \
    --output assets/model/model_tokens_rf.onnx

# 3. 验证新模型
./build/kadd_sqli_demo
```

#### 安全建议

1. **默认不运行**：使用预生成的产物即可满足大部分需求
2. **隔离环境**：如需定制，在独立的沙箱/容器中运行
3. **审查代码**：运行前务必检查脚本内容
4. **验证产物**：对生成的模型和状态表进行充分测试

## 🏗️ 架构

```
输入字符串 → DFA 分词 → Token 序列 → 特征提取 → 特征向量 → ML 推理 → 概率
```

## 🌍 平台支持

| 平台 | 架构 | 状态 |
|------|------|------|
| Linux | x86_64 | ✅ 支持 |
| Linux | aarch64 | ✅ 支持 (NEON) |
| Linux | ARMv9-A | ✅ 支持 (SVE/SVE2) |

## 📜 第三方开源库

本项目使用以下开源库：

| 库名 | 许可证 | 仓库地址 |
|---------|---------|------------|
| [cJSON](https://github.com/DaveGamble/cJSON) | MIT | https://github.com/DaveGamble/cJSON |
| [ONNX Runtime](https://github.com/microsoft/onnxruntime) | MIT | https://github.com/microsoft/onnxruntime |
| [Google Test](https://github.com/google/googletest) | BSD 3-Clause | https://github.com/google/googletest |

完整许可证文本请参见 [THIRD_PARTY_LICENSES.txt](THIRD_PARTY_LICENSES.txt)

## 📄 许可证

源代码 **Private Repository Source Code License 1.0**  [LICENSE](LICENSE)。

文档 **CC-BY 4.0 (Creative Commons Attribution 4.0 International)** [LICENSE](./docs/LICENSE)。
