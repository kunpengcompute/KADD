/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *
 * @file test_rf_engine_gtest.cpp
 * @brief Native random forest engine Google Test tests.
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

#include "kadd_errors.h"
#include "kadd_rf_engine.h"

namespace {

constexpr uint32_t kHeaderSize = 64U;
constexpr uint32_t kNodeSize = 32U;
constexpr uint32_t kFeatureCount = 2U;
constexpr float kProbabilityTolerance = 1.0e-6F;

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

void AppendF64(std::vector<unsigned char> &bytes, double value)
{
    uint64_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    AppendU64(bytes, bits);
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

void AppendNode(std::vector<unsigned char> &payload, int32_t feature,
                uint32_t left, uint32_t right, double threshold,
                double probability)
{
    AppendU32(payload, static_cast<uint32_t>(feature));
    AppendU32(payload, left);
    AppendU32(payload, right);
    AppendU32(payload, 0U);
    AppendF64(payload, threshold);
    AppendF64(payload, probability);
}

std::vector<unsigned char> BuildModelBytes()
{
    std::vector<unsigned char> payload;
    AppendU32(payload, 0U);
    AppendU32(payload, 3U);
    AppendNode(payload, 0, 1U, 2U, 0.5, 0.0);
    AppendNode(payload, -1, UINT32_MAX, UINT32_MAX, 0.0, 0.25);
    AppendNode(payload, -1, UINT32_MAX, UINT32_MAX, 0.0, 0.75);

    std::vector<unsigned char> model = {'K', 'A', 'D', 'D', 'R', 'F', '1', '\0'};
    AppendU32(model, 1U);
    AppendU32(model, kHeaderSize);
    AppendU32(model, kNodeSize);
    AppendU32(model, kFeatureCount);
    AppendU32(model, 2U);
    AppendU32(model, 1U);
    AppendU32(model, 1U);
    AppendU32(model, 3U);
    AppendU32(model, 0U);
    AppendU64(model, payload.size());
    AppendU32(model, Crc32(payload));
    AppendU32(model, 0U);
    AppendU32(model, 0U);
    model.insert(model.end(), payload.begin(), payload.end());
    return model;
}

std::string WriteTempFile(const std::vector<unsigned char> &bytes)
{
    char path[] = "/tmp/kadd_rf_test_XXXXXX";
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

class RfEngineTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        model_path_ = WriteTempFile(BuildModelBytes());
        ASSERT_FALSE(model_path_.empty());
        ASSERT_EQ(kadd_rf_model_load(&model_, model_path_.c_str()), KADD_SUCCESS);
        ASSERT_NE(model_, nullptr);
    }

    void TearDown() override
    {
        kadd_rf_model_free(model_);
        if (!model_path_.empty()) {
            std::remove(model_path_.c_str());
        }
    }

    kadd_rf_model_t *model_ = nullptr;
    std::string model_path_;
};

TEST(RfModelFileTest, IdentifiesMagicAndRejectsInvalidPaths)
{
    const std::string valid_path = WriteTempFile(BuildModelBytes());
    ASSERT_FALSE(valid_path.empty());
    EXPECT_NE(kadd_rf_model_is_file(valid_path.c_str()), 0);
    EXPECT_EQ(kadd_rf_model_is_file(nullptr), 0);
    EXPECT_EQ(kadd_rf_model_is_file("/tmp/kadd_rf_missing_model"), 0);

    std::vector<unsigned char> invalid = BuildModelBytes();
    invalid[0] = 'X';
    const std::string invalid_path = WriteTempFile(invalid);
    ASSERT_FALSE(invalid_path.empty());
    EXPECT_EQ(kadd_rf_model_is_file(invalid_path.c_str()), 0);

    std::remove(valid_path.c_str());
    std::remove(invalid_path.c_str());
}

TEST(RfModelLoadTest, ValidatesArgumentsAndCorruptModels)
{
    kadd_rf_model_t *model = reinterpret_cast<kadd_rf_model_t *>(1);
    EXPECT_EQ(kadd_rf_model_load(nullptr, "unused"), KADD_ERROR_NULL_PTR);
    EXPECT_EQ(kadd_rf_model_load(&model, nullptr), KADD_ERROR_NULL_PTR);
    EXPECT_EQ(kadd_rf_model_load(&model, "/tmp/kadd_rf_missing_model"),
              KADD_ERROR_FILE_OPERATION_FAILED);
    EXPECT_EQ(model, nullptr);

    std::vector<unsigned char> corrupt = BuildModelBytes();
    corrupt.back() ^= 1U;
    const std::string corrupt_path = WriteTempFile(corrupt);
    ASSERT_FALSE(corrupt_path.empty());
    EXPECT_EQ(kadd_rf_model_load(&model, corrupt_path.c_str()),
              KADD_ERROR_MODEL_LOAD_FAILED);
    EXPECT_EQ(model, nullptr);
    std::remove(corrupt_path.c_str());
}

TEST_F(RfEngineTest, PredictsBothTreeBranches)
{
    const float left_features[kFeatureCount] = {0.5F, 10.0F};
    const float right_features[kFeatureCount] = {0.5001F, -10.0F};
    float probability = 0.0F;

    EXPECT_EQ(kadd_rf_model_predict(model_, left_features, &probability),
              KADD_SUCCESS);
    EXPECT_NEAR(probability, 0.25F, kProbabilityTolerance);
    EXPECT_EQ(kadd_rf_model_predict(model_, right_features, &probability),
              KADD_SUCCESS);
    EXPECT_NEAR(probability, 0.75F, kProbabilityTolerance);
}

TEST_F(RfEngineTest, BatchMatchesSinglePrediction)
{
    const std::vector<float> features = {
        0.0F, 1.0F,
        1.0F, 2.0F,
        -1.0F, 3.0F
    };
    float expected[3] = {};
    float actual[3] = {};
    for (int index = 0; index < 3; ++index) {
        ASSERT_EQ(kadd_rf_model_predict(
                      model_, features.data() + index * kFeatureCount,
                      &expected[index]),
                  KADD_SUCCESS);
    }

    EXPECT_EQ(kadd_rf_model_predict_batch(model_, features.data(), 3,
                                          actual, 2),
              KADD_SUCCESS);
    for (int index = 0; index < 3; ++index) {
        EXPECT_FLOAT_EQ(actual[index], expected[index]);
    }
    EXPECT_EQ(kadd_rf_model_predict_batch(model_, features.data(), 3,
                                          actual, 2048),
              KADD_SUCCESS);
}

TEST_F(RfEngineTest, ValidatesPredictionArguments)
{
    const float features[kFeatureCount] = {};
    float probability = 0.0F;
    EXPECT_EQ(kadd_rf_model_predict(nullptr, features, &probability),
              KADD_ERROR_NULL_PTR);
    EXPECT_EQ(kadd_rf_model_predict(model_, nullptr, &probability),
              KADD_ERROR_NULL_PTR);
    EXPECT_EQ(kadd_rf_model_predict(model_, features, nullptr),
              KADD_ERROR_NULL_PTR);
    EXPECT_EQ(kadd_rf_model_predict_batch(nullptr, features, 1,
                                          &probability, 1),
              KADD_ERROR_NULL_PTR);
    EXPECT_EQ(kadd_rf_model_predict_batch(model_, nullptr, 1,
                                          &probability, 1),
              KADD_ERROR_NULL_PTR);
    EXPECT_EQ(kadd_rf_model_predict_batch(model_, features, 1, nullptr, 1),
              KADD_ERROR_NULL_PTR);
    EXPECT_EQ(kadd_rf_model_predict_batch(model_, features, 0,
                                          &probability, 1),
              KADD_ERROR_INVALID_PARAM);
    EXPECT_EQ(kadd_rf_model_predict_batch(model_, features, 1,
                                          &probability, 0),
              KADD_ERROR_INVALID_PARAM);
}

TEST_F(RfEngineTest, ReportsRuntimeMetadata)
{
    EXPECT_EQ(kadd_rf_model_get_feature_dim(model_),
              static_cast<int>(kFeatureCount));
    EXPECT_EQ(kadd_rf_model_get_feature_dim(nullptr), KADD_ERROR_NULL_PTR);
    EXPECT_TRUE(std::strcmp(kadd_rf_runtime_backend(), "scalar") == 0 ||
                std::strcmp(kadd_rf_runtime_backend(), "sve") == 0);
    EXPECT_NE(kadd_rf_runtime_kernel(), nullptr);
    EXPECT_GE(kadd_rf_runtime_vector_bits(), 0);
    kadd_rf_model_free(nullptr);
}

}  // namespace
