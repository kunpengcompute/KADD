// Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
/**
 * @file test_cnn_engine_gtest.cpp
 * @brief kadd_cnn_engine 真实模型性能 + 准确率 gtest。
 *
 * 加载 assets/model/model_tokens_dl_cnn.kcn，用真实训练 CSV（经 DFA 分词）
 * 测准确率/精确率/召回率/F1，并测单样本推理吞吐量。模型路径写死，
 * 由 copy_test_resources 复制到 build/tests/assets/。
 */

#include "kadd_cnn_engine.h"
#include "kadd_dfa_engine.h"
#include "test_model_data.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <vector>
#include <algorithm>

namespace {

constexpr int kSeq  = 300;
constexpr int kVocab = 29;
constexpr int kPad   = 28;

constexpr const char* kModelPath = "assets/model/model_tokens_dl_cnn.kcn";
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

/* tokens -> float[300]，pad 补 28，越界 clamp 到 28 */
std::vector<float> to_seq(const std::vector<int>& tokens) {
    std::vector<float> seq(kSeq, static_cast<float>(kPad));
    size_t n = std::min(tokens.size(), (size_t)kSeq);
    for (size_t i = 0; i < n; ++i) {
        int t = tokens[i];
        if (t < 0 || t >= kVocab) t = kPad;
        seq[i] = static_cast<float>(t);
    }
    return seq;
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

class CnnEngineTest : public ::testing::Test {
protected:
    kadd_cnn_model_t* model = nullptr;
    kadd_dfa_engine_t* dfa = nullptr;

    void SetUp() override {
        ASSERT_EQ(kadd_cnn_model_load(&model, kModelPath), KADD_SUCCESS)
            << "读不到模型 " << kModelPath;
        dfa = make_dfa();
        ASSERT_NE(dfa, nullptr) << "DFA 创建失败（assets/data 缺表？）";
    }
    void TearDown() override {
        kadd_cnn_model_free(model);
        kadd_dfa_engine_free(dfa);
    }
};

TEST_F(CnnEngineTest, LoadModel) {
    /* SetUp 已成功加载模型，此处仅确认句柄非空 + 一次推理在 [0,1] */
    std::vector<float> seq(kSeq, (float)kPad);
    float prob = -1.0f;
    ASSERT_EQ(kadd_cnn_model_predict(model, seq.data(), &prob), KADD_SUCCESS);
    EXPECT_GE(prob, 0.0f);
    EXPECT_LE(prob, 1.0f);
}

TEST_F(CnnEngineTest, AccuracyOnRealData) {
    std::vector<Sample> samples;
    ASSERT_TRUE(load_csv_samples(kCsvPath, dfa, samples))
        << "读不到训练 CSV " << kCsvPath;

    Metrics m;
    for (const auto& s : samples) {
        auto seq = to_seq(s.tokens);
        float prob = 0.0f;
        if (kadd_cnn_model_predict(model, seq.data(), &prob) != KADD_SUCCESS) continue;
        int pred = (prob >= 0.5f) ? 1 : 0;
        if (pred && s.label) ++m.tp; else if (pred && !s.label) ++m.fp;
        else if (!pred && !s.label) ++m.tn; else ++m.fn;
    }
    print_metrics("cnn accuracy", m, (int)samples.size());
    /* 模型应明显优于随机猜测（>0.9），具实用价值 */
    EXPECT_GT((float)(m.tp + m.tn) / (float)samples.size(), 0.9f);
}

TEST_F(CnnEngineTest, Performance) {
    const int kIterations = 2000;
    const int kWarmup = 50;
    const int kSampleCount = 10;  /* 使用多条样本取平均，避免单条样本长度影响 */
    /* 用真实短序列（非全 pad）测，避免 kadd_cnn 短序列裁剪在 active=0 时
     * 只算 1 个窗口而虚低——与 demo 的真实样本基准对齐。 */
    std::vector<Sample> samples;
    ASSERT_TRUE(load_csv_samples(kCsvPath, dfa, samples));
    ASSERT_GE((int)samples.size(), kSampleCount);

    /* 准备多条样本 */
    std::vector<std::vector<float>> seqs;
    for (int i = 0; i < kSampleCount; ++i) {
        seqs.push_back(to_seq(samples[i].tokens));
    }

    /* warmup */
    for (int i = 0; i < kWarmup; ++i) {
        float p = 0;
        kadd_cnn_model_predict(model, seqs[i % kSampleCount].data(), &p);
    }

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        float p = 0;
        ASSERT_EQ(kadd_cnn_model_predict(model, seqs[i % kSampleCount].data(), &p), KADD_SUCCESS);
    }
    auto t1 = std::chrono::steady_clock::now();
    double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / kIterations;
    double ops = 1e6 / us;
    std::printf("[cnn perf] %d 样本平均 %.2f us, 吞吐 %.0f ops/sec (%d iter)\n",
                kSampleCount, us, ops, kIterations);
    EXPECT_GT(ops, 0.0);
}

// 覆盖 kadd_cnn_model_predict_batch
TEST_F(CnnEngineTest, PredictBatch) {
    std::vector<Sample> samples;
    ASSERT_TRUE(load_csv_samples(kCsvPath, dfa, samples));
    const int kBatchCount = 3;
    std::vector<float> batch(kBatchCount * kSeq, 0.0f);
    for (int i = 0; i < kBatchCount; ++i) {
        auto seq = to_seq(samples[i].tokens);
        std::copy(seq.begin(), seq.end(), batch.begin() + i * kSeq);
    }
    std::vector<float> probs(kBatchCount, 0.0f);
    ASSERT_EQ(kadd_cnn_model_predict_batch(model, batch.data(), kBatchCount,
                                           probs.data(), 2), KADD_SUCCESS);
    for (int i = 0; i < kBatchCount; ++i) {
        EXPECT_GE(probs[i], 0.0f);
        EXPECT_LE(probs[i], 1.0f);
    }
}

// 覆盖 kadd_cnn_model_get_feature_dim
TEST_F(CnnEngineTest, GetFeatureDim) {
    int dim = kadd_cnn_model_get_feature_dim(model);
    EXPECT_EQ(dim, 300);  /* KADD_CNN_SEQUENCE_LENGTH */
}

TEST_F(CnnEngineTest, GetFeatureDimNullModel) {
    EXPECT_EQ(kadd_cnn_model_get_feature_dim(nullptr), KADD_ERROR_NULL_PTR);
}
