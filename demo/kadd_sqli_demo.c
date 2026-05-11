/**
 * @file kadd_sqli_demo.c
 * @brief SQL 注入检测引擎交互式演示
 *
 * 演示如何使用 kadd_sqli_engine 进行 SQL 注入检测
 * 整合 DFA 分词 + 特征提取 + ML 推理
 *
 * 使用方式:
 *   ./kadd_sqli_demo    # 交互式模式
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kadd_sqli_engine.h"
#include "kadd_config_loader.h"
#include "tools.h"

#ifdef _WIN32
#include <windows.h>
#endif

/* ========== 检测和打印函数 ========== */

static void detect_and_print(kadd_sqli_engine_t *engine, const char *input) {
    float probability = 0.0f;

    printf("  输入: \"%s\"\n", input);

    void *timer = timer_start();
    int ret = kadd_sqli_detect(engine, input, &probability);
    double elapsed = timer_end(timer);

    if (ret != KADD_SUCCESS) {
        printf("  检测失败 (错误码: %d)\n\n", ret);
        return;
    }

    int is_sqli = (probability >= 0.5f) ? 1 : 0;
    const char *icon = is_sqli ? "[SQL 注入]" : "[安全]";

    printf("  %s 概率: %.2f%% | 耗时: %.2f us\n\n", icon, probability * 100, elapsed);
}

/* ========== 主程序 ========== */

int main(int argc, char *argv[])
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("=== SQL 注入检测引擎交互式演示 ===\n\n");

    /* 创建引擎 */
    printf("正在初始化引擎...\n");

    kadd_sqli_engine_t *engine = NULL;
    if (kadd_sqli_engine_create_from_file(&engine, "config.json") != KADD_SUCCESS) {
        printf("引擎创建失败，请检查 config.json\n");
        return 1;
    }

    printf("引擎初始化成功\n\n");
    printf("输入 'quit' 或 'exit' 退出，'help' 查看帮助\n\n");

    /* 交互式检测 */
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
            printf("  SQL 注入示例:\n");
            printf("    id=1' OR '1'='1\n");
            printf("    admin'--\n");
            printf("    1' UNION SELECT NULL--\n\n");
            printf("  正常输入示例:\n");
            printf("    user@example.com\n");
            printf("    SELECT * FROM users\n");
            printf("    price=99.99\n\n");
            printf("  命令:\n");
            printf("    batch  - 进入批处理模式（批量检测多个输入）\n");
            printf("    quit   - 退出程序\n\n");
            line_num++;
            continue;
        }

        if (strcmp(input, "batch") == 0) {
            printf("\n=== 批处理模式 ===\n\n");

            int batch_count;
            printf("请输入要检测的字符串数量: ");
            scanf("%d", &batch_count);
            getchar();

            const char **batch_inputs = (const char **)malloc(batch_count * sizeof(char *));
            float *batch_probs = (float *)malloc(batch_count * sizeof(float));

            if (!batch_inputs || !batch_probs) {
                printf("内存分配失败\n");
                free(batch_inputs);
                free(batch_probs);
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

            int result = kadd_sqli_detect_batch(engine, batch_inputs, batch_count,
                                                batch_probs, 32);
            printf("返回值: %d\n", result);

            if (result >= 0) {
                for (int i = 0; i < batch_count; i++) {
                    int is_sqli = (batch_probs[i] >= 0.5f) ? 1 : 0;
                    const char *status = is_sqli ? "[SQL 注入]" : "[安全]";
                    printf("  [%d] \"%s\"  %s 概率: %.2f%%\n",
                           i + 1, batch_inputs[i], status, batch_probs[i] * 100);
                }
            }

            for (int i = 0; i < batch_count; i++) {
                free((void *)batch_inputs[i]);
            }
            free(batch_inputs);
            free(batch_probs);

            printf("\n批处理模式结束\n");
            line_num++;
            continue;
        }

        if (strlen(input) == 0) continue;

        detect_and_print(engine, input);
        line_num++;
    }

    /* 释放资源 */
    kadd_sqli_engine_free(engine);
    printf("\n引擎已释放\n");

    return 0;
}