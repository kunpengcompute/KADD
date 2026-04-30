/**
 * @file debug_dfa_engine.c
 * @brief DFA 引擎调试测试程序
 *
 * 用于对比 kadd_dfa_engine 与 libinjection 的 token 分词结果
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/kadd_dfa_engine.h"
#include "libinjection_sqli.h"

/* ========== 类型定义 ========== */

typedef struct {
    const char *input;
    const char *description;
} test_case_t;

typedef struct {
    int index;
    char type;
    char value[32];
} token_info_t;

/* ========== Token 类型名称映射 ========== */

static const char *token_type_name(int index) {
    static const char *names[] = {
        "NONE", "KEYWORD", "UNION", "GROUP", "EXPRESSION",
        "SQLTYPE", "FUNCTION", "BAREWORD", "NUMBER", "VARIABLE",
        "STRING", "OPERATOR", "LOGIC_OP", "COMMENT", "COLLATE",
        "LPAREN", "RPAREN", "LBRACE", "RBRACE", "DOT",
        "COMMA", "COLON", "SEMICOLON", "STATEMENT", "UNKNOWN",
        "EVIL", "FINGERPRINT", "BACKSLASH"
    };
    if (index >= 0 && index < 28) return names[index];
    return "UNKNOWN";
}

/* ========== libinjection 辅助函数 ========== */

/**
 * @brief 将 libinjection 的 token 类型字符映射到索引
 */
static int libinjection_type_to_index(char type) {
    static const int8_t map[256] = {
        [0 ... 255] = 24,  // 默认 TYPE_UNKNOWN
        ['0'] = 0,     ['k'] = 1,     ['U'] = 2,     ['B'] = 3,
        ['E'] = 4,     ['t'] = 5,     ['f'] = 6,     ['n'] = 7,
        ['1'] = 8,     ['v'] = 9,     ['s'] = 10,    ['o'] = 11,
        ['&'] = 12,    ['c'] = 13,    ['A'] = 14,    ['('] = 15,
        [')'] = 16,    ['{'] = 17,    ['}'] = 18,    ['.'] = 19,
        [','] = 20,    [':'] = 21,    [';'] = 22,    ['T'] = 23,
        ['?'] = 24,    ['X'] = 25,    ['F'] = 26,    ['\\'] = 27,
    };
    return (type >= 0 && type < 256) ? map[(unsigned char)type] : 24;
}

/**
 * @brief 使用 libinjection 进行分词，返回 token 详细信息
 */
static token_info_t *libinjection_tokenize(const char *s, int *count) {
    if (!s || !count) return NULL;

    size_t len = strlen(s);
    struct libinjection_sqli_state sf;
    libinjection_sqli_init(&sf, s, len, FLAG_QUOTE_NONE | FLAG_SQL_MYSQL);

    token_info_t *tokens = NULL;
    int capacity = 0;
    *count = 0;

    while (libinjection_sqli_tokenize(&sf)) {
        if (*count >= capacity) {
            capacity = capacity == 0 ? 64 : capacity * 2;
            token_info_t *new_tokens = realloc(tokens, capacity * sizeof(token_info_t));
            if (!new_tokens) {
                free(tokens);
                *count = 0;
                return NULL;
            }
            tokens = new_tokens;
        }
        tokens[*count].index = libinjection_type_to_index(sf.current->type);
        tokens[*count].type = sf.current->type;
        strncpy(tokens[*count].value, sf.current->val, 31);
        tokens[*count].value[31] = '\0';
        (*count)++;
    }

    return tokens;
}

/**
 * @brief 比较 DFA 和 libinjection 的分词结果
 */
static int compare_tokens(const int *dfa, int dfa_count, const token_info_t *lib, int lib_count) {
    if (dfa_count != lib_count) return 0;
    for (int i = 0; i < dfa_count; i++) {
        if (dfa[i] != lib[i].index) return 0;
    }
    return 1;
}

/* ========== 测试用例定义 ========== */

static const test_case_t test_cases[] = {
    /* 10.1 空白字符处理 */
    {"a   b", "空白字符 - 空格"},
    {"SELECT\nFROM", "空白字符 - 换行"},

    /* 10.2 单字符运算符 */
    {"a % b", "运算符 - %"},
    {"x + y", "运算符 - +"},
    {"a ^ b", "运算符 - ^"},
    {"~x", "运算符 - ~"},

    /* 10.3 双字符运算符 */
    {"a != b", "运算符 - !="},
    {"x && y", "运算符 - &&"},
    {"i || j", "运算符 - ||"},
    {"a << b", "运算符 - <<"},
    {"a >> b", "运算符 - >>"},
    {"a <= b", "运算符 - <="},
    {"a >= b", "运算符 - >="},
    {"a <> b", "运算符 - <>"},
    {"a == b", "运算符 - =="},
    {"a <=> b", "运算符 - <=>"},
    {"!!", "运算符 - !!"},
    {"a::b", "运算符 - ::"},

    /* 10.4 单字符标点 */
    {"(a + b)", "标点 - 括号"},
    {"func(a, b)", "标点 - 逗号"},
    {"BEGIN; END;", "标点 - 分号"},
    {"{a: b}", "标点 - 花括号冒号"},

    /* 10.5 未知字符 */
    {"?param", "未知 - ?"},
    {"a]", "未知 - ]"},

    /* 10.6 行尾注释 */
    {"-- comment\nSELECT", "注释 --"},
    {"# comment\nSELECT", "注释 #"},

    /* 10.7 减号和注释 */
    {"a - b", "减号"},
    {"-- comment\na", "注释 --"},

    /* 10.8 斜杠和块注释 */
    {"a / b", "除号"},
    {"/* comment */", "块注释 */"},
    {"/*/", "不完整块注释"},

    /* 10.9 反斜杠 */
    {"\\N", "反斜杠N"},
    {"\\", "单独反斜杠"},

    /* 10.10 井号 */
    {"# MySQL comment", "井号注释"},

    /* 10.11 货币符号 */
    {"$", "货币符号"},
    {"$100", "货币值"},
    {"$1,000.00", "格式化货币"},
    {"$$string$$", "美元符引用"},

    /* 10.12 数字 */
    {"0", "数字 - 0"},
    {"123", "整数"},
    {"3.14", "浮点数"},
    {"0xFF", "十六进制"},
    {"0b10101", "二进制"},
    {"1.5e10", "科学计数法"},
    {"123f", "浮点后缀"},

    /* 10.13 字符串 */
    {"'hello'", "单引号字符串"},
    {"\"world\"", "双引号字符串"},
    {"''", "空字符串"},
    {"'it''s'", "转义单引号"},

    /* 10.14 关键字和标识符 */
    {"SELECT", "关键字 SELECT"},
    {"FROM", "关键字 FROM"},
    {"WHERE", "关键字 WHERE"},
    {"identifier", "标识符"},
    {"_underscore", "下划线标识符"},

    /* 10.15 反引号 */
    {"`table`", "反引号表名"},
    {"`SELECT`", "反引号关键字"},
    {"`COUNT`(*)", "反引号函数"},

    /* 10.16 变量 */
    {"@variable", "用户变量"},
    {"@@global", "系统变量"},
    {"@`var`", "反引号变量"},

    /* 10.17 二进制字符串 */
    {"B'10101'", "大写B二进制"},
    {"b'10101'", "小写b二进制"},

    /* 10.18 十六进制字符串 */
    {"X'FFA2'", "大写X十六进制"},
    {"x'FFA2'", "小写x十六进制"},

    /* 10.19 转义字符串 */
    {"E'escaped\\nstring'", "大写E转义"},
    {"e'escaped\\nstring'", "小写e转义"},

    /* 10.20 Unicode字符串 */
    {"U&'unicode string'", "大写U&"},
    {"u&'unicode string'", "小写u&"},

    /* 10.21 引用字符串 */
    {"q'(string)'", "q括号"},
    {"q'[string]'", "q方括号"},
    {"q'{string}'", "q花括号"},
    {"q'<string>'", "q尖括号"},

    /* 10.22 国家字符集字符串 */
    {"N'string'", "大写N字符串"},
    {"n'string'", "小写n字符串"},

    /* 10.23 方括号标识符 */
    {"[identifier]", "方括号标识符"},
    {"[identifier with spaces]", "方括号带空格"},

    /* 综合测试 - SQL注入模式 */
    {"passwd=1024 or 1=1", "注入 - or 1=1"},
    {"SELECT * FROM users WHERE id=1", "基本查询"},
    {"'admin' OR '1'='1'", "注入 - OR绕过"},
    {"'; DROP TABLE users; --", "注入 - DROP表"},
    {"1' UNION SELECT 1,2,3--", "注入 - UNION查询"},

    /* 高级测试 - SQL注入模式 */
    {"1' OR '1'='1'--", "注入 - OR='1'"},
    {"admin' UNION SELECT NULL,password,NULL FROM users WHERE username='admin'--", "注入 - UNION密码"},
    {"1'; DROP TABLE users; --", "注入 - DROP"},
    {"1' AND 1=1--", "注入 - AND1=1"},
    {"' OR 1=1#", "注入 - OR#"},
    {"1' OR 1=1--", "注入 - OR1=1"},
    {"admin'--", "注入 - admin注释"},
    {"' UNION SELECT 1,version(),3--", "注入 - 版本"},
    {"1' AND (SELECT COUNT(*) FROM users) > 0--", "注入 - 子查询"},
    {"'; EXEC xp_cmdshell('dir');--", "注入 - xp_cmdshell"},

    /* 实际SQL语句 */
    {"SELECT * FROM users WHERE id=1 AND status='active'", "SQL - WHERE条件"},
    {"SELECT u.name, COUNT(o.id) as order_count FROM users u JOIN orders o ON u.id=o.user_id GROUP BY u.name", "SQL - JOIN聚合"},
    {"INSERT INTO users (name,email,created_at) VALUES ('test@example.com','password123',NOW())", "SQL - INSERT"},
    {"UPDATE products SET price=price*1.1, updated_at=NOW() WHERE id=100", "SQL - UPDATE"},
    {"DELETE FROM logs WHERE created_at < DATE_SUB(NOW(), INTERVAL 30 DAY)", "SQL - DELETE"},
    {"SELECT N'unicode string', E'escaped\nstring' FROM table1", "SQL - 特殊字符串"},

    /* 类型转换和特殊字符串 */
    {"SELECT U&'\\0041\\0042' as unicode, B'1010' as binary FROM table1", "SQL - Unicode二进制"},
    {"SELECT n'national string', e'\\x41\\x42', x'FF' FROM table1", "SQL - 国家字符集"},
    {"SELECT q'[bracket string]', q'{brace string}', q'(paren string)' FROM table1", "SQL - q字符串"},

    /* 运算符组合 */
    {"x && y || z ! w & a | b ^ c << e >> f", "运算符组合"},
    {"a == b != c <> d <= e >= f <=> g", "比较运算符"},
    {"a::text || b::int || c::date", "类型转换"},
    {"col1 % 2 = 0 AND col2 BETWEEN 1 AND 100", "BETWEEN"},
    {"value1 IN (1,2,3) AND value2 NOT IN (4,5,6)", "IN NOT IN"},

    /* 复杂查询 */
    {"SELECT * FROM (SELECT a,b FROM t1 WHERE x>0) AS subq", "子查询"},
    {"CASE WHEN type=1 THEN 'one' WHEN type=2 THEN 'two' ELSE 'other' END", "CASE WHEN"},
    {"COALESCE(NULLIF(a,b),c,d,e) as result", "COALESCE"},
    {"SELECT CAST('123' AS UNSIGNED), CONVERT(varchar, 123)", "CAST CONVERT"},
    {"SELECT JSON_EXTRACT(data, '$.user.name') FROM users", "JSON函数"},

    /* 注释测试 */
    {"SELECT * FROM users -- 这是单行注释\nWHERE id=1", "单行注释"},
    {"/* 这是多行注释 */\nSELECT * FROM table", "多行注释"},
    {"# MySQL 注释风格\nSELECT 1", "MySQL注释"},

    /* 变量和函数 */
    {"@@global.sql_mode, @session_var, @user_var", "变量组合"},
    {"SELECT CONCAT_WS(':', name, email), UPPER(name) FROM users", "字符串函数"},
    {"SELECT IFNULL(value, 'default'), IF(cond, a, b) FROM table", "IF函数"},
    {"SELECT NOW(), CURRENT_DATE(), CURRENT_TIME FROM dual", "日期函数"},
    {"DATE_ADD('2024-01-01', INTERVAL 1 YEAR), TIMESTAMPDIFF(SECOND, start_time, end_time)", "日期运算"},

    /* Web/URI 测试 */
    {"api.php?action=get);SELECT PG_SLEEP(5)--&table=user_profiles&id=1", "Web - PG_SLEEP"},
    {"api.php?action=get));SELECT PG_SLEEP(5)--&table=user_profiles&id=1", "Web - PG_SLEEP双括号"},
    {"api.php?action=get;SELECT PG_SLEEP(5)--&table=user_profiles&id=1", "Web - PG_SLEEP单分号"},
    {"api.php?action=get);WAITFOR DELAY '0:0:5'--&table=user_profiles&id=1", "Web - WAITFOR"},
    {"api.php?action=get));WAITFOR DELAY '0:0:5'--&table=user_profiles&id=1", "Web - WAITFOR双括号"},
    {"api.php?action=get;WAITFOR DELAY '0:0:5'--&table=user_profiles&id=1", "Web - WAITFOR单分号"},
    {"api.php?action=get);DECLARE @x CHAR(9);SET @x=0x303a303a35;WAITFOR DELAY @x--&table=user_profiles&id=1", "Web - DECLARE变量"},
    {"api.php?action=get));DECLARE @x CHAR(9);SET @x=0x303a303a35;WAITFOR DELAY @x--&table=user_profiles&id=1", "Web - DECLARE变量双括号"},
    {"api.php?action=get;DECLARE @x CHAR(9);SET @x=0x303a303a35;WAITFOR DELAY @x--&table=user_profiles&id=1", "Web - DECLARE变量单分号"},
    {"api.php?action=get);SELECT DBMS_PIPE(CHR(121)||CHR(83)||CHR(78)||CHR(70),5) FROM DUAL--&table=user_profiles&id=1", "Web - DBMS_PIPE"},
    {"index.php?table=user_profiles&id=1\")));SELECT SLEEP(5) AND (((\"eQVc\"=\"eQVc", "Web - SLEEP双引号"},
    {"search.php?q=test\")));(SELECT * FROM (SELECT(SLEEP(5)))mwZE) AND (((\"taNc\" LIKE \"taNc", "Web - SLEEP嵌套"},
    {"api.php?action=get);SELECT DBMS_PIPE.RECEIVE_MESSAGE(CHR(82)||CHR(102)||CHR(121)||CHR(71),5) FROM DUAL--&table=user_profiles&id=1", "Web - RECEIVE_MESSAGE"},
    {"index.php?table=products&id=1';SELECT SLEEP(5) AND 'notB'='notB", "Web - SLEEP单引号"},
    {"index.php?table=articles&id=1';SELECT SLEEP(5) AND 'NOtr'='NOtr", "Web - SLEEP单引号2"},
    {"index.php?table=products&id=51&ref=search", "Web - 正常参数"},
    {"jenkins/job/Ksim_CI/wfapi/runs?since=#289&fullStages=true&_=1767144313183", "Web - Jenkins URL"},
    {"api.php?action=get&table=user_profiles\") AS AyWa WHERE 8450=8450;SELECT SLEEP(5)-- mFBr&id=1", "Web - AS别名注入"},
    {"index.php?table=user_profiles(',)....\",&id=1", "Web - 特殊字符"},
    {"index.php?table=user_profiles'iWoioD<'\">HOvEOc&id=1", "Web - 混合引号"},
    {"index.php?table=user_profiles\")) AS OPBO WHERE 9345=9345;SELECT SLEEP(5)#&id=1", "Web - AS别名双括号"},
    {"api.php?action=get&table=user_profiles`=`user_profiles`;SELECT SLEEP(5)#&id=1", "Web - 反引号注入"},
    {"index.php?table=user_profiles` WHERE 8358=8358;SELECT SLEEP(5)#&id=1", "Web - 反引号WHERE"},
    {"api.php?action=get&table=user_profiles` WHERE 3424=3424;SELECT SLEEP(5)-- LTup&id=1", "Web - 反引号WHERE注释"},
    {"api.php?action=get&table=user_profiles')) AS mpsf WHERE 1708=1708;SELECT SLEEP(5)#&id=1", "Web - AS别名三括号"},
    {"api.php?action=get');SELECT DBMS_PIPE.RECEIVE_MESSAGE(CHR(75)||CHR(74)||CHR(66)||CHR(76),5) FROM DUAL--&table=user_profiles&id=1", "Web - RECEIVE单引号"},
    {"api.php?action=get);SELECT DBMS_PIPE.RECEIVE_MESSAGE(CHR(121)||CHR(83)||CHR(78)||CHR(70),5) FROM DUAL--&table=user_profiles&id=1", "Web - RECEIVE重复"},
    {"api.php?action=get&table=user_profiles WHERE 1224=1224 UNION ALL SELECT NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,CONCAT(0x716a767871,(CASE WHEN ((SELECT super_priv FROM mysql.user WHERE user=0x77656275736572 LIMIT 0,1)=0x59) THEN 1 ELSE 0 END),0x71786a7871),NULL,NULL,NULL,NULL-- -&id=1", "Web - UNION复杂注入"},

    /* 手工测试 */
    {"search.php?q=test\")));(SELECT * FROM (SELECT(SLEEP(5)))mwZE) AND (((\"taNc\" LIKE \"taNc", "手工 - SLEEP嵌套SELECT"},
    {"api.php?action=get&table=user_profiles);SELECT BENCHMARK(5000000,MD5(0x676d514e))-- FdDd&id=1", "手工 - BENCHMARK注入"},
    {"index.php?table=user_profiles]-(SELECT 0 WHERE 4468=4468;SELECT SLEEP(5))|[user_profiles&id=1", "手工 - 方括号注入"},
    {"index.php?table=products&id=1\")));SELECT BENCHMARK(5000000,MD5(0x7965694c)) AND (((\"powc\"=\"powc", "手工 - BENCHMARK双引号"},
    {"index.php?table=user_profiles\");SELECT BENCHMARK(5000000,MD5(0x6164476d)) AND (\"fOrC\"=\"fOrC&id=1", "手工 - BENCHMARK单引号"},
};

/* ========== 主程序 ========== */

int main(int argc, char *argv[]) {
#ifdef _WIN32
    system("chcp 65001 > nul 2>&1");
#endif

    const int num_tests = sizeof(test_cases) / sizeof(test_cases[0]);

    /* 命令行模式：测试单个字符串 */
    if (argc > 1) {
        kadd_dfa_engine_t *engine = NULL;
        if (kadd_dfa_engine_create(&engine, "tools/profile_data/transition_table.bin",
                                    "tools/profile_data/states_meta.json", 8192, 8192) != 0) {
            fprintf(stderr, "错误: 无法创建 DFA 引擎\n");
            return 1;
        }

        int dfa_tokens[1000];
        int dfa_count = 0;
        kadd_dfa_engine_tokenize(engine, argv[1], strlen(argv[1]), dfa_tokens, &dfa_count);

        int lib_count = 0;
        token_info_t *lib_tokens = libinjection_tokenize(argv[1], &lib_count);

        printf("输入: \"%s\"\n\n", argv[1]);
        printf("DFA 引擎 (%d tokens):\n", dfa_count);
        for (int i = 0; i < dfa_count; i++) {
            printf("  [%d] %s\n", dfa_tokens[i], token_type_name(dfa_tokens[i]));
        }

        printf("\nlibinjection (%d tokens):\n", lib_count);
        for (int i = 0; i < lib_count; i++) {
            printf("  [%d] %s (type:'%c', val:\"%s\")\n",
                   lib_tokens[i].index, token_type_name(lib_tokens[i].index),
                   lib_tokens[i].type, lib_tokens[i].value);
        }

        free(lib_tokens);
        kadd_dfa_engine_free(engine);
        return 0;
    }

    /* 批量测试模式 */
    kadd_dfa_engine_t *engine = NULL;
    if (kadd_dfa_engine_create(&engine, "tools/profile_data/transition_table.bin",
                                "tools/profile_data/states_meta.json", 8192, 8192) != 0) {
        fprintf(stderr, "错误: 无法创建 DFA 引擎\n");
        return 1;
    }

    printf("DFA 引擎分词对比测试\n");
    printf("====================\n");
    printf("测试用例数: %d\n\n", num_tests);

    int passed = 0, failed = 0;

    for (int i = 0; i < num_tests; i++) {
        const char *input = test_cases[i].input;

        /* DFA 分词 */
        int dfa_tokens[1000];
        int dfa_count = 0;
        kadd_dfa_engine_tokenize(engine, input, strlen(input), dfa_tokens, &dfa_count);

        /* libinjection 分词 */
        int lib_count = 0;
        token_info_t *lib_tokens = libinjection_tokenize(input, &lib_count);

        /* 对比结果 */
        int is_match = compare_tokens(dfa_tokens, dfa_count, lib_tokens, lib_count);

        if (is_match) {
            printf("✓ [%d] %s\n", i + 1, test_cases[i].description);
            passed++;
        } else {
            printf("✗ [%d] %s\n", i + 1, test_cases[i].description);
            printf("  原始输入: \"%s\"\n\n", input);

            printf("  DFA 引擎 (%d tokens):\n", dfa_count);
            for (int j = 0; j < dfa_count; j++) {
                printf("    [%d] %s\n", dfa_tokens[j], token_type_name(dfa_tokens[j]));
            }

            printf("\n  libinjection (%d tokens):\n", lib_count);
            for (int j = 0; j < lib_count; j++) {
                printf("    [%d] %s (type:'%c', val:\"%s\")\n",
                       lib_tokens[j].index, token_type_name(lib_tokens[j].index),
                       lib_tokens[j].type, lib_tokens[j].value);
            }
            printf("\n");
            failed++;
        }

        free(lib_tokens);
    }

    /* 输出汇总 */
    printf("====================\n");
    printf("通过: %d\n", passed);
    printf("失败: %d\n", failed);
    printf("通过率: %.1f%%\n", num_tests > 0 ? (double)passed / num_tests * 100 : 0);

    kadd_dfa_engine_free(engine);
    return failed > 0 ? 1 : 0;
}
