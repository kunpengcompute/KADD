/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *
 * @file kadd_inference_demo.c
 * @brief KADD 推理引擎示例 - 展示 RF/MLP/CNN 三种模型
 *
 * 构建：由顶层 CMake 一并编译（cmake .. && make）
 * 运行：cd build/demo && ./kadd_inference_demo
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kadd_inference_engine.h"

/* RF/MLP 测试特征（28维） */
static const float g_test_features_28[][28] = {
    {100, 200, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {500, 300, 200, 100, 50, 25, 10, 5, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
};

/* CNN 测试特征（300维 token 序列，值范围 0-28） */
static float g_test_features_300[300];

/* 初始化 CNN 测试数据 */
static void init_cnn_test_data(void)
{
    /* 模拟 SQL 注入 token 序列：SELECT(7) * (8) FROM(11) WHERE(12) ... */
    int sql_tokens[] = {7, 8, 11, 12, 0, 1, 7, 8, 11, 12, 5, 6};
    int token_count = sizeof(sql_tokens) / sizeof(sql_tokens[0]);

    for (int i = 0; i < 300; i++) {
        g_test_features_300[i] = (float)sql_tokens[i % token_count];
    }
}

/* 单模型推理测试 */
static int test_single_model(const char *config, kadd_inference_type_t type, const char *name)
{
    printf("\n=== 测试 %s 模型 ===\n", name);

    kadd_inference_engine_t *engine = NULL;
    int ret = kadd_inference_engine_load(&engine, config, type);

    if (ret != 0) {
        printf("❌ 模型加载失败: ret=%d\n", ret);
        return -1;
    }
    printf("✅ 模型加载成功\n");

    int feature_dim = kadd_inference_engine_get_feature_dim(engine);
    printf("  feature_dim = %d\n", feature_dim);

    /* 选择合适的测试数据 */
    const float *features;
    int test_count;

    if (feature_dim == 28) {
        features = (const float *)g_test_features_28;
        test_count = 3;
    } else if (feature_dim == 300) {
        init_cnn_test_data();
        features = g_test_features_300;
        test_count = 1;
    } else {
        printf("❌ 未知的 feature_dim: %d\n", feature_dim);
        kadd_inference_engine_free(engine);
        return -1;
    }

    /* 单样本推理 */
    printf("\n--- 单样本推理 ---\n");
    for (int i = 0; i < test_count; i++) {
        float prob = 0.0f;
        ret = kadd_inference_predict(engine, features + i * feature_dim, &prob);
        if (ret != 0) {
            printf("❌ 推理失败: ret=%d\n", ret);
            kadd_inference_engine_free(engine);
            return -1;
        }
        printf("  sample[%d] -> prob=%.4f\n", i, prob);
    }

    /* 批量推理 */
    printf("\n--- 批量推理 ---\n");
    float *probs = (float *)calloc(test_count, sizeof(float));
    if (!probs) {
        kadd_inference_engine_free(engine);
        return -1;
    }

    ret = kadd_inference_predict_batch(engine, features, test_count, probs, 2);
    if (ret != 0) {
        printf("❌ 批量推理失败: ret=%d\n", ret);
        free(probs);
        kadd_inference_engine_free(engine);
        return -1;
    }

    for (int i = 0; i < test_count; i++) {
        printf("  batch[%d] -> prob=%.4f\n", i, probs[i]);
    }

    free(probs);
    kadd_inference_engine_free(engine);
    printf("✅ 引擎释放成功\n");

    return 0;
}

int main(int argc, char *argv[])
{
    const char *config = (argc >= 2) ? argv[1] : "../config.json";
    printf("=== KADD 推理引擎示例 (config=%s) ===\n", config);

    int errors = 0;

    /* 测试 RF */
    errors += test_single_model(config, KADD_INFERENCE_TYPE_RF, "RF");

    /* 测试 CNN */
    errors += test_single_model(config, KADD_INFERENCE_TYPE_CNN, "CNN");

    printf("\n=== 结果: %s (错误数: %d) ===\n",
           errors == 0 ? "全部通过" : "存在错误", errors);

    return (errors == 0) ? 0 : 1;
}