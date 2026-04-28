/**
 * @file dfa_engine_pro_test.c
 * @brief DFA引擎Pro版本测试 - 基于dfa_engine.py的test_sections
 *
 * 测试用例与libinjection完全对齐
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include "../include/kadd_dfa_engine_pro.h"
#include "libinjection_sqli.h"

/* ========== 测试数据定义 ========== */

typedef struct {
    const char *section_name;
    const char **test_inputs;
    size_t test_count;
} TestSection;

/* 所有测试用例 - 来自dfa_engine.py的test_sections */
static const char *section_10_1[] = {
    "a   b", "SELECT\nFROM",
};

static const char *section_10_2[] = {
    "a % b", "x + y", "a ^ b", "~x",
};

static const char *section_10_3[] = {
    "a != b", "x && y", "i || j", "a << b", "a >> b",
    "a <= b", "a >= b", "a <> b", "a == b", "a <=> b", "!!", "::",
};

static const char *section_10_4[] = {
    "(a + b)", "func(a, b)", "BEGIN; END;", "{a: b}",
};

static const char *section_10_5[] = {
    "?param", "a]",
};

static const char *section_10_6[] = {
    "-- comment\nSELECT", "# comment\nSELECT",
};

static const char *section_10_7[] = {
    "a - b", "-- comment\na",
};

static const char *section_10_8[] = {
    "a / b", "/* comment */", "/*/",
};

static const char *section_10_9[] = {
    "\\N", "\\",
};

static const char *section_10_10[] = {
    "# MySQL comment",
};

static const char *section_10_11[] = {
    "$", "$100", "$1,000.00", "$$string$$",
};

static const char *section_10_12[] = {
    "0", "123", "3.14", "0xFF", "0b10101", "1.5e10", "123f",
};

static const char *section_10_13[] = {
    "'hello'", "\"world\"", "''", "'it''s'",
};

static const char *section_10_14[] = {
    "SELECT", "FROM", "WHERE", "identifier", "_underscore",
};

static const char *section_10_15[] = {
    "`table`", "`SELECT`", "`COUNT`(*)",
};

static const char *section_10_16[] = {
    "@variable", "@@global", "@`var`",
};

static const char *section_10_17[] = {
    "B'10101'", "b'10101'",
};

static const char *section_10_18[] = {
    "X'FFA2'", "x'FFA2'",
};

static const char *section_10_19[] = {
    "E'escaped\\nstring'", "e'escaped\\nstring'",
};

static const char *section_10_20[] = {
    "U&'unicode string'", "u&'unicode string'",
};

static const char *section_10_21[] = {
    "q'(string)'", "q'[string]'", "q'{string}'", "q'<string>'",
};

static const char *section_10_22[] = {
    "N'string'", "n'string'",
};

static const char *section_10_23[] = {
    "[identifier]", "[identifier with spaces]",
};

static const char *section_comprehensive[] = {
    "passwd=1024 or 1=1",
    "SELECT * FROM users WHERE id=1",
    "'admin' OR '1'='1'",
    "'; DROP TABLE users; --",
    "1' UNION SELECT 1,2,3--",
};

static const char *section_advanced[] = {
    /* SQL 注入模式 */
    "1' OR '1'='1'--",
    "admin' UNION SELECT NULL,password,NULL FROM users WHERE username='admin'--",
    "1'; DROP TABLE users; --",
    "1' AND 1=1--",
    "' OR 1=1#",
    "1' OR 1=1--",
    "admin'--",
    "' UNION SELECT 1,version(),3--",
    "1' AND (SELECT COUNT(*) FROM users) > 0--",
    "'; EXEC xp_cmdshell('dir');--",

    /* 实际 SQL 语句 */
    "SELECT * FROM users WHERE id=1 AND status='active'",
    "SELECT u.name, COUNT(o.id) as order_count FROM users u JOIN orders o ON u.id=o.user_id GROUP BY u.name",
    "INSERT INTO users (name,email,created_at) VALUES ('test@example.com','password123',NOW())",
    "UPDATE products SET price=price*1.1, updated_at=NOW() WHERE id=100",
    "DELETE FROM logs WHERE created_at < DATE_SUB(NOW(), INTERVAL 30 DAY)",
    "SELECT N'unicode string', E'escaped\\nstring' FROM table1",

    /* 类型转换和特殊字符串 */
    "SELECT U&'\\0041\\0042' as unicode, B'1010' as binary FROM table1",
    "SELECT n'national string', e'\\x41\\x42', x'FF' FROM table1",
    "SELECT q'[bracket string]', q'{brace string}', q'(paren string)' FROM table1",

    /* 运算符组合 */
    "x && y || z ! w & a | b ^ c << e >> f",
    "a == b != c <> d <= e >= f <=> g",
    "a::text || b::int || c::date",
    "col1 % 2 = 0 AND col2 BETWEEN 1 AND 100",
    "value1 IN (1,2,3) AND value2 NOT IN (4,5,6)",

    /* 复杂查询 */
    "SELECT * FROM (SELECT a,b FROM t1 WHERE x>0) AS subq",
    "CASE WHEN type=1 THEN 'one' WHEN type=2 THEN 'two' ELSE 'other' END",
    "COALESCE(NULLIF(a,b),c,d,e) as result",
    "SELECT CAST('123' AS UNSIGNED), CONVERT(varchar, 123)",
    "SELECT JSON_EXTRACT(data, '$.user.name') FROM users",

    /* 注释测试 */
    "SELECT * FROM users -- 这是单行注释\nWHERE id=1",
    "/* 这是多行注释 */\nSELECT * FROM table",
    "# MySQL 注释风格\nSELECT 1",

    /* 变量和函数 */
    "@@global.sql_mode, @session_var, @user_var",
    "SELECT CONCAT_WS(':', name, email), UPPER(name) FROM users",
    "SELECT IFNULL(value, 'default'), IF(cond, a, b) FROM table",
    "SELECT NOW(), CURRENT_DATE(), CURRENT_TIME FROM dual",
    "DATE_ADD('2024-01-01', INTERVAL 1 YEAR), TIMESTAMPDIFF(SECOND, start_time, end_time)",
};

static const char *section_web_uri[] = {
    "api.php?action=get);SELECT PG_SLEEP(5)--&table=user_profiles&id=1",
    "api.php?action=get));SELECT PG_SLEEP(5)--&table=user_profiles&id=1",
    "api.php?action=get;SELECT PG_SLEEP(5)--&table=user_profiles&id=1",
    "api.php?action=get);WAITFOR DELAY '0:0:5'--&table=user_profiles&id=1",
    "api.php?action=get));WAITFOR DELAY '0:0:5'--&table=user_profiles&id=1",
    "api.php?action=get;WAITFOR DELAY '0:0:5'--&table=user_profiles&id=1",
    "api.php?action=get);DECLARE @x CHAR(9);SET @x=0x303a303a35;WAITFOR DELAY @x--&table=user_profiles&id=1",
    "api.php?action=get));DECLARE @x CHAR(9);SET @x=0x303a303a35;WAITFOR DELAY @x--&table=user_profiles&id=1",
    "api.php?action=get;DECLARE @x CHAR(9);SET @x=0x303a303a35;WAITFOR DELAY @x--&table=user_profiles&id=1",
    "api.php?action=get);SELECT DBMS_PIPE(CHR(121)||CHR(83)||CHR(78)||CHR(70),5) FROM DUAL--&table=user_profiles&id=1",
    "index.php?table=user_profiles&id=1\")));SELECT SLEEP(5) AND (((\"eQVc\"=\"eQVc",
    "search.php?q=test\")));(SELECT * FROM (SELECT(SLEEP(5)))mwZE) AND (((\"taNc\" LIKE \"taNc",
    "api.php?action=get);SELECT DBMS_PIPE.RECEIVE_MESSAGE(CHR(82)||CHR(102)||CHR(121)||CHR(71),5) FROM DUAL--&table=user_profiles&id=1",
    "index.php?table=products&id=1';SELECT SLEEP(5) AND 'notB'='notB",
    "index.php?table=articles&id=1';SELECT SLEEP(5) AND 'NOtr'='NOtr",
    "index.php?table=products&id=51&ref=search",
    "jenkins/job/Ksim_CI/wfapi/runs?since=#289&fullStages=true&_=1767144313183",
    "api.php?action=get&table=user_profiles\") AS AyWa WHERE 8450=8450;SELECT SLEEP(5)-- mFBr&id=1",
    "index.php?table=user_profiles(',)....\",&id=1",
    "index.php?table=user_profiles'iWoioD<'\">HOvEOc&id=1",
    "index.php?table=user_profiles\")) AS OPBO WHERE 9345=9345;SELECT SLEEP(5)#&id=1",
    "api.php?action=get&table=user_profiles`=`user_profiles`;SELECT SLEEP(5)#&id=1",
    "index.php?table=user_profiles` WHERE 8358=8358;SELECT SLEEP(5)#&id=1",
    "api.php?action=get&table=user_profiles` WHERE 3424=3424;SELECT SLEEP(5)-- LTup&id=1",
    "api.php?action=get&table=user_profiles')) AS mpsf WHERE 1708=1708;SELECT SLEEP(5)#&id=1",
    "api.php?action=get');SELECT DBMS_PIPE.RECEIVE_MESSAGE(CHR(75)||CHR(74)||CHR(66)||CHR(76),5) FROM DUAL--&table=user_profiles&id=1",
    "api.php?action=get);SELECT DBMS_PIPE.RECEIVE_MESSAGE(CHR(121)||CHR(83)||CHR(78)||CHR(70),5) FROM DUAL--&table=user_profiles&id=1",
    "api.php?action=get&table=user_profiles`=`user_profiles`;SELECT SLEEP(5)#&id=1",
    "api.php?action=get&table=user_profiles WHERE 1224=1224 UNION ALL SELECT NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,CONCAT(0x716a767871,(CASE WHEN ((SELECT super_priv FROM mysql.user WHERE user=0x77656275736572 LIMIT 0,1)=0x59) THEN 1 ELSE 0 END),0x71786a7871),NULL,NULL,NULL,NULL-- -&id=1"
};

/* 测试章节定义 */
static TestSection test_sections[] = {
    {"10.1 parse_white - 空白字符处理", section_10_1, sizeof(section_10_1) / sizeof(section_10_1[0])},
    {"10.2 parse_operator1 - 单字符运算符", section_10_2, sizeof(section_10_2) / sizeof(section_10_2[0])},
    {"10.3 parse_operator2 - 双字符运算符", section_10_3, sizeof(section_10_3) / sizeof(section_10_3[0])},
    {"10.4 parse_char - 单字符标点", section_10_4, sizeof(section_10_4) / sizeof(section_10_4[0])},
    {"10.5 parse_other - 未知字符", section_10_5, sizeof(section_10_5) / sizeof(section_10_5[0])},
    {"10.6 parse_eol_comment - 行尾注释", section_10_6, sizeof(section_10_6) / sizeof(section_10_6[0])},
    {"10.7 parse_dash - 减号和注释", section_10_7, sizeof(section_10_7) / sizeof(section_10_7[0])},
    {"10.8 parse_slash - 斜杠和块注释", section_10_8, sizeof(section_10_8) / sizeof(section_10_8[0])},
    {"10.9 parse_backslash - 反斜杠", section_10_9, sizeof(section_10_9) / sizeof(section_10_9[0])},
    {"10.10 parse_hash - 井号", section_10_10, sizeof(section_10_10) / sizeof(section_10_10[0])},
    {"10.11 parse_money - 货币符号", section_10_11, sizeof(section_10_11) / sizeof(section_10_11[0])},
    {"10.12 parse_number - 数字", section_10_12, sizeof(section_10_12) / sizeof(section_10_12[0])},
    {"10.13 parse_string - 字符串", section_10_13, sizeof(section_10_13) / sizeof(section_10_13[0])},
    {"10.14 parse_word - 关键字和标识符", section_10_14, sizeof(section_10_14) / sizeof(section_10_14[0])},
    {"10.15 parse_tick - 反引号", section_10_15, sizeof(section_10_15) / sizeof(section_10_15[0])},
    {"10.16 parse_var - 变量", section_10_16, sizeof(section_10_16) / sizeof(section_10_16[0])},
    {"10.17 parse_bstring - 二进制字符串", section_10_17, sizeof(section_10_17) / sizeof(section_10_17[0])},
    {"10.18 parse_xstring - 十六进制字符串", section_10_18, sizeof(section_10_18) / sizeof(section_10_18[0])},
    {"10.19 parse_estring - 转义字符串", section_10_19, sizeof(section_10_19) / sizeof(section_10_19[0])},
    {"10.20 parse_ustring - Unicode字符串", section_10_20, sizeof(section_10_20) / sizeof(section_10_20[0])},
    {"10.21 parse_qstring - 引用字符串", section_10_21, sizeof(section_10_21) / sizeof(section_10_21[0])},
    {"10.22 parse_nqstring - 国家字符集字符串", section_10_22, sizeof(section_10_22) / sizeof(section_10_22[0])},
    {"10.23 parse_bword - 方括号标识符", section_10_23, sizeof(section_10_23) / sizeof(section_10_23[0])},
    {"综合测试 - SQL注入模式", section_comprehensive, sizeof(section_comprehensive) / sizeof(section_comprehensive[0])},
    {"高级测试 - complex SQL patterns", section_advanced, sizeof(section_advanced) / sizeof(section_advanced[0])},
    {"Web/URI 测试", section_web_uri, sizeof(section_web_uri) / sizeof(section_web_uri[0])},
};

/* ========== libinjection 辅助函数 ========== */

/**
 * @brief 将libinjection的token类型映射到索引
 */
static int libinjection_type_to_index(char type) {
    switch (type) {
        case 0:     return 0;   /* TYPE_NONE */
        case 'k':   return 1;   /* TYPE_KEYWORD */
        case 'U':   return 2;   /* TYPE_UNION */
        case 'B':   return 3;   /* TYPE_GROUP */
        case 'E':   return 4;   /* TYPE_EXPRESSION */
        case 't':   return 5;   /* TYPE_SQLTYPE */
        case 'f':   return 6;   /* TYPE_FUNCTION */
        case 'n':   return 7;   /* TYPE_BAREWORD */
        case '1':   return 8;   /* TYPE_NUMBER */
        case 'v':   return 9;   /* TYPE_VARIABLE */
        case 's':   return 10;  /* TYPE_STRING */
        case 'o':   return 11;  /* TYPE_OPERATOR */
        case '&':   return 12;  /* TYPE_LOGIC_OPERATOR */
        case 'c':   return 13;  /* TYPE_COMMENT */
        case 'A':   return 14;  /* TYPE_COLLATE */
        case '(':   return 15;  /* TYPE_LEFTPARENS */
        case ')':   return 16;  /* TYPE_RIGHTPARENS */
        case '{':   return 17;  /* TYPE_LEFTBRACE */
        case '}':   return 18;  /* TYPE_RIGHTBRACE */
        case '.':   return 19;  /* TYPE_DOT */
        case ',':   return 20;  /* TYPE_COMMA */
        case ':':   return 21;  /* TYPE_COLON */
        case ';':   return 22;  /* TYPE_SEMICOLON */
        case 'T':   return 23;  /* TYPE_STATEMENT */
        case '?':   return 24;  /* TYPE_UNKNOWN */
        case 'X':   return 25;  /* TYPE_EVIL */
        case 'F':   return 26;  /* TYPE_FINGERPRINT */
        case '\\':  return 27;  /* TYPE_BACKSLASH */
        default:  return 24;  /* TYPE_UNKNOWN */
    }
}

/**
 * @brief 使用libinjection对字符串进行分词（作为期望结果）
 */
static int *libinjection_tokenize_to_indices(const char *s, size_t *count) {
    if (!s || !count) return NULL;

    size_t input_len = strlen(s);
    int temp_output[8192];
    int temp_dim = 0;

    struct libinjection_sqli_state sf;
    libinjection_sqli_init(&sf, s, input_len, FLAG_QUOTE_NONE | FLAG_SQL_MYSQL);

    while (libinjection_sqli_tokenize(&sf) && temp_dim < 8192) {
        char token_type = sf.current->type;
        temp_output[temp_dim++] = libinjection_type_to_index(token_type);
    }

    if (temp_dim <= 0) {
        *count = 0;
        return NULL;
    }

    int *result = malloc(temp_dim * sizeof(int));
    if (!result) {
        *count = 0;
        return NULL;
    }

    memcpy(result, temp_output, temp_dim * sizeof(int));
    *count = (size_t)temp_dim;
    return result;
}

/* ========== 测试函数 ========== */

/**
 * @brief 比较两个token数组
 */
static int compare_tokens(const int *actual, int actual_count,
                         const int *expected, int expected_count) {
    if (actual_count != expected_count) {
        return 0;
    }

    for (int i = 0; i < actual_count; i++) {
        if (actual[i] != expected[i]) {
            return 0;
        }
    }

    return 1;
}

/**
 * @brief 运行单个测试章节
 */
static int run_test_section(kadd_dfa_engine_pro_t *engine,
                           const char *section_name,
                           const char **test_inputs,
                           size_t test_count,
                           int verbose) {
    int passed = 0;
    int failed = 0;

    printf("\n%s\n", section_name);
    printf("%s\n", "----------------------------------------------------------------------------------");

    for (size_t i = 0; i < test_count; i++) {
        const char *test = test_inputs[i];

        /* 显示输入（转义换行符） */
        char test_display[512];
        size_t j = 0;
        for (size_t k = 0; test[k] && j < sizeof(test_display) - 1; k++) {
            if (test[k] == '\n') {
                test_display[j++] = '\\';
                test_display[j++] = 'n';
            } else {
                test_display[j++] = test[k];
            }
        }
        test_display[j] = '\0';

        if (verbose) {
            printf("\n输入: '%s'\n", test_display);
        }

        /* 使用 DFA 引擎分词 */
        size_t dfa_count = 0;
        int *dfa_tokens = NULL;
        int dfa_output[8192];
        int dfa_dim = 0;

        if (kadd_dfa_engine_pro_tokenize(engine, test, strlen(test),
                                        dfa_output, &dfa_dim) == 0) {
            dfa_count = (size_t)dfa_dim;
            dfa_tokens = dfa_output;
        }

        /* 使用 libinjection 分词（期望结果） */
        size_t lib_count = 0;
        int *lib_tokens = libinjection_tokenize_to_indices(test, &lib_count);

        /* 比较结果 */
        int match = compare_tokens(dfa_tokens, (int)dfa_count,
                                  lib_tokens, (int)lib_count);

        if (match) {
            passed++;
            if (verbose) {
                printf("  ✓ 通过 - Token类型索引: [");
                for (size_t j = 0; j < dfa_count && j < 20; j++) {
                    printf("%d", dfa_tokens[j]);
                    if (j < dfa_count - 1 && j < 19) printf(", ");
                }
                if (dfa_count > 20) printf("...");
                printf("] (共%zu个)\n", dfa_count);
            }
        } else {
            failed++;
            printf("  ✗ 失败 - 输入: '%s'\n", test_display);
            printf("    期望 (libinjection): [");
            for (size_t j = 0; j < lib_count && j < 300; j++) {
                printf("%d", lib_tokens[j]);
                if (j < lib_count - 1 && j < 299) printf(", ");
            }
            printf("] (共%zu个)\n", lib_count);

            printf("    实际 (DFA):        [");
            for (size_t j = 0; j < dfa_count && j < 300; j++) {
                printf("%d", dfa_tokens[j]);
                if (j < dfa_count - 1 && j < 299) printf(", ");
            }
            printf("] (共%zu个)\n", dfa_count);
        }

        if (lib_tokens) free(lib_tokens);
    }

    printf("\n章节结果: 通过 %d/%d, 失败 %d/%d\n", passed, (int)test_count, failed, (int)test_count);

    return failed;
}

/* ========== 主函数 ========== */

int main(void) {
    /* 设置 Windows 控制台为 UTF-8 编码 */
    #ifdef _WIN32
    system("chcp 65001 > nul 2>&1");
    #endif

    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║              DFA 引擎 Pro 版本测试                          ║\n");
    printf("║         对比 libinjection (Expected) vs DFA (Actual)        ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");

    /* 创建 DFA 引擎 */
    kadd_dfa_engine_pro_t *engine = NULL;

    /* 尝试多个可能的路径 */
    const char *table_paths[] = {
        "../tools/profile_data/transition_table.bin",
        "C:/Users/60907/Desktop/KADD/tools/profile_data/transition_table.bin",
        NULL
    };

    const char *state_paths[] = {
        "../tools/profile_data/states_meta.json",
        "C:/Users/60907/Desktop/KADD/tools/profile_data/states_meta.json",
        NULL
    };

    int engine_created = 0;
    for (int i = 0; table_paths[i]; i++) {
        FILE *f = fopen(table_paths[i], "rb");
        if (f) {
            fclose(f);
            for (int j = 0; state_paths[j]; j++) {
                FILE *sf = fopen(state_paths[j], "r");
                if (sf) {
                    fclose(sf);
                    if (kadd_dfa_engine_pro_create(&engine, table_paths[i],
                                                     state_paths[j], 8192, 8192) == 0) {
                        engine_created = 1;
                        printf("\nDFA引擎创建成功\n");
                        printf("转移表: %s\n", table_paths[i]);
                        printf("元数据: %s\n", state_paths[j]);
                        break;
                    }
                }
            }
            if (engine_created) break;
        }
    }

    if (!engine_created || !engine) {
        printf("\n错误: 无法创建 DFA 引擎\n");
        return 1;
    }

    /* 运行所有测试 */
    int total_passed = 0;
    int total_failed = 0;
    int total_tests = 0;

    int verbose = 1;  /* 详细输出 */

    for (size_t i = 0; i < sizeof(test_sections) / sizeof(test_sections[0]); i++) {
        int failed = run_test_section(engine, test_sections[i].section_name,
                                     test_sections[i].test_inputs,
                                     test_sections[i].test_count,
                                     verbose);
        int passed = (int)test_sections[i].test_count - failed;
        total_passed += passed;
        total_failed += failed;
        total_tests += (int)test_sections[i].test_count;
    }

    /* 打印最终结果 */
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║                      测试结果汇总                           ║\n");
    printf("╠════════════════════════════════════════════════════════════╣\n");
    printf("║  总测试数:       %10d                                      ║\n", total_tests);
    printf("║  通过:           %10d                                      ║\n", total_passed);
    printf("║  失败:           %10d                                      ║\n", total_failed);
    if (total_tests > 0) {
        printf("║  通过率:         %10.2f%%                                  ║\n",
               (double)total_passed / total_tests * 100.0);
    }
    printf("╚════════════════════════════════════════════════════════════╝\n");

    /* 释放引擎 */
    kadd_dfa_engine_pro_free(engine);

    return (total_failed == 0) ? 0 : 1;
}
