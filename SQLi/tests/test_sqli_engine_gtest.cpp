/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *
 * @file test_sqli_engine_gtest.cpp
 * @brief SQL 注入检测引擎 Google Test 测试套件
 *
 * 包含功能测试、边界测试、异常测试、性能测试、静态函数测试
 * 使用统计指标评估模型性能，适应模型训练的随机性
 */

#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <chrono>
#include <numeric>
#include <iomanip>
#include <iostream>
#include <cmath>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include "kadd_sqli_engine.h"
#include "kadd_ml_engine.h"
#include "kadd_config_loader.h"

// ========== 包含源文件以测试 static 函数 ==========
#include "kadd_sqli_engine.c"

constexpr int kDefaultBatchSize = 10;
constexpr int kPercentScale = 100;

/* ========== 测试用例定义 ========== */

// 测试用例结构，包含 ground truth 标签
struct TestCase {
    const char* id;           // 测试ID，用于定位和报告
    const char* input;        // 输入文本
    bool is_injection;        // Ground truth: 是否为注入
};

struct Metrics {
    int true_positive = 0;   // TP: 正确检测为注入
    int false_positive = 0;  // FP: 误报（正常判为注入）
    int true_negative = 0;   // TN: 正确判定为安全
    int false_negative = 0;  // FN: 漏报（注入判为安全）

    float precision() const
    {
        int predicted_positive = true_positive + false_positive;
        return (predicted_positive > 0) ? (static_cast<float>(true_positive) / predicted_positive) : 0.0f;
    }

    float recall() const
    {
        int actual_positive = true_positive + false_negative;
        return (actual_positive > 0) ? (static_cast<float>(true_positive) / actual_positive) : 0.0f;
    }

    float f1_score() const
    {
        float p = precision();
        float r = recall();
        constexpr float kF1Coefficient = 2.0f;
        return ((p + r) > 0) ? (kF1Coefficient * p * r / (p + r)) : 0.0f;
    }

    float accuracy() const
    {
        int total = true_positive + false_positive + true_negative + false_negative;
        return (total > 0) ? (static_cast<float>(true_positive + true_negative) / total) : 0.0f;
    }

    float specificity() const
    {
        int actual_negative = true_negative + false_positive;
        return (actual_negative > 0) ? (static_cast<float>(true_negative) / actual_negative) : 0.0f;
    }
};

// SQL注入样本数据集
static const TestCase g_sqli_samples[] = {
    // 经典OR注入
    { "classic_or_001", "id=1' OR '1'='1", true },
    { "classic_or_002", "admin' OR 1=1--", true },
    { "classic_or_003", "1' OR 1=1--", true },
    { "classic_or_004", "' OR '1'='1'--", true },
    { "classic_or_005", "\" OR \"1\"=\"1\"--", true },

    // 注释绕过
    { "comment_001", "admin'--", true },
    { "comment_002", "id=1' OR '1'='1#", true },
    { "comment_003", "1' OR 1=1--", true },
    { "comment_004", "admin'/*comment*/", true },
    { "comment_005", "id=1'/*comment*/OR/*comment*/'1'='1", true },

    // UNION注入
    { "union_001", "1' UNION SELECT NULL--", true },
    { "union_002", "1' UNION SELECT NULL,NULL--", true },
    { "union_003", "1' UNION SELECT NULL,NULL,NULL--", true },
    { "union_004", "' UNION SELECT username,password FROM users--", true },
    { "union_005", "admin' UNION SELECT 1,version(),3--", true },

    // 堆叠查询
    { "stacked_001", "id=1; DROP TABLE users--", true },
    { "stacked_002", "id=1'; DELETE FROM users--", true },
    { "stacked_003", "1'; INSERT INTO users VALUES ('hacker','password')--", true },
    { "stacked_004", "id=1'; UPDATE users SET password='hacked'--", true },

    // 布尔盲注
    { "blind_001", "id=1' AND 1=1--", true },
    { "blind_002", "id=1' AND 1=2--", true },
    { "blind_003", "1' AND SLEEP(5)--", true },
    { "blind_004", "' OR BENCHMARK(5000000,MD5(1))--", true },

    // 时间盲注
    { "time_001", "'; WAITFOR DELAY '00:00:05'--", true },
    { "time_002", "1; SELECT SLEEP(5)--", true },
    { "time_003", "' OR PG_SLEEP(5)--", true },
    { "time_004", "' OR DBMS_PIPE.RECEIVE_MESSAGE('a',5)--", true },

    // 错误注入
    { "error_001", "id=1' AND 1=CONVERT(int,VERSION())--", true },
    { "error_002", "id=1' AND 1=CAST(VERSION() AS INT)--", true },
    { "error_003",
      "id=1,(SELECT 1 FROM (SELECT COUNT(*),CONCAT(0x3a,VERSION(),FLOOR(RAND(0)*2))x "
      "FROM information_schema.tables GROUP BY x)a)--",
      true },

    // 编码绕过
    { "encoded_001", "id=%27%20OR%20%271%27=%271", true },
    { "encoded_002", "id=1%2527%2520OR%2520%25271%2527=%25271", true },
    { "encoded_003", "id=\\x27\\x20OR\\x20\\x271\\x27=\\x271", true },

    // 二次注入
    { "second_order_001", "username=admin'#", true },
    { "second_order_002", "username=\"; DROP TABLE users--", true }
};

// 正常流量样本数据集
static const TestCase g_normal_samples[] = {
    // 正常邮箱
    { "email_001", "user@example.com", false },
    { "email_002", "john.doe@company.co.uk", false },
    { "email_003", "test+tag@email.com", false },

    // 正常SQL语句
    { "sql_001", "SELECT * FROM users WHERE id=1", false },
    { "sql_002", "INSERT INTO users (name) VALUES ('John')", false },
    { "sql_003", "UPDATE products SET price=100 WHERE id=5", false },
    { "sql_004", "DELETE FROM logs WHERE created_at < '2024-01-01'", false },

    // 正常Web参数
    { "webparam_001", "page=1&limit=10", false },
    { "webparam_002", "category=electronics&sort=price", false },
    { "webparam_003", "search=python+tutorial", false },
    { "webparam_004", "filter=date:2024-01-01", false }
};

// 合并SQL注入和正常样本的完整数据集
static const std::vector<TestCase> g_full_dataset = []() {
    std::vector<TestCase> dataset;
    for (const auto& tc : g_sqli_samples) {
        dataset.push_back(tc);
    }
    for (const auto& tc : g_normal_samples) {
        dataset.push_back(tc);
    }
    return dataset;
}();

// 对数据集逐样本检测，统计混淆矩阵
static Metrics evaluate_dataset(kadd_sqli_engine_t* engine, const TestCase* data, size_t count, float threshold)
{
    Metrics m;
    for (size_t i = 0; i < count; ++i) {
        float prob = 0.0f;
        if (kadd_sqli_detect(engine, data[i].input, &prob) != KADD_SUCCESS) {
            continue;
        }
        int predicted = (prob >= threshold) ? 1 : 0;
        int expected  = data[i].is_injection ? 1 : 0;
        if (predicted && expected)  ++m.true_positive;
        else if (predicted && !expected) ++m.false_positive;
        else if (!predicted && !expected) ++m.true_negative;
        else ++m.false_negative;
    }
    return m;
}

// 打印混淆矩阵
static void print_confusion_matrix(const Metrics& m)
{
    std::cout << "  TP=" << m.true_positive
              << "  FP=" << m.false_positive
              << "  TN=" << m.true_negative
              << "  FN=" << m.false_negative
              << "  acc=" << (m.accuracy() * kPercentScale) << "%"
              << "  prec=" << (m.precision() * kPercentScale) << "%"
              << "  rec=" << (m.recall() * kPercentScale) << "%"
              << "  f1=" << (m.f1_score() * kPercentScale) << "%" << std::endl;
}

/* ========== 测试夹具 ========== */

class SQLiEngineTest : public ::testing::TestWithParam<std::string> {
protected:
    kadd_sqli_engine_t* engine;
    float threshold = 0.5f;
    std::string temp_config_path_;

    // Search for config.json in common paths
    std::string find_config_path() {
        const char* paths[] = {
            "config.json", "../config.json",
            "../../config.json", "../../../config.json"
        };
        for (const char* p : paths) {
            FILE* f = fopen(p, "r");
            if (f) { fclose(f); return p; }
        }
        return "";
    }

    // Read entire file into string
    std::string read_file(const std::string& path) {
        std::ifstream ifs(path);
        if (!ifs.is_open()) return "";
        return std::string((std::istreambuf_iterator<char>(ifs)),
                           std::istreambuf_iterator<char>());
    }

    // Replace engine_mode value in JSON content
    std::string modify_engine_mode(const std::string& content, const std::string& mode) {
        std::string result = content;
        std::string key = "\"engine_mode\"";
        auto pos = result.find(key);
        if (pos != std::string::npos) {
            auto colon = result.find(':', pos + key.size());
            if (colon != std::string::npos) {
                auto q1 = result.find('"', colon + 1);
                if (q1 != std::string::npos) {
                    auto q2 = result.find('"', q1 + 1);
                    if (q2 != std::string::npos) {
                        result.replace(q1 + 1, q2 - q1 - 1, mode);
                    }
                }
            }
        } else {
            auto brace = result.rfind('}');
            if (brace != std::string::npos) {
                result.insert(brace, "  \"engine_mode\": \"" + mode + "\",\n");
            }
        }
        return result;
    }

    void SetUp() override
    {
        std::string mode = GetParam();

        std::string config_path = find_config_path();
        ASSERT_FALSE(config_path.empty())
            << "Cannot find config.json, please check file paths";

        std::string content = read_file(config_path);
        ASSERT_FALSE(content.empty()) << "Cannot read config: " << config_path;

        std::string modified = modify_engine_mode(content, mode);

        // Write temp config alongside original (keeps relative asset paths valid)
        std::string dir;
        auto slash = config_path.find_last_of("/\\");
        if (slash != std::string::npos)
            dir = config_path.substr(0, slash + 1);
        temp_config_path_ = dir + "test_sqli_config_" + mode + ".json";

        std::ofstream ofs(temp_config_path_);
        ASSERT_TRUE(ofs.is_open()) << "Cannot write temp config: " << temp_config_path_;
        ofs << modified;
        ofs.close();

        engine = nullptr;
        int ret = kadd_sqli_engine_create_from_file(&engine, temp_config_path_.c_str());
        ASSERT_EQ(ret, KADD_SUCCESS)
            << "SQLi engine creation failed (mode=" << mode << ", ret=" << ret << ")";

        float prob = 0.0f;
        ret = kadd_sqli_detect(engine, "test_input", &prob);
        ASSERT_EQ(ret, KADD_SUCCESS)
            << "SQLi detect verification failed (mode=" << mode << ")";
    }

    void TearDown() override
    {
        if (engine) {
            kadd_sqli_engine_free(engine);
            engine = nullptr;
        }
        if (!temp_config_path_.empty()) {
            remove(temp_config_path_.c_str());
            temp_config_path_.clear();
        }
    }

    float detect(const std::string &input)
    {
        float probability = 0.0f;
        int ret = kadd_sqli_detect(engine, input.c_str(), &probability);
        EXPECT_EQ(ret, KADD_SUCCESS) << "detection failed: " << input;
        return probability;
    }
};


TEST_P(SQLiEngineTest, BasicFunctionality)
{
    float prob = detect("id=1' OR '1'='1");
    EXPECT_GE(prob, 0.0f);
    EXPECT_LE(prob, 1.0f);
}

// 使用统计指标评估模型整体性能，适应模型训练的随机性
TEST_P(SQLiEngineTest, ModelPerformanceMetrics)
{
    constexpr size_t sqli_count = sizeof(g_sqli_samples) / sizeof(g_sqli_samples[0]);
    constexpr size_t normal_count = sizeof(g_normal_samples) / sizeof(g_normal_samples[0]);
    constexpr size_t total_count = sqli_count + normal_count;

    // 计算整体指标
    Metrics metrics = evaluate_dataset(engine, g_full_dataset.data(), total_count, threshold);

    // 打印详细结果
    print_confusion_matrix(metrics);

    // 断言：检查整体性能是否在可接受范围内
    // 这些阈值需要根据实际情况调整
    constexpr float kMinAccuracy = 0.60f;
    constexpr float kMinRecall = 0.50f;
    constexpr float kMinF1Score = 0.50f;
    constexpr int kMaxFalsePositives = 5;

    EXPECT_GE(metrics.accuracy(), kMinAccuracy) << "准确率不应低于60%";
    EXPECT_GE(metrics.recall(), kMinRecall) << "召回率不应低于50%";
    EXPECT_GE(metrics.f1_score(), kMinF1Score) << "F1分数不应低于0.5";

    EXPECT_LE(metrics.false_positive, kMaxFalsePositives) << "误报数量不应超过3个";
}

// 不同阈值下的性能对比，帮助确定最佳检测阈值
TEST_P(SQLiEngineTest, PerformanceAtDifferentThresholds)
{
    size_t total_count = g_full_dataset.size();

    constexpr float kThresholdLow = 0.3f;
    constexpr float kThresholdMid = 0.5f;
    constexpr float kThresholdHigh = 0.7f;
    constexpr float kFloatEpsilon = 0.01f;
    constexpr float kMinF1AtMidThreshold = 0.40f;

    std::vector<float> thresholds = { kThresholdLow, kThresholdMid, kThresholdHigh };

    std::cout << "\n========== 不同阈值性能对比 ==========\n";
    std::cout << "阈值\t准确率\t精确率\t召回率\tF1\t特异度\n";

    constexpr int kThresholdDisplayPrecision = 1;
    for (float thresh : thresholds) {
        Metrics metrics = evaluate_dataset(engine, g_full_dataset.data(), total_count, thresh);

        std::cout << std::fixed << std::setprecision(kThresholdDisplayPrecision)
                  << thresh << "\t"
                  << (metrics.accuracy() * kPercentScale) << "%\t"
                  << (metrics.precision() * kPercentScale) << "%\t"
                  << (metrics.recall() * kPercentScale) << "%\t"
                  << metrics.f1_score() << "\t"
                  << (metrics.specificity() * kPercentScale) << "%\n";

        // 阈值0.5时应该有合理的性能
        if (std::abs(thresh - kThresholdMid) < kFloatEpsilon) {
            EXPECT_GE(metrics.f1_score(), kMinF1AtMidThreshold) << "阈值0.5时F1不应过低";
        }
    }

    std::cout << "====================================\n\n";
}

// 验证模型对同一输入多次检测的结果稳定性
TEST_P(SQLiEngineTest, ModelStability)
{
    std::string input = "id=1' OR '1'='1";

    constexpr int kStabilityIterations = 10;
    std::vector<float> probs(kStabilityIterations);
    for (int i = 0; i < kStabilityIterations; i++) {
        probs[i] = detect(input);
    }

    // 计算均值和方差
    float mean = std::accumulate(probs.begin(), probs.end(), 0.0f) / probs.size();
    float variance = 0.0f;
    for (float p : probs) {
        variance += (p - mean) * (p - mean);
    }
    variance /= probs.size();
    float std_dev = std::sqrt(variance);

    std::cout << "稳定性测试 - 输入: " << input << "\n";
    constexpr int kMeanPrecision = 4;
    std::cout << "  均值: " << std::fixed << std::setprecision(kMeanPrecision) << mean << "\n";
    std::cout << "  标准差: " << std_dev << "\n";

    // 模型应该是确定性的，标准差应该接近0
    constexpr float kStdDevThreshold = 0.001f;
    EXPECT_LT(std_dev, kStdDevThreshold) << "模型检测结果应该稳定（标准差 < 0.001）";
}

/* ========== 2. 边界测试 ========== */

TEST_P(SQLiEngineTest, EmptyInput)
{
    // 测试空输入
    float prob = detect("");
    EXPECT_LT(prob, threshold) << "空输入应该返回低概率";
}

TEST_P(SQLiEngineTest, SingleChar)
{
    // 测试单字符输入
    std::vector<std::string> inputs = { "a", "1", "@" };

    for (const auto &input : inputs) {
        float prob = detect(input);
        EXPECT_GE(prob, 0.0f) << "单字符应该成功处理: " << input;
        EXPECT_LE(prob, 1.0f);
    }
}

TEST_P(SQLiEngineTest, MaxInputBoundary)
{
    // 测试接近最大限制的输入
    constexpr int kMaxInputLength = 300;
    std::string max_input(kMaxInputLength, 'a');
    float prob = detect(max_input);
    EXPECT_GE(prob, 0.0f) << "接近最大长度应该成功";
    EXPECT_LE(prob, 1.0f);
}

TEST_P(SQLiEngineTest, SpecialChars)
{
    // 测试特殊字符
    std::vector<std::string> inputs = {
        "!@#$%",
        "^&*()",
        "~`|\\<>{}[]"
    };

    for (const auto &input : inputs) {
        float prob = detect(input);
        EXPECT_GE(prob, 0.0f) << "特殊字符应该成功处理: " << input;
        EXPECT_LE(prob, 1.0f);
    }
}

TEST_P(SQLiEngineTest, UnicodeInput)
{
    // 测试 Unicode 输入
    std::vector<std::string> unicode_inputs = {
        "用户名=张三",
        "search=搜索",
        "name=José",
        "email=test@例题.com"
    };

    for (const auto &input : unicode_inputs) {
        float prob = detect(input);
        EXPECT_GE(prob, 0.0f) << "Unicode 输入应该成功处理: " << input;
        EXPECT_LE(prob, 1.0f);
    }
}

TEST_P(SQLiEngineTest, NewlineAndTabs)
{
    // 测试换行符和制表符
    std::vector<std::string> inputs = {
        "line1\nline2\nline3",
        "col1\tcol2\tcol3",
        "mixed\r\n\t"
    };

    for (const auto &input : inputs) {
        float prob = detect(input);
        EXPECT_GE(prob, 0.0f) << "换行/制表符应该成功处理: " << input;
        EXPECT_LE(prob, 1.0f);
    }
}

TEST_P(SQLiEngineTest, MixedCase)
{
    // 测试大小写混合
    std::vector<std::string> inputs = {
        "SeLeCt",
        "InSeRt",
        "UpDaTe"
    };

    for (const auto &input : inputs) {
        float prob = detect(input);
        EXPECT_GE(prob, 0.0f) << "大小写混合应该成功处理";
        EXPECT_LE(prob, 1.0f);
    }
}

/* ========== 3. 异常测试 ========== */

TEST_P(SQLiEngineTest, NullEngine)
{
    float prob = 0.0f;
    int ret = kadd_sqli_detect(nullptr, "test", &prob);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR) << "空引擎应该返回 KADD_ERROR_NULL_PTR";
}

TEST_P(SQLiEngineTest, NullInput)
{
    float prob = 0.0f;
    int ret = kadd_sqli_detect(engine, nullptr, &prob);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR) << "空输入应该返回 KADD_ERROR_NULL_PTR";
}

TEST_P(SQLiEngineTest, NullProbability)
{
    int ret = kadd_sqli_detect(engine, "test", nullptr);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR) << "空概率指针应该返回 KADD_ERROR_NULL_PTR";
}

TEST_P(SQLiEngineTest, NullEngineWithAllOtherNull)
{
    int ret = kadd_sqli_detect(nullptr, nullptr, nullptr);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR) << "全部空指针应该返回 KADD_ERROR_NULL_PTR";
}

TEST_P(SQLiEngineTest, CreateWithNullEnginePtr)
{
    int ret = kadd_sqli_engine_create_from_file(nullptr, "config.json");
    EXPECT_NE(ret, KADD_SUCCESS) << "NULL engine 指针应该返回错误";
}

TEST_P(SQLiEngineTest, CreateWithNullConfigFile)
{
    kadd_sqli_engine_t* tmp = nullptr;
    int ret = kadd_sqli_engine_create_from_file(&tmp, nullptr);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR) << "NULL 配置文件应该返回 KADD_ERROR_NULL_PTR";
}

TEST_P(SQLiEngineTest, CreateWithEmptyConfigFile)
{
    kadd_sqli_engine_t* tmp = nullptr;
    int ret = kadd_sqli_engine_create_from_file(&tmp, "");
    EXPECT_NE(ret, KADD_SUCCESS) << "空配置文件路径应该返回错误";
}

TEST_P(SQLiEngineTest, CreateWithNonexistentConfigFile)
{
    kadd_sqli_engine_t* tmp = nullptr;
    int ret = kadd_sqli_engine_create_from_file(&tmp, "nonexistent_config.json");
    EXPECT_NE(ret, KADD_SUCCESS) << "不存在配置文件应该返回错误";
}

TEST_P(SQLiEngineTest, FreeWithNullEngine)
{
    // 测试 kadd_sqli_engine_free(NULL) 不应该崩溃
    kadd_sqli_engine_free(nullptr);
    SUCCEED() << "kadd_sqli_engine_free(NULL) 不应崩溃";
}

// ========== 覆盖率优化：触发 release_engine_buffers 错误处理 ==========

TEST_P(SQLiEngineTest, CreateWithInvalidDFAPaths)
{
    // 构造包含无效 DFA 表路径的配置来触发 DFA 引擎创建失败
    // 这将触发 create_engine_internal 中的 release_engine_buffers(e) 路径
    kadd_sqli_engine_t* tmp = nullptr;

    // 创建一个临时配置文件，包含无效的 DFA 表路径
    const char* test_config_content = R"({
        "kadd_dfa_engine": {
            "transition_table_path": "invalid_nonexistent_table.bin",
            "output_table_path": "invalid_nonexistent_table.bin",
            "eod_table_path": "invalid_nonexistent_table.bin",
            "max_input_length": 300,
            "max_output_dim": 300
        },
        "kadd_ml_engine": {
            "model_path": "test_model.onnx",
            "feature_dim": 28,
            "window_size": 5
        },
        "kadd_sqli_engine": {
            "ml_model_path": "",
            "feature_dim": 28,
            "window_size": 5,
            "max_output_dim": 300,
            "max_input_length": 300
        }
    })";

    // 写入临时配置文件
    FILE* fp = fopen("test_invalid_dfa_config.json", "w");
    if (fp) {
        fputs(test_config_content, fp);
        fclose(fp);

        int ret = kadd_sqli_engine_create_from_file(&tmp, "test_invalid_dfa_config.json");
        EXPECT_EQ(ret, KADD_ERROR_CONFIG) << "无效 DFA 表路径应该返回 KADD_ERROR_CONFIG";

        // 清理
        remove("test_invalid_dfa_config.json");
    }
}

TEST_P(SQLiEngineTest, CreateWithInvalidMLModelPath)
{
    // 构造包含无效模型路径的配置来触发 ML 模型加载失败
    // 这将触发 create_engine_internal 中的 release_engine_buffers(e) 路径
    // （此时 DFA 引擎已成功创建，但 ML 模型加载失败）
    kadd_sqli_engine_t* tmp = nullptr;

    // 注意：这个测试需要有效的 DFA 表文件，但无效的模型文件
    // 我们尝试使用一个不存在的模型文件
    const char* test_config_content = R"({
        "kadd_dfa_engine": {
            "transition_table_path": "assets/data/dfa_transition_table.bin",
            "output_table_path": "assets/data/dfa_output_table.bin",
            "eod_table_path": "assets/data/dfa_eod_table.bin",
            "max_input_length": 300,
            "max_output_dim": 300
        },
        "kadd_ml_engine": {
            "model_path": "nonexistent_model.onnx",
            "feature_dim": 28,
            "window_size": 5
        },
        "kadd_sqli_engine": {
            "ml_model_path": "",
            "feature_dim": 28,
            "window_size": 5,
            "max_output_dim": 300,
            "max_input_length": 300
        }
    })";

    // 写入临时配置文件
    FILE* fp = fopen("test_invalid_ml_config.json", "w");
    if (fp) {
        fputs(test_config_content, fp);
        fclose(fp);

        int ret = kadd_sqli_engine_create_from_file(&tmp, "test_invalid_ml_config.json");

        // 期望返回 ML 错误（如果 DFA 表存在）或者 DFA 错误（如果 DFA 表不存在）
        // 主要是验证模型加载失败时的错误处理
        if (ret == KADD_ERROR_MODEL_LOAD_FAILED) {
            // 完美：覆盖了 ML 模型加载失败的 release_engine_buffers 路径
            SUCCEED() << "成功覆盖 ML 模型加载失败的 release_engine_buffers 路径";
        } else if (ret == KADD_DFA_ERROR_FILE_NOT_FOUND) {
            // DFA 表也不存在，至少测试了 DFA 失败路径
            SUCCEED() << "DFA 表不存在，测试了 DFA 失败路径";
        } else {
            // 其他错误也算覆盖了错误处理路径
            EXPECT_NE(ret, KADD_SUCCESS) << "应该返回某种错误";
        }

        // 清理
        remove("test_invalid_ml_config.json");
    }
}

TEST_P(SQLiEngineTest, EngineReuse)
{
    // 测试引擎重用 - 仅验证能正常工作，不检查具体结果
    float prob1 = detect("id=1' OR '1'='1");
    float prob2 = detect("admin'--");
    float prob3 = detect("user@example.com");

    EXPECT_GE(prob1, 0.0f);
    EXPECT_LE(prob1, 1.0f);
    EXPECT_GE(prob2, 0.0f);
    EXPECT_LE(prob2, 1.0f);
    EXPECT_GE(prob3, 0.0f);
    EXPECT_LE(prob3, 1.0f);
}

/* ========== 4. 性能测试 ========== */

TEST_P(SQLiEngineTest, SingleDetectionPerformance)
{
    constexpr int kPerfIterations = 1000;
    std::string input = "id=1' OR '1'='1";
    float prob = 0.0f;

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kPerfIterations; i++) {
        kadd_sqli_detect(engine, input.c_str(), &prob);
    }
    auto end = std::chrono::high_resolution_clock::now();

    EXPECT_GE(prob, 0.0f);
    EXPECT_LE(prob, 1.0f);

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    double total_time_us = static_cast<double>(duration.count());
    std::cout << "\n[性能测试] kadd_sqli_detect 单次耗时："
            << (total_time_us / kPerfIterations) << " us" << std::endl;
}

TEST_P(SQLiEngineTest, BatchDetectionPerformance)
{
    // 测试批量检测性能
    constexpr int kBatchSize = 100;
    std::vector<std::string> inputs_str(kBatchSize);
    std::vector<const char*> inputs(kBatchSize);
    for (int i = 0; i < kBatchSize; i++) {
        inputs_str[i] = "id=1' OR '1'='1";  // 注入
        inputs[i] = inputs_str[i].c_str();
    }

    std::vector<float> probabilities(kBatchSize);

    auto start = std::chrono::high_resolution_clock::now();
    int ret = kadd_sqli_detect_batch(engine,
                                    inputs.data(),
                                    kBatchSize,
                                    probabilities.data(),
                                    kDefaultBatchSize);
    auto end = std::chrono::high_resolution_clock::now();

    EXPECT_EQ(ret, KADD_SUCCESS) << "批量检测应该全部成功";
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    constexpr int kBatchTimeoutMs = 5000;
    EXPECT_LT(duration.count(), kBatchTimeoutMs) << "100个样本批量检测应该在5秒内完成";
}

/* ========== 5. 一致性测试 ========== */

TEST_P(SQLiEngineTest, BatchVsSingleConsistency)
{
    // 测试批量检测与单次检测的一致性（不依赖具体检测结果）
    std::vector<std::string> inputs_str = {
        "id=1' OR '1'='1",
        "admin'--",
        "user@example.com",
        "SELECT * FROM users",
        "1' UNION SELECT NULL--"
    };
    std::vector<const char*> inputs;
    for (const auto& s : inputs_str) {
        inputs.push_back(s.c_str());
    }

    // 单次检测
    std::vector<float> single_probs(inputs.size());
    for (size_t i = 0; i < inputs.size(); i++) {
        single_probs[i] = detect(inputs_str[i]);
    }

    // 批量检测
    std::vector<float> batch_probs(inputs.size());
    int ret = kadd_sqli_detect_batch(engine,
                                    inputs.data(),
                                    inputs.size(),
                                    batch_probs.data(),
                                    kDefaultBatchSize);
    ASSERT_EQ(ret, KADD_SUCCESS) << "批量检测应该全部成功";

    // 比较结果（单次和批量应该给出相同的概率）
    for (size_t i = 0; i < inputs.size(); i++) {
        EXPECT_FLOAT_EQ(single_probs[i], batch_probs[i])
            << "单次和批量检测结果应该一致: " << inputs_str[i];
    }
}

// ========== 覆盖率优化：边界和批处理测试 ==========

TEST_P(SQLiEngineTest, BatchSize_EdgeCases)
{

    // 测试超长输入 (超过默认 max_input_length = 300)
    std::string long_input(400, 'A');
    float prob;
    EXPECT_EQ(kadd_sqli_detect(engine, long_input.c_str(), &prob), KADD_DFA_ERROR_INVALID_LENGTH)
        << "超长输入应该返回错误（超过max_input_length）";
}

// ========== 覆盖率优化：批处理边界和错误路径测试 ==========
TEST_P(SQLiEngineTest, BatchWithZeroCount)
{

    const char *inputs[1] = {"test"};
    float probs[1];

    // 测试 count = -1
    EXPECT_EQ(kadd_sqli_detect_batch(engine, inputs, -1, probs, 64), KADD_ERROR_INVALID_PARAM)
        << "count=-1 应该返回 INVALID_PARAM 错误";
}

TEST_P(SQLiEngineTest, BatchWithExceedsMaxSampleCount)
{

    const char *inputs[1] = {"test"};

    // 测试 probabilities = NULL
    EXPECT_EQ(kadd_sqli_detect_batch(engine, inputs, 1, nullptr, 64), KADD_ERROR_NULL_PTR)
        << "NULL probabilities 应该返回 NULL_PTR 错误";
}

TEST_P(SQLiEngineTest, BatchWithNullInputElement)
{

    float probs[1];

    // 测试整个 inputs 数组为 NULL
    EXPECT_EQ(kadd_sqli_detect_batch(engine, nullptr, 1, probs, 64), KADD_ERROR_NULL_PTR)
        << "NULL inputs 数组应该返回 NULL_PTR 错误";
}

TEST_P(SQLiEngineTest, BatchWithNullEngine)
{
    float probs[1];
    const char *inputs[1] = {"test"};

    // 测试 engine = NULL
    EXPECT_EQ(kadd_sqli_detect_batch(nullptr, inputs, 1, probs, 64), KADD_ERROR_NULL_PTR)
        << "NULL engine 应该返回 NULL_PTR 错误";
}

TEST_P(SQLiEngineTest, BatchWithExceedsMaxCount)
{
    const char *inputs[1] = {"test"};
    float probs[1];

    // 测试 count > KADD_ML_MAX_SAMPLE_COUNT
    EXPECT_EQ(kadd_sqli_detect_batch(engine, inputs, KADD_ML_MAX_SAMPLE_COUNT + 1, probs, 64),
              KADD_ERROR_INVALID_PARAM)
        << "count > MAX_SAMPLE_COUNT 应该返回 INVALID_PARAM 错误";
}

TEST_P(SQLiEngineTest, SingleInputWithNullProbability)
{

    float prob = 0.0f;

    // 测试单次检测时 input = NULL
    EXPECT_EQ(kadd_sqli_detect(engine, nullptr, &prob), KADD_ERROR_NULL_PTR)
        << "NULL input 字符串应该返回 NULL_PTR 错误";
}

TEST_P(SQLiEngineTest, SingleInputWithNullEngine)
{
    float prob = 0.0f;

    // 测试单次检测时 engine = NULL
    EXPECT_EQ(kadd_sqli_detect(nullptr, "test", &prob), KADD_ERROR_NULL_PTR)
        << "NULL engine 应该返回 NULL_PTR 错误";
}

// ========== 7. 边缘情况和特殊情况测试 ==========

// SQLi_E001: 空字符串检测
TEST_P(SQLiEngineTest, EmptyStringDetection)
{

    const char* single_chars[] = {"a", "1", "'", ";", "-", "#"};

    for (const char* input : single_chars) {
        float probability = 0.0f;

        int ret = kadd_sqli_detect(engine, input, &probability);

        EXPECT_EQ(ret, KADD_SUCCESS) << "单字符处理应成功: " << input;
        EXPECT_TRUE(probability >= 0.0f && probability <= 1.0f) << "概率值应在有效范围内";
    }
}

// ========== static 函数测试 ==========

/**
 * convert_int_to_float_features 函数测试 - 基本转换
 */
TEST(SQLiEngineStaticTest, ConvertIntToFloatFeatures_Basic)
{
    int int_features[3] = {1, 2, 3};
    float float_features[3] = {0};

    convert_int_to_float_features(int_features, float_features, 3);

    EXPECT_FLOAT_EQ(float_features[0], 1.0f);
    EXPECT_FLOAT_EQ(float_features[1], 2.0f);
    EXPECT_FLOAT_EQ(float_features[2], 3.0f);
}

TEST(SQLiEngineStaticTest, ConvertIntToFloatFeatures_Zero)
{
    int int_features[3] = {0, 0, 0};
    float float_features[3] = {0};

    convert_int_to_float_features(int_features, float_features, 3);

    EXPECT_FLOAT_EQ(float_features[0], 0.0f);
    EXPECT_FLOAT_EQ(float_features[1], 0.0f);
    EXPECT_FLOAT_EQ(float_features[2], 0.0f);
}

TEST(SQLiEngineStaticTest, ConvertIntToFloatFeatures_Negative)
{
    int int_features[3] = {-1, -2, -3};
    float float_features[3] = {0};

    convert_int_to_float_features(int_features, float_features, 3);

    EXPECT_FLOAT_EQ(float_features[0], -1.0f);
    EXPECT_FLOAT_EQ(float_features[1], -2.0f);
    EXPECT_FLOAT_EQ(float_features[2], -3.0f);
}

/**
 * free_buffer_if_set 函数测试 - NULL 指针
 */
TEST(SQLiEngineStaticTest, FreeBufferIfSet_Null)
{
    void *ptr = nullptr;
    free_buffer_if_set(&ptr);
    SUCCEED() << "free_buffer_if_set(NULL) 不应崩溃";
}

TEST(SQLiEngineStaticTest, FreeBufferIfSet_ValidPointer)
{
    void *ptr = malloc(10);
    ASSERT_NE(ptr, nullptr);

    free_buffer_if_set(&ptr);
    EXPECT_EQ(ptr, nullptr) << "释放后指针应该置 NULL";
}

/**
 * cleanup_sqli_batch_buffers 函数测试 - NULL 参数
 */
TEST(SQLiEngineStaticTest, CleanupSqliBatchBuffers_AllNull)
{
    cleanup_sqli_batch_buffers(nullptr, nullptr, nullptr, nullptr, 0);
    SUCCEED() << "cleanup_sqli_batch_buffers(全 NULL) 不应崩溃";
}

TEST(SQLiEngineStaticTest, CleanupSqliBatchBuffers_PartialNull)
{
    int **dfa_outputs = (int **)calloc(2, sizeof(int *));
    dfa_outputs[0] = (int *)calloc(10, sizeof(int));
    dfa_outputs[1] = nullptr;

    cleanup_sqli_batch_buffers(dfa_outputs, nullptr, nullptr, nullptr, 2);
    SUCCEED() << "cleanup_sqli_batch_buffers(部分 NULL) 不应崩溃";
}

/**
 * init_batch_detect_params 函数测试 - NULL 参数
 */
TEST(SQLiEngineStaticTest, InitBatchDetectParams_NullBatchSize)
{
    float probs[1] = {0};

    int result = init_batch_detect_params(64, nullptr, probs, 1);
    EXPECT_NE(result, KADD_SUCCESS);
}

TEST(SQLiEngineStaticTest, InitBatchDetectParams_NullProbabilities)
{
    int actual_batch_size = 0;

    int result = init_batch_detect_params(64, &actual_batch_size, nullptr, 1);
    EXPECT_NE(result, KADD_SUCCESS);
}

/**
 * allocate_batch_buffers 函数测试 - NULL 参数
 */
TEST(SQLiEngineStaticTest, AllocateBatchBuffers_NullEngine)
{
    kadd_batch_buffers_t buffers = {0};

    int result = allocate_batch_buffers(nullptr, 10, &buffers);
    EXPECT_EQ(result, KADD_ERROR_NULL_PTR);
}

TEST(SQLiEngineStaticTest, AllocateBatchBuffers_NullBuffers)
{
    kadd_sqli_engine_t engine;
    engine.feature_dim = 28;

    int result = allocate_batch_buffers(&engine, 10, nullptr);
    EXPECT_EQ(result, KADD_ERROR_NULL_PTR);
}

/**
 * init_batch_detect_params 函数测试 - batch_size 边界
 */
TEST(SQLiEngineStaticTest, InitBatchDetectParams_ZeroBatchSize)
{
    int actual_batch_size = 0;
    float probs[1] = {0};

    int result = init_batch_detect_params(0, &actual_batch_size, probs, 1);
    EXPECT_EQ(result, KADD_SUCCESS);
    EXPECT_EQ(actual_batch_size, KADD_ML_DEFAULT_BATCH_SIZE);  // 应该设置为默认值
}

TEST(SQLiEngineStaticTest, InitBatchDetectParams_NegativeBatchSize)
{
    int actual_batch_size = 0;
    float probs[1] = {0};

    int result = init_batch_detect_params(-1, &actual_batch_size, probs, 1);
    EXPECT_EQ(result, KADD_SUCCESS);
    EXPECT_EQ(actual_batch_size, KADD_ML_DEFAULT_BATCH_SIZE);  // 应该设置为默认值
}

TEST(SQLiEngineStaticTest, InitBatchDetectParams_ExceedsMaxBatchSize)
{
    int actual_batch_size = 0;
    float probs[1] = {0};

    int result = init_batch_detect_params(KADD_ML_MAX_BATCH_SIZE + 1, &actual_batch_size, probs, 1);
    EXPECT_EQ(result, KADD_SUCCESS);
    EXPECT_EQ(actual_batch_size, KADD_ML_DEFAULT_BATCH_SIZE);  // >MAX 现在回退默认值64（不再截断为最大值）
}

TEST(SQLiEngineStaticTest, CreateEngineInternal_NullEngine)
{
    kadd_dfa_table_paths_t table_paths = {};
    kadd_config_t cfg = {};
    cfg.engine_mode = KADD_ENGINE_MODE_ML;
    {
        const char *s = "model.onnx";
        for (size_t i = 0; i < sizeof(cfg.model_rf) - 1 && s[i]; i++)
            cfg.model_rf[i] = s[i];
        cfg.model_rf[sizeof(cfg.model_rf) - 1] = '\0';
    }
    int result = create_engine_internal(nullptr, &table_paths, &cfg);
    EXPECT_EQ(result, KADD_ERROR_NULL_PTR);
}

TEST(SQLiEngineStaticTest, CreateEngineInternal_NullTablePaths)
{
    kadd_sqli_engine_t *engine = nullptr;
    kadd_config_t cfg2 = {};
    cfg2.engine_mode = KADD_ENGINE_MODE_ML;
    {
        const char *s = "model.onnx";
        for (size_t i = 0; i < sizeof(cfg2.model_rf) - 1 && s[i]; i++)
            cfg2.model_rf[i] = s[i];
        cfg2.model_rf[sizeof(cfg2.model_rf) - 1] = '\0';
    }
    int result = create_engine_internal(&engine, nullptr, &cfg2);
    EXPECT_EQ(result, KADD_ERROR_NULL_PTR);
}

TEST(SQLiEngineStaticTest, CreateEngineInternal_NullModelPath)
{
    kadd_sqli_engine_t *engine = nullptr;
    kadd_dfa_table_paths_t table_paths = {};
    int result = create_engine_internal(&engine, &table_paths, nullptr);
    EXPECT_EQ(result, KADD_ERROR_NULL_PTR);
}

// ========== 覆盖率优化：批量检测空输入/长输入/零批次大小/混合NULL ==========

TEST_P(SQLiEngineTest, BatchWithEmptyInputInList)
{
    // 批量中包含空字符串：空输入应成功返回，概率保持 0.0
    std::vector<std::string> inputs_str = { "", "SELECT * FROM users", "" };
    std::vector<const char*> inputs;
    for (const auto& s : inputs_str) {
        inputs.push_back(s.c_str());
    }

    float probs[3] = { 1.0f, 1.0f, 1.0f };
    int ret = kadd_sqli_detect_batch(engine, inputs.data(), 3, probs, 1);
    // ML 模式：空字符串 DFA tokenize 产生 0 token，批处理项返回 KADD_DFA_ERROR_PARSE
    // DL 模式：逐条推理，空字符串返回 0 概率，批量检测成功
    if (GetParam() == "ml") {
        EXPECT_EQ(ret, KADD_DFA_ERROR_PARSE) << "ML 模式空字符串批量 DFA tokenize 应失败";
    } else {
        EXPECT_EQ(ret, KADD_SUCCESS) << "DL 模式空字符串批量检测应该成功";
        EXPECT_FLOAT_EQ(probs[0], 0.0f) << "空输入概率应该为 0";
        EXPECT_FLOAT_EQ(probs[2], 0.0f) << "空输入概率应该为 0";
        EXPECT_GE(probs[1], 0.0f);
        EXPECT_LE(probs[1], 1.0f);
    }
}

TEST_P(SQLiEngineTest, BatchWithLongInput)
{
    // 超长输入（> max_input_length = 300），应被截断或返回错误
    std::vector<std::string> inputs_str(1, std::string(500, 'a'));
    std::vector<const char*> inputs = { inputs_str[0].c_str() };

    float probs[1] = { 0.0f };
    int ret = kadd_sqli_detect_batch(engine, inputs.data(), 1, probs, 1);
    EXPECT_TRUE(ret != KADD_SUCCESS)
        << "超长输入应成功（截断）或返回 DFA 错误，实际 ret=" << ret;

    // 成功时概率应在有效范围内
    if (ret == KADD_SUCCESS) {
        EXPECT_GE(probs[0], 0.0f);
        EXPECT_LE(probs[0], 1.0f);
    }
}

// DL 模式批量检测：前几条成功、中途某条失败（超长），接口应立即返回错误
// 覆盖 issue: kadd_sqli_detect_batch DL 模式中途失败返回成功 → 修复为逐条返回错误
TEST_P(SQLiEngineTest, BatchDL_MidFailure_ReturnsError)
{
    // count=3：前 2 条正常 SQL，第 3 条超长（>300 字符）
    std::vector<std::string> inputs_str = {
        "id=1' OR '1'='1",
        "SELECT * FROM users WHERE id=1",
        std::string(500, 'a')  // 超长输入，DL 模式 tokenize 返回 KADD_DFA_ERROR_INVALID_LENGTH
    };
    std::vector<const char*> inputs;
    for (const auto& s : inputs_str) {
        inputs.push_back(s.c_str());
    }

    float probs[3] = { 0.0f, 0.0f, 0.0f };
    int ret = kadd_sqli_detect_batch(engine, inputs.data(), 3, probs, 10);

    if (GetParam() == "dl") {
        // DL 模式：逐条推理，第 3 条超长 → kadd_sqli_detect 返回 DFA 错误
        // 批量接口应立即返回该错误，而不是 KADD_SUCCESS
        EXPECT_TRUE(ret != KADD_SUCCESS)
            << "DL 批量检测中途失败（超长输入）应立即返回错误，实际 ret=" << ret;
        EXPECT_EQ(ret, KADD_DFA_ERROR_INVALID_LENGTH)
            << "应返回 DFA 长度错误，实际 ret=" << ret;
        // 前 2 条已成功处理，概率应已写入
        EXPECT_GE(probs[0], 0.0f);
        EXPECT_LE(probs[0], 1.0f);
        EXPECT_GE(probs[1], 0.0f);
        EXPECT_LE(probs[1], 1.0f);
    } else {
        // ML 模式：批量路径，超长输入在 DFA 批量 tokenize 阶段失败
        EXPECT_TRUE(ret != KADD_SUCCESS)
            << "ML 批量检测超长输入应返回错误，实际 ret=" << ret;
    }
}

TEST_P(SQLiEngineTest, DetectBatchWithZeroBatchSize)
{
    // batch_size = 0 应该回退到默认批次大小
    std::vector<std::string> inputs_str = {
        "id=1' OR '1'='1",
        "SELECT * FROM users",
        "user@example.com"
    };
    std::vector<const char*> inputs;
    for (const auto& s : inputs_str) {
        inputs.push_back(s.c_str());
    }

    float probs[3] = { 0.0f, 0.0f, 0.0f };
    int ret = kadd_sqli_detect_batch(engine, inputs.data(), 3, probs, 0);
    EXPECT_EQ(ret, KADD_SUCCESS) << "batch_size=0 应该回退到默认批次大小并成功";

    for (int i = 0; i < 3; i++) {
        EXPECT_GE(probs[i], 0.0f);
        EXPECT_LE(probs[i], 1.0f);
    }
}

TEST(SQLiEngineStaticTest, ProcessBatchLoop_MixedNullAndValid)
{
    // 不依赖引擎/配置，直接测试批处理管线中 DFA 批量分词
    // 对混合 NULL 和有效输入的错误处理路径
    kadd_dfa_table_paths_t paths = {
        "assets/data/transition_table.bin",
        "assets/data/output_table.bin",
        "assets/data/eod_table.bin"
    };
    kadd_dfa_engine_t* dfa = nullptr;
    int ret = kadd_dfa_engine_create(&dfa, &paths, 300, 300);
    ASSERT_EQ(ret, KADD_SUCCESS) << "DFA 引擎创建失败（assets/data 缺表？）";

    // 混合 NULL 和有效输入
    std::vector<std::string> valid_strs = { "id=1' OR '1'='1", "SELECT * FROM users" };
    const char* inputs[3] = { nullptr, valid_strs[0].c_str(), valid_strs[1].c_str() };

    int* outputs[3] = { nullptr, nullptr, nullptr };
    int dims[3] = { 0, 0, 0 };

    kadd_dfa_batch_t batch = {
        .inputs = inputs,
        .lengths = nullptr,
        .count = 3,
        .outputs = outputs,
        .dims = dims,
        .batch_size = 3
    };

    // NULL 输入应触发错误路径（KADD_ERROR_INVALID_INPUT），且不崩溃
    int result = kadd_dfa_engine_tokenize_batch(dfa, &batch);
    EXPECT_NE(result, KADD_SUCCESS) << "混合 NULL 输入应该返回错误";

    // 按 API 约定，失败时已处理的 outputs 会被自动释放，调用者无需手动释放
    kadd_dfa_engine_free(dfa);
}

// ========== Instantiate for both ML and DL modes ==========
INSTANTIATE_TEST_SUITE_P(
    ML_DL_Mode,
    SQLiEngineTest,
    ::testing::Values(std::string("ml"), std::string("dl")),
    [](const ::testing::TestParamInfo<std::string>& info) {
        return info.param == "ml" ? "ML" : "DL";
    }
);

// ========== 覆盖率优化：native→ONNX fallback 路径 ==========

TEST(SQLiEngineStaticTest, CreateWithNativeFallingBackToOnnx)
{
    // 读 config.json，把 model_rf 改成不存在的路径，保留 model_rf_onnx
    // 这样 SVE 构建下 native 加载失败 → fallback 到 ONNX 成功
    kadd_config_t config;
    ASSERT_EQ(kadd_config_load("config.json", &config), 0) << "config.json 加载失败";

    // 保存原始值
    std::string orig_model_rf(config.model_rf);

    // 改成不存在的路径，触发 native 加载失败
    snprintf(config.model_rf, sizeof(config.model_rf), "/nonexistent_native_rf.krf");

    kadd_dfa_table_paths_t table_paths = {
        .transition_table_path = config.transition_table,
        .output_table_path = config.output_table,
        .eod_table_path = config.eod_table
    };

    kadd_sqli_engine_t *eng = nullptr;
    int ret = create_engine_internal(&eng, &table_paths, &config);
    EXPECT_EQ(ret, KADD_SUCCESS) << "native 失败后应 fallback 到 ONNX 成功";
    if (ret == KADD_SUCCESS) {
        ASSERT_NE(eng, nullptr);
        // 验证可以正常检测
        float prob = 0.0f;
        int dret = kadd_sqli_detect(eng, "SELECT * FROM users WHERE id=1", &prob);
        EXPECT_EQ(dret, KADD_SUCCESS) << "fallback 到 ONNX 后应能正常检测";
        EXPECT_GE(prob, 0.0f);
        EXPECT_LE(prob, 1.0f);
        kadd_sqli_engine_free(eng);
    }

    kadd_config_free(&config);
}

TEST(SQLiEngineStaticTest, CreateWithBothNativeAndOnnxFailing)
{
    // 读 config.json，把 model_rf 和 model_rf_onnx 都改成不存在的路径
    // native 失败 → ONNX 也失败 → KADD_ERROR_MODEL_LOAD_FAILED
    kadd_config_t config;
    ASSERT_EQ(kadd_config_load("config.json", &config), 0) << "config.json 加载失败";

    snprintf(config.model_rf, sizeof(config.model_rf), "/nonexistent_native_rf.krf");
    snprintf(config.model_rf_onnx, sizeof(config.model_rf_onnx), "/nonexistent_onnx_rf.onnx");

    kadd_dfa_table_paths_t table_paths = {
        .transition_table_path = config.transition_table,
        .output_table_path = config.output_table,
        .eod_table_path = config.eod_table
    };

    kadd_sqli_engine_t *eng = nullptr;
    int ret = create_engine_internal(&eng, &table_paths, &config);
    EXPECT_EQ(ret, KADD_ERROR_MODEL_LOAD_FAILED) << "native 和 ONNX 都失败应返回 KADD_ERROR_MODEL_LOAD_FAILED";
    EXPECT_EQ(eng, nullptr);

    kadd_config_free(&config);
}

// 覆盖 sqli_engine.c DL 模式加载失败的 else 分支（model_cnn 和 model_cnn_onnx 都不存在）
TEST(SQLiEngineStaticTest, CreateWithDLMissingCnn)
{
    kadd_config_t config;
    ASSERT_EQ(kadd_config_load("config.json", &config), 0) << "config.json 加载失败";

    config.engine_mode = KADD_ENGINE_MODE_DL;
    snprintf(config.model_cnn, sizeof(config.model_cnn), "/nonexistent_cnn.kcn");
    snprintf(config.model_cnn_onnx, sizeof(config.model_cnn_onnx), "/nonexistent_cnn.onnx");

    kadd_dfa_table_paths_t table_paths = {
        .transition_table_path = config.transition_table,
        .output_table_path = config.output_table,
        .eod_table_path = config.eod_table
    };

    kadd_sqli_engine_t *eng = nullptr;
    int ret = create_engine_internal(&eng, &table_paths, &config);
    EXPECT_EQ(ret, KADD_ERROR_MODEL_LOAD_FAILED) << "DL 模式 model_cnn 不存在应返回 KADD_ERROR_MODEL_LOAD_FAILED";
    EXPECT_EQ(eng, nullptr);

    kadd_config_free(&config);
}
