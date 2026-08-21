/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * @file test_inference_engine_gtest.cpp
 * @brief 统一推理引擎功能测试
 *
 * 测试内容：
 * 1. 功能测试 - RF/CNN 模型加载、推理、批量推理
 * 2. 边界测试 - NULL 参数、无效批次
 * 3. 一致性测试 - 单样本 vs 批量推理
 */

#include <gtest/gtest.h>
#include <vector>
#include <cmath>
#include <chrono>
#include <iostream>
#include <algorithm>
#include "kadd_inference_engine.h"
#include "kadd_config_loader.h"
#include "kadd_errors.h"

// ========== 包含源文件以测试 static 函数 ==========
#include "kadd_inference_engine.c"

// ========== 测试常量 ==========

constexpr float kFloatTolerance = 0.001f;
constexpr int kDefaultFeatureDim = 28;

// ========== 测试夹具 ==========

class InferenceEngineTest : public ::testing::Test {
protected:
    kadd_inference_engine_t *engine_rf = nullptr;
    kadd_config_t config;
    int feature_dim_rf = kDefaultFeatureDim;

    void SetUp() override
    {
        // 尝试多个可能的配置文件路径
        const char* config_paths[] = {
            "config.json",
            "../config.json",
            "../../config.json",
            "../../../config.json"
        };

        bool config_loaded = false;
        for (const char* path : config_paths) {
            if (kadd_config_load(path, &config) == KADD_SUCCESS) {
                config_loaded = true;
                break;
            }
        }

        // 配置加载失败则测试失败
        ASSERT_TRUE(config_loaded) << "无法加载 config.json，请检查配置文件路径";
    }

    void TearDown() override
    {
        if (engine_rf) {
            kadd_inference_engine_free(engine_rf);
            engine_rf = nullptr;
        }
    }

    // 辅助函数：检查概率值是否有效
    bool IsValidProbability(float prob)
    {
        return prob >= -kFloatTolerance && prob <= (1.0f + kFloatTolerance);
    }
};

// ========== 1. 功能测试：模型加载 ==========

TEST_F(InferenceEngineTest, LoadRFModel)
{
    int ret = kadd_inference_engine_load(&engine_rf, "config.json", KADD_INFERENCE_TYPE_RF);
    ASSERT_EQ(ret, KADD_SUCCESS) << "RF 模型加载失败";

    EXPECT_NE(engine_rf, nullptr);

    int dim = kadd_inference_engine_get_feature_dim(engine_rf);
    EXPECT_GT(dim, 0);
    feature_dim_rf = dim;
}

TEST_F(InferenceEngineTest, LoadCNNModel)
{
    kadd_inference_engine_t *engine_cnn = nullptr;
    int ret = kadd_inference_engine_load(&engine_cnn, "config.json", KADD_INFERENCE_TYPE_CNN);
    ASSERT_EQ(ret, KADD_SUCCESS) << "CNN 模型加载失败";

    EXPECT_NE(engine_cnn, nullptr);

    int dim = kadd_inference_engine_get_feature_dim(engine_cnn);
    EXPECT_GT(dim, 0);

    kadd_inference_engine_free(engine_cnn);
}

// ========== 2. 功能测试：单样本推理 ==========

TEST_F(InferenceEngineTest, PredictSingleRF)
{
    int ret = kadd_inference_engine_load(&engine_rf, "config.json", KADD_INFERENCE_TYPE_RF);
    ASSERT_EQ(ret, KADD_SUCCESS) << "RF 模型加载失败";

    feature_dim_rf = kadd_inference_engine_get_feature_dim(engine_rf);
    std::vector<float> features(feature_dim_rf, 0.0f);

    float prob = 0.0f;
    ret = kadd_inference_predict(engine_rf, features.data(), &prob);

    EXPECT_EQ(ret, KADD_SUCCESS);
    EXPECT_TRUE(IsValidProbability(prob));
}

// ========== 3. 功能测试：批量推理 ==========

TEST_F(InferenceEngineTest, PredictBatchRF)
{
    int ret = kadd_inference_engine_load(&engine_rf, "config.json", KADD_INFERENCE_TYPE_RF);
    ASSERT_EQ(ret, KADD_SUCCESS) << "RF 模型加载失败";

    feature_dim_rf = kadd_inference_engine_get_feature_dim(engine_rf);
    const int n = 10;
    std::vector<float> features(n * feature_dim_rf, 0.0f);
    std::vector<float> probs(n, 0.0f);

    ret = kadd_inference_predict_batch(engine_rf, features.data(), n, probs.data(), 2);

    EXPECT_EQ(ret, KADD_SUCCESS);
    for (int i = 0; i < n; i++) {
        EXPECT_TRUE(IsValidProbability(probs[i]));
    }
}

// ========== 4. 边界测试：NULL 参数 ==========

TEST_F(InferenceEngineTest, NullEngine)
{
    // 测试 NULL engine，参数不重要（函数会在检查 engine 时直接返回错误）
    std::vector<float> features(10, 0.0f);  // 任意大小
    float prob = 0.0f;

    int ret = kadd_inference_predict(nullptr, features.data(), &prob);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR);
}

TEST_F(InferenceEngineTest, GetFeatureDimWithNullEngine)
{
    int dim = kadd_inference_engine_get_feature_dim(nullptr);
    EXPECT_EQ(dim, KADD_ERROR_NULL_PTR) << "NULL engine 获取特征维度应该返回 KADD_ERROR_NULL_PTR";
}

TEST_F(InferenceEngineTest, NullFeatures)
{
    int ret = kadd_inference_engine_load(&engine_rf, "config.json", KADD_INFERENCE_TYPE_RF);
    ASSERT_EQ(ret, KADD_SUCCESS) << "RF 模型加载失败";

    float prob = 0.0f;
    ret = kadd_inference_predict(engine_rf, nullptr, &prob);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR);
}

TEST_F(InferenceEngineTest, NullProbability)
{
    int ret = kadd_inference_engine_load(&engine_rf, "config.json", KADD_INFERENCE_TYPE_RF);
    ASSERT_EQ(ret, KADD_SUCCESS) << "RF 模型加载失败";

    feature_dim_rf = kadd_inference_engine_get_feature_dim(engine_rf);
    std::vector<float> features(feature_dim_rf, 0.0f);

    ret = kadd_inference_predict(engine_rf, features.data(), nullptr);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR);
}

// ========== 5. 边界测试：无效批次参数 ==========

TEST_F(InferenceEngineTest, InvalidBatchSize)
{
    int ret = kadd_inference_engine_load(&engine_rf, "config.json", KADD_INFERENCE_TYPE_RF);
    ASSERT_EQ(ret, KADD_SUCCESS) << "RF 模型加载失败";

    feature_dim_rf = kadd_inference_engine_get_feature_dim(engine_rf);
    std::vector<float> features(10 * feature_dim_rf, 0.0f);
    std::vector<float> probs(10, 0.0f);

    // 负批次大小 — 应与 RF native 后端行为一致，返回 INVALID_PARAM
    ret = kadd_inference_predict_batch(engine_rf, features.data(), 10, probs.data(), -1);
    EXPECT_EQ(ret, KADD_ERROR_INVALID_PARAM);

    // 零批次大小 — 应与 RF native 后端行为一致，返回 INVALID_PARAM
    ret = kadd_inference_predict_batch(engine_rf, features.data(), 10, probs.data(), 0);
    EXPECT_EQ(ret, KADD_ERROR_INVALID_PARAM);
}

// ========== 6. 一致性测试 ==========

TEST_F(InferenceEngineTest, ConsistencySingleVsBatch)
{
    int ret = kadd_inference_engine_load(&engine_rf, "config.json", KADD_INFERENCE_TYPE_RF);
    ASSERT_EQ(ret, KADD_SUCCESS) << "RF 模型加载失败";

    feature_dim_rf = kadd_inference_engine_get_feature_dim(engine_rf);
    const int n = 3;

    // 准备特征数据
    std::vector<float> flat_features(n * feature_dim_rf, 0.0f);
    for (int i = 0; i < n * feature_dim_rf; i++) {
        flat_features[i] = static_cast<float>(i % 100);
    }

    // 单样本推理
    std::vector<float> single_probs(n);
    for (int i = 0; i < n; i++) {
        kadd_inference_predict(engine_rf, flat_features.data() + i * feature_dim_rf, &single_probs[i]);
    }

    // 批量推理
    std::vector<float> batch_probs(n);
    kadd_inference_predict_batch(engine_rf, flat_features.data(), n, batch_probs.data(), 2);

    // 对比结果
    for (int i = 0; i < n; i++) {
        float diff = std::fabs(single_probs[i] - batch_probs[i]);
        EXPECT_LT(diff, 1e-4f) << "单样本和批量推理结果不一致，索引: " << i;
    }
}

// ========== 7. 异常测试：配置文件错误 ==========

TEST_F(InferenceEngineTest, InvalidConfigPath)
{
    kadd_inference_engine_t *engine = nullptr;
    int ret = kadd_inference_engine_load(&engine, "nonexistent_config.json", KADD_INFERENCE_TYPE_RF);

    EXPECT_NE(ret, KADD_SUCCESS);
    EXPECT_EQ(engine, nullptr);
}

TEST_F(InferenceEngineTest, NullConfigPath)
{
    kadd_inference_engine_t *engine = nullptr;
    int ret = kadd_inference_engine_load(&engine, nullptr, KADD_INFERENCE_TYPE_RF);

    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR);
    EXPECT_EQ(engine, nullptr);
}

// ========== 8. 类型边界测试 ==========

TEST_F(InferenceEngineTest, InvalidType_Negative)
{
    kadd_inference_engine_t *engine = nullptr;
    int ret = kadd_inference_engine_load(&engine, "config.json",
                                          static_cast<kadd_inference_type_t>(-1));
    EXPECT_EQ(ret, KADD_ERROR_INVALID_PARAM);
    EXPECT_EQ(engine, nullptr);
}

TEST_F(InferenceEngineTest, InvalidType_OutOfRange)
{
    kadd_inference_engine_t *engine = nullptr;
    int ret = kadd_inference_engine_load(&engine, "config.json",
                                          static_cast<kadd_inference_type_t>(100));
    EXPECT_EQ(ret, KADD_ERROR_INVALID_PARAM);
    EXPECT_EQ(engine, nullptr);
}

// ========== 9. 样本数边界测试 ==========

TEST_F(InferenceEngineTest, SampleCount_ExceedsMax)
{
    int ret = kadd_inference_engine_load(&engine_rf, "config.json", KADD_INFERENCE_TYPE_RF);
    ASSERT_EQ(ret, KADD_SUCCESS);

    feature_dim_rf = kadd_inference_engine_get_feature_dim(engine_rf);
    std::vector<float> features(10 * feature_dim_rf, 0.0f);
    std::vector<float> probs(10);

    // n > KADD_INFERENCE_MAX_SAMPLE_COUNT (10,000,000)
    ret = kadd_inference_predict_batch(engine_rf, features.data(),
                                        10000001, probs.data(), 64);
    EXPECT_EQ(ret, KADD_ERROR_INVALID_PARAM);
}

TEST_F(InferenceEngineTest, SampleCount_Zero)
{
    int ret = kadd_inference_engine_load(&engine_rf, "config.json", KADD_INFERENCE_TYPE_RF);
    ASSERT_EQ(ret, KADD_SUCCESS);

    feature_dim_rf = kadd_inference_engine_get_feature_dim(engine_rf);
    std::vector<float> features(feature_dim_rf, 0.0f);
    std::vector<float> probs(1);

    ret = kadd_inference_predict_batch(engine_rf, features.data(), 0, probs.data(), 1);
    EXPECT_EQ(ret, KADD_ERROR_INVALID_PARAM);
}

// ========== 10. 多实例并发测试 ==========

TEST_F(InferenceEngineTest, MultipleEnginesConcurrent)
{
    kadd_inference_engine_t *engine_rf_local = nullptr;
    kadd_inference_engine_t *engine_cnn = nullptr;

    // 同时加载两种模型
    int ret1 = kadd_inference_engine_load(&engine_rf_local, "config.json", KADD_INFERENCE_TYPE_RF);
    int ret3 = kadd_inference_engine_load(&engine_cnn, "config.json", KADD_INFERENCE_TYPE_CNN);

    ASSERT_EQ(ret1, KADD_SUCCESS);
    ASSERT_EQ(ret3, KADD_SUCCESS);

    // 获取各模型维度
    int dim_rf = kadd_inference_engine_get_feature_dim(engine_rf_local);
    int dim_cnn = kadd_inference_engine_get_feature_dim(engine_cnn);

    EXPECT_GT(dim_rf, 0);
    EXPECT_GT(dim_cnn, 0);

    // 同时推理
    std::vector<float> features_rf(dim_rf, 1.0f);
    std::vector<float> features_cnn(dim_cnn, 1.0f);

    float prob_rf = 0.0f, prob_cnn = 0.0f;

    EXPECT_EQ(kadd_inference_predict(engine_rf_local, features_rf.data(), &prob_rf), KADD_SUCCESS);
    EXPECT_EQ(kadd_inference_predict(engine_cnn, features_cnn.data(), &prob_cnn), KADD_SUCCESS);

    EXPECT_TRUE(IsValidProbability(prob_rf));
    EXPECT_TRUE(IsValidProbability(prob_cnn));

    // 释放
    kadd_inference_engine_free(engine_rf_local);
    kadd_inference_engine_free(engine_cnn);
}

// ========== 11. 内存压力测试 ==========

TEST_F(InferenceEngineTest, MemoryStress_LargeBatch)
{
    int ret = kadd_inference_engine_load(&engine_rf, "config.json", KADD_INFERENCE_TYPE_RF);
    ASSERT_EQ(ret, KADD_SUCCESS);

    feature_dim_rf = kadd_inference_engine_get_feature_dim(engine_rf);
    const int n = 1000;  // 大批量
    std::vector<float> features(n * feature_dim_rf, 0.0f);
    std::vector<float> probs(n);

    ret = kadd_inference_predict_batch(engine_rf, features.data(), n, probs.data(), 128);

    EXPECT_EQ(ret, KADD_SUCCESS);
    for (int i = 0; i < n; i++) {
        EXPECT_TRUE(IsValidProbability(probs[i]));
    }
}

TEST_F(InferenceEngineTest, MemoryStress_RepeatedLoadFree)
{
    // 重复加载/释放，检测内存泄漏
    for (int i = 0; i < 10; i++) {
        kadd_inference_engine_t *engine = nullptr;
        int ret = kadd_inference_engine_load(&engine, "config.json", KADD_INFERENCE_TYPE_RF);
        ASSERT_EQ(ret, KADD_SUCCESS);

        kadd_inference_engine_free(engine);
    }
    SUCCEED() << "重复加载/释放未崩溃";
}

// ========== 12. 批处理边界测试 ==========

TEST_F(InferenceEngineTest, BatchBoundary_ExactMultiple)
{
    int ret = kadd_inference_engine_load(&engine_rf, "config.json", KADD_INFERENCE_TYPE_RF);
    ASSERT_EQ(ret, KADD_SUCCESS);

    feature_dim_rf = kadd_inference_engine_get_feature_dim(engine_rf);
    const int batch_size = 64;
    const int n = batch_size * 2;  // 刚好是批次大小的倍数

    std::vector<float> features(n * feature_dim_rf, 0.0f);
    std::vector<float> probs(n);

    ret = kadd_inference_predict_batch(engine_rf, features.data(), n, probs.data(), batch_size);

    EXPECT_EQ(ret, KADD_SUCCESS);
    for (int i = 0; i < n; i++) {
        EXPECT_TRUE(IsValidProbability(probs[i]));
    }
}

TEST_F(InferenceEngineTest, BatchBoundary_NotMultiple)
{
    int ret = kadd_inference_engine_load(&engine_rf, "config.json", KADD_INFERENCE_TYPE_RF);
    ASSERT_EQ(ret, KADD_SUCCESS);

    feature_dim_rf = kadd_inference_engine_get_feature_dim(engine_rf);
    const int batch_size = 64;
    const int n = 100;  // 不是批次大小的倍数

    std::vector<float> features(n * feature_dim_rf, 0.0f);
    std::vector<float> probs(n);

    ret = kadd_inference_predict_batch(engine_rf, features.data(), n, probs.data(), batch_size);

    EXPECT_EQ(ret, KADD_SUCCESS);
    for (int i = 0; i < n; i++) {
        EXPECT_TRUE(IsValidProbability(probs[i]));
    }
}

TEST_F(InferenceEngineTest, BatchBoundary_MaxSize)
{
    int ret = kadd_inference_engine_load(&engine_rf, "config.json", KADD_INFERENCE_TYPE_RF);
    ASSERT_EQ(ret, KADD_SUCCESS);

    feature_dim_rf = kadd_inference_engine_get_feature_dim(engine_rf);
    const int n = KADD_INFERENCE_MAX_BATCH_SIZE;  // 1024

    std::vector<float> features(n * feature_dim_rf, 0.0f);
    std::vector<float> probs(n);

    ret = kadd_inference_predict_batch(engine_rf, features.data(), n, probs.data(), n);

    EXPECT_EQ(ret, KADD_SUCCESS);
}

// ========== 13. 批处理 NULL 参数测试 ==========

TEST_F(InferenceEngineTest, BatchNullHandle)
{
    // 测试 NULL engine，参数不重要（函数会在检查 engine 时直接返回错误）
    std::vector<float> features(10, 0.0f);  // 任意大小
    std::vector<float> probs(10);

    int ret = kadd_inference_predict_batch(nullptr, features.data(), 10, probs.data(), 2);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR);
}

TEST_F(InferenceEngineTest, BatchNullFeatures)
{
    int ret = kadd_inference_engine_load(&engine_rf, "config.json", KADD_INFERENCE_TYPE_RF);
    ASSERT_EQ(ret, KADD_SUCCESS);

    feature_dim_rf = kadd_inference_engine_get_feature_dim(engine_rf);
    std::vector<float> probs(10);

    ret = kadd_inference_predict_batch(engine_rf, nullptr, 10, probs.data(), 2);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR);
}

TEST_F(InferenceEngineTest, BatchNullProbabilities)
{
    int ret = kadd_inference_engine_load(&engine_rf, "config.json", KADD_INFERENCE_TYPE_RF);
    ASSERT_EQ(ret, KADD_SUCCESS);

    feature_dim_rf = kadd_inference_engine_get_feature_dim(engine_rf);
    std::vector<float> features(10 * feature_dim_rf, 0.0f);

    ret = kadd_inference_predict_batch(engine_rf, features.data(), 10, nullptr, 2);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR);
}

// ========== 14. 性能测试 ==========

TEST_F(InferenceEngineTest, Performance_SinglePrediction_RF)
{
    int ret = kadd_inference_engine_load(&engine_rf, "config.json", KADD_INFERENCE_TYPE_RF);
    ASSERT_EQ(ret, KADD_SUCCESS);

    feature_dim_rf = kadd_inference_engine_get_feature_dim(engine_rf);
    // 使用非零数据（递增序列）
    std::vector<float> features(feature_dim_rf);
    for (int i = 0; i < feature_dim_rf; i++) {
        features[i] = static_cast<float>(i % 100 + 1);
    }

    const int iterations = 1000;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; i++) {
        float probability = 0.0f;
        kadd_inference_predict(engine_rf, features.data(), &probability);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    double avg_time_us = static_cast<double>(duration.count()) / iterations;
    double ops_per_sec = 1000000.0 / avg_time_us;

    std::cout << "\n[性能测试-RF] 单样本推理平均耗时: " << avg_time_us << " μs" << std::endl;
    std::cout << "[性能测试-RF] 单样本推理吞吐量: " << static_cast<int>(ops_per_sec) << " ops/sec" << std::endl;

    EXPECT_GT(ops_per_sec, 1000.0) << "RF 单样本推理性能应该 > 1,000 ops/sec";
}

TEST_F(InferenceEngineTest, Performance_SinglePrediction_CNN)
{
    kadd_inference_engine_t *engine_cnn = nullptr;
    int ret = kadd_inference_engine_load(&engine_cnn, "config.json", KADD_INFERENCE_TYPE_CNN);
    ASSERT_EQ(ret, KADD_SUCCESS);

    int dim_cnn = kadd_inference_engine_get_feature_dim(engine_cnn);
    // CNN 模型需要嵌入索引，使用 0-28 范围内的值
    std::vector<float> features(dim_cnn);
    for (int i = 0; i < dim_cnn; i++) {
        features[i] = static_cast<float>(i % 29);  // 0-28
    }

    const int iterations = 1000;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; i++) {
        float probability = 0.0f;
        kadd_inference_predict(engine_cnn, features.data(), &probability);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    double avg_time_us = static_cast<double>(duration.count()) / iterations;
    double ops_per_sec = 1000000.0 / avg_time_us;

    std::cout << "\n[性能测试-CNN] 单样本推理平均耗时: " << avg_time_us << " μs" << std::endl;
    std::cout << "[性能测试-CNN] 单样本推理吞吐量: " << static_cast<int>(ops_per_sec) << " ops/sec" << std::endl;

    EXPECT_GT(ops_per_sec, 1000.0) << "CNN 单样本推理性能应该 > 1,000 ops/sec";
    kadd_inference_engine_free(engine_cnn);
}

TEST_F(InferenceEngineTest, Performance_BatchPrediction_RF)
{
    int ret = kadd_inference_engine_load(&engine_rf, "config.json", KADD_INFERENCE_TYPE_RF);
    ASSERT_EQ(ret, KADD_SUCCESS);

    feature_dim_rf = kadd_inference_engine_get_feature_dim(engine_rf);

    const int batch_size = 64;
    const int iterations = 100;
    const int total_samples = batch_size * iterations;

    // 使用非零数据填充
    std::vector<float> features(feature_dim_rf * batch_size);
    for (size_t i = 0; i < features.size(); i++) {
        features[i] = static_cast<float>((i % 100) + 1);
    }
    std::vector<float> probabilities(batch_size);

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; i++) {
        kadd_inference_predict_batch(engine_rf, features.data(), batch_size, probabilities.data(), batch_size);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    double avg_time_us = static_cast<double>(duration.count()) / iterations;
    double samples_per_sec = (total_samples * 1000000.0) / duration.count();

    std::cout << "\n[性能测试-RF] 批量推理(batch=" << batch_size << ")平均耗时: " << avg_time_us << " μs" << std::endl;
    std::cout << "[性能测试-RF] 批量推理吞吐量: " << static_cast<int>(samples_per_sec) << " samples/sec" << std::endl;

    EXPECT_GT(samples_per_sec, 10000.0) << "RF 批量推理性能应该 > 10,000 samples/sec";
}

TEST_F(InferenceEngineTest, Performance_BatchPrediction_CNN)
{
    kadd_inference_engine_t *engine_cnn = nullptr;
    int ret = kadd_inference_engine_load(&engine_cnn, "config.json", KADD_INFERENCE_TYPE_CNN);
    ASSERT_EQ(ret, KADD_SUCCESS);

    int dim_cnn = kadd_inference_engine_get_feature_dim(engine_cnn);

    const int batch_size = 64;
    const int iterations = 100;
    const int total_samples = batch_size * iterations;

    // CNN 模型需要嵌入索引，使用 0-28 范围内的值
    std::vector<float> features(dim_cnn * batch_size);
    for (size_t i = 0; i < features.size(); i++) {
        features[i] = static_cast<float>(i % 29);  // 0-28
    }
    std::vector<float> probabilities(batch_size);

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; i++) {
        kadd_inference_predict_batch(engine_cnn, features.data(), batch_size, probabilities.data(), batch_size);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    double avg_time_us = static_cast<double>(duration.count()) / iterations;
    double samples_per_sec = (total_samples * 1000000.0) / duration.count();

    std::cout << "\n[性能测试-CNN] 批量推理(batch=" << batch_size << ")平均耗时: " << avg_time_us << " μs" << std::endl;
    std::cout << "[性能测试-CNN] 批量推理吞吐量: " << static_cast<int>(samples_per_sec) << " samples/sec" << std::endl;

    EXPECT_GT(samples_per_sec, 5000.0) << "CNN 批量推理性能应该 > 5,000 samples/sec";
    kadd_inference_engine_free(engine_cnn);
}

TEST_F(InferenceEngineTest, Performance_ModelLoading_RF)
{
    const int iterations = 10;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; i++) {
        kadd_inference_engine_t *temp_engine = nullptr;
        kadd_inference_engine_load(&temp_engine, "config.json", KADD_INFERENCE_TYPE_RF);
        kadd_inference_engine_free(temp_engine);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    double avg_time_ms = static_cast<double>(duration.count()) / iterations;

    std::cout << "\n[性能测试-RF] 模型加载平均耗时: " << avg_time_ms << " ms" << std::endl;

    EXPECT_LT(avg_time_ms, 1000.0) << "RF 模型加载时间应该 < 1 秒";
}

TEST_F(InferenceEngineTest, Performance_ModelLoading_CNN)
{
    const int iterations = 10;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; i++) {
        kadd_inference_engine_t *temp_engine = nullptr;
        kadd_inference_engine_load(&temp_engine, "config.json", KADD_INFERENCE_TYPE_CNN);
        kadd_inference_engine_free(temp_engine);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    double avg_time_ms = static_cast<double>(duration.count()) / iterations;

    std::cout << "\n[性能测试-CNN] 模型加载平均耗时: " << avg_time_ms << " ms" << std::endl;

    EXPECT_LT(avg_time_ms, 1000.0) << "CNN 模型加载时间应该 < 1 秒";
}

TEST_F(InferenceEngineTest, Performance_Scalability_RF)
{
    int ret = kadd_inference_engine_load(&engine_rf, "config.json", KADD_INFERENCE_TYPE_RF);
    ASSERT_EQ(ret, KADD_SUCCESS);

    feature_dim_rf = kadd_inference_engine_get_feature_dim(engine_rf);

    std::cout << "\n[性能测试-RF] 批量大小扩展性测试:" << std::endl;

    std::vector<int> batch_sizes = {1, 10, 32, 64, 128, 256};

    for (int batch_size : batch_sizes) {
        // 使用非零数据填充
        std::vector<float> features(feature_dim_rf * batch_size);
        for (size_t i = 0; i < features.size(); i++) {
            features[i] = static_cast<float>((i % 100) + 1);
        }
        std::vector<float> probabilities(batch_size);

        const int iterations = std::max(10, 1000 / batch_size);
        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < iterations; i++) {
            kadd_inference_predict_batch(engine_rf, features.data(), batch_size, probabilities.data(), batch_size);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        double avg_time_us = static_cast<double>(duration.count()) / iterations;
        double samples_per_sec = (batch_size * 1000000.0) / avg_time_us;

        std::cout << "  batch_size=" << batch_size
                  << ": 耗时=" << avg_time_us << " μs"
                  << ", 吞吐量=" << static_cast<int>(samples_per_sec) << " samples/sec" << std::endl;
    }

    SUCCEED();
}

// ========== static 函数测试 ==========

/**
 * check_ort_status 函数测试 - 直接测试 static 函数
 * 测试 NULL status 的成功路径（应返回 0）
 */
TEST(InferenceEngineStaticTest, CheckOrtStatus_NullStatus)
{
    kadd_inference_engine_t *engine = nullptr;
    int ret = kadd_inference_engine_load(&engine, "config.json", KADD_INFERENCE_TYPE_RF);
    ASSERT_EQ(ret, KADD_SUCCESS) << "引擎加载失败，无法测试 static 函数";
    ASSERT_NE(engine, nullptr);

    // 直接测试 check_ort_status static 函数
    // 当 status 为 NULL 时，应返回 0（成功）
    int result = check_ort_status(engine->api, nullptr, "test message");
    EXPECT_EQ(result, 0);

    kadd_inference_engine_free(engine);
}

/**
 * init_ort_api 函数测试 - NULL api_base 场景
 * 注意：由于 OrtGetApiBase 是 ONNX Runtime 提供的全局函数，无法直接 mock
 * 此测试验证已初始化 API 的正常路径
 */
TEST(InferenceEngineStaticTest, InitOrtApi_SuccessPath)
{
    kadd_inference_engine_t *engine = nullptr;
    int ret = kadd_inference_engine_load(&engine, "config.json", KADD_INFERENCE_TYPE_RF);
    ASSERT_EQ(ret, KADD_SUCCESS) << "引擎加载失败，无法测试 static 函数";
    ASSERT_NE(engine, nullptr);
    ASSERT_NE(engine->api, nullptr);

    // 验证 API 结构完整性
    EXPECT_NE(engine->api->CreateEnv, nullptr);
    EXPECT_NE(engine->api->CreateSession, nullptr);
    EXPECT_NE(engine->api->Run, nullptr);

    kadd_inference_engine_free(engine);
}

/**
 * init_ort_env 函数测试 - 环境指针验证
 */
TEST(InferenceEngineStaticTest, InitOrtEnv_Success)
{
    kadd_inference_engine_t *engine = nullptr;
    int ret = kadd_inference_engine_load(&engine, "config.json", KADD_INFERENCE_TYPE_RF);
    ASSERT_EQ(ret, KADD_SUCCESS) << "引擎加载失败，无法测试 static 函数";
    ASSERT_NE(engine, nullptr);

    // 验证环境指针有效性
    EXPECT_NE(engine->env, nullptr);

    kadd_inference_engine_free(engine);
}

// ========== ONNX API 错误路径测试 ==========

/**
 * 测试 kadd_inference_engine_get_feature_dim 的错误返回值
 * 确保返回 KADD_ERROR_NULL_PTR 而非 -1
 */
TEST(InferenceEngineStaticTest, GetFeatureDim_NullEngine)
{
    int dim = kadd_inference_engine_get_feature_dim(nullptr);
    EXPECT_EQ(dim, KADD_ERROR_NULL_PTR) << "NULL engine 应返回 KADD_ERROR_NULL_PTR";
}

/**
 * 测试 kadd_inference_predict 的错误返回值
 */
TEST(InferenceEngineStaticTest, Predict_NullEngine)
{
    float features[10] = {0};
    float prob = 0.0f;
    int ret = kadd_inference_predict(nullptr, features, &prob);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR);
}

TEST(InferenceEngineStaticTest, Predict_NullFeatures)
{
    kadd_inference_engine_t *engine = nullptr;
    int ret = kadd_inference_engine_load(&engine, "config.json", KADD_INFERENCE_TYPE_RF);
    ASSERT_EQ(ret, KADD_SUCCESS);

    float prob = 0.0f;
    ret = kadd_inference_predict(engine, nullptr, &prob);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR);

    kadd_inference_engine_free(engine);
}

TEST(InferenceEngineStaticTest, Predict_NullProbability)
{
    kadd_inference_engine_t *engine = nullptr;
    int ret = kadd_inference_engine_load(&engine, "config.json", KADD_INFERENCE_TYPE_RF);
    ASSERT_EQ(ret, KADD_SUCCESS);

    int dim = kadd_inference_engine_get_feature_dim(engine);
    std::vector<float> features(dim, 0.0f);
    ret = kadd_inference_predict(engine, features.data(), nullptr);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR);

    kadd_inference_engine_free(engine);
}

/**
 * 测试 kadd_inference_predict_batch 的错误返回值
 */
TEST(InferenceEngineStaticTest, PredictBatch_NullEngine)
{
    float features[10] = {0};
    float probs[10] = {0};
    int ret = kadd_inference_predict_batch(nullptr, features, 1, probs, 1);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR);
}

TEST(InferenceEngineStaticTest, PredictBatch_InvalidSampleCount)
{
    kadd_inference_engine_t *engine = nullptr;
    int ret = kadd_inference_engine_load(&engine, "config.json", KADD_INFERENCE_TYPE_RF);
    ASSERT_EQ(ret, KADD_SUCCESS);

    int dim = kadd_inference_engine_get_feature_dim(engine);
    std::vector<float> features(dim, 0.0f);
    std::vector<float> probs(1);

    // n <= 0
    ret = kadd_inference_predict_batch(engine, features.data(), 0, probs.data(), 1);
    EXPECT_EQ(ret, KADD_ERROR_INVALID_PARAM);

    // n > MAX_SAMPLE_COUNT
    ret = kadd_inference_predict_batch(engine, features.data(), KADD_INFERENCE_MAX_SAMPLE_COUNT + 1, probs.data(), 1);
    EXPECT_EQ(ret, KADD_ERROR_INVALID_PARAM);

    kadd_inference_engine_free(engine);
}

/**
 * 测试 create_ort_session 正常路径（通过完整加载验证）
 */
TEST(InferenceEngineStaticTest, CreateOrtSession_Success)
{
    kadd_inference_engine_t *engine = nullptr;
    int ret = kadd_inference_engine_load(&engine, "config.json", KADD_INFERENCE_TYPE_RF);
    ASSERT_EQ(ret, KADD_SUCCESS);
    ASSERT_NE(engine, nullptr);

    // 验证 session 和 memory_info 已创建
    EXPECT_NE(engine->session, nullptr);
    EXPECT_NE(engine->memory_info, nullptr);

    kadd_inference_engine_free(engine);
}

/**
 * 测试 release_engine_resources 直接调用
 * 构造部分初始化的 engine，验证清理逻辑
 */
TEST(InferenceEngineStaticTest, ReleaseEngineResources_DirectCall)
{
    // 情况1: NULL 指针 - 不应崩溃
    release_engine_resources(nullptr);
    SUCCEED() << "release_engine_resources(nullptr) 未崩溃";

    // 情况2: 零初始化的 engine - 不应崩溃
    kadd_inference_engine_t *e = (kadd_inference_engine_t *)calloc(1, sizeof(*e));
    ASSERT_NE(e, nullptr);
    release_engine_resources(e);
    free(e);
    SUCCEED() << "release_engine_resources(零初始化) 未崩溃";

    // 情况3: 部分初始化的 engine（只有 input_name）
    e = (kadd_inference_engine_t *)calloc(1, sizeof(*e));
    e->input_name = strdup("test_input");
    release_engine_resources(e);
    free(e);
    SUCCEED() << "release_engine_resources(部分初始化) 未崩溃";

    // 情况4: 部分初始化的 engine（有 input_name 和 output_name）
    e = (kadd_inference_engine_t *)calloc(1, sizeof(*e));
    e->input_name = strdup("test_input");
    e->output_name = strdup("test_output");
    release_engine_resources(e);
    free(e);
    SUCCEED() << "release_engine_resources(完整IO名称) 未崩溃";
}

// ========== 从 ml_engine_test 迁移的 static 函数测试 ==========

/**
 * check_ort_status 函数测试 - 非 NULL status 的错误路径
 * 从 ml_engine_test 迁移：原测试 check_ort_status_simple 的非 null 场景
 * 由于无法直接构造 OrtStatus*，通过故意传入错误模型路径触发 ORT 返回 error status，
 * 间接验证 check_ort_status 的错误处理路径（打印错误 + 返回 -1）。
 */
TEST(InferenceEngineStaticTest, CheckOrtStatus_NonNullStatus_ErrorPath)
{
    kadd_inference_engine_t *engine = nullptr;
    // 加载一个不存在的模型 → create_ort_session 内部 check_ort_status 会被触发
    int ret = kadd_inference_engine_load_from_path(&engine, "/nonexistent_model.onnx");
    EXPECT_NE(ret, KADD_SUCCESS) << "不存在的模型应加载失败";
    EXPECT_EQ(engine, nullptr) << "失败时 engine 应为 NULL";
}

/**
 * get_io_name / detect_io_names 函数测试 - 通过成功加载间接验证
 * 从 ml_engine_test 迁移：原测试 is_name_matched 的 IO 名称匹配场景
 * inference_engine 使用简化的 get_io_name (取第0个IO名)，
 * 通过成功加载验证 IO 名称检测正常工作。
 */
TEST(InferenceEngineStaticTest, DetectIoNames_SuccessPath)
{
    kadd_inference_engine_t *engine = nullptr;
    int ret = kadd_inference_engine_load(&engine, "config.json", KADD_INFERENCE_TYPE_RF);
    ASSERT_EQ(ret, KADD_SUCCESS) << "引擎加载失败";
    ASSERT_NE(engine, nullptr);

    // 验证 IO 名称已被正确检测（非 NULL 且非空）
    EXPECT_NE(engine->input_name, nullptr) << "input_name 应已检测";
    EXPECT_NE(engine->output_name, nullptr) << "output_name 应已检测";
    if (engine->input_name) {
        EXPECT_GT(strlen(engine->input_name), 0U) << "input_name 不应为空";
    }
    if (engine->output_name) {
        EXPECT_GT(strlen(engine->output_name), 0U) << "output_name 不应为空";
    }

    kadd_inference_engine_free(engine);
}

/**
 * get_io_name 函数测试 - CNN 模型类型的 IO 名称检测
 * 验证不同模型类型下 IO 名称检测均正常工作
 */
TEST(InferenceEngineStaticTest, DetectIoNames_CnnModel)
{
    kadd_inference_engine_t *engine = nullptr;
    int ret = kadd_inference_engine_load(&engine, "config.json", KADD_INFERENCE_TYPE_CNN);
    ASSERT_EQ(ret, KADD_SUCCESS) << "CNN 引擎加载失败";
    ASSERT_NE(engine, nullptr);

    EXPECT_NE(engine->input_name, nullptr);
    EXPECT_NE(engine->output_name, nullptr);

    kadd_inference_engine_free(engine);
}

// ========== 主函数 ==========

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}