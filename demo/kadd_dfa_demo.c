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

/* ========== 分词和打印函数 ========== */

static void tokenize_and_print(kadd_dfa_engine_t *engine, const char *input) {
    int tokens[8192];
    int count = 0;

    printf("  输入: \"%s\"\n", input);

    void *timer = timer_start();
    int ret = kadd_dfa_engine_tokenize(engine, input, strlen(input), tokens, &count);
    double elapsed = timer_end(timer);

    if (ret != 0) {
        printf("  DFA 分词失败 (错误码: %d)\n\n", ret);
        return;
    }

    printf("  DFA 结果: %d tokens, %.2f us\n", count, elapsed);

    printf("  Token向量: [");
    int line_pos = 0;
    for (int i = 0; i < count; i++) {
        char buf[16];
        int len = snprintf(buf, sizeof(buf), "%d", tokens[i]);

        if (line_pos > 0 && line_pos + len + 2 > 60) {
            printf("]\n             [");
            line_pos = 0;
        }

        printf("%s", buf);
        line_pos += len;

        if (i < count - 1) {
            printf(", ");
            line_pos += 2;
        }
    }
    printf("]\n");

    /* 显示前10个token的类型名称 */
    if (count > 0) {
        int show = count < 10 ? count : 10;
        printf("  类型:      ");
        for (int i = 0; i < show; i++) {
            printf("%s", token_type_name(tokens[i]));
            if (i < show - 1) printf(", ");
        }
        if (count > 10) printf(" ...");
        printf("\n");
    }

    printf("\n");
}

/* ========== 主程序 ========== */

int main(int argc, char *argv[])
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("=== DFA 分词引擎交互式演示 ===\n\n");

    /* 从配置文件创建 DFA 引擎 */
    printf("正在从 config.json 读取配置...\n");

    const char *config_file = (argc > 1) ? argv[1] : "config.json";

    kadd_config_t config;
    if (kadd_config_load(config_file, &config) != 0) {
        printf("无法加载配置文件: %s\n", config_file);
        return 1;
    }

    printf("  transition_table_path: %s\n", config.dfa.transition_table_path);
    printf("  output_table_path: %s\n", config.dfa.output_table_path);
    printf("  eod_table_path: %s\n", config.dfa.eod_table_path);
    printf("  max_input_length: %d\n", config.dfa.max_input_length);
    printf("  max_output_dim: %d\n", config.dfa.max_output_dim);

    printf("\n正在初始化 DFA 引擎...\n");

    kadd_dfa_table_paths_t table_paths = {
        config.dfa.transition_table_path,
        config.dfa.output_table_path,
        config.dfa.eod_table_path
    };

    kadd_dfa_engine_t *engine = NULL;
    int ret = kadd_dfa_engine_create(&engine, &table_paths,
                                     config.dfa.max_input_length,
                                     config.dfa.max_output_dim);
    if (ret != 0) {
        printf("引擎创建失败 (错误码: %d)\n", ret);
        return 1;
    }

    printf("引擎初始化成功\n\n");
    printf("输入 'quit' 或 'exit' 退出，'help' 查看帮助，'batch' 进入批处理模式\n\n");

    /* 交互式模式 */
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
            printf("\n=== 批处理模式 ===\n\n");

            int batch_count;
            printf("请输入要处理的字符串数量: ");
            scanf("%d", &batch_count);
            getchar();

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

            printf("请输入 %d 个字符串:\n", batch_count);
            for (int i = 0; i < batch_count; i++) {
                printf("[%d]> ", i + 1);
                char batch_input[2048];
                if (!fgets(batch_input, sizeof(batch_input), stdin)) break;
                batch_input[strcspn(batch_input, "\n")] = '\0';
                batch_inputs[i] = strdup(batch_input);
            }

            kadd_dfa_batch_t batch = {
                .inputs = batch_inputs,
                .lengths = NULL,
                .count = batch_count,
                .outputs = batch_outputs,
                .dims = batch_dims,
                .batch_size = 32
            };

            int result = kadd_dfa_engine_tokenize_batch(engine, &batch);
            printf("返回值: %d\n", result);

            if (result >= 0) {
                for (int i = 0; i < batch_count; i++) {
                    printf("  [%d] \"%s\"\n", i + 1, batch_inputs[i]);
                    if (batch_dims[i] >= 0) {
                        printf("      Tokens: [");
                        for (int j = 0; j < batch_dims[i]; j++) {
                            printf("%d", batch_outputs[i][j]);
                            if (j < batch_dims[i] - 1) printf(", ");
                        }
                        printf("]\n");
                        free(batch_outputs[i]);
                    }
                }
            }

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

        tokenize_and_print(engine, input);
        line_num++;
    }

    /* 释放资源 */
    kadd_dfa_engine_free(engine);
    kadd_config_free(&config);
    printf("\n引擎已释放\n");

    return 0;
}