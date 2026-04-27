/**
 * @file kadd_sqli_demo.c
 * @brief SQL 注入检测引擎使用示例
 *
 * 演示如何使用 kadd_sqli_engine 进行完整的 SQL 注入检测流程
 * 整合 DFA 分词 + 特征提取 + ML 推理
 *
 * 使用方式:
 *   ./kadd_sqli_demo                    # 使用默认配置
 *   ./kadd_sqli_demo config.json        # 使用配置文件
 *   ./kadd_sqli_demo config.json "test" # 检测指定字符串
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kadd_sqli_engine.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

// 简单计时器
void *timer_start() {
#ifdef _WIN32
    LARGE_INTEGER *start = malloc(sizeof(LARGE_INTEGER));
    QueryPerformanceCounter(start);
    return start;
#else
    struct timeval *start = malloc(sizeof(struct timeval));
    gettimeofday(start, NULL);
    return start;
#endif
}

double timer_end(void *handle) {
#ifdef _WIN32
    LARGE_INTEGER end, freq, *start = (LARGE_INTEGER *)handle;
    QueryPerformanceCounter(&end);
    QueryPerformanceFrequency(&freq);
    free(handle);
    return ((double)(end.QuadPart - start->QuadPart) * 1000.0 / freq.QuadPart);
#else
    struct timeval end, *start = (struct timeval *)handle;
    gettimeofday(&end, NULL);
    free(handle);
    return ((double)(end.tv_sec - start->tv_sec) * 1000.0 + (double)(end.tv_usec - start->tv_usec) / 1000.0);
#endif
}

// SQL 注入测试样本
static const struct {
    const char *input;
    const char *description;
    int expected;
} test_samples[] = {
    // SQL 注入样本
    {"id=1' OR '1'='1", "经典 OR 注入", 1},
    {"id=1; DROP TABLE users--", "DROP TABLE 注入", 1},
    {"admin'--", "注释绕过", 1},
    {"1' UNION SELECT NULL--", "UNION 注入", 1},
    {"' OR 1=1--", "布尔注入", 1},
    {"' OR SLEEP(5)--", "时间盲注", 1},
    {"search=test%' AND '1'='1", "百分比编码注入", 1},
    {"id=1`WHERE`1=1", "反引号注入", 1},

    // 正常样本
    {"SELECT * FROM users WHERE id=1", "正常 SQL 查询", 0},
    {"INSERT INTO users (name) VALUES ('John')", "正常 INSERT", 0},
    {"UPDATE users SET name='Jane' WHERE id=1", "正常 UPDATE", 0},
    {"user@example.com", "邮箱地址", 0},
    {"Hello World 123", "普通文本", 0},
    {"price=99.99", "价格参数", 0},
    {"name=John+Doe", "姓名参数", 0},
    {"category=electronics", "分类参数", 0},
};

/**
 * @brief 检测单个输入并打印结果
 */
static void detect_and_print(kadd_sqli_engine_t *engine, const char *input, const char *description) {
    float probability;
    void *timer = timer_start();

    int ret = kadd_sqli_detect(engine, input, &probability);
    double elapsed = timer_end(timer);

    if (ret == KADD_SQLI_OK) {
        const char *status = (probability >= 0.5f) ? "[SQL 注入]" : "[安全]";
        printf("  %-35s | %-15s | %.6f ms | %.2f%%\n",
               input, status, elapsed, probability * 100);
    } else {
        printf("  %-35s | [检测失败]      | -         | 错误码: %d\n",
               input, ret);
    }
}

/**
 * @brief 打印使用帮助
 */
static void print_usage(const char *prog_name) {
    printf("使用方式:\n");
    printf("  %s                    # 使用默认配置运行完整演示\n", prog_name);
    printf("  %s <config.json>      # 使用配置文件运行完整演示\n", prog_name);
    printf("  %s <config.json> \"test\"  # 检测指定字符串\n\n", prog_name);
    printf("示例:\n");
    printf("  %s\n", prog_name);
    printf("  %s config.json\n", prog_name);
    printf("  %s config.json \"id=1' OR '1'='1\"\n\n", prog_name);
}

int main(int argc, char *argv[])
{
#ifdef _WIN32
    /* 设置控制台为UTF-8编码 */
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║        SQL 注入检测引擎演示 (kadd_sqli_engine)            ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");

    // ========== 检查命令行参数 ==========
    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        print_usage(argv[0]);
        return 0;
    }

    // ========== 步骤 1: 创建引擎 ==========
    printf("【步骤 1】创建 SQL 注入检测引擎\n");
    printf("─────────────────────────────────────────────────────────────\n");

    kadd_sqli_engine_t *engine = NULL;
    const char *config_file = "config.json";

    // 判断运行模式
    int quick_test_mode = 0;
    const char *test_string = NULL;

    if (argc == 1) {
        // 无参数：使用默认配置文件
        printf("模式: 使用默认配置文件 (config.json)\n");
        printf("提示: 使用 '%s <config.json>' 来指定配置文件\n", argv[0]);
        printf("提示: 使用 '%s <config.json> \"test\"' 来检测指定字符串\n", argv[0]);
        printf("\n");

        kadd_sqli_engine_create_from_file(&engine, "config.json");

    } else if (argc == 2) {
        // 1个参数：可能是配置文件或测试字符串
        if (strstr(argv[1], ".json")) {
            // 配置文件
            config_file = argv[1];
            printf("模式: 从配置文件创建引擎\n");
            printf("配置文件: %s\n", config_file);
            printf("\n");
            kadd_sqli_engine_create_from_file(&engine, config_file);
        } else {
            // 测试字符串（使用默认配置文件）
            test_string = argv[1];
            quick_test_mode = 1;
            printf("模式: 快速检测模式\n");
            printf("测试字符串: %s\n", test_string);
            printf("使用默认配置文件: config.json\n");
            printf("\n");

            kadd_sqli_engine_create_from_file(&engine, "config.json");
        }

    } else if (argc >= 3) {
        // 2个以上参数：配置文件 + 测试字符串
        config_file = argv[1];
        test_string = argv[2];
        quick_test_mode = 1;
        printf("模式: 快速检测模式（使用配置文件）\n");
        printf("配置文件: %s\n", config_file);
        printf("测试字符串: %s\n", test_string);
        printf("\n");
        kadd_sqli_engine_create_from_file(&engine, config_file);
    }

    if (!engine) {
        printf("✗ 引擎创建失败\n");
        printf("请检查:\n");
        printf("  1. DFA 表文件是否存在于 tools/py/profile_data/\n");
        printf("  2. ONNX 模型是否存在于 assets/model/\n");
        printf("  3. 配置文件路径是否正确: %s\n", config_file);
        return 1;
    }

    printf("✓ 引擎创建成功\n\n");

    // ========== 快速检测模式 ==========
    if (quick_test_mode) {
        printf("【步骤 2】快速检测\n");
        printf("─────────────────────────────────────────────────────────────\n");
        detect_and_print(engine, test_string, "用户输入");
        goto cleanup;
    }

    // ========== 完整演示模式 ==========
    printf("【步骤 2】单样本检测演示\n");
    printf("─────────────────────────────────────────────────────────────\n");

    const char *demo_input = "id=1' OR '1'='1";
    printf("测试输入: %s\n", demo_input);
    detect_and_print(engine, demo_input, "演示样本");
    printf("\n");

    // ========== 步骤 3: 批量检测 ==========
    printf("【步骤 3】批量检测演示 (%d个样本)\n", (int)(sizeof(test_samples) / sizeof(test_samples[0])));
    printf("─────────────────────────────────────────────────────────────\n");

    int num_samples = sizeof(test_samples) / sizeof(test_samples[0]);
    int correct = 0;

    printf("%-35s | %-15s | %-10s | %s\n", "输入", "检测结果", "概率", "预期");
    printf("─────────────────────────────────────────────────────────────\n");

    for (int i = 0; i < num_samples; i++) {
        float prob;
        if (kadd_sqli_detect(engine, test_samples[i].input, &prob) == KADD_SQLI_OK) {
            int is_sqli = (prob >= 0.5f) ? 1 : 0;
            const char *result_str = is_sqli ? "[SQL 注入]" : "[安全]";
            const char *expected_str = test_samples[i].expected ? "[SQL 注入]" : "[安全]";
            const char *match = (is_sqli == test_samples[i].expected) ? "✓" : "✗";

            printf("%-35s | %-15s | %8.2f%% | %s %s\n",
                   test_samples[i].input, result_str, prob * 100, match, expected_str);

            if (is_sqli == test_samples[i].expected) {
                correct++;
            }
        }
    }

    printf("─────────────────────────────────────────────────────────────\n");
    printf("准确率: %d/%d (%.1f%%)\n\n", correct, num_samples, (float)correct / num_samples * 100);

    // ========== 步骤 4: 性能测试 ==========
    printf("【步骤 4】性能测试 (1000次检测)\n");
    printf("─────────────────────────────────────────────────────────────\n");

    const int PERF_ITERATIONS = 1000;
    const char *perf_input = "id=1' OR '1'='1";

    void *timer = timer_start();
    for (int i = 0; i < PERF_ITERATIONS; i++) {
        float prob;
        kadd_sqli_detect(engine, perf_input, &prob);
    }
    double perf_elapsed = timer_end(timer);

    printf("总耗时: %.3f ms\n", perf_elapsed);
    printf("平均耗时: %.6f ms/次\n", perf_elapsed / PERF_ITERATIONS);
    printf("吞吐量: %.0f 次/秒\n", 1000.0 / (perf_elapsed / PERF_ITERATIONS));
    printf("\n");

    // ========== 步骤 5: 交互式检测 ==========
    printf("【步骤 5】交互式检测模式\n");
    printf("─────────────────────────────────────────────────────────────\n");
    printf("输入要检测的文本 (输入 'quit' 或 'exit' 退出):\n\n");

    char input[512];
    while (fgets(input, sizeof(input), stdin)) {
        // 去除换行符
        input[strcspn(input, "\n")] = '\0';

        // 退出检测
        if (strcmp(input, "quit") == 0 || strcmp(input, "exit") == 0) {
            break;
        }

        // 跳过空行
        if (strlen(input) == 0) {
            continue;
        }

        detect_and_print(engine, input, "用户输入");
    }

cleanup:
    // ========== 步骤 6: 释放资源 ==========
    printf("\n【步骤 6】释放引擎资源\n");
    printf("─────────────────────────────────────────────────────────────\n");

    kadd_sqli_engine_free(engine);
    printf("✓ 引擎资源已释放\n");

    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║                     演示完成                                ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");

    return 0;
}
