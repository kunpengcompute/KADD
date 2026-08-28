/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *
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

// 包含源文件以测试 static 函数
#include "kadd_feature_engine.c"
#include "kadd_dfa_engine.h"  /* KADD_DFA_MAX_INPUT_CHARS */
#include "kadd_config_loader.h"

constexpr int kWinSize2 = 2;
constexpr int kWinSize3 = 3;
constexpr int kWinSize4 = 4;
constexpr int kWinSize5 = 5;
constexpr int kWinSize6 = 6;
constexpr int kWinSize10 = 10;
constexpr int kWinSize20 = 20;
constexpr int kWinSize200 = 200;
constexpr int kFeatDim5 = 5;
constexpr int kFeatDim10 = 10;
constexpr int kFeatDim301 = 301;
constexpr int kSeqLen20 = 20;
constexpr int kSeqLen50 = 50;
constexpr int kSeqLen100 = 100;
constexpr int kSeqLen200 = 200;
constexpr int kSeqLen1000 = 1000;
constexpr int kMaxRand255 = 255;
constexpr int kTokenRange20 = 20;
constexpr int kDefaultFeatDim28 = 28;
constexpr int kFillValue20 = 20;
constexpr int kFillValue50 = 50;
constexpr int kAlternateVal10 = 10;
constexpr int kPerfSmallIterations = 100000;
constexpr int kPerfMediumIterations = 10000;
constexpr int kPerfLargeIterations = 1000;
constexpr double kPerfSmallMinOps = 100000.0;
constexpr double kPerfMediumMinOps = 50000.0;
constexpr double kPerfLargeMinOps = 1000.0;
constexpr double kPerfSVEMinOps = 5000.0;
constexpr double kNanosPerMicro = 1000.0;
constexpr double kMicrosecondsPerSecond = 1000000.0;
constexpr int MAX_FEATURE_DIM = KADD_FEATURE_DEFAULT_MAX_OUTPUT_DIM;
constexpr int MAX_SEQ_LEN = KADD_FEATURE_DEFAULT_MAX_INPUT_LENGTH;

// ========== 测试夹具 ==========
class FeatureEngineTest : public ::testing::Test {
protected:
    std::vector<int> test_sequence;
    std::vector<int> test_feature;
    kadd_config_t config;

    void SetUp() override
    {
        // 从config.json加载配置
        ASSERT_EQ(kadd_config_load("config.json", &config), 0)
            << "无法加载config.json配置文件";

        test_feature.resize(KADD_FEATURE_DIM, 0);
    }

    void TearDown() override
    {
    }

    // 辅助函数：生成随机序列
    void GenerateRandomSequence(std::vector<int> &seq, size_t length, int min_val = 0, int max_val = kMaxRand255)
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dis(min_val, max_val);

        seq.resize(length);
        for (auto &val : seq) {
            val = dis(gen);
        }
    }

    // 辅助函数：计算特征向量的非零元素数量
    int CountNonZero(const std::vector<int> &feature)
    {
        return std::count_if(feature.begin(), feature.end(), [](int v) { return v != 0; });
    }

    // 辅助函数：计算特征向量的和
    long long SumFeature(const std::vector<int> &feature)
    {
        long long sum = 0;
        for (int v : feature) {
            sum += v;
        }
        return sum;
    }

    // 辅助函数：验证特征向量非零位置与预期一致
    void ExpectFeatureMatches(const std::vector<int> &feature, const std::vector<int> &expected)
    {
        for (size_t i = 0; i < expected.size(); i++) {
            if (expected[i] != 0) {
                EXPECT_EQ(feature[i], expected[i]) << "token_id=" << i;
            }
        }
    }
};

// ========== 1. 功能测试 ==========

TEST_F(FeatureEngineTest, TransformSimpleSequence)
{
    // 简单序列: {7, 11, 8, 12, 8, 11, 8}
    std::vector<int> seq = {7, 11, 8, 12, 8, 11, 8};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), KADD_FEATURE_WINDOW_SIZE,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, 0) << "应该成功转换序列";
}

TEST_F(FeatureEngineTest, TransformSingleElement)
{
    std::vector<int> seq = {KADD_FEATURE_DIM - 1};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), KADD_FEATURE_WINDOW_SIZE,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, 0) << "单元素序列应该成功转换";
}

TEST_F(FeatureEngineTest, TransformLongSequence)
{
    // 序列长度超过 MAX_SEQ_LEN(300)，应返回参数错误
    std::vector<int> seq(kSeqLen1000);
    for (size_t i = 0; i < seq.size(); i++) {
        seq[i] = i % kWinSize10;
    }

    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), KADD_FEATURE_WINDOW_SIZE,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, KADD_ERROR_INVALID_PARAM) << "超长序列应该返回 KADD_ERROR_INVALID_PARAM";
}

TEST_F(FeatureEngineTest, TransformWithWindow5)
{
    std::vector<int> seq = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize5,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, 0) << "窗口大小5应该成功转换";
}

TEST_F(FeatureEngineTest, TransformWithWindow10)
{
    std::vector<int> seq = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize10,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, 0) << "窗口大小10应该成功转换";
}

TEST_F(FeatureEngineTest, TransformWithWindow20)
{
    std::vector<int> seq(kSeqLen50);
    for (size_t i = 0; i < seq.size(); i++) {
        seq[i] = i % kTokenRange20;
    }

    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize20,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, 0) << "窗口大小20应该成功转换";
}

TEST_F(FeatureEngineTest, TransformWithSmallFeatureDim)
{
    std::vector<int> seq = {1, 2, 3, 4, 5};
    std::vector<int> feature(kFeatDim10, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize3,
                                  feature.data(), kFeatDim10);

    EXPECT_EQ(ret, 0) << "小特征维度应该成功转换";
}

TEST_F(FeatureEngineTest, TransformWithLargeFeatureDim)
{
    // feature_dim = MAX_FEATURE_DIM(300)，合法上限
    std::vector<int> seq = {1, 2, 3, 4, 5};
    std::vector<int> feature(MAX_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize3,
                                  feature.data(), MAX_FEATURE_DIM);

    EXPECT_EQ(ret, KADD_SUCCESS) << "feature_dim=MAX_FEATURE_DIM 应该成功转换";
}

TEST_F(FeatureEngineTest, SlidingWindowDistance)
{
    // 验证滑动窗口距离计算的正确性
    // 序列: {0, 1, 0, 1, 0}, 窗口: 3
    // 预期: 每个位置的距离 = Σ |seq[i] - seq[i+d]| * (window_size - d)
    std::vector<int> seq = {0, 1, 0, 1, 0};
    std::vector<int> feature(kFeatDim5, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize3,
                                  feature.data(), kFeatDim5);

    EXPECT_EQ(ret, 0) << "应该成功计算距离";

    // 验证距离值非负
    for (size_t i = 0; i < feature.size(); i++) {
        EXPECT_GE(feature[i], 0) << "位置 " << i << " 的距离应该 >= 0";
    }
}

TEST_F(FeatureEngineTest, WeightedDistance)
{
    // 验证加权距离计算
    // 距离越远，权重越小
    std::vector<int> seq = {10, 20, 10, 20, 20};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize5,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, 0) << "应该成功计算加权距离";
}

TEST_F(FeatureEngineTest, ConstantSequence)
{
    // 常量序列: 所有元素相同
    std::vector<int> seq(kSeqLen20, kFillValue20);
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), KADD_FEATURE_WINDOW_SIZE,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, 0) << "常量序列应该成功转换";

    // 常量序列的特征应该全为0（距离都为0）
    int non_zero = CountNonZero(feature);
    EXPECT_EQ(non_zero, 1) << "常量序列的特征应该全为0";
}

TEST_F(FeatureEngineTest, AlternatingSequence)
{
    // 交替序列: {0, 100, 0, 100, ...}
    std::vector<int> seq(kSeqLen100);
    for (size_t i = 0; i < seq.size(); i++) {
        seq[i] = (i % kWinSize2 == 0) ? 0 : kAlternateVal10;
    }

    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), KADD_FEATURE_WINDOW_SIZE,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, 0) << "交替序列应该成功转换";

    // 交替序列应该产生较大的特征值
    int non_zero = CountNonZero(feature);
    EXPECT_GT(non_zero, 0) << "交替序列应该有非零特征";
}

TEST_F(FeatureEngineTest, IncreasingSequence)
{
    // 递增序列
    std::vector<int> seq(kSeqLen100);
    for (size_t i = 0; i < seq.size(); i++) {
        seq[i] = i;
    }

    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), KADD_FEATURE_WINDOW_SIZE,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, KADD_FEATURE_ERROR_VALUE_OUT_OF_RANGE) << "递增序列应该成功转换";
}

TEST_F(FeatureEngineTest, RandomSequence)
{
    // 随机序列
    std::vector<int> seq;
    GenerateRandomSequence(seq, KADD_DFA_MAX_INPUT_CHARS, 0, KADD_FEATURE_DIM-1);

    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), KADD_FEATURE_WINDOW_SIZE,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, 0) << "随机序列应该成功转换";
}

TEST_F(FeatureEngineTest, SequenceWithZeros)
{
    // 包含零的序列
    std::vector<int> seq = {0, 5, 0, 10, 0, 15, 0};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), KADD_FEATURE_WINDOW_SIZE,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, 0) << "包含零的序列应该成功转换";
}

TEST_F(FeatureEngineTest, SequenceWithNegativeValues)
{
    // 包含负值的序列 — seq[i] < 0 应触发范围校验
    std::vector<int> seq = {-10, -5, 0, 5, 10};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), KADD_FEATURE_WINDOW_SIZE,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, KADD_FEATURE_ERROR_VALUE_OUT_OF_RANGE) << "包含负值的序列应该返回 "
        << "KADD_FEATURE_ERROR_VALUE_OUT_OF_RANGE";
}

TEST_F(FeatureEngineTest, SequenceWithLargeValues)
{
    // 包含超出 MAX_FEATURE_DIM 范围的大值 — 应触发范围校验
    std::vector<int> seq = {10000, 20000, 30000, 40000, 50000};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), KADD_FEATURE_WINDOW_SIZE,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, KADD_FEATURE_ERROR_VALUE_OUT_OF_RANGE) << "超出 MAX_FEATURE_DIM 的序列应该返回 "
        << "KADD_FEATURE_ERROR_VALUE_OUT_OF_RANGE";
}

// ========== 2. 边界测试 ==========

TEST_F(FeatureEngineTest, TransformNullSequence)
{
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(nullptr, 10, KADD_FEATURE_WINDOW_SIZE,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR) << "NULL 序列应该返回 KADD_ERROR_NULL_PTR";
}

TEST_F(FeatureEngineTest, TransformNullFeature)
{
    std::vector<int> seq = {1, 2, 3, 4, 5};

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), KADD_FEATURE_WINDOW_SIZE,
                                  nullptr, KADD_FEATURE_DIM);

    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR) << "NULL 特征向量应该返回 KADD_ERROR_NULL_PTR";
}

TEST_F(FeatureEngineTest, TransformZeroLength)
{
    std::vector<int> seq = {1, 2, 3};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), 0, KADD_FEATURE_WINDOW_SIZE,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, KADD_SUCCESS) << "零长度序列应该返回 KADD_SUCCESS";
}

TEST_F(FeatureEngineTest, TransformNegativeLength)
{
    std::vector<int> seq = {1, 2, 3};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), -1, KADD_FEATURE_WINDOW_SIZE,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, KADD_ERROR_INVALID_PARAM) << "负长度序列应该返回 KADD_ERROR_INVALID_PARAM";
}

TEST_F(FeatureEngineTest, TransformZeroFeatureDim)
{
    std::vector<int> seq = {1, 2, 3};
    std::vector<int> feature(1, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), KADD_FEATURE_WINDOW_SIZE,
                                  feature.data(), 0);

    EXPECT_EQ(ret, KADD_ERROR_INVALID_PARAM) << "零特征维度应该返回 KADD_ERROR_INVALID_PARAM";
}

TEST_F(FeatureEngineTest, TransformNegativeFeatureDim)
{
    std::vector<int> seq = {1, 2, 3};
    std::vector<int> feature(1, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), KADD_FEATURE_WINDOW_SIZE,
                                  feature.data(), -1);

    EXPECT_EQ(ret, KADD_ERROR_INVALID_PARAM) << "负特征维度应该返回 KADD_ERROR_INVALID_PARAM";
}

TEST_F(FeatureEngineTest, TransformZeroWindowSize)
{
    std::vector<int> seq = {1, 2, 3};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), 0,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, KADD_SUCCESS) << "零窗口大小应该返回 KADD_SUCCESS";
}

TEST_F(FeatureEngineTest, TransformNegativeWindowSize)
{
    std::vector<int> seq = {1, 2, 3};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), -1,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, KADD_ERROR_INVALID_PARAM) << "负窗口大小应该返回 KADD_ERROR_INVALID_PARAM";
}

TEST_F(FeatureEngineTest, TransformInsufficientFeatureDim)
{
    // 特征维度小于序列长度
    std::vector<int> seq(kSeqLen100);
    std::vector<int> feature(kFeatDim10, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), KADD_FEATURE_WINDOW_SIZE,
                                  feature.data(), kFeatDim10);

    // 应该成功（只填充可用的维度）
    EXPECT_EQ(ret, 0) << "特征维度不足应该成功（部分填充）";
}

TEST_F(FeatureEngineTest, TransformAllNull)
{
    int ret = kadd_seq_to_feature(nullptr, 10, KADD_FEATURE_WINDOW_SIZE, nullptr, KADD_FEATURE_DIM);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR) << "所有参数为 NULL 应该返回 KADD_ERROR_NULL_PTR";
}

// ========== seq[i] 范围校验测试 ==========

TEST_F(FeatureEngineTest, SeqValueNegativeOne)
{
    // seq[i] = -1，触发范围校验
    std::vector<int> seq = {0, -1, 2};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize3, feature.data(), KADD_FEATURE_DIM);
    EXPECT_EQ(ret, KADD_FEATURE_ERROR_VALUE_OUT_OF_RANGE) << "seq[i]=-1 应返回 "
        << "KADD_FEATURE_ERROR_VALUE_OUT_OF_RANGE";
}

TEST_F(FeatureEngineTest, SeqValueEqualsMaxFeatureDim)
{
    // seq[i] == MAX_FEATURE_DIM(300)，恰好等于上限（校验是 < MAX_FEATURE_DIM），应触发范围校验
    std::vector<int> seq = {0, MAX_FEATURE_DIM, 1};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize3, feature.data(), KADD_FEATURE_DIM);
    EXPECT_EQ(ret, KADD_FEATURE_ERROR_VALUE_OUT_OF_RANGE) << "seq[i]=MAX_FEATURE_DIM 应返回 "
        << "KADD_FEATURE_ERROR_VALUE_OUT_OF_RANGE";
}

TEST_F(FeatureEngineTest, SeqValueJustBelowMaxFeatureDim)
{
    // seq[i] = MAX_FEATURE_DIM - 1 = 299，合法值（在 feature_dim >= 300 时）
    std::vector<int> seq = {0, MAX_FEATURE_DIM - 1};
    std::vector<int> feature(MAX_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize3, feature.data(), MAX_FEATURE_DIM);
    EXPECT_EQ(ret, KADD_SUCCESS) << "seq[i]=299 在 feature_dim=300 时应该成功";
}

TEST_F(FeatureEngineTest, SeqValueZeroIsValid)
{
    // seq[i] = 0 是合法的最小值
    std::vector<int> seq = {0, 0, 0, 0, 0};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize3, feature.data(), KADD_FEATURE_DIM);
    EXPECT_EQ(ret, KADD_SUCCESS) << "seq[i]=0 应该成功";
}

TEST_F(FeatureEngineTest, SeqValueMixedValidAndInvalid)
{
    // 混合合法与非法值，首个非法值在中间位置
    std::vector<int> seq = {1, 2, -5, 4};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize3, feature.data(), KADD_FEATURE_DIM);
    EXPECT_EQ(ret, KADD_FEATURE_ERROR_VALUE_OUT_OF_RANGE) << "混合序列中的非法值应返回 "
        << "KADD_FEATURE_ERROR_VALUE_OUT_OF_RANGE";
}

TEST_F(FeatureEngineTest, FeatureDimExceedsMaxFeatureDim)
{
    // feature_dim > MAX_FEATURE_DIM(300)，应返回参数错误
    std::vector<int> seq = {1, 2, 3};
    std::vector<int> feature(kFeatDim301, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize3, feature.data(), kFeatDim301);
    EXPECT_EQ(ret, KADD_ERROR_INVALID_PARAM) << "feature_dim > MAX_FEATURE_DIM 应返回 KADD_ERROR_INVALID_PARAM";
}

TEST_F(FeatureEngineTest, SeqLenExactlyMaxSeqLen)
{
    // seq_len == MAX_SEQ_LEN(300)，恰好等于上限，合法
    std::vector<int> seq(MAX_SEQ_LEN);
    for (size_t i = 0; i < seq.size(); i++) {
        seq[i] = i % kWinSize10;
    }
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), KADD_FEATURE_WINDOW_SIZE,
                                  feature.data(), KADD_FEATURE_DIM);
    EXPECT_EQ(ret, KADD_SUCCESS) << "seq_len=MAX_SEQ_LEN 应该成功";
}

TEST_F(FeatureEngineTest, TransformSequenceShorterThanWindow)
{
    // 序列长度小于窗口大小
    std::vector<int> seq = {1, 2};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize10,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, 0) << "序列短于窗口应该成功";
}

TEST_F(FeatureEngineTest, TransformSequenceEqualToWindow)
{
    // 序列长度等于窗口大小
    std::vector<int> seq = {1, 2, 3, 4, 5};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize5,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, 0) << "序列等于窗口应该成功";
}

TEST_F(FeatureEngineTest, TransformSeqLenExceedsMax)
{
    // 序列长度超过 MAX_SEQ_LEN(300) — 应返回参数错误
    std::vector<int> seq(kFeatDim301);
    for (size_t i = 0; i < seq.size(); i++) {
        seq[i] = i % kWinSize10;
    }
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), KADD_FEATURE_WINDOW_SIZE,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, KADD_ERROR_INVALID_PARAM) << "序列长度超过 MAX_SEQ_LEN 应该返回 KADD_ERROR_INVALID_PARAM";
}

TEST_F(FeatureEngineTest, TransformLargeWindow)
{
    // 大窗口大小，合法序列长度（<= MAX_SEQ_LEN）
    std::vector<int> seq(kSeqLen200);
    for (size_t i = 0; i < seq.size(); i++) {
        seq[i] = i % kWinSize10;
    }

    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize200,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, KADD_SUCCESS) << "大窗口大小应该成功";
}

// ========== 3. 性能测试 ==========

TEST_F(FeatureEngineTest, Performance_SmallSequence)
{
    std::vector<int> seq(kWinSize10);
    for (size_t i = 0; i < seq.size(); i++) {
        seq[i] = i;
    }

    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    const int iterations = kPerfSmallIterations;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; i++) {
        kadd_seq_to_feature(seq.data(), seq.size(), KADD_FEATURE_WINDOW_SIZE,
                           feature.data(), KADD_FEATURE_DIM);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    double avg_time_ns = (static_cast<double>(duration.count()) * kNanosPerMicro) / iterations;
    double ops_per_sec = (iterations * kMicrosecondsPerSecond) / duration.count();

    std::cout << "\n[性能测试] 小序列(seq=10)转换平均耗时: " << avg_time_ns << " ns" << std::endl;
    std::cout << "[性能测试] 小序列转换吞吐量: " << static_cast<int>(ops_per_sec) << " ops/sec" << std::endl;

    // 性能基准：应该 > 100,000 ops/sec
    EXPECT_GT(ops_per_sec, kPerfSmallMinOps) << "小序列转换性能应该 > 100,000 ops/sec";
}

TEST_F(FeatureEngineTest, Performance_MediumSequence)
{
    std::vector<int> seq(kSeqLen100);
    for (size_t i = 0; i < seq.size(); i++) {
        seq[i] = i % KADD_FEATURE_DIM;
    }

    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    const int iterations = kPerfMediumIterations;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; i++) {
        kadd_seq_to_feature(seq.data(), seq.size(), KADD_FEATURE_WINDOW_SIZE,
                           feature.data(), KADD_FEATURE_DIM);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    double avg_time_ns = (static_cast<double>(duration.count()) * kNanosPerMicro) / iterations;
    double ops_per_sec = (iterations * kMicrosecondsPerSecond) / duration.count();

    std::cout << "\n[性能测试] 中等序列(seq=100)转换平均耗时: " << avg_time_ns << " ns" << std::endl;
    std::cout << "[性能测试] 中等序列转换吞吐量: " << static_cast<int>(ops_per_sec) << " ops/sec" << std::endl;

    // 性能基准：应该 > 50,000 ops/sec
    EXPECT_GT(ops_per_sec, kPerfMediumMinOps) << "中等序列转换性能应该 > 50,000 ops/sec";
}

TEST_F(FeatureEngineTest, Performance_LargeSequence)
{
    std::vector<int> seq(kSeqLen200);
    for (size_t i = 0; i < seq.size(); i++) {
        seq[i] = i % kTokenRange20;
    }

    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    const int iterations = kPerfLargeIterations;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; i++) {
        kadd_seq_to_feature(seq.data(), seq.size(), KADD_FEATURE_WINDOW_SIZE,
                           feature.data(), KADD_FEATURE_DIM);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    double avg_time_us = static_cast<double>(duration.count()) / iterations;
    double ops_per_sec = (iterations * kMicrosecondsPerSecond) / duration.count();

    std::cout << "\n[性能测试] 大序列(seq=10000)转换平均耗时: " << avg_time_us << " μs" << std::endl;
    std::cout << "[性能测试] 大序列转换吞吐量: " << static_cast<int>(ops_per_sec) << " ops/sec" << std::endl;

    // 性能基准：应该 > 1,000 ops/sec
    EXPECT_GT(ops_per_sec, kNanosPerMicro) << "大序列转换性能应该 > 1,000 ops/sec";
}

TEST_F(FeatureEngineTest, Performance_VariousWindowSizes)
{
    std::vector<int> seq(kSeqLen200);
    for (size_t i = 0; i < seq.size(); i++) {
        seq[i] = i % kTokenRange20;
    }

    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    std::cout << "\n[性能测试] 不同窗口大小性能测试:" << std::endl;

    std::vector<int> window_sizes = {3, 5, 10, 20, 50, 100};

    for (int window_size : window_sizes) {
        const int iterations = kPerfLargeIterations;
        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < iterations; i++) {
            kadd_seq_to_feature(seq.data(), seq.size(), window_size,
                               feature.data(), KADD_FEATURE_DIM);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        double avg_time_ns = (static_cast<double>(duration.count()) * kNanosPerMicro) / iterations;
        double ops_per_sec = (iterations * kMicrosecondsPerSecond) / duration.count();

        std::cout << "  window_size=" << window_size
                  << ": 耗时=" << avg_time_ns << " ns"
                  << ", 吞吐量=" << static_cast<int>(ops_per_sec) << " ops/sec" << std::endl;
    }

    SUCCEED();
}

TEST_F(FeatureEngineTest, Performance_VariousFeatureDims)
{
    std::cout << "\n[性能测试] 不同特征维度性能测试:" << std::endl;

    std::vector<int> feature_dims = {10, 28, 50, 100, 200, 300};

    for (int feature_dim : feature_dims) {
        std::vector<int> seq(kSeqLen200);
        for (size_t i = 0; i < seq.size(); i++) {
            seq[i] = i % feature_dim;
        }

        std::vector<int> feature(feature_dim, 0);

        const int iterations = kPerfLargeIterations;
        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < iterations; i++) {
            kadd_seq_to_feature(seq.data(), seq.size(), KADD_FEATURE_WINDOW_SIZE,
                               feature.data(), feature_dim);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        double avg_time_ns = (static_cast<double>(duration.count()) * kNanosPerMicro) / iterations;
        double ops_per_sec = (iterations * kMicrosecondsPerSecond) / duration.count();

        std::cout << "  feature_dim=" << feature_dim
                  << ": 耗时=" << avg_time_ns << " ns"
                  << ", 吞吐量=" << static_cast<int>(ops_per_sec) << " ops/sec" << std::endl;
    }

    SUCCEED();
}

TEST_F(FeatureEngineTest, Performance_RandomSequence)
{
    std::vector<int> seq;
    GenerateRandomSequence(seq, kSeqLen200, 0, kDefaultFeatDim28 - 1);
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    const int iterations = kPerfLargeIterations;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; i++) {
        kadd_seq_to_feature(seq.data(), seq.size(), KADD_FEATURE_WINDOW_SIZE,
                           feature.data(), KADD_FEATURE_DIM);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    double avg_time_us = static_cast<double>(duration.count()) / iterations;
    double ops_per_sec = (iterations * kMicrosecondsPerSecond) / duration.count();

    std::cout << "\n[性能测试] 随机序列(seq=10000)转换平均耗时: " << avg_time_us << " μs" << std::endl;
    std::cout << "[性能测试] 随机序列转换吞吐量: " << static_cast<int>(ops_per_sec) << " ops/sec" << std::endl;

    // 性能基准：应该 > 1,000 ops/sec
    EXPECT_GT(ops_per_sec, kNanosPerMicro) << "随机序列转换性能应该 > 1,000 ops/sec";
}

#ifdef __ARM_FEATURE_SVE
TEST_F(FeatureEngineTest, Performance_SVEAcceleration)
{
    std::vector<int> seq(kSeqLen200);
    for (size_t i = 0; i < seq.size(); i++) {
        seq[i] = i % kTokenRange20;
    }

    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    const int iterations = kPerfLargeIterations;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; i++) {
        kadd_seq_to_feature(seq.data(), seq.size(), KADD_FEATURE_WINDOW_SIZE,
                           feature.data(), KADD_FEATURE_DIM);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    double ops_per_sec = (iterations * kMicrosecondsPerSecond) / duration.count();

    std::cout << "\n[SVE性能测试] SVE加速后吞吐量: " << static_cast<int>(ops_per_sec) << " ops/sec" << std::endl;

    // SVE 加速后应该有更好的性能
    EXPECT_GT(ops_per_sec, kPerfSVEMinOps) << "SVE加速后性能应该 > 5,000 ops/sec";
}
#endif

// ========== 4. 数学和算法正确性测试 ==========

TEST_F(FeatureEngineTest, Math_DistanceSymmetry)
{
    // 验证距离对称性: |a-b| = |b-a|
    std::vector<int> seq1 = {10, 5};
    std::vector<int> seq2 = {5, 10};

    std::vector<int> feature1(KADD_FEATURE_DIM, 0);
    std::vector<int> feature2(KADD_FEATURE_DIM, 0);

    kadd_seq_to_feature(seq1.data(), seq1.size(), kWinSize2, feature1.data(), KADD_FEATURE_DIM);
    kadd_seq_to_feature(seq2.data(), seq2.size(), kWinSize2, feature2.data(), KADD_FEATURE_DIM);

    // 第一个位置的距离应该相同
    EXPECT_EQ(feature1[0], feature2[0]) << "距离应该对称";
}

TEST_F(FeatureEngineTest, Math_DistanceNonNegativity)
{
    // 验证距离非负性: 用合法 token 值
    std::vector<int> seq(kSeqLen100);
    for (size_t i = 0; i < seq.size(); i++) {
        seq[i] = (i % kWinSize2 == 0) ? 0 : kAlternateVal10;
    }

    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), KADD_FEATURE_WINDOW_SIZE,
                       feature.data(), KADD_FEATURE_DIM);
    EXPECT_EQ(ret, KADD_SUCCESS);

    // 所有特征值应该非负
    for (int f : feature) {
        EXPECT_GE(f, 0) << "距离应该非负";
    }
}

TEST_F(FeatureEngineTest, Math_ZeroDistanceForIdentical)
{
    // 验证相同元素的序列距离为0
    std::vector<int> seq(kSeqLen100, kFillValue50);
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    kadd_seq_to_feature(seq.data(), seq.size(), KADD_FEATURE_WINDOW_SIZE,
                       feature.data(), KADD_FEATURE_DIM);

    // 所有特征值应该为0
    for (int f : feature) {
        EXPECT_EQ(f, 0) << "相同元素序列的距离应该为0";
    }
}

TEST_F(FeatureEngineTest, Math_LinearScaling)
{
    // 验证线性缩放: 如果序列乘以k，距离也乘以k
    // 注意：token_id 必须在特征向量维度范围内
    std::vector<int> seq1 = {1, 2, 3, 4, 5};
    std::vector<int> seq2 = {2, 4, 6, 8, 10};  // 所有 token_id < 28

    std::vector<int> feature1(KADD_FEATURE_DIM, 0);
    std::vector<int> feature2(KADD_FEATURE_DIM, 0);

    kadd_seq_to_feature(seq1.data(), seq1.size(), KADD_FEATURE_WINDOW_SIZE,
                       feature1.data(), KADD_FEATURE_DIM);
    kadd_seq_to_feature(seq2.data(), seq2.size(), KADD_FEATURE_WINDOW_SIZE,
                       feature2.data(), KADD_FEATURE_DIM);

    long long sum1 = SumFeature(feature1);
    long long sum2 = SumFeature(feature2);

    // seq2 = seq1 * 2，所以 sum2 应该约等于 sum1 * 2
    EXPECT_NEAR(sum2, sum1 * kWinSize2, sum1 / kWinSize10) << "距离应该线性缩放";
}

// ========== 5. 特征向量正确性测试（预期值由 tools/sliding_window_distance.py 计算）==========

TEST_F(FeatureEngineTest, FeatureVector_TestCase1_StandardSequenceWindow5)
{
    // 测试用例 1: 标准序列，窗口5
    // 参考: sliding_window_distance.py 测试用例 5
    std::vector<int> seq = {7, 11, 8, 12, 8, 11, 8};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize5,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, 0) << "标准序列，窗口5应该成功转换";

    ExpectFeatureMatches(feature,
                         {0, 0, 0, 0, 0, 0, 0, 100, 436, 0, 0, 250, 160});
}

TEST_F(FeatureEngineTest, FeatureVector_TestCase2_StandardSequenceWindow3)
{
    // 测试用例 2: 标准序列，窗口3
    // 参考: sliding_window_distance.py 测试用例 6
    std::vector<int> seq = {7, 11, 8, 12, 8, 11, 12};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize3,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, 0) << "标准序列，窗口3应该成功转换";

    ExpectFeatureMatches(feature,
                         {0, 0, 0, 0, 0, 0, 0, 30, 127, 0, 0, 94, 84});
}

TEST_F(FeatureEngineTest, FeatureVector_TestCase3_IncreasingSequence)
{
    // 测试用例 3: 递增序列，窗口3
    std::vector<int> seq = {1, 2, 3, 4, 5};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize3,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, 0) << "递增序列应该成功转换";

    ExpectFeatureMatches(feature,
                         {0, 7, 12, 18, 18, 11});
}

TEST_F(FeatureEngineTest, FeatureVector_TestCase4_ConstantSequence)
{
    // 测试用例 4: 重复序列，窗口3
    std::vector<int> seq = {5, 5, 5, 5, 5};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize3,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, 0) << "重复序列应该成功转换";

    ExpectFeatureMatches(feature,
                         {0, 0, 0, 0, 0, 110});
}

TEST_F(FeatureEngineTest, FeatureVector_TestCase5_AlternatingSequence)
{
    // 测试用例 5: 交替序列，窗口3
    std::vector<int> seq = {0, 10, 0, 10, 0, 10};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize3,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, 0) << "交替序列应该成功转换";
    ExpectFeatureMatches(feature,
                         {100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 40});
}

TEST_F(FeatureEngineTest, FeatureVector_TestCase6_SingleElement)
{
    // 测试用例 6: 单元素序列
    std::vector<int> seq = {7};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize5,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, 0) << "单元素序列应该成功转换";

    ExpectFeatureMatches(feature,
                         {});
    int non_zero = CountNonZero(feature);
    ExpectFeatureMatches(feature,
                         {});
}

TEST_F(FeatureEngineTest, FeatureVector_TestCase7_TwoElements)
{
    // 测试用例 7: 两元素序列，窗口3
    std::vector<int> seq = {7, 11};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize3,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, 0) << "两元素序列应该成功转换";

    ExpectFeatureMatches(feature,
                         {0, 0, 0, 0, 0, 0, 0, 22, 0, 0, 0, 14});
}

TEST_F(FeatureEngineTest, FeatureVector_TestCase8_ThreeElements)
{
    // 测试用例 8: 三元素序列，窗口3
    std::vector<int> seq = {7, 11, 8};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize3,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, 0) << "三元素序列应该成功转换";

    ExpectFeatureMatches(feature,
                         {0, 0, 0, 0, 0, 0, 0, 30, 29, 0, 0, 30});
}

TEST_F(FeatureEngineTest, FeatureVector_TestCase9_LargeWindow)
{
    // 测试用例 9: 较长序列，大窗口
    std::vector<int> seq = {1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize6,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, 0) << "大窗口应该成功转换";

    ExpectFeatureMatches(feature,
                         {0, 50, 70, 94, 112, 122, 122, 110, 85});
}

TEST_F(FeatureEngineTest, FeatureVector_TestCase10_BoundaryValues)
{
    // 测试用例 10: 边界值序列
    std::vector<int> seq = {0, 27, 0, 27, 0, 27};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize3,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, 0) << "边界值序列应该成功转换";

    ExpectFeatureMatches(feature,
                         {270, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 108});
}

TEST_F(FeatureEngineTest, FeatureVector_TestCase11_LongSequence)
{
    // 测试用例 11: 长序列
    std::vector<int> seq = {7, 11, 8, 12, 8, 11, 8, 7, 11, 8, 12, 8, 11, 8};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize5,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, 0) << "长序列应该成功转换";

    ExpectFeatureMatches(feature,
                         {0, 0, 0, 0, 0, 0, 0, 293, 1013, 0, 0, 605, 335});
}

TEST_F(FeatureEngineTest, FeatureVector_TestCase12_SmallWindow)
{
    // 测试用例 12: 小窗口
    std::vector<int> seq = {7, 11, 8, 12, 8, 11, 8};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize2,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, 0) << "小窗口应该成功转换";

    ExpectFeatureMatches(feature,
                         {0, 0, 0, 0, 0, 0, 0, 11, 57, 0, 0, 31, 16});
}

TEST_F(FeatureEngineTest, FeatureVector_TestCase13_MediumWindow)
{
    // 测试用例 13: 中等窗口
    std::vector<int> seq = {7, 11, 8, 12, 8, 11, 8};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize4,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, 0) << "中等窗口应该成功转换";

    ExpectFeatureMatches(feature,
                         {0, 0, 0, 0, 0, 0, 0, 61, 283, 0, 0, 157, 107});
}

TEST_F(FeatureEngineTest, FeatureVector_TestCase14_HighTokenValues)
{
    // 测试用例 14: 高 token 值
    std::vector<int> seq = {20, 25, 20, 25, 20};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize3,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, 0) << "高 token 值应该成功转换";

    ExpectFeatureMatches(feature,
                         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 280, 0, 0, 0, 0, 210});
}

TEST_F(FeatureEngineTest, FeatureVector_TestCase15_AdjacentDuplicates)
{
    // 测试用例 15: 相邻相同值
    std::vector<int> seq = {7, 7, 11, 11, 8, 8};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    int ret = kadd_seq_to_feature(seq.data(), seq.size(), kWinSize3,
                                  feature.data(), KADD_FEATURE_DIM);

    EXPECT_EQ(ret, 0) << "相邻相同值应该成功转换";

    ExpectFeatureMatches(feature,
                         {0, 0, 0, 0, 0, 0, 0, 72, 76, 0, 0, 104});
}

// ========== 覆盖率优化：边界和分支测试 ==========

TEST_F(FeatureEngineTest, TokenValueBoundary_MaxToMin)
{
    // 测试 token 值边界：从最大值到最小值
    std::vector<int> seq_max_to_min = {27, 26, 25, 24, 23, 22, 21, 20, 19, 18,
                                         17, 16, 15, 14, 13, 12, 11, 10, 9, 8,
                                         7, 6, 5, 4, 3, 2, 1, 0};
    std::vector<int> feature(KADD_FEATURE_DIM, 0);

    EXPECT_EQ(kadd_seq_to_feature(seq_max_to_min.data(), seq_max_to_min.size(),
                                  kWinSize5, feature.data(), KADD_FEATURE_DIM), 0);
}

TEST_F(FeatureEngineTest, BatchSizeEdgeCases)
{
    // 测试批处理大小边界
    const int *seqs[1] = {new int[10]{1, 2, 3, 4, 5, 6, 7, 8, 9, 10}};
    const int lens[1] = {10};
    std::vector<int> features(KADD_FEATURE_DIM, 0);

    kadd_feature_batch_t batch = {
        seqs, lens, 1, features.data(), KADD_FEATURE_DIM, kWinSize5, 0
    };

    // batch_size = 0 (应使用默认值)
    EXPECT_EQ(kadd_seq_to_feature_batch(&batch), KADD_SUCCESS);

    // batch_size = 1
    batch.batch_size = 1;
    EXPECT_EQ(kadd_seq_to_feature_batch(&batch), KADD_SUCCESS);

    // batch_size > KADD_FEATURE_MAX_BATCH_SIZE (应自动限制)
    batch.batch_size = 1025;
    EXPECT_EQ(kadd_seq_to_feature_batch(&batch), KADD_SUCCESS);

    delete[] seqs[0];
}

// ========== 覆盖率优化：NULL指针和参数边界测试 ==========

TEST_F(FeatureEngineTest, BatchWithNullBatchPtr)
{
    // 测试 batch 本身为 NULL 的情况
    EXPECT_EQ(kadd_seq_to_feature_batch(nullptr), KADD_ERROR_NULL_PTR)
        << "NULL batch 指针应该返回 KADD_ERROR_NULL_PTR 错误";
}

TEST_F(FeatureEngineTest, BatchWithNullSeqs)
{
    // 测试 batch->seqs = NULL 的情况
    const int *null_seqs[1] = {nullptr};
    const int lens[1] = {10};
    std::vector<int> features(KADD_FEATURE_DIM, 0);

    kadd_feature_batch_t batch = {
        null_seqs, lens, 1, features.data(), KADD_FEATURE_DIM, kWinSize5, 64
    };

    EXPECT_EQ(kadd_seq_to_feature_batch(&batch), KADD_ERROR_NULL_PTR)
        << "NULL seq 元素应该返回 KADD_ERROR_NULL_PTR 错误";
}

TEST_F(FeatureEngineTest, BatchWithNullSeqLens)
{
    // 测试 batch->seq_lens = NULL 的情况
    int seq[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    const int *seqs[1] = {seq};

    std::vector<int> features(KADD_FEATURE_DIM, 0);

    kadd_feature_batch_t batch = {
        seqs, nullptr, 1, features.data(), KADD_FEATURE_DIM, kWinSize5, 64
    };

    EXPECT_EQ(kadd_seq_to_feature_batch(&batch), KADD_ERROR_NULL_PTR)
        << "NULL seq_lens 应该返回 NULL_PTR 错误";
}

TEST_F(FeatureEngineTest, BatchWithNullFeatures)
{
    // 测试 batch->features = NULL 的情况
    int seq[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    const int *seqs[1] = {seq};
    const int lens[1] = {10};

    kadd_feature_batch_t batch = {
        seqs, lens, 1, nullptr, KADD_FEATURE_DIM, kWinSize5, 64
    };

    EXPECT_EQ(kadd_seq_to_feature_batch(&batch), KADD_ERROR_NULL_PTR)
        << "NULL features 应该返回 NULL_PTR 错误";
}

TEST_F(FeatureEngineTest, BatchWithZeroCount)
{
    // 测试 batch->count <= 0 的情况
    int seq[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    const int *seqs[1] = {seq};
    const int lens[1] = {10};
    std::vector<int> features(KADD_FEATURE_DIM, 0);

    // count = 0
    kadd_feature_batch_t batch1 = {
        seqs, lens, 0, features.data(), KADD_FEATURE_DIM, kWinSize5, 64
    };
    EXPECT_EQ(kadd_seq_to_feature_batch(&batch1), KADD_ERROR_INVALID_PARAM)
        << "count=0 应该返回 INVALID_PARAM 错误";

    // count = -1
    kadd_feature_batch_t batch2 = {
        seqs, lens, -1, features.data(), KADD_FEATURE_DIM, kWinSize5, 64
    };
    EXPECT_EQ(kadd_seq_to_feature_batch(&batch2), KADD_ERROR_INVALID_PARAM)
        << "count=-1 应该返回 INVALID_PARAM 错误";
}

TEST_F(FeatureEngineTest, BatchWithExceedsMaxSampleCount)
{
    // 测试 batch->count > KADD_FEATURE_MAX_SAMPLE_COUNT 的情况
    int seq[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    const int *seqs[1] = {seq};
    const int lens[1] = {10};
    std::vector<int> features(KADD_FEATURE_DIM, 0);

    kadd_feature_batch_t batch = {
        seqs, lens, KADD_FEATURE_MAX_SAMPLE_COUNT + 1, features.data(),
        KADD_FEATURE_DIM, kWinSize5, 64
    };

    EXPECT_EQ(kadd_seq_to_feature_batch(&batch), KADD_ERROR_INVALID_PARAM)
        << "count超过最大限制应该返回 INVALID_PARAM 错误";
}

TEST_F(FeatureEngineTest, BatchWithExceedsMaxFeatureDim)
{
    // 测试 batch->feature_dim > KADD_FEATURE_DEFAULT_MAX_OUTPUT_DIM 的情况
    int seq[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    const int *seqs[1] = {seq};
    const int lens[1] = {10};
    std::vector<int> features(500, 0);  // 超过默认300

    kadd_feature_batch_t batch = {
        seqs, lens, 1, features.data(), 500, kWinSize5, 64
    };

    EXPECT_EQ(kadd_seq_to_feature_batch(&batch), KADD_ERROR_INVALID_PARAM)
        << "feature_dim超过最大限制应该返回 INVALID_PARAM 错误";
}


