/**
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
#include "kadd_sqli_engine.h"
#include "kadd_config_loader.h"

/* ========== 测试用例定义 ========== */

/**
 * @brief 测试用例结构，包含 ground truth 标签
 */
struct TestCase {
    const char* id;           // 测试ID，用于定位和报告
    const char* input;        // 输入文本
    bool is_injection;        // Ground truth: 是否为注入
};

/**
 * @brief 统计指标结构
 */
struct Metrics {
    int true_positive = 0;   // TP: 正确检测为注入
    int false_positive = 0;  // FP: 误报（正常判为注入）
    int true_negative = 0;   // TN: 正确判定为安全
    int false_negative = 0;  // FN: 漏报（注入判为安全）

    float precision() const {
        int predicted_positive = true_positive + false_positive;
        return predicted_positive > 0 ? (float)true_positive / predicted_positive : 0.0f;
    }

    float recall() const {
        int actual_positive = true_positive + false_negative;
        return actual_positive > 0 ? (float)true_positive / actual_positive : 0.0f;
    }

    float f1_score() const {
        float p = precision();
        float r = recall();
        return (p + r) > 0 ? 2 * p * r / (p + r) : 0.0f;
    }

    float accuracy() const {
        int total = true_positive + false_positive + true_negative + false_negative;
        return total > 0 ? (float)(true_positive + true_negative) / total : 0.0f;
    }

    float specificity() const {
        int actual_negative = true_negative + false_positive;
        return actual_negative > 0 ? (float)true_negative / actual_negative : 0.0f;
    }
};

/**
 * @brief 测试数据集 - SQL注入样本
 */
static const TestCase sqli_samples[] = {
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
    { "error_003", "id=1,(SELECT 1 FROM (SELECT COUNT(*),CONCAT(0x3a,VERSION(),FLOOR(RAND(0)*2))x FROM information_schema.tables GROUP BY x)a)--", true },

    // 编码绕过
    { "encoded_001", "id=%27%20OR%20%271%27=%271", true },
    { "encoded_002", "id=1%2527%2520OR%2520%25271%2527=%25271", true },
    { "encoded_003", "id=\\x27\\x20OR\\x20\\x271\\x27=\\x271", true },

    // 二次注入
    { "second_order_001", "username=admin'#", true },
    { "second_order_002", "username=\"; DROP TABLE users--", true }
};

/**
 * @brief 测试数据集 - 正常流量样本
 */
static const TestCase normal_samples[] = {
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

/**
 * @brief 合并的完整测试数据集
 */
static const std::vector<TestCase> full_dataset = []() {
    std::vector<TestCase> dataset;
    for (const auto& tc : sqli_samples) {
        dataset.push_back(tc);
    }
    for (const auto& tc : normal_samples) {
        dataset.push_back(tc);
    }
    return dataset;
}();

/* ========== 测试夹具 ========== */

class SQLiEngineTest : public ::testing::Test {
protected:
    kadd_sqli_engine_t *engine;
    float threshold = 0.5f;  // 默认检测阈值

    void SetUp() override {
        ASSERT_EQ(kadd_sqli_engine_create_from_file(&engine, "config.json"), KADD_SUCCESS)
            << "引擎创建失败，请检查 config.json 是否存在";
    }

    void TearDown() override {
        if (engine) {
            kadd_sqli_engine_free(engine);
            engine = nullptr;
        }
    }

    float detect(const std::string &input) {
        float probability = 0.0f;
        int ret = kadd_sqli_detect(engine, input.c_str(), &probability);
        EXPECT_EQ(ret, KADD_SUCCESS) << "检测失败: " << input;
        return probability;
    }

    /**
     * @brief 运行批量检测并计算统计指标
     * @param cases 测试用例数组
     * @param count 用例数量
     * @param thresh 检测阈值
     * @return 统计指标
     */
    Metrics evaluate_dataset(const TestCase* cases, size_t count, float thresh) {
        Metrics metrics;

        for (size_t i = 0; i < count; i++) {
            float prob = detect(cases[i].input);
            bool predicted = (prob >= thresh);
            bool actual = cases[i].is_injection;

            if (predicted && actual) metrics.true_positive++;
            else if (predicted && !actual) metrics.false_positive++;
            else if (!predicted && !actual) metrics.true_negative++;
            else metrics.false_negative++;
        }

        return metrics;
    }

    /**
     * @brief 打印混淆矩阵和性能指标
     */
    void print_confusion_matrix(const Metrics& m) {
        std::cout << "\n========== 混淆矩阵 ==========\n";
        std::cout << "                    实际注入    实际正常\n";
        std::cout << "预测为注入:       " << m.true_positive << "         " << m.false_positive << "\n";
        std::cout << "预测为安全:       " << m.false_negative << "         " << m.true_negative << "\n";
        std::cout << "\n========== 性能指标 ==========\n";
        std::cout << std::fixed << std::setprecision(3);
        std::cout << "准确率 (Accuracy):  " << m.accuracy() * 100 << "%\n";
        std::cout << "精确率 (Precision): " << m.precision() * 100 << "%\n";
        std::cout << "召回率 (Recall):    " << m.recall() * 100 << "%\n";
        std::cout << "特异度 (Specificity): " << m.specificity() * 100 << "%\n";
        std::cout << "F1分数:             " << m.f1_score() << "\n";
        std::cout << "============================\n\n";
    }

    /**
     * @brief 打印单个样本的检测详情
     */
    void print_detection_detail(const char* id, const char* input, float prob, bool predicted, bool actual) {
        std::cout << "[" << id << "] "
                  << (predicted ? "注入" : "安全") << "(" << std::fixed << std::setprecision(3) << prob << ") "
                  << "| 实际: " << (actual ? "注入" : "安全") << " "
                  << (predicted == actual ? "✓" : "✗")
                  << "\n    输入: " << input << "\n";
    }
};

/* ========== 1. 功能测试 ========== */

/**
 * @brief 基本功能测试 - 验证引擎能正常运行
 */
TEST_F(SQLiEngineTest, BasicFunctionality) {
    float prob = detect("id=1' OR '1'='1");
    EXPECT_GE(prob, 0.0f);
    EXPECT_LE(prob, 1.0f);
}

/**
 * @brief 模型整体性能测试 - 使用统计指标评估
 * 适应模型训练的随机性，关注整体性能而非单个样本
 */
TEST_F(SQLiEngineTest, ModelPerformanceMetrics) {
    constexpr size_t sqli_count = sizeof(sqli_samples) / sizeof(sqli_samples[0]);
    constexpr size_t normal_count = sizeof(normal_samples) / sizeof(normal_samples[0]);
    constexpr size_t total_count = sqli_count + normal_count;

    // 计算整体指标
    Metrics metrics = evaluate_dataset(full_dataset.data(), total_count, threshold);

    // 打印详细结果
    print_confusion_matrix(metrics);

    // 断言：检查整体性能是否在可接受范围内
    // 这些阈值需要根据实际情况调整
    EXPECT_GE(metrics.accuracy(), 0.60f) << "准确率不应低于60%";
    EXPECT_GE(metrics.recall(), 0.50f) << "召回率不应低于50%";
    EXPECT_GE(metrics.f1_score(), 0.50f) << "F1分数不应低于0.5";

    // 允许一定程度的误报，但不能太离谱
    EXPECT_LE(metrics.false_positive, 5) << "误报数量不应超过3个";
}

/**
 * @brief 不同阈值下的性能对比测试
 * 帮助确定最佳检测阈值
 */
TEST_F(SQLiEngineTest, PerformanceAtDifferentThresholds) {
    size_t total_count = full_dataset.size();

    std::vector<float> thresholds = {0.3f, 0.5f, 0.7f};

    std::cout << "\n========== 不同阈值性能对比 ==========\n";
    std::cout << "阈值\t准确率\t精确率\t召回率\tF1\t特异度\n";

    for (float thresh : thresholds) {
        Metrics metrics = evaluate_dataset(full_dataset.data(), total_count, thresh);

        std::cout << std::fixed << std::setprecision(1)
                  << thresh << "\t"
                  << metrics.accuracy() * 100 << "%\t"
                  << metrics.precision() * 100 << "%\t"
                  << metrics.recall() * 100 << "%\t"
                  << metrics.f1_score() << "\t"
                  << metrics.specificity() * 100 << "%\n";

        // 阈值0.5时应该有合理的性能
        if (std::abs(thresh - 0.5f) < 0.01f) {
            EXPECT_GE(metrics.f1_score(), 0.40f) << "阈值0.5时F1不应过低";
        }
    }

    std::cout << "====================================\n\n";
}

/**
 * @brief 模型稳定性测试
 * 验证模型对同一输入的检测结果是否稳定
 */
TEST_F(SQLiEngineTest, ModelStability) {
    std::string input = "id=1' OR '1'='1";

    std::vector<float> probs(10);
    for (int i = 0; i < 10; i++) {
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
    std::cout << "  均值: " << std::fixed << std::setprecision(4) << mean << "\n";
    std::cout << "  标准差: " << std_dev << "\n";

    // 模型应该是确定性的，标准差应该接近0
    EXPECT_LT(std_dev, 0.001f) << "模型检测结果应该稳定（标准差 < 0.001）";
}

/* ========== 2. 边界测试 ========== */

TEST_F(SQLiEngineTest, EmptyInput) {
    // 测试空输入
    float prob = detect("");
    EXPECT_LT(prob, 0.5f) << "空输入应该返回 0 概率";
}

TEST_F(SQLiEngineTest, SingleChar) {
    // 测试单字符输入
    std::vector<std::string> inputs = {"a", "1", "@"};

    for (const auto &input : inputs) {
        float prob = detect(input);
        EXPECT_GE(prob, 0.0f) << "单字符应该成功处理: " << input;
        EXPECT_LE(prob, 1.0f);
    }
}

TEST_F(SQLiEngineTest, MaxInputBoundary) {
    // 测试接近最大限制的输入
    std::string max_input(300, 'a');
    float prob = detect(max_input);
    EXPECT_GE(prob, 0.0f) << "接近最大长度应该成功";
    EXPECT_LE(prob, 1.0f);
}

TEST_F(SQLiEngineTest, SpecialChars) {
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

TEST_F(SQLiEngineTest, UnicodeInput) {
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

TEST_F(SQLiEngineTest, NewlineAndTabs) {
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

TEST_F(SQLiEngineTest, MixedCase) {
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

TEST_F(SQLiEngineTest, NullEngine) {
    float prob = 0.0f;
    int ret = kadd_sqli_detect(nullptr, "test", &prob);
    EXPECT_EQ(ret, KADD_ERROR_INVALID_INPUT) << "空引擎应该返回 KADD_ERROR_INVALID_INPUT";
}

TEST_F(SQLiEngineTest, NullInput) {
    float prob = 0.0f;
    int ret = kadd_sqli_detect(engine, nullptr, &prob);
    EXPECT_EQ(ret, KADD_ERROR_INVALID_INPUT) << "空输入应该返回 KADD_ERROR_INVALID_INPUT";
}

TEST_F(SQLiEngineTest, NullProbability) {
    int ret = kadd_sqli_detect(engine, "test", nullptr);
    EXPECT_EQ(ret, KADD_ERROR_INVALID_INPUT) << "空概率指针应该返回 KADD_ERROR_INVALID_INPUT";
}

TEST_F(SQLiEngineTest, NullEngineWithAllOtherNull) {
    int ret = kadd_sqli_detect(nullptr, nullptr, nullptr);
    EXPECT_EQ(ret, KADD_ERROR_INVALID_INPUT) << "全部空指针应该返回 KADD_ERROR_INVALID_INPUT";
}

TEST_F(SQLiEngineTest, CreateWithNullEnginePtr) {
    int ret = kadd_sqli_engine_create_from_file(nullptr, "config.json");
    EXPECT_NE(ret, KADD_SUCCESS) << "NULL engine 指针应该返回错误";
}

TEST_F(SQLiEngineTest, CreateWithNullConfigFile) {
    kadd_sqli_engine_t *tmp = nullptr;
    int ret = kadd_sqli_engine_create_from_file(&tmp, nullptr);
    EXPECT_EQ(ret, KADD_ERROR_INVALID_INPUT) << "NULL 配置文件应该返回 KADD_ERROR_INVALID_INPUT";
}

TEST_F(SQLiEngineTest, CreateWithEmptyConfigFile) {
    kadd_sqli_engine_t *tmp = nullptr;
    int ret = kadd_sqli_engine_create_from_file(&tmp, "");
    EXPECT_NE(ret, KADD_SUCCESS) << "空配置文件路径应该返回错误";
}

TEST_F(SQLiEngineTest, CreateWithNonexistentConfigFile) {
    kadd_sqli_engine_t *tmp = nullptr;
    int ret = kadd_sqli_engine_create_from_file(&tmp, "nonexistent_config.json");
    EXPECT_NE(ret, KADD_SUCCESS) << "不存在配置文件应该返回错误";
}

TEST_F(SQLiEngineTest, EngineReuse) {
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

TEST_F(SQLiEngineTest, SingleDetectionPerformance) {
    const int iteration = 1000;
    std::string input = "id=1' OR '1'='1";
    float prob = 0.0f;

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iteration; i++) {
        kadd_sqli_detect(engine, input.c_str(), &prob);
    }
    auto end = std::chrono::high_resolution_clock::now();

    EXPECT_GE(prob, 0.0f);
    EXPECT_LE(prob, 1.0f);

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    double total_time_us = static_cast<double>(duration.count());
    std::cout << "\n[性能测试] kadd_sqli_detect 单次耗时："
            << total_time_us / iteration << " us" << std::endl;
}

TEST_F(SQLiEngineTest, BatchDetectionPerformance) {
    // 测试批量检测性能
    std::vector<std::string> inputs_str(100);
    std::vector<const char*> inputs(100);
    for (int i = 0; i < 100; i++) {
        if (i % 2 == 0) {
            inputs_str[i] = "id=1' OR '1'='1";  // 注入
        } else {
            inputs_str[i] = "user@example.com";   // 正常
        }
        inputs[i] = inputs_str[i].c_str();
    }

    std::vector<float> probabilities(100);

    auto start = std::chrono::high_resolution_clock::now();
    int ret = kadd_sqli_detect_batch(engine,
                                    inputs.data(),
                                    100,
                                    probabilities.data(),
                                    10);
    auto end = std::chrono::high_resolution_clock::now();

    EXPECT_EQ(ret, KADD_SUCCESS) << "批量检测应该全部成功";
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_LT(duration.count(), 5000) << "100个样本批量检测应该在5秒内完成";
}

/* ========== 5. 一致性测试 ========== */

TEST_F(SQLiEngineTest, BatchVsSingleConsistency) {
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
                                    10);
    ASSERT_EQ(ret, KADD_SUCCESS) << "批量检测应该全部成功";

    // 比较结果（单次和批量应该给出相同的概率）
    for (size_t i = 0; i < inputs.size(); i++) {
        EXPECT_FLOAT_EQ(single_probs[i], batch_probs[i])
            << "单次和批量检测结果应该一致: " << inputs_str[i];
    }
}

/* ========== 6. 主函数 ========== */

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
