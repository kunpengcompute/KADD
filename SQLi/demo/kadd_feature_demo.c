/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *
 * @file kadd_feature_demo.c
 * @brief KADD 特征提取引擎 - 简单示例
 *
 * 构建：由顶层 CMake 一并编译（cmake .. && make，详见 docs/zh/SQLi/installation_guide.md）
 * 运行：cd build/demo && ./kadd_feature_demo
 */

#include <stdio.h>
#include "kadd_feature_engine.h"
#include "kadd_config_loader.h"

int main()
{
    printf("=== KADD 特征提取引擎示例 ===\n\n");

    // 1. 加载配置
    printf("[1] 加载配置...\n");
    kadd_config_t config;
    if (kadd_config_load("../config.json", &config) != KADD_SUCCESS) {
        printf("❌ 配置加载失败\n");
        return 1;
    }
    printf("✅ 配置加载成功\n\n");

    // 2. 创建模拟 Token 序列
    printf("[2] 创建 Token 序列...\n");
    int seq[] = {7, 11, 8, 12, 8, 11, 8};  // 假设的 token ID 序列
    int seq_len = 7;

    printf("Token 序列 (%d 个):\n", seq_len);
    for (int i = 0; i < seq_len; i++) {
        printf("  [%d] Token ID: %d\n", i + 1, seq[i]);
    }
    printf("\n");

    // 3. 提取特征
    printf("[3] 提取特征...\n");
    int features[config.feature.feature_dim];
    for (int i = 0; i < config.feature.feature_dim; i++) {
        features[i] = 0;
    }

    if (kadd_seq_to_feature(seq, seq_len, config.feature.window_size,
                           features, config.feature.feature_dim) == KADD_SUCCESS) {
        printf("特征向量 (%d 维):\n", config.feature.feature_dim);
        printf("[");
        for (int i = 0; i < config.feature.feature_dim; i++) {
            printf("%d ", features[i]);
        }
        printf("]\n");
        printf("✅ 特征提取成功\n\n");
    } else {
        printf("❌ 特征提取失败\n\n");
    }

    // 4. 清理
    printf("[4] 清理资源...\n");
    kadd_config_free(&config);
    printf("✅ 完成\n");

    return 0;
}
