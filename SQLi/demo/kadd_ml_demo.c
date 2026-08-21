/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *
 * @file kadd_ml_demo.c
 * @brief KADD ML 推理引擎 - 简单示例
 *
 * 构建：由顶层 CMake 一并编译（cmake .. && make，详见 docs/zh/SQLi/installation_guide.md）
 * 运行：cd build/demo && ./kadd_ml_demo
 */

#include <stdio.h>
#include "kadd_ml_engine.h"
#include "kadd_config_loader.h"

int main()
{
    printf("=== KADD ML 推理引擎示例 ===\n\n");

    // 1. 加载配置
    printf("[1] 加载配置...\n");
    kadd_config_t config;
    if (kadd_config_load("../config.json", &config) != KADD_SUCCESS) {
        printf("❌ 配置加载失败\n");
        return 1;
    }
    printf("✅ 配置加载成功\n\n");

    // 2. 加载模型
    printf("[2] 加载模型...\n");
    kadd_model_handle_t *model = NULL;
    // feature_dim=0 表示从模型自动读取特征维度
    if (kadd_model_load(&model, config.model_rf, 0) != KADD_SUCCESS) {
        printf("❌ 模型加载失败\n");
        kadd_config_free(&config);
        return 1;
    }
    printf("✅ 模型加载成功\n\n");

    // 获取模型的特征维度
    int feature_dim = kadd_model_get_feature_dim(model);
    printf("模型特征维度: %d\n\n", feature_dim);

    // 3. 推理测试
    printf("[3] 推理测试...\n");

    // 创建模拟特征向量
    float features[feature_dim];
    for (int i = 0; i < feature_dim; i++) {
        features[i] = 0;
    }
    // 设置模拟特征值
    features[7] = 100;
    features[8] = 436;
    features[11] = 250;
    features[12] = 160;

    float probability = 0.0f;
    if (kadd_model_predict(model, features, &probability) == KADD_SUCCESS) {
        printf("输入特征: [");
        for (int i = 0; i < feature_dim; i++) {
            printf("%.2f ", features[i]);
        }
        printf("]\n");
        printf("注入概率: %.2f%%\n", probability * 100);
        printf("✅ 推理成功\n\n");
    } else {
        printf("❌ 推理失败\n\n");
    }

    // 4. 清理
    printf("[4] 清理资源...\n");
    kadd_model_free(model);
    kadd_config_free(&config);
    printf("✅ 完成\n");

    return 0;
}
