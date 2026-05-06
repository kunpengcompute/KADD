/**
 * @file test_sqli_engine_gtest.cpp
 * @brief SQL 注入检测引擎 Google Test 测试套件
 *
 * 包含功能测试、边界测试、异常测试、性能测试
 */

#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <chrono>
#include "kadd_sqli_engine.h"

/* ========== 测试夹具 ========== */

class SQLiEngineTest : public ::testing::Test {
protected:
    kadd_sqli_engine_t *engine;
    float threshold = 0.5f;  // 默认阈值

    void SetUp() override {
        // 每个测试前创建引擎
        ASSERT_EQ(kadd_sqli_engine_create_from_file(&engine, "../config.json"), KADD_SQLI_OK)
            << "引擎创建失败，请检查 config.json 是否存在";
    }

    void TearDown() override {
        // 每个测试后释放引擎
        if (engine) {
            kadd_sqli_engine_free(engine);
            engine = nullptr;
        }
    }

    // 辅助函数：检测输入并返回概率
    float detect(const std::string &input) {
        float probability = 0.0f;
        int ret = kadd_sqli_detect(engine, input.c_str(), &probability);
        EXPECT_EQ(ret, KADD_SQLI_OK) << "检测失败: " << input;
        return probability;
    }

    // 辅助函数：检查是否为 SQL 注入
    bool is_sqli(const std::string &input, float thresh) {
        return detect(input) >= thresh;
    }
};

/* ========== 1. 功能测试 ========== */

TEST_F(SQLiEngineTest, BasicFunctionality) {
    // 测试基本功能是否正常工作
    float prob = detect("id=1' OR '1'='1");
    EXPECT_GE(prob, 0.0f);
    EXPECT_LE(prob, 1.0f);
}

TEST_F(SQLiEngineTest, ClassicORInjection) {
    // 测试经典 OR 注入
    std::vector<std::string> injections = {
        "id=1' OR '1'='1",
        "admin' OR 1=1--",
        "1' OR 1=1--",
        "' OR '1'='1'--",
        "\" OR \"1\"=\"1\"--"
    };

    for (const auto &injection : injections) {
        float prob = detect(injection);
        EXPECT_GE(prob, threshold) << "应该检测为注入: " << injection;
    }
}

TEST_F(SQLiEngineTest, CommentBypass) {
    // 测试注释绕过
    std::vector<std::string> injections = {
        "admin'--",
        "id=1' OR '1'='1#",
        "1' OR 1=1--",
        "admin'/*comment*/",
        "id=1'/*comment*/OR/*comment*/'1'='1"
    };

    for (const auto &injection : injections) {
        float prob = detect(injection);
        EXPECT_GE(prob, threshold) << "应该检测为注入: " << injection;
    }
}

TEST_F(SQLiEngineTest, UnionInjection) {
    // 测试 UNION 注入
    std::vector<std::string> injections = {
        "1' UNION SELECT NULL--",
        "1' UNION SELECT NULL,NULL--",
        "1' UNION SELECT NULL,NULL,NULL--",
        "' UNION SELECT username,password FROM users--",
        "admin' UNION SELECT 1,version(),3--"
    };

    for (const auto &injection : injections) {
        float prob = detect(injection);
        EXPECT_GE(prob, threshold) << "应该检测为注入: " << injection;
    }
}

TEST_F(SQLiEngineTest, StackedQuery) {
    // 测试堆叠查询注入
    std::vector<std::string> injections = {
        "id=1; DROP TABLE users--",
        "id=1'; DELETE FROM users--",
        "1'; INSERT INTO users VALUES ('hacker','password')--",
        "id=1'; UPDATE users SET password='hacked'--"
    };

    for (const auto &injection : injections) {
        float prob = detect(injection);
        EXPECT_GE(prob, threshold) << "应该检测为注入: " << injection;
    }
}

TEST_F(SQLiEngineTest, BooleanBlind) {
    // 测试布尔盲注
    std::vector<std::string> injections = {
        "id=1' AND 1=1--",
        "id=1' AND 1=2--",
        "1' AND SLEEP(5)--",
        "' OR BENCHMARK(5000000,MD5(1))--"
    };

    for (const auto &injection : injections) {
        float prob = detect(injection);
        EXPECT_GE(prob, threshold) << "应该检测为注入: " << injection;
    }
}

TEST_F(SQLiEngineTest, TimeBlind) {
    // 测试时间盲注
    std::vector<std::string> injections = {
        "'; WAITFOR DELAY '00:00:05'--",
        "1; SELECT SLEEP(5)--",
        "' OR PG_SLEEP(5)--",
        "' OR DBMS_PIPE.RECEIVE_MESSAGE('a',5)--"
    };

    for (const auto &injection : injections) {
        float prob = detect(injection);
        EXPECT_GE(prob, threshold) << "应该检测为注入: " << injection;
    }
}

TEST_F(SQLiEngineTest, ErrorBased) {
    // 测试错误注入
    std::vector<std::string> injections = {
        "id=1' AND 1=CONVERT(int,VERSION())--",
        "id=1' AND 1=CAST(VERSION() AS INT)--",
        "id=1,(SELECT 1 FROM (SELECT COUNT(*),CONCAT(0x3a,VERSION(),FLOOR(RAND(0)*2))x FROM information_schema.tables GROUP BY x)a)--"
    };

    for (const auto &injection : injections) {
        float prob = detect(injection);
        EXPECT_GE(prob, threshold) << "应该检测为注入: " << injection;
    }
}

TEST_F(SQLiEngineTest, EncodedBypass) {
    // 测试编码绕过
    std::vector<std::string> injections = {
        "id=%27%20OR%20%271%27=%271",
        "id=1%2527%2520OR%2520%25271%2527=%25271",
        "id=\\x27\\x20OR\\x20\\x271\\x27=\\x271"
    };

    for (const auto &injection : injections) {
        float prob = detect(injection);
        EXPECT_GE(prob, threshold) << "应该检测为注入: " << injection;
    }
}

TEST_F(SQLiEngineTest, SecondOrder) {
    // 测试二次注入
    std::vector<std::string> injections = {
        "username=admin'#",
        "username=\"; DROP TABLE users--",
        "email=[SQL注入]"
    };

    for (const auto &injection : injections) {
        float prob = detect(injection);
        EXPECT_GE(prob, threshold) << "应该检测为注入: " << injection;
    }
}

TEST_F(SQLiEngineTest, NormalTraffic_Email) {
    // 测试正常邮箱
    std::vector<std::string> normal_inputs = {
        "user@example.com",
        "john.doe@company.co.uk",
        "test+tag@email.com"
    };

    for (const auto &input : normal_inputs) {
        float prob = detect(input);
        EXPECT_LT(prob, threshold) << "正常邮箱应该是安全的: " << input;
    }
}

TEST_F(SQLiEngineTest, NormalTraffic_SQL) {
    // 测试正常 SQL 语句
    std::vector<std::string> normal_sql = {
        "SELECT * FROM users WHERE id=1",
        "INSERT INTO users (name) VALUES ('John')",
        "UPDATE products SET price=100 WHERE id=5",
        "DELETE FROM logs WHERE created_at < '2024-01-01'"
    };

    for (const auto &sql : normal_sql) {
        float prob = detect(sql);
        EXPECT_LT(prob, threshold) << "正常 SQL 应该是安全的: " << sql;
    }
}

TEST_F(SQLiEngineTest, NormalTraffic_WebParams) {
    // 测试正常 Web 参数
    std::vector<std::string> params = {
        "page=1&limit=10",
        "category=electronics&sort=price",
        "search=python+tutorial",
        "filter=date:2024-01-01"
    };

    for (const auto &param : params) {
        float prob = detect(param);
        EXPECT_LT(prob, threshold) << "正常参数应该是安全的: " << param;
    }
}

/* ========== 2. 边界测试 ========== */

TEST_F(SQLiEngineTest, EmptyInput) {
    // 测试空输入
    float prob = detect("");
    EXPECT_EQ(prob, 0.0f) << "空输入应该返回 0 概率";
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

TEST_F(SQLiEngineTest, VeryLongInput) {
    // 测试长输入
    std::string long_input(5000, 'a');
    float prob = detect(long_input);
    EXPECT_GE(prob, 0.0f) << "长输入应该成功处理";
    EXPECT_LE(prob, 1.0f);
}

TEST_F(SQLiEngineTest, MaxInputBoundary) {
    // 测试接近最大限制的输入
    std::string max_input(8000, 'a');
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
    // 测试空引擎指针
    float prob = 0.0f;
    int ret = kadd_sqli_detect(nullptr, "test", &prob);
    EXPECT_NE(ret, KADD_SQLI_OK) << "空引擎应该返回错误";
}

TEST_F(SQLiEngineTest, NullInput) {
    // 测试空输入指针
    float prob = 0.0f;
    int ret = kadd_sqli_detect(engine, nullptr, &prob);
    EXPECT_NE(ret, KADD_SQLI_OK) << "空输入应该返回错误";
}

TEST_F(SQLiEngineTest, NullProbability) {
    // 测试空概率指针
    int ret = kadd_sqli_detect(engine, "test", nullptr);
    EXPECT_NE(ret, KADD_SQLI_OK) << "空概率指针应该返回错误";
}

TEST_F(SQLiEngineTest, NullEngineWithAllOtherNull) {
    // 测试全部为空指针
    int ret = kadd_sqli_detect(nullptr, nullptr, nullptr);
    EXPECT_NE(ret, KADD_SQLI_OK) << "全部空指针应该返回错误";
}

TEST_F(SQLiEngineTest, EngineReuse) {
    // 测试引擎重用
    float prob1 = detect("id=1' OR '1'='1");
    float prob2 = detect("admin'--");
    float prob3 = detect("user@example.com");

    EXPECT_GE(prob1, threshold) << "第一次检测应该识别注入";
    EXPECT_GE(prob2, threshold) << "第二次检测应该识别注入";
    EXPECT_LT(prob3, threshold) << "第三次检测应该是安全的";
}

/* ========== 4. 性能测试 ========== */

TEST_F(SQLiEngineTest, SingleDetectionPerformance) {
    // 测试单次检测性能
    std::string input = "id=1' OR '1'='1";

    auto start = std::chrono::high_resolution_clock::now();
    float prob = detect(input);
    auto end = std::chrono::high_resolution_clock::now();

    EXPECT_GE(prob, threshold) << "检测应该成功";
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_LT(duration.count(), 100) << "单次检测应该在100ms内完成";
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

    EXPECT_EQ(ret, 100) << "批量检测应该全部成功";
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_LT(duration.count(), 5000) << "100个样本批量检测应该在5秒内完成";
}

TEST_F(SQLiEngineTest, LargeInputPerformance) {
    // 测试大输入性能
    std::string large_input(2000, 'a');
    large_input += "' OR '1'='1";

    auto start = std::chrono::high_resolution_clock::now();
    float prob = detect(large_input);
    auto end = std::chrono::high_resolution_clock::now();

    EXPECT_GE(prob, threshold) << "大输入应该检测为注入";
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_LT(duration.count(), 200) << "大输入检测应该在200ms内完成";
}

/* ========== 5. 综合场景测试 ========== */

TEST_F(SQLiEngineTest, RealWorldScenario_Login) {
    // 测试真实登录场景
    std::vector<std::pair<std::string, bool>> scenarios = {
        {"login.php?user=admin'--&pass=any", true},     // 注入
        {"login.php?user=admin&pass=123456", false},    // 正常
        {"login.php?user=admin' OR '1'='1--", true}     // 注入
    };

    for (const auto &scenario : scenarios) {
        float prob = detect(scenario.first);
        bool is_injection = prob >= threshold;

        if (scenario.second) {
            EXPECT_TRUE(is_injection) << "应该检测为注入: " << scenario.first;
        } else {
            EXPECT_FALSE(is_injection) << "应该是安全的: " << scenario.first;
        }
    }
}

TEST_F(SQLiEngineTest, RealWorldScenario_Search) {
    // 测试真实搜索场景
    std::vector<std::pair<std::string, bool>> scenarios = {
        {"search.php?q=test' AND '1'='1", true},    // 注入
        {"search.php?q=python+tutorial", false},    // 正常
        {"search?q=<script>alert(1)</script>", true} // XSS（也可能检测为异常）
    };

    for (const auto &scenario : scenarios) {
        float prob = detect(scenario.first);
        bool is_injection = prob >= threshold;

        if (scenario.second) {
            EXPECT_TRUE(is_injection) << "应该检测为注入: " << scenario.first;
        } else {
            EXPECT_FALSE(is_injection) << "应该是安全的: " << scenario.first;
        }
    }
}

TEST_F(SQLiEngineTest, RealWorldScenario_API) {
    // 测试真实 API 场景
    std::vector<std::pair<std::string, bool>> scenarios = {
        {"api/user?id=1; DELETE FROM logs--", true},       // 注入
        {"api/user?id=1", false},                           // 正常
        {"api/user?id=1' UNION SELECT username,password--", true}  // 注入
    };

    for (const auto &scenario : scenarios) {
        float prob = detect(scenario.first);
        bool is_injection = prob >= threshold;

        if (scenario.second) {
            EXPECT_TRUE(is_injection) << "应该检测为注入: " << scenario.first;
        } else {
            EXPECT_FALSE(is_injection) << "应该是安全的: " << scenario.first;
        }
    }
}

/* ========== 6. 阈值敏感性测试 ========== */

TEST_F(SQLiEngineTest, DifferentThresholds) {
    // 测试不同阈值的影响
    std::string injection = "id=1' OR '1'='1";
    std::string normal = "user@example.com";

    float injection_prob = detect(injection);
    float normal_prob = detect(normal);

    // 测试不同阈值
    std::vector<float> thresholds = {0.3f, 0.5f, 0.7f, 0.9f};

    for (float thresh : thresholds) {
        bool injection_detected = injection_prob >= thresh;
        bool normal_detected = normal_prob >= thresh;

        EXPECT_TRUE(injection_detected) << "注入应该被检测到 (阈值=" << thresh << ")";
        EXPECT_FALSE(normal_detected) << "正常不应该被误报 (阈值=" << thresh << ")";
    }
}

/* ========== 7. 一致性测试 ========== */

TEST_F(SQLiEngineTest, DetectionConsistency) {
    // 测试检测一致性
    std::string input = "id=1' OR '1'='1";

    // 多次检测同一输入
    std::vector<float> probs(10);
    for (int i = 0; i < 10; i++) {
        probs[i] = detect(input);
    }

    // 所有结果应该一致
    for (int i = 1; i < 10; i++) {
        EXPECT_FLOAT_EQ(probs[0], probs[i]) << "检测结果应该一致";
    }
}

TEST_F(SQLiEngineTest, BatchVsSingleConsistency) {
    // 测试批量检测与单次检测的一致性
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
    ASSERT_EQ(ret, inputs.size()) << "批量检测应该全部成功";

    // 比较结果
    for (size_t i = 0; i < inputs.size(); i++) {
        EXPECT_FLOAT_EQ(single_probs[i], batch_probs[i])
            << "单次和批量检测结果应该一致: " << inputs_str[i];
    }
}

/* ========== 8. 主函数 ========== */

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
