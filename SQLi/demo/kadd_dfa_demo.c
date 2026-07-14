/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *
 * @file kadd_dfa_demo.c
 * @brief KADD DFA 分词引擎 - 简单示例
 *
 * 构建：由顶层 CMake 一并编译（cmake .. && make，详见 docs/zh/SQLi/installation_guide.md）
 * 运行：cd build/demo && ./kadd_dfa_demo
 */

#include <stdio.h>
#include <string.h>
#include "kadd_dfa_engine.h"
#include "kadd_config_loader.h"

int main()
{
    printf("=== KADD DFA 分词引擎示例 ===\n\n");

    // 1. 加载配置
    printf("[1] 加载配置...\n");
    kadd_config_t config;
    if (kadd_config_load("../config.json", &config) != KADD_SUCCESS) {
        printf("❌ 配置加载失败\n");
        return 1;
    }
    printf("✅ 配置加载成功\n\n");

    // 2. 创建引擎
    printf("[2] 创建 DFA 引擎...\n");
    kadd_dfa_engine_t *engine = NULL;

    kadd_dfa_table_paths_t table_paths = {
        .transition_table_path = config.dfa.transition_table_path,
        .output_table_path = config.dfa.output_table_path,
        .eod_table_path = config.dfa.eod_table_path
    };

    if (kadd_dfa_engine_create(&engine, &table_paths,
                               config.dfa.max_input_length,
                               config.dfa.max_output_dim) != KADD_SUCCESS) {
        printf("❌ 引擎创建失败\n");
        kadd_config_free(&config);
        return 1;
    }
    printf("✅ 引擎创建成功\n\n");

    // 3. 分词测试
    printf("[3] 分词测试...\n");
    const char *input = "SELECT * FROM users WHERE id = 1";
    printf("输入: %s\n\n", input);

    int tokens[128];
    int token_count = 0;

    if (kadd_dfa_engine_tokenize(engine, input, strlen(input), tokens, &token_count) == KADD_SUCCESS) {
        printf("分词结果 (%d 个 Token):\n", token_count);
        for (int i = 0; i < token_count; i++) {
            printf("  [%d] Token 索引: %d\n", i + 1, tokens[i]);
        }
        printf("✅ 分词成功\n");
    } else {
        printf("❌ 分词失败\n");
    }

    // 4. 清理
    printf("\n[4] 清理资源...\n");
    kadd_dfa_engine_free(engine);
    kadd_config_free(&config);
    printf("✅ 完成\n");

    return 0;
}
