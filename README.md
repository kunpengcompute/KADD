# KADD SQL 注入检测引擎

## 🚀 快速开始

### 前置要求

- **编译器**: Clang 12+ / GCC 11+
- **CMake**: 3.16+
- **依赖**: ONNX Runtime、cJSON、Google Test（可选）

### 依赖安装

> 详见 [第三方库安装指南](docs/DEPENDENCIES.md)

### 构建与测试

```bash
# 一键构建（下载依赖 + 编译）
chmod +x build.sh && ./build.sh

# 或手动构建
mkdir -p build && cd build
cmake -DUSE_SYSTEM_LIBS=OFF -DKADD_OPTIMIZE=ON -DCMAKE_BUILD_TYPE=Release ..
cmake --build .

# 运行测试
export LD_LIBRARY_PATH=$(pwd)/src:../third_party/onnxruntime/lib:/usr/local/lib64:/usr/local/lib:$LD_LIBRARY_PATH
ctest --verbose

# 生成 UT 覆盖率报告（需安装 lcov）
cmake -DUSE_SYSTEM_LIBS=OFF -DKADD_OPTIMIZE=ON -DENABLE_CODE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
ctest --output-on-failure
make coverage
```

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

## 🔬 FUZZ 测试

KADD 项目集成了 LibFuzzer 进行自动化模糊测试（ARM平台优化）。

> **注意：** FUZZ 测试需要 Clang 编译器（LibFuzzer 不支持 GCC）

### 测试覆盖

- **SQLi 引擎**：各种注入模式、编码绕过、边界情况
- **DFA 引擎**：特殊字符、超长输入、空指针处理
- **Feature 引擎**：随机序列、负值序列、批量处理边界
- **ML 引擎**：异常特征向量、批处理大小边界、内存泄漏

### 覆盖率报告

LibFuzzer 测试支持生成代码覆盖率报告。

#### 生成覆盖率报告

```bash
# 1. 使用 Clang 编译（必需）
mkdir build_fuzz && cd build_fuzz
# 如果使用third party; 规避 -fPIC 报错
CC=clang CXX=clang++ cmake -DUSE_SYSTEM_LIBS=OFF -DKADD_OPTIMIZE=ON -DCMAKE_BUILD_TYPE=Release ..
cmake --build .

# 2. 进入测试目录运行 FUZZ（语料已由 cmake 复制到 assets/fuzz_corpus/）
cd tests && ./kadd_fuzzer assets/fuzz_corpus -runs=10000

# 3. 合并覆盖率数据
llvm-profdata merge -sparse default.profraw -o kadd.profdata

# 4. 查看覆盖率报告（仅 src/ 源码）
llvm-cov report ./kadd_fuzzer -instr-profile=kadd.profdata -ignore-filename-regex="tests/.*|third_party/.*|include/*"

# 5. 生成 HTML 覆盖率报告
llvm-cov show ./kadd_fuzzer -instr-profile=kadd.profdata  -format=html -o coverage_html -ignore-filename-regex="tests/.*|third_party/.*|include/*"
```

#### 覆盖率报告选项

| 选项 | 描述 | 默认值 |
|------|------|--------|
| `-runs` | 运行的测试用例数量 | 无限 |
| `-max_total_time` | 最大运行时间（秒） | 10 |
| `-max_len` | 最大输入长度 | 4096 |

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
- 生成 DFA 状态转移表

**重要声明：**

> 本目录代码**仅供学习和参考**，**不保证**：
> - 代码质量符合生产标准
> - 无安全漏洞
> - 与主项目代码规范一致
>
> 这些工具是**开发过程中的临时脚本**，可能未经过充分测试和审查。
> 如需用于生产环境，请自行评估安全性并进行必要的加固。

#### 运行机制说明

**正常使用场景：**

KADD 引擎运行时**仅使用** `tools/` 生成的二进制产物：

| 产物文件 | 说明 | 引擎使用位置 |
|---------|------|-------------|
| `assets/model/model_tokens_rf.onnx` | 训练好的 ML 模型 | ML 引擎加载推理 |
| `assets/data/dfa_states.json` | DFA 状态转移表 | DFA 引擎查表匹配 |

**主项目代码不会执行** `tools/` 中的任何 Python 脚本。

#### 何时需要使用 tools 工具

仅在以下情况才需要运行 `tools/` 中的脚本：

| 场景 | 需要运行的脚本 | 生成的产物 |
|------|--------------|-----------|
| 更新检测模型 | `tools/train_tokens.py` | 新的 `.onnx` 模型文件 |
| 修改分词规则 | `tools/generate_dfa.py`* | 新的 DFA 状态表 |
| 自定义训练数据 | `tools/train_tokens.py --dataset your_data.csv` | 定制化模型 |

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

## 🔧 编译选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `-DKADD_OPTIMIZE=ON` | OFF | 启用 -O3 优化 |
| `-DENABLE_ARM_SVE=ON` | OFF | 启用 ARM SVE（自动检测 SVE2） |
| `-DENABLE_CODE_COVERAGE=ON` | OFF | 启用代码覆盖率 |
| `-DBUILD_TESTS=ON` | ON | 构建测试程序 |
| `-DUSE_SYSTEM_LIBS=ON` | ON | 优先使用系统库 |

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
