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
#include "tools.h"

#ifdef _WIN32
#include <windows.h>
#endif

/* libinjection 头文件 */
#include "libinjection_sqli.h"

/* ========== 检测和打印函数 ========== */

/**
 * @brief 检测输入并打印结果
 * 同时对比 KADD SQLi 和 libinjection 的判断结果及耗时
 */
static void detect_and_print(kadd_sqli_engine_t *engine, const char *input) {
    float probability = 0.0f;

    printf("  输入: \"%s\"\n", input);

    // ========== KADD SQLi 检测 ==========
    void *timer = timer_start();
    int ret = kadd_sqli_detect(engine, input, &probability);
    double kadd_time = timer_end(timer);

    int kadd_is_sqli = 0;
    if (ret == KADD_SQLI_OK) {
        kadd_is_sqli = (probability >= 0.5f) ? 1 : 0;
    }

    // ========== libinjection 检测 ==========
    char fingerprint[8];

    timer = timer_start();
    int lib_is_sqli = libinjection_sqli(input, strlen(input), fingerprint);
    double lib_time = timer_end(timer);

    // ========== 打印结果对比 ==========
    const char *kadd_icon = kadd_is_sqli ? "⚠"  : "✓";
    const char *kadd_status = kadd_is_sqli ? "[SQL 注入]" : "[安全]";

    const char *lib_icon = lib_is_sqli ? "⚠"  : "✓";
    const char *lib_status = lib_is_sqli ? "[SQL 注入]" : "[安全]";

    printf("  ┌─────────────────────────────────────────────────────────────┐\n");
    printf("  │ KADD SQLi 引擎                                              │\n");
    printf("  ├─────────────────────────────────────────────────────────────┤\n");
    printf("  │ 结果:       %s %-10s                                 │\n", kadd_icon, kadd_status);
    if (ret == KADD_SQLI_OK) {
        printf("  │ 概率:       %6.2f%%                                          │\n", probability * 100);
    }
    printf("  │ 耗时:       %8.2f μs                                      │\n", kadd_time);
    printf("  └─────────────────────────────────────────────────────────────┘\n");

    printf("  ┌─────────────────────────────────────────────────────────────┐\n");
    printf("  │ libinjection                                               │\n");
    printf("  ├─────────────────────────────────────────────────────────────┤\n");
    printf("  │ 结果:       %s %-10s                                 │\n", lib_icon, lib_status);
    printf("  │ 指纹:       %-6s                                            │\n", fingerprint);
    printf("  │ 耗时:       %8.2f μs                                      │\n", lib_time);
    printf("  └─────────────────────────────────────────────────────────────┘\n");

    // ========== 对比总结 ==========
    int result_match = (kadd_is_sqli == lib_is_sqli);
    const char *match_icon = result_match ? "✓" : "✗";
    const char *match_text = result_match ? "一致" : "不一致";

    double speed_ratio = (lib_time > 0) ? (kadd_time / lib_time) : 0.0;

    printf("  ┌─────────────────────────────────────────────────────────────┐\n");
    printf("  │ 对比总结                                                    │\n");
    printf("  ├─────────────────────────────────────────────────────────────┤\n");
    printf("  │ 判断结果:   %s %-8s                                   │\n", match_icon, match_text);
    printf("  │ 性能对比:   KADD %.2fx %s                         │\n",
           speed_ratio >= 1.0 ? speed_ratio : 1.0/speed_ratio,
           speed_ratio >= 1.0 ? "libinjection" : "KADD");
    printf("  └─────────────────────────────────────────────────────────────┘\n\n");
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

    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║        SQL 注入检测引擎交互式演示 (kadd_sqli_engine)       ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");

    // ========== 创建引擎 ==========
    printf("正在初始化引擎...\n");

    kadd_sqli_engine_t *engine = NULL;
    if (kadd_sqli_engine_create_from_file(&engine, "config.json") != KADD_SQLI_OK) {
        printf("✗ 引擎创建失败，请检查 config.json\n");
        return 1;
    }

    printf("✓ 引擎初始化成功\n\n");
    printf("输入 'quit' 或 'exit' 退出，'help' 查看帮助\n\n");

    // ========== 交互式检测 ==========
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
            // ========== 批处理模式 ==========
            printf("\n╔════════════════════════════════════════════════════════════╗\n");
            printf("║                    批处理模式                              ║\n");
            printf("╚════════════════════════════════════════════════════════════╝\n\n");

            printf("请输入要检测的字符串数量: ");
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
            float *batch_probs = (float *)malloc(batch_count * sizeof(float));

            if (!batch_inputs || !batch_probs) {
                printf("内存分配失败\n");
                free(batch_inputs);
                free(batch_probs);
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

            int result = kadd_sqli_detect_batch(engine, batch_inputs, batch_count,
                                                batch_probs, 32);
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
                int is_sqli = (batch_probs[i] >= 0.5f) ? 1 : 0;
                const char *status = is_sqli ? "[SQL 注入]" : "[安全]";
                const char *icon = is_sqli ? "⚠" : "✓";
                printf("  [%d] \"%s\"\n", i + 1, batch_inputs[i]);
                printf("      %s %-10s  概率: %5.2f%%\n", icon, status, batch_probs[i] * 100);
            }

            // 清理
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

    // ========== 释放资源 ==========
    kadd_sqli_engine_free(engine);
    printf("\n✓ 引擎已释放\n");

    return 0;
}
