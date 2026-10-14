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
/* [FIX REPORT-2026-0000028] 用于在测试内构造已签名的 .krf 模型 */
#include "kadd_hmac_sha256.h"
#include "test_model_hmac_key.h"   /* [FIX REPORT-2026-0000028] 注入 .krf/.kcn 共用 HMAC 开发密钥 */

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>


namespace {

constexpr int kSeq = 300;
constexpr int kFeatureDim = 28;
constexpr int kWindow = 5;

/* 数据集最低可用样本数，含义同 test_cnn_engine_gtest.cpp */
constexpr size_t kMinSamples = 10;

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
        kadd_rf_model_free(&model);
        kadd_dfa_engine_free(&dfa);
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

    /* 数据集样本不足时跳过（典型为 Git LFS 未拉取，train_data.csv 只剩指针文本）。
     * 否则 n=2 会让下面的准确率断言退化成无意义的空转通过。 */
    const std::string dataset_skip = check_dataset_size(kCsvPath, samples, kMinSamples);
    if (!dataset_skip.empty()) GTEST_SKIP() << dataset_skip;

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

// 覆盖 kadd_rf_model_predict_batch 非有限特征值检查
// （SVE 原生后端与非 SVE 回退后端的返回码必须一致，且失败时不得写输出）
TEST_F(RfEngineTest, PredictBatchWithNaNFeature) {
    std::vector<float> features(3 * kFeatureDim, 0.5f);
    std::vector<float> probs(3, -1.0f);          // 哨兵：校验失败时不得被覆盖
    features[2 * kFeatureDim] = NAN;             // 非法值放在最后一个分片（batch_size=2）
    EXPECT_EQ(kadd_rf_model_predict_batch(model, features.data(), 3,
                                          probs.data(), 2), KADD_ERROR_INVALID_PARAM);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(probs[i], -1.0f) << "非法输入时不得写出任何概率";
    }
}

TEST_F(RfEngineTest, PredictBatchWithInfFeature) {
    std::vector<float> features(3 * kFeatureDim, 0.5f);
    std::vector<float> probs(3, -1.0f);
    features[kFeatureDim + 1] = INFINITY;        // 非法值放在第 2 行
    EXPECT_EQ(kadd_rf_model_predict_batch(model, features.data(), 3,
                                          probs.data(), 2), KADD_ERROR_INVALID_PARAM);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(probs[i], -1.0f) << "非法输入时不得写出任何概率";
    }
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

/* ==========================================================================
 * [FIX REPORT-2026-0000028] .krf 模型真实性认证（HMAC-SHA256）回归测试
 *
 * 背景：原格式仅有 CRC32（多项式 0xEDB88320，公开且无需密钥）作为完整性控制，
 * 只能检测意外损坏、不能抵抗蓄意篡改。持有模型文件写权限者可把叶节点
 * positive_probability 全部置 0.0 再重算 CRC32，加载器会照单全收，从而完全绕过
 * SQLi 检测（或全置 1.0 造成误报 DoS）。现在 HMAC-SHA256 认证是强制项，
 * 未签名的旧格式模型一律拒绝加载。
 *
 * 这些用例使用独立 fixture（不复用 RfEngineTest），因为 RfEngineTest::SetUp()
 * 会加载仓库资产，而下面多个用例需要临时改写 KADD_MODEL_HMAC_KEY。
 * HmacKeyGuard 保证用例结束后把环境变量恢复原状（即 test_model_hmac_key.h
 * 在 main() 之前注入的开发密钥），避免污染其它用例。
 * ========================================================================== */
namespace {

constexpr const char* kHmacKeyEnv = "KADD_MODEL_HMAC_KEY";
/* 与 test_model_hmac_key.h 注入的开发密钥一致：仓库资产正是用它签名的 */
constexpr const char* kDevKeyHex = kadd_test::kModelHmacDevKeyHex;
constexpr const char* kWrongKeyHex =
    "ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100";
constexpr const char* kSignedPath = "kadd_test_rf_signed.krf";
constexpr const char* kUnsignedPath = "kadd_test_rf_unsigned.krf";
constexpr const char* kTamperedPath = "kadd_test_rf_tampered.krf";
constexpr uint32_t kFlagHmacSha256 = 0x00000001u;
constexpr size_t kRfHeaderSize = 64u;

class HmacKeyGuard {
public:
    explicit HmacKeyGuard(const char* value) {
        const char* prev = getenv(kHmacKeyEnv);
        had_prev_ = (prev != nullptr);
        if (had_prev_) {
            prev_ = prev;
        }
        Apply(value);
    }
    ~HmacKeyGuard() { Apply(had_prev_ ? prev_.c_str() : nullptr); }

private:
    static void Apply(const char* value) {
        if (value != nullptr) {
#ifdef _WIN32
            const std::string entry = std::string(kHmacKeyEnv) + "=" + value;
            _putenv(entry.c_str());
#else
            setenv(kHmacKeyEnv, value, 1);
#endif
        } else {
#ifdef _WIN32
            _putenv((std::string(kHmacKeyEnv) + "=").c_str());
#else
            unsetenv(kHmacKeyEnv);
#endif
        }
    }
    bool had_prev_ = false;
    std::string prev_;
};

std::vector<char> ReadModel() {
    std::ifstream in(kModelPath, std::ios::binary);
    if (!in.good()) {
        return {};
    }
    return std::vector<char>((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
}

void WriteModel(const char* path, const std::vector<char>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

/* .krf 头部字段为小端；逐字节组装以不依赖宿主字节序 */
uint64_t ReadLe64(const std::vector<char>& b, size_t off) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) {
        v = (v << 8) | static_cast<uint8_t>(b[off + static_cast<size_t>(i)]);
    }
    return v;
}
uint32_t ReadLe32(const std::vector<char>& b, size_t off) {
    uint32_t v = 0;
    for (int i = 3; i >= 0; --i) {
        v = (v << 8) | static_cast<uint8_t>(b[off + static_cast<size_t>(i)]);
    }
    return v;
}
void WriteLe32(std::vector<char>& b, size_t off, uint32_t v) {
    for (uint32_t i = 0; i < 4u; ++i) {
        b[off + i] = static_cast<char>((v >> (8u * i)) & 0xFFu);
    }
}

/* 被认证区域长度 = header + payload，以头部声明的 payload_size 为准 */
size_t SignedRegionSize(const std::vector<char>& raw) {
    return kRfHeaderSize + static_cast<size_t>(ReadLe64(raw, 44));
}

/* 构造已签名模型：flags bit0 置位，尾部追加 HMAC-SHA256(header || payload)。
 * 仓库资产本身已是已签名格式，故先按 payload_size 剥离既有尾部再重新签名。 */
std::vector<char> MakeSigned(const std::vector<char>& raw, const char* key_hex) {
    std::vector<char> out = raw;
    if (out.size() <= kRfHeaderSize) {
        return out;
    }
    uint8_t key[KADD_SHA256_DIGEST_SIZE];
    if (kadd_hex_to_bytes(key_hex, strlen(key_hex), key, sizeof(key)) != 0) {
        return out;
    }
    out.resize(SignedRegionSize(raw));
    WriteLe32(out, 40, ReadLe32(out, 40) | kFlagHmacSha256);

    uint8_t mac[KADD_SHA256_DIGEST_SIZE];
    kadd_hmac_sha256(key, sizeof(key),
                     reinterpret_cast<const uint8_t*>(out.data()), out.size(), mac);
    out.insert(out.end(), reinterpret_cast<const char*>(mac),
               reinterpret_cast<const char*>(mac) + KADD_SHA256_DIGEST_SIZE);
    return out;
}

/* 构造未签名的旧格式模型：去掉 HMAC 尾部并清掉 flags bit0，
 * 用于验证"旧行为不再支持" */
std::vector<char> MakeUnsigned(const std::vector<char>& raw) {
    std::vector<char> out = raw;
    if (out.size() <= kRfHeaderSize) {
        return out;
    }
    out.resize(SignedRegionSize(raw));
    WriteLe32(out, 40, ReadLe32(out, 40) & ~kFlagHmacSha256);
    return out;
}

}  // namespace

/* 仓库资产已由 rf_sign.py 用开发密钥签名；密钥经 test_model_hmac_key.h 注入 -> 加载成功 */
TEST(RfHmacTest, SignedAssetLoadsWithDevKey) {
    kadd_rf_model_t* m = nullptr;
    ASSERT_EQ(kadd_rf_model_load(&m, kModelPath), KADD_SUCCESS)
        << "仓库资产应已签名，且开发密钥应由 test_model_hmac_key.h 注入";
    ASSERT_NE(m, nullptr);
    EXPECT_GT(kadd_rf_model_get_feature_dim(m), 0);
    kadd_rf_model_free(&m);
}

/* 已签名模型 + 未配置密钥 -> 拒绝（无法验证就不加载，fail-closed） */
TEST(RfHmacTest, SignedAssetRejectedWithoutKey) {
    HmacKeyGuard guard(nullptr);
    kadd_rf_model_t* m = nullptr;
    EXPECT_NE(kadd_rf_model_load(&m, kModelPath), KADD_SUCCESS);
    EXPECT_EQ(m, nullptr) << "加载失败时不得输出句柄";
}

/* 未签名旧格式模型 + 已配置密钥 -> 拒绝（旧行为不再支持） */
TEST(RfHmacTest, UnsignedModelRejectedWithKey) {
    std::vector<char> raw = ReadModel();
    ASSERT_GT(raw.size(), kRfHeaderSize) << "读不到模型 " << kModelPath;
    WriteModel(kUnsignedPath, MakeUnsigned(raw));

    HmacKeyGuard guard(kDevKeyHex);
    kadd_rf_model_t* m = nullptr;
    EXPECT_NE(kadd_rf_model_load(&m, kUnsignedPath), KADD_SUCCESS);
    EXPECT_EQ(m, nullptr);
    std::remove(kUnsignedPath);
}

/* 未签名旧格式模型 + 未配置密钥 -> 同样拒绝（不存在任何放行未签名模型的路径） */
TEST(RfHmacTest, UnsignedModelRejectedWithoutKey) {
    std::vector<char> raw = ReadModel();
    ASSERT_GT(raw.size(), kRfHeaderSize);
    WriteModel(kUnsignedPath, MakeUnsigned(raw));

    HmacKeyGuard guard(nullptr);
    kadd_rf_model_t* m = nullptr;
    EXPECT_NE(kadd_rf_model_load(&m, kUnsignedPath), KADD_SUCCESS);
    EXPECT_EQ(m, nullptr);
    std::remove(kUnsignedPath);
}

/* 已签名模型 + 错误密钥 -> 拒绝。
 * 此时文件完好、CRC32 有效，能拒绝它的只有 HMAC，
 * 因此本用例证明认证确实生效，而非由 CRC 顺带拦下。 */
TEST(RfHmacTest, SignedModelRejectedWithWrongKey) {
    std::vector<char> raw = ReadModel();
    ASSERT_GT(raw.size(), kRfHeaderSize);
    WriteModel(kSignedPath, MakeSigned(raw, kDevKeyHex));

    HmacKeyGuard guard(kWrongKeyHex);
    kadd_rf_model_t* m = nullptr;
    EXPECT_NE(kadd_rf_model_load(&m, kSignedPath), KADD_SUCCESS);
    EXPECT_EQ(m, nullptr);
    std::remove(kSignedPath);
}

/* 密钥格式非法 -> 拒绝（配置错误不得静默降级为"无密钥"） */
TEST(RfHmacTest, MalformedKeyRejected) {
    HmacKeyGuard guard("not-a-valid-hex-key");
    kadd_rf_model_t* m = nullptr;
    EXPECT_NE(kadd_rf_model_load(&m, kModelPath), KADD_SUCCESS);
    EXPECT_EQ(m, nullptr);
}

/* 已签名模型被篡改（复现原报告 PoC 的篡改位置）+ 正确密钥 -> 拒绝。
 * 攻击者不知道密钥，无法在改写叶节点概率后重新生成有效 HMAC。 */
TEST(RfHmacTest, TamperedSignedModelRejected) {
    std::vector<char> raw = ReadModel();
    ASSERT_GT(raw.size(), kRfHeaderSize);
    std::vector<char> tampered = MakeSigned(raw, kDevKeyHex);

    /* payload 布局为 tree_offsets((tree_count+1)*4) 之后紧跟 node 数组，
     * 每个 node 32 字节，positive_probability 位于 node 内偏移 24 处 */
    const uint32_t tree_count = ReadLe32(raw, 32);
    const size_t nodes_off = kRfHeaderSize + (static_cast<size_t>(tree_count) + 1u) * 4u;
    ASSERT_GT(tampered.size(), nodes_off + 32u);
    tampered[nodes_off + 24] = static_cast<char>(tampered[nodes_off + 24] ^ 0x01);
    WriteModel(kTamperedPath, tampered);

    HmacKeyGuard guard(kDevKeyHex);
    kadd_rf_model_t* m = nullptr;
    EXPECT_NE(kadd_rf_model_load(&m, kTamperedPath), KADD_SUCCESS);
    EXPECT_EQ(m, nullptr);
    std::remove(kTamperedPath);
}

/* 尾部长度必须恰好 32 字节：截断 HMAC 尾部 -> 拒绝 */
TEST(RfHmacTest, TruncatedHmacTrailerRejected) {
    std::vector<char> raw = ReadModel();
    ASSERT_GT(raw.size(), kRfHeaderSize);
    std::vector<char> signed_model = MakeSigned(raw, kDevKeyHex);
    signed_model.resize(signed_model.size() - 8);   /* 少 8 字节 */
    WriteModel(kTamperedPath, signed_model);

    HmacKeyGuard guard(kDevKeyHex);
    kadd_rf_model_t* m = nullptr;
    EXPECT_NE(kadd_rf_model_load(&m, kTamperedPath), KADD_SUCCESS);
    EXPECT_EQ(m, nullptr);
    std::remove(kTamperedPath);
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

