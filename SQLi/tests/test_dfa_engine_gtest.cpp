/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *
 * @file test_dfa_engine_gtest.cpp
 * @brief DFA 引擎 Google Test 测试套件
 *
 * 包含功能测试、边界测试、异常测试、静态函数测试
 */

#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>
#include <cinttypes>
#include <unistd.h>
#include "kadd_dfa_engine.h"
#include "kadd_config_loader.h"

// ========== 包含源文件以测试 static 函数 ==========
#include "kadd_dfa_engine.c"

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
    kadd_dfa_table_paths_t table_paths;  // 成员变量

    void SetUp() override
    {
        // 从config.json加载配置
        ASSERT_EQ(kadd_config_load("config.json", &config), 0)
            << "无法加载config.json配置文件";

        // 使用配置文件中的参数
        table_paths = {
            config.transition_table,
            config.output_table,
            config.eod_table
        };
        ASSERT_EQ(kadd_dfa_engine_create(&engine, &table_paths,
                                         KADD_DFA_MAX_INPUT_CHARS,
                                         KADD_DFA_MAX_TOKENS), 0);
    }

    void TearDown() override
    {
        // 每个测试后释放引擎
        if (engine) {
            kadd_dfa_engine_free(&engine);
            engine = nullptr;
        }
    }

    // 辅助函数：执行分词并返回结果
    std::vector<int> tokenize(const std::string &input)
    {
        std::vector<int> tokens(KADD_DFA_MAX_TOKENS);
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
    std::string max_input(KADD_DFA_MAX_INPUT_CHARS, 'a');

    int tokens[KADD_DFA_MAX_TOKENS];
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
        config.transition_table,
        config.output_table,
        config.eod_table
    };
    int ret = kadd_dfa_engine_create(nullptr, &table_paths,
                                     KADD_DFA_MAX_INPUT_CHARS,
                                     KADD_DFA_MAX_TOKENS);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR) << "NULL engine 指针应该返回 KADD_ERROR_NULL_PTR";
}

TEST_F(DFAEngineTest, CreateWithNullTablePaths)
{
    kadd_dfa_engine_t *tmp = nullptr;
    int ret = kadd_dfa_engine_create(&tmp, nullptr,
                                     KADD_DFA_MAX_INPUT_CHARS,
                                     KADD_DFA_MAX_TOKENS);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR) << "NULL table_paths 应该返回 KADD_ERROR_NULL_PTR";
}

TEST_F(DFAEngineTest, CreateWithNullPathInTablePaths)
{
    kadd_dfa_table_paths_t bad_paths = {
        nullptr, config.output_table, config.eod_table
    };
    kadd_dfa_engine_t *tmp = nullptr;
    int ret = kadd_dfa_engine_create(&tmp, &bad_paths,
                                     KADD_DFA_MAX_INPUT_CHARS,
                                     KADD_DFA_MAX_TOKENS);
    EXPECT_NE(ret, 0) << "NULL transition_table_path 应该返回错误";
}

TEST_F(DFAEngineTest, ExceedsMaxInput)
{
    // 测试超过最大输入长度
    std::string too_long(KADD_DFA_MAX_INPUT_CHARS + 1, 'a');

    int tokens[KADD_DFA_MAX_TOKENS];
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

// ========== static 函数测试 ==========

/**
 * read_le32 函数测试 - 小端序读取
 */
TEST(DFAEngineStaticTest, ReadLE32_Basic)
{
    unsigned char bytes[4] = {0x78, 0x56, 0x34, 0x12};
    uint32_t result = read_le32(bytes);
    EXPECT_EQ(result, 0x12345678u);
}

TEST(DFAEngineStaticTest, ReadLE32_Zero)
{
    unsigned char bytes[4] = {0x00, 0x00, 0x00, 0x00};
    uint32_t result = read_le32(bytes);
    EXPECT_EQ(result, 0u);
}

TEST(DFAEngineStaticTest, ReadLE32_MaxValue)
{
    unsigned char bytes[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    uint32_t result = read_le32(bytes);
    EXPECT_EQ(result, 0xFFFFFFFFu);
}

/**
 * read_le16 函数测试 - 小端序读取
 */
TEST(DFAEngineStaticTest, ReadLE16_Basic)
{
    unsigned char bytes[2] = {0x34, 0x12};
    uint16_t result = read_le16(bytes);
    EXPECT_EQ(result, 0x1234u);
}

TEST(DFAEngineStaticTest, ReadLE16_Zero)
{
    unsigned char bytes[2] = {0x00, 0x00};
    uint16_t result = read_le16(bytes);
    EXPECT_EQ(result, 0u);
}

TEST(DFAEngineStaticTest, ReadLE16_MaxValue)
{
    unsigned char bytes[2] = {0xFF, 0xFF};
    uint16_t result = read_le16(bytes);
    EXPECT_EQ(result, 0xFFFFu);
}

/**
 * cleanup_dfa_batch 函数测试 - NULL 参数
 */
TEST(DFAEngineStaticTest, CleanupDfaBatch_NullBatch)
{
    // NULL batch 应该不崩溃
    cleanup_dfa_batch(nullptr, 0);
    SUCCEED() << "cleanup_dfa_batch(NULL) 不应崩溃";
}

TEST(DFAEngineStaticTest, CleanupDfaBatch_NullOutputs)
{
    kadd_dfa_batch_t batch = {0};
    batch.outputs = nullptr;
    batch.count = 5;

    // NULL outputs 应该不崩溃
    cleanup_dfa_batch(&batch, 3);
    SUCCEED() << "cleanup_dfa_batch with NULL outputs 不应崩溃";
}

/**
 * validate_table_header 函数测试 - 文件不存在
 */
TEST(DFAEngineStaticTest, ValidateTableHeader_FileNotFound)
{
    FILE *fp = nullptr;
    uint32_t states = 0;

    int result = validate_table_header("nonexistent_file.bin", 0, &fp, &states);
    EXPECT_EQ(result, KADD_DFA_ERROR_FILE_NOT_FOUND);
    EXPECT_EQ(fp, nullptr);
}

/**
 * load_u16_table 函数测试 - NULL 参数
 */
TEST(DFAEngineStaticTest, LoadU16Table_NullPath)
{
    uint16_t *table = nullptr;
    uint32_t num_states = 0;

    int result = load_u16_table(nullptr, 0, 256, &num_states, &table);
    EXPECT_NE(result, KADD_SUCCESS);
    EXPECT_EQ(table, nullptr);
}

TEST(DFAEngineStaticTest, LoadU16Table_NullOutputs)
{
    uint16_t *table = nullptr;
    uint32_t num_states = 0;

    int result = load_u16_table("test.bin", 0, 256, nullptr, &table);
    EXPECT_NE(result, KADD_SUCCESS);
}

TEST(DFAEngineStaticTest, LoadU16Table_NullTable)
{
    uint32_t num_states = 0;

    int result = load_u16_table("test.bin", 0, 256, &num_states, nullptr);
    EXPECT_NE(result, KADD_SUCCESS);
}

TEST(DFAEngineStaticTest, LoadU16Table_ZeroCellsPerState)
{
    uint16_t *table = nullptr;
    uint32_t num_states = 0;

    int result = load_u16_table("test.bin", 0, 0, &num_states, &table);
    EXPECT_NE(result, KADD_SUCCESS);
}

// ========== 批处理错误处理测试 ==========

TEST_F(DFAEngineTest, BatchTokenize_NullEngine)
{
    const char *inputs[2] = {"test1", "test2"};
    int *outputs[2] = {nullptr};
    int dims[2];

    kadd_dfa_batch_t batch = {
        inputs,
        nullptr,
        2,
        outputs,
        dims,
        10
    };

    int ret = kadd_dfa_engine_tokenize_batch(nullptr, &batch);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR);
}

TEST_F(DFAEngineTest, BatchTokenize_NullBatch)
{
    int ret = kadd_dfa_engine_tokenize_batch(engine, nullptr);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR);
}

TEST_F(DFAEngineTest, BatchTokenize_NullInputs)
{
    int *outputs[2] = {nullptr};
    int dims[2];

    kadd_dfa_batch_t batch = {
        nullptr,  // inputs = NULL
        nullptr,
        2,
        outputs,
        dims,
        10
    };

    int ret = kadd_dfa_engine_tokenize_batch(engine, &batch);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR);
}

TEST_F(DFAEngineTest, BatchTokenize_NullOutputs)
{
    const char *inputs[2] = {"test1", "test2"};
    int dims[2];

    kadd_dfa_batch_t batch = {
        inputs,
        nullptr,
        2,
        nullptr,  // outputs = NULL
        dims,
        10
    };

    int ret = kadd_dfa_engine_tokenize_batch(engine, &batch);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR);
}

TEST_F(DFAEngineTest, BatchTokenize_NullDims)
{
    const char *inputs[2] = {"test1", "test2"};
    int *outputs[2] = {nullptr};

    kadd_dfa_batch_t batch = {
        inputs,
        nullptr,
        2,
        outputs,
        nullptr,  // dims = NULL
        10
    };

    int ret = kadd_dfa_engine_tokenize_batch(engine, &batch);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR);
}

TEST_F(DFAEngineTest, BatchTokenize_ZeroCount)
{
    const char *inputs[1] = {"test"};
    int *outputs[1] = {nullptr};
    int dims[1];

    kadd_dfa_batch_t batch = {
        inputs,
        nullptr,
        0,  // count = 0
        outputs,
        dims,
        10
    };

    int ret = kadd_dfa_engine_tokenize_batch(engine, &batch);
    EXPECT_EQ(ret, KADD_ERROR_INVALID_INPUT);
}

TEST_F(DFAEngineTest, BatchTokenize_NegativeCount)
{
    const char *inputs[1] = {"test"};
    int *outputs[1] = {nullptr};
    int dims[1];

    kadd_dfa_batch_t batch = {
        inputs,
        nullptr,
        -1,  // count = -1
        outputs,
        dims,
        10
    };

    int ret = kadd_dfa_engine_tokenize_batch(engine, &batch);
    EXPECT_EQ(ret, KADD_ERROR_INVALID_INPUT);
}

TEST_F(DFAEngineTest, BatchTokenize_ExceedsMaxSampleCount)
{
    const char *inputs[1] = {"test"};
    int *outputs[1] = {nullptr};
    int dims[1];

    kadd_dfa_batch_t batch = {
        inputs,
        nullptr,
        KADD_DFA_MAX_SAMPLE_COUNT + 1,  // 超过限制
        outputs,
        dims,
        10
    };

    int ret = kadd_dfa_engine_tokenize_batch(engine, &batch);
    EXPECT_EQ(ret, KADD_ERROR_INVALID_PARAM);
}

// ========== cleanup_dfa_batch 测试 ==========

TEST_F(DFAEngineTest, CleanupDfaBatch_UpToIdx)
{
    // 创建包含有效指针的 outputs
    int *outputs[3] = {nullptr};
    outputs[0] = (int *)malloc(10 * sizeof(int));
    outputs[1] = (int *)malloc(10 * sizeof(int));

    kadd_dfa_batch_t batch;
    batch.outputs = outputs;
    batch.count = 3;

    // cleanup 应该只清理前 2 个
    cleanup_dfa_batch(&batch, 2);

    // 第三个应该保持不变（但我们无法直接验证，只是确保不崩溃）
}

// ========== kadd_dfa_engine_create 错误路径 ==========

TEST_F(DFAEngineTest, Create_InvalidMaxInputChars)
{
    kadd_dfa_table_paths_t table_paths = {
        config.transition_table,
        config.output_table,
        config.eod_table
    };
    kadd_dfa_engine_t *tmp = nullptr;

    // max_input_chars = 0 应该失败
    int ret = kadd_dfa_engine_create(&tmp, &table_paths, 0, 100);
    EXPECT_NE(ret, KADD_SUCCESS);
}

TEST_F(DFAEngineTest, Create_InvalidMaxTokens)
{
    kadd_dfa_table_paths_t table_paths = {
        config.transition_table,
        config.output_table,
        config.eod_table
    };
    kadd_dfa_engine_t *tmp = nullptr;

    // max_tokens = 0 应该失败
    int ret = kadd_dfa_engine_create(&tmp, &table_paths, 300, 0);
    EXPECT_NE(ret, KADD_SUCCESS);
}

TEST_F(DFAEngineTest, Create_EmptyPath)
{
    kadd_dfa_table_paths_t empty_paths = {
        "",  // 空路径
        config.output_table,
        config.eod_table
    };
    kadd_dfa_engine_t *tmp = nullptr;

    int ret = kadd_dfa_engine_create(&tmp, &empty_paths, 300, 300);
    EXPECT_NE(ret, KADD_SUCCESS);
}

TEST_F(DFAEngineTest, Free_Engine)
{
    // 1. 释放 NULL 指针应该安全
    EXPECT_NO_THROW(kadd_dfa_engine_free(nullptr));

    // 2. 释放 SetUp 中创建的有效引擎
    EXPECT_NO_THROW(kadd_dfa_engine_free(&engine));
    engine = nullptr;  // 避免 TearDown 再次释放

    // 3. 再次释放 NULL 指针仍然安全
    EXPECT_NO_THROW(kadd_dfa_engine_free(nullptr));
}

// ========== kadd_dfa_emit_token 边界测试 ==========

TEST_F(DFAEngineTest, EmitToken_BoundaryConditions)
{
    int output[10];
    size_t count, pos, token_start;
    uint16_t accept_info;
    const uint16_t *trans_row = engine->transition_table;
    const uint16_t *out_row = engine->output_table;

    // 场景1: BufferOverflow - count >= max_output_dim
    count = 1; pos = 0; token_start = 0; accept_info = 0;
    EXPECT_EQ(kadd_dfa_emit_token(1, 10, &pos, &token_start, &accept_info, output, &count,
                                  1, 0x0020, &trans_row, &out_row,
                                  engine->transition_table, engine->output_table, engine->num_states),  /* [FIX] */
              KADD_DFA_ERROR_BUFFER_OVERFLOW);

    // 场景2: ScanPosOverflow - pos + scan_delta(3) > input_length
    count = 0; pos = 8; token_start = 0; accept_info = 0;
    EXPECT_EQ(kadd_dfa_emit_token(10, 10, &pos, &token_start, &accept_info, output, &count,
                                  1, 0x0C20, &trans_row, &out_row,
                                  engine->transition_table, engine->output_table, engine->num_states),  /* [FIX] */
              KADD_DFA_ERROR_PARSE);

    // 场景3: StartDeltaUnderflow - start_delta=-1, pos=0
    count = 0; pos = 0; token_start = 0; accept_info = 0;
    EXPECT_EQ(kadd_dfa_emit_token(10, 10, &pos, &token_start, &accept_info, output, &count,
                                  1, 0x0020, &trans_row, &out_row,
                                  engine->transition_table, engine->output_table, engine->num_states),  /* [FIX] */
              KADD_DFA_ERROR_PARSE);

    // 场景4: TokenStartOverflow - token_start_pos > input_length
    count = 0; pos = 9; token_start = 0; accept_info = 0;
    EXPECT_EQ(kadd_dfa_emit_token(10, 10, &pos, &token_start, &accept_info, output, &count,
                                  1, 0x3020, &trans_row, &out_row,
                                  engine->transition_table, engine->output_table, engine->num_states),  /* [FIX] */
              KADD_DFA_ERROR_PARSE);
}

// ========== 覆盖批处理错误分支和 batch_size 边界 ==========

TEST_F(DFAEngineTest, BatchTokenize_ErrorBranches)
{
    // 1. 超长输入 -> result != SUCCESS
    std::string too_long(KADD_DFA_MAX_INPUT_CHARS + 100, 'a');
    const char *inputs1[2] = {"valid", too_long.c_str()};
    size_t lens1[2] = {5, too_long.length()};
    int *outs1[2] = {nullptr};
    int dims1[2];
    kadd_dfa_batch_t b1 = {inputs1, lens1, 2, outs1, dims1, 10};
    EXPECT_EQ(kadd_dfa_engine_tokenize_batch(engine, &b1), KADD_DFA_ERROR_PARSE);
    EXPECT_EQ(outs1[0], nullptr);  // cleanup 清空

    // 2. 全空白输入 -> token_count == 0
    const char *inputs2[2] = {"SELECT", "   \t\n   "};
    int *outs2[2] = {nullptr};
    int dims2[2];
    kadd_dfa_batch_t b2 = {inputs2, nullptr, 2, outs2, dims2, 10};
    EXPECT_EQ(kadd_dfa_engine_tokenize_batch(engine, &b2), KADD_DFA_ERROR_PARSE);
    EXPECT_EQ(outs2[0], nullptr);  // cleanup 清空
}

TEST_F(DFAEngineTest, BatchTokenize_BatchSizeBoundary)
{
    // 测试 batch_size 边界: 0/负数/>MAX 均回退默认值 64
    int batch_sizes[] = {0, -1, KADD_DFA_MAX_BATCH_SIZE + 100, KADD_DFA_MAX_BATCH_SIZE};

    for (int i = 0; i < 4; i++) {
        const char *inputs[2] = {"SELECT", "FROM"};
        int *outputs[2] = {nullptr};
        int dims[2];
        kadd_dfa_batch_t batch = {inputs, nullptr, 2, outputs, dims, batch_sizes[i]};

        EXPECT_EQ(kadd_dfa_engine_tokenize_batch(engine, &batch), KADD_SUCCESS)
            << "batch_size=" << batch_sizes[i] << " 应成功";
        EXPECT_NE(outputs[0], nullptr);
        EXPECT_NE(outputs[1], nullptr);

        for (int j = 0; j < 2; j++) free(outputs[j]);
    }
}


TEST(DFAEngineStaticTest, CleanupDfaBatch_WithActualOutputs)
{
    // 创建带有实际输出的 batch
    kadd_dfa_batch_t batch;
    batch.count = 3;
    batch.outputs = (int **)calloc(3, sizeof(int *));
    ASSERT_NE(batch.outputs, nullptr);

    for (int i = 0; i < 3; i++) {
        batch.outputs[i] = (int *)calloc(10, sizeof(int));
        ASSERT_NE(batch.outputs[i], nullptr);
        batch.outputs[i][0] = i * 100;
    }

    // 清理所有输出
    cleanup_dfa_batch(&batch, KADD_DFA_BATCH_CLEANUP_ALL);

    // 验证输出已被释放（置为 NULL）
    for (int i = 0; i < 3; i++) {
        EXPECT_EQ(batch.outputs[i], nullptr);
    }

    // 释放 outputs 数组本身
    free(batch.outputs);
}

TEST(DFAEngineStaticTest, CleanupDfaBatch_UpToIdxPartial)
{
    // 创建带有实际输出的 batch，只清理部分
    kadd_dfa_batch_t batch;
    batch.count = 5;
    batch.outputs = (int **)calloc(5, sizeof(int *));
    ASSERT_NE(batch.outputs, nullptr);

    for (int i = 0; i < 5; i++) {
        batch.outputs[i] = (int *)calloc(10, sizeof(int));
        ASSERT_NE(batch.outputs[i], nullptr);
    }

    // 只清理前 3 个
    cleanup_dfa_batch(&batch, 3);

    // 验证前 3 个已释放
    for (int i = 0; i < 3; i++) {
        EXPECT_EQ(batch.outputs[i], nullptr);
    }
    // 验证后 2 个未被释放
    for (int i = 3; i < 5; i++) {
        EXPECT_NE(batch.outputs[i], nullptr);
        free(batch.outputs[i]);
    }

    free(batch.outputs);
}

// ========== 覆盖率补充：未覆盖错误分支 / 静态函数分支 ==========

/**
 * 辅助：定位可用的 DFA 表文件（兼容 Linux 绝对路径 config 与 Windows 本地 assets）
 */
static bool find_dfa_tables(std::string &trans, std::string &out, std::string &eod)
{
    kadd_config_t config;
    if (kadd_config_load("config.json", &config) == KADD_SUCCESS) {
        structstat_check:
        if (access(config.transition_table, R_OK) == 0) {
            trans = config.transition_table;
            out = config.output_table;
            eod = config.eod_table;
            return true;
        }
    }
    trans = "assets/data/transition_table.bin";
    out = "assets/data/output_table.bin";
    eod = "assets/data/eod_table.bin";
    return access(trans.c_str(), R_OK) == 0;
}

/**
 * 辅助：创建真实表的引擎（兼容本地 assets 路径）
 */
static kadd_dfa_engine_t *create_real_engine()
{
    std::string trans, out, eod;
    if (!find_dfa_tables(trans, out, eod)) {
        return nullptr;
    }
    kadd_dfa_table_paths_t paths = {trans.c_str(), out.c_str(), eod.c_str()};
    kadd_dfa_engine_t *engine = nullptr;
    if (kadd_dfa_engine_create(&engine, &paths,
                               KADD_DFA_MAX_INPUT_CHARS, KADD_DFA_MAX_TOKENS) != KADD_SUCCESS) {
        return nullptr;
    }
    return engine;
}

/* ---------- cleanup_dfa_batch ---------- */

TEST(DFAEngineStaticTest, CleanupDfaBatch_MixedNulls)
{
    // 覆盖 cleanup_dfa_batch 的 if(batch->outputs[i]) false 分支
    kadd_dfa_batch_t batch;
    int **outputs = (int **)calloc(3, sizeof(int *));
    ASSERT_NE(outputs, nullptr);
    outputs[0] = (int *)calloc(10, sizeof(int));  // 有效指针
    outputs[1] = nullptr;                          // NULL
    outputs[2] = (int *)calloc(10, sizeof(int));  // 有效指针
    batch.outputs = outputs;
    batch.count = 3;

    cleanup_dfa_batch(&batch, KADD_DFA_BATCH_CLEANUP_ALL);
    EXPECT_EQ(outputs[0], nullptr);
    EXPECT_EQ(outputs[1], nullptr);
    EXPECT_EQ(outputs[2], nullptr);
    free(outputs);
}

/* ---------- validate_table_header 错误分支 ---------- */

TEST(DFAEngineStaticTest, ValidateTableHeader_ReadFail)
{
    // 文件小于 4 字节头 → 头读取失败
    const char *path = "dfa_test_bad_header_2b.bin";
    FILE *f = fopen(path, "wb");
    ASSERT_NE(f, nullptr);
    unsigned char buf[2] = {0x01, 0x02};
    ASSERT_EQ(fwrite(buf, 1, 2, f), 2u);
    fclose(f);

    FILE *fp = nullptr;
    uint32_t states = 0;
    int ret = validate_table_header(path, 0, &fp, &states);
    EXPECT_EQ(ret, KADD_DFA_ERROR_PARSE);
    EXPECT_EQ(fp, nullptr);
    unlink(path);
}

TEST(DFAEngineStaticTest, ValidateTableHeader_ZeroStates)
{
    // states = 0 → 非法状态数
    const char *path = "dfa_test_zero_states.bin";
    FILE *f = fopen(path, "wb");
    ASSERT_NE(f, nullptr);
    unsigned char buf[4] = {0, 0, 0, 0};  // LE32: states = 0
    ASSERT_EQ(fwrite(buf, 1, 4, f), 4u);
    fclose(f);

    FILE *fp = nullptr;
    uint32_t states = 0;
    int ret = validate_table_header(path, 0, &fp, &states);
    EXPECT_EQ(ret, KADD_DFA_ERROR_PARSE);
    EXPECT_EQ(fp, nullptr);
    unlink(path);
}

TEST(DFAEngineStaticTest, ValidateTableHeader_StatesMismatch)
{
    // states = 3 但期望 5 → 不匹配
    const char *path = "dfa_test_mismatch_states.bin";
    FILE *f = fopen(path, "wb");
    ASSERT_NE(f, nullptr);
    unsigned char buf[4] = {0x03, 0x00, 0x00, 0x00};  // LE32: states = 3
    ASSERT_EQ(fwrite(buf, 1, 4, f), 4u);
    fclose(f);

    FILE *fp = nullptr;
    uint32_t states = 0;
    int ret = validate_table_header(path, 5, &fp, &states);
    EXPECT_EQ(ret, KADD_DFA_ERROR_PARSE);
    EXPECT_EQ(fp, nullptr);
    unlink(path);
}

/* ---------- load_u16_table 错误分支 ---------- */

TEST(DFAEngineStaticTest, LoadU16Table_InvalidParams)
{
    // 参数无效：cells_per_state = 0
    uint16_t *table = nullptr;
    uint32_t num_states = 0;
    int ret = load_u16_table("test.bin", 0, 0, &num_states, &table);
    EXPECT_EQ(ret, KADD_ERROR_INVALID_INPUT);
    EXPECT_EQ(table, nullptr);
}

TEST(DFAEngineStaticTest, LoadU16Table_IncompleteData)
{
    // 合法头 states=1（需 256 单元 = 512 字节数据），但文件只有 4+100 字节
    const char *path = "dfa_test_incomplete.bin";
    FILE *f = fopen(path, "wb");
    ASSERT_NE(f, nullptr);
    unsigned char hdr[4] = {0x01, 0x00, 0x00, 0x00};  // LE32: states = 1
    ASSERT_EQ(fwrite(hdr, 1, 4, f), 4u);
    unsigned char data[100] = {0};
    ASSERT_EQ(fwrite(data, 1, sizeof(data), f), sizeof(data));
    fclose(f);

    uint16_t *table = nullptr;
    uint32_t num_states = 0;
    int ret = load_u16_table(path, 0, 256, &num_states, &table);
    EXPECT_EQ(ret, KADD_DFA_ERROR_PARSE);
    EXPECT_EQ(table, nullptr);
    unlink(path);
}

TEST(DFAEngineStaticTest, LoadTables_OutputTableFail)
{
    // transition 表用真实文件，output/eod 表用不存在的文件
    // → 覆盖 kadd_dfa_engine_load_tables 的 output/eod 加载失败分支
    std::string trans, out, eod;
    ASSERT_TRUE(find_dfa_tables(trans, out, eod));

    kadd_dfa_engine_t *engine = nullptr;
    kadd_dfa_table_paths_t bad_paths = {trans.c_str(), "no_such_output_table.bin", eod.c_str()};
    int ret = kadd_dfa_engine_create(&engine, &bad_paths,
                                     KADD_DFA_MAX_INPUT_CHARS, KADD_DFA_MAX_TOKENS);
    EXPECT_EQ(ret, KADD_DFA_ERROR_FILE_NOT_FOUND);
    EXPECT_EQ(engine, nullptr);
}

/* ---------- kadd_dfa_validate_input ---------- */

TEST(DFAEngineStaticTest, ValidateInput_NotInit)
{
    // 构造未初始化的引擎（三表 NULL）→ NOT_INIT
    kadd_dfa_engine_t e;
    memset(&e, 0, sizeof(e));
    e.max_input_chars = KADD_DFA_MAX_INPUT_CHARS;
    e.max_tokens = KADD_DFA_MAX_TOKENS;

    int *out = (int *)calloc(10, sizeof(int));
    ASSERT_NE(out, nullptr);
    int cnt = 0;
    int ret = kadd_dfa_validate_input(&e, "x", 1, out, &cnt);
    EXPECT_EQ(ret, KADD_ERROR_NOT_INIT);
    free(out);
}

/* ---------- kadd_dfa_handle_eod ---------- */

TEST(DFAEngineStaticTest, HandleEod_EmitTwo)
{
    // eod_code = action | (token1 << 2) | (token2 << 8)
    // EMIT_TWO(3) + first_token=1 + second_token=2
    uint16_t eod_table[1];
    eod_table[0] = 3 | (1u << 2) | (2u << 8);

    int *out = (int *)calloc(10, sizeof(int));
    ASSERT_NE(out, nullptr);
    out[0] = -1;
    out[1] = -1;
    size_t count = 0;
    size_t token_start = 0;
    uint16_t accept_info = 0;

    // 成功路径：空间足够
    int ret = kadd_dfa_handle_eod(eod_table, 0, 10, 5, &token_start, &accept_info, out, &count);
    EXPECT_EQ(ret, KADD_SUCCESS);
    EXPECT_EQ(count, 2u);
    EXPECT_EQ(out[0], 1);
    EXPECT_EQ(out[1], 2);
    EXPECT_EQ(accept_info, 0u);
    EXPECT_EQ(token_start, 5u);

    // BUFFER_OVERFLOW：count + 2 > max_output_dim
    count = 9;
    ret = kadd_dfa_handle_eod(eod_table, 0, 2, 5, &token_start, &accept_info, out, &count);
    EXPECT_EQ(ret, KADD_DFA_ERROR_BUFFER_OVERFLOW);
    EXPECT_EQ(count, 9u);
    free(out);
}

/* ---------- kadd_dfa_engine_tokenize 末尾 count >= max_tokens ---------- */

TEST_F(DFAEngineTest, Tokenize_BufferOverflow)
{
    // 将 max_tokens 压缩到 1：
    // 1) emit_token 内 count(1) >= max_output_dim(1) → BUFFER_OVERFLOW 分支
    // 2) 末尾 count >= engine->max_tokens 分支（若引擎表恰好提前结束）
    engine->max_tokens = 1;

    int tokens[KADD_DFA_MAX_TOKENS];
    int token_count = 0;
    int ret = kadd_dfa_engine_tokenize(engine, "SELECT FROM", 11, tokens, &token_count);
    EXPECT_EQ(ret, KADD_DFA_ERROR_BUFFER_OVERFLOW);
}

/* ---------- kadd_dfa_tokenize_batch_item 错误路径 ---------- */

TEST_F(DFAEngineTest, TokenizeBatchItem_NullInput)
{
    const char *inputs[1] = {nullptr};
    int *outputs[1] = {nullptr};
    int dims[1] = {0};
    kadd_dfa_batch_t batch = {inputs, nullptr, 1, outputs, dims, 10};

    int ret = kadd_dfa_tokenize_batch_item(engine, &batch, 0);
    EXPECT_EQ(ret, KADD_ERROR_NULL_PTR);
    EXPECT_EQ(outputs[0], nullptr);  // cleanup 后保持 NULL
}

TEST_F(DFAEngineTest, TokenizeBatchItem_TokenizeFail)
{
    // 超长输入（> max_input_chars）→ tokenize 返回 INVALID_LENGTH
    // → batch item 走 tokenize 失败分支，释放 output 并清理 batch
    std::string too_long(KADD_DFA_MAX_INPUT_CHARS + 1, 'a');
    const char *inputs[1] = {too_long.c_str()};
    int *outputs[1] = {nullptr};
    int dims[1] = {0};
    kadd_dfa_batch_t batch = {inputs, nullptr, 1, outputs, dims, 10};

    int ret = kadd_dfa_tokenize_batch_item(engine, &batch, 0);
    EXPECT_EQ(ret, KADD_DFA_ERROR_PARSE);
    EXPECT_EQ(outputs[0], nullptr);
}

/* ---------- 末尾 count >= max_tokens（真实表引擎，max_tokens=1） ---------- */
