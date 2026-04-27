# KADD SQL Injection Detection Engine

基于 ONNX Runtime 的轻量级 SQL 注入检测引擎，支持 DFA 分词、特征转换和模型推理。

## 特性

- ✅ **ONNX Runtime 集成** - 支持标准 ONNX 模型推理
- ✅ **模块化 CMake** - 清晰的构建结构，易于维护
- ✅ **Google Test 测试** - 完整的单元测试和集成测试
- ✅ **ARM SVE 加速** - 支持 ARMv9-A SVE 向量化
- ✅ **JSON 配置** - 灵活的配置文件支持
- ✅ **跨平台** - Windows/Linux/macOS
- ✅ **系统库支持** - 使用标准包管理器安装依赖

## 目录结构

```
c/
├── CMakeLists.txt              # 根配置（项目选项、子目录）
├── cmake/                      # CMake 模块
│   ├── CompilerOptions.cmake   # 编译选项（警告、优化、ARM SVE）
│   ├── FindONNXRuntime.cmake   # ONNX Runtime 查找模块
│   ├── Libraries.cmake          # 库目标定义
│   └── Tests.cmake             # 测试配置（Google Test）
├── src/                        # 源代码
│   ├── kadd_feature_engine.c   # 特征转换（滑动窗口）
│   ├── kadd_dfa_engine.c       # DFA 分词引擎
│   ├── kadd_ml_engine.c        # ONNX 推理引擎
│   ├── kadd_sqli_engine.c      # SQL 注入检测引擎（整合）
│   └── py/                     # Python DFA 编译器
│       ├── dfa_compiler_2.py   # DFA 转移表编译器
│       ├── profile_data/       # 转移表数据
│       │   ├── transition_table.bin
│       │   └── states_meta.json
│       └── dfa_engine_copy.py  # Python DFA 引擎
├── include/                    # 公共头文件
│   ├── kadd_feature_engine.h
│   ├── kadd_dfa_engine.h       # DFA 引擎头文件
│   ├── kadd_ml_engine.h        # ML 推理引擎头文件
│   └── kadd_sqli_engine.h      # SQL 注入检测引擎头文件
├── demo/                       # 示例程序
│   ├── kadd_feature_demo.c
│   ├── kadd_ml_demo.c
│   └── kadd_sqli_demo.c
├── tests/                      # 测试
│   ├── test_seq_to_feature_gtest.cpp
│   ├── test_inference_engine_gtest.cpp
│   ├── test_dfa_engine_gtest.cpp
│   └── README.md               # 测试文档
└── config.json                 # 配置文件
```

## 快速开始

### 1. 安装 ONNX Runtime

详细安装指南请参阅 [INSTALL.md](INSTALL.md)

**快速安装**:

```bash
# Ubuntu/Debian
wget https://github.com/microsoft/onnxruntime/releases/download/v1.16.0/onnxruntime-linux-x64-1.16.0.tgz
tar -xzf onnxruntime-linux-x64-1.16.0.tgz
sudo cp -r onnxruntime-linux-x64-1.16.0/* /usr/local/
sudo ldconfig

# macOS
brew install onnxruntime

# Windows
vcpkg install onnxruntime:x64-windows

# 自定义路径
export ONNXRuntime_DIR=/path/to/onnxruntime
```

### 2. 编译

```bash
mkdir build && cd build

# 标准编译（启用 ONNX 和测试）
cmake .. -DUSE_ONNX=ON -DBUILD_TESTS=ON
cmake --build .

# Windows MinGW
cmake .. -G "MinGW Makefiles" -DUSE_ONNX=ON -DBUILD_TESTS=ON
mingw32-make
```

### 3. 运行

```bash
# 运行所有测试
ctest --verbose

# 或直接运行 demo
cd ../bin
./kadd_ml_demo
./kadd_feature_demo
./kadd_sqli_demo
```

## 编译选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `-DUSE_ONNX=ON` | OFF | 启用 ONNX Runtime 支持 |
| `-DENABLE_SVE=ON` | OFF | 启用 ARM SVE 加速 |
| `-DBUILD_TESTS=ON` | OFF | 构建测试程序 |
| `-DBUILD_DEMOS=ON` | ON | 构建 Demo 程序 |
| `-DENABLE_COVERAGE=ON` | OFF | 启用代码覆盖率 |

### 示例

```bash
# Debug + 覆盖率
cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON

# ARM SVE 加速
cmake .. -DENABLE_SVE=ON -DUSE_ONNX=ON

# 自定义 ONNX Runtime 路径
cmake .. -DONNXRuntime_DIR=/opt/onnxruntime

# 只构建库（不构建 demo）
cmake .. -DBUILD_DEMOS=OFF
```

## 配置文件

`config.json` 示例：

```json
{
  "model": {
    "path": "assets/model/model_tokens_rf.onnx",
    "feature_dim": 28
  },
  "inference": {
    "batch_size": 10,
    "warmup_iterations": 10
  }
}
```

## API 使用

### DFA 分词引擎

```c
#include "kadd_dfa_engine.h"

// 创建引擎（指定转移表目录）
kadd_dfa_engine_t *engine = kadd_dfa_engine_create("src/py");

// 分词
kadd_token_list_t *tokens = kadd_dfa_engine_tokenize(engine, "SELECT * FROM users WHERE id=1");

// 获取 token 数量和内容
size_t count = kadd_token_list_count(tokens);
for (size_t i = 0; i < count; i++) {
    kadd_token_t *tok = kadd_token_list_get(tokens, i);
    printf("Token(%c, '%s', pos=%zu)\n", tok->type, tok->value, tok->pos);
}

// 获取类型索引数组
size_t idx_count;
int *indices = kadd_dfa_engine_tokenize_to_indices(engine, input, &idx_count);

// 清理
kadd_token_list_destroy(tokens);
free(indices);
kadd_dfa_engine_destroy(engine);
```

### 特征转换

```c
#include "kadd_feature_engine.h"

int seq[] = {7, 11, 8, 12, 8, 11, 8};
int feature_dim = 28;
int *feature = calloc(feature_dim, sizeof(int));

kadd_seq_to_feature(seq, 7, 5, feature, feature_dim);

free(feature);
```

### ML 模型推理

```c
#include "kadd_ml_engine.h"

kadd_model_handle_t *model = NULL;
kadd_model_load(&model, "assets/model/model_tokens_rf.onnx", 28);

float *features = calloc(28, sizeof(float));
// ... 填充特征 ...

float probability = 0.0f;
kadd_model_predict(model, features, &probability);

kadd_model_free(model);
free(features);
```

### SQL 注入检测（整合引擎）

```c
#include "kadd_sqli_engine.h"

// 方式 1: 从配置文件创建引擎（推荐）
kadd_sqli_engine_t *engine = kadd_sqli_engine_create_from_file("config.json");

// 方式 2: 手动配置引擎
kadd_sqli_config_t config = {
    .table_dir = "src/py",
    .model_path = "assets/model/model_tokens_rf.onnx",
    .feature_dim = 28,
    .window_size = 5,
    .threshold = 0.5f
};
kadd_sqli_engine_t *engine = kadd_sqli_engine_create(&config);

// 检测 SQL 注入
kadd_sqli_result_t result;
const char *input = "id=1' OR '1'='1";
if (kadd_sqli_detect(engine, input, &result) == 0) {
    if (result.is_sqli) {
        printf("SQL 注入检测到! 概率: %.2f\n", result.probability);
    } else {
        printf("安全. 概率: %.2f\n", result.probability);
    }
}

// 释放引擎
kadd_sqli_engine_free(engine);
```

## 测试

详细测试文档请参阅 [tests/README.md](tests/README.md)

### 运行测试

```bash
# DFA 引擎测试
cmake --build . --target test_dfa

# 所有测试
cmake --build . --target test_all

# 核心测试（不依赖 ONNX）
cmake --build . --target test_units

# 集成测试（需要 ONNX）
cmake --build . --target test_integration

# 覆盖率报告
cmake .. -DENABLE_COVERAGE=ON
cmake --build . --target coverage
```

## 性能

- **特征转换**: ~100,000 ops/sec (ARM SVE)
- **单次推理**: ~111,000 calls/sec
- **批量推理**: ~166,000 samples/sec

## ARM 平台支持

### ARM SVE 加速

```bash
cmake .. -DENABLE_SVE=ON -DUSE_ONNX=ON
```

**要求**:
- ARMv9-A 或更高
- GCC 10+ / Clang 12+

### 兼容性

代码使用条件编译，完全兼容 ARM 平台：

```c
#if defined(__aarch64__) && defined(__ARM_FEATURE_SVE)
    sliding_window_distance_sve(...);  // SVE 加速
#else
    sliding_window_distance_shift_fast(...);  // 标量版本
#endif
```

### ARM64 ONNX Runtime 安装

详细说明请参阅 [INSTALL.md](INSTALL.md)

```bash
# Linux ARM64
wget https://github.com/microsoft/onnxruntime/releases/download/v1.16.0/onnxruntime-linux-aarch64-1.16.0.tgz
tar -xzf onnxruntime-linux-aarch64-1.16.0.tgz
sudo cp -r onnxruntime-linux-aarch64-1.16.0/* /usr/local/
sudo ldconfig
```

## 故障排除

### 问题：找不到 ONNX Runtime

```bash
# 检查系统库
ldconfig -p | grep onnxruntime

# 或设置自定义路径
export ONNXRuntime_DIR=/path/to/onnxruntime
cmake .. -DUSE_ONNX=ON
```

### 问题：测试失败

```bash
# 检查模型文件
ls assets/model/model_tokens_rf.onnx

# 查看详细日志
./tests/test_inference_engine --gtest_filter=*LoadModel*
```

### 问题：CMake 弃用警告

```bash
# 忽略第三方库警告
cmake .. -Wno-dev -DUSE_ONNX=ON
```

### 问题：Windows DLL 找不到

CMake 会自动复制 DLL 到输出目录。如果仍有问题：

```bash
# 手动添加 bin 目录到 PATH
export PATH=$PATH:/path/to/onnxruntime/bin
```

## 架构设计

详细的重构文档请参阅 [CMAKE_REFACTOR.md](CMAKE_REFACTOR.md)

### 模块职责

| 模块 | 职责 | 文件 |
|------|------|------|
| 根配置 | 项目选项、子目录协调 | CMakeLists.txt |
| 编译选项 | 警告、优化、SVE | cmake/CompilerOptions.cmake |
| ONNX 查找 | 标准系统库查找 | cmake/FindONNXRuntime.cmake |
| 库定义 | 静态库目标 | cmake/Libraries.cmake |
| 测试 | Google Test 集成 | cmake/Tests.cmake |

### 依赖管理

项目使用标准 CMake `find_package()` 查找依赖：

1. **系统路径** - `/usr/local`, `/usr`
2. **pkg-config** - Linux 标准查找工具
3. **环境变量** - `ONNXRuntime_DIR`
4. **vcpkg** - Windows 包管理器
5. **Homebrew** - macOS 包管理器

这种方式符合 CMake 最佳实践，便于集成到其他项目。

## 贡献

欢迎提交 Issue 和 Pull Request。

## 许可证

[MIT License](LICENSE)

## 参考资料

- [ONNX Runtime](https://github.com/microsoft/onnxruntime)
- [Google Test](https://google.github.io/googletest/)
- [ARM SVE](https://developer.arm.com/documentation/ddi0602/latest/)
- [CMake find_package](https://cmake.org/cmake/help/latest/command/find_package.html)

## ralph-loop

/ralph-wiggum:ralph-loop "优化kadd_dfa_engine.c的内容 使测试dfa_accuracy_test.c准确率到达100%; 代码参考dfa_engine_copy.py ; 对比文件来自代码的result目录，注意该文件很大；所有辅助函数 放到tools/scripts目录下，别搞乱我的代码目录<promise>dfa_accuracy=100%</promise>" --completion-promise 'DONE'
