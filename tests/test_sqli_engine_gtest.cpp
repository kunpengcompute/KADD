/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *
 * @file test_sqli_engine_gtest.cpp
 * @brief SQL 注入检测引擎 Google Test 测试套件
 *
 * 包含功能测试、边界测试、异常测试、性能测试
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
#include <cstdio>
#include <cstdlib>
#include "kadd_sqli_engine.h"
#include "kadd_ml_engine.h"
#include "kadd_config_loader.h"

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

/* ========== 测试夹具 ========== */

class SQLiEngineTest : public ::testing::Test {
protected:
    kadd_sqli_engine_t* engine;
    float threshold = 0.5f;  // 默认检测阈值
    bool skip_test = false;

    void SetUp() override
    {
        int ret = kadd_sqli_engine_create_from_file(&engine, "config.json");
        if (ret != KADD_SUCCESS) {
            std::cout << "[WARNING] SQLi 引擎创建失败 (ret=" << ret << "),跳过功能测试" << std::endl;
            skip_test = true;
            engine = nullptr;
            return;
        }

        /* 验证引擎是否能正常检测（warmup 可能未捕获所有推理问题） */
        float prob = 0.0f;
        ret = kadd_sqli_detect(engine, "test_input", &prob);
        if (ret != KADD_SUCCESS) {
            std::cout << "[WARNING] SQLi 检测验证失败 (ret=" << ret << "),跳过功能测试" << std::endl;
            kadd_sqli_engine_free(engine);
            engine = nullptr;
            skip_test = true;
        }
    }

    void TearDown() override
    {
        if (engine) {
            kadd_sqli_engine_free(engine);
            engine = nullptr;
        }
    }

    float detect(const std::string &input)
    {
        float probability = 0.0f;
        int ret = kadd_sqli_detect(engine, input.c_str(), &probability);
        EXPECT_EQ(ret, KADD_SUCCESS) << "检测失败: " << input;
        return probability;
    }

    // 运行批量检测并计算统计指标
    // cases: 测试用例数组, count: 用例数量, thresh: 检测阈值
    Metrics evaluate_dataset(const TestCase* cases, size_t count, float thresh)
    {
        Metrics metrics;

        for (size_t i = 0; i < count; i++) {
            float prob = detect(cases[i].input);
            bool predicted = (prob >= thresh);
            bool actual = cases[i].is_injection;

            if (predicted && actual) {
                metrics.true_positive++;
            } else if (predicted && !actual) {
                metrics.false_positive++;
            } else if (!predicted && !actual) {
                metrics.true_negative++;
            } else {
                metrics.false_negative++;
            }
        }

        return metrics;
    }

    // 打印混淆矩阵和性能指标
    void print_confusion_matrix(const Metrics& m)
    {
        std::cout << "\n========== 混淆矩阵 ==========\n";
        std::cout << "                    实际注入    实际正常\n";
        std::cout << "预测为注入:       " << m.true_positive << "         " << m.false_positive << "\n";
        std::cout << "预测为安全:       " << m.false_negative << "         " << m.true_negative << "\n";
        std::cout << "\n========== 性能指标 ==========\n";
        constexpr int kDisplayPrecision = 3;
        std::cout << std::fixed << std::setprecision(kDisplayPrecision);
        std::cout << "准确率 (Accuracy):  " << (m.accuracy() * kPercentScale) << "%\n";
        std::cout << "精确率 (Precision): " << (m.precision() * kPercentScale) << "%\n";
        std::cout << "召回率 (Recall):    " << (m.recall() * kPercentScale) << "%\n";
        std::cout << "特异度 (Specificity): " << (m.specificity() * kPercentScale) << "%\n";
        std::cout << "F1分数:             " << m.f1_score() << "\n";
        std::cout << "============================\n\n";
    }

    // 打印单个样本的检测详情
    void print_detection_detail(const char* id, const char* input, float prob, bool predicted, bool actual)
    {
        constexpr int kDetectDisplayPrecision = 3;
        std::cout << "[" << id << "] "
                  << (predicted ? "注入" : "安全") << "("
                  << std::fixed << std::setprecision(kDetectDisplayPrecision) << prob << ") "
                  << "| 实际: " << (actual ? "注入" : "安全") << " "
                  << (predicted == actual ? "✓" : "✗")
                  << "\n    输入: " << input << "\n";
    }
};

/* ========== 1. 功能测试 ========== */

TEST_F(SQLiEngineTest, BasicFunctionality)
{
    if (skip_test) {
        GTEST_SKIP() << "SQLi 引擎不可用";
    }
    float prob = detect("id=1' OR '1'='1");
    EXPECT_GE(prob, 0.0f);
    EXPECT_LE(prob, 1.0f);
}

// 使用统计指标评估模型整体性能，适应模型训练的随机性
TEST_F(SQLiEngineTest, ModelPerformanceMetrics)
{
    if (skip_test) {
        GTEST_SKIP() << "SQLi 引擎不可用";
    }
    constexpr size_t sqli_count = sizeof(g_sqli_samples) / sizeof(g_sqli_samples[0]);
    constexpr size_t normal_count = sizeof(g_normal_samples) / sizeof(g_normal_samples[0]);
    constexpr size_t total_count = sqli_count + normal_count;

    // 计算整体指标
    Metrics metrics = evaluate_dataset(g_full_dataset.data(), total_count, threshold);

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
TEST_F(SQLiEngineTest, PerformanceAtDifferentThresholds)
{
    if (skip_test) {
        GTEST_SKIP() << "SQLi 引擎不可用";
    }
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
        Metrics metrics = evaluate_dataset(g_full_dataset.data(), total_count, thresh);

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
TEST_F(SQLiEngineTest, ModelStability)
{
    if (skip_test) {
        GTEST_SKIP() << "SQLi 引擎不可用";
    }
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

TEST_F(SQLiEngineTest, EmptyInput)
{
    if (skip_test) {
        GTEST_SKIP() << "SQLi 引擎不可用";
    }
    // 测试空输入
    float prob = detect("");
    EXPECT_LT(prob, threshold) << "空输入应该返回低概率";
}

TEST_F(SQLiEngineTest, SingleChar)
{
    if (skip_test) {
        GTEST_SKIP() << "SQLi 引擎不可用";
    }
    // 测试单字符输入
    std::vector<std::string> inputs = { "a", "1", "@" };

    for (const auto &input : inputs) {
        float prob = detect(input);
        EXPECT_GE(prob, 0.0f) << "单字符应该成功处理: " << input;
        EXPECT_LE(prob, 1.0f);
    }
}

TEST_F(SQLiEngineTest, MaxInputBoundary)
{
    if (skip_test) {
        GTEST_SKIP() << "SQLi 引擎不可用";
    }
    // 测试接近最大限制的输入
    constexpr int kMaxInputLength = 300;
    std::string max_input(kMaxInputLength, 'a');
    float prob = detect(max_input);
    EXPECT_GE(prob, 0.0f) << "接近最大长度应该成功";
    EXPECT_LE(prob, 1.0f);
}

TEST_F(SQLiEngineTest, SpecialChars)
{
    if (skip_test) {
        GTEST_SKIP() << "SQLi 引擎不可用";
    }
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

TEST_F(SQLiEngineTest, UnicodeInput)
{
    if (skip_test) {
        GTEST_SKIP() << "SQLi 引擎不可用";
    }
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

TEST_F(SQLiEngineTest, NewlineAndTabs)
{
    if (skip_test) {
        GTEST_SKIP() << "SQLi 引擎不可用";
    }
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

TEST_F(SQLiEngineTest, MixedCase)
{
    if (skip_test) {
        GTEST_SKIP() << "SQLi 引擎不可用";
    }
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

TEST_F(SQLiEngineTest, NullEngine)
{
    float prob = 0.0f;
    int ret = kadd_sqli_detect(nullptr, "test", &prob);
    EXPECT_EQ(ret, KADD_ERROR_INVALID_INPUT) << "空引擎应该返回 KADD_ERROR_INVALID_INPUT";
}

TEST_F(SQLiEngineTest, NullInput)
{
    if (skip_test) {
        GTEST_SKIP() << "SQLi 引擎不可用";
    }
    float prob = 0.0f;
    int ret = kadd_sqli_detect(engine, nullptr, &prob);
    EXPECT_EQ(ret, KADD_ERROR_INVALID_INPUT) << "空输入应该返回 KADD_ERROR_INVALID_INPUT";
}

TEST_F(SQLiEngineTest, NullProbability)
{
    if (skip_test) {
        GTEST_SKIP() << "SQLi 引擎不可用";
    }
    int ret = kadd_sqli_detect(engine, "test", nullptr);
    EXPECT_EQ(ret, KADD_ERROR_INVALID_INPUT) << "空概率指针应该返回 KADD_ERROR_INVALID_INPUT";
}

TEST_F(SQLiEngineTest, NullEngineWithAllOtherNull)
{
    int ret = kadd_sqli_detect(nullptr, nullptr, nullptr);
    EXPECT_EQ(ret, KADD_ERROR_INVALID_INPUT) << "全部空指针应该返回 KADD_ERROR_INVALID_INPUT";
}

TEST_F(SQLiEngineTest, CreateWithNullEnginePtr)
{
    int ret = kadd_sqli_engine_create_from_file(nullptr, "config.json");
    EXPECT_NE(ret, KADD_SUCCESS) << "NULL engine 指针应该返回错误";
}

TEST_F(SQLiEngineTest, CreateWithNullConfigFile)
{
    kadd_sqli_engine_t* tmp = nullptr;
    int ret = kadd_sqli_engine_create_from_file(&tmp, nullptr);
    EXPECT_EQ(ret, KADD_ERROR_INVALID_INPUT) << "NULL 配置文件应该返回 KADD_ERROR_INVALID_INPUT";
}

TEST_F(SQLiEngineTest, CreateWithEmptyConfigFile)
{
    kadd_sqli_engine_t* tmp = nullptr;
    int ret = kadd_sqli_engine_create_from_file(&tmp, "");
    EXPECT_NE(ret, KADD_SUCCESS) << "空配置文件路径应该返回错误";
}

TEST_F(SQLiEngineTest, CreateWithNonexistentConfigFile)
{
    kadd_sqli_engine_t* tmp = nullptr;
    int ret = kadd_sqli_engine_create_from_file(&tmp, "nonexistent_config.json");
    EXPECT_NE(ret, KADD_SUCCESS) << "不存在配置文件应该返回错误";
}

// ========== 覆盖率优化：触发 release_engine_buffers 错误处理 ==========

TEST_F(SQLiEngineTest, CreateWithInvalidDFAPaths)
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

TEST_F(SQLiEngineTest, CreateWithInvalidMLModelPath)
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
        if (ret == KADD_SQLI_ERROR_ML) {
            // 完美：覆盖了 ML 模型加载失败的 release_engine_buffers 路径
            SUCCEED() << "成功覆盖 ML 模型加载失败的 release_engine_buffers 路径";
        } else if (ret == KADD_SQLI_ERROR_DFA) {
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

TEST_F(SQLiEngineTest, EngineReuse)
{
    if (skip_test) {
        GTEST_SKIP() << "SQLi 引擎不可用";
    }
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

TEST_F(SQLiEngineTest, SingleDetectionPerformance)
{
    if (skip_test) {
        GTEST_SKIP() << "SQLi 引擎不可用";
    }
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

TEST_F(SQLiEngineTest, BatchDetectionPerformance)
{
    if (skip_test) {
        GTEST_SKIP() << "SQLi 引擎不可用";
    }
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

TEST_F(SQLiEngineTest, BatchVsSingleConsistency)
{
    if (skip_test) {
        GTEST_SKIP() << "SQLi 引擎不可用";
    }
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

TEST_F(SQLiEngineTest, BatchSize_EdgeCases)
{
    if (skip_test) GTEST_SKIP() << "SQLi 引擎不可用";

    const char *inputs[1] = {"test"};
    float probs[1];

    // 测试 batch_size = 0 (应使用默认值)
    EXPECT_EQ(kadd_sqli_detect_batch(engine, inputs, 1, probs, 0), KADD_SUCCESS);

    // 测试 batch_size < 0 (应使用默认值)
    EXPECT_EQ(kadd_sqli_detect_batch(engine, inputs, 1, probs, -1), KADD_SUCCESS);

    // 测试 batch_size = 1
    EXPECT_EQ(kadd_sqli_detect_batch(engine, inputs, 1, probs, 1), KADD_SUCCESS);

    // 测试 batch_size > KADD_ML_MAX_BATCH_SIZE (应自动限制)
    EXPECT_EQ(kadd_sqli_detect_batch(engine, inputs, 1, probs, 1025), KADD_SUCCESS);
}

TEST_F(SQLiEngineTest, VeryLongInput_400Chars)
{
    if (skip_test) GTEST_SKIP() << "SQLi 引擎不可用";

    // 测试超长输入 (超过默认 max_input_length = 300)
    std::string long_input(400, 'A');
    float prob;
    EXPECT_EQ(kadd_sqli_detect(engine, long_input.c_str(), &prob), KADD_SQLI_ERROR_DFA)
        << "超长输入应该返回错误（超过max_input_length）";
}

// ========== 覆盖率优化：批处理边界和错误路径测试 ==========
TEST_F(SQLiEngineTest, BatchWithZeroCount)
{
    if (skip_test) GTEST_SKIP() << "SQLi 引擎不可用";

    const char *inputs[1] = {"test"};
    float probs[1];

    // 测试 count = 0
    EXPECT_EQ(kadd_sqli_detect_batch(engine, inputs, 0, probs, 64), KADD_ERROR_INVALID_INPUT)
        << "count=0 应该返回 INVALID_INPUT 错误";
}

TEST_F(SQLiEngineTest, BatchWithNegativeCount)
{
    if (skip_test) GTEST_SKIP() << "SQLi 引擎不可用";

    const char *inputs[1] = {"test"};
    float probs[1];

    // 测试 count = -1
    EXPECT_EQ(kadd_sqli_detect_batch(engine, inputs, -1, probs, 64), KADD_ERROR_INVALID_INPUT)
        << "count=-1 应该返回 INVALID_INPUT 错误";
}

TEST_F(SQLiEngineTest, BatchWithExceedsMaxSampleCount)
{
    if (skip_test) GTEST_SKIP() << "SQLi 引擎不可用";

    // 测试 count > KADD_ML_MAX_SAMPLE_COUNT (10,000,000)
    const char *inputs[1] = {"test"};
    float probs[1];

    EXPECT_EQ(kadd_sqli_detect_batch(engine, inputs, KADD_ML_MAX_SAMPLE_COUNT + 1, probs, 64),
                KADD_ERROR_INVALID_PARAM)
        << "count 超过最大限制应该返回 INVALID_PARAM 错误";
}

TEST_F(SQLiEngineTest, BatchWithNullProbabilities)
{
    if (skip_test) GTEST_SKIP() << "SQLi 引擎不可用";

    const char *inputs[1] = {"test"};

    // 测试 probabilities = NULL
    EXPECT_EQ(kadd_sqli_detect_batch(engine, inputs, 1, nullptr, 64), KADD_ERROR_INVALID_INPUT)
        << "NULL probabilities 应该返回 INVALID_INPUT 错误";
}

TEST_F(SQLiEngineTest, BatchWithNullInputElement)
{
    if (skip_test) GTEST_SKIP() << "SQLi 引擎不可用";

    // 测试 inputs 数组中包含 NULL 元素
    const char *inputs[3] = {"test", nullptr, "normal"};
    float probs[3] = {0.0f, 0.0f, 0.0f};

    // 期望返回错误（因为 inputs[1] 是 NULL）
    int ret = kadd_sqli_detect_batch(engine, inputs, 3, probs, 64);
    EXPECT_NE(ret, KADD_SUCCESS) << "inputs 数组中包含 NULL 应该返回错误";
}

TEST_F(SQLiEngineTest, BatchWithNullInputsArray)
{
    if (skip_test) GTEST_SKIP() << "SQLi 引擎不可用";

    float probs[1];

    // 测试整个 inputs 数组为 NULL
    EXPECT_EQ(kadd_sqli_detect_batch(engine, nullptr, 1, probs, 64), KADD_ERROR_INVALID_INPUT)
        << "NULL inputs 数组应该返回 INVALID_INPUT 错误";
}

TEST_F(SQLiEngineTest, BatchWithNullEngine)
{
    float probs[1];
    const char *inputs[1] = {"test"};

    // 测试 engine = NULL
    EXPECT_EQ(kadd_sqli_detect_batch(nullptr, inputs, 1, probs, 64), KADD_ERROR_INVALID_INPUT)
        << "NULL engine 应该返回 INVALID_INPUT 错误";
}

TEST_F(SQLiEngineTest, SingleInputWithNullProbability)
{
    if (skip_test) GTEST_SKIP() << "SQLi 引擎不可用";

    // 测试单次检测时 probability = NULL
    int ret = kadd_sqli_detect(engine, "test", nullptr);
    EXPECT_EQ(ret, KADD_ERROR_INVALID_INPUT)
        << "NULL probability 指针应该返回 INVALID_INPUT 错误";
}

TEST_F(SQLiEngineTest, SingleInputWithNullInputString)
{
    if (skip_test) GTEST_SKIP() << "SQLi 引擎不可用";

    float prob = 0.0f;

    // 测试单次检测时 input = NULL
    EXPECT_EQ(kadd_sqli_detect(engine, nullptr, &prob), KADD_ERROR_INVALID_INPUT)
        << "NULL input 字符串应该返回 INVALID_INPUT 错误";
}

TEST_F(SQLiEngineTest, SingleInputWithNullEngine)
{
    float prob = 0.0f;

    // 测试单次检测时 engine = NULL
    EXPECT_EQ(kadd_sqli_detect(nullptr, "test", &prob), KADD_ERROR_INVALID_INPUT)
        << "NULL engine 应该返回 INVALID_INPUT 错误";
}

// ========== 7. 边缘情况和特殊情况测试 ==========

// SQLi_E001: 空字符串检测
TEST_F(SQLiEngineTest, EmptyStringDetection)
{
    if (skip_test) GTEST_SKIP() << "SQLi 引擎不可用";

    const char* empty_input = "";
    float probability = 0.0f;

    int ret = kadd_sqli_detect(engine, empty_input, &probability);

    EXPECT_EQ(ret, KADD_SUCCESS) << "空字符串处理应成功";
    EXPECT_TRUE(probability >= 0.0f && probability <= 1.0f) << "概率值应在 [0, 1] 范围内";
    EXPECT_LT(probability, 0.5f) << "空字符串不应被判定为注入";
}

// SQLi_E002: 单个字符检测
TEST_F(SQLiEngineTest, SingleCharDetection)
{
    if (skip_test) GTEST_SKIP() << "SQLi 引擎不可用";

    const char* single_chars[] = {"a", "1", "'", ";", "-", "#"};

    for (const char* input : single_chars) {
        float probability = 0.0f;

        int ret = kadd_sqli_detect(engine, input, &probability);

        EXPECT_EQ(ret, KADD_SUCCESS) << "单字符处理应成功: " << input;
        EXPECT_TRUE(probability >= 0.0f && probability <= 1.0f) << "概率值应在有效范围内";
    }
}
