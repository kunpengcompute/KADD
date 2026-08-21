// Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
/**
 * @file test_cnn_engine_gtest.cpp
 * @brief kadd_cnn_engine 鐪熷疄妯″瀷鎬ц兘 + 鍑嗙‘锟?gtest锟? *
 * 鍔犺浇 assets/model/model_tokens_dl_cnn.kcn锛岀敤鐪熷疄璁粌 CSV锛堢粡 DFA 鍒嗚瘝锟? * 娴嬪噯纭巼/绮剧‘锟?鍙洖锟?F1锛屽苟娴嬪崟鏍锋湰鎺ㄧ悊鍚炲悙閲忋€傛ā鍨嬭矾寰勫啓姝伙紝
 * 锟?copy_test_resources 澶嶅埗锟?build/tests/assets/锟? */

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
    "../../tools/model_train/train_data/total_training_dataset_v4_pinnacle_corrected.cleaned.csv";

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

/* tokens -> float[300]锛宲ad 锟?28锛岃秺锟?clamp 锟?28 */
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
            << "璇讳笉鍒版ā锟?" << kModelPath;
        dfa = make_dfa();
        ASSERT_NE(dfa, nullptr) << "DFA 鍒涘缓澶辫触锛坅ssets/data 缂鸿〃锛燂級";
    }
    void TearDown() override {
        kadd_cnn_model_free(model);
        kadd_dfa_engine_free(dfa);
    }
};

TEST_F(CnnEngineTest, LoadModel) {
    /* SetUp 宸叉垚鍔熷姞杞芥ā鍨嬶紝姝ゅ浠呯‘璁ゅ彞鏌勯潪锟?+ 涓€娆℃帹鐞嗗湪 [0,1] */
    std::vector<float> seq(kSeq, (float)kPad);
    float prob = -1.0f;
    ASSERT_EQ(kadd_cnn_model_predict(model, seq.data(), &prob), KADD_SUCCESS);
    EXPECT_GE(prob, 0.0f);
    EXPECT_LE(prob, 1.0f);
}

TEST_F(CnnEngineTest, AccuracyOnRealData) {
    std::vector<Sample> samples;
    ASSERT_TRUE(load_csv_samples(kCsvPath, dfa, samples))
        << "璇讳笉鍒拌锟?CSV " << kCsvPath;

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
    /* 妯″瀷搴旀槑鏄句紭浜庨殢鏈虹寽娴嬶紙>0.9锛夛紝鍏峰疄鐢ㄤ环锟?*/
    EXPECT_GT((float)(m.tp + m.tn) / (float)samples.size(), 0.9f);
}

TEST_F(CnnEngineTest, Performance) {
    const int kIterations = 2000;
    const int kWarmup = 50;
    /* 鐢ㄧ湡瀹炵煭搴忓垪锛堥潪锟?pad锛夋祴锛岄伩锟?kadd_cnn 鐭簭鍒楄鍓湪 active=0 锟?     * 鍙畻 1 涓獥鍙ｈ€岃櫄浣庘€斺€斾笌 demo 鐨勭湡瀹炴牱鏈熀鍑嗗榻愶拷?*/
    std::vector<Sample> samples;
    ASSERT_TRUE(load_csv_samples(kCsvPath, dfa, samples));
    auto seq = to_seq(samples[0].tokens);

    for (int i = 0; i < kWarmup; ++i) { float p = 0; kadd_cnn_model_predict(model, seq.data(), &p); }

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        float p = 0;
        ASSERT_EQ(kadd_cnn_model_predict(model, seq.data(), &p), KADD_SUCCESS);
    }
    auto t1 = std::chrono::steady_clock::now();
    double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / kIterations;
    double ops = 1e6 / us;
    std::printf("[cnn perf] 鍗曟牱鏈钩锟?%.2f us, 鍚炲悙 %.0f ops/sec (%d iter, real sample)\n",
                us, ops, kIterations);
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