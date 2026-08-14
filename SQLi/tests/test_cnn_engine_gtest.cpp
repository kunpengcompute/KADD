/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *
 * @file test_cnn_engine_gtest.cpp
 * @brief Native TextCNN engine Google Test tests.
 */

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include <unistd.h>

#include "kadd_cnn_engine.h"
#include "kadd_errors.h"

namespace {

constexpr uint32_t kHeaderSize = 64U;
constexpr uint32_t kSequenceLength = 300U;
constexpr uint32_t kVocabSize = 29U;
constexpr uint32_t kEmbeddingDim = 32U;
constexpr uint32_t kChannelCount = 64U;
constexpr uint32_t kBranchCount = 4U;
constexpr uint32_t kHiddenDim = 64U;
constexpr uint32_t kClassCount = 2U;
constexpr uint32_t kPadId = 28U;
constexpr size_t kPayloadFloatCount =
    kVocabSize * kEmbeddingDim +
    kChannelCount * kEmbeddingDim * (2U + 3U + 4U + 5U) +
    kChannelCount * kBranchCount +
    kHiddenDim * kChannelCount * kBranchCount +
    kHiddenDim + kClassCount * kHiddenDim + kClassCount;

void AppendU32(std::vector<unsigned char> &bytes, uint32_t value)
{
    for (unsigned int shift = 0U; shift < 32U; shift += 8U) {
        bytes.push_back(static_cast<unsigned char>(value >> shift));
    }
}

void AppendU64(std::vector<unsigned char> &bytes, uint64_t value)
{
    for (unsigned int shift = 0U; shift < 64U; shift += 8U) {
        bytes.push_back(static_cast<unsigned char>(value >> shift));
    }
}

void AppendF32(std::vector<unsigned char> &bytes, float value)
{
    uint32_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    AppendU32(bytes, bits);
}

uint32_t Crc32(const std::vector<unsigned char> &bytes)
{
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    for (unsigned char byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t mask = static_cast<uint32_t>(
                -static_cast<int32_t>(crc & 1U));
            crc = (crc >> 1U) ^ (UINT32_C(0xEDB88320) & mask);
        }
    }
    return ~crc;
}

std::vector<unsigned char> BuildModelBytes()
{
    std::vector<unsigned char> payload;
    payload.reserve(kPayloadFloatCount * sizeof(float));
    for (size_t index = 0; index < kPayloadFloatCount; ++index) {
        AppendF32(payload, index + 1U == kPayloadFloatCount ? 1.0F : 0.0F);
    }

    std::vector<unsigned char> model = {'K', 'A', 'D', 'D', 'C', 'N', '1', '\0'};
    AppendU32(model, 1U);
    AppendU32(model, kHeaderSize);
    AppendU32(model, kSequenceLength);
    AppendU32(model, kVocabSize);
    AppendU32(model, kEmbeddingDim);
    AppendU32(model, kChannelCount);
    AppendU32(model, kBranchCount);
    AppendU32(model, kHiddenDim);
    AppendU32(model, kClassCount);
    AppendU32(model, kPadId);
    AppendU64(model, payload.size());
    AppendU32(model, Crc32(payload));
    AppendU32(model, 0U);
    model.insert(model.end(), payload.begin(), payload.end());
    return model;
}

std::string WriteTempFile(const std::vector<unsigned char> &bytes)
{
    char path[] = "/tmp/kadd_cnn_test_XXXXXX";
    const int fd = mkstemp(path);
    if (fd < 0) {
        return "";
    }
    close(fd);

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!stream.good()) {
        stream.close();
        std::remove(path);
        return "";
    }
    return path;
}

class CnnEngineTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        model_path_ = WriteTempFile(BuildModelBytes());
        ASSERT_FALSE(model_path_.empty());
        ASSERT_EQ(kadd_cnn_model_load(&model_, model_path_.c_str()), KADD_SUCCESS);
        ASSERT_NE(model_, nullptr);
        tokens_.assign(kSequenceLength, static_cast<float>(kPadId));
    }

    void TearDown() override
    {
        kadd_cnn_model_free(model_);
        if (!model_path_.empty()) {
            std::remove(model_path_.c_str());
        }
    }

    kadd_cnn_model_t *model_ = nullptr;
    std::string model_path_;
    std::vector<float> tokens_;
};

TEST(CnnModelFileTest, IdentifiesMagicAndRejectsInvalidPaths)
{
    const std::string valid_path = WriteTempFile(BuildModelBytes());
    ASSERT_FALSE(valid_path.empty());
    EXPECT_NE(kadd_cnn_model_is_file(valid_path.c_str()), 0);
    EXPECT_EQ(kadd_cnn_model_is_file(nullptr), 0);
    EXPECT_EQ(kadd_cnn_model_is_file("/tmp/kadd_cnn_missing_model"), 0);

    std::vector<unsigned char> invalid = BuildModelBytes();
    invalid[0] = 'X';
    const std::string invalid_path = WriteTempFile(invalid);
    ASSERT_FALSE(invalid_path.empty());
    EXPECT_EQ(kadd_cnn_model_is_file(invalid_path.c_str()), 0);

    std::remove(valid_path.c_str());
    std::remove(invalid_path.c_str());
}

TEST(CnnModelLoadTest, ValidatesArgumentsAndCorruptModels)
{
    kadd_cnn_model_t *model = reinterpret_cast<kadd_cnn_model_t *>(1);
    EXPECT_EQ(kadd_cnn_model_load(nullptr, "unused"), KADD_ERROR_NULL_PTR);
    EXPECT_EQ(kadd_cnn_model_load(&model, nullptr), KADD_ERROR_NULL_PTR);
    EXPECT_EQ(kadd_cnn_model_load(&model, "/tmp/kadd_cnn_missing_model"),
              KADD_ERROR_FILE_OPERATION_FAILED);
    EXPECT_EQ(model, nullptr);

    std::vector<unsigned char> corrupt = BuildModelBytes();
    corrupt.back() ^= 1U;
    const std::string corrupt_path = WriteTempFile(corrupt);
    ASSERT_FALSE(corrupt_path.empty());
    EXPECT_EQ(kadd_cnn_model_load(&model, corrupt_path.c_str()),
              KADD_ERROR_MODEL_LOAD_FAILED);
    EXPECT_EQ(model, nullptr);
    std::remove(corrupt_path.c_str());
}

TEST_F(CnnEngineTest, PredictsExpectedProbability)
{
    float probability = 0.0F;
    EXPECT_EQ(kadd_cnn_model_predict(model_, tokens_.data(), &probability),
              KADD_SUCCESS);
    EXPECT_NEAR(probability, 1.0F / (1.0F + std::exp(-1.0F)), 1.0e-6F);

    tokens_[0] = 1.0F;
    EXPECT_EQ(kadd_cnn_model_predict(model_, tokens_.data(), &probability),
              KADD_SUCCESS);
    EXPECT_NEAR(probability, 1.0F / (1.0F + std::exp(-1.0F)), 1.0e-6F);
}

TEST_F(CnnEngineTest, RejectsInvalidTokenValues)
{
    float probability = 0.0F;
    tokens_[0] = 1.5F;
    EXPECT_EQ(kadd_cnn_model_predict(model_, tokens_.data(), &probability),
              KADD_ERROR_INVALID_PARAM);
    tokens_[0] = 29.0F;
    EXPECT_EQ(kadd_cnn_model_predict(model_, tokens_.data(), &probability),
              KADD_ERROR_INVALID_PARAM);
    tokens_[0] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(kadd_cnn_model_predict(model_, tokens_.data(), &probability),
              KADD_ERROR_INVALID_PARAM);
}

TEST_F(CnnEngineTest, BatchMatchesSinglePrediction)
{
    std::vector<float> batch(kSequenceLength * 3U,
                             static_cast<float>(kPadId));
    batch[0] = 1.0F;
    batch[kSequenceLength] = 2.0F;
    float expected[3] = {};
    float actual[3] = {};
    for (int index = 0; index < 3; ++index) {
        ASSERT_EQ(kadd_cnn_model_predict(
                      model_, batch.data() + index * kSequenceLength,
                      &expected[index]),
                  KADD_SUCCESS);
    }

    EXPECT_EQ(kadd_cnn_model_predict_batch(model_, batch.data(), 3,
                                           actual, 2),
              KADD_SUCCESS);
    for (int index = 0; index < 3; ++index) {
        EXPECT_FLOAT_EQ(actual[index], expected[index]);
    }
    EXPECT_EQ(kadd_cnn_model_predict_batch(model_, batch.data(), 3,
                                           actual, 2048),
              KADD_SUCCESS);
}

TEST_F(CnnEngineTest, ValidatesPredictionArguments)
{
    float probability = 0.0F;
    EXPECT_EQ(kadd_cnn_model_predict(nullptr, tokens_.data(), &probability),
              KADD_ERROR_NULL_PTR);
    EXPECT_EQ(kadd_cnn_model_predict(model_, nullptr, &probability),
              KADD_ERROR_NULL_PTR);
    EXPECT_EQ(kadd_cnn_model_predict(model_, tokens_.data(), nullptr),
              KADD_ERROR_NULL_PTR);
    EXPECT_EQ(kadd_cnn_model_predict_batch(nullptr, tokens_.data(), 1,
                                           &probability, 1),
              KADD_ERROR_NULL_PTR);
    EXPECT_EQ(kadd_cnn_model_predict_batch(model_, nullptr, 1,
                                           &probability, 1),
              KADD_ERROR_NULL_PTR);
    EXPECT_EQ(kadd_cnn_model_predict_batch(model_, tokens_.data(), 1,
                                           nullptr, 1),
              KADD_ERROR_NULL_PTR);
    EXPECT_EQ(kadd_cnn_model_predict_batch(model_, tokens_.data(), 0,
                                           &probability, 1),
              KADD_ERROR_INVALID_PARAM);
    EXPECT_EQ(kadd_cnn_model_predict_batch(model_, tokens_.data(), 1,
                                           &probability, 0),
              KADD_ERROR_INVALID_PARAM);
}

TEST_F(CnnEngineTest, ReportsRuntimeMetadata)
{
    EXPECT_EQ(kadd_cnn_model_get_feature_dim(model_),
              static_cast<int>(kSequenceLength));
    EXPECT_EQ(kadd_cnn_model_get_feature_dim(nullptr), KADD_ERROR_NULL_PTR);
    EXPECT_TRUE(std::strcmp(kadd_cnn_runtime_backend(), "scalar") == 0 ||
                std::strcmp(kadd_cnn_runtime_backend(), "sve") == 0);
    EXPECT_NE(kadd_cnn_runtime_kernel(), nullptr);
    EXPECT_GE(kadd_cnn_runtime_vector_bits(), 0);
    kadd_cnn_model_free(nullptr);
}

}  // namespace
