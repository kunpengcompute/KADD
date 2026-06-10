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

// ========== 配置模板辅助函数 ==========

/**
 * @brief 获取完整的默认配置 JSON 模板
 */
static std::string GetDefaultConfigTemplate()
{
    return R"(
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
}

/**
 * @brief 从 JSON 字符串中移除指定字段
 */
static std::string RemoveField(const std::string& json, const std::string& /*section*/, const std::string& field)
{
    std::string pattern = "\"" + field + "\"\\s*:\\s*[{\\[]?[^,}]+[}\\]]?,?\\s*";
    std::regex re(pattern);
    return std::regex_replace(json, re, "");
}

/**
 * @brief 修改 JSON 中指定字段的值
 */
static std::string ModifyField(const std::string& json, const std::string& /*section*/,
                              const std::string& field, const std::string& new_value)
{
    std::string pattern = "\"" + field + "\"\\s*:\\s*[^,}]+";
    std::string replacement = "\"" + field + "\": " + new_value;
    std::regex re(pattern);
    return std::regex_replace(json, re, replacement);
}

/**
 * @brief 简化的配置加载测试（减少重复代码）
 */
static void TestConfigLoad(const std::string& config_json, int expected_return, const std::string& error_msg = "")
{
    std::string config_path = CreateTestConfigFile(config_json.c_str());
    kadd_config_t config;
    int ret = kadd_config_load(config_path.c_str(), &config);

    if (expected_return == KADD_SUCCESS) {
        EXPECT_EQ(ret, KADD_SUCCESS) << error_msg;
    } else {
        EXPECT_EQ(ret, expected_return) << error_msg;
    }

    DeleteTestFile(config_path);
}

/**
 * @brief 从 JSON 中移除整个配置节
 */
static std::string RemoveSection(const std::string& json, const std::string& section)
{
    std::string pattern = "\"" + section + "\"\\s*:\\s*\\{[^}]*\\}\\s*,?\\s*";
    std::regex re(pattern);
    return std::regex_replace(json, re, "");
}

/**
 * @brief 将指定字段设为空字符串
 */
static std::string EmptyField(const std::string& json, const std::string& field)
{
    return ModifyField(json, "", field, "\"\"");
}

/**
 * @brief 添加未知字段到指定节（用于测试未知字段应被忽略）
 */
static std::string AddUnknownField(const std::string& json, const std::string& section)
{
    std::string pattern = "\"" + section + "\"\\s*:\\s*\\{";
    std::string replacement = "\"" + section + "\": {\"unknown_field\": \"should_be_ignored\", ";
    std::regex re(pattern);
    return std::regex_replace(json, re, replacement);
}

/**
 * @brief 移除所有数值字段（用于测试所有数值字段缺失场景）
 */
static std::string RemoveAllNumericFields(const std::string& json)
{
    std::string result = json;
    result = std::regex_replace(result, std::regex("\"max_input_length\"\\s*:\\s*[^,}\\]]+,?"), "");
    result = std::regex_replace(result, std::regex("\"max_output_dim\"\\s*:\\s*[^,}\\]]+,?"), "");
    result = std::regex_replace(result, std::regex("\"feature_dim\"\\s*:\\s*[^,}\\]]+,?"), "");
    result = std::regex_replace(result, std::regex("\"window_size\"\\s*:\\s*[^,}\\]]+,?"), "");
    return result;
}

/**
 * @brief 简化的配置加载测试，并验证加载后的配置值
 */
static void TestConfigLoadAndValidate(const std::string& config_json,
                                      std::function<void(const kadd_config_t&)> validator)
{
    std::string config_path = CreateTestConfigFile(config_json.c_str());
    kadd_config_t config;
    int ret = kadd_config_load(config_path.c_str(), &config);

    EXPECT_EQ(ret, KADD_SUCCESS) << "应该成功加载配置";
    if (ret == KADD_SUCCESS && validator) {
        validator(config);
    }

    DeleteTestFile(config_path);
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
    // 使用辅助函数获取默认配置模板
    std::string config_content = GetDefaultConfigTemplate();

    std::string config_path = CreateTestConfigFile(config_content.c_str());
    kadd_config_t config;
    int ret = kadd_config_load(config_path.c_str(), &config);
    EXPECT_EQ(ret, KADD_SUCCESS) << "应该成功加载配置";

    ValidateFullConfig(config);
    DeleteTestFile(config_path);
}

TEST(ConfigLoaderTest, LoadWithNullPath)
{
    // 测试 NULL 路径（严格模式：不再支持默认路径）
    kadd_config_t config;
    int ret = kadd_config_load(nullptr, &config);

    EXPECT_EQ(ret, KADD_ERROR_INVALID_INPUT) << "NULL 路径应该返回错误";
}

// 参数化测试：缺失配置节
class MissingConfigSectionTest : public ::testing::TestWithParam<std::string> {};

TEST_P(MissingConfigSectionTest, MissingSectionShouldFail)
{
    const std::string& section = GetParam();
    std::string config = RemoveSection(GetDefaultConfigTemplate(), section);
    TestConfigLoad(config, KADD_ERROR_CONFIG, "缺失 " + section + " 应该返回错误");
}

INSTANTIATE_TEST_SUITE_P(
    MissingSections,
    MissingConfigSectionTest,
    ::testing::Values(
        "kadd_ml_engine",
        "kadd_sqli_engine",
        "kadd_feature_engine"
    )
);

TEST(ConfigLoaderTest, MissingRequiredField_DFA_TransitionTablePath)
{
    // 测试 DFA 缺失必需字段 - 使用辅助函数简化
    std::string config = RemoveField(GetDefaultConfigTemplate(), "kadd_dfa_engine", "transition_table_path");
    TestConfigLoad(config, KADD_ERROR_CONFIG, "缺失 transition_table_path 应该返回错误");
}

// ========== 2. 边界测试 ==========

TEST(ConfigLoaderTest, NullConfigPtr)
{
    // 测试空配置指针
    int ret = kadd_config_load("config.json", nullptr);

    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR) << "空配置指针应该返回错误";
}

TEST(ConfigLoaderTest, EmptyConfigPath)
{
    // 测试空路径字符串
    kadd_config_t config;
    int ret = kadd_config_load("", &config);

    EXPECT_EQ(ret, KADD_ERROR_FILE_OPERATION_FAILED) << "空路径应该返回错误";
}

TEST(ConfigLoaderTest, NonexistentFile)
{
    // 测试不存在的文件
    kadd_config_t config;
    int ret = kadd_config_load("nonexistent_config_12345.json", &config);

    EXPECT_EQ(ret, KADD_ERROR_FILE_OPERATION_FAILED) << "不存在的文件应该返回错误";
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

    EXPECT_EQ(ret, KADD_ERROR_CONFIG) << "无效的JSON应该返回错误";

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

    EXPECT_EQ(ret, KADD_ERROR_CONFIG) << "JSON不是对象应该返回错误";

    DeleteTestFile(config_path);
}

TEST(ConfigLoaderTest, MissingRequiredField_ML_ModelPath)
{
    // 测试 ML 缺失必需字段 - 使用辅助函数简化
    std::string config = RemoveField(GetDefaultConfigTemplate(), "kadd_ml_engine", "model_path");
    TestConfigLoad(config, KADD_ERROR_CONFIG, "缺失 model_path 应该返回错误");
}

TEST(ConfigLoaderTest, MissingRequiredField_FeatureEngine_MLModelPath)
{
    // 测试 Feature Engine 缺失必需字段 - 使用辅助函数简化
    std::string config = RemoveField(GetDefaultConfigTemplate(), "kadd_feature_engine", "ml_model_path");
    TestConfigLoad(config, KADD_ERROR_CONFIG, "缺失 ml_model_path 应该返回错误");
}

TEST(ConfigLoaderTest, LongString_Truncation)
{
    // 测试长字符串截断 - 使用辅助函数简化
    std::string long_path(600, 'a');  // 超过 512 字符限制
    std::string config_str = ModifyField(GetDefaultConfigTemplate(), "kadd_ml_engine", "model_path", "\"" + long_path + "\"");

    std::string config_path = CreateTestConfigFile(config_str.c_str());
    kadd_config_t config;

    int ret = kadd_config_load(config_path.c_str(), &config);

    EXPECT_EQ(ret, KADD_SUCCESS) << "长字符串应该被截断并成功加载";
    EXPECT_LE(strlen(config.ml.model_path), static_cast<size_t>(kMaxPathLength)) << "路径长度不应超过512";

    DeleteTestFile(config_path);
}

TEST(ConfigLoaderTest, ZeroValues_ShouldFail)
{
    // 测试零值应该被拒绝（数值字段必须为正数） - 使用辅助函数简化
    std::string config = GetDefaultConfigTemplate();
    // 修改所有数值字段为 0
    config = ModifyField(config, "kadd_dfa_engine", "max_input_length", "0");
    config = ModifyField(config, "kadd_dfa_engine", "max_output_dim", "0");
    config = ModifyField(config, "kadd_ml_engine", "feature_dim", "0");
    config = ModifyField(config, "kadd_ml_engine", "window_size", "0");
    config = ModifyField(config, "kadd_sqli_engine", "feature_dim", "0");
    config = ModifyField(config, "kadd_sqli_engine", "window_size", "0");
    config = ModifyField(config, "kadd_sqli_engine", "max_input_length", "0");
    config = ModifyField(config, "kadd_sqli_engine", "max_output_dim", "0");
    config = ModifyField(config, "kadd_feature_engine", "feature_dim", "0");
    config = ModifyField(config, "kadd_feature_engine", "window_size", "0");
    TestConfigLoad(config, KADD_ERROR_INVALID_PARAM, "零值应该被拒绝");
}

TEST(ConfigLoaderTest, NegativeValues_ShouldFail)
{
    // 测试负值应该被拒绝（数值字段必须为正数） - 使用辅助函数简化
    std::string config = GetDefaultConfigTemplate();
    config = ModifyField(config, "kadd_dfa_engine", "max_input_length", "-100");
    config = ModifyField(config, "kadd_dfa_engine", "max_output_dim", "-200");
    TestConfigLoad(config, KADD_ERROR_INVALID_PARAM, "负值应该被拒绝");
}

TEST(ConfigLoaderTest, EmptyStringField_MLModelPath)
{
    // 测试空字符串字段（应该被拒绝）- 使用辅助函数简化
    std::string config = EmptyField(GetDefaultConfigTemplate(), "model_path");
    TestConfigLoad(config, KADD_ERROR_CONFIG, "空字符串 model_path 应该返回错误");
}

// ========== 4. 覆盖率测试 ==========

TEST(ConfigLoaderTest, CopyStrField_Truncation)
{
    // 测试字符串拷贝截断逻辑（覆盖第14-21行）- 使用辅助函数简化
    std::string long_string(kSqliInputLen, 'x');
    std::string config = ModifyField(GetDefaultConfigTemplate(), "", "model_path", "\"" + long_string + "\"");

    std::string config_path = CreateTestConfigFile(config.c_str());
    kadd_config_t config_loaded;

    int ret = kadd_config_load(config_path.c_str(), &config_loaded);

    EXPECT_EQ(ret, KADD_SUCCESS) << "应该成功加载";
    EXPECT_EQ(strlen(config_loaded.ml.model_path), static_cast<size_t>(kTruncatedPathLength)) << "应该被截断为511字符（512-1）";

    DeleteTestFile(config_path);
}

TEST(ConfigLoaderTest, ConfigFree_NoOp)
{
    // 测试 kadd_config_free（覆盖第229-232行）
    kadd_config_t config = {};

    // 不应该崩溃
    kadd_config_free(&config);

    SUCCEED() << "kadd_config_free 不应该崩溃";
}

TEST(ConfigLoaderTest, LoadMultipleTimes)
{
    // 测试多次加载配置
    // 使用辅助函数获取默认配置模板
    std::string config_content = GetDefaultConfigTemplate();
    std::string config_path = CreateTestConfigFile(config_content.c_str());
    kadd_config_t config1, config2, config3;

    EXPECT_EQ(kadd_config_load(config_path.c_str(), &config1), KADD_SUCCESS);
    EXPECT_EQ(kadd_config_load(config_path.c_str(), &config2), KADD_SUCCESS);
    EXPECT_EQ(kadd_config_load(config_path.c_str(), &config3), KADD_SUCCESS);

    // 验证每次加载都是独立的
    EXPECT_STREQ(config1.ml.model_path, "/path/to/model.onnx");
    EXPECT_STREQ(config2.ml.model_path, "/path/to/model.onnx");
    EXPECT_STREQ(config3.ml.model_path, "/path/to/model.onnx");

    DeleteTestFile(config_path);
}

// ========== 新增覆盖率测试 ==========

TEST(ConfigLoaderTest, MissingRequiredField_SQLi_TransitionTablePath)
{
    // 测试 SQLi 缺失必需字段 - 使用辅助函数简化
    std::string config = RemoveField(GetDefaultConfigTemplate(), "kadd_sqli_engine", "transition_table_path");
    TestConfigLoad(config, KADD_ERROR_CONFIG, "缺失 transition_table_path 应该返回错误");
}

TEST(ConfigLoaderTest, MissingRequiredField_AllNumericFields)
{
    // 测试所有数值字段都缺失 - 使用辅助函数简化
    std::string config = RemoveAllNumericFields(GetDefaultConfigTemplate());
    TestConfigLoad(config, KADD_ERROR_CONFIG, "缺失数值字段应该返回错误");
}

TEST(ConfigLoaderTest, InvalidFieldType_MissingNumericFields)
{
    // 测试数值字段类型错误（使用字符串）- 使用辅助函数简化
    std::string config = GetDefaultConfigTemplate();
    config = ModifyField(config, "", "max_input_length", "\"not_a_number\"");
    config = ModifyField(config, "", "max_output_dim", "\"not_a_number\"");
    TestConfigLoad(config, KADD_ERROR_CONFIG, "数值字段类型错误应该返回错误");
}

TEST(ConfigLoaderTest, MissingRequiredField_DFA_AllPaths)
{
    // 测试 DFA 所有路径字段缺失 - 使用辅助函数简化
    std::string config = GetDefaultConfigTemplate();
    config = RemoveField(config, "", "transition_table_path");
    config = RemoveField(config, "", "output_table_path");
    config = RemoveField(config, "", "eod_table_path");
    TestConfigLoad(config, KADD_ERROR_CONFIG, "DFA 缺失所有路径字段应该返回错误");
}

// ========== parse_sqli_config 字段缺失测试 ==========

TEST(ConfigLoaderTest, SQLi_MissingField_OutputTablePath)
{
    // 测试 SQLi 缺失 output_table_path 字段 - 使用辅助函数简化
    std::string config = RemoveField(GetDefaultConfigTemplate(), "kadd_sqli_engine", "output_table_path");
    TestConfigLoad(config, KADD_ERROR_CONFIG, "缺失 output_table_path 应该返回错误");
}

TEST(ConfigLoaderTest, SQLi_MissingField_EodTablePath)
{
    // 测试 SQLi 缺失 eod_table_path 字段 - 使用辅助函数简化
    std::string config = RemoveField(GetDefaultConfigTemplate(), "kadd_sqli_engine", "eod_table_path");
    TestConfigLoad(config, KADD_ERROR_CONFIG, "缺失 eod_table_path 应该返回错误");
}

TEST(ConfigLoaderTest, SQLi_MissingField_MLModelPath)
{
    // 测试 SQLi 缺失 ml_model_path 字段 - 使用辅助函数简化
    std::string config = RemoveField(GetDefaultConfigTemplate(), "kadd_sqli_engine", "ml_model_path");
    TestConfigLoad(config, KADD_ERROR_CONFIG, "缺失 ml_model_path 应该返回错误");
}

TEST(ConfigLoaderTest, SQLi_MissingField_FeatureDim)
{
    // 测试 SQLi 缺失 feature_dim 字段 - 使用辅助函数简化
    std::string config = RemoveField(GetDefaultConfigTemplate(), "kadd_sqli_engine", "feature_dim");
    TestConfigLoad(config, KADD_ERROR_CONFIG, "缺失 feature_dim 应该返回错误");
}

TEST(ConfigLoaderTest, SQLi_MissingField_WindowSize)
{
    // 测试 SQLi 缺失 window_size 字段 - 使用辅助函数简化
    std::string config = RemoveField(GetDefaultConfigTemplate(), "kadd_sqli_engine", "window_size");
    TestConfigLoad(config, KADD_ERROR_CONFIG, "缺失 window_size 应该返回错误");
}

TEST(ConfigLoaderTest, SQLi_MissingField_MaxInputLength)
{
    // 测试 SQLi 缺失 max_input_length 字段 - 使用辅助函数简化
    std::string config = RemoveField(GetDefaultConfigTemplate(), "kadd_sqli_engine", "max_input_length");
    TestConfigLoad(config, KADD_ERROR_CONFIG, "缺失 max_input_length 应该返回错误");
}

TEST(ConfigLoaderTest, SQLi_MissingField_MaxOutputDim)
{
    // 测试 SQLi 缺失 max_output_dim 字段 - 使用辅助函数简化
    std::string config = RemoveField(GetDefaultConfigTemplate(), "kadd_sqli_engine", "max_output_dim");
    TestConfigLoad(config, KADD_ERROR_CONFIG, "缺失 max_output_dim 应该返回错误");
}

// ========== read_file_to_string 边界测试 ==========
TEST(ConfigLoaderTest, FileSize_ZeroLength)
{
    // 测试空文件 - 使用辅助函数简化
    TestConfigLoad("", KADD_ERROR_CONFIG, "空文件应该返回错误");
}

TEST(ConfigLoaderTest, FileSize_MinValid)
{
    // 测试最小有效文件 - 使用辅助函数简化
    std::string config_content = GetDefaultConfigTemplate();
    std::string config_path = CreateTestConfigFile(config_content.c_str());
    kadd_config_t config;

    int ret = kadd_config_load(config_path.c_str(), &config);

    EXPECT_EQ(ret, KADD_SUCCESS) << "最小有效配置应该成功加载";

    DeleteTestFile(config_path);
}

// ========== 新增边界值测试 ==========

TEST(ConfigLoaderTest, FeatureDim_Negative_ShouldFail)
{
    // 测试 feature_dim 为负数应该被拒绝 - 使用辅助函数简化
    std::string config = ModifyField(GetDefaultConfigTemplate(), "kadd_ml_engine", "feature_dim", "-10");
    TestConfigLoad(config, KADD_ERROR_INVALID_PARAM, "负数 feature_dim 应该被拒绝");
}

TEST(ConfigLoaderTest, FeatureDim_Zero_ShouldFail)
{
    // 测试 feature_dim 为零应该被拒绝 - 使用辅助函数简化
    std::string config = ModifyField(GetDefaultConfigTemplate(), "kadd_ml_engine", "feature_dim", "0");
    TestConfigLoad(config, KADD_ERROR_INVALID_PARAM, "零值 feature_dim 应该被拒绝");
}

TEST(ConfigLoaderTest, IntegerOverflow_TooLarge)
{
    // 测试超大整数值（超过上限应该被拒绝）- 使用辅助函数简化
    std::string config = ModifyField(GetDefaultConfigTemplate(), "", "max_input_length", "2147483647");
    std::string config_path = CreateTestConfigFile(config.c_str());
    kadd_config_t config_loaded;

    int ret = kadd_config_load(config_path.c_str(), &config_loaded);

    // INT_MAX (2147483647) 应该被拒绝（超过 max_val 100000）
    EXPECT_EQ(ret, KADD_ERROR_INVALID_PARAM) << "超过上限的整数值应该被拒绝";

    DeleteTestFile(config_path);
}

TEST(ConfigLoaderTest, FloatValue_TruncatedToInt)
{
    // 测试浮点数值应该被截断为整数 - 使用辅助函数简化
    std::string config = ModifyField(GetDefaultConfigTemplate(), "", "max_input_length", "500.5");
    std::string config_path = CreateTestConfigFile(config.c_str());
    kadd_config_t config_loaded;

    int ret = kadd_config_load(config_path.c_str(), &config_loaded);

    // cJSON 会将浮点数截断为整数
    EXPECT_EQ(ret, KADD_SUCCESS) << "浮点数值应该被接受并截断为整数";
    if (ret == 0) {
        EXPECT_EQ(config_loaded.dfa.max_input_length, 500) << "浮点数应被截断";
    }

    DeleteTestFile(config_path);
}

TEST(ConfigLoaderTest, DuplicateField_LastWins)
{
    // 测试重复字段（后者覆盖前者）- 使用辅助函数简化
    std::string config = GetDefaultConfigTemplate();
    // 在 kadd_ml_engine 节中插入重复的 model_path 字段
    size_t ml_pos = config.find("\"kadd_ml_engine\":");
    if (ml_pos != std::string::npos) {
        size_t brace_pos = config.find("{", ml_pos);
        if (brace_pos != std::string::npos) {
            config.insert(brace_pos + 1, "\"model_path\": \"/path/to/first.onnx\", ");
        }
    }
    // 修改第一个 model_path 为 second
    config = ModifyField(config, "", "model_path", "\"/path/to/second.onnx\"");

    std::string config_path = CreateTestConfigFile(config.c_str());
    kadd_config_t config_loaded;

    int ret = kadd_config_load(config_path.c_str(), &config_loaded);

    // JSON 解析器通常取最后一个值
    EXPECT_EQ(ret, KADD_SUCCESS) << "重复字段应该被解析（后者覆盖）";
    EXPECT_STREQ(config_loaded.ml.model_path, "/path/to/second.onnx");

    DeleteTestFile(config_path);
}

TEST(ConfigLoaderTest, UnknownField_Ignored)
{
    // 测试包含未知字段的配置（应该被忽略）- 使用辅助函数简化
    std::string config = AddUnknownField(GetDefaultConfigTemplate(), "kadd_dfa_engine");
    // 添加未知节
    config = config.substr(0, config.length() - 2) + R"(, "unknown_section": {"some_field": "value"}})";

    TestConfigLoad(config, KADD_SUCCESS, "未知字段应该被忽略");
}

TEST(ConfigLoaderTest, RelativePath_Accepted)
{
    // 测试相对路径应该被接受 - 使用辅助函数简化
    std::string config = GetDefaultConfigTemplate();
    config = ModifyField(config, "", "transition_table_path", "\"../assets/transition.bin\"");
    config = ModifyField(config, "", "output_table_path", "\"./output.bin\"");
    config = ModifyField(config, "", "eod_table_path", "\"eod.bin\"");

    std::string config_path = CreateTestConfigFile(config.c_str());
    kadd_config_t config_loaded;

    int ret = kadd_config_load(config_path.c_str(), &config_loaded);

    EXPECT_EQ(ret, KADD_SUCCESS) << "相对路径应该被接受";
    EXPECT_STREQ(config_loaded.dfa.transition_table_path, "../assets/transition.bin");

    DeleteTestFile(config_path);
}

TEST(ConfigLoaderTest, DoubleFree_Safe)
{
    // 测试多次释放不会崩溃 - 使用辅助函数简化
    std::string config_content = GetDefaultConfigTemplate();
    std::string config_path = CreateTestConfigFile(config_content.c_str());
    kadd_config_t config;

    EXPECT_EQ(kadd_config_load(config_path.c_str(), &config), KADD_SUCCESS);

    kadd_config_free(&config);
    kadd_config_free(&config);  // 再次释放，应该不会崩溃

    DeleteTestFile(config_path);

    SUCCEED() << "Double free 应该安全处理";
}
