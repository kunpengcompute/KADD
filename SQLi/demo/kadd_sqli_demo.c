/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *
 * @file kadd_sqli_demo.c
 * @brief KADD SQL 注入检测引擎 - 简单示例
 *
 * 构建：由顶层 CMake 一并编译（cmake .. && make，详见 docs/zh/SQLi/installation_guide.md）
 * 运行：cd build/demo && ./kadd_sqli_demo
 */

#include <stdio.h>
#include "kadd_sqli_engine.h"

int main()
{
    printf("=== KADD SQL 注入检测引擎示例 ===\n\n");

    // 1. 创建引擎（注意：传入指针的指针）
    printf("[1] 创建引擎...\n");
    kadd_sqli_engine_t *engine = NULL;
    if (kadd_sqli_engine_create_from_file(&engine, "../config.json") != KADD_SUCCESS) {
        printf("❌ 引擎创建失败\n");
        return 1;
    }
    printf("✅ 引擎创建成功\n\n");

    // 2. 检测输入
    const char *inputs[] = {
        "id=1' OR '1'='1",           // SQL 注入
        "id=123",                     // 正常输入
        "admin'--",                   // 注入
        "name=john",                  // 正常
    };

    printf("[2] 检测测试:\n\n");

    for (int i = 0; i < 4; i++) {
        float probability = 0.0f;
        if (kadd_sqli_detect(engine, inputs[i], &probability) == KADD_SUCCESS) {
            printf("输入: %s\n", inputs[i]);
            printf("概率: %.2f%%", probability * 100);

            if (probability >= 0.5f) {
                printf(" 🔴 危险\n");
            } else {
                printf(" ✅ 安全\n");
            }
            printf("\n");
        }
    }

    // 3. 清理
    printf("[3] 清理资源...\n");
    kadd_sqli_engine_free(&engine);
    printf("✅ 完成\n");

    return 0;
}
