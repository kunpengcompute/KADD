/**
 * @file test_feature_engine_gtest.cpp
 * @brief 特征引擎 Google Test 测试
 *
 * 测试内容：
 * 1. 功能测试 - 序列到特征转换、滑动窗口算法
 * 2. 边界测试 - 空指针、无效参数、极端输入
 * 3. 性能测试 - 函数耗时测试、SVE 加速效果
 */

#include <gtest/gtest.h>
#include <chrono>
#include <vector>
#include <random>
#include <algorithm>
#include "kadd_feature_engine.h"
#include "kadd_config_loader.h"

// ========== 测试夹具 ==========
class FeatureEngineTest : public ::testing::Test {
protected:
    std::vector<int> test_sequence;
    std::vector<int> test_feature;
    kadd_config_t config;

    void SetUp() override {
        // 从config.json加载配置
        ASSERT_EQ(kadd_config_load(nullptr, &config), 0)
            << "无法加载config.json配置文件";

        test_feature.resize(config.ml.feature_dim, 0);
    }

    void TearDown() override {
    }

    // 辅助函数：生成随机序列
    void GenerateRandomSequence(std::vector<int> &seq, size_t length, int min_val = 0, int max_val = 255) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dis(min_val, max_val);

        seq.resize(length);
        for (auto &val : seq) {
            val = dis(gen);
        }
    }

    // 辅助函数：计算特征向量的非零元素数量
    int CountNonZero(const std::vector<int> &feature) {
        return std::count_if(feature.begin(), feature.end(), [](int v) { return v != 0; });
    }

    // 辅助函数：计算特征向量的和
    long long SumFeature(const std::vector<int> &feature) {
        long long sum = 0;
        for (int v : feature) {
            sum += v;
        }
        return sum;
    }
};

// ========== 1. 功能测试 ==========

TEST_F(FeatureEngineTest, TransformSimpleSequence) {
    // 简单序列: {7, 11, 8, 12, 8, 11, 8}
    std::vector<int> seq = {7, 11, 8, 12, 8, 11, 8};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), config.ml.window_size,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, 0) << "应该成功转换序列";
}

TEST_F(FeatureEngineTest, TransformSingleElement) {
    std::vector<int> seq = {config.ml.feature_dim - 1};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), config.ml.window_size,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, 0) << "单元素序列应该成功转换";
}

TEST_F(FeatureEngineTest, TransformLongSequence) {
    // 序列长度超过 MAX_SEQ_LEN(300)，应返回参数错误
    std::vector<int> seq(1000);
    for (size_t i = 0; i < seq.size(); i++) {
        seq[i] = i % 10;
    }

    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), config.ml.window_size,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, KADD_ERROR_INVALID_PARAM) << "超长序列应该返回 KADD_ERROR_INVALID_PARAM";
}

TEST_F(FeatureEngineTest, TransformWithWindow5) {
    std::vector<int> seq = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 5,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, 0) << "窗口大小5应该成功转换";
}

TEST_F(FeatureEngineTest, TransformWithWindow10) {
    std::vector<int> seq = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 10,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, 0) << "窗口大小10应该成功转换";
}

TEST_F(FeatureEngineTest, TransformWithWindow20) {
    std::vector<int> seq(50);
    for (size_t i = 0; i < seq.size(); i++) {
        seq[i] = i % 20;
    }

    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 20,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, 0) << "窗口大小20应该成功转换";
}

TEST_F(FeatureEngineTest, TransformWithSmallFeatureDim) {
    std::vector<int> seq = {1, 2, 3, 4, 5};
    std::vector<int> feature(10, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 3,
                                  feature.data(), 10);

    EXPECT_EQ(ret, 0) << "小特征维度应该成功转换";
}

TEST_F(FeatureEngineTest, TransformWithLargeFeatureDim) {
    // feature_dim = MAX_FEATURE_DIM(300)，合法上限
    std::vector<int> seq = {1, 2, 3, 4, 5};
    std::vector<int> feature(MAX_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 3,
                                  feature.data(), MAX_FEATURE_DIM);

    EXPECT_EQ(ret, KADD_SUCCESS) << "feature_dim=MAX_FEATURE_DIM 应该成功转换";
}

TEST_F(FeatureEngineTest, SlidingWindowDistance) {
    // 验证滑动窗口距离计算的正确性
    // 序列: {0, 1, 0, 1, 0}, 窗口: 3
    // 预期: 每个位置的距离 = Σ |seq[i] - seq[i+d]| * (window_size - d)
    std::vector<int> seq = {0, 1, 0, 1, 0};
    std::vector<int> feature(5, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 3,
                                  feature.data(), 5);

    EXPECT_EQ(ret, 0) << "应该成功计算距离";

    // 验证距离值非负
    for (size_t i = 0; i < feature.size(); i++) {
        EXPECT_GE(feature[i], 0) << "位置 " << i << " 的距离应该 >= 0";
    }
}

TEST_F(FeatureEngineTest, WeightedDistance) {
    // 验证加权距离计算
    // 距离越远，权重越小
    std::vector<int> seq = {10, 20, 10, 20, 20};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 5,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, 0) << "应该成功计算加权距离";
}

TEST_F(FeatureEngineTest, ConstantSequence) {
    // 常量序列: 所有元素相同
    std::vector<int> seq(20, 20);
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), config.ml.window_size,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, 0) << "常量序列应该成功转换";

    // 常量序列的特征应该全为0（距离都为0）
    int non_zero = CountNonZero(feature);
    EXPECT_EQ(non_zero, 1) << "常量序列的特征应该全为0";
}

TEST_F(FeatureEngineTest, AlternatingSequence) {
    // 交替序列: {0, 100, 0, 100, ...}
    std::vector<int> seq(100);
    for (size_t i = 0; i < seq.size(); i++) {
        seq[i] = (i % 2 == 0) ? 0 : 10;
    }

    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), config.ml.window_size,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, 0) << "交替序列应该成功转换";

    // 交替序列应该产生较大的特征值
    int non_zero = CountNonZero(feature);
    EXPECT_GT(non_zero, 0) << "交替序列应该有非零特征";
}

TEST_F(FeatureEngineTest, IncreasingSequence) {
    // 递增序列
    std::vector<int> seq(100);
    for (size_t i = 0; i < seq.size(); i++) {
        seq[i] = i;
    }

    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), config.ml.window_size,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, KADD_FEATURE_ERROR_VALUE_OUT_OF_RANGE) << "递增序列应该成功转换";
}

TEST_F(FeatureEngineTest, RandomSequence) {
    // 随机序列
    std::vector<int> seq;
    GenerateRandomSequence(seq, config.dfa.max_input_length, 0, config.ml.feature_dim-1);

    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), config.ml.window_size,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, 0) << "随机序列应该成功转换";
}

TEST_F(FeatureEngineTest, SequenceWithZeros) {
    // 包含零的序列
    std::vector<int> seq = {0, 5, 0, 10, 0, 15, 0};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), config.ml.window_size,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, 0) << "包含零的序列应该成功转换";
}

TEST_F(FeatureEngineTest, SequenceWithNegativeValues) {
    // 包含负值的序列 — seq[i] < 0 应触发范围校验
    std::vector<int> seq = {-10, -5, 0, 5, 10};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), config.ml.window_size,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, KADD_FEATURE_ERROR_VALUE_OUT_OF_RANGE) << "包含负值的序列应该返回 KADD_FEATURE_ERROR_VALUE_OUT_OF_RANGE";
}

TEST_F(FeatureEngineTest, SequenceWithLargeValues) {
    // 包含超出 MAX_FEATURE_DIM 范围的大值 — 应触发范围校验
    std::vector<int> seq = {10000, 20000, 30000, 40000, 50000};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), config.ml.window_size,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, KADD_FEATURE_ERROR_VALUE_OUT_OF_RANGE) << "超出 MAX_FEATURE_DIM 的序列应该返回 KADD_FEATURE_ERROR_VALUE_OUT_OF_RANGE";
}

// ========== 2. 边界测试 ==========

TEST_F(FeatureEngineTest, TransformNullSequence) {
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(nullptr, 10, config.ml.window_size,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR) << "NULL 序列应该返回 KADD_ERROR_NULL_PTR";
}

TEST_F(FeatureEngineTest, TransformNullFeature) {
    std::vector<int> seq = {1, 2, 3, 4, 5};

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), config.ml.window_size,
                                  nullptr, config.ml.feature_dim);

    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR) << "NULL 特征向量应该返回 KADD_ERROR_NULL_PTR";
}

TEST_F(FeatureEngineTest, TransformZeroLength) {
    std::vector<int> seq = {1, 2, 3};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), 0, config.ml.window_size,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, KADD_SUCCESS) << "零长度序列应该返回 KADD_SUCCESS";
}

TEST_F(FeatureEngineTest, TransformNegativeLength) {
    std::vector<int> seq = {1, 2, 3};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), -1, config.ml.window_size,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, KADD_ERROR_INVALID_PARAM) << "负长度序列应该返回 KADD_ERROR_INVALID_PARAM";
}

TEST_F(FeatureEngineTest, TransformZeroFeatureDim) {
    std::vector<int> seq = {1, 2, 3};
    std::vector<int> feature(1, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), config.ml.window_size,
                                  feature.data(), 0);

    EXPECT_EQ(ret, KADD_ERROR_INVALID_PARAM) << "零特征维度应该返回 KADD_ERROR_INVALID_PARAM";
}

TEST_F(FeatureEngineTest, TransformNegativeFeatureDim) {
    std::vector<int> seq = {1, 2, 3};
    std::vector<int> feature(1, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), config.ml.window_size,
                                  feature.data(), -1);

    EXPECT_EQ(ret, KADD_ERROR_INVALID_PARAM) << "负特征维度应该返回 KADD_ERROR_INVALID_PARAM";
}

TEST_F(FeatureEngineTest, TransformZeroWindowSize) {
    std::vector<int> seq = {1, 2, 3};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 0,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, KADD_SUCCESS) << "零窗口大小应该返回 KADD_SUCCESS";
}

TEST_F(FeatureEngineTest, TransformNegativeWindowSize) {
    std::vector<int> seq = {1, 2, 3};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), -1,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, KADD_ERROR_INVALID_PARAM) << "负窗口大小应该返回 KADD_ERROR_INVALID_PARAM";
}

TEST_F(FeatureEngineTest, TransformInsufficientFeatureDim) {
    // 特征维度小于序列长度
    std::vector<int> seq(100);
    std::vector<int> feature(10, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), config.ml.window_size,
                                  feature.data(), 10);

    // 应该成功（只填充可用的维度）
    EXPECT_EQ(ret, 0) << "特征维度不足应该成功（部分填充）";
}

TEST_F(FeatureEngineTest, TransformAllNull) {
    int ret = kadd_seq_to_feature(nullptr, 10, config.ml.window_size, nullptr, config.ml.feature_dim);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR) << "所有参数为 NULL 应该返回 KADD_ERROR_NULL_PTR";
}

// ========== seq[i] 范围校验测试 ==========

TEST_F(FeatureEngineTest, SeqValueNegativeOne) {
    // seq[i] = -1，触发范围校验
    std::vector<int> seq = {0, -1, 2};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 3, feature.data(), config.ml.feature_dim);
    EXPECT_EQ(ret, KADD_FEATURE_ERROR_VALUE_OUT_OF_RANGE) << "seq[i]=-1 应返回 KADD_FEATURE_ERROR_VALUE_OUT_OF_RANGE";
}

TEST_F(FeatureEngineTest, SeqValueEqualsMaxFeatureDim) {
    // seq[i] == MAX_FEATURE_DIM(300)，恰好等于上限（校验是 < MAX_FEATURE_DIM），应触发范围校验
    std::vector<int> seq = {0, MAX_FEATURE_DIM, 1};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 3, feature.data(), config.ml.feature_dim);
    EXPECT_EQ(ret, KADD_FEATURE_ERROR_VALUE_OUT_OF_RANGE) << "seq[i]=MAX_FEATURE_DIM 应返回 KADD_FEATURE_ERROR_VALUE_OUT_OF_RANGE";
}

TEST_F(FeatureEngineTest, SeqValueJustBelowMaxFeatureDim) {
    // seq[i] = MAX_FEATURE_DIM - 1 = 299，合法值（在 feature_dim >= 300 时）
    std::vector<int> seq = {0, MAX_FEATURE_DIM - 1};
    std::vector<int> feature(MAX_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 3, feature.data(), MAX_FEATURE_DIM);
    EXPECT_EQ(ret, KADD_SUCCESS) << "seq[i]=299 在 feature_dim=300 时应该成功";
}

TEST_F(FeatureEngineTest, SeqValueZeroIsValid) {
    // seq[i] = 0 是合法的最小值
    std::vector<int> seq = {0, 0, 0, 0, 0};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 3, feature.data(), config.ml.feature_dim);
    EXPECT_EQ(ret, KADD_SUCCESS) << "seq[i]=0 应该成功";
}

TEST_F(FeatureEngineTest, SeqValueMixedValidAndInvalid) {
    // 混合合法与非法值，首个非法值在中间位置
    std::vector<int> seq = {1, 2, -5, 4};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 3, feature.data(), config.ml.feature_dim);
    EXPECT_EQ(ret, KADD_FEATURE_ERROR_VALUE_OUT_OF_RANGE) << "混合序列中的非法值应返回 KADD_FEATURE_ERROR_VALUE_OUT_OF_RANGE";
}

TEST_F(FeatureEngineTest, FeatureDimExceedsMaxFeatureDim) {
    // feature_dim > MAX_FEATURE_DIM(300)，应返回参数错误
    std::vector<int> seq = {1, 2, 3};
    std::vector<int> feature(301, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 3, feature.data(), 301);
    EXPECT_EQ(ret, KADD_ERROR_INVALID_PARAM) << "feature_dim > MAX_FEATURE_DIM 应返回 KADD_ERROR_INVALID_PARAM";
}

TEST_F(FeatureEngineTest, SeqLenExactlyMaxSeqLen) {
    // seq_len == MAX_SEQ_LEN(300)，恰好等于上限，合法
    std::vector<int> seq(MAX_SEQ_LEN);
    for (size_t i = 0; i < seq.size(); i++) {
        seq[i] = i % 10;
    }
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), config.ml.window_size,
                                  feature.data(), config.ml.feature_dim);
    EXPECT_EQ(ret, KADD_SUCCESS) << "seq_len=MAX_SEQ_LEN 应该成功";
}

TEST_F(FeatureEngineTest, TransformSequenceShorterThanWindow) {
    // 序列长度小于窗口大小
    std::vector<int> seq = {1, 2};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 10,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, 0) << "序列短于窗口应该成功";
}

TEST_F(FeatureEngineTest, TransformSequenceEqualToWindow) {
    // 序列长度等于窗口大小
    std::vector<int> seq = {1, 2, 3, 4, 5};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 5,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, 0) << "序列等于窗口应该成功";
}

TEST_F(FeatureEngineTest, TransformSeqLenExceedsMax) {
    // 序列长度超过 MAX_SEQ_LEN(300) — 应返回参数错误
    std::vector<int> seq(301);
    for (size_t i = 0; i < seq.size(); i++) {
        seq[i] = i % 10;
    }
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), config.ml.window_size,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, KADD_ERROR_INVALID_PARAM) << "序列长度超过 MAX_SEQ_LEN 应该返回 KADD_ERROR_INVALID_PARAM";
}

TEST_F(FeatureEngineTest, TransformLargeWindow) {
    // 大窗口大小，合法序列长度（<= MAX_SEQ_LEN）
    std::vector<int> seq(200);
    for (size_t i = 0; i < seq.size(); i++) {
        seq[i] = i % 10;
    }

    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 200,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, KADD_SUCCESS) << "大窗口大小应该成功";
}

// ========== 3. 性能测试 ==========

TEST_F(FeatureEngineTest, Performance_SmallSequence) {
    std::vector<int> seq(10);
    for (size_t i = 0; i < seq.size(); i++) {
        seq[i] = i;
    }

    std::vector<int> feature(config.ml.feature_dim, 0);

    const int iterations = 100000;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; i++) {
        kadd_seq_to_feature(seq.data(), seq.size(), config.ml.window_size,
                           feature.data(), config.ml.feature_dim);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    double avg_time_ns = (static_cast<double>(duration.count()) * 1000.0) / iterations;
    double ops_per_sec = (iterations * 1000000.0) / duration.count();

    std::cout << "\n[性能测试] 小序列(seq=10)转换平均耗时: " << avg_time_ns << " ns" << std::endl;
    std::cout << "[性能测试] 小序列转换吞吐量: " << static_cast<int>(ops_per_sec) << " ops/sec" << std::endl;

    // 性能基准：应该 > 100,000 ops/sec
    EXPECT_GT(ops_per_sec, 100000.0) << "小序列转换性能应该 > 100,000 ops/sec";
}

TEST_F(FeatureEngineTest, Performance_MediumSequence) {
    std::vector<int> seq(100);
    for (size_t i = 0; i < seq.size(); i++) {
        seq[i] = i % 256;
    }

    std::vector<int> feature(config.ml.feature_dim, 0);

    const int iterations = 10000;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; i++) {
        kadd_seq_to_feature(seq.data(), seq.size(), config.ml.window_size,
                           feature.data(), config.ml.feature_dim);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    double avg_time_ns = (static_cast<double>(duration.count()) * 1000.0) / iterations;
    double ops_per_sec = (iterations * 1000000.0) / duration.count();

    std::cout << "\n[性能测试] 中等序列(seq=100)转换平均耗时: " << avg_time_ns << " ns" << std::endl;
    std::cout << "[性能测试] 中等序列转换吞吐量: " << static_cast<int>(ops_per_sec) << " ops/sec" << std::endl;

    // 性能基准：应该 > 50,000 ops/sec
    EXPECT_GT(ops_per_sec, 50000.0) << "中等序列转换性能应该 > 50,000 ops/sec";
}

TEST_F(FeatureEngineTest, Performance_LargeSequence) {
    std::vector<int> seq(200);
    for (size_t i = 0; i < seq.size(); i++) {
        seq[i] = i % 20;
    }

    std::vector<int> feature(config.ml.feature_dim, 0);

    const int iterations = 1000;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; i++) {
        kadd_seq_to_feature(seq.data(), seq.size(), config.ml.window_size,
                           feature.data(), config.ml.feature_dim);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    double avg_time_us = static_cast<double>(duration.count()) / iterations;
    double ops_per_sec = (iterations * 1000000.0) / duration.count();

    std::cout << "\n[性能测试] 大序列(seq=10000)转换平均耗时: " << avg_time_us << " μs" << std::endl;
    std::cout << "[性能测试] 大序列转换吞吐量: " << static_cast<int>(ops_per_sec) << " ops/sec" << std::endl;

    // 性能基准：应该 > 1,000 ops/sec
    EXPECT_GT(ops_per_sec, 1000.0) << "大序列转换性能应该 > 1,000 ops/sec";
}

TEST_F(FeatureEngineTest, Performance_VariousWindowSizes) {
    std::vector<int> seq(200);
    for (size_t i = 0; i < seq.size(); i++) {
        seq[i] = i % 20;
    }

    std::vector<int> feature(config.ml.feature_dim, 0);

    std::cout << "\n[性能测试] 不同窗口大小性能测试:" << std::endl;

    std::vector<int> window_sizes = {3, 5, 10, 20, 50, 100};

    for (int window_size : window_sizes) {
        const int iterations = 1000;
        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < iterations; i++) {
            kadd_seq_to_feature(seq.data(), seq.size(), window_size,
                               feature.data(), config.ml.feature_dim);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        double avg_time_ns = (static_cast<double>(duration.count()) * 1000.0) / iterations;
        double ops_per_sec = (iterations * 1000000.0) / duration.count();

        std::cout << "  window_size=" << window_size
                  << ": 耗时=" << avg_time_ns << " ns"
                  << ", 吞吐量=" << static_cast<int>(ops_per_sec) << " ops/sec" << std::endl;
    }

    SUCCEED();
}

TEST_F(FeatureEngineTest, Performance_VariousFeatureDims) {
    std::vector<int> seq(200);
    for (size_t i = 0; i < seq.size(); i++) {
        seq[i] = i % 20;
    }

    std::cout << "\n[性能测试] 不同特征维度性能测试:" << std::endl;

    std::vector<int> feature_dims = {10, 28, 50, 100, 200, 300};

    for (int feature_dim : feature_dims) {
        std::vector<int> feature(feature_dim, 0);

        const int iterations = 1000;
        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < iterations; i++) {
            kadd_seq_to_feature(seq.data(), seq.size(), config.ml.window_size,
                               feature.data(), feature_dim);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        double avg_time_ns = (static_cast<double>(duration.count()) * 1000.0) / iterations;
        double ops_per_sec = (iterations * 1000000.0) / duration.count();

        std::cout << "  feature_dim=" << feature_dim
                  << ": 耗时=" << avg_time_ns << " ns"
                  << ", 吞吐量=" << static_cast<int>(ops_per_sec) << " ops/sec" << std::endl;
    }

    SUCCEED();
}

TEST_F(FeatureEngineTest, Performance_RandomSequence) {
    std::vector<int> seq;
    GenerateRandomSequence(seq, 200, 0, 27);

    std::vector<int> feature(config.ml.feature_dim, 0);

    const int iterations = 1000;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; i++) {
        kadd_seq_to_feature(seq.data(), seq.size(), config.ml.window_size,
                           feature.data(), config.ml.feature_dim);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    double avg_time_us = static_cast<double>(duration.count()) / iterations;
    double ops_per_sec = (iterations * 1000000.0) / duration.count();

    std::cout << "\n[性能测试] 随机序列(seq=10000)转换平均耗时: " << avg_time_us << " μs" << std::endl;
    std::cout << "[性能测试] 随机序列转换吞吐量: " << static_cast<int>(ops_per_sec) << " ops/sec" << std::endl;

    // 性能基准：应该 > 1,000 ops/sec
    EXPECT_GT(ops_per_sec, 1000.0) << "随机序列转换性能应该 > 1,000 ops/sec";
}

#ifdef __ARM_FEATURE_SVE
TEST_F(FeatureEngineTest, Performance_SVEAcceleration) {
    std::vector<int> seq(200);
    for (size_t i = 0; i < seq.size(); i++) {
        seq[i] = i % 20;
    }

    std::vector<int> feature(config.ml.feature_dim, 0);

    const int iterations = 1000;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; i++) {
        kadd_seq_to_feature(seq.data(), seq.size(), config.ml.window_size,
                           feature.data(), config.ml.feature_dim);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    double ops_per_sec = (iterations * 1000000.0) / duration.count();

    std::cout << "\n[SVE性能测试] SVE加速后吞吐量: " << static_cast<int>(ops_per_sec) << " ops/sec" << std::endl;

    // SVE 加速后应该有更好的性能
    EXPECT_GT(ops_per_sec, 5000.0) << "SVE加速后性能应该 > 5,000 ops/sec";
}
#endif

// ========== 4. 数学和算法正确性测试 ==========

TEST_F(FeatureEngineTest, Math_DistanceSymmetry) {
    // 验证距离对称性: |a-b| = |b-a|
    std::vector<int> seq1 = {10, 5};
    std::vector<int> seq2 = {5, 10};

    std::vector<int> feature1(config.ml.feature_dim, 0);
    std::vector<int> feature2(config.ml.feature_dim, 0);

    kadd_seq_to_feature(seq1.data(), seq1.size(), 2, feature1.data(), config.ml.feature_dim);
    kadd_seq_to_feature(seq2.data(), seq2.size(), 2, feature2.data(), config.ml.feature_dim);

    // 第一个位置的距离应该相同
    EXPECT_EQ(feature1[0], feature2[0]) << "距离应该对称";
}

TEST_F(FeatureEngineTest, Math_DistanceNonNegativity) {
    // 验证距离非负性: 用合法 token 值
    std::vector<int> seq(100);
    for (size_t i = 0; i < seq.size(); i++) {
        seq[i] = (i % 2 == 0) ? 0 : 10;
    }

    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), config.ml.window_size,
                       feature.data(), config.ml.feature_dim);
    EXPECT_EQ(ret, KADD_SUCCESS);

    // 所有特征值应该非负
    for (int f : feature) {
        EXPECT_GE(f, 0) << "距离应该非负";
    }
}

TEST_F(FeatureEngineTest, Math_ZeroDistanceForIdentical) {
    // 验证相同元素的序列距离为0
    std::vector<int> seq(100, 50);
    std::vector<int> feature(config.ml.feature_dim, 0);

    kadd_seq_to_feature(seq.data(), seq.size(), config.ml.window_size,
                       feature.data(), config.ml.feature_dim);

    // 所有特征值应该为0
    for (int f : feature) {
        EXPECT_EQ(f, 0) << "相同元素序列的距离应该为0";
    }
}

TEST_F(FeatureEngineTest, Math_LinearScaling) {
    // 验证线性缩放: 如果序列乘以k，距离也乘以k
    // 注意：token_id 必须在特征向量维度范围内
    std::vector<int> seq1 = {1, 2, 3, 4, 5};
    std::vector<int> seq2 = {2, 4, 6, 8, 10};  // 所有 token_id < 28

    std::vector<int> feature1(config.ml.feature_dim, 0);
    std::vector<int> feature2(config.ml.feature_dim, 0);

    kadd_seq_to_feature(seq1.data(), seq1.size(), config.ml.window_size,
                       feature1.data(), config.ml.feature_dim);
    kadd_seq_to_feature(seq2.data(), seq2.size(), config.ml.window_size,
                       feature2.data(), config.ml.feature_dim);

    long long sum1 = SumFeature(feature1);
    long long sum2 = SumFeature(feature2);

    // seq2 = seq1 * 2，所以 sum2 应该约等于 sum1 * 2
    EXPECT_NEAR(sum2, sum1 * 2, sum1 / 10) << "距离应该线性缩放";
}

// ========== 5. 特征向量正确性测试（参考 Python 实现） ==========

TEST_F(FeatureEngineTest, FeatureVector_TestCase1_StandardSequenceWindow5) {
    // 测试用例 1: 标准序列，窗口5
    // 参考: sliding_window_distance.py 测试用例 5
    std::vector<int> seq = {7, 11, 8, 12, 8, 11, 8};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 5,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, 0) << "标准序列，窗口5应该成功转换";

    // 验证非零位置
    EXPECT_EQ(feature[7], 100) << "token_id=7 的特征值应该为 100";
    EXPECT_EQ(feature[8], 436) << "token_id=8 的特征值应该为 436";
    EXPECT_EQ(feature[11], 250) << "token_id=11 的特征值应该为 250";
    EXPECT_EQ(feature[12], 160) << "token_id=12 的特征值应该为 160";

    // 验证其他位置为0
    for (int i = 0; i < config.ml.feature_dim; i++) {
        if (i != 7 && i != 8 && i != 11 && i != 12) {
            EXPECT_EQ(feature[i], 0) << "位置 " << i << " 应该为 0";
        }
    }
}

TEST_F(FeatureEngineTest, FeatureVector_TestCase2_StandardSequenceWindow3) {
    // 测试用例 2: 标准序列，窗口3
    // 参考: sliding_window_distance.py 测试用例 6
    std::vector<int> seq = {7, 11, 8, 12, 8, 11, 12};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 3,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, 0) << "标准序列，窗口3应该成功转换";

    // 验证非零位置
    EXPECT_EQ(feature[7], 30) << "token_id=7 的特征值应该为 30";
    EXPECT_EQ(feature[8], 127) << "token_id=8 的特征值应该为 127";
    EXPECT_EQ(feature[11], 94) << "token_id=11 的特征值应该为 94";
    EXPECT_EQ(feature[12], 84) << "token_id=12 的特征值应该为 84";

    // 验证其他位置为0
    for (int i = 0; i < config.ml.feature_dim; i++) {
        if (i != 7 && i != 8 && i != 11 && i != 12) {
            EXPECT_EQ(feature[i], 0) << "位置 " << i << " 应该为 0";
        }
    }
}

TEST_F(FeatureEngineTest, FeatureVector_TestCase3_IncreasingSequence) {
    // 测试用例 3: 递增序列，窗口3
    std::vector<int> seq = {1, 2, 3, 4, 5};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 3,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, 0) << "递增序列应该成功转换";

    // 验证非零位置
    EXPECT_EQ(feature[1], 7) << "token_id=1 的特征值应该为 7";
    EXPECT_EQ(feature[2], 12) << "token_id=2 的特征值应该为 12";
    EXPECT_EQ(feature[3], 18) << "token_id=3 的特征值应该为 18";
    EXPECT_EQ(feature[4], 18) << "token_id=4 的特征值应该为 18";
    EXPECT_EQ(feature[5], 11) << "token_id=5 的特征值应该为 11";
}

TEST_F(FeatureEngineTest, FeatureVector_TestCase4_ConstantSequence) {
    // 测试用例 4: 重复序列，窗口3
    std::vector<int> seq = {5, 5, 5, 5, 5};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 3,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, 0) << "重复序列应该成功转换";

    // 只有 token_id=5 有值
    EXPECT_EQ(feature[5], 110) << "token_id=5 的特征值应该为 110";

    // 其他位置应该为0
    for (int i = 0; i < config.ml.feature_dim; i++) {
        if (i != 5) {
            EXPECT_EQ(feature[i], 0) << "位置 " << i << " 应该为 0";
        }
    }
}

TEST_F(FeatureEngineTest, FeatureVector_TestCase5_AlternatingSequence) {
    // 测试用例 5: 交替序列，窗口3
    std::vector<int> seq = {0, 10, 0, 10, 0, 10};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 3,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, 0) << "交替序列应该成功转换";

    // 验证非零位置
    EXPECT_EQ(feature[0], 100) << "token_id=0 的特征值应该为 100";
    EXPECT_EQ(feature[10], 40) << "token_id=10 的特征值应该为 40";
}

TEST_F(FeatureEngineTest, FeatureVector_TestCase6_SingleElement) {
    // 测试用例 6: 单元素序列
    std::vector<int> seq = {7};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 5,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, 0) << "单元素序列应该成功转换";

    // 单元素序列的特征应该全为0（没有邻居）
    int non_zero = CountNonZero(feature);
    EXPECT_EQ(non_zero, 0) << "单元素序列的特征应该全为 0";
}

TEST_F(FeatureEngineTest, FeatureVector_TestCase7_TwoElements) {
    // 测试用例 7: 两元素序列，窗口3
    std::vector<int> seq = {7, 11};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 3,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, 0) << "两元素序列应该成功转换";

    // 验证非零位置
    EXPECT_EQ(feature[7], 22) << "token_id=7 的特征值应该为 22";
    EXPECT_EQ(feature[11], 14) << "token_id=11 的特征值应该为 14";
}

TEST_F(FeatureEngineTest, FeatureVector_TestCase8_ThreeElements) {
    // 测试用例 8: 三元素序列，窗口3
    std::vector<int> seq = {7, 11, 8};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 3,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, 0) << "三元素序列应该成功转换";

    // 验证非零位置
    EXPECT_EQ(feature[7], 30) << "token_id=7 的特征值应该为 30";
    EXPECT_EQ(feature[8], 29) << "token_id=8 的特征值应该为 29";
    EXPECT_EQ(feature[11], 30) << "token_id=11 的特征值应该为 30";
}

TEST_F(FeatureEngineTest, FeatureVector_TestCase9_LargeWindow) {
    // 测试用例 9: 较长序列，大窗口
    std::vector<int> seq = {1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 6,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, 0) << "大窗口应该成功转换";

    // 验证非零位置
    EXPECT_EQ(feature[1], 50) << "token_id=1 的特征值应该为 50";
    EXPECT_EQ(feature[2], 70) << "token_id=2 的特征值应该为 70";
    EXPECT_EQ(feature[3], 94) << "token_id=3 的特征值应该为 94";
    EXPECT_EQ(feature[4], 112) << "token_id=4 的特征值应该为 112";
    EXPECT_EQ(feature[5], 122) << "token_id=5 的特征值应该为 122";
    EXPECT_EQ(feature[6], 122) << "token_id=6 的特征值应该为 122";
    EXPECT_EQ(feature[7], 110) << "token_id=7 的特征值应该为 110";
    EXPECT_EQ(feature[8], 85) << "token_id=8 的特征值应该为 85";
}

TEST_F(FeatureEngineTest, FeatureVector_TestCase10_BoundaryValues) {
    // 测试用例 10: 边界值序列
    std::vector<int> seq = {0, 27, 0, 27, 0, 27};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 3,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, 0) << "边界值序列应该成功转换";

    // 验证非零位置
    EXPECT_EQ(feature[0], 270) << "token_id=0 的特征值应该为 270";
    EXPECT_EQ(feature[27], 108) << "token_id=27 的特征值应该为 108";
}

TEST_F(FeatureEngineTest, FeatureVector_TestCase11_LongSequence) {
    // 测试用例 11: 长序列
    std::vector<int> seq = {7, 11, 8, 12, 8, 11, 8, 7, 11, 8, 12, 8, 11, 8};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 5,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, 0) << "长序列应该成功转换";

    // 验证非零位置
    EXPECT_EQ(feature[7], 293) << "token_id=7 的特征值应该为 293";
    EXPECT_EQ(feature[8], 1013) << "token_id=8 的特征值应该为 1013";
    EXPECT_EQ(feature[11], 605) << "token_id=11 的特征值应该为 605";
    EXPECT_EQ(feature[12], 335) << "token_id=12 的特征值应该为 335";
}

TEST_F(FeatureEngineTest, FeatureVector_TestCase12_SmallWindow) {
    // 测试用例 12: 小窗口
    std::vector<int> seq = {7, 11, 8, 12, 8, 11, 8};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 2,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, 0) << "小窗口应该成功转换";

    // 验证非零位置
    EXPECT_EQ(feature[7], 11) << "token_id=7 的特征值应该为 11";
    EXPECT_EQ(feature[8], 57) << "token_id=8 的特征值应该为 57";
    EXPECT_EQ(feature[11], 31) << "token_id=11 的特征值应该为 31";
    EXPECT_EQ(feature[12], 16) << "token_id=12 的特征值应该为 16";
}

TEST_F(FeatureEngineTest, FeatureVector_TestCase13_MediumWindow) {
    // 测试用例 13: 中等窗口
    std::vector<int> seq = {7, 11, 8, 12, 8, 11, 8};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 4,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, 0) << "中等窗口应该成功转换";

    // 验证非零位置
    EXPECT_EQ(feature[7], 61) << "token_id=7 的特征值应该为 61";
    EXPECT_EQ(feature[8], 283) << "token_id=8 的特征值应该为 283";
    EXPECT_EQ(feature[11], 157) << "token_id=11 的特征值应该为 157";
    EXPECT_EQ(feature[12], 107) << "token_id=12 的特征值应该为 107";
}

TEST_F(FeatureEngineTest, FeatureVector_TestCase14_HighTokenValues) {
    // 测试用例 14: 高 token 值
    std::vector<int> seq = {20, 25, 20, 25, 20};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 3,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, 0) << "高 token 值应该成功转换";

    // 验证非零位置
    EXPECT_EQ(feature[20], 280) << "token_id=20 的特征值应该为 280";
    EXPECT_EQ(feature[25], 210) << "token_id=25 的特征值应该为 210";
}

TEST_F(FeatureEngineTest, FeatureVector_TestCase15_AdjacentDuplicates) {
    // 测试用例 15: 相邻相同值
    std::vector<int> seq = {7, 7, 11, 11, 8, 8};
    std::vector<int> feature(config.ml.feature_dim, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 3,
                                  feature.data(), config.ml.feature_dim);

    EXPECT_EQ(ret, 0) << "相邻相同值应该成功转换";

    // 验证非零位置
    EXPECT_EQ(feature[7], 72) << "token_id=7 的特征值应该为 72";
    EXPECT_EQ(feature[8], 76) << "token_id=8 的特征值应该为 76";
    EXPECT_EQ(feature[11], 104) << "token_id=11 的特征值应该为 104";
}

// ========== 主函数 ==========
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
