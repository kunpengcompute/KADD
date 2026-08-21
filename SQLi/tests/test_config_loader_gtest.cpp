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
 * 4. 简化测试 - 使用扁平配置结构，移除复杂嵌套测试
 */

#include <gtest/gtest.h>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <regex>
#include <unistd.h>
#include "kadd_config_loader.h"
#include "kadd_errors.h"

// ========== 包含源文件以测试 static 函数 ==========
#include "kadd_config_loader.c"

constexpr int kMaxPathLength = 512;
constexpr int kTruncatedPathLength = 511;

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
 * @brief 获取完整的默认配置 JSON 模板（扁平结构）
 */
static std::string GetDefaultConfigTemplate()
{
    return R"(
    {
        "transition_table": "/path/to/transition.bin",
        "output_table": "/path/to/output.bin",
        "eod_table": "/path/to/eod.bin",
        "model_rf": "/path/to/model_rf.krf",
        "model_rf_onnx": "/path/to/model_rf.onnx",
        "model_cnn": "/path/to/model_cnn.kcn",
        "model_cnn_onnx": "/path/to/model_cnn.onnx"
    }
    )";
}

/**
 * @brief 从 JSON 字符串中移除指定字段（简化版，扁平结构）
 */
static std::string RemoveField(const std::string& json, const std::string& field)
{
    std::string pattern = "\"" + field + "\"\\s*:\\s*\"[^\"]*\"\\s*,?\\s*";
    std::regex re(pattern);
    return std::regex_replace(json, re, "");
}

/**
 * @brief 修改 JSON 中指定字段的值（简化版，扁平结构）
 */
static std::string ModifyField(const std::string& json, const std::string& field, const std::string& new_value)
{
    std::string pattern = "\"" + field + "\"\\s*:\\s*\"[^\"]*\"";
    std::string replacement = "\"" + field + "\": \"" + new_value + "\"";
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

// ========== 1. 功能测试 ==========

// 验证完整配置的各字段（扁平结构）
static void ValidateFullConfig(const kadd_config_t &config)
{
    EXPECT_STREQ(config.transition_table, "/path/to/transition.bin");
    EXPECT_STREQ(config.output_table, "/path/to/output.bin");
    EXPECT_STREQ(config.eod_table, "/path/to/eod.bin");
    EXPECT_STREQ(config.model_rf, "/path/to/model_rf.krf");
    EXPECT_STREQ(config.model_rf_onnx, "/path/to/model_rf.onnx");
    EXPECT_STREQ(config.model_cnn, "/path/to/model_cnn.kcn");
    EXPECT_STREQ(config.model_cnn_onnx, "/path/to/model_cnn.onnx");
}

TEST(ConfigLoaderTest, LoadValidConfig)
{
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
    kadd_config_t config;
    int ret = kadd_config_load(nullptr, &config);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR) << "NULL 路径应该返回错误";
}

TEST(ConfigLoaderTest, LoadWithNullConfigPtr)
{
    int ret = kadd_config_load("config.json", nullptr);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR) << "NULL 配置指针应该返回错误";
}

TEST(ConfigLoaderTest, EmptyConfigPath)
{
    kadd_config_t config;
    int ret = kadd_config_load("", &config);
    EXPECT_EQ(ret, KADD_ERROR_FILE_OPERATION_FAILED) << "空路径应该返回错误";
}

TEST(ConfigLoaderTest, NonexistentFile)
{
    kadd_config_t config;
    int ret = kadd_config_load("nonexistent_config_12345.json", &config);
    EXPECT_EQ(ret, KADD_ERROR_FILE_OPERATION_FAILED) << "不存在的文件应该返回错误";
}

// ========== 3. 错误路径测试 ==========

TEST(ConfigLoaderTest, InvalidJSON_Syntax)
{
    const char *config_content = R"(
    {
        "transition_table": "/path/to/transition.bin",
    }
    )";  // 多了一个逗号

    std::string config_path = CreateTestConfigFile(config_content);
    kadd_config_t config;
    int ret = kadd_config_load(config_path.c_str(), &config);

    EXPECT_EQ(ret, KADD_ERROR_CONFIG) << "无效的JSON应该返回错误";
    DeleteTestFile(config_path);
}

TEST(ConfigLoaderTest, InvalidJSON_NotAnObject)
{
    const char *config_content = R"(["item1", "item2"])";
    std::string config_path = CreateTestConfigFile(config_content);
    kadd_config_t config;
    int ret = kadd_config_load(config_path.c_str(), &config);

    EXPECT_EQ(ret, KADD_ERROR_CONFIG) << "JSON不是对象应该返回错误";
    DeleteTestFile(config_path);
}

TEST(ConfigLoaderTest, MissingRequiredField_TransitionTable)
{
    std::string config = RemoveField(GetDefaultConfigTemplate(), "transition_table");
    TestConfigLoad(config, KADD_ERROR_CONFIG, "缺失 transition_table 应该返回错误");
}

TEST(ConfigLoaderTest, MissingRequiredField_OutputTable)
{
    std::string config = RemoveField(GetDefaultConfigTemplate(), "output_table");
    TestConfigLoad(config, KADD_ERROR_CONFIG, "缺失 output_table 应该返回错误");
}

TEST(ConfigLoaderTest, MissingRequiredField_EodTable)
{
    std::string config = RemoveField(GetDefaultConfigTemplate(), "eod_table");
    TestConfigLoad(config, KADD_ERROR_CONFIG, "缺失 eod_table 应该返回错误");
}

TEST(ConfigLoaderTest, MissingRequiredField_ModelRf)
{
    std::string config = RemoveField(GetDefaultConfigTemplate(), "model_rf");
    config = RemoveField(config, "model_rf_onnx");
    TestConfigLoad(config, KADD_ERROR_CONFIG, "同时缺失 model_rf 和 model_rf_onnx 应该返回错误");
}

TEST(ConfigLoaderTest, LongString_Truncation)
{
    std::string long_path(600, 'a');  // 超过 512 字符限制
    std::string config_str = ModifyField(GetDefaultConfigTemplate(), "model_rf", long_path);

    std::string config_path = CreateTestConfigFile(config_str.c_str());
    kadd_config_t config;
    int ret = kadd_config_load(config_path.c_str(), &config);

    EXPECT_EQ(ret, KADD_SUCCESS) << "长字符串应该被截断并成功加载";
    EXPECT_LE(strlen(config.model_rf), static_cast<size_t>(kMaxPathLength)) << "路径长度不应超过512";

    DeleteTestFile(config_path);
}

TEST(ConfigLoaderTest, EmptyStringField_ModelPath)
{
    // 注意：当前实现不检查空字符串，会成功加载
    std::string config = ModifyField(GetDefaultConfigTemplate(), "model_rf", "");

    std::string config_path = CreateTestConfigFile(config.c_str());
    kadd_config_t config_loaded;
    int ret = kadd_config_load(config_path.c_str(), &config_loaded);

    // 当前实现会成功加载（空字符串也会被接受）
    EXPECT_EQ(ret, KADD_SUCCESS) << "当前实现接受空字符串";

    DeleteTestFile(config_path);
}

// ========== 4. 覆盖率测试 ==========

TEST(ConfigLoaderTest, CopyStrField_Truncation)
{
    std::string long_string(600, 'x');
    std::string config = ModifyField(GetDefaultConfigTemplate(), "model_rf", long_string);

    std::string config_path = CreateTestConfigFile(config.c_str());
    kadd_config_t config_loaded;
    int ret = kadd_config_load(config_path.c_str(), &config_loaded);

    EXPECT_EQ(ret, KADD_SUCCESS) << "应该成功加载";
    EXPECT_EQ(strlen(config_loaded.model_rf), static_cast<size_t>(kTruncatedPathLength))
        << "应该被截断为511字符（512-1）";

    DeleteTestFile(config_path);
}

TEST(ConfigLoaderTest, ConfigFree_NoOp)
{
    kadd_config_t config = {};
    kadd_config_free(&config);
    SUCCEED() << "kadd_config_free 不应该崩溃";
}

TEST(ConfigLoaderTest, LoadMultipleTimes)
{
    std::string config_content = GetDefaultConfigTemplate();
    std::string config_path = CreateTestConfigFile(config_content.c_str());
    kadd_config_t config1, config2, config3;

    EXPECT_EQ(kadd_config_load(config_path.c_str(), &config1), KADD_SUCCESS);
    EXPECT_EQ(kadd_config_load(config_path.c_str(), &config2), KADD_SUCCESS);
    EXPECT_EQ(kadd_config_load(config_path.c_str(), &config3), KADD_SUCCESS);

    // 验证每次加载都是独立的
    EXPECT_STREQ(config1.model_rf, "/path/to/model_rf.krf");
    EXPECT_STREQ(config2.model_rf, "/path/to/model_rf.krf");
    EXPECT_STREQ(config3.model_rf, "/path/to/model_rf.krf");

    DeleteTestFile(config_path);
}

// ========== 新增边界值测试 ==========

TEST(ConfigLoaderTest, UnknownField_Ignored)
{
    // 添加未知字段（应该被忽略）
    std::string config = GetDefaultConfigTemplate();
    size_t pos = config.find("{");
    if (pos != std::string::npos) {
        config.insert(pos + 1, "\"unknown_field\": \"should_be_ignored\", ");
    }

    TestConfigLoad(config, KADD_SUCCESS, "未知字段应该被忽略");
}

TEST(ConfigLoaderTest, RelativePath_Accepted)
{
    std::string config = GetDefaultConfigTemplate();
    config = ModifyField(config, "transition_table", "../assets/transition.bin");
    config = ModifyField(config, "output_table", "./output.bin");
    config = ModifyField(config, "eod_table", "eod.bin");

    std::string config_path = CreateTestConfigFile(config.c_str());
    kadd_config_t config_loaded;
    int ret = kadd_config_load(config_path.c_str(), &config_loaded);

    EXPECT_EQ(ret, KADD_SUCCESS) << "相对路径应该被接受";
    EXPECT_STREQ(config_loaded.transition_table, "../assets/transition.bin");

    DeleteTestFile(config_path);
}

TEST(ConfigLoaderTest, DoubleFree_Safe)
{
    std::string config_content = GetDefaultConfigTemplate();
    std::string config_path = CreateTestConfigFile(config_content.c_str());
    kadd_config_t config;

    EXPECT_EQ(kadd_config_load(config_path.c_str(), &config), KADD_SUCCESS);

    kadd_config_free(&config);
    kadd_config_free(&config);  // 再次释放，应该不会崩溃

    DeleteTestFile(config_path);
    SUCCEED() << "Double free 应该安全处理";
}

TEST(ConfigLoaderTest, FileSize_ZeroLength)
{
    TestConfigLoad("", KADD_ERROR_CONFIG, "空文件应该返回错误");
}

TEST(ConfigLoaderTest, FileSize_MinValid)
{
    std::string config_content = GetDefaultConfigTemplate();
    std::string config_path = CreateTestConfigFile(config_content.c_str());
    kadd_config_t config;
    int ret = kadd_config_load(config_path.c_str(), &config);

    EXPECT_EQ(ret, KADD_SUCCESS) << "最小有效配置应该成功加载";
    DeleteTestFile(config_path);
}

// ========== static 函数测试 ==========

/**
 * safe_copy 函数测试
 */
TEST(ConfigLoaderStaticTest, SafeCopy_Normal)
{
    char dest[64] = {0};
    const char *src = "hello world";

    safe_copy(dest, src, sizeof(dest));

    EXPECT_STREQ(dest, "hello world");
}

TEST(ConfigLoaderStaticTest, SafeCopy_Truncate)
{
    char dest[8] = {0};
    const char *src = "hello world long string";

    safe_copy(dest, src, sizeof(dest));

    // 应该被截断，且保留一个字节给 '\0'
    EXPECT_EQ(strlen(dest), 7);
    EXPECT_EQ(dest[7], '\0');
}

TEST(ConfigLoaderStaticTest, SafeCopy_Empty)
{
    char dest[64] = "existing";
    const char *src = "";

    safe_copy(dest, src, sizeof(dest));

    EXPECT_STREQ(dest, "");
}

/**
 * read_file 函数测试
 */
TEST(ConfigLoaderStaticTest, ReadFile_InvalidPath)
{
    char *content = nullptr;

    int ret = read_file("/nonexistent/path/file.json", &content);

    EXPECT_NE(ret, KADD_SUCCESS);
    EXPECT_EQ(content, nullptr);
}

TEST(ConfigLoaderStaticTest, ReadFile_NullPath)
{
    char *content = nullptr;

    int ret = read_file(nullptr, &content);

    EXPECT_NE(ret, KADD_SUCCESS);
}

TEST(ConfigLoaderStaticTest, ReadFile_ValidFile)
{
    const char *test_file = "test_static_temp.txt";
    const char *test_content = "test content 123";

    FILE *fp = fopen(test_file, "w");
    ASSERT_NE(fp, nullptr);
    fputs(test_content, fp);
    fclose(fp);

    char *content = nullptr;
    int ret = read_file(test_file, &content);

    EXPECT_EQ(ret, KADD_SUCCESS);
    EXPECT_NE(content, nullptr);
    EXPECT_STREQ(content, test_content);

    free(content);
    remove(test_file);
}

TEST(ConfigLoaderStaticTest, ReadFile_EmptyFile)
{
    const char *test_file = "test_empty.txt";

    FILE *fp = fopen(test_file, "w");
    ASSERT_NE(fp, nullptr);
    fclose(fp);

    char *content = nullptr;
    int ret = read_file(test_file, &content);

    // 空文件可能返回错误（file_size <= 0）
    EXPECT_NE(ret, KADD_SUCCESS);

    remove(test_file);
}

// ========== 错误处理测试 ==========

/**
 * 文件操作失败测试 - 无效文件路径
 * 覆盖 read_file 中的错误处理路径（fopen 失败）
 */
TEST(ConfigLoaderErrorTest, FseekFailure_InvalidPath)
{
    kadd_config_t config;
    int ret = kadd_config_load("nonexistent_config.json", &config);
    EXPECT_NE(ret, KADD_SUCCESS);
}

/**
 * 文件读取测试 - 小文件正常处理
 * 测试文件读取的正常流程
 */
TEST(ConfigLoaderErrorTest, IncompleteRead_SmallFile)
{
    // 创建一个临时文件
    const char *temp_file = "/tmp/truncated_test.json";
    FILE *fp = fopen(temp_file, "w");

    ASSERT_NE(fp, nullptr) << "无法创建临时文件";

    // 写入一个最小有效配置
    const char *min_config = R"({
        "transition_table": "/tmp/t.bin",
        "output_table": "/tmp/o.bin",
        "eod_table": "/tmp/e.bin",
        "model_rf": "/tmp/r.onnx",
        "model_cnn": "/tmp/c.onnx"
    })";

    fputs(min_config, fp);
    fclose(fp);

    // 正常加载应该成功
    kadd_config_t config;
    int ret = kadd_config_load(temp_file, &config);

    EXPECT_EQ(ret, KADD_SUCCESS) << "最小有效配置应该成功加载";

    if (ret == KADD_SUCCESS) {
        kadd_config_free(&config);
    }

    remove(temp_file);
}


// ========== engine_mode 解析测试 ==========

TEST(ConfigLoaderTest, EngineMode_ML)
{
    std::string config_content = R"({
        "transition_table": "/path/to/transition.bin",
        "output_table": "/path/to/output.bin",
        "eod_table": "/path/to/eod.bin",
        "engine_mode": "ml",
        "model_rf": "/path/to/model_rf.krf",
        "model_rf_onnx": "/path/to/model_rf.onnx",
        "model_cnn": "/path/to/model_cnn.kcn",
        "model_cnn_onnx": "/path/to/model_cnn.onnx"
    })";

    std::string config_path = CreateTestConfigFile(config_content.c_str());
    kadd_config_t config;
    int ret = kadd_config_load(config_path.c_str(), &config);
    EXPECT_EQ(ret, KADD_SUCCESS);
    EXPECT_EQ(config.engine_mode, KADD_ENGINE_MODE_ML);
    kadd_config_free(&config);
    DeleteTestFile(config_path);
}

TEST(ConfigLoaderTest, EngineMode_DL)
{
    std::string config_content = R"({
        "transition_table": "/path/to/transition.bin",
        "output_table": "/path/to/output.bin",
        "eod_table": "/path/to/eod.bin",
        "engine_mode": "dl",
        "model_rf": "/path/to/model_rf.krf",
        "model_rf_onnx": "/path/to/model_rf.onnx",
        "model_cnn": "/path/to/model_cnn.kcn",
        "model_cnn_onnx": "/path/to/model_cnn.onnx"
    })";

    std::string config_path = CreateTestConfigFile(config_content.c_str());
    kadd_config_t config;
    int ret = kadd_config_load(config_path.c_str(), &config);
    EXPECT_EQ(ret, KADD_SUCCESS);
    EXPECT_EQ(config.engine_mode, KADD_ENGINE_MODE_DL);
    kadd_config_free(&config);
    DeleteTestFile(config_path);
}

TEST(ConfigLoaderTest, EngineMode_Empty)
{
    std::string config_content = R"({
        "transition_table": "/path/to/transition.bin",
        "output_table": "/path/to/output.bin",
        "eod_table": "/path/to/eod.bin",
        "engine_mode": "",
        "model_rf": "/path/to/model_rf.krf",
        "model_rf_onnx": "/path/to/model_rf.onnx",
        "model_cnn": "/path/to/model_cnn.kcn",
        "model_cnn_onnx": "/path/to/model_cnn.onnx"
    })";

    std::string config_path = CreateTestConfigFile(config_content.c_str());
    kadd_config_t config;
    int ret = kadd_config_load(config_path.c_str(), &config);
    EXPECT_EQ(ret, KADD_SUCCESS);
    EXPECT_EQ(config.engine_mode, KADD_ENGINE_MODE_ML) << "空字符串应该默认为ML模式";
    kadd_config_free(&config);
    DeleteTestFile(config_path);
}
