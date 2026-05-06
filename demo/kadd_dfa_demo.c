/**
 * @file kadd_dfa_demo.c
 * @brief DFA 分词引擎交互式演示
 *
 * 演示如何使用 kadd_dfa_engine 进行文本分词
 * 将输入字符串转换为 token 索引向量
 *
 * 使用方式:
 *   ./kadd_dfa_demo    # 交互式模式
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kadd_dfa_engine.h"
#include "tools.h"

/* 检测是否需要使用 third_party cJSON */
#ifndef KADD_USE_THIRD_PARTY_CJSON
#define KADD_USE_THIRD_PARTY_CJSON 1
#endif

#if KADD_USE_THIRD_PARTY_CJSON
#include "../third_party/cJSON/cJSON.h"
#else
#include <cJSON.h>
#endif

/* libinjection 头文件 */
#include "libinjection_sqli.h"

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

/* ========== libinjection 类型转换 ========== */

static int libinjection_type_to_index(char type) {
    static const int8_t map[256] = {
        [0 ... 255] = 24,  // 默认 TYPE_UNKNOWN
        ['0'] = 0,     // TYPE_NONE
        ['k'] = 1,     // TYPE_KEYWORD
        ['U'] = 2,     // TYPE_UNION
        ['B'] = 3,     // TYPE_GROUP
        ['E'] = 4,     // TYPE_EXPRESSION
        ['t'] = 5,     // TYPE_SQLTYPE
        ['f'] = 6,     // TYPE_FUNCTION
        ['n'] = 7,     // TYPE_BAREWORD
        ['1'] = 8,     // TYPE_NUMBER
        ['v'] = 9,     // TYPE_VARIABLE
        ['s'] = 10,    // TYPE_STRING
        ['o'] = 11,    // TYPE_OPERATOR
        ['&'] = 12,    // TYPE_LOGIC_OPERATOR
        ['c'] = 13,    // TYPE_COMMENT
        ['A'] = 14,    // TYPE_COLLATE
        ['('] = 15,    // TYPE_LEFTPARENS
        [')'] = 16,    // TYPE_RIGHTPARENS
        ['{'] = 17,    // TYPE_LEFTBRACE
        ['}'] = 18,    // TYPE_RIGHTBRACE
        ['.'] = 19,    // TYPE_DOT
        [','] = 20,    // TYPE_COMMA
        [':'] = 21,    // TYPE_COLON
        [';'] = 22,    // TYPE_SEMICOLON
        ['T'] = 23,    // TYPE_STATEMENT
        ['?'] = 24,    // TYPE_UNKNOWN
        ['X'] = 25,    // TYPE_EVIL
        ['F'] = 26,    // TYPE_FINGERPRINT
        ['\\'] = 27,   // TYPE_BACKSLASH
    };

    if (type >= 0 && type < 256) {
        return map[(unsigned char)type];
    }
    return 24;  // TYPE_UNKNOWN
}

/* ========== 分词和打印函数 ========== */

/**
 * @brief 对输入进行分词并打印结果
 * 同时输出 DFA 和 libinjection 的结果及耗时对比
 */
static void tokenize_and_print(kadd_dfa_engine_t *engine, const char *input) {
    int dfa_tokens[8192];
    int dfa_count = 0;
    int lib_tokens[8192];
    int lib_count = 0;

    printf("  输入: \"%s\"\n", input);

    // ========== DFA 分词 ==========
    void *timer = timer_start();
    int ret = kadd_dfa_engine_tokenize(engine, input, strlen(input), dfa_tokens, &dfa_count);
    double dfa_time = timer_end(timer);

    if (ret != 0) {
        printf("  ✗ DFA 分词失败 (错误码: %d)\n\n", ret);
        return;
    }

    // ========== libinjection 分词 ==========
    struct libinjection_sqli_state lib_state;
    libinjection_sqli_init(&lib_state, input, strlen(input), FLAG_QUOTE_NONE | FLAG_SQL_MYSQL);

    timer = timer_start();
    while (libinjection_sqli_tokenize(&lib_state)) {
        if (lib_count < 8192) {
            lib_tokens[lib_count++] = libinjection_type_to_index(lib_state.current->type);
        }
    }
    double lib_time = timer_end(timer);

    // ========== 打印结果对比 ==========
    printf("  ┌─────────────────────────────────────────────────────────────┐\n");
    printf("  │ DFA 结果                                                    │\n");
    printf("  ├─────────────────────────────────────────────────────────────┤\n");
    printf("  │ 耗时:       %8.2f μs                                      │\n", dfa_time);
    printf("  │ Token数:    %8d                                          │\n", dfa_count);
    printf("  │ Token向量: [");
    for (int i = 0; i < dfa_count && i < 20; i++) {
        printf("%d", dfa_tokens[i]);
        if (i < dfa_count - 1 && i < 19) printf(", ");
    }
    if (dfa_count > 20) printf("...");
    printf("]                                              │\n");
    printf("  └─────────────────────────────────────────────────────────────┘\n");

    printf("  ┌─────────────────────────────────────────────────────────────┐\n");
    printf("  │ libinjection 结果                                          │\n");
    printf("  ├─────────────────────────────────────────────────────────────┤\n");
    printf("  │ 耗时:       %8.2f μs    (转换耗时: %.2f μs)            │\n", lib_time, lib_time);
    printf("  │ Token数:    %8d                                          │\n", lib_count);
    printf("  │ Token向量: [");
    for (int i = 0; i < lib_count && i < 20; i++) {
        printf("%d", lib_tokens[i]);
        if (i < lib_count - 1 && i < 19) printf(", ");
    }
    if (lib_count > 20) printf("...");
    printf("]                                              │\n");
    printf("  └─────────────────────────────────────────────────────────────┘\n");

    // ========== 对比总结 ==========
    int match = (dfa_count == lib_count);
    if (match) {
        for (int i = 0; i < dfa_count; i++) {
            if (dfa_tokens[i] != lib_tokens[i]) {
                match = 0;
                break;
            }
        }
    }

    const char *match_icon = match ? "✓" : "✗";
    const char *match_text = match ? "匹配" : "不匹配";

    double speed_ratio = (lib_time > 0) ? (dfa_time / lib_time) : 0.0;

    printf("  ┌─────────────────────────────────────────────────────────────┐\n");
    printf("  │ 对比总结                                                    │\n");
    printf("  ├─────────────────────────────────────────────────────────────┤\n");
    printf("  │ 向量对比:   %s %-8s                                   │\n", match_icon, match_text);
    printf("  │ 性能对比:   DFA %.2fx %s                         │\n",
           speed_ratio >= 1.0 ? speed_ratio : 1.0/speed_ratio,
           speed_ratio >= 1.0 ? "libinjection" : "DFA");
    printf("  └─────────────────────────────────────────────────────────────┘\n\n");
}

/* ========== 主程序 ========== */

int main(int argc, char *argv[])
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║           DFA 分词引擎交互式演示 (kadd_dfa_engine)        ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");

    // ========== 从配置文件创建 DFA 引擎 ==========
    printf("正在从 config.json 读取配置...\n");

    const char *config_file = (argc > 1) ? argv[1] : "config.json";

    FILE *f = fopen(config_file, "r");
    if (!f) {
        printf("✗ 无法打开配置文件: %s\n", config_file);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *config_content = (char *)malloc(file_size + 1);
    if (!config_content) {
        printf("✗ 内存分配失败\n");
        fclose(f);
        return 1;
    }

    fread(config_content, 1, file_size, f);
    config_content[file_size] = '\0';
    fclose(f);

    cJSON *config = cJSON_Parse(config_content);
    free(config_content);

    if (!config) {
        printf("✗ 配置文件解析失败\n");
        return 1;
    }

    cJSON *dfa_config = cJSON_GetObjectItemCaseSensitive(config, "kadd_dfa_engine");
    if (!dfa_config) {
        printf("✗ 配置文件中缺少 kadd_dfa_engine 节点\n");
        cJSON_Delete(config);
        return 1;
    }

    cJSON *table_path_item = cJSON_GetObjectItemCaseSensitive(dfa_config, "table_path");
    cJSON *state_path_item = cJSON_GetObjectItemCaseSensitive(dfa_config, "state_path");
    cJSON *max_input_item = cJSON_GetObjectItemCaseSensitive(dfa_config, "max_input_length");
    cJSON *max_output_item = cJSON_GetObjectItemCaseSensitive(dfa_config, "max_output_dim");

    if (!cJSON_IsString(table_path_item) || !cJSON_IsString(state_path_item)) {
        printf("✗ 配置文件中缺少 table_path 或 state_path\n");
        cJSON_Delete(config);
        return 1;
    }

    const char *table_path = table_path_item->valuestring;
    const char *state_path = state_path_item->valuestring;
    size_t max_input = max_input_item && cJSON_IsNumber(max_input_item) ? (size_t)max_input_item->valuedouble : 8192;
    size_t max_output = max_output_item && cJSON_IsNumber(max_output_item) ? (size_t)max_output_item->valuedouble : 8192;

    printf("  table_path: %s\n", table_path);
    printf("  state_path: %s\n", state_path);
    printf("  max_input_length: %zu\n", max_input);
    printf("  max_output_dim: %zu\n", max_output);

    printf("\n正在初始化 DFA 引擎...\n");

    kadd_dfa_engine_t *engine = NULL;
    int ret = kadd_dfa_engine_create(&engine, table_path, state_path, max_input, max_output);
    if (ret != 0) {
        printf("✗ 引擎创建失败 (错误码: %d)\n", ret);
        cJSON_Delete(config);
        return 1;
    }

    printf("✓ 引擎初始化成功\n\n");
    printf("输入 'quit' 或 'exit' 退出，'help' 查看帮助\n\n");

    // ========== 交互式模式 ==========
    char input[2048];
    int line_num = 1;

    while (printf("[%d]> ", line_num), fgets(input, sizeof(input), stdin)) {
        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "quit") == 0 || strcmp(input, "exit") == 0 ||
            strcmp(input, "q") == 0) {
            break;
        }

        if (strcmp(input, "help") == 0 || strcmp(input, "h") == 0 ||
            strcmp(input, "?") == 0) {
            printf("\n帮助信息:\n");
            printf("  Token 类型: 0=NONE 1=KEYWORD 8=NUMBER 10=STRING 11=OPERATOR\n");
            printf("             15=LPAREN 16=RPAREN 22=SEMICOLON 24=UNKNOWN\n\n");
            line_num++;
            continue;
        }

        if (strlen(input) == 0) continue;

        printf("  输入: \"%s\"\n", input);
        tokenize_and_print(engine, input);
        line_num++;
    }

    // ========== 释放资源 ==========
    kadd_dfa_engine_free(engine);
    cJSON_Delete(config);
    printf("\n✓ 引擎已释放\n");

    return 0;
}
