/**
 * @file kadd_feature_demo.c
 * @brief 特征转换功能与性能示例
 *
 * 演示如何使用 kadd_seq_to_feature 接口将不定长序列转换为固定维度特征向量
 */

#include <stdio.h>
#include <stdlib.h>
#include "kadd_feature_engine.h"
#include "tools.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

int main(int argc, char *argv[])
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    /* 默认配置 */
    const int window_size = 5;
    const int feature_dim = 28;
    const int performance_iterations = 10000;

    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║        特征转换功能与性能演示 (kadd_feature_engine)       ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");

    // ========== 示例 1: 基本功能演示 ==========
    printf("【示例 1】基本功能演示\n");
    printf("─────────────────────────────────────────────────────────────\n");

    int seq1[] = {7, 11, 8, 12, 8, 11, 8};
    int seq_len = sizeof(seq1) / sizeof(seq1[0]);
    int *feature = (int *)calloc(feature_dim, sizeof(int));
    if (!feature) {
        printf("✗ 内存分配失败\n");
        return 1;
    }

    printf("输入序列: ");
    print_array(seq1, seq_len);
    printf(" (长度=%d)\n", seq_len);
    printf("窗口大小: %d\n", window_size);
    printf("输出维度: %d\n", feature_dim);

    int ret = kadd_seq_to_feature(seq1, seq_len, window_size, feature, feature_dim);

    if (ret == 0) {
        printf("✓ 转换成功\n");
        printf("特征向量: ");
        print_array(feature, feature_dim);
        printf("\n");
        free(feature);
    } else {
        printf("✗ 转换失败\n");
        free(feature);
        return 1;
    }

    // ========== 示例 2: 不同序列长度 ==========
    printf("\n【示例 2】不同序列长度测试\n");
    printf("─────────────────────────────────────────────────────────────\n");

    int test_cases[][20] = {
        {1, 2, 3},                                    // 短序列
        {5, 10, 15, 20, 25, 30, 35},                  // 中等序列
        {100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100, 1200}  // 长序列
    };
    int test_lengths[] = {3, 7, 12};

    for (int i = 0; i < 3; i++) {
        int *f = (int *)calloc(feature_dim, sizeof(int));
        if (!f) continue;

        printf("测试 %d: 长度=%d, ", i + 1, test_lengths[i]);
        if (kadd_seq_to_feature(test_cases[i], test_lengths[i], window_size, f, feature_dim) == 0) {
            printf("结果: [");
            for (int j = 0; j < 10 && j < feature_dim; j++) {  // 只显示前10个
                printf("%d", f[j]);
                if (j < 9 && j < feature_dim - 1) printf(", ");
            }
            printf("...]\n");
        }
        free(f);
    }

    // ========== 示例 3: 性能测试 ==========
    printf("\n【示例 3】性能测试 (%d次转换)\n", performance_iterations);
    printf("─────────────────────────────────────────────────────────────\n");

    const int ITERATIONS = performance_iterations;
    void *timer = timer_start();

    int *f = (int *)calloc(feature_dim, sizeof(int));
    if (!f) {
        printf("✗ 内存分配失败\n");
        /* 配置已内置，无需释放 */
        return 1;
    }

    for (int i = 0; i < ITERATIONS; i++) {
        kadd_seq_to_feature(seq1, seq_len, window_size, f, feature_dim);
    }

    double elapsed = timer_end(timer);
    double avg_time = elapsed / ITERATIONS;

    printf("总耗时: %.3f ms\n", elapsed);
    printf("平均耗时: %.6f ms/次\n", avg_time);
    printf("吞吐量: %.0f 次/秒\n", 1000.0 / avg_time);

    free(f);

    // ========== 示例 4: 边界条件测试 ==========
    printf("\n【示例 4】边界条件测试\n");
    printf("─────────────────────────────────────────────────────────────\n");

    struct {
        const char *name;
        const int *seq;
        int len;
        int window;
        int expected;
    } boundary_tests[] = {
        {"空序列", NULL, 0, 5, -1},
        {"NULL特征向量", seq1, seq_len, 5, -1},
        {"窗口大小=0", seq1, seq_len, 0, -1},
        {"窗口大小=1", seq1, seq_len, 1, 0},
        {"窗口大小>序列长度", seq1, seq_len, 100, 0},
    };

    int *f = (int *)calloc(feature_dim, sizeof(int));
    if (!f) {
        printf("✗ 内存分配失败\n");
    } else {
        int passed = 0;
        for (int i = 0; i < 5; i++) {
            // 对于 NULL 特征向量的测试，跳过
            if (i == 1) continue;

            int result = kadd_seq_to_feature(
                boundary_tests[i].seq,
                boundary_tests[i].len,
                boundary_tests[i].window,
                f, feature_dim
            );

            if (result == boundary_tests[i].expected) {
                printf("✓ %s: 通过\n", boundary_tests[i].name);
                passed++;
            } else {
                printf("✗ %s: 失败 (期望=%d, 实际=%d)\n",
                       boundary_tests[i].name,
                       boundary_tests[i].expected, result);
            }
        }
        printf("\n边界测试通过: %d/4\n", passed);
        free(f);
    }

    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║                     演示完成                                ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");

    /* 配置已内置，无需释放 */

    return 0;
}
