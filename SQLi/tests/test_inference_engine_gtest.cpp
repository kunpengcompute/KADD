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
#include <string>
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
/* ORT 故障注入用例（覆盖 check_ort_status / get_model_*_dim 的错误清理分支）。
 * 必须紧跟在 kadd_inference_engine.c 之后包含：它需要访问内部结构与 static 函数。 */
#include "kadd_inference_fault_inject.h"

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

    // 负批次大小 — 应回退到默认值，返回成功
    ret = kadd_inference_predict_batch(engine_rf, features.data(), 10, probs.data(), -1);
    EXPECT_EQ(ret, KADD_SUCCESS);

    // 零批次大小 — 应回退到默认值，返回成功
    ret = kadd_inference_predict_batch(engine_rf, features.data(), 10, probs.data(), 0);
    EXPECT_EQ(ret, KADD_SUCCESS);
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

    /* CNN 批量推理吞吐强依赖 ORT 的 intra_op 线程数：本引擎模型很小（batch<=64、
     * feature_dim=300），ORT 默认按物理核数开线程时，派发/同步开销远超算子计算本身，
     * 实测吞吐会掉到约 1.6k samples/sec。create_ort_session() 已显式设置 intra=8/inter=1
     * （可用 KADD_ORT_INTRA_OP_THREADS / KADD_ORT_INTER_OP_THREADS 覆盖），
     * 修复后 ONNX Runtime 1.28.0 下实测 13.6k-17.7k samples/sec。阈值取 5,000，
     * 在覆盖率插桩等较慢环境下仍有约 2.7 倍余量，用于捕获线程配置退化导致的性能崩塌。 */
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

TEST(InferenceEngineStaticTest, TensorDimensionCountLimit)
{
    EXPECT_FALSE(tensor_dim_count_is_supported(0U));
    EXPECT_FALSE(tensor_dim_count_is_supported(1U));
    EXPECT_TRUE(tensor_dim_count_is_supported(2U));
    EXPECT_TRUE(tensor_dim_count_is_supported(MAX_TENSOR_DIMS));
    EXPECT_FALSE(tensor_dim_count_is_supported(MAX_TENSOR_DIMS + 1U));
}

TEST(InferenceEngineStaticTest, OnnxRuntimeApiVersionMeetsMinimum)
{
    EXPECT_GE(ORT_API_VERSION, ORT_MIN_API_VERSION);
    EXPECT_GE(ORT_MIN_API_VERSION, 28);
}

namespace {
/* ORT 的 OrtApi 成员在 C++ 下由 ORT_API2/NO_EXCEPTION 声明为 noexcept，
 * 桩函数必须同样带 noexcept，否则赋值给 api.XXX 会报 invalid conversion [-fpermissive]。
 * 反之 noexcept 桩函数赋给旧版无 noexcept 的成员是允许的隐式转换。 */
int g_fake_get_dimensions_calls = 0;

OrtStatus *FakeSessionGetInputTypeInfo(const OrtSession *, size_t,
                                        OrtTypeInfo **type_info) noexcept
{
    *type_info = reinterpret_cast<OrtTypeInfo *>(0x1);
    return nullptr;
}

OrtStatus *FakeSessionGetOutputTypeInfo(const OrtSession *, size_t,
                                         OrtTypeInfo **type_info) noexcept
{
    *type_info = reinterpret_cast<OrtTypeInfo *>(0x1);
    return nullptr;
}

OrtStatus *FakeCastTypeInfoToTensorInfo(const OrtTypeInfo *,
                                         const OrtTensorTypeAndShapeInfo **tensor_info) noexcept
{
    *tensor_info = reinterpret_cast<const OrtTensorTypeAndShapeInfo *>(0x2);
    return nullptr;
}

OrtStatus *FakeGetTooManyDimensionsCount(const OrtTensorTypeAndShapeInfo *,
                                          size_t *dim_count) noexcept
{
    *dim_count = MAX_TENSOR_DIMS + 1U;
    return nullptr;
}

OrtStatus *FakeGetDimensions(const OrtTensorTypeAndShapeInfo *,
                             int64_t *dims, size_t dim_count) noexcept
{
    ++g_fake_get_dimensions_calls;
    for (size_t i = 0; i < dim_count; ++i) {
        dims[i] = static_cast<int64_t>(i);
    }
    return nullptr;
}

void FakeReleaseTypeInfo(OrtTypeInfo *) noexcept {}
}  // namespace

TEST(InferenceEngineStaticTest, GetModelInputDimRejectsTooManyDimensions)
{
    g_fake_get_dimensions_calls = 0;
    OrtApi api = {};
    api.SessionGetInputTypeInfo = FakeSessionGetInputTypeInfo;
    api.CastTypeInfoToTensorInfo = FakeCastTypeInfoToTensorInfo;
    api.GetDimensionsCount = FakeGetTooManyDimensionsCount;
    api.GetDimensions = FakeGetDimensions;
    api.ReleaseTypeInfo = FakeReleaseTypeInfo;

    kadd_inference_engine_t engine = {};
    engine.api = &api;
    engine.session = reinterpret_cast<OrtSession *>(0x1);

    EXPECT_EQ(get_model_input_dim(&engine), -1);
    EXPECT_EQ(g_fake_get_dimensions_calls, 0)
        << "维度数超过栈数组容量时不得调用 GetDimensions";
}

TEST(InferenceEngineStaticTest, GetModelOutputDimSkipsTooManyDimensions)
{
    g_fake_get_dimensions_calls = 0;
    OrtApi api = {};
    api.SessionGetOutputTypeInfo = FakeSessionGetOutputTypeInfo;
    api.CastTypeInfoToTensorInfo = FakeCastTypeInfoToTensorInfo;
    api.GetDimensionsCount = FakeGetTooManyDimensionsCount;
    api.GetDimensions = FakeGetDimensions;
    api.ReleaseTypeInfo = FakeReleaseTypeInfo;

    kadd_inference_engine_t engine = {};
    engine.api = &api;
    engine.session = reinterpret_cast<OrtSession *>(0x1);

    get_model_output_dim(&engine);
    EXPECT_EQ(engine.output_dim, 2);
    EXPECT_EQ(g_fake_get_dimensions_calls, 0)
        << "维度数超过栈数组容量时不得调用 GetDimensions";
}

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

// ========== 17. 覆盖率优化：非有限特征值与路径加载测试 ==========

TEST_F(InferenceEngineTest, PredictWithNaNFeature)
{
    int ret = kadd_inference_engine_load(&engine_rf, "config.json", KADD_INFERENCE_TYPE_RF);
    ASSERT_EQ(ret, KADD_SUCCESS) << "RF 模型加载失败";

    feature_dim_rf = kadd_inference_engine_get_feature_dim(engine_rf);
    std::vector<float> features(feature_dim_rf, 0.5f);
    features[0] = NAN;

    float prob = 0.0f;
    ret = kadd_inference_predict(engine_rf, features.data(), &prob);
    EXPECT_EQ(ret, KADD_ERROR_INVALID_PARAM) << "NaN 特征应该返回 INVALID_PARAM";
}

TEST_F(InferenceEngineTest, PredictWithInfFeature)
{
    int ret = kadd_inference_engine_load(&engine_rf, "config.json", KADD_INFERENCE_TYPE_RF);
    ASSERT_EQ(ret, KADD_SUCCESS) << "RF 模型加载失败";

    feature_dim_rf = kadd_inference_engine_get_feature_dim(engine_rf);
    std::vector<float> features(feature_dim_rf, 0.5f);
    features[0] = INFINITY;

    float prob = 0.0f;
    ret = kadd_inference_predict(engine_rf, features.data(), &prob);
    EXPECT_EQ(ret, KADD_ERROR_INVALID_PARAM) << "Inf 特征应该返回 INVALID_PARAM";
}

TEST_F(InferenceEngineTest, PredictBatchWithNaNFeature)
{
    int ret = kadd_inference_engine_load(&engine_rf, "config.json", KADD_INFERENCE_TYPE_RF);
    ASSERT_EQ(ret, KADD_SUCCESS) << "RF 模型加载失败";

    feature_dim_rf = kadd_inference_engine_get_feature_dim(engine_rf);
    const int n = 2;
    std::vector<float> features(n * feature_dim_rf, 0.5f);
    features[0] = NAN;
    std::vector<float> probs(n, 0.0f);

    ret = kadd_inference_predict_batch(engine_rf, features.data(), n, probs.data(), 1);
    EXPECT_EQ(ret, KADD_ERROR_INVALID_PARAM) << "批量推理含 NaN 特征应该返回 INVALID_PARAM";
}

TEST_F(InferenceEngineTest, LoadFromPath_InvalidModel)
{
    kadd_inference_engine_t *engine = nullptr;
    int ret = kadd_inference_engine_load_from_path(&engine, "/nonexistent_model_xyz.onnx");

    EXPECT_NE(ret, KADD_SUCCESS);
    EXPECT_EQ(engine, nullptr);
}

TEST_F(InferenceEngineTest, LoadFromPath_GarbageFile)
{
    // 写一个包含垃圾数据的 .onnx 文件，覆盖 create_ort_session 的
    // CreateSession 失败路径 → check_ort_status 错误路径
    const char *garbage_path = "/tmp/garbage_model_xyz.onnx";
    FILE *fp = fopen(garbage_path, "wb");
    ASSERT_NE(fp, nullptr);
    const char garbage[] = "THIS_IS_NOT_A_VALID_ONNX_MODEL_FILE_GARBAGE_DATA_12345";
    fwrite(garbage, 1, sizeof(garbage), fp);
    fclose(fp);

    kadd_inference_engine_t *engine = nullptr;
    int ret = kadd_inference_engine_load_from_path(&engine, garbage_path);

    EXPECT_NE(ret, KADD_SUCCESS) << "垃圾文件加载应失败";
    EXPECT_EQ(engine, nullptr);

    remove(garbage_path);
}

// ========== 18. ORT 故障注入：错误清理分支覆盖率 ==========

/**
 * kadd_inference_engine.c 中约 90 行属于「ONNX Runtime 返回错误状态之后的清理分支」，
 * 真实模型正常加载时永远走不到。下面用 kadd_inference_fault_inject.h 提供的注入器
 * 逐个替换 OrtApi 函数表成员，使这些分支可达。
 *
 * 注入器只伪造 OrtApi 成员，错误状态本身由真实 ORT 产生，因此 ReleaseStatus /
 * GetErrorMessage 走的都是真实实现，不存在悬空指针。
 *
 * 若运行环境没有可用的 ONNX Runtime（RunAllScenarios 返回 0），用例记为跳过而非失败，
 * 避免在无 ORT 的机器上把整个测试二进制拖挂。
 */
TEST(InferenceFaultInjectTest, AllOrtErrorBranches)
{
    const int done = kadd_inject::RunAllScenarios();
    if (done == 0) {
        GTEST_SKIP() << "ONNX Runtime 不可用，跳过故障注入用例";
    }
    EXPECT_EQ(done, KADD_INJ_SCENARIO_COUNT - 1)
        << "所有注入场景都应成功执行（ORT 就绪时）";
}

TEST(InferenceFaultInjectTest, CheckOrtStatusErrorBranch)
{
    if (kadd_inject::RunScenario(KADD_INJ_CHECK_STATUS) != 0) {
        GTEST_SKIP() << "ONNX Runtime 不可用";
    }
    SUCCEED() << "check_ort_status 的错误分支（打印 + ReleaseStatus + 返回 -1）已覆盖";
}

TEST(InferenceFaultInjectTest, GetModelInputDimErrorBranches)
{
    const int cases[] = {KADD_INJ_INPUT_TYPE_INFO, KADD_INJ_CAST_INPUT,
                         KADD_INJ_DIM_COUNT_INPUT, KADD_INJ_DIM_COUNT_TOO_BIG,
                         KADD_INJ_DIMS_INPUT, KADD_INJ_INPUT_DIM_OK};
    int done = 0;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (kadd_inject::RunScenario(cases[i]) == 0) {
            ++done;
        }
    }
    if (done == 0) {
        GTEST_SKIP() << "ONNX Runtime 不可用";
    }
    EXPECT_EQ(done, (int)(sizeof(cases) / sizeof(cases[0])));
}

TEST(InferenceFaultInjectTest, GetModelOutputDimErrorBranches)
{
    const int cases[] = {KADD_INJ_OUTPUT_TYPE_INFO, KADD_INJ_CAST_OUTPUT,
                         KADD_INJ_OUTPUT_DIM_OK};
    int done = 0;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (kadd_inject::RunScenario(cases[i]) == 0) {
            ++done;
        }
    }
    if (done == 0) {
        GTEST_SKIP() << "ONNX Runtime 不可用";
    }
    EXPECT_EQ(done, (int)(sizeof(cases) / sizeof(cases[0])));
}

// ========== 19. 公共接口入参校验（不依赖 ORT，任何环境都必须覆盖） ==========

TEST(InferencePublicApiTest, LoadNullAndInvalidArgs)
{
    kadd_inference_engine_t *e = reinterpret_cast<kadd_inference_engine_t *>(0x1);
    EXPECT_EQ(kadd_inference_engine_load(nullptr, "config.json", KADD_INFERENCE_TYPE_RF),
              KADD_ERROR_NULL_PTR);
    EXPECT_EQ(kadd_inference_engine_load(&e, nullptr, KADD_INFERENCE_TYPE_RF),
              KADD_ERROR_NULL_PTR);
    EXPECT_EQ(kadd_inference_engine_load(&e, "config.json",
                                         static_cast<kadd_inference_type_t>(-1)),
              KADD_ERROR_INVALID_PARAM);
    EXPECT_EQ(kadd_inference_engine_load(&e, "config.json",
                                         static_cast<kadd_inference_type_t>(99)),
              KADD_ERROR_INVALID_PARAM);
    /* 配置文件不存在 / 非法 -> KADD_ERROR_CONFIG */
    EXPECT_EQ(kadd_inference_engine_load(&e, "/tmp/kadd_infer_no_such_config.json",
                                         KADD_INFERENCE_TYPE_RF),
              KADD_ERROR_CONFIG);
    EXPECT_EQ(kadd_inference_engine_load_from_path(nullptr, "x.onnx"), KADD_ERROR_NULL_PTR);
    EXPECT_EQ(kadd_inference_engine_load_from_path(&e, nullptr), KADD_ERROR_NULL_PTR);
}

TEST(InferencePublicApiTest, PredictBatchValidationWithoutSession)
{
    /* 直接构造未初始化会话的引擎，验证 predict_batch 在触碰 ORT 之前的全部校验分支 */
    kadd_inference_engine_t engine;
    memset(&engine, 0, sizeof(engine));
    engine.api = nullptr;
    engine.session = nullptr;
    engine.feature_dim = 4;
    engine.output_dim = 2;

    float feats[8] = {0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f};
    float probs[2] = {0.0f, 0.0f};

    EXPECT_EQ(kadd_inference_predict_batch(nullptr, feats, 2, probs, 1), KADD_ERROR_NULL_PTR);
    EXPECT_EQ(kadd_inference_predict_batch(&engine, nullptr, 2, probs, 1), KADD_ERROR_NULL_PTR);
    EXPECT_EQ(kadd_inference_predict_batch(&engine, feats, 2, nullptr, 1), KADD_ERROR_NULL_PTR);
    EXPECT_EQ(kadd_inference_predict_batch(&engine, feats, 0, probs, 1), KADD_ERROR_INVALID_PARAM);
    EXPECT_EQ(kadd_inference_predict_batch(&engine, feats, -1, probs, 1), KADD_ERROR_INVALID_PARAM);
    EXPECT_EQ(kadd_inference_predict_batch(&engine, feats, KADD_INFERENCE_MAX_SAMPLE_COUNT + 1,
                                           probs, 1),
              KADD_ERROR_INVALID_PARAM);

    /* feature_dim 非法 -> 在创建张量之前就被拦下 */
    kadd_inference_engine_t bad = engine;
    bad.feature_dim = 0;
    EXPECT_EQ(kadd_inference_predict_batch(&bad, feats, 1, probs, 1), KADD_ERROR_INVALID_PARAM);
    bad.feature_dim = KADD_INFERENCE_MAX_FEATURE_DIM + 1;
    EXPECT_EQ(kadd_inference_predict_batch(&bad, feats, 1, probs, 1), KADD_ERROR_INVALID_PARAM);

    /* 非有限特征 -> INVALID_PARAM（在触碰 ORT 之前返回） */
    float nan_feats[4] = {0.0f, NAN, 0.2f, 0.3f};
    EXPECT_EQ(kadd_inference_predict_batch(&engine, nan_feats, 1, probs, 1),
              KADD_ERROR_INVALID_PARAM);
    float inf_feats[4] = {0.0f, INFINITY, 0.2f, 0.3f};
    EXPECT_EQ(kadd_inference_predict_batch(&engine, inf_feats, 1, probs, 1),
              KADD_ERROR_INVALID_PARAM);

    /* 单样本推理是批量的特例，同样应被校验拦下 */
    float one = 0.0f;
    EXPECT_EQ(kadd_inference_predict(nullptr, feats, &one), KADD_ERROR_NULL_PTR);
    EXPECT_EQ(kadd_inference_predict(&engine, nan_feats, &one), KADD_ERROR_INVALID_PARAM);
}

TEST(InferencePublicApiTest, AccessorAndFreeNullSafety)
{
    EXPECT_EQ(kadd_inference_engine_get_feature_dim(nullptr), KADD_ERROR_NULL_PTR);
    kadd_inference_engine_free(nullptr);   /* 不应崩溃 */
    release_engine_resources(nullptr);     /* 不应崩溃 */
    SUCCEED();
}

// ========== 20. 故障注入补充：create_ort_session / get_io_name / predict_batch 错误分支 ==========

/**
 * 第 18 节的注入场景已扩展到会话创建、IO 名称探测与推理运行期三组分支，
 * 这里按组拆成独立用例，便于报告中按分支归因（AllOrtErrorBranches 仍做全量兜底）。
 */
TEST(InferenceFaultInjectTest, CreateSessionErrorBranches)
{
    const int cases[] = {KADD_INJ_CREATE_OPTIONS_FAIL, KADD_INJ_EXEC_MODE_FAIL,
                         KADD_INJ_GRAPH_OPT_FAIL, KADD_INJ_INTRA_THREADS_FAIL,
                         KADD_INJ_INTER_THREADS_FAIL, KADD_INJ_MEMORY_INFO_FAIL};
    int done = 0;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (kadd_inject::RunScenario(cases[i]) == 0) {
            ++done;
        }
    }
    if (done == 0) {
        GTEST_SKIP() << "ONNX Runtime 不可用";
    }
    EXPECT_EQ(done, (int)(sizeof(cases) / sizeof(cases[0])));
}

TEST(InferenceFaultInjectTest, IoNameErrorBranches)
{
    const int cases[] = {KADD_INJ_ALLOCATOR_FAIL, KADD_INJ_OUTPUT_COUNT_FAIL,
                         KADD_INJ_INPUT_NAME_FAIL, KADD_INJ_IO_NAME_NULL,
                         KADD_INJ_ALLOCATOR_FREE_FAIL};
    int done = 0;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (kadd_inject::RunScenario(cases[i]) == 0) {
            ++done;
        }
    }
    if (done == 0) {
        GTEST_SKIP() << "ONNX Runtime 不可用";
    }
    EXPECT_EQ(done, (int)(sizeof(cases) / sizeof(cases[0])));
}

TEST(InferenceFaultInjectTest, InitEnvErrorBranch)
{
    /* init_ort_env 的 CreateEnv 失败（110-113）与 load_engine_internal 的初始化
     * 失败上抛（395-398）：该场景会临时操作 TU 内 static 全局量并在结束时复原 */
    if (kadd_inject::RunScenario(KADD_INJ_CREATE_ENV_FAIL) != 0) {
        GTEST_SKIP() << "ONNX Runtime 不可用";
    }
    SUCCEED();
}

TEST(InferenceFaultInjectTest, PredictRuntimeErrorBranches)
{
    const int cases[] = {KADD_INJ_CREATE_TENSOR_FAIL, KADD_INJ_RUN_FAIL,
                         KADD_INJ_GET_DATA_FAIL, KADD_INJ_OUTPUT_DIM_SMALL};
    int done = 0;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (kadd_inject::RunScenario(cases[i]) == 0) {
            ++done;
        }
    }
    if (done == 0) {
        GTEST_SKIP() << "ONNX Runtime 不可用";
    }
    EXPECT_EQ(done, (int)(sizeof(cases) / sizeof(cases[0])));
}

// ========== 21. ORT 线程数环境变量分支（ort_thread_count_from_env，源码 273-291） ==========

/**
 * coverage_all.sh 恒定导出合法的 KADD_ORT_INTRA_OP_THREADS=8 / INTER_OP_THREADS=1，
 * 因此「未设置 / 空串 / 取值非法 → 回退默认」这几类分支在常规 UT 与 Fuzz 下永远走不到。
 * 下面用 RAII 守卫临时改写环境变量并在析构时精确复原，既不污染同进程内的其它用例，
 * 也不会破坏脚本导出的调优值。
 */
class ScopedEnvVar {
public:
    ScopedEnvVar(const char *name, const char *value) : had_(false)
    {
        /* [FIX 安全函数整改] std::string 替代定长数组 + snprintf，
         * 无截断风险；实际使用的环境变量名均为短常量，行为不变 */
        name_ = name;
        const char *prev = getenv(name);
        if (prev != nullptr) {
            had_ = true;
            saved_ = prev;
        }
        Apply(value);
    }
    ~ScopedEnvVar() { Apply(had_ ? saved_ : nullptr); }
    ScopedEnvVar(const ScopedEnvVar &) = delete;
    ScopedEnvVar &operator=(const ScopedEnvVar &) = delete;

private:
    void Apply(const char *value)
    {
#ifdef _WIN32
        (void)_putenv_s(name_.c_str(), (value != nullptr) ? value : "");   /* 空串即删除 */
#else
        if (value != nullptr) {
            (void)setenv(name_.c_str(), value, 1);
        } else {
            (void)unsetenv(name_.c_str());
        }
#endif
    }
    std::string name_;
    std::string saved_;
    bool had_;
};

TEST(InferenceEnvConfigTest, ThreadCountFromEnvAllBranches)
{
    const char *const name = KADD_ORT_INTRA_OP_ENV;

    /* 未设置 -> fallback（276-278 的 value == NULL） */
    {
        ScopedEnvVar guard(name, nullptr);
        EXPECT_EQ(getenv(name), nullptr);
        EXPECT_EQ(ort_thread_count_from_env(name, KADD_ORT_DEFAULT_INTRA_OP_THREADS), 8);
        EXPECT_EQ(ort_thread_count_from_env(name, 3), 3);
    }
    /* 空串 -> fallback（276-278 的 value[0] == '\0'） */
    {
        ScopedEnvVar guard(name, "");
        EXPECT_EQ(ort_thread_count_from_env(name, 8), 8);
    }
    /* 取值非法 -> 告警并回退（283-289）：非数字 / 前导空白 / 尾随字符 /
     * 下溢 / 负数 / 正号 / 十六进制 / 越上界 / strtol 溢出 */
    {
        const char *const bad[] = {"abc", " 8", "8x", "8 ", "0", "-1", "+8",
                                   "0x8", "1025", "99999999999999999999"};
        for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
            ScopedEnvVar guard(name, bad[i]);
            EXPECT_EQ(ort_thread_count_from_env(name, 8), 8)
                << "非法取值未被忽略: " << bad[i];
        }
    }
    /* 取值合法 -> 采用（290），含下界 1、默认 8 与上界 1024 */
    {
        const char *const good[] = {"1", "2", "8", "16", "1024"};
        const int expect[] = {1, 2, 8, 16, 1024};
        for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); ++i) {
            ScopedEnvVar guard(name, good[i]);
            EXPECT_EQ(ort_thread_count_from_env(name, 8), expect[i])
                << "合法取值未被采用: " << good[i];
        }
    }
    /* 两个环境变量共用同一函数，确认无名称耦合 */
    {
        ScopedEnvVar guard(KADD_ORT_INTER_OP_ENV, "4");
        EXPECT_EQ(ort_thread_count_from_env(KADD_ORT_INTER_OP_ENV,
                                           KADD_ORT_DEFAULT_INTER_OP_THREADS), 4);
    }
}

TEST(InferenceEnvConfigTest, LoadModelWithEnvUnsetAndInvalid)
{
    /* 端到端确认功能契约：线程数属于性能调优项，环境变量缺失或非法时
     * create_ort_session 只告警不中断，模型加载必须成功（源码 312-334）。
     * 同时让 276-289 在真实加载路径上再被命中一次。 */
    const char *const cfg = "config.json";
    kadd_config_t probe;
    if (kadd_config_load(cfg, &probe) != KADD_SUCCESS) {
        GTEST_SKIP() << "config.json 不可用，跳过环境变量端到端加载用例";
    }
    kadd_config_free(&probe);

    {
        ScopedEnvVar intra(KADD_ORT_INTRA_OP_ENV, nullptr);
        ScopedEnvVar inter(KADD_ORT_INTER_OP_ENV, nullptr);
        kadd_inference_engine_t *e = nullptr;
        ASSERT_EQ(kadd_inference_engine_load(&e, cfg, KADD_INFERENCE_TYPE_RF), KADD_SUCCESS)
            << "环境变量未设置时应回退默认线程数并成功加载";
        EXPECT_GT(kadd_inference_engine_get_feature_dim(e), 0);
        kadd_inference_engine_free(e);
    }
    {
        ScopedEnvVar intra(KADD_ORT_INTRA_OP_ENV, "not_a_number");
        ScopedEnvVar inter(KADD_ORT_INTER_OP_ENV, "-3");
        kadd_inference_engine_t *e = nullptr;
        ASSERT_EQ(kadd_inference_engine_load(&e, cfg, KADD_INFERENCE_TYPE_RF), KADD_SUCCESS)
            << "环境变量非法时应告警回退并成功加载";
        EXPECT_GT(kadd_inference_engine_get_feature_dim(e), 0);
        kadd_inference_engine_free(e);
    }
}

// ========== 主函数 ==========

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
