/**
 * @file kadd_feature_demo.c
 * @brief 特征转换交互式演示
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kadd_feature_engine.h"
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
void test_feature_conversion() {
    int feature_dim = 28;
    int *feature = (int *)calloc(feature_dim, sizeof(int));
    if (!feature) {
        printf("内存分配失败\n");
        return;
    }

    printf("请输入序列 (用逗号或空格分隔，直接回车使用默认值): ");

    char input[1024];
    fgets(input, sizeof(input), stdin);
    input[strcspn(input, "\n")] = 0;

    int *seq = NULL;
    int seq_len = 0;

    if (strlen(input) == 0) {
        // 默认序列
        int default_seq[] = {7, 11, 8, 12, 8, 11, 8};
        seq_len = 7;
        seq = (int *)malloc(seq_len * sizeof(int));
        if (seq) {
            memcpy(seq, default_seq, seq_len * sizeof(int));
            printf("使用默认序列: ");
            for (int i = 0; i < seq_len; i++) {
                printf("%d", seq[i]);
                if (i < seq_len - 1) printf(", ");
            }
            printf("\n");
        }
    } else {
        seq_len = parse_sequence(input, &seq);
        if (seq_len < 0) {
            printf("序列解析失败\n");
            free(feature);
            return;
        }
        printf("解析到 %d 个值\n", seq_len);
    }

    if (!seq) {
        free(feature);
        return;
    }

    printf("请输入窗口大小 (直接回车使用默认值5): ");

    char window_input[64];
    fgets(window_input, sizeof(window_input), stdin);
    window_input[strcspn(window_input, "\n")] = 0;

    int window_size = 5;
    if (strlen(window_input) > 0 && sscanf(window_input, "%d", &window_size) == 1) {
        if (window_size <= 0) {
            printf("窗口大小必须大于0，使用默认值5\n");
            window_size = 5;
        }
    } else {
        printf("使用默认窗口大小: 5\n");
    }

    printf("请输入输出特征维度 (直接回车使用默认值28): ");

    char dim_input[64];
    fgets(dim_input, sizeof(dim_input), stdin);
    dim_input[strcspn(dim_input, "\n")] = 0;

    if (strlen(dim_input) > 0 && sscanf(dim_input, "%d", &feature_dim) == 1) {
        if (feature_dim <= 0 || feature_dim > 1024) {
            printf("特征维度无效，使用默认值28\n");
            feature_dim = 28;
        }
    } else {
        printf("使用默认特征维度: 28\n");
    }

    void *timer = timer_start();
    int ret = kadd_seq_to_feature(seq, seq_len, window_size, feature, feature_dim);
    double elapsed = timer_end(timer);

    if (ret == 0) {
        printf("转换成功，耗时: %.2f μs\n", elapsed);
        printf("特征向量: [");
        for (int i = 0; i < feature_dim; i++) {
            printf("%d", feature[i]);
            if (i < feature_dim - 1) printf(", ");
        }
        printf("]\n");
    } else {
        printf("转换失败\n");
    }

    free(seq);
    free(feature);
}

// 性能测试
void test_performance() {
    int iterations = 0;
    printf("请输入测试次数 (100-100000, 直接回车使用默认值10000): ");

    char input[64];
    fgets(input, sizeof(input), stdin);
    input[strcspn(input, "\n")] = 0;

    if (strlen(input) == 0) {
        iterations = 10000;
        printf("使用默认测试次数: 10000\n");
    } else if (sscanf(input, "%d", &iterations) != 1 || iterations < 100 || iterations > 100000) {
        printf("测试次数无效，使用默认值10000\n");
        iterations = 10000;
    }

    int feature_dim = 28;
    int *feature = (int *)calloc(feature_dim, sizeof(int));
    int seq[] = {7, 11, 8, 12, 8, 11, 8};
    int seq_len = 7;
    int window_size = 5;

    if (!feature) {
        printf("内存分配失败\n");
        return;
    }

    printf("开始性能测试...\n");
    void *timer = timer_start();

    for (int i = 0; i < iterations; i++) {
        kadd_seq_to_feature(seq, seq_len, window_size, feature, feature_dim);
    }

    double elapsed = timer_end(timer);
    printf("总耗时: %.2f μs\n", elapsed);
    printf("平均耗时: %.3f μs/次\n", elapsed / iterations);
    printf("吞吐量: %.0f 次/秒\n", 1000.0 / (elapsed / iterations));

    free(feature);
}

// 批处理测试
void test_batch_feature() {
    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║                    批处理测试                                ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");

    int batch_count = 0;
    printf("请输入要处理的序列数量 (1-100, 直接回车使用默认值10): ");

    char input[64];
    fgets(input, sizeof(input), stdin);
    input[strcspn(input, "\n")] = 0;

    if (strlen(input) == 0) {
        batch_count = 10;
        printf("使用默认数量: 10\n");
    } else if (sscanf(input, "%d", &batch_count) != 1 || batch_count < 1 || batch_count > 100) {
        printf("数量无效，使用默认值10\n");
        batch_count = 10;
    }

    int feature_dim = 28;
    int window_size = 5;

    // 准备批处理数据
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

    // 读取序列
    printf("\n请输入 %d 个序列（用逗号或空格分隔，直接回车使用默认值）:\n", batch_count);
    for (int i = 0; i < batch_count; i++) {
        printf("[%d]> ", i + 1);
        char seq_input[1024];
        if (!fgets(seq_input, sizeof(seq_input), stdin)) break;
        seq_input[strcspn(seq_input, "\n")] = 0;

        if (strlen(seq_input) == 0) {
            // 使用默认序列
            int *default_seq = (int *)malloc(7 * sizeof(int));
            if (default_seq) {
                int temp[] = {7, 11, 8, 12, 8, 11, 8};
                memcpy(default_seq, temp, 7 * sizeof(int));
                seqs[i] = default_seq;
                seq_lens[i] = 7;
                printf("使用默认序列: [7, 11, 8, 12, 8, 11, 8]\n");
            }
        } else {
            seq_lens[i] = parse_sequence(seq_input, (int **)&seqs[i]);
            if (seq_lens[i] < 0) {
                printf("序列解析失败\n");
                seq_lens[i] = 0;
            } else {
                printf("解析到 %d 个值\n", seq_lens[i]);
            }
        }
    }

    // 执行批处理
    printf("\n正在执行批处理...\n");
    void *timer = timer_start();

    kadd_feature_batch_t batch = {
        .seqs = seqs,
        .seq_lens = seq_lens,
        .count = batch_count,
        .features = features,
        .feature_dim = feature_dim,
        .window_size = window_size,
        .batch_size = 32
    };

    int result = kadd_seq_to_feature_batch(&batch);
    double batch_time = timer_end(timer);

    // 打印结果
    printf("\n批处理完成！\n");
    printf("┌─────────────────────────────────────────────────────────────┐\n");
    printf("│ 批处理结果                                                  │\n");
    printf("├─────────────────────────────────────────────────────────────┤\n");
    printf("│ 总序列数:     %5d                                         │\n", batch_count);
    printf("│ 成功处理:     %5d                                         │\n", result > 0 ? result : 0);
    printf("│ 总耗时:       %8.2f μs                                 │\n", batch_time);
    printf("│ 平均耗时:     %8.2f μs/条                               │\n", batch_time / batch_count);
    printf("└─────────────────────────────────────────────────────────────┘\n\n");

    // 详细结果
    for (int i = 0; i < batch_count; i++) {
        printf("  [%d] 序列长度: %d, 特征前5维: [", i + 1, seq_lens[i]);
        int *feat = &features[i * feature_dim];
        for (int j = 0; j < 5 && j < feature_dim; j++) {
            printf("%d", feat[j]);
            if (j < 4) printf(", ");
        }
        printf("]\n");
    }

    // 清理
    for (int i = 0; i < batch_count; i++) {
        if (seq_lens[i] > 0) {
            free((void *)seqs[i]);
        }
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
                test_feature_conversion();
                break;
            case 2:
                test_performance();
                break;
            case 3:
                test_batch_feature();
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
