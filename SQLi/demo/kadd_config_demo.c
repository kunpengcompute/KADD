/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *
 * @file kadd_config_demo.c
 * @brief KADD 配置加载器 - 简单示例
 *
 * 构建：由顶层 CMake 一并编译（cmake .. && make，详见 docs/zh/SQLi/installation_guide.md）
 * 运行：cd build/demo && ./kadd_config_demo
 */

#include <stdio.h>
#include "kadd_config_loader.h"

int main()
{
    printf("=== KADD 配置加载器示例 ===\n\n");

    kadd_config_t config;

    // 加载配置
    printf("加载配置文件: ../config.json\n");
    if (kadd_config_load("../config.json", &config) == KADD_SUCCESS) {
        printf("\n✅ 配置加载成功\n\n");

        printf("DFA 引擎:\n");
        printf("  转移表路径: %s\n", config.dfa.transition_table_path);
        printf("  最大输入长度: %d\n", config.dfa.max_input_length);

        printf("\nML 引擎:\n");
        printf("  模型路径: %s\n", config.ml.model_path);
        printf("  特征维度: %d\n", config.ml.feature_dim);

        printf("\nSQLi 引擎:\n");
        printf("  特征维度: %d\n", config.sqli.feature_dim);
    } else {
        printf("❌ 配置加载失败\n");
    }

    return 0;
}
