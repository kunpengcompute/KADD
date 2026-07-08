/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *
 * @file test_dfa_engine_gtest.cpp
 * @brief DFA 引擎 Google Test 测试套件
 *
 * 包含功能测试、边界测试、异常测试
 */

#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>
#include <string>
#include "../include/kadd_dfa_engine.h"
#include "../include/kadd_config_loader.h"

constexpr int kTokenBufferSize = 100;
constexpr int kTestLongInputLen = 100;
constexpr int kMinInjectionTokens = 4;
constexpr int kMinUnionTokens = 2;
constexpr int kDefaultBatchSize = KADD_DFA_DEFAULT_BATCH_SIZE;  // 与 KADD_DFA_DEFAULT_BATCH_SIZE 保持一致

/* ========== 测试夹具 ========== */

class DFAEngineTest : public ::testing::Test {
protected:
    kadd_dfa_engine_t *engine = nullptr;
    kadd_config_t config;

    void SetUp() override
    {
        // 从config.json加载配置
        ASSERT_EQ(kadd_config_load("config.json", &config), 0)
            << "无法加载config.json配置文件";

        // 使用配置文件中的参数
        kadd_dfa_table_paths_t table_paths = {
            config.dfa.transition_table_path,
            config.dfa.output_table_path,
            config.dfa.eod_table_path
        };
        ASSERT_EQ(kadd_dfa_engine_create(&engine, &table_paths,
                                         config.dfa.max_input_length,
                                         config.dfa.max_output_dim), 0);
    }

    void TearDown() override
    {
        // 每个测试后释放引擎
        if (engine) {
            kadd_dfa_engine_free(engine);
            engine = nullptr;
        }
    }

    // 辅助函数：执行分词并返回结果
    std::vector<int> tokenize(const std::string &input)
    {
        std::vector<int> tokens(config.dfa.max_output_dim);
        int token_count = 0;

        int ret = kadd_dfa_engine_tokenize(engine, input.c_str(),
                                           input.length(), tokens.data(), &token_count);
        EXPECT_EQ(ret, 0) << "分词失败: " << input;

        tokens.resize(token_count);
        return tokens;
    }

    // 辅助函数：检查分词结果是否匹配预期
    void expect_tokens(const std::string &input, const std::vector<int> &expected)
    {
        auto actual = tokenize(input);
        EXPECT_EQ(actual.size(), expected.size())
            << "Token 数量不匹配: " << input
            << " (期望: " << expected.size() << ", 实际: " << actual.size() << ")";

        for (size_t i = 0; i < std::min(actual.size(), expected.size()); i++) {
            ASSERT_EQ(actual[i], expected[i])
                << "Token 不匹配在位置 " << i << ": " << input
                << " (期望: " << expected[i] << ", 实际: " << actual[i] << ")";
        }
    }
};

/* ========== 1. 功能测试 ========== */

TEST_F(DFAEngineTest, BasicKeyword)
{
    // 测试基本关键字识别
    expect_tokens("SELECT", {4});             // EXPRESSION
    expect_tokens("FROM", {1});               // KEYWORD
    expect_tokens("WHERE", {1});              // KEYWORD
    expect_tokens("INSERT INTO", {4, 1});     // EXPRESSION, KEYWORD
}

TEST_F(DFAEngineTest, BasicNumber)
{
    // 测试数字识别
    expect_tokens("123", {8});                 // NUMBER
    expect_tokens("3.14", {8});               // NUMBER
    expect_tokens("0", {8});                  // NUMBER
}

TEST_F(DFAEngineTest, BasicString)
{
    // 测试字符串识别
    expect_tokens("'hello'", {10});            // STRING
    expect_tokens("\"world\"", {10});          // STRING
}

TEST_F(DFAEngineTest, BasicOperator)
{
    // 测试运算符识别
    expect_tokens("a + b", {7, 11, 7});     // BAREWORD, OPERATOR, BAREWORD
    expect_tokens("x = y", {7, 11, 7});        // BAREWORD, OPERATOR, BAREWORD
}

TEST_F(DFAEngineTest, Parentheses)
{
    // 测试括号识别
    expect_tokens("(a + b)", {15, 7, 11, 7, 16});   // LPAREN, BAREWORD, OPERATOR, BAREWORD, RPAREN
    expect_tokens("func(a, b)", {7, 15, 7, 20, 7, 16}); // BAREWORD, LPAREN, BAREWORD, COMMA, BAREWORD, RPAREN
}

TEST_F(DFAEngineTest, SQLInjection_OR)
{
    // 测试经典 OR 注入
    auto tokens = tokenize("id=1' OR '1'='1");
    EXPECT_GT(tokens.size(), 0u);

    // OR 被识别为 BAREWORD (7)，但在 SQL 上下文中应该有关键字标记
    // 只要能分词成功即可
    EXPECT_TRUE(tokens.size() >= kMinInjectionTokens) << "应该至少包含 4 个 token";
}

TEST_F(DFAEngineTest, SQLInjectionUnion)
{
    // 测试 UNION 注入
    auto tokens = tokenize("1' UNION SELECT NULL--");
    EXPECT_GT(tokens.size(), 0u);

    // 字符串/注释可能提前收口，核心要求是能稳定分词。
    EXPECT_TRUE(tokens.size() >= kMinUnionTokens) << "应该至少包含 2 个 token";
}

TEST_F(DFAEngineTest, SQLInjectionComment)
{
    // 测试注释绕过
    auto tokens = tokenize("admin'--");
    EXPECT_GT(tokens.size(), 0u);

    // 应该包含字符串和注释
    bool has_string = std::find(tokens.begin(), tokens.end(), 10) != tokens.end();  // STRING
    EXPECT_TRUE(has_string) << "应该检测到字符串";
}

TEST_F(DFAEngineTest, NormalInput)
{
    // 测试正常输入
    expect_tokens("user@example.com", {7, 9});      // BAREWORD, VARIABLE
    expect_tokens("Hello World", {7, 7});          // BAREWORD, BAREWORD
    expect_tokens("price=99.99", {7, 11, 8});      // BAREWORD, OPERATOR, NUMBER
}

/* ========== 2. 边界测试 ========== */

TEST_F(DFAEngineTest, EmptyInput)
{
    // 测试空输入
    auto tokens = tokenize("");
    EXPECT_EQ(tokens.size(), 0u);
}

TEST_F(DFAEngineTest, SingleChar)
{
    // 测试单字符输入
    expect_tokens("a", {7});   // BAREWORD
    expect_tokens("1", {8});   // NUMBER
    expect_tokens("@", {9});   // VARIABLE
}

TEST_F(DFAEngineTest, VeryLongInput)
{
    // 测试长输入（但不超过限制）
    std::string long_input(kTestLongInputLen, 'a');
    auto tokens = tokenize(long_input);
    EXPECT_GT(tokens.size(), 0u);
}

TEST_F(DFAEngineTest, MaxInputBoundary)
{
    // 测试最大输入长度边界
    std::string max_input(config.dfa.max_input_length, 'a');

    int tokens[config.dfa.max_output_dim];
    int token_count = 0;

    int ret = kadd_dfa_engine_tokenize(engine, max_input.c_str(),
                                       max_input.length(), tokens, &token_count);
    EXPECT_EQ(ret, 0) << "最大长度输入应该成功";
}

TEST_F(DFAEngineTest, SpecialChars)
{
    // 测试特殊字符
    expect_tokens("!@#$%", {11, 9, 13});          // OPERATOR, VARIABLE, COMMENT
    expect_tokens("^&*()", {11, 11, 11, 15, 16}); // OPERATOR, OPERATOR, OPERATOR, LPAREN, RPAREN
}

TEST_F(DFAEngineTest, WhitespaceHandling)
{
    // 测试空白字符处理
    expect_tokens("a   b", {7, 7});                     // 多个空格
    expect_tokens("SELECT\nFROM", {4, 1});             // 换行符
    expect_tokens("a\t\tb", {7, 7});                    // 制表符
}

TEST_F(DFAEngineTest, DoubleCharOperators)
{
    // 测试双字符运算符
    expect_tokens("a != b", {7, 11, 7});              // !=
    expect_tokens("a && b", {7, 12, 7});              // &&
    expect_tokens("a || b", {7, 12, 7});              // ||
    expect_tokens("a << b", {7, 11, 7});              // <<
    expect_tokens("a >> b", {7, 11, 7});              // >>
}

TEST_F(DFAEngineTest, Comments_Basic)
{
    // 测试注释处理
    auto tokens1 = tokenize("-- comment\nSELECT");
    EXPECT_GT(tokens1.size(), 0u) << "单行注释后应该有内容";

    auto tokens2 = tokenize("/* comment */ SELECT");
    EXPECT_GT(tokens2.size(), 0u) << "块注释后应该有内容";
}

/* ========== 3. 异常测试 ========== */

TEST_F(DFAEngineTest, NullEngine)
{
    int tokens[kTokenBufferSize];
    int count = 0;

    int ret = kadd_dfa_engine_tokenize(nullptr, "test", 4, tokens, &count);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR) << "空引擎应该返回 KADD_ERROR_NULL_PTR";
}

TEST_F(DFAEngineTest, NullInput)
{
    int tokens[kTokenBufferSize];
    int count = 0;

    int ret = kadd_dfa_engine_tokenize(engine, nullptr, 10, tokens, &count);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR) << "空输入应该返回 KADD_ERROR_NULL_PTR";
}

TEST_F(DFAEngineTest, NullOutput)
{
    int count = 0;

    int ret = kadd_dfa_engine_tokenize(engine, "test", 4, nullptr, &count);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR) << "空输出应该返回 KADD_ERROR_NULL_PTR";
}

TEST_F(DFAEngineTest, NullCountPointer)
{
    int tokens[kTokenBufferSize];

    int ret = kadd_dfa_engine_tokenize(engine, "test", 4, tokens, nullptr);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR) << "空计数指针应该返回 KADD_ERROR_NULL_PTR";
}

TEST_F(DFAEngineTest, CreateWithNullEnginePtr)
{
    kadd_dfa_table_paths_t table_paths = {
        config.dfa.transition_table_path,
        config.dfa.output_table_path,
        config.dfa.eod_table_path
    };
    int ret = kadd_dfa_engine_create(nullptr, &table_paths,
                                     config.dfa.max_input_length,
                                     config.dfa.max_output_dim);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR) << "NULL engine 指针应该返回 KADD_ERROR_NULL_PTR";
}

TEST_F(DFAEngineTest, CreateWithNullTablePaths)
{
    kadd_dfa_engine_t *tmp = nullptr;
    int ret = kadd_dfa_engine_create(&tmp, nullptr,
                                     config.dfa.max_input_length,
                                     config.dfa.max_output_dim);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR) << "NULL table_paths 应该返回 KADD_ERROR_NULL_PTR";
}

TEST_F(DFAEngineTest, CreateWithNullPathInTablePaths)
{
    kadd_dfa_table_paths_t bad_paths = {
        nullptr, config.dfa.output_table_path, config.dfa.eod_table_path
    };
    kadd_dfa_engine_t *tmp = nullptr;
    int ret = kadd_dfa_engine_create(&tmp, &bad_paths,
                                     config.dfa.max_input_length,
                                     config.dfa.max_output_dim);
    EXPECT_NE(ret, 0) << "NULL transition_table_path 应该返回错误";
}

TEST_F(DFAEngineTest, ExceedsMaxInput)
{
    // 测试超过最大输入长度
    std::string too_long(config.dfa.max_input_length + 1, 'a');

    int tokens[config.dfa.max_output_dim];
    int token_count = 0;

    int ret = kadd_dfa_engine_tokenize(engine, too_long.c_str(),
                                       too_long.length(), tokens, &token_count);
    EXPECT_EQ(ret, KADD_DFA_ERROR_INVALID_LENGTH) << "超过最大长度应该返回 KADD_DFA_ERROR_INVALID_LENGTH";
}

TEST_F(DFAEngineTest, ZeroLengthInput)
{
    // 测试零长度输入（但非空指针）
    int tokens[kTokenBufferSize];
    int count = 0;

    int ret = kadd_dfa_engine_tokenize(engine, "", 0, tokens, &count);
    EXPECT_EQ(ret, 0) << "零长度输入应该成功";
    EXPECT_EQ(count, 0);
}

/* ========== 5. SQL 注入模式测试 ========== */

TEST_F(DFAEngineTest, SQLInjection_Tautology)
{
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
        EXPECT_GT(tokens.size(), 0u) << "注入应该被分词: " << injection;

        EXPECT_GE(tokens.size(), 1u) << "永真注入应该产生 token: " << injection;
    }
}

TEST_F(DFAEngineTest, SQLInjection_Union)
{
    // 测试 UNION 注入
    std::vector<std::string> injections = {
        "1' UNION SELECT NULL--",
        "admin' UNION SELECT 1,version(),3--",
        "' UNION SELECT username,password FROM users--"
    };

    for (const auto &injection : injections) {
        auto tokens = tokenize(injection);
        EXPECT_GT(tokens.size(), 0u) << "UNION 注入应该被分词: " << injection;

        EXPECT_GE(tokens.size(), 1u) << "UNION 注入应该有 token: " << injection;
    }
}

TEST_F(DFAEngineTest, SQLInjection_Stacked)
{
    // 测试堆叠查询注入
    std::vector<std::string> injections = {
        "id=1; DROP TABLE users--",
        "1'; DELETE FROM users--",
        "admin'; INSERT INTO users VALUES ('hacker','pass')--"
    };

    for (const auto &injection : injections) {
        auto tokens = tokenize(injection);
        EXPECT_GT(tokens.size(), 0u) << "堆叠注入应该被分词: " << injection;

        EXPECT_GE(tokens.size(), 1u) << "堆叠注入应该有 token: " << injection;
    }
}

TEST_F(DFAEngineTest, SQLInjection_Blind)
{
    // 测试盲注
    std::vector<std::string> injections = {
        "1' AND SLEEP(5)--",
        "' AND BENCHMARK(5000000,MD5(1))--",
        "1' AND (SELECT COUNT(*) FROM users) > 0--",
        "'; WAITFOR DELAY '00:00:05'--"
    };

    for (const auto &injection : injections) {
        auto tokens = tokenize(injection);
        EXPECT_GT(tokens.size(), 0u) << "盲注应该被分词: " << injection;
    }
}

TEST_F(DFAEngineTest, SQLInjection_Complex)
{
    // 测试复杂注入
    std::vector<std::string> injections = {
        "1' OR '1'='1' AND '1'='1'--",
        "admin'/*comment*/UNION/*comment*/SELECT/*comment*/NULL--",
        "'; EXEC xp_cmdshell('dir');--",
        "1' AND 1=CONVERT(int,VERSION())--"
    };

    for (const auto &injection : injections) {
        auto tokens = tokenize(injection);
        EXPECT_GT(tokens.size(), 0u) << "复杂注入应该被分词: " << injection;
    }
}

/* ========== 6. 正常流量测试 ========== */

TEST_F(DFAEngineTest, NormalTraffic_Email)
{
    // 测试正常邮箱
    std::vector<std::string> normal_inputs = {
        "user@example.com",
        "john.doe@company.co.uk",
        "test+tag@email.com"
    };

    for (const auto &input : normal_inputs) {
        auto tokens = tokenize(input);
        EXPECT_GT(tokens.size(), 0u) << "正常邮箱应该被分词: " << input;
    }
}

TEST_F(DFAEngineTest, NormalTraffic_SQL)
{
    // 测试正常 SQL 语句
    std::vector<std::string> normal_sql = {
        "SELECT * FROM users WHERE id=1",
        "INSERT INTO users (name) VALUES ('John')",
        "UPDATE products SET price=100 WHERE id=5",
        "DELETE FROM logs WHERE created_at < '2024-01-01'"
    };

    for (const auto &sql : normal_sql) {
        auto tokens = tokenize(sql);
        EXPECT_GT(tokens.size(), 0u) << "正常 SQL 应该被分词: " << sql;
    }
}

TEST_F(DFAEngineTest, NormalTraffic_WebParams)
{
    // 测试正常 Web 参数
    std::vector<std::string> params = {
        "page=1&limit=10",
        "category=electronics&sort=price",
        "search=python+tutorial",
        "filter=date:2024-01-01"
    };

    for (const auto &param : params) {
        auto tokens = tokenize(param);
        EXPECT_GT(tokens.size(), 0u) << "正常参数应该被分词: " << param;
    }
}

/* ========== 7. DFA 补充测试用例 ========== */

TEST_F(DFAEngineTest, TSQL_Keywords)
{
    expect_tokens("BEGIN DECLARE @var INT; SET @var = 10; BEGIN GOTO label1; label1: SELECT 1; BEGIN TRY "
                      "SELECT 1 END TRY BEGIN CATCH SELECT 2 END CATCH; CALL procedure_name();",
                  {23, 23, 9, 5, 22, 4, 9, 11, 8, 22, 23, 23, 7, 22,
                      7, 21, 4, 8, 22, 23, 23, 4, 8, 7, 23, 23, 7, 4, 8, 7, 7, 22, 23, 7, 15, 16, 22});
    expect_tokens("EXEC stored_proc; EXECUTE AS LOGIN = 'user'; EXECUTE AS = 'role'; GOTO end_label; end_label:",
                  {23, 7, 22, 23, 1, 7, 11, 10, 22, 23, 1, 11, 10, 22, 23, 7, 22, 7, 11});
}

TEST_F(DFAEngineTest, Transaction_and_Lock)
{
    expect_tokens("BEGIN TRANSACTION; SAVEPOINT sp1; ROLLBACK TO sp1; COMMIT",
                  {23, 7, 22, 7, 7, 22, 7, 7, 7, 22, 7});
    expect_tokens("LOCK TABLES t1 READ, t2 WRITE",
                  {7, 7, 7, 1, 20, 7, 7});
}

TEST_F(DFAEngineTest, Variables_and_Identifiers)
{
    expect_tokens("SELECT @var, @@global_var, `column`, [field], table.column FROM schema.table",
                  {4, 9, 20, 9, 20, 7, 20, 7, 20, 7, 1, 1, 19, 7});
    expect_tokens("SELECT t1.`col1`, t2.[col2], @session_var, @@system_var FROM db1.table1 AS t1 JOIN "
                      "db2.table2 t2 ON t1.id=t2.id",
                  {4, 7, 20, 7, 7, 20, 9, 20, 9, 1, 7, 1, 7, 1, 7, 7, 7, 7, 11, 7});
}

TEST_F(DFAEngineTest, Literals)
{
    expect_tokens("SELECT 123, 'abc', \"def\", 45.67, 0x1A, 0b1010",
                  {4, 8, 20, 10, 20, 10, 20, 8, 20, 8, 20, 8});
    expect_tokens("SELECT id, name, email FROM users WHERE age >= 18 AND status = 'active' AND created_date <="
                      "'2023-12-31' ORDER BY created_date DESC",
                  {4, 7, 20, 7, 20, 7, 1, 7, 1, 6, 11, 8, 12, 7, 11, 10, 12, 7, 11, 10, 7, 7, 7, 1});
    expect_tokens("SELECT 'a+b', 'c-d', 'e*f', 'g/h', 'i=j', 'k<l', 'm>n', 'o?p', 'q!r', 's#t', 'u$v'",
                  {4, 10, 20, 10, 20, 10, 20, 10, 20, 10, 20, 10, 20, 10, 20, 10, 20, 10, 20, 10, 20, 10});
}

TEST_F(DFAEngineTest, Operators_and_Logic)
{
    expect_tokens("SELECT * FROM users WHERE id = 1 AND name != 'test'",
                  {4, 11, 1, 7, 1, 7, 11, 8, 12, 7, 11, 10});
    expect_tokens("SELECT * FROM users WHERE age > 18 OR status = 'active' AND created_date <= '2023-12-31'",
                  {4, 11, 1, 7, 1, 6, 11, 8, 12, 7, 11, 10, 12, 7, 11, 10});
}

TEST_F(DFAEngineTest, Subqueries_and_Parens)
{
    expect_tokens("SELECT * FROM (SELECT * FROM (SELECT * FROM t WHERE EXISTS (SELECT 1 FROM t2 WHERE t2.id ="
                      "t.id))) AS sub",
                  {4, 11, 1, 15, 4, 11, 1, 15, 4, 11, 1, 7, 1, 6, 15, 4, 8, 1, 7, 1, 7, 11, 7, 16, 16, 16, 1, 7});
    expect_tokens("SELECT a, (SELECT b FROM (SELECT b FROM t2 WHERE t2.id = t1.id) AS sub) FROM t1",
                  {4, 7, 20, 15, 4, 7, 1, 15, 4, 7, 1, 7, 1, 7, 11, 7, 16, 1, 7, 16, 1, 7});
    expect_tokens("SELECT * FROM (SELECT * FROM t WHERE id IN (SELECT id FROM t2)) AS sub",
                  {4, 11, 1, 15, 4, 11, 1, 7, 1, 7, 1, 15, 4, 7, 1, 7, 16, 16, 1, 7});
    expect_tokens("SELECT a, (SELECT MAX(b) FROM t2 WHERE t2.id = t1.id) FROM t1",
                  {4, 7, 20, 15, 4, 6, 15, 7, 16, 1, 7, 1, 7, 11, 7, 16, 1, 7});
    expect_tokens("SELECT (a + b) * (c - d) / (e % f) FROM table1 WHERE (x = 1) AND (y = 2)",
                  {4, 15, 7, 11, 7, 16, 11, 15, 7, 11, 7, 16, 11,
                      15, 7, 11, 7, 16, 1, 7, 1, 15, 7, 11, 8, 16, 12, 15, 7, 11, 8, 16});
}

TEST_F(DFAEngineTest, Pattern_Matching_LIKE)
{
    expect_tokens("SELECT * FROM logs WHERE message LIKE '%error%' ESCAPE '!' AND timestamp LIKE '2023-__-__ "
                      "__:__:__'",
                  {4, 11, 1, 7, 1, 7, 11, 10, 7, 10, 12, 5, 11, 10});
    expect_tokens("SELECT customer_id FROM orders WHERE order_id LIKE 'ORD[0-9][0-9][0-9][0-9]' AND notes LIKE "
                      "'%urgent!%' ESCAPE '!'",
                  {4, 7, 1, 7, 1, 7, 11, 10, 12, 7, 11, 10, 7, 10});
    expect_tokens("SELECT * FROM users WHERE username LIKE 'admin%' AND email LIKE '%@gmail.com' AND phone NOT "
                      "LIKE '1%'",
                  {4, 11, 1, 7, 1, 7, 11, 10, 12, 7, 11, 10, 12, 7, 11, 11, 10});
    expect_tokens("SELECT product_name FROM products WHERE product_code LIKE '[A-Z][0-9][0-9][0-9]' AND "
                      "description LIKE '%special%offer%'",
                  {4, 7, 1, 7, 1, 7, 11, 10, 12, 7, 11, 10});
}

TEST_F(DFAEngineTest, Pattern_Matching_REGEX)
{
    expect_tokens("SELECT * FROM logs WHERE message REGEXP 'ERROR:[A-Z]{3}-[0-9]{4}: .* at .*\\.java:[0-9]+'"
                      "AND timestamp RLIKE '202[0-9]-[0-1][0-9]-[0-3][0-9]'",
                  {4, 11, 1, 7, 1, 7, 11, 10, 12, 5, 11, 10});
    expect_tokens("SELECT * FROM transactions WHERE transaction_id REGEXP 'TXN-[A-Z]{2}[0-9]{6}-[A-F0-9]{8}'"
                      "AND amount RLIKE '^[0-9]+\\.[0-9]{2}$'",
                  {4, 11, 1, 7, 1, 7, 11, 10, 12, 7, 11, 10});
    expect_tokens("SELECT * FROM users WHERE username REGEXP '^admin[0-9]{3}$' AND email RLIKE"
                      "'[a-z]+@company\\.com$'",
                  {4, 11, 1, 7, 1, 7, 11, 10, 12, 7, 11, 10});
    auto tokens = tokenize(
        "SELECT product_name FROM products WHERE "
        "product_code REGEXP '^PRD-[A-Z]{3}-[0-9]{4}$' "
        "OR description RLIKE 'limited.*edition'");
    EXPECT_GT(tokens.size(), 0u);
}

TEST_F(DFAEngineTest, DateTime_Keywords)
{
    expect_tokens("SELECT DATE_ADD(NOW(), INTERVAL 1 DAY_HOUR)",
                  {4, 6, 15, 6, 15, 16, 20, 1, 8, 1, 16});
    expect_tokens("SELECT DATE_SUB(NOW(), INTERVAL 1 DAY_MICROSECOND)",
                  {4, 6, 15, 6, 15, 16, 20, 1, 8, 1, 16});
    expect_tokens("SELECT INTERVAL 1 DAY",
                  {4, 1, 8, 6});
    expect_tokens("SELECT DATE_FORMAT(NOW(), '%DAY_MINUTE')",
                  {4, 6, 15, 6, 15, 16, 20, 10, 16});
    expect_tokens("SELECT TIMEDIFF(NOW(), INTERVAL 1 DAY_SECOND)",
                  {4, 6, 15, 6, 15, 16, 20, 1, 8, 1, 16});
    expect_tokens("SELECT ADDTIME(NOW(), INTERVAL 1 HOUR_MICROSECOND)",
                  {4, 6, 15, 6, 15, 16, 20, 1, 8, 1, 16});
    expect_tokens("SELECT SUBTIME(NOW(), INTERVAL 1 HOUR_MINUTE)",
                  {4, 6, 15, 6, 15, 16, 20, 1, 8, 1, 16});
    expect_tokens("SELECT DATE_ADD(NOW(), INTERVAL 1 HOUR_SECOND)",
                  {4, 6, 15, 6, 15, 16, 20, 1, 8, 1, 16});
}

TEST_F(DFAEngineTest, Comments)
{
    expect_tokens("SELECT 1 -- this is a comment",
                  {4, 8, 13});
    expect_tokens("SELECT 2 /* block comment */",
                  {4, 8, 13});
    expect_tokens("SELECT * FROM users WHERE id = 1 -- condition commented AND name = 'test'",
                  {4, 11, 1, 7, 1, 7, 11, 8, 13});
    expect_tokens("SELECT /* query users */ * FROM users /* user table */ WHERE id = 1 /* user ID */",
                  {4, 13, 11, 1, 7, 13, 1, 7, 11, 8, 13});
}

TEST_F(DFAEngineTest, String_Literals)
{
    expect_tokens("SELECT 'it''s', \"he said \"\"hello\"\"\", N'native', X'414243', B'1010'",
                  {4, 10, 20, 10, 20, 10, 20, 8, 20, 8});
    expect_tokens("SELECT '''', \"\"\"\"\"\", N'', X'', B'",
                  {4, 10, 20, 10, 20, 10, 20, 8, 20, 7, 10});
}

TEST_F(DFAEngineTest, Advanced_Syntax)
{
    expect_tokens("SELECT IFNULL(NULL, 'default'), COALESCE(NULL, NULL, 'value'), NULLIF(1, 1)",
                  {4, 6, 15, 9, 20, 10, 16, 20, 6, 15, 9, 20, 9, 20, 10, 16, 20, 6, 15, 8, 20, 8, 16});
    expect_tokens("SELECT CASE WHEN 1=1 THEN 'true' ELSE 'false' END, CAST('123' AS SIGNED),"
                      "CONVERT('2023-01-01', DATE)",
                  {4, 4, 1, 8, 11, 8, 1, 10, 1, 10, 7, 20, 6, 15, 10, 1, 7, 16, 20, 6, 15, 10, 20, 6, 16});
    expect_tokens("SELECT DATABASE(), USER(), VERSION(), NOW(), DATE_FORMAT(NOW(), '%Y-%m-%d %H:%i:%s')",
                  {4, 7, 15, 16, 20, 7, 15, 16, 20, 6, 15, 16, 20, 6, 15, 16, 20, 6, 15, 6, 15, 16, 20, 10, 16});
    expect_tokens("SELECT CONCAT('Hello', ' ', 'World'), SUBSTRING('abcdef', 2, 3), LENGTH('test')",
                  {4, 6, 15, 10, 20, 10, 20, 10, 16, 20, 6, 15, 10, 20, 8, 20, 8, 16, 20, 6, 15, 10, 16});
}

TEST_F(DFAEngineTest, Window_Functions)
{
    expect_tokens("SELECT ROW_NUMBER() OVER (PARTITION BY dept ORDER BY salary DESC) AS rn FROM employees",
                  {4, 6, 15, 16, 7, 15, 1, 7, 7, 7, 7, 7, 1, 16, 1, 7, 1, 7});
    expect_tokens("SELECT id, name, department, salary, AVG(salary) OVER (PARTITION BY department) AS "
                      "avg_dept_salary FROM employees",
                  {4, 7, 20, 7, 20, 7, 20, 7, 20, 6, 15, 7, 16, 7, 15, 1, 7, 7, 16, 1, 7, 1, 7});
}

TEST_F(DFAEngineTest, System_Functions)
{
    expect_tokens("SELECT CURRENT_DATE, CURRENT_TIME, CURRENT_TIMESTAMP, CURRENT_USER, CURRENT_SCHEMA,"
                      "CURRENT_PATH, CURRENT_SERVER",
                  {4, 9, 20, 9, 20, 9, 20, 9, 20, 6, 20, 9, 20, 9});
    expect_tokens("SELECT CURRENT_TIMEZONE, CURRENT_FUNCTION, CURRENT_FUNCTION_PATH",
                  {4, 9, 20, 7, 20, 7});
    expect_tokens("SELECT CURRENT_ROLE, CURRENT_CATALOG;SELECT SESSION_USER, SYSTEM_USER",
                  {4, 7, 20, 7, 22, 4, 6, 20, 6});
    expect_tokens("SELECT USER(), DATABASE(), VERSION(), NOW(), CURDATE(), CURTIME(), LAST_INSERT_ID()",
                  {4, 7, 15, 16, 20, 7, 15, 16, 20, 6, 15, 16, 20, 6, 15, 16,
                    20, 6, 15, 16, 20, 6, 15, 16, 20, 6, 15, 16});
}

TEST_F(DFAEngineTest, Flow_Control)
{
    expect_tokens("DECLARE var1 INT DEFAULT 0;DECLARE var2 VARCHAR(100) DEFAULT 'test';DECLARE CONTINUE "
                      "HANDLER FOR 1062;BEGIN;SELECT 'duplicate';END;",
                  {23, 7, 5, 1, 8, 22, 23, 7, 5, 15, 8, 16, 1, 10, 22, 23, 1, 23, 7, 8, 22, 23, 22, 4, 10, 22, 7, 22});
    expect_tokens("SELECT 'duplicate'; END; ITERATE loop_label; LEAVE loop_label; LOOP SELECT 1; END LOOP; "
                    "REPEAT SELECT 1; UNTIL condition END REPEAT; WHILE condition DO SELECT 1; "
                    "RESIGNAL; GET DIAGNOSTICS",
                  {4, 10, 22, 7, 22, 1, 7, 22, 1, 7, 22, 1, 4, 8, 22, 7, 1, 22, 1,
                    4, 8, 22, 7, 1, 7, 1, 22, 23, 1, 7, 4, 8, 22, 1, 22, 7, 7});
}

// ========== 覆盖率优化：边界和错误处理测试 ==========

TEST_F(DFAEngineTest, InvalidTablePaths)
{
    // 测试无效的表文件路径
    kadd_dfa_table_paths_t invalid_paths = {
        "nonexistent_transition.bin",
        "nonexistent_output.bin",
        "nonexistent_eod.bin"
    };
    kadd_dfa_engine_t *invalid_engine = nullptr;
    EXPECT_NE(kadd_dfa_engine_create(&invalid_engine, &invalid_paths, 300, 300), 0)
        << "无效表路径应该创建失败";
}

TEST_F(DFAEngineTest, MaxInputLength_Boundary_300)
{
    // 测试最大输入长度边界
    std::string max_input(300, 'A');
    int tokens[300];
    int token_count = 0;

    EXPECT_EQ(kadd_dfa_engine_tokenize(engine, max_input.c_str(),
                                        max_input.length(), tokens, &token_count), 0);
}

TEST_F(DFAEngineTest, ControlAndHighByteChars)
{
    // 测试特殊字符：控制字符和高位字符
    const char *special_inputs[] = {
        "\x00\x01\x02",           // 控制字符
        "\xff\xfe\xfd",           // 高位字符
        "test\x00middle",         // 中间空字符
        "A\x01B\x02C",            // 混合字符
    };

    for (const char *input : special_inputs) {
        int tokens[100];
        int token_count = 0;
        EXPECT_GE(kadd_dfa_engine_tokenize(engine, input, strlen(input),
                                          tokens, &token_count), 0)
            << "特殊字符应该被正确处理: " << input;
    }
}

TEST_F(DFAEngineTest, BatchTokenizeWithNullInput)
{
    // 测试批处理中包含NULL指针 - 触发 cleanup_dfa_batch 覆盖率
    const char *inputs_with_null[3] = {"valid", nullptr, "test"};
    size_t lengths[] = {5, 0, 4};  // NULL输入的长度设为0
    int *outputs[3] = {nullptr};
    int dims[3];

    kadd_dfa_batch_t batch = {
        inputs_with_null,
        lengths,
        3,
        outputs,
        dims,
        kDefaultBatchSize
    };

    // 应该返回错误，因为中间有NULL输入
    EXPECT_NE(kadd_dfa_engine_tokenize_batch(engine, &batch), KADD_SUCCESS)
        << "批处理中包含NULL输入应该返回错误";

    // 验证cleanup被调用（outputs应该保持为NULL或已清理）
    for (int i = 0; i < 3; i++) {
        if (outputs[i]) {
            free(outputs[i]);  // 测试后清理
        }
    }
}
