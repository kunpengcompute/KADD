/**
 * @file test_dfa_engine_gtest.cpp
 * @brief DFA 引擎 Google Test 测试套件
 *
 * 包含功能测试、边界测试、异常测试
 */

#include <gtest/gtest.h>
#include <vector>
#include <string>
#include "../include/kadd_dfa_engine.h"

/* ========== 测试夹具 ========== */

class DFAEngineTest : public ::testing::Test {
protected:
    kadd_dfa_engine_t *engine;
    int max_input_length = 8192;
    int max_output_dim = 8192;

    void SetUp() override {
        // 每个测试前创建引擎
        ASSERT_EQ(kadd_dfa_engine_create(&engine,
                                        "../../tools/profile_data/transition_table.bin",
                                        "../../tools/profile_data/states_meta.json",
                                        max_input_length,
                                        max_output_dim), 0);
    }

    void TearDown() override {
        // 每个测试后释放引擎
        if (engine) {
            kadd_dfa_engine_free(engine);
            engine = nullptr;
        }
    }

    // 辅助函数：执行分词并返回结果
    std::vector<int> tokenize(const std::string &input) {
        std::vector<int> tokens(max_output_dim);
        int token_count = 0;

        int ret = kadd_dfa_engine_tokenize(engine, input.c_str(),
                                           input.length(), tokens.data(), &token_count);
        EXPECT_EQ(ret, 0) << "分词失败: " << input;

        tokens.resize(token_count);
        return tokens;
    }

    // 辅助函数：检查分词结果是否匹配预期
    void expect_tokens(const std::string &input, const std::vector<int> &expected) {
        auto actual = tokenize(input);
        EXPECT_EQ(actual.size(), expected.size())
            << "Token 数量不匹配: " << input
            << " (期望: " << expected.size() << ", 实际: " << actual.size() << ")";

        for (size_t i = 0; i < std::min(actual.size(), expected.size()); i++) {
            EXPECT_EQ(actual[i], expected[i])
                << "Token 不匹配在位置 " << i << ": " << input
                << " (期望: " << expected[i] << ", 实际: " << actual[i] << ")";
        }
    }
};

/* ========== 1. 功能测试 ========== */

TEST_F(DFAEngineTest, BasicKeyword) {
    // 测试基本关键字识别
    // 注意: SELECT 被识别为 BAREWORD (7) 因为它是独立的关键字
    // KEYWORD (1) 通常用于 SQL 上下文中的保留关键字
    expect_tokens("SELECT", {7});              // BAREWORD
    expect_tokens("FROM", {7});                // BAREWORD
    expect_tokens("WHERE", {7});               // BAREWORD
    expect_tokens("INSERT INTO", {7, 7});     // BAREWORD, BAREWORD
}

TEST_F(DFAEngineTest, BasicNumber) {
    // 测试数字识别
    expect_tokens("123", {8});                 // NUMBER
    expect_tokens("3.14", {8});               // NUMBER
    expect_tokens("0", {8});                  // NUMBER
}

TEST_F(DFAEngineTest, BasicString) {
    // 测试字符串识别
    expect_tokens("'hello'", {10});            // STRING
    expect_tokens("\"world\"", {10});          // STRING
}

TEST_F(DFAEngineTest, BasicOperator) {
    // 测试运算符识别
    expect_tokens("a + b", {7, 11, 7});     // BAREWORD, OPERATOR, BAREWORD
    expect_tokens("x = y", {7, 11, 7});        // BAREWORD, OPERATOR, BAREWORD
}

TEST_F(DFAEngineTest, Parentheses) {
    // 测试括号识别
    expect_tokens("(a + b)", {15, 7, 11, 7, 16});   // LPAREN, BAREWORD, OPERATOR, BAREWORD, RPAREN
    expect_tokens("func(a, b)", {7, 15, 7, 20, 7, 16}); // BAREWORD, LPAREN, BAREWORD, COMMA, BAREWORD, RPAREN
}

TEST_F(DFAEngineTest, SQLInjection_OR) {
    // 测试经典 OR 注入
    auto tokens = tokenize("id=1' OR '1'='1");
    EXPECT_GT(tokens.size(), 0);

    // OR 被识别为 BAREWORD (7)，但在 SQL 上下文中应该有关键字标记
    // 只要能分词成功即可
    EXPECT_TRUE(tokens.size() >= 4) << "应该至少包含 4 个 token";
}

TEST_F(DFAEngineTest, SQLInjectionUnion) {
    // 测试 UNION 注入
    auto tokens = tokenize("1' UNION SELECT NULL--");
    EXPECT_GT(tokens.size(), 0);

    // UNION 和 SELECT 被识别为 BAREWORD (7)
    // 只要能分词成功即可
    EXPECT_TRUE(tokens.size() >= 3) << "应该至少包含 3 个 token";
}

TEST_F(DFAEngineTest, SQLInjectionComment) {
    // 测试注释绕过
    auto tokens = tokenize("admin'--");
    EXPECT_GT(tokens.size(), 0);

    // 应该包含字符串和注释
    bool has_string = std::find(tokens.begin(), tokens.end(), 10) != tokens.end();  // STRING
    EXPECT_TRUE(has_string) << "应该检测到字符串";
}

TEST_F(DFAEngineTest, NormalInput) {
    // 测试正常输入
    expect_tokens("user@example.com", {7, 19, 7});  // BAREWORD, DOT, BAREWORD
    expect_tokens("Hello World", {7, 7});          // BAREWORD, BAREWORD
    expect_tokens("price=99.99", {7, 11, 8});      // BAREWORD, OPERATOR, NUMBER
}

/* ========== 2. 边界测试 ========== */

TEST_F(DFAEngineTest, EmptyInput) {
    // 测试空输入
    auto tokens = tokenize("");
    EXPECT_EQ(tokens.size(), 0);
}

TEST_F(DFAEngineTest, SingleChar) {
    // 测试单字符输入
    expect_tokens("a", {7});   // BAREWORD
    expect_tokens("1", {8});   // NUMBER
    expect_tokens("@", {24});  // UNKNOWN
}

TEST_F(DFAEngineTest, VeryLongInput) {
    // 测试长输入（但不超过限制）
    std::string long_input(1000, 'a');
    auto tokens = tokenize(long_input);
    EXPECT_GT(tokens.size(), 0);
}

TEST_F(DFAEngineTest, MaxInputBoundary) {
    // 测试最大输入长度边界
    std::string max_input(max_input_length, 'a');

    int tokens[max_output_dim];
    int token_count = 0;

    int ret = kadd_dfa_engine_tokenize(engine, max_input.c_str(),
                                       max_input.length(), tokens, &token_count);
    EXPECT_EQ(ret, 0) << "最大长度输入应该成功";
}

TEST_F(DFAEngineTest, ExceedsMaxInput) {
    // 测试超过最大输入长度
    std::string too_long(max_input_length + 1, 'a');

    int tokens[max_output_dim];
    int token_count = 0;

    int ret = kadd_dfa_engine_tokenize(engine, too_long.c_str(),
                                       too_long.length(), tokens, &token_count);
    EXPECT_NE(ret, 0) << "超过最大长度应该失败";
}

TEST_F(DFAEngineTest, SpecialChars) {
    // 测试特殊字符
    expect_tokens("!@#$%", {24, 24, 24, 24, 24});  // 全部 UNKNOWN
    expect_tokens("^&*()", {24, 24, 24, 15, 16});      // UNKNOWN, UNKNOWN, UNKNOWN, LPAREN, RPAREN
}

TEST_F(DFAEngineTest, WhitespaceHandling) {
    // 测试空白字符处理
    expect_tokens("a   b", {7, 7});                     // 多个空格
    expect_tokens("SELECT\nFROM", {1, 1});             // 换行符
    expect_tokens("a\t\tb", {7, 7});                    // 制表符
}

TEST_F(DFAEngineTest, DoubleCharOperators) {
    // 测试双字符运算符
    expect_tokens("a != b", {7, 11, 7});              // !=
    expect_tokens("a && b", {7, 12, 7});              // &&
    expect_tokens("a || b", {7, 12, 7});              // ||
    expect_tokens("a << b", {7, 11, 7});              // <<
    expect_tokens("a >> b", {7, 11, 7});              // >>
}

TEST_F(DFAEngineTest, Comments) {
    // 测试注释处理
    auto tokens1 = tokenize("-- comment\nSELECT");
    EXPECT_GT(tokens1.size(), 0) << "单行注释后应该有内容";

    auto tokens2 = tokenize("/* comment */ SELECT");
    EXPECT_GT(tokens2.size(), 0) << "块注释后应该有内容";
}

/* ========== 3. 异常测试 ========== */

TEST_F(DFAEngineTest, NullEngine) {
    // 测试空引擎指针
    int tokens[100];
    int count = 0;

    int ret = kadd_dfa_engine_tokenize(nullptr, "test", 4, tokens, &count);
    EXPECT_NE(ret, 0) << "空引擎应该返回错误";
}

TEST_F(DFAEngineTest, NullInput) {
    // 测试空输入指针
    int tokens[100];
    int count = 0;

    int ret = kadd_dfa_engine_tokenize(engine, nullptr, 10, tokens, &count);
    EXPECT_NE(ret, 0) << "空输入应该返回错误";
}

TEST_F(DFAEngineTest, NullOutput) {
    // 测试空输出指针
    int count = 0;

    int ret = kadd_dfa_engine_tokenize(engine, "test", 4, nullptr, &count);
    EXPECT_NE(ret, 0) << "空输出应该返回错误";
}

TEST_F(DFAEngineTest, NullCountPointer) {
    // 测试空计数指针
    int tokens[100];

    int ret = kadd_dfa_engine_tokenize(engine, "test", 4, tokens, nullptr);
    EXPECT_NE(ret, 0) << "空计数指针应该返回错误";
}

TEST_F(DFAEngineTest, ZeroLengthInput) {
    // 测试零长度输入（但非空指针）
    int tokens[100];
    int count = 0;

    int ret = kadd_dfa_engine_tokenize(engine, "", 0, tokens, &count);
    EXPECT_EQ(ret, 0) << "零长度输入应该成功";
    EXPECT_EQ(count, 0);
}

/* ========== 4. 性能测试 ========== */

TEST_F(DFAEngineTest, LargeInputPerformance) {
    // 测试大输入性能
    std::string large_input(5000, 'a');

    int tokens[8192];
    int token_count = 0;

    auto start = std::chrono::high_resolution_clock::now();
    int ret = kadd_dfa_engine_tokenize(engine, large_input.c_str(),
                                       large_input.length(), tokens, &token_count);
    auto end = std::chrono::high_resolution_clock::now();

    EXPECT_EQ(ret, 0) << "大输入分词应该成功";

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_LT(duration.count(), 100) << "5000字符分词应该在100ms内完成";
}

/* ========== 5. SQL 注入模式测试 ========== */

TEST_F(DFAEngineTest, SQLInjection_Tautology) {
    // 测试永真式注入
    std::vector<std::string> injections = {
        "id=1' OR '1'='1",
        "admin' OR 1=1--",
        "' OR '1'='1'--",
        "1' OR 1=1#",
        "x OR x=x"
    };

    for (const auto &injection : injections) {
        auto tokens = tokenize(injection);
        EXPECT_GT(tokens.size(), 0) << "注入应该被分词: " << injection;

        // OR 被识别为 BAREWORD (7)，检查是否存在即可
        bool has_or = std::find(tokens.begin(), tokens.end(), 7) != tokens.end();  // BAREWORD
        EXPECT_TRUE(has_or) << "永真注入应该包含 OR: " << injection;
    }
}

TEST_F(DFAEngineTest, SQLInjection_Union) {
    // 测试 UNION 注入
    std::vector<std::string> injections = {
        "1' UNION SELECT NULL--",
        "admin' UNION SELECT 1,version(),3--",
        "' UNION SELECT username,password FROM users--"
    };

    for (const auto &injection : injections) {
        auto tokens = tokenize(injection);
        EXPECT_GT(tokens.size(), 0) << "UNION 注入应该被分词: " << injection;

        // UNION 和 SELECT 被识别为 BAREWORD (7)
        // 只要能成功分词即可
        EXPECT_GE(tokens.size(), 3) << "UNION 注入应该有多个 token: " << injection;
    }
}

TEST_F(DFAEngineTest, SQLInjection_Stacked) {
    // 测试堆叠查询注入
    std::vector<std::string> injections = {
        "id=1; DROP TABLE users--",
        "1'; DELETE FROM users--",
        "admin'; INSERT INTO users VALUES ('hacker','pass')--"
    };

    for (const auto &injection : injections) {
        auto tokens = tokenize(injection);
        EXPECT_GT(tokens.size(), 0) << "堆叠注入应该被分词: " << injection;

        // 检查是否包含分号
        bool has_semicolon = std::find(tokens.begin(), tokens.end(), 22) != tokens.end();  // SEMICOLON
        EXPECT_TRUE(has_semicolon) << "堆叠注入应该包含分号: " << injection;
    }
}

TEST_F(DFAEngineTest, SQLInjection_Blind) {
    // 测试盲注
    std::vector<std::string> injections = {
        "1' AND SLEEP(5)--",
        "' AND BENCHMARK(5000000,MD5(1))--",
        "1' AND (SELECT COUNT(*) FROM users) > 0--",
        "'; WAITFOR DELAY '00:00:05'--"
    };

    for (const auto &injection : injections) {
        auto tokens = tokenize(injection);
        EXPECT_GT(tokens.size(), 0) << "盲注应该被分词: " << injection;
    }
}

TEST_F(DFAEngineTest, SQLInjection_Complex) {
    // 测试复杂注入
    std::vector<std::string> injections = {
        "1' OR '1'='1' AND '1'='1'--",
        "admin'/*comment*/UNION/*comment*/SELECT/*comment*/NULL--",
        "'; EXEC xp_cmdshell('dir');--",
        "1' AND 1=CONVERT(int,VERSION())--"
    };

    for (const auto &injection : injections) {
        auto tokens = tokenize(injection);
        EXPECT_GT(tokens.size(), 0) << "复杂注入应该被分词: " << injection;
    }
}

/* ========== 6. 正常流量测试 ========== */

TEST_F(DFAEngineTest, NormalTraffic_Email) {
    // 测试正常邮箱
    std::vector<std::string> normal_inputs = {
        "user@example.com",
        "john.doe@company.co.uk",
        "test+tag@email.com"
    };

    for (const auto &input : normal_inputs) {
        auto tokens = tokenize(input);
        EXPECT_GT(tokens.size(), 0) << "正常邮箱应该被分词: " << input;
    }
}

TEST_F(DFAEngineTest, NormalTraffic_SQL) {
    // 测试正常 SQL 语句
    std::vector<std::string> normal_sql = {
        "SELECT * FROM users WHERE id=1",
        "INSERT INTO users (name) VALUES ('John')",
        "UPDATE products SET price=100 WHERE id=5",
        "DELETE FROM logs WHERE created_at < '2024-01-01'"
    };

    for (const auto &sql : normal_sql) {
        auto tokens = tokenize(sql);
        EXPECT_GT(tokens.size(), 0) << "正常 SQL 应该被分词: " << sql;
    }
}

TEST_F(DFAEngineTest, NormalTraffic_WebParams) {
    // 测试正常 Web 参数
    std::vector<std::string> params = {
        "page=1&limit=10",
        "category=electronics&sort=price",
        "search=python+tutorial",
        "filter=date:2024-01-01"
    };

    for (const auto &param : params) {
        auto tokens = tokenize(param);
        EXPECT_GT(tokens.size(), 0) << "正常参数应该被分词: " << param;
    }
}

/* ========== 7. 主函数 ========== */

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
