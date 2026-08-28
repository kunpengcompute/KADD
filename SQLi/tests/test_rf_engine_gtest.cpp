// Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
/**
 * @file test_rf_engine_gtest.cpp
 * @brief kadd_rf_engine 真实模型性能 + 准确率 gtest。
 *
 * 加载 assets/model/model_tokens_rf.krf，用真实训练 CSV（DFA 分词 ->
 * kadd_seq_to_feature 28 维特征）测准确率/精确率/召回率/F1，并测单样本
 * 推理吞吐量。模型路径写死，由 copy_test_resources 复制到 build/tests/assets/。
 */

#include "kadd_rf_engine.h"
#include "kadd_dfa_engine.h"
#include "kadd_feature_engine.h"
#include "test_model_data.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>


namespace {

constexpr int kSeq = 300;
constexpr int kFeatureDim = 28;
constexpr int kWindow = 5;

constexpr const char* kModelPath = "assets/model/model_tokens_rf.krf";
constexpr const char* kCsvPath =
    "../../tools/model_train/train_data/train_data.csv";

kadd_dfa_engine_t* make_dfa() {
    kadd_dfa_table_paths_t paths = {
        "assets/data/transition_table.bin",
        "assets/data/output_table.bin",
        "assets/data/eod_table.bin"
    };
    kadd_dfa_engine_t* dfa = nullptr;
    if (kadd_dfa_engine_create(&dfa, &paths, kSeq, kSeq) != KADD_SUCCESS) {
        return nullptr;
    }
    return dfa;
}

/* tokens -> 28 维特征 -> float 数组 */
std::vector<float> to_features(const std::vector<int>& tokens) {
    int feat[kFeatureDim] = {0};
    kadd_seq_to_feature(tokens.data(), (int)tokens.size(), kWindow, feat, kFeatureDim);
    std::vector<float> f(kFeatureDim);
    for (int i = 0; i < kFeatureDim; ++i) f[i] = (float)feat[i];
    return f;
}

struct Metrics { long tp = 0, fp = 0, tn = 0, fn = 0; };

void print_metrics(const char* tag, const Metrics& m, int n) {
    float acc = (float)(m.tp + m.tn) / (float)n;
    float p1  = (m.tp + m.fp) ? (float)m.tp / (float)(m.tp + m.fp) : 0.0f;
    float r1  = (m.tp + m.fn) ? (float)m.tp / (float)(m.tp + m.fn) : 0.0f;
    float f1  = (p1 + r1) > 0 ? 2 * p1 * r1 / (p1 + r1) : 0.0f;
    std::printf("[%s] n=%d acc=%.4f | prec=%.4f rec=%.4f f1=%.4f (tp=%ld fp=%ld tn=%ld fn=%ld)\n",
                tag, n, acc, p1, r1, f1, m.tp, m.fp, m.tn, m.fn);
}

}  // namespace

class RfEngineTest : public ::testing::Test {
protected:
    kadd_rf_model_t* model = nullptr;
    kadd_dfa_engine_t* dfa = nullptr;
    std::vector<float> feat;   /* 单样本特征，供性能测试复用 */

    void SetUp() override {
        ASSERT_EQ(kadd_rf_model_load(&model, kModelPath), KADD_SUCCESS)
            << "读不到模型 " << kModelPath;
        dfa = make_dfa();
        ASSERT_NE(dfa, nullptr) << "DFA 创建失败（assets/data 缺表？）";
        std::vector<int> tokens = {7, 11, 8, 12, 8, 11, 8};
        feat = to_features(tokens);
    }
    void TearDown() override {
        kadd_rf_model_free(model);
        kadd_dfa_engine_free(dfa);
    }
};

TEST_F(RfEngineTest, LoadModel) {
    float prob = -1.0f;
    ASSERT_EQ(kadd_rf_model_predict(model, feat.data(), &prob), KADD_SUCCESS);
    EXPECT_GE(prob, 0.0f);
    EXPECT_LE(prob, 1.0f);
}

TEST_F(RfEngineTest, AccuracyOnRealData) {
    std::vector<Sample> samples;
    ASSERT_TRUE(load_csv_samples(kCsvPath, dfa, samples))
        << "读不到训练 CSV " << kCsvPath;

    Metrics m;
    for (const auto& s : samples) {
        auto f = to_features(s.tokens);
        float prob = 0.0f;
        if (kadd_rf_model_predict(model, f.data(), &prob) != KADD_SUCCESS) continue;
        int pred = (prob >= 0.5f) ? 1 : 0;
        if (pred && s.label) ++m.tp; else if (pred && !s.label) ++m.fp;
        else if (!pred && !s.label) ++m.tn; else ++m.fn;
    }
    print_metrics("rf accuracy", m, (int)samples.size());
    EXPECT_GT((float)(m.tp + m.tn) / (float)samples.size(), 0.9f);
}

TEST_F(RfEngineTest, Performance) {
    const int kIterations = 20000;
    const int kWarmup = 200;
    for (int i = 0; i < kWarmup; ++i) { float p = 0; kadd_rf_model_predict(model, feat.data(), &p); }

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        float p = 0;
        ASSERT_EQ(kadd_rf_model_predict(model, feat.data(), &p), KADD_SUCCESS);
    }
    auto t1 = std::chrono::steady_clock::now();
    double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / kIterations;
    double ops = 1e6 / us;
    std::printf("[rf perf] 单样本平均 %.2f us, 吞吐 %.0f ops/sec (%d iter)\n",
                us, ops, kIterations);
    EXPECT_GT(ops, 0.0);
}

// 覆盖 kadd_rf_model_get_feature_dim
TEST_F(RfEngineTest, GetFeatureDim) {
    int dim = kadd_rf_model_get_feature_dim(model);
    EXPECT_EQ(dim, kFeatureDim);
    EXPECT_EQ(kadd_rf_model_get_feature_dim(nullptr), KADD_ERROR_NULL_PTR);
}

// 覆盖 kadd_rf_model_predict_batch
TEST_F(RfEngineTest, PredictBatch) {
    // 用 3 个样本测试批量预测
    std::vector<float> batch_features(3 * kFeatureDim, 0.0f);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < kFeatureDim; ++j) {
            batch_features[i * kFeatureDim + j] = feat[j];
        }
    }
    std::vector<float> probs(3, 0.0f);
    ASSERT_EQ(kadd_rf_model_predict_batch(model, batch_features.data(), 3,
                                           probs.data(), 2), KADD_SUCCESS);
    for (int i = 0; i < 3; ++i) {
        EXPECT_GE(probs[i], 0.0f);
        EXPECT_LE(probs[i], 1.0f);
    }
}

// 覆盖 kadd_rf_model_predict 非有限特征值检查
TEST_F(RfEngineTest, PredictWithNaNFeature) {
    std::vector<float> f(kFeatureDim, 0.5f);
    f[0] = NAN;
    float prob = -1.0f;
    EXPECT_EQ(kadd_rf_model_predict(model, f.data(), &prob), KADD_ERROR_INVALID_PARAM);
}

TEST_F(RfEngineTest, PredictWithInfFeature) {
    std::vector<float> f(kFeatureDim, 0.5f);
    f[0] = INFINITY;
    float prob = -1.0f;
    EXPECT_EQ(kadd_rf_model_predict(model, f.data(), &prob), KADD_ERROR_INVALID_PARAM);
}

// 覆盖 kadd_rf_model_predict_batch 无效样本数检查
TEST_F(RfEngineTest, PredictBatch_InvalidSampleCount) {
    std::vector<float> features(3 * kFeatureDim, 0.0f);
    std::vector<float> probs(3, 0.0f);

    EXPECT_EQ(kadd_rf_model_predict_batch(model, features.data(), 0,
                                          probs.data(), 1), KADD_ERROR_INVALID_PARAM);
    EXPECT_EQ(kadd_rf_model_predict_batch(model, features.data(), -1,
                                          probs.data(), 1), KADD_ERROR_INVALID_PARAM);
}

// 覆盖 kadd_rf_model_predict_batch 无效批次大小回退（<=0 或 > 最大批次回退默认值）
TEST_F(RfEngineTest, PredictBatch_InvalidBatchSize) {
    std::vector<float> batch_features(3 * kFeatureDim, 0.0f);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < kFeatureDim; ++j) {
            batch_features[i * kFeatureDim + j] = feat[j];
        }
    }

    const int batch_sizes[] = {0, -1, 2000};
    for (int batch_size : batch_sizes) {
        std::vector<float> probs(3, 0.0f);
        ASSERT_EQ(kadd_rf_model_predict_batch(model, batch_features.data(), 3,
                                              probs.data(), batch_size), KADD_SUCCESS);
        for (int i = 0; i < 3; ++i) {
            EXPECT_GE(probs[i], 0.0f);
            EXPECT_LE(probs[i], 1.0f);
        }
    }
}

// 覆盖 kadd_rf_model_load 损坏文件加载失败路径
TEST_F(RfEngineTest, LoadCorruptModel) {
    std::ifstream in(kModelPath, std::ios::binary);
    ASSERT_TRUE(in.good()) << "读不到模型 " << kModelPath;
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
    in.close();
    ASSERT_GT(bytes.size(), 64u) << "模型文件过小";

    // 截断到一半 → 大小校验失败
    const char* trunc_path = "/tmp/kadd_test_rf_trunc.krf";
    {
        std::ofstream out(trunc_path, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size() / 2));
    }
    {
        kadd_rf_model_t* m = nullptr;
        EXPECT_NE(kadd_rf_model_load(&m, trunc_path), KADD_SUCCESS);
        EXPECT_EQ(m, nullptr);
    }

    // 翻转首字节（坏 magic）→ magic 校验失败
    const char* magic_path = "/tmp/kadd_test_rf_magic.krf";
    {
        std::vector<char> corrupted = bytes;
        corrupted[0] ^= 0xFF;
        std::ofstream out(magic_path, std::ios::binary | std::ios::trunc);
        out.write(corrupted.data(), static_cast<std::streamsize>(corrupted.size()));
    }
    {
        kadd_rf_model_t* m = nullptr;
        EXPECT_NE(kadd_rf_model_load(&m, magic_path), KADD_SUCCESS);
        EXPECT_EQ(m, nullptr);
    }

    // 空指针参数 → NULL_PTR
    EXPECT_EQ(kadd_rf_model_load(nullptr, kModelPath), KADD_ERROR_NULL_PTR);
    EXPECT_EQ(kadd_rf_model_load(&model, nullptr), KADD_ERROR_NULL_PTR);

    std::remove(trunc_path);
    std::remove(magic_path);
}

// 覆盖 kadd_rf_model_load 文件不存在路径
TEST_F(RfEngineTest, LoadNonExistentFile) {
    kadd_rf_model_t* m = nullptr;
    EXPECT_NE(kadd_rf_model_load(&m, "/nonexistent_model_xyz.krf"), KADD_SUCCESS);
    EXPECT_EQ(m, nullptr);
}

// 覆盖 kadd_rf_model_load 文件小于 header 大小（file_size < KADD_RF_HEADER_SIZE）
TEST_F(RfEngineTest, LoadShortFile) {
    const char* short_path = "/tmp/kadd_test_rf_short.krf";
    {
        std::ofstream out(short_path, std::ios::binary | std::ios::trunc);
        out.write("SHORT", 5);  // 5 字节 < 64 字节 header
    }
    kadd_rf_model_t* m = nullptr;
    int ret = kadd_rf_model_load(&m, short_path);
    EXPECT_NE(ret, KADD_SUCCESS) << "短文件加载应失败";
    EXPECT_EQ(m, nullptr);
    std::remove(short_path);
}

