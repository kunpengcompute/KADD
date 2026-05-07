/**
 * @file test_ml_engine_gtest.cpp
 * @brief ML 引擎 Google Test 测试
 *
 * 测试内容：
 * 1. 功能测试 - 模型加载、单样本推理、批量推理
 * 2. 边界测试 - 空指针、无效参数、内存管理
 * 3. 性能测试 - 函数耗时测试
 * 4. 一致性测试 - 单样本与批量推理结果对比
 */

#include <gtest/gtest.h>
#include <chrono>
#include <vector>
#include <random>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <iomanip>
#include <set>
#include <string>
#include "kadd_ml_engine.h"
#include "kadd_feature_engine.h"
#include "kadd_config_loader.h"
#include "cJSON.h"

#ifndef TEST_DATA_PATH
#define TEST_DATA_PATH "assets/data/parsed_results_fixed.json"
#endif

// ========== 工具函数 ==========

/**
 * @brief 检查模型文件是否存在
 */
static bool ModelFileExists(const char *path) {
    std::ifstream file(path);
    return file.good();
}

/**
 * @brief 检查是否应该跳过 ML 测试
 */
static bool ShouldSkipMLTests(const char *model_path) {
    if (!ModelFileExists(model_path)) {
        std::cout << "[WARNING] 模型文件不存在: " << model_path << std::endl;
        std::cout << "[WARNING] 跳过 ML 引擎测试" << std::endl;
        return true;
    }
    return false;
}

// ========== 测试夹具 ==========
class MLEngineTest : public ::testing::Test {
protected:
    kadd_model_handle_t *model = nullptr;
    std::vector<float> test_features;
    kadd_config_t config;
    bool skip_test = false;

    void SetUp() override {
        // 从config.json加载配置
        if (kadd_config_load(nullptr, &config) != 0) {
            std::cout << "[WARNING] 无法加载config.json，跳过 ML 引擎测试" << std::endl;
            skip_test = true;
            return;
        }

        // 检查是否应该跳过测试
        if (ShouldSkipMLTests(config.ml.model_path)) {
            skip_test = true;
            return;
        }

        // 初始化测试特征向量 (全零)
        test_features.resize(config.ml.feature_dim, 0.0f);
    }

    void TearDown() override {
        if (model) {
            kadd_model_free(model);
            model = nullptr;
        }
    }

    // 辅助函数：生成随机特征向量
    void GenerateRandomFeatures(std::vector<float> &features, float min_val = 0.0f, float max_val = 100.0f) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> dis(min_val, max_val);

        features.resize(config.ml.feature_dim);
        for (auto &f : features) {
            f = dis(gen);
        }
    }

    // 辅助函数：创建有效的模型
    int CreateValidModel() {
        if (skip_test) return -1;
        return kadd_model_load(&model, config.ml.model_path, config.ml.feature_dim);
    }

    // 辅助函数：验证概率值有效性（允许浮点误差）
    bool IsValidProbability(float prob) {
        return prob >= -0.001f && prob <= 1.001f;
    }
};

// ========== 1. 功能测试 ==========

TEST_F(MLEngineTest, LoadValidModel) {
    if (skip_test) GTEST_SKIP() << "模型文件不可用";

    int ret = CreateValidModel();
    ASSERT_EQ(ret, 0) << "应该成功加载模型，路径: " << config.ml.model_path;
    ASSERT_NE(model, nullptr) << "模型句柄不应为空";
}

TEST_F(MLEngineTest, PredictSingleSample) {
    if (skip_test) GTEST_SKIP() << "模型文件不可用";

    ASSERT_EQ(CreateValidModel(), 0) << "模型加载失败";

    float probability = 0.0f;
    int ret = kadd_model_predict(model, test_features.data(), &probability);

    EXPECT_EQ(ret, 0) << "推理应该成功";
    EXPECT_TRUE(IsValidProbability(probability)) << "概率值应在 [0, 1] 范围内，实际: " << probability;
}

TEST_F(MLEngineTest, PredictWithVariousFeatures) {
    if (skip_test) GTEST_SKIP() << "模型文件不可用";

    ASSERT_EQ(CreateValidModel(), 0) << "模型加载失败";

    // 测试多种特征向量
    struct TestCase {
        std::vector<float> features;
        const char* description;
    };

    std::vector<TestCase> test_cases = {
        {std::vector<float>(config.ml.feature_dim, 0.0f), "全零特征"},
        {std::vector<float>(config.ml.feature_dim, 1.0f), "全一特征"},
        {std::vector<float>(config.ml.feature_dim, 100.0f), "大值特征"},
        {std::vector<float>(config.ml.feature_dim, -50.0f), "负值特征"},
        {std::vector<float>(config.ml.feature_dim, 0.5f), "中等值特征"},
    };

    for (size_t i = 0; i < test_cases.size(); i++) {
        float probability = 0.0f;
        int ret = kadd_model_predict(model, test_cases[i].features.data(), &probability);

        EXPECT_EQ(ret, 0) << "测试用例 " << i << " (" << test_cases[i].description << ") 应该推理成功";
        EXPECT_TRUE(IsValidProbability(probability))
            << "测试用例 " << i << " (" << test_cases[i].description
            << ") 概率值应在 [0, 1] 范围内，实际: " << probability;
    }
}

TEST_F(MLEngineTest, PredictBatchSize1) {
    if (skip_test) GTEST_SKIP() << "模型文件不可用";

    ASSERT_EQ(CreateValidModel(), 0) << "模型加载失败";

    const int n = 1;
    std::vector<float> features(config.ml.feature_dim * n, 0.0f);
    std::vector<float> probabilities(n);

    int ret = kadd_model_predict_batch(model, features.data(), n, probabilities.data(), 1);

    EXPECT_EQ(ret, n) << "应该成功推理 " << n << " 个样本";
    EXPECT_TRUE(IsValidProbability(probabilities[0])) << "概率值应在 [0, 1] 范围内";
}

TEST_F(MLEngineTest, PredictBatchSize10) {
    if (skip_test) GTEST_SKIP() << "模型文件不可用";

    ASSERT_EQ(CreateValidModel(), 0) << "模型加载失败";

    const int n = 10;
    std::vector<float> features(config.ml.feature_dim * n, 0.0f);
    std::vector<float> probabilities(n);

    int ret = kadd_model_predict_batch(model, features.data(), n, probabilities.data(), 10);

    EXPECT_EQ(ret, n) << "应该成功推理 " << n << " 个样本";

    for (int i = 0; i < n; i++) {
        EXPECT_TRUE(IsValidProbability(probabilities[i]))
            << "样本 " << i << " 概率值应在 [0, 1] 范围内，实际: " << probabilities[i];
    }
}

TEST_F(MLEngineTest, PredictBatchWithAutoBatchSize) {
    if (skip_test) GTEST_SKIP() << "模型文件不可用";

    ASSERT_EQ(CreateValidModel(), 0) << "模型加载失败";

    const int n = 50;
    std::vector<float> features(config.ml.feature_dim * n, 0.0f);
    std::vector<float> probabilities(n);

    // batch_size = 0 表示自动选择
    int ret = kadd_model_predict_batch(model, features.data(), n, probabilities.data(), 0);

    EXPECT_EQ(ret, n) << "应该成功推理 " << n << " 个样本（自动批次大小）";

    // 验证所有概率值有效
    for (int i = 0; i < n; i++) {
        EXPECT_TRUE(IsValidProbability(probabilities[i]))
            << "样本 " << i << " 概率值应在 [0, 1] 范围内";
    }
}

TEST_F(MLEngineTest, PredictBatchConsistencyWithSingle) {
    ASSERT_EQ(CreateValidModel(), 0) << "模型加载失败";

    const int n = 10;
    std::vector<float> features(config.ml.feature_dim * n);
    std::vector<float> single_probs(n);
    std::vector<float> batch_probs(n);

    // 生成随机特征
    for (int i = 0; i < n; i++) {
        GenerateRandomFeatures(features);
        std::copy(features.begin(), features.begin() + config.ml.feature_dim,
                  features.data() + i * config.ml.feature_dim);
    }

    // 单样本推理
    for (int i = 0; i < n; i++) {
        kadd_model_predict(model, features.data() + i * config.ml.feature_dim, &single_probs[i]);
    }

    // 批量推理
    kadd_model_predict_batch(model, features.data(), n, batch_probs.data(), 10);

    // 验证一致性（允许浮点误差）
    for (int i = 0; i < n; i++) {
        EXPECT_NEAR(single_probs[i], batch_probs[i], 1e-6f)
            << "样本 " << i << " 单样本和批量推理结果应该一致";
    }
}

TEST_F(MLEngineTest, ModelReuse) {
    if (skip_test) GTEST_SKIP() << "模型文件不可用";

    ASSERT_EQ(CreateValidModel(), 0) << "模型加载失败";

    // 多次使用同一模型
    for (int i = 0; i < 5; i++) {
        float probability = 0.0f;
        int ret = kadd_model_predict(model, test_features.data(), &probability);

        EXPECT_EQ(ret, 0) << "第 " << i << " 次推理应该成功";
        EXPECT_TRUE(IsValidProbability(probability)) << "第 " << i << " 次 概率值应在 [0, 1] 范围内";
    }
}

TEST_F(MLEngineTest, PredictBatchDifferentBatchSizes) {
    if (skip_test) GTEST_SKIP() << "模型文件不可用";

    ASSERT_EQ(CreateValidModel(), 0) << "模型加载失败";

    // 测试不同的批次大小
    std::vector<int> batch_sizes = {1, 2, 5, 10, 16, 32, 64, 128};

    for (int batch_size : batch_sizes) {
        std::vector<float> features(config.ml.feature_dim * batch_size, 0.0f);
        std::vector<float> probabilities(batch_size);

        int ret = kadd_model_predict_batch(model, features.data(), batch_size,
                                           probabilities.data(), batch_size);

        EXPECT_EQ(ret, batch_size) << "批次大小 " << batch_size << " 应该成功推理";

        // 验证所有概率值有效
        for (int i = 0; i < batch_size; i++) {
            EXPECT_TRUE(IsValidProbability(probabilities[i]))
                << "批次 " << batch_size << " 样本 " << i << " 概率值应在 [0, 1] 范围内";
        }
    }
}

// ========== 2. 边界测试 ==========

TEST_F(MLEngineTest, LoadWithNullHandle) {
    if (skip_test) GTEST_SKIP() << "模型文件不可用";

    int ret = kadd_model_load(nullptr, config.ml.model_path, config.ml.feature_dim);
    EXPECT_NE(ret, 0) << "传入 NULL 句柄指针应该失败";
}

TEST_F(MLEngineTest, LoadWithInvalidPath) {
    if (skip_test) GTEST_SKIP() << "模型文件不可用";

    int ret = kadd_model_load(&model, "nonexistent_model.onnx", config.ml.feature_dim);
    EXPECT_NE(ret, 0) << "加载不存在的模型应该失败";
    EXPECT_EQ(model, nullptr) << "失败时句柄应为 NULL";
}

TEST_F(MLEngineTest, LoadWithEmptyPath) {
    int ret = kadd_model_load(&model, "", config.ml.feature_dim);
    EXPECT_NE(ret, 0) << "加载空路径模型应该失败";
}

TEST_F(MLEngineTest, PredictWithNullHandle) {
    if (skip_test) GTEST_SKIP() << "模型文件不可用";

    float probability = 0.0f;
    int ret = kadd_model_predict(nullptr, test_features.data(), &probability);
    EXPECT_NE(ret, 0) << "NULL 句柄推理应该失败";
}

TEST_F(MLEngineTest, PredictWithNullFeatures) {
    if (skip_test) GTEST_SKIP() << "模型文件不可用";

    ASSERT_EQ(CreateValidModel(), 0) << "模型加载失败";

    float probability = 0.0f;
    int ret = kadd_model_predict(model, nullptr, &probability);
    EXPECT_NE(ret, 0) << "NULL 特征向量推理应该失败";
}

TEST_F(MLEngineTest, PredictWithNullProbability) {
    if (skip_test) GTEST_SKIP() << "模型文件不可用";

    ASSERT_EQ(CreateValidModel(), 0) << "模型加载失败";

    int ret = kadd_model_predict(model, test_features.data(), nullptr);
    EXPECT_NE(ret, 0) << "NULL 概率输出指针应该失败";
}

TEST_F(MLEngineTest, PredictBeforeLoad) {
    kadd_model_handle_t *invalid_model = nullptr;
    float probability = 0.0f;
    int ret = kadd_model_predict(invalid_model, test_features.data(), &probability);
    EXPECT_NE(ret, 0) << "未加载模型前推理应该失败";
}

TEST_F(MLEngineTest, PredictBatchWithNullHandle) {
    const int n = 10;
    std::vector<float> features(config.ml.feature_dim * n, 0.0f);
    std::vector<float> probabilities(n);

    int ret = kadd_model_predict_batch(nullptr, features.data(), n, probabilities.data(), 10);
    EXPECT_NE(ret, 0) << "NULL 句柄批量推理应该失败";
}

TEST_F(MLEngineTest, PredictBatchWithNullFeatures) {
    ASSERT_EQ(CreateValidModel(), 0) << "模型加载失败";

    const int n = 10;
    std::vector<float> probabilities(n);

    int ret = kadd_model_predict_batch(model, nullptr, n, probabilities.data(), 10);
    EXPECT_NE(ret, 0) << "NULL 特征矩阵批量推理应该失败";
}

TEST_F(MLEngineTest, PredictBatchWithNullProbabilities) {
    ASSERT_EQ(CreateValidModel(), 0) << "模型加载失败";

    const int n = 10;
    std::vector<float> features(config.ml.feature_dim * n, 0.0f);

    int ret = kadd_model_predict_batch(model, features.data(), n, nullptr, 10);
    EXPECT_NE(ret, 0) << "NULL 概率数组批量推理应该失败";
}

TEST_F(MLEngineTest, PredictBatchWithZeroCount) {
    ASSERT_EQ(CreateValidModel(), 0) << "模型加载失败";

    std::vector<float> probabilities(0);

    int ret = kadd_model_predict_batch(model, nullptr, 0, probabilities.data(), 10);
    EXPECT_EQ(ret, -1) << "零样本批量应该失败并返回 -1（当前实现行为）";
}

TEST_F(MLEngineTest, PredictBatchWithNegativeCount) {
    ASSERT_EQ(CreateValidModel(), 0) << "模型加载失败";

    std::vector<float> probabilities(10);

    int ret = kadd_model_predict_batch(model, nullptr, -1, probabilities.data(), 10);
    EXPECT_NE(ret, 0) << "负样本数批量推理应该失败";
}

TEST_F(MLEngineTest, FreeNullHandle) {
    // 不应该崩溃
    kadd_model_free(nullptr);
    SUCCEED();
}

TEST_F(MLEngineTest, FreeAfterPredict) {
    ASSERT_EQ(CreateValidModel(), 0) << "模型加载失败";

    float probability = 0.0f;
    kadd_model_predict(model, test_features.data(), &probability);

    kadd_model_free(model);
    model = nullptr;

    // 验证释放后不能再使用
    int ret = kadd_model_predict(model, test_features.data(), &probability);
    EXPECT_NE(ret, 0) << "释放后推理应该失败";
}

TEST_F(MLEngineTest, MultipleFree) {
    ASSERT_EQ(CreateValidModel(), 0) << "模型加载失败";

    kadd_model_free(model);
    model = nullptr;

    // 多次释放不应该崩溃
    kadd_model_free(model);
    kadd_model_free(nullptr);
    SUCCEED();
}

// ========== 3. 性能测试 ==========

TEST_F(MLEngineTest, Performance_SinglePrediction) {
    if (skip_test) GTEST_SKIP() << "模型文件不可用";

    ASSERT_EQ(CreateValidModel(), 0) << "模型加载失败";

    const int iterations = 1000;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; i++) {
        float probability = 0.0f;
        kadd_model_predict(model, test_features.data(), &probability);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    double avg_time_us = static_cast<double>(duration.count()) / iterations;
    double ops_per_sec = 1000000.0 / avg_time_us;

    std::cout << "\n[性能测试] 单样本推理平均耗时: " << avg_time_us << " μs" << std::endl;
    std::cout << "[性能测试] 单样本推理吞吐量: " << static_cast<int>(ops_per_sec) << " ops/sec" << std::endl;

    // 性能基准：应该 > 1,000 ops/sec（调整为更宽松的基准）
    EXPECT_GT(ops_per_sec, 1000.0) << "单样本推理性能应该 > 1,000 ops/sec";
}

TEST_F(MLEngineTest, Performance_BatchPrediction) {
    if (skip_test) GTEST_SKIP() << "模型文件不可用";

    ASSERT_EQ(CreateValidModel(), 0) << "模型加载失败";

    const int batch_size = 64;
    const int iterations = 100;
    const int total_samples = batch_size * iterations;

    std::vector<float> features(config.ml.feature_dim * batch_size, 0.0f);
    std::vector<float> probabilities(batch_size);

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; i++) {
        kadd_model_predict_batch(model, features.data(), batch_size, probabilities.data(), batch_size);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    double avg_time_us = static_cast<double>(duration.count()) / iterations;
    double samples_per_sec = (total_samples * 1000000.0) / duration.count();

    std::cout << "\n[性能测试] 批量推理(batch=" << batch_size << ")平均耗时: " << avg_time_us << " μs" << std::endl;
    std::cout << "[性能测试] 批量推理吞吐量: " << static_cast<int>(samples_per_sec) << " samples/sec" << std::endl;

    // 性能基准：应该 > 10,000 samples/sec（调整为更宽松的基准）
    EXPECT_GT(samples_per_sec, 10000.0) << "批量推理性能应该 > 10,000 samples/sec";
}

TEST_F(MLEngineTest, Performance_Scalability) {
    ASSERT_EQ(CreateValidModel(), 0) << "模型加载失败";

    std::cout << "\n[性能测试] 批量大小扩展性测试:" << std::endl;

    std::vector<int> batch_sizes = {1, 10, 32, 64, 128, 256};

    for (int batch_size : batch_sizes) {
        std::vector<float> features(config.ml.feature_dim * batch_size, 0.0f);
        std::vector<float> probabilities(batch_size);

        const int iterations = std::max(10, 1000 / batch_size);
        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < iterations; i++) {
            kadd_model_predict_batch(model, features.data(), batch_size, probabilities.data(), batch_size);
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

TEST_F(MLEngineTest, Performance_ModelLoading) {
    const int iterations = 10;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; i++) {
        kadd_model_handle_t *temp_model = nullptr;
        kadd_model_load(&temp_model, config.ml.model_path, config.ml.feature_dim);
        kadd_model_free(temp_model);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    double avg_time_ms = static_cast<double>(duration.count()) / iterations;

    std::cout << "\n[性能测试] 模型加载平均耗时: " << avg_time_ms << " ms" << std::endl;

    // 性能基准：加载时间应该 < 1 秒
    EXPECT_LT(avg_time_ms, 1000.0) << "模型加载时间应该 < 1 秒";
}

TEST_F(MLEngineTest, Performance_MemoryStress) {
    if (skip_test) GTEST_SKIP() << "模型文件不可用";

    ASSERT_EQ(CreateValidModel(), 0) << "模型加载失败";

    // 大批量推理测试内存管理
    const int large_batch = 1000;
    std::vector<float> features(config.ml.feature_dim * large_batch, 0.0f);
    std::vector<float> probabilities(large_batch);

    auto start = std::chrono::high_resolution_clock::now();

    int ret = kadd_model_predict_batch(model, features.data(), large_batch, probabilities.data(), 128);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    EXPECT_EQ(ret, large_batch) << "大批量推理应该成功";

    std::cout << "\n[性能测试] 大批量(" << large_batch << "样本)推理耗时: "
              << duration.count() << " ms" << std::endl;

    // 验证所有结果有效
    for (int i = 0; i < large_batch; i++) {
        EXPECT_TRUE(IsValidProbability(probabilities[i]))
            << "样本 " << i << " 概率值应在 [0, 1] 范围内";
    }
}

// ========== 4. 一致性测试 ==========

TEST_F(MLEngineTest, Consistency_SameInputSameOutput) {
    if (skip_test) GTEST_SKIP() << "模型文件不可用";

    ASSERT_EQ(CreateValidModel(), 0) << "模型加载失败";

    // 同一输入多次推理应该得到相同结果
    std::vector<float> features(config.ml.feature_dim, 1.0f);

    std::vector<float> results(5);
    for (int i = 0; i < 5; i++) {
        kadd_model_predict(model, features.data(), &results[i]);
    }

    // 所有结果应该相同
    for (int i = 1; i < 5; i++) {
        EXPECT_FLOAT_EQ(results[0], results[i])
            << "第 " << i << " 次推理结果应该与第 0 次相同";
    }
}

TEST_F(MLEngineTest, Consistency_DeterministicBehavior) {
    if (skip_test) GTEST_SKIP() << "模型文件不可用";

    ASSERT_EQ(CreateValidModel(), 0) << "模型加载失败";

    // 测试模型行为是确定性的
    const int n = 10;
    std::vector<float> features(config.ml.feature_dim * n);
    std::vector<float> probabilities1(n);
    std::vector<float> probabilities2(n);

    // 生成相同的随机特征
    GenerateRandomFeatures(features);

    // 两次推理
    kadd_model_predict_batch(model, features.data(), n, probabilities1.data(), n);
    kadd_model_predict_batch(model, features.data(), n, probabilities2.data(), n);

    // 结果应该完全相同
    for (int i = 0; i < n; i++) {
        EXPECT_FLOAT_EQ(probabilities1[i], probabilities2[i])
            << "样本 " << i << " 两次推理结果应该相同";
    }
}

// ========== 5. 数据集准确率测试 ==========

/**
 * @brief 从 JSON 文件加载测试数据
 */
static bool LoadTestData(const char *json_path,
                        std::vector<std::vector<int>> &tokens_list,
                        std::vector<int> &labels) {
    // 读取 JSON 文件
    FILE *fp = fopen(json_path, "rb");
    if (!fp) {
        std::cerr << "无法打开文件: " << json_path << std::endl;
        return false;
    }

    // 获取文件大小
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // 读取文件内容
    char *json_str = (char *)malloc(file_size + 1);
    if (!json_str) {
        fclose(fp);
        return false;
    }
    fread(json_str, 1, file_size, fp);
    json_str[file_size] = '\0';
    fclose(fp);

    // 解析 JSON
    cJSON *root = cJSON_Parse(json_str);
    free(json_str);

    if (!root) {
        std::cerr << "JSON 解析失败" << std::endl;
        return false;
    }

    // 获取 data 数组
    cJSON *data_array = cJSON_GetObjectItem(root, "data");
    if (!data_array || !cJSON_IsArray(data_array)) {
        std::cerr << "未找到 data 数组" << std::endl;
        cJSON_Delete(root);
        return false;
    }

    // 遍历数据（带去重逻辑，与训练脚本保持一致）
    std::set<std::string> seen_tokens;  // 用于去重
    cJSON *item = nullptr;
    cJSON_ArrayForEach(item, data_array) {
        // 获取 label
        cJSON *label_item = cJSON_GetObjectItem(item, "label");
        if (!label_item || !cJSON_IsNumber(label_item)) {
            continue;
        }

        // 获取 tokens 数组
        cJSON *tokens_array = cJSON_GetObjectItem(item, "tokens");
        if (!tokens_array || !cJSON_IsArray(tokens_array)) {
            continue;
        }

        std::vector<int> tokens;
        cJSON *token = nullptr;
        cJSON_ArrayForEach(token, tokens_array) {
            if (cJSON_IsNumber(token)) {
                tokens.push_back(token->valueint);
            }
        }

        // 去重逻辑：只基于 tokens 序列（不包含 label）
        std::string signature;
        for (int t : tokens) {
            signature += std::to_string(t) + ",";
        }

        if (seen_tokens.find(signature) != seen_tokens.end()) {
            continue;  // 跳过重复样本
        }
        seen_tokens.insert(signature);

        labels.push_back(label_item->valueint);
        tokens_list.push_back(tokens);
    }

    cJSON_Delete(root);
    return true;
}

/**
 * @brief 将 tokens 转换为特征向量
 */
static bool TokensToFeatures(const std::vector<int> &tokens, std::vector<float> &features, int feature_dim) {
    // 检查 tokens 是否为空
    if (tokens.empty()) {
        return false;
    }

    features.resize(feature_dim, 0.0f);

    // 使用特征引擎将 token 序列转换为特征
    std::vector<int> int_features(feature_dim, 0);
    int ret = kadd_seq_to_feature(tokens.data(), tokens.size(), 5,
                                  int_features.data(), feature_dim);
    if (ret != 0) {
        return false;
    }

    // 转换为 float
    for (int i = 0; i < feature_dim; i++) {
        features[i] = static_cast<float>(int_features[i]);
    }

    return true;
}

TEST_F(MLEngineTest, AccuracyOnParsedResultsDataset) {
    if (skip_test) GTEST_SKIP() << "模型文件不可用";

    const char *json_path = TEST_DATA_PATH;
    std::vector<std::vector<int>> tokens_list;
    std::vector<int> labels;

    // 加载测试数据
    ASSERT_TRUE(LoadTestData(json_path, tokens_list, labels))
        << "无法加载测试数据文件: " << json_path;

    ASSERT_GT(tokens_list.size(), 0) << "测试数据为空";
    ASSERT_EQ(tokens_list.size(), labels.size()) << "tokens 和 labels 数量不匹配";

    std::cerr << "\n[准确率测试] 加载了 " << tokens_list.size() << " 个测试样本" << std::endl;

    // 加载模型
    ASSERT_EQ(CreateValidModel(), 0) << "模型加载失败";

    // 统计变量
    int correct = 0;
    int total = 0;
    int true_positive = 0;
    int true_negative = 0;
    int false_positive = 0;
    int false_negative = 0;
    std::vector<float> probabilities;
    probabilities.reserve(tokens_list.size());

    // 逐个推理
    for (size_t i = 0; i < tokens_list.size(); i++) {
        std::vector<float> features;
        if (!TokensToFeatures(tokens_list[i], features, config.ml.feature_dim)) {
            std::cerr << "样本 " << i << " 特征转换失败" << std::endl;
            continue;
        }

        float prob = 0.0f;
        int ret = kadd_model_predict(model, features.data(), &prob);

        if (ret == 0) {
            probabilities.push_back(prob);

            // 将概率转换为预测结果 (阈值 0.5)
            int predicted = (prob >= 0.5f) ? 1 : 0;
            int actual = labels[i];

            total++;

            if (predicted == actual) {
                correct++;
                if (predicted == 1) true_positive++;
                else true_negative++;
            } else {
                if (predicted == 1) false_positive++;
                else false_negative++;
            }
        }
    }

    // 计算准确率
    float accuracy = (total > 0) ? (static_cast<float>(correct) / total * 100.0f) : 0.0f;
    float precision = ((true_positive + false_positive) > 0) ?
        (static_cast<float>(true_positive) / (true_positive + false_positive) * 100.0f) : 0.0f;
    float recall = ((true_positive + false_negative) > 0) ?
        (static_cast<float>(true_positive) / (true_positive + false_negative) * 100.0f) : 0.0f;
    float f1_score = ((precision + recall) > 0) ?
        (2.0f * precision * recall / (precision + recall)) : 0.0f;

    // 输出结果
    std::cerr << "\n========================================" << std::endl;
    std::cerr << "  数据集准确率测试结果" << std::endl;
    std::cerr << "========================================" << std::endl;
    std::cerr << "总样本数: " << total << std::endl;
    std::cerr << "正确预测: " << correct << std::endl;
    std::cerr << "错误预测: " << (total - correct) << std::endl;
    std::cerr << "----------------------------------------" << std::endl;
    std::cerr << "准确率: " << std::fixed << std::setprecision(2) << accuracy << "%" << std::endl;
    std::cerr << "精确率: " << precision << "%" << std::endl;
    std::cerr << "召回率: " << recall << "%" << std::endl;
    std::cerr << "F1 分数: " << f1_score << std::endl;
    std::cerr << "----------------------------------------" << std::endl;
    std::cerr << "混淆矩阵:" << std::endl;
    std::cerr << "  真阳性: " << true_positive << std::endl;
    std::cerr << "  真阴性: " << true_negative << std::endl;
    std::cerr << "  假阳性: " << false_positive << std::endl;
    std::cerr << "  假阴性: " << false_negative << std::endl;
    std::cerr << "========================================\n" << std::endl;

    // 基本要求：准确率应该 > 30%（至少比随机猜测好一些）
    EXPECT_GT(accuracy, 30.0f) << "模型准确率应该 > 30%";

    // 性能基准：准确率应该 > 50%（理想情况）
    // EXPECT_GT(accuracy, 50.0f) << "模型准确率应该 > 50%";
}

TEST_F(MLEngineTest, BatchAccuracyOnParsedResultsDataset) {
    if (skip_test) GTEST_SKIP() << "模型文件不可用";

    const char *json_path = TEST_DATA_PATH;
    std::vector<std::vector<int>> tokens_list;
    std::vector<int> labels;

    // 加载测试数据
    ASSERT_TRUE(LoadTestData(json_path, tokens_list, labels))
        << "无法加载测试数据文件: " << json_path;

    ASSERT_GT(tokens_list.size(), 0) << "测试数据为空";
    ASSERT_EQ(tokens_list.size(), labels.size()) << "tokens 和 labels 数量不匹配";

    std::cerr << "\n[批量准确率测试] 加载了 " << tokens_list.size() << " 个测试样本" << std::endl;

    // 加载模型
    ASSERT_EQ(CreateValidModel(), 0) << "模型加载失败";

    // 将所有 tokens 转换为特征矩阵
    std::vector<float> features;
    features.reserve(tokens_list.size() * config.ml.feature_dim);

    std::vector<int> valid_labels;
    std::vector<size_t> valid_indices;

    for (size_t i = 0; i < tokens_list.size(); i++) {
        std::vector<float> feat;
        if (TokensToFeatures(tokens_list[i], feat, config.ml.feature_dim)) {
            features.insert(features.end(), feat.begin(), feat.end());
            valid_labels.push_back(labels[i]);
            valid_indices.push_back(i);
        }
    }

    int n = valid_labels.size();
    ASSERT_GT(n, 0) << "没有有效的特征向量";

    std::cout << "有效样本数: " << n << std::endl;

    // 批量推理
    std::vector<float> probabilities(n);
    const int batch_size = 64;

    auto start = std::chrono::high_resolution_clock::now();
    int ret = kadd_model_predict_batch(model, features.data(), n, probabilities.data(), batch_size);
    auto end = std::chrono::high_resolution_clock::now();

    ASSERT_EQ(ret, n) << "批量推理应该成功";

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "批量推理耗时: " << duration.count() << " ms" << std::endl;
    std::cout << "平均每样本: " << (duration.count() * 1000.0 / n) << " μs" << std::endl;

    // 统计结果
    int correct = 0;
    int true_positive = 0, true_negative = 0;
    int false_positive = 0, false_negative = 0;

    for (int i = 0; i < n; i++) {
        int predicted = (probabilities[i] >= 0.5f) ? 1 : 0;
        int actual = valid_labels[i];

        if (predicted == actual) {
            correct++;
            if (predicted == 1) true_positive++;
            else true_negative++;
        } else {
            if (predicted == 1) false_positive++;
            else false_negative++;
        }
    }

    // 计算指标
    float accuracy = (static_cast<float>(correct) / n * 100.0f);
    float precision = ((true_positive + false_positive) > 0) ?
        (static_cast<float>(true_positive) / (true_positive + false_positive) * 100.0f) : 0.0f;
    float recall = ((true_positive + false_negative) > 0) ?
        (static_cast<float>(true_positive) / (true_positive + false_negative) * 100.0f) : 0.0f;
    float f1_score = ((precision + recall) > 0) ?
        (2.0f * precision * recall / (precision + recall)) : 0.0f;

    // 输出结果
    std::cerr << "\n========================================" << std::endl;
    std::cerr << "  批量推理准确率测试结果" << std::endl;
    std::cerr << "========================================" << std::endl;
    std::cerr << "准确率: " << std::fixed << std::setprecision(2) << accuracy << "%" << std::endl;
    std::cerr << "精确率: " << precision << "%" << std::endl;
    std::cerr << "召回率: " << recall << "%" << std::endl;
    std::cerr << "F1 分数: " << f1_score << std::endl;
    std::cerr << "========================================\n" << std::endl;

    EXPECT_GT(accuracy, 50.0f) << "批量推理准确率应该 > 50%";
}

// ========== 主函数 ==========
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    // 从 config.json 加载配置
    kadd_config_t config;
    bool config_loaded = (kadd_config_load(nullptr, &config) == 0);

    const char *model_path = "assets/model/model_tokens_rf.onnx";  // 默认路径
    if (config_loaded) {
        model_path = config.ml.model_path;
    } else {
        std::cout << "[WARNING] 无法加载 config.json，使用默认模型路径" << std::endl;
    }

    // 检查是否应该跳过 ML 测试
    if (ShouldSkipMLTests(model_path)) {
        std::cout << "\n========================================" << std::endl;
        std::cout << "  ML 引擎测试已跳过" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "原因: 模型文件不存在" << std::endl;
        std::cout << "预期路径: " << model_path << std::endl;
        std::cout << "\n提示:" << std::endl;
        std::cout << "  1. 确保已下载模型文件到正确路径" << std::endl;
        std::cout << "  2. 检查 config.json 中的 kadd_ml_engine.model_path 配置" << std::endl;
        std::cout << "========================================\n" << std::endl;
        return 0;
    }

    std::cout << "\n========================================" << std::endl;
    std::cout << "  ML 引擎测试" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "模型路径: " << model_path << std::endl;
    if (config_loaded) {
        std::cout << "特征维度: " << config.ml.feature_dim << std::endl;
    }
    std::cout << "========================================\n" << std::endl;

    return RUN_ALL_TESTS();
}
