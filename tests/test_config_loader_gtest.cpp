/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *
 * @file test_config_loader_gtest.cpp
 * @brief 配置加载器 Google Test 测试
 *
 * 测试内容：
 * 1. 功能测试 - 正常加载、默认路径、部分配置
 * 2. 边界测试 - 空指针、空路径、损坏文件
 * 3. 错误路径测试 - JSON格式错误、缺失字段
 * 4. 覆盖率提升 - 覆盖所有默认值分支
 */

#include <gtest/gtest.h>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <regex>
#include <unistd.h>
#include "kadd_config_loader.h"
#include "kadd_errors.h"

constexpr int kDefaultFeatureDim = 28;
constexpr int kDefaultWindowSize = 5;
constexpr int kDefaultMaxInputLength = 300;
constexpr int kDefaultMaxOutputDim = 300;
constexpr int kMaxPathLength = 512;
constexpr int kTruncatedPathLength = 511;

constexpr int kDfaInputLen = 500;
constexpr int kDfaOutputDim = 400;
constexpr int kMlFeatureDim = 32;
constexpr int kMlWindowSize = 6;
constexpr int kSqliFeatureDim = 64;
constexpr int kSqliWindowSize = 8;
constexpr int kSqliInputLen = 600;
constexpr int kSqliOutputDim = 500;
constexpr int kFeatureFeatureDim = 128;
constexpr int kFeatureWindowSize = 10;

// ========== 测试辅助函数 ==========

/**
 * @brief 创建一个临时的测试配置文件
 */
static std::string CreateTestConfigFile(const char *content)
{
    char temp_template[] = "/tmp/kadd_config_XXXXXX";
    int fd = mkstemp(temp_template);
    if (fd == -1) {
        return "";
    }
    close(fd);
    std::string path = temp_template;

    std::ofstream file(path);
    file << content;
    file.close();

    return path;
}

/**
 * @brief 删除测试文件
 */
static void DeleteTestFile(const std::string &path)
{
    remove(path.c_str());
}

// ========== 1. 功能测试 ==========

// 验证完整配置的各模块字段
static void ValidateFullConfig(const kadd_config_t &config)
{
    EXPECT_STREQ(config.dfa.transition_table_path, "/path/to/transition.bin");
    EXPECT_STREQ(config.dfa.output_table_path, "/path/to/output.bin");
    EXPECT_STREQ(config.dfa.eod_table_path, "/path/to/eod.bin");
    EXPECT_EQ(config.dfa.max_input_length, kDfaInputLen);
    EXPECT_EQ(config.dfa.max_output_dim, kDfaOutputDim);

    EXPECT_STREQ(config.ml.model_path, "/path/to/model.onnx");
    EXPECT_EQ(config.ml.feature_dim, kMlFeatureDim);
    EXPECT_EQ(config.ml.window_size, kMlWindowSize);

    EXPECT_STREQ(config.sqli.transition_table_path, "/path/to/sqli_transition.bin");
    EXPECT_STREQ(config.sqli.ml_model_path, "/path/to/sqli_model.onnx");
    EXPECT_EQ(config.sqli.feature_dim, kSqliFeatureDim);
    EXPECT_EQ(config.sqli.window_size, kSqliWindowSize);
    EXPECT_EQ(config.sqli.max_input_length, kSqliInputLen);
    EXPECT_EQ(config.sqli.max_output_dim, kSqliOutputDim);

    EXPECT_STREQ(config.feature.ml_model_path, "/path/to/feature_model.onnx");
    EXPECT_EQ(config.feature.feature_dim, kFeatureFeatureDim);
    EXPECT_EQ(config.feature.window_size, kFeatureWindowSize);
}

TEST(ConfigLoaderTest, LoadValidConfig)
{
    const char *config_content = R"(
    {
        "kadd_dfa_engine": {
            "transition_table_path": "/path/to/transition.bin",
            "output_table_path": "/path/to/output.bin",
            "eod_table_path": "/path/to/eod.bin",
            "max_input_length": 500,
            "max_output_dim": 400
        },
        "kadd_ml_engine": {
            "model_path": "/path/to/model.onnx",
            "feature_dim": 32,
            "window_size": 6
        },
        "kadd_sqli_engine": {
            "transition_table_path": "/path/to/sqli_transition.bin",
            "output_table_path": "/path/to/sqli_output.bin",
            "eod_table_path": "/path/to/sqli_eod.bin",
            "ml_model_path": "/path/to/sqli_model.onnx",
            "feature_dim": 64,
            "window_size": 8,
            "max_input_length": 600,
            "max_output_dim": 500
        },
        "kadd_feature_engine": {
            "ml_model_path": "/path/to/feature_model.onnx",
            "feature_dim": 128,
            "window_size": 10
        }
    }
    )";

    std::string config_path = CreateTestConfigFile(config_content);
    kadd_config_t config;
    int ret = kadd_config_load(config_path.c_str(), &config);
    EXPECT_EQ(ret, 0) << "应该成功加载配置";

    ValidateFullConfig(config);
    DeleteTestFile(config_path);
}

TEST(ConfigLoaderTest, LoadWithDefaultPath)
{
    // 测试使用默认路径 "config.json"
    kadd_config_t config;
    int ret = kadd_config_load(nullptr, &config);

    // 如果当前目录有 config.json，应该成功；否则失败
    // 这里只验证函数不会崩溃
    if (ret == 0) {
        // 成功加载了默认配置文件
        EXPECT_GT(strlen(config.dfa.transition_table_path), 0u);
    } else {
        // 默认配置文件不存在，这是预期的
        EXPECT_NE(ret, 0);
    }
}

TEST(ConfigLoaderTest, PartialConfig_OnlyDFA)
{
    // 测试只有 DFA 配置的情况
    const char *config_content = R"(
    {
        "kadd_dfa_engine": {
            "transition_table_path": "/path/to/transition.bin",
            "output_table_path": "/path/to/output.bin",
            "eod_table_path": "/path/to/eod.bin"
        }
    }
    )";

    std::string config_path = CreateTestConfigFile(config_content);
    kadd_config_t config;

    int ret = kadd_config_load(config_path.c_str(), &config);

    EXPECT_EQ(ret, 0) << "部分配置应该成功加载";

    // 验证 DFA 配置已加载
    EXPECT_STREQ(config.dfa.transition_table_path, "/path/to/transition.bin");

    // 验证默认值被设置
    EXPECT_EQ(config.dfa.max_input_length, kDefaultMaxInputLength);
    EXPECT_EQ(config.dfa.max_output_dim, kDefaultMaxOutputDim);
    EXPECT_EQ(config.ml.feature_dim, kDefaultFeatureDim);
    EXPECT_EQ(config.ml.window_size, kDefaultWindowSize);

    DeleteTestFile(config_path);
}

TEST(ConfigLoaderTest, PartialConfig_OnlyML)
{
    // 测试只有 ML 配置的情况
    const char *config_content = R"(
    {
        "kadd_ml_engine": {
            "model_path": "/path/to/model.onnx"
        }
    }
    )";

    std::string config_path = CreateTestConfigFile(config_content);
    kadd_config_t config;

    int ret = kadd_config_load(config_path.c_str(), &config);

    EXPECT_EQ(ret, 0) << "部分配置应该成功加载";

    // 验证 ML 配置已加载
    EXPECT_STREQ(config.ml.model_path, "/path/to/model.onnx");

    // 验证默认值被设置
    EXPECT_EQ(config.ml.feature_dim, kDefaultFeatureDim);
    EXPECT_EQ(config.ml.window_size, kDefaultWindowSize);

    DeleteTestFile(config_path);
}

TEST(ConfigLoaderTest, FeatureConfigFallbackToML)
{
    // 测试 Feature 配置从 ML 配置复制的逻辑
    const char *config_content = R"(
    {
        "kadd_ml_engine": {
            "model_path": "/path/to/ml_model.onnx",
            "feature_dim": 48,
            "window_size": 7
        }
    }
    )";

    std::string config_path = CreateTestConfigFile(config_content);
    kadd_config_t config;

    int ret = kadd_config_load(config_path.c_str(), &config);

    EXPECT_EQ(ret, 0) << "配置应该成功加载";

    // 验证 Feature 配置从 ML 复制
    EXPECT_STREQ(config.feature.ml_model_path, "/path/to/ml_model.onnx");
    EXPECT_EQ(config.feature.feature_dim, 48);
    EXPECT_EQ(config.feature.window_size, 7);

    DeleteTestFile(config_path);
}

TEST(ConfigLoaderTest, FeatureConfigOverrideML)
{
    // 测试 Feature 配置覆盖 ML 配置
    const char *config_content = R"(
    {
        "kadd_ml_engine": {
            "model_path": "/path/to/ml_model.onnx",
            "feature_dim": 48
        },
        "kadd_feature_engine": {
            "ml_model_path": "/path/to/feature_model.onnx",
            "feature_dim": 96
        }
    }
    )";

    std::string config_path = CreateTestConfigFile(config_content);
    kadd_config_t config;

    int ret = kadd_config_load(config_path.c_str(), &config);

    EXPECT_EQ(ret, 0) << "配置应该成功加载";

    // 验证 Feature 配置优先
    EXPECT_STREQ(config.feature.ml_model_path, "/path/to/feature_model.onnx");
    EXPECT_EQ(config.feature.feature_dim, 96);

    DeleteTestFile(config_path);
}

// ========== 2. 边界测试 ==========

TEST(ConfigLoaderTest, NullConfigPtr)
{
    // 测试空配置指针
    int ret = kadd_config_load("config.json", nullptr);

    EXPECT_NE(ret, 0) << "空配置指针应该返回错误";
}

TEST(ConfigLoaderTest, EmptyConfigPath)
{
    // 测试空路径字符串
    kadd_config_t config;
    int ret = kadd_config_load("", &config);

    EXPECT_NE(ret, 0) << "空路径应该返回错误";
}

TEST(ConfigLoaderTest, NonexistentFile)
{
    // 测试不存在的文件
    kadd_config_t config;
    int ret = kadd_config_load("nonexistent_config_12345.json", &config);

    EXPECT_NE(ret, 0) << "不存在的文件应该返回错误";
}

// ========== 3. 错误路径测试 ==========

TEST(ConfigLoaderTest, InvalidJSON_Syntax)
{
    // 测试 JSON 语法错误
    const char *config_content = R"(
    {
        "kadd_dfa_engine": {
            "transition_table_path": "/path/to/transition.bin"
        },
    }
    )";  // 注意：多了一个逗号

    std::string config_path = CreateTestConfigFile(config_content);
    kadd_config_t config;

    int ret = kadd_config_load(config_path.c_str(), &config);

    EXPECT_NE(ret, 0) << "无效的JSON应该返回错误";

    DeleteTestFile(config_path);
}

TEST(ConfigLoaderTest, InvalidJSON_NotAnObject)
{
    // 测试 JSON 不是对象
    const char *config_content = R"(
    [
        "item1",
        "item2"
    ]
    )";

    std::string config_path = CreateTestConfigFile(config_content);
    kadd_config_t config;

    int ret = kadd_config_load(config_path.c_str(), &config);

    EXPECT_NE(ret, 0) << "JSON不是对象应该返回错误";

    DeleteTestFile(config_path);
}

TEST(ConfigLoaderTest, WrongFieldType_StringInsteadOfNumber)
{
    // 测试字段类型错误
    const char *config_content = R"(
    {
        "kadd_ml_engine": {
            "feature_dim": "should_be_number"
        }
    }
    )";

    std::string config_path = CreateTestConfigFile(config_content);
    kadd_config_t config;

    int ret = kadd_config_load(config_path.c_str(), &config);

    EXPECT_EQ(ret, 0) << "类型错误应该使用默认值";
    EXPECT_EQ(config.ml.feature_dim, kDefaultFeatureDim) << "应该使用默认值";

    DeleteTestFile(config_path);
}

TEST(ConfigLoaderTest, WrongFieldType_NumberInsteadOfString)
{
    // 测试字段类型错误
    const char *config_content = R"(
    {
        "kadd_ml_engine": {
            "model_path": 12345
        }
    }
    )";

    std::string config_path = CreateTestConfigFile(config_content);
    kadd_config_t config;

    int ret = kadd_config_load(config_path.c_str(), &config);

    EXPECT_EQ(ret, 0) << "应该成功加载（路径字段为空）";
    EXPECT_EQ(config.ml.model_path[0], '\0') << "路径应该是空字符串";

    DeleteTestFile(config_path);
}

TEST(ConfigLoaderTest, LongString_Truncation)
{
    // 测试长字符串截断
    std::string long_path(600, 'a');  // 超过 512 字符限制

    const char *config_content = R"(
    {
        "kadd_ml_engine": {
            "model_path": "REPLACE_ME"
        }
    }
    )";

    std::string modified_content = std::regex_replace(
        std::string(config_content),
        std::regex("REPLACE_ME"),
        long_path
    );

    std::string config_path = CreateTestConfigFile(modified_content.c_str());
    kadd_config_t config;

    int ret = kadd_config_load(config_path.c_str(), &config);

    EXPECT_EQ(ret, 0) << "长字符串应该被截断并成功加载";
    EXPECT_LE(strlen(config.ml.model_path), static_cast<size_t>(kMaxPathLength)) << "路径长度不应超过512";

    DeleteTestFile(config_path);
}

TEST(ConfigLoaderTest, ZeroValues_Accepted)
{
    // 测试零值是否被接受
    const char *config_content = R"(
    {
        "kadd_dfa_engine": {
            "max_input_length": 0,
            "max_output_dim": 0
        },
        "kadd_ml_engine": {
            "feature_dim": 0,
            "window_size": 0
        }
    }
    )";

    std::string config_path = CreateTestConfigFile(config_content);
    kadd_config_t config;

    int ret = kadd_config_load(config_path.c_str(), &config);

    EXPECT_EQ(ret, 0) << "零值应该被接受";
    EXPECT_EQ(config.dfa.max_input_length, 0);
    EXPECT_EQ(config.ml.feature_dim, 0);

    DeleteTestFile(config_path);
}

TEST(ConfigLoaderTest, NegativeValues_Accepted)
{
    // 测试负值是否被接受
    const char *config_content = R"(
    {
        "kadd_dfa_engine": {
            "max_input_length": -100,
            "max_output_dim": -200
        }
    }
    )";

    std::string config_path = CreateTestConfigFile(config_content);
    kadd_config_t config;

    int ret = kadd_config_load(config_path.c_str(), &config);

    EXPECT_EQ(ret, 0) << "负值应该被接受";
    EXPECT_EQ(config.dfa.max_input_length, -100);
    EXPECT_EQ(config.dfa.max_output_dim, -200);

    DeleteTestFile(config_path);
}

TEST(ConfigLoaderTest, SQLiConfigDefaults)
{
    // 测试 SQLi 配置的默认值
    const char *config_content = "{}";

    std::string config_path = CreateTestConfigFile(config_content);
    kadd_config_t config;

    int ret = kadd_config_load(config_path.c_str(), &config);

    EXPECT_EQ(ret, 0) << "空配置应该使用默认值";

    // 验证 SQLi 默认值（覆盖第154-176行）
    EXPECT_EQ(config.sqli.feature_dim, kDefaultFeatureDim);
    EXPECT_EQ(config.sqli.window_size, kDefaultWindowSize);
    EXPECT_EQ(config.sqli.max_input_length, kDefaultMaxInputLength);
    EXPECT_EQ(config.sqli.max_output_dim, kDefaultMaxOutputDim);
    EXPECT_EQ(config.sqli.transition_table_path[0], '\0');
    EXPECT_EQ(config.sqli.output_table_path[0], '\0');
    EXPECT_EQ(config.sqli.eod_table_path[0], '\0');
    EXPECT_EQ(config.sqli.ml_model_path[0], '\0');
    // 验证 Feature 默认值（覆盖第192-200行）
    EXPECT_EQ(config.feature.feature_dim, kDefaultFeatureDim);
    EXPECT_EQ(config.feature.window_size, kDefaultWindowSize);
    EXPECT_EQ(config.feature.ml_model_path[0], '\0');
    DeleteTestFile(config_path);
}

// ========== 4. 覆盖率测试 ==========

TEST(ConfigLoaderTest, CopyStrField_Truncation)
{
    // 测试字符串拷贝截断逻辑（覆盖第14-21行）
    std::string long_string(kSqliInputLen, 'x');

    const char *config_content = R"(
    {
        "kadd_ml_engine": {
            "model_path": "REPLACE_ME"
        }
    }
    )";

    std::string modified_content = std::regex_replace(
        std::string(config_content),
        std::regex("REPLACE_ME"),
        long_string
    );

    std::string config_path = CreateTestConfigFile(modified_content.c_str());
    kadd_config_t config;

    int ret = kadd_config_load(config_path.c_str(), &config);

    EXPECT_EQ(ret, 0) << "应该成功加载";
    EXPECT_EQ(strlen(config.ml.model_path), static_cast<size_t>(kTruncatedPathLength)) << "应该被截断为511字符（512-1）";

    DeleteTestFile(config_path);
}

TEST(ConfigLoaderTest, ConfigFree_NoOp)
{
    // 测试 kadd_config_free（覆盖第229-232行）
    kadd_config_t config;
    memset(&config, 0, sizeof(config));

    // 不应该崩溃
    kadd_config_free(&config);

    SUCCEED() << "kadd_config_free 不应该崩溃";
}

TEST(ConfigLoaderTest, LoadMultipleTimes)
{
    // 测试多次加载配置
    const char *config_content = R"(
    {
        "kadd_ml_engine": {
            "model_path": "/path/to/model.onnx"
        }
    }
    )";

    std::string config_path = CreateTestConfigFile(config_content);
    kadd_config_t config1, config2, config3;

    EXPECT_EQ(kadd_config_load(config_path.c_str(), &config1), 0);
    EXPECT_EQ(kadd_config_load(config_path.c_str(), &config2), 0);
    EXPECT_EQ(kadd_config_load(config_path.c_str(), &config3), 0);

    // 验证每次加载都是独立的
    EXPECT_STREQ(config1.ml.model_path, "/path/to/model.onnx");
    EXPECT_STREQ(config2.ml.model_path, "/path/to/model.onnx");
    EXPECT_STREQ(config3.ml.model_path, "/path/to/model.onnx");

    DeleteTestFile(config_path);
}

// ========== 主函数 ==========

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
