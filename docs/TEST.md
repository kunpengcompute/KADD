# 测试文档

本文档介绍项目的测试框架、测试用例和运行方法。

## 目录

- [测试框架](#测试框架)
- [运行测试](#运行测试)
- [测试用例说明](#测试用例说明)
- [准确率测试](#准确率测试)
- [性能测试](#性能测试)

## 测试框架

项目使用 **Google Test** 框架进行单元测试和集成测试。

### 测试文件

| 测试文件 | 测试内容 | 依赖 |
|---------|---------|------|
| `test_ml_engine_gtest.cpp` | ML 引擎测试 | ONNX Runtime |
| `test_feature_engine_gtest.cpp` | 特征引擎测试 | 无 |
| `test_dfa_engine_gtest.cpp` | DFA 引擎测试 | 无 |


## 运行测试

### 编译测试

```bash
# 编译所有测试
cmake --build build --target ml_engine_test
cmake --build build --target feature_engine_test
cmake --build build --target dfa_engine_test
```

### 运行所有测试

```bash
cd build
./ml_engine_test.exe
./feature_engine_test.exe
./dfa_engine_test.exe
```

### 运行特定测试

```bash
# 只运行准确率测试
./ml_engine_test.exe --gtest_filter="*Accuracy*"

# 只运行性能测试
./ml_engine_test.exe --gtest_filter="*Performance*"

# 只运行单个测试
./ml_engine_test.exe --gtest_filter="MLEngineTest.LoadValidModel"
```

### 列出所有测试

```bash
./ml_engine_test.exe --gtest_list_tests
```

## 测试用例说明

### ML 引擎测试 (MLEngineTest)

#### 功能测试

| 测试用例 | 说明 |
|---------|------|
| `LoadValidModel` | 加载有效模型 |
| `PredictSingleSample` | 单样本推理 |
| `PredictWithVariousFeatures` | 各种特征向量推理 |
| `PredictBatchSize1` | 批次大小 1 |
| `PredictBatchSize10` | 批次大小 10 |
| `PredictBatchWithAutoBatchSize` | 自动批次大小 |
| `PredictBatchConsistencyWithSingle` | 单样本与批量一致性 |
| `ModelReuse` | 模型复用 |

#### 边界测试

| 测试用例 | 说明 |
|---------|------|
| `LoadWithNullHandle` | NULL 句柄加载 |
| `LoadWithInvalidPath` | 无效路径加载 |
| `LoadWithEmptyPath` | 空路径加载 |
| `PredictWithNullHandle` | NULL 句柄推理 |
| `PredictWithNullFeatures` | NULL 特征推理 |
| `PredictWithNullProbability` | NULL 概率指针推理 |
| `PredictBeforeLoad` | 加载前推理 |
| `PredictBatchWithZeroCount` | 零样本批量 |
| `PredictBatchWithNegativeCount` | 负样本批量 |

#### 性能测试

| 测试用例 | 说明 |
|---------|------|
| `Performance_SinglePrediction` | 单样本推理性能 |
| `Performance_BatchPrediction` | 批量推理性能 |
| `Performance_Scalability` | 批次大小扩展性 |
| `Performance_ModelLoading` | 模型加载性能 |
| `Performance_MemoryStress` | 大批量内存压力 |

#### 一致性测试

| 测试用例 | 说明 |
|---------|------|
| `Consistency_SameInputSameOutput` | 相同输入产生相同输出 |
| `Consistency_DeterministicBehavior` | 确定性行为验证 |

#### 准确率测试

| 测试用例 | 说明 |
|---------|------|
| `AccuracyOnParsedResultsDataset` | 数据集准确率（单样本） |
| `BatchAccuracyOnParsedResultsDataset` | 数据集准确率（批量） |

## 准确率测试

### 测试数据

准确率测试使用 `assets/data/parsed_results_fixed.json`，包含 253,493 个标注样本。

### 去重策略

测试使用**平衡去重策略**：
- 正常样本：保留重复（正常流量本身就有重复性）
- 攻击样本：去重（攻击有很多变体）

去重后约 1,323 个唯一样本。

### 预期结果

使用当前模型（平衡去重训练）：

| 指标 | 预期值 |
|------|--------|
| 准确率 | ~99.77% |
| 精确率 | 100% |
| 召回率 | 99.77% |
| F1 分数 | 99.88% |

### 混淆矩阵示例

```
真阳性: 1296  |  假阳性: 0
真阴性: 24    |  假阴性: 3
```

- **真阴性**: 正确识别的正常样本（24 个）
- **真阳性**: 正确识别的攻击样本（1296 个）
- **假阳性**: 误判为攻击的正常样本（0 个）
- **假阴性**: 误判为正常的攻击样本（3 个）

### 运行准确率测试

```bash
# 单样本准确率
./ml_engine_test.exe --gtest_filter="MLEngineTest.AccuracyOnParsedResultsDataset"

# 批量准确率
./ml_engine_test.exe --gtest_filter="MLEngineTest.BatchAccuracyOnParsedResultsDataset"

# 所有准确率测试
./ml_engine_test.exe --gtest_filter="*Accuracy*"
```

## 性能测试

### 性能指标

单样本推理性能（参考值）：
- 平均耗时: ~5-10 μs
- 吞吐量: ~100,000 ops/sec

批量推理性能（batch=64）：
- 平均耗时: ~10-20 μs/样本
- 吞吐量: ~5,000,000 samples/sec

### 运行性能测试

```bash
# 所有性能测试
./ml_engine_test.exe --gtest_filter="*Performance*"

# 单样本性能
./ml_engine_test.exe --gtest_filter="*Performance_Single*"

# 批量性能
./ml_engine_test.exe --gtest_filter="*Performance_Batch*"
```

## 测试输出

### 标准输出

```
========================================
  ML 引擎测试
========================================
模型路径: assets/model/model_tokens_rf.onnx
特征维度: 28
========================================

[==========] Running 33 tests from 1 test suite.
[----------] Global test environment set-up.
[----------] 33 tests from MLEngineTest
[ RUN      ] MLEngineTest.LoadValidModel
[       OK ] MLEngineTest.LoadValidModel (33 ms)
...
[----------] 33 tests from MLEngineTest (6200 ms total)

[  PASSED  ] 33 tests.
```

### 准确率测试输出

```
[准确率测试] 加载了 1323 个测试样本

========================================
  数据集准确率测试结果
========================================
总样本数: 1323
正确预测: 1320
错误预测: 3
----------------------------------------
准确率: 99.77%
精确率: 100.00%
召回率: 99.77%
F1 分数: 99.88
----------------------------------------
混淆矩阵:
  真阳性: 1296
  真阴性: 24
  假阳性: 0
  假阴性: 3
========================================
```

## Windows 特别说明

### 编码问题

PowerShell 可能对中文输出有编码问题。测试代码使用 `std::cerr` 确保输出立即显示。

### DLL 依赖

确保以下 DLL 文件在构建目录或系统 PATH 中：
- `onnxruntime.dll`
- `onnxruntime_providers_shared.dll`

```bash
# 复制 DLL 到构建目录
copy third_party\lib\*.dll build\
```

## 调试测试

### 详细输出

```bash
# 启用详细输出
./ml_engine_test.exe --gtest_print_time=1

# 重复运行（用于调试不稳定的测试）
./ml_engine_test.exe --gtest_repeat=3
```

### 只运行失败的测试

```bash
# 第一次运行
./ml_engine_test.exe

# 只重运行失败的测试
./ml_engine_test.exe --gtest_filter="*:FAILED"
```

## 添加新测试

### 测试命名规范

```cpp
// 功能测试: <模块>_<功能>
TEST_F(MLEngineTest, PredictSingleSample) { }

// 边界测试: <模块>With<条件>
TEST_F(MLEngineTest, PredictWithNullHandle) { }

// 性能测试: Performance_<操作>
TEST_F(MLEngineTest, Performance_SinglePrediction) { }

// 准确率测试: <数据集>Accuracy
TEST_F(MLEngineTest, AccuracyOnParsedResultsDataset) { }
```

### 测试模板

```cpp
TEST_F(MLEngineTest, YourTestCase) {
    if (skip_test) GTEST_SKIP() << "模型文件不可用";

    // Arrange: 准备测试数据
    ASSERT_EQ(CreateValidModel(), 0) << "模型加载失败";

    // Act: 执行被测试的操作
    float probability = 0.0f;
    int ret = kadd_model_predict(model, test_features.data(), &probability);

    // Assert: 验证结果
    EXPECT_EQ(ret, 0) << "推理应该成功";
    EXPECT_GE(probability, 0.0f) << "概率应该 >= 0";
    EXPECT_LE(probability, 1.0f) << "概率应该 <= 1";
}
```

## 持续集成

### GitHub Actions

测试在 CI 中自动运行：

```yaml
- name: Build and Test
  run: |
    cmake -Bbuild -G "MinGW Makefiles" -DUSE_ONNX=ON
    cmake --build build --target ml_engine_test
    ./build/ml_engine_test.exe
```

### 测试覆盖率

启用覆盖率测试：

```bash
cmake .. -DENABLE_COVERAGE=ON
cmake --build . --target coverage
```

## 故障排除

### 测试被跳过

如果看到：
```
[WARNING] 模型文件不存在: assets/model/model_tokens_rf.onnx
[WARNING] 跳过 ML 引擎测试
```

**解决方案**: 确保模型文件在正确位置：
```bash
ls assets/model/model_tokens_rf.onnx
```

### 测试超时

如果测试超时，可能是：
1. 模型文件损坏
2. ONNX Runtime DLL 版本不匹配
3. 系统资源不足

**解决方案**: 
```bash
# 重新下载模型和 ONNX Runtime
# 检查 DLL 版本
ls -l third_party/lib/onnxruntime.dll
```

### DLL 找不到

如果运行时报错找不到 DLL：

```bash
# 检查 DLL 是否在构建目录
ls build/onnxruntime.dll

# 或复制到构建目录
copy third_party\lib\*.dll build\
```
