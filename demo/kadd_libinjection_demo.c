/**
 * @file kadd_libinjection_demo.c
 * @brief libinjection SQL 注入检测交互式演示
 *
 * 演示如何使用 libinjection 进行 SQL 注入检测
 * 显示 init、tokenize、fold、lookup 各步骤耗时
 * 与 KADD DFA 引擎进行性能对比
 *
 * 使用方式:
 *   ./kadd_libinjection_demo    # 交互式模式
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tools.h"
#include "libinjection_sqli.h"

#ifdef _WIN32
#include <windows.h>
#endif

/* ========== 分步检测函数 ========== */

/**
 * @brief 对输入进行分步检测并打印各步骤耗时
 */
static void detect_and_print(const char *input) {
    struct libinjection_sqli_state state;

    printf("  输入: \"%s\"\n", input);

    // 步骤 1: init + tokenize (在 libinjection_sqli_init 中完成)
    void *timer = timer_start();
    libinjection_sqli_init(&state, input, strlen(input), FLAG_NONE);
    // 设置默认 lookup 函数
    libinjection_sqli_callback(&state, libinjection_sqli_lookup, NULL);
    double init_time = timer_end(timer);

    // 步骤 2: fold (生成指纹)
    timer = timer_start();
    int token_count = libinjection_sqli_fold(&state);
    double fold_time = timer_end(timer);

    // 步骤 3: lookup (检查指纹)
    timer = timer_start();
    int is_sqli = state.lookup(&state, LOOKUP_FINGERPRINT, state.fingerprint,
                             strlen(state.fingerprint));
    double lookup_time = timer_end(timer);

    double total_time = init_time + fold_time + lookup_time;

    // 结果
    const char *icon = is_sqli ? "⚠"  : "✓";
    const char *status = is_sqli ? "[SQL 注入]" : "[安全]";

    printf("  %s 结果: %-10s | 指纹: %6s | Fold后Token: %d\n", icon, status, state.fingerprint, token_count);
    printf("  耗时分解:\n");
    printf("    init+tokenize: %8.2f μs (%5.1f%%)\n", init_time, (init_time / total_time) * 100);
    printf("    fold:          %8.2f μs (%5.1f%%)\n", fold_time, (fold_time / total_time) * 100);
    printf("    lookup:       %8.2f μs (%5.1f%%)\n", lookup_time, (lookup_time / total_time) * 100);
    printf("    总计:          %8.2f μs\n\n", total_time);
}

/**
 * @brief 批量检测性能测试
 */
static void benchmark_batch(const char **inputs, int count, int iterations) {
    printf("批量性能测试 (%d 次迭代):\n", iterations);
    printf("输入数量: %d\n\n", count);

    char fingerprint[8];
    double total_time = 0.0;
    int sqli_count = 0;

    for (int i = 0; i < iterations; i++) {
        for (int j = 0; j < count; j++) {
            void *timer = timer_start();
            int is_sqli = libinjection_sqli(inputs[j], strlen(inputs[j]), fingerprint);
            double elapsed = timer_end(timer);
            total_time += elapsed;

            if (i == 0 && is_sqli) {
                sqli_count++;
            }
        }
    }

    printf("总耗时: %.2f μs\n", total_time);
    printf("平均耗时: %.2f μs/次\n", total_time / (iterations * count));
    printf("吞吐量: %.0f 次/秒\n", 1000000.0 / (total_time / (iterations * count)));
    printf("检测到 SQL 注入: %d/%d\n\n", sqli_count, count);
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
    printf("║        libinjection SQL 注入检测演示 (分步耗时分析)         ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");

    // ========== 测试数据集 ==========
    const char *test_inputs[] = {
        // 安全输入
        "user@example.com",
        "SELECT * FROM users",
        "price=99.99",
        "search=test",
        "id=1",
        "name=John Doe",
        "status=active",

        // SQL 注入输入
        "id=1' OR '1'='1",
        "admin'--",
        "1' UNION SELECT NULL--",
        "1' AND 1=1--",
        "1'; DROP TABLE users--",
        "admin' OR '1'='1'--",
        "1' UNION SELECT username, password FROM users--",

        // 边界情况
        "",
        "'",
        "1'",
        "' OR 1=1",
    };
    const int test_count = sizeof(test_inputs) / sizeof(test_inputs[0]);

    // ========== 交互式检测 ==========
    printf("输入 'quit' 或 'exit' 退出，'bench' 运行性能测试，'help' 查看帮助\n\n");

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
            printf("    bench  - 运行性能测试\n");
            printf("    help   - 显示帮助\n\n");
            line_num++;
            continue;
        }

        if (strcmp(input, "bench") == 0) {
            benchmark_batch(test_inputs, test_count, 10000);
            line_num++;
            continue;
        }

        if (strlen(input) == 0) continue;

        detect_and_print(input);
        line_num++;
    }

    // ========== 单样本详细分析 ==========
    printf("\n单样本详细分析 (1000 次迭代平均):\n");
    printf("==========================================\n");

    const char *samples[] = {
        "id=1' OR '1'='1",           // SQL 注入
        "id=1",                       // 安全
        "admin'--",                   // SQL 注入
        "SELECT * FROM users",        // 安全
    };

    for (int i = 0; i < 4; i++) {
        const char *sample = samples[i];
        struct libinjection_sqli_state state;
        int token_count = 0;

        double init_total = 0, fold_total = 0, lookup_total = 0;
        int iterations = 1000;

        for (int j = 0; j < iterations; j++) {
            void *timer;

            timer = timer_start();
            libinjection_sqli_init(&state, sample, strlen(sample), FLAG_NONE);
            libinjection_sqli_callback(&state, libinjection_sqli_lookup, NULL);
            init_total += timer_end(timer);

            timer = timer_start();
            token_count = libinjection_sqli_fold(&state);
            fold_total += timer_end(timer);

            timer = timer_start();
            state.lookup(&state, LOOKUP_FINGERPRINT, state.fingerprint,
                      strlen(state.fingerprint));
            lookup_total += timer_end(timer);
        }

        printf("样本: \"%s\"\n", sample);
        printf("  init+tokenize: %8.2f μs\n", init_total / iterations);
        printf("  fold:          %8.2f μs (Fold后Token: %d)\n", fold_total / iterations, token_count);
        printf("  lookup:       %8.2f μs\n", lookup_total / iterations);
        printf("  总计:          %8.2f μs\n\n", (init_total + fold_total + lookup_total) / iterations);
    }

    printf("✓ 测试完成\n");

    return 0;
}
