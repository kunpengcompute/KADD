/**
 * @file kadd_ml_demo.c
 * @brief ML 推理引擎使用示例
 *
 * 演示如何使用推理引擎进行模型加载、单次推理、批量推理和资源释放
 */

#include <stdio.h>
#include <stdlib.h>
#include "kadd_ml_engine.h"
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
    const char *model_path = (argc > 1) ? argv[1] : "assets/model/model_tokens_rf.onnx";
    const int feature_dim = 28;
    const int batch_size = 64;
    const int benchmark_iterations = 1000;

    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║           ML 推理引擎使用演示 (kadd_ml_engine)              ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");

    // ========== 步骤 1: 模型加载 ==========
    printf("【步骤 1】加载 ONNX 模型\n");
    printf("─────────────────────────────────────────────────────────────\n");

    kadd_model_handle_t *model = NULL;

    printf("模型路径: %s\n", model_path);
    printf("特征维度: %d\n", feature_dim);

    if (kadd_model_load(&model, model_path, feature_dim) != 0) {
        printf("✗ 模型加载失败\n");
        printf("请确保模型文件存在于: %s\n", model_path);
        return 1;
    }

    printf("✓ 模型加载成功\n\n");

    // ========== 步骤 2: 单次推理 ==========
    printf("【步骤 2】单样本推理\n");
    printf("─────────────────────────────────────────────────────────────\n");

    // 准备测试特征
    float *features = (float *)calloc(feature_dim, sizeof(float));
    if (!features) {
        printf("✗ 内存分配失败\n");
        kadd_model_free(model);
        /* 配置已内置，无需释放 */
        return 1;
    }

    float temp_features[28] = {0.0f, 152.0f, 0.0f, 0.0f, 0.0f, 0.0f, 185.0f, 1717.0f, 4152.0f, 0.0f, 0.0f, 4494.0f, 190.0f,
                               0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 129.0f, 0.0f, 0.0f, 0.0f, 0.0f, 173.0f, 0.0f, 0.0f, 0.0f};
    int copy_size = (feature_dim < 28) ? feature_dim : 28;
    memcpy(features, temp_features, copy_size * sizeof(float));

    printf("输入特征: ");
    for (int i = 0; i < feature_dim; i++) {
        printf("%.0f", features[i]);
        if (i < feature_dim - 1) printf(", ");
    }
    printf("\n");

    float probability = 0.0f;
    void *timer = timer_start();

    if (kadd_model_predict(model, features, &probability) == 0) {
        double elapsed = timer_end(timer);
        printf("✓ 推理成功\n");
        printf("预测概率 (Label=1): %.4f (%.2f%%)\n", probability, probability * 100);
        printf("推理耗时: %.3f ms\n\n", elapsed);
    } else {
        printf("✗ 推理失败\n\n");
        free(features);
        kadd_model_free(model);
        /* 配置已内置，无需释放 */
        return 1;
    }

    // 注意：features 后面还会用到，不要在这里释放

    // ========== 步骤 3: 批量推理 ==========
    printf("【步骤 3】批量推理\n");
    printf("─────────────────────────────────────────────────────────────\n");

    int batch_size = batch_size;
    float temp_batch[5][28] = {
        {0.0f, 152.0f, 0.0f, 0.0f, 0.0f, 0.0f, 185.0f, 1717.0f, 4152.0f, 0.0f, 0.0f, 4494.0f, 190.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 129.0f, 0.0f, 0.0f, 0.0f, 0.0f, 173.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1120.0f, 1456.0f, 0.0f, 0.0f, 2126.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 114.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 337.0f, 0.0f, 0.0f, 368.0f, 0.0f, 179.0f, 1424.0f, 3130.0f, 0.0f, 0.0f, 3350.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 129.0f, 0.0f, 0.0f, 0.0f, 0.0f, 173.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 926.0f, 1411.0f, 0.0f, 0.0f, 1568.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 114.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 712.0f, 0.0f, 0.0f, 368.0f, 0.0f, 179.0f, 1942.0f, 3605.0f, 0.0f, 0.0f, 4465.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 129.0f, 0.0f, 0.0f, 0.0f, 0.0f, 173.0f, 0.0f, 0.0f, 0.0f}
    };

    // 分配批量特征数组
    float *batch_features = (float *)malloc(batch_size * feature_dim * sizeof(float));
    if (!batch_features) {
        printf("✗ 内存分配失败\n");
        free(features);
        kadd_model_free(model);
        /* 配置已内置，无需释放 */
        return 1;
    }

    // 复制数据（最多复制5个样本，每个样本最多28维）
    int samples_to_copy = (batch_size < 5) ? batch_size : 5;
    int dims_to_copy = (feature_dim < 28) ? feature_dim : 28;
    for (int i = 0; i < samples_to_copy; i++) {
        for (int j = 0; j < dims_to_copy; j++) {
            batch_features[i * feature_dim + j] = temp_batch[i][j];
        }
    }

    // 准备输出概率数组
    float *probabilities = (float *)malloc(batch_size * sizeof(float));
    if (!probabilities) {
        printf("✗ 内存分配失败\n");
        free(batch_features);
        free(features);
        kadd_model_free(model);
        /* 配置已内置，无需释放 */
        return 1;
    }

    printf("批量大小: %d\n", batch_size);

    timer = timer_start();
    int count = kadd_model_predict_batch(model, (const float *)batch_features, batch_size, probabilities);
    double batch_elapsed = timer_end(timer);

    if (count > 0) {
        printf("✓ 批量推理成功 (%d/%d)\n", count, batch_size);
        printf("推理结果:\n");
        for (int i = 0; i < batch_size; i++) {
            printf("  样本 %d: %.4f (%.2f%%)\n", i + 1, probabilities[i], probabilities[i] * 100);
        }
        printf("总耗时: %.3f ms\n", batch_elapsed);
        printf("平均耗时: %.3f ms/样本\n", batch_elapsed / batch_size);
    } else {
        printf("✗ 批量推理失败\n");
    }

    free(probabilities);
    free(batch_features);

    // ========== 步骤 4: 性能基准测试 ==========
    {
        printf("\n【步骤 4】性能基准测试 (%d次推理)\n", benchmark_iterations);
        printf("─────────────────────────────────────────────────────────────\n");

        const int warmup_iterations = 10;
        float bench_prob;

        // 预热
        for (int i = 0; i < warmup_iterations; i++) {
            kadd_model_predict(model, features, &bench_prob);
        }

        // 基准测试
        timer = timer_start();
        for (int i = 0; i < benchmark_iterations; i++) {
            kadd_model_predict(model, features, &bench_prob);
        }
        double bench_elapsed = timer_end(timer);

        printf("总耗时: %.3f ms\n", bench_elapsed);
        printf("平均耗时: %.6f ms/次\n", bench_elapsed / benchmark_iterations);
        printf("吞吐量: %.0f 次/秒\n", 1000.0 / (bench_elapsed / benchmark_iterations));
    }

    // ========== 步骤 5: 资源释放 ==========
    printf("\n【步骤 5】释放模型资源\n");
    printf("─────────────────────────────────────────────────────────────\n");

    free(features);
    kadd_model_free(model);
    printf("✓ 模型资源已释放\n");

    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║                     演示完成                                ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");

    /* 配置已内置，无需释放 */

    return 0;
}
