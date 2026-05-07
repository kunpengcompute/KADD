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
#include "kadd_config_loader.h"
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
        if (lib_count < 8192 && lib_state.current) {
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

    // 打印完整的 token 向量
    printf("  │ Token向量: [");
    int line_pos = 0;
    for (int i = 0; i < dfa_count; i++) {
        char num_buf[16];
        int num_len = snprintf(num_buf, sizeof(num_buf), "%d", dfa_tokens[i]);

        // 如果当前行放不下，换行并对齐
        if (line_pos > 0 && line_pos + num_len + 2 > 53) {
            printf("]\n");
            printf("  │             [");
            line_pos = 0;
        }

        printf("%s", num_buf);
        line_pos += num_len;

        if (i < dfa_count - 1) {
            printf(", ");
            line_pos += 2;
        }
    }
    printf("]                                              │\n");
    printf("  └─────────────────────────────────────────────────────────────┘\n");

    printf("  ┌─────────────────────────────────────────────────────────────┐\n");
    printf("  │ libinjection 结果                                          │\n");
    printf("  ├─────────────────────────────────────────────────────────────┤\n");
    printf("  │ 耗时:       %8.2f μs    (转换耗时: %.2f μs)            │\n", lib_time, lib_time);
    printf("  │ Token数:    %8d                                          │\n", lib_count);

    // 打印完整的 token 向量
    printf("  │ Token向量: [");
    line_pos = 0;
    for (int i = 0; i < lib_count; i++) {
        char num_buf[16];
        int num_len = snprintf(num_buf, sizeof(num_buf), "%d", lib_tokens[i]);

        // 如果当前行放不下，换行并对齐
        if (line_pos > 0 && line_pos + num_len + 2 > 53) {
            printf("]\n");
            printf("  │             [");
            line_pos = 0;
        }

        printf("%s", num_buf);
        line_pos += num_len;

        if (i < lib_count - 1) {
            printf(", ");
            line_pos += 2;
        }
    }
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

    kadd_config_t config;
    if (kadd_config_load(config_file, &config) != 0) {
        printf("✗ 无法加载配置文件: %s\n", config_file);
        return 1;
    }

    printf("  transition_table_path: %s\n", config.dfa.table_paths.transition_table_path);
    printf("  output_table_path: %s\n", config.dfa.table_paths.output_table_path);
    printf("  eod_table_path: %s\n", config.dfa.table_paths.eod_table_path);
    printf("  max_input_length: %d\n", config.dfa.max_input_length);
    printf("  max_output_dim: %d\n", config.dfa.max_output_dim);

    printf("\n正在初始化 DFA 引擎...\n");

    kadd_dfa_engine_t *engine = NULL;
    int ret = kadd_dfa_engine_create(&engine, &config.dfa.table_paths,
                                     config.dfa.max_input_length,
                                     config.dfa.max_output_dim);
    if (ret != 0) {
        printf("✗ 引擎创建失败 (错误码: %d)\n", ret);
        return 1;
    }

    printf("✓ 引擎初始化成功\n\n");
    printf("输入 'quit' 或 'exit' 退出，'help' 查看帮助，'batch' 进入批处理模式\n\n");

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
            printf("             15=LPAREN 16=RPAREN 22=SEMICOLON 24=UNKNOWN\n");
            printf("  命令:\n");
            printf("    batch  - 进入批处理模式（批量处理多个输入）\n");
            printf("    quit   - 退出程序\n\n");
            line_num++;
            continue;
        }

        if (strcmp(input, "batch") == 0) {
            // ========== 批处理模式 ==========
            printf("\n╔════════════════════════════════════════════════════════════╗\n");
            printf("║                    批处理模式                              ║\n");
            printf("╚════════════════════════════════════════════════════════════╝\n\n");

            printf("请输入要处理的字符串数量: ");
            int batch_count = 0;
            if (scanf("%d", &batch_count) != 1 || batch_count <= 0 || batch_count > 100) {
                printf("无效的数量，退出批处理模式\n");
                while (getchar() != '\n'); // 清空输入缓冲区
                line_num++;
                continue;
            }
            while (getchar() != '\n'); // 清空换行符

            // 准备批处理输入
            const char **batch_inputs = (const char **)malloc(batch_count * sizeof(char *));
            int **batch_outputs = (int **)malloc(batch_count * sizeof(int *));
            int *batch_dims = (int *)malloc(batch_count * sizeof(int));

            if (!batch_inputs || !batch_outputs || !batch_dims) {
                printf("内存分配失败\n");
                free(batch_inputs);
                free(batch_outputs);
                free(batch_dims);
                line_num++;
                continue;
            }

            // 读取输入
            printf("请输入 %d 个字符串（每行一个，空行结束）:\n", batch_count);
            for (int i = 0; i < batch_count; i++) {
                printf("[%d]> ", i + 1);
                char batch_input[2048];
                if (!fgets(batch_input, sizeof(batch_input), stdin)) break;
                batch_input[strcspn(batch_input, "\n")] = '\0';
                batch_inputs[i] = strdup(batch_input);
            }

            // 执行批处理
            printf("\n正在执行批处理...\n");
            void *timer = timer_start();

            kadd_dfa_batch_t batch = {
                .inputs = batch_inputs,
                .lengths = NULL,
                .count = batch_count,
                .outputs = batch_outputs,
                .dims = batch_dims,
                .batch_size = 32
            };

            int result = kadd_dfa_engine_tokenize_batch(engine, &batch);
            double batch_time = timer_end(timer);

            // 打印结果
            printf("\n批处理完成！\n");
            printf("┌─────────────────────────────────────────────────────────────┐\n");
            printf("│ 批处理结果                                                  │\n");
            printf("├─────────────────────────────────────────────────────────────┤\n");
            printf("│ 总输入数:     %5d                                         │\n", batch_count);
            printf("│ 成功处理:     %5d                                         │\n", result > 0 ? result : 0);
            printf("│ 总耗时:       %8.2f μs                                 │\n", batch_time);
            printf("│ 平均耗时:     %8.2f μs/条                               │\n", batch_time / batch_count);
            printf("└─────────────────────────────────────────────────────────────┘\n\n");

            // 详细结果
            for (int i = 0; i < batch_count; i++) {
                printf("  [%d] \"%s\"\n", i + 1, batch_inputs[i]);
                if (batch_dims[i] >= 0) {
                    printf("      Tokens: [");
                    int line_pos = 0;
                    for (int j = 0; j < batch_dims[i]; j++) {
                        char num_buf[16];
                        int num_len = snprintf(num_buf, sizeof(num_buf), "%d", batch_outputs[i][j]);

                        // 如果当前行放不下，换行并对齐
                        if (line_pos > 0 && line_pos + num_len + 2 > 60) {
                            printf("\n                ");
                            line_pos = 0;
                        }

                        printf("%s", num_buf);
                        line_pos += num_len;

                        if (j < batch_dims[i] - 1) {
                            printf(", ");
                            line_pos += 2;
                        }
                    }
                    printf("] (%d个)\n", batch_dims[i]);
                    free(batch_outputs[i]);
                } else {
                    printf("      ✗ 处理失败\n");
                }
            }

            // 清理
            for (int i = 0; i < batch_count; i++) {
                free((void *)batch_inputs[i]);
            }
            free(batch_inputs);
            free(batch_outputs);
            free(batch_dims);

            printf("\n批处理模式结束\n");
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
    kadd_config_free(&config);
    printf("\n✓ 引擎已释放\n");

    return 0;
}
