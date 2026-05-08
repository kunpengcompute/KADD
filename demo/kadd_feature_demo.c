/**
 * @file kadd_feature_demo.c
 * @brief 特征转换交互式演示
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kadd_feature_engine.h"
#include "kadd_config_loader.h"
#include "tools.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

// 解析整数序列
int parse_sequence(const char *input, int **seq_out) {
    int capacity = 32;
    int *seq = (int *)malloc(capacity * sizeof(int));
    if (!seq) return -1;

    int len = 0;
    char *token = strtok((char *)input, ", ");
    while (token != NULL) {
        if (len >= capacity) {
            capacity *= 2;
            int *new_seq = (int *)realloc(seq, capacity * sizeof(int));
            if (!new_seq) {
                free(seq);
                return -1;
            }
            seq = new_seq;
        }

        if (sscanf(token, "%d", &seq[len]) == 1) {
            len++;
        }
        token = strtok(NULL, ", ");
    }

    *seq_out = seq;
    return len;
}

// 特征转换测试
void test_feature_conversion(const kadd_config_t *config) {
    int seq_len;
    printf("请输入序列长度: ");
    scanf("%d", &seq_len);
    getchar();

    int *seq = NULL;
    printf("请输入序列 (用逗号或空格分隔): ");
    char input[4096];
    fgets(input, sizeof(input), stdin);
    input[strcspn(input, "\n")] = 0;
    parse_sequence(input, &seq);

    int window_size;
    printf("请输入窗口大小: ");
    scanf("%d", &window_size);
    getchar();

    int feature_dim;
    printf("请输入输出特征维度: ");
    scanf("%d", &feature_dim);
    getchar();

    int *feature = (int *)calloc(feature_dim, sizeof(int));
    if (!feature) {
        printf("内存分配失败\n");
        free(seq);
        return;
    }

    void *timer = timer_start();
    int ret = kadd_seq_to_feature(seq, seq_len, window_size, feature, feature_dim);
    double elapsed = timer_end(timer);

    printf("返回值: %d\n", ret);

    if (ret >= 0) {
        printf("耗时: %.2f μs\n", elapsed);
        printf("特征向量: [");
        for (int i = 0; i < feature_dim; i++) {
            printf("%d", feature[i]);
            if (i < feature_dim - 1) printf(", ");
        }
        printf("]\n");
    }

    free(seq);
    free(feature);
}

// 性能测试
void test_performance(const kadd_config_t *config) {
    int iterations;
    printf("请输入测试次数: ");
    scanf("%d", &iterations);
    getchar();

    int seq_len;
    printf("请输入序列长度: ");
    scanf("%d", &seq_len);
    getchar();

    int *seq = NULL;
    printf("请输入序列: ");
    char input[4096];
    fgets(input, sizeof(input), stdin);
    input[strcspn(input, "\n")] = 0;
    parse_sequence(input, &seq);

    int window_size;
    printf("请输入窗口大小: ");
    scanf("%d", &window_size);
    getchar();

    int feature_dim;
    printf("请输入特征维度: ");
    scanf("%d", &feature_dim);
    getchar();

    int *feature = (int *)calloc(feature_dim, sizeof(int));
    if (!feature) {
        printf("内存分配失败\n");
        free(seq);
        return;
    }

    printf("开始性能测试...\n");
    void *timer = timer_start();

    for (int i = 0; i < iterations; i++) {
        kadd_seq_to_feature(seq, seq_len, window_size, feature, feature_dim);
    }

    double elapsed = timer_end(timer);
    int ret = kadd_seq_to_feature(seq, seq_len, window_size, feature, feature_dim);

    printf("返回值: %d\n", ret);
    printf("总耗时: %.2f μs\n", elapsed);
    printf("平均耗时: %.3f μs/次\n", elapsed / iterations);

    free(seq);
    free(feature);
}

// 批处理测试
void test_batch_feature(const kadd_config_t *config) {
    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║                    批处理测试                                ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");

    int batch_count;
    printf("请输入序列数量: ");
    scanf("%d", &batch_count);
    getchar();

    int feature_dim;
    printf("请输入特征维度: ");
    scanf("%d", &feature_dim);
    getchar();

    int window_size;
    printf("请输入窗口大小: ");
    scanf("%d", &window_size);
    getchar();

    const int **seqs = (const int **)malloc(batch_count * sizeof(int *));
    int *seq_lens = (int *)malloc(batch_count * sizeof(int));
    int *features = (int *)calloc(batch_count * feature_dim, sizeof(int));

    if (!seqs || !seq_lens || !features) {
        printf("内存分配失败\n");
        free(seqs);
        free(seq_lens);
        free(features);
        return;
    }

    printf("\n请输入 %d 个序列:\n", batch_count);
    for (int i = 0; i < batch_count; i++) {
        printf("[%d]> ", i + 1);
        char seq_input[1024];
        if (!fgets(seq_input, sizeof(seq_input), stdin)) break;
        seq_input[strcspn(seq_input, "\n")] = 0;
        seq_lens[i] = parse_sequence(seq_input, (int **)&seqs[i]);
    }

    kadd_feature_batch_t batch = {
        .seqs = seqs,
        .seq_lens = seq_lens,
        .count = batch_count,
        .features = features,
        .feature_dim = feature_dim,
        .window_size = window_size,
        .batch_size = 32
    };

    printf("\n正在执行批处理...\n");
    void *timer = timer_start();
    int result = kadd_seq_to_feature_batch(&batch);
    double batch_time = timer_end(timer);

    printf("返回值: %d\n", result);

    if (result >= 0) {
        printf("\n批处理完成！\n");
        printf("┌─────────────────────────────────────────────────────────────┐\n");
        printf("│ 批处理结果                                                  │\n");
        printf("├─────────────────────────────────────────────────────────────┤\n");
        printf("│ 总序列数:     %5d                                         │\n", batch_count);
        printf("│ 成功处理:     %5d                                         │\n", result);
        printf("│ 总耗时:       %8.2f μs                                 │\n", batch_time);
        printf("│ 平均耗时:     %8.2f μs/条                               │\n", batch_time / batch_count);
        printf("└─────────────────────────────────────────────────────────────┘\n\n");

        for (int i = 0; i < batch_count; i++) {
            printf("  [%d] 序列长度: %d, 特征前5维: [", i + 1, seq_lens[i]);
            int *feat = &features[i * feature_dim];
            for (int j = 0; j < 5 && j < feature_dim; j++) {
                printf("%d", feat[j]);
                if (j < 4) printf(", ");
            }
            printf("]\n");
        }
    }

    for (int i = 0; i < batch_count; i++) {
        free((void *)seqs[i]);
    }
    free(seqs);
    free(seq_lens);
    free(features);

    printf("\n批处理测试完成\n");
}

// 主菜单
void show_main_menu() {
    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║           特征转换交互式测试 (kadd_seq_to_feature)           ║\n");
    printf("╠════════════════════════════════════════════════════════════╣\n");
    printf("║  1. 特征转换测试                                            ║\n");
    printf("║  2. 性能测试                                                ║\n");
    printf("║  3. 批处理测试                                              ║\n");
    printf("║ 0. 退出程序                                               ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
}

int main() {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    // 加载配置
    kadd_config_t config;
    if (kadd_config_load(NULL, &config) != 0) {
        printf("警告: 无法加载 config.json，使用默认配置\n");
        config.ml.feature_dim = 28;
        config.ml.window_size = 5;
    }

    int running = 1;

    while (running) {
        show_main_menu();
        printf("请选择操作 (0-3): ");

        char input[64];
        if (fgets(input, sizeof(input), stdin) == NULL) break;

        int choice;
        if (sscanf(input, "%d", &choice) != 1) {
            printf("输入无效，请输入数字\n");
            continue;
        }

        switch (choice) {
            case 1:
                test_feature_conversion(&config);
                break;
            case 2:
                test_performance(&config);
                break;
            case 3:
                test_batch_feature(&config);
                break;
            case 0:
                running = 0;
                printf("退出程序\n");
                break;
            default:
                printf("无效选择，请输入 0-3\n");
                break;
        }
    }

    return 0;
}
