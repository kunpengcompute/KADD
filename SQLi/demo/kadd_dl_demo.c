/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *
 * @file kadd_dl_demo.c
 * @brief KADD DL/CNN 推理引擎 - 最小示例
 *
 * ===== config.json 配置说明（DL 模式）=====
 *
 *   "engine_mode": "dl"
 *
 *   模型路径（二选一或同时配置）：
 *     "model_cnn":      "model.kcn"    <- 原生 CNN 二进制（SVE 加速，ARM Kunpeng）
 *     "model_cnn_onnx": "model.onnx"   <- ONNX Runtime（通用回退，x86/ARM 均可）
 *
 *   SVE <-> ONNX 切换：
 *     ONNX -> SVE : model_cnn 填写 .kcn 路径，编译加 -DENABLE_ARM_SVE=ON
 *     SVE -> ONNX : model_cnn 改 .onnx 路径（或用 model_cnn_onnx），默认编译即可
 *     DL -> ML    : engine_mode 改为 "ml"，配置 model_rf / model_rf_onnx
 *
 *   完整示例：
 *   {
 *       "engine_mode": "dl",
 *       "transition_table": "/opt/KADD/SQLi/assets/data/transition_table.bin",
 *       "output_table":     "/opt/KADD/SQLi/assets/data/output_table.bin",
 *       "eod_table":        "/opt/KADD/SQLi/assets/data/eod_table.bin",
 *       "model_cnn":        "/opt/KADD/SQLi/assets/model/model_tokens_dl_cnn.kcn",
 *       "model_cnn_onnx":   "/opt/KADD/SQLi/assets/model/model_tokens_dl_cnn.onnx"
 *   }
 *
 * 构建：cmake .. && make
 * 运行：cd build/demo && ./kadd_dl_demo [config.json路径]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kadd_dl_engine.h"
#include "kadd_config_loader.h"

int main(int argc, char *argv[])
{
    const char *config_path = (argc >= 2) ? argv[1] : "../config.json";
    printf("=== KADD DL 推理示例 ===\n\n");

    /* 1. 加载配置 */
    kadd_config_t config;
    if (kadd_config_load(config_path, &config) != KADD_SUCCESS) {
        printf("配置加载失败\n");
        return 1;
    }

    /* 2. 加载模型（优先原生 CNN，失败回退 ONNX） */
    kadd_dl_model_t *model = NULL;
    const char *primary  = config.model_cnn[0]      ? config.model_cnn      : NULL;
    const char *fallback = config.model_cnn_onnx[0]  ? config.model_cnn_onnx : NULL;

    if ((primary  && kadd_dl_model_load(&model, primary, 0) == KADD_SUCCESS) ||
        (fallback && kadd_dl_model_load(&model, fallback, 0) == KADD_SUCCESS)) {
        printf("模型加载成功\n");
    } else {
        printf("模型加载失败\n");
        kadd_config_free(&config);
        return 1;
    }

    int feature_dim = kadd_dl_model_get_feature_dim(model);
    printf("特征维度: %d\n\n", feature_dim);

    /* 3. 单样本推理 */
    /* [FIX REPORT-2026-0000016] feature_dim 来自模型文件，calloc 可能失败或为 0 长度 */
    float *input = (feature_dim > 0) ? (float *)calloc((size_t)feature_dim, sizeof(float)) : NULL;
    if (input == NULL) {
        printf("输入缓冲区分配失败\n");
        kadd_dl_model_free(model);
        kadd_config_free(&config);
        return 1;
    }
    /* CNN: 填充 token ID 序列（28=PAD）; ONNX: 填充特征值 */
    if (feature_dim >= 300) {
        for (int i = 0; i < feature_dim; i++) input[i] = 28.0f;
        int pat[] = {7, 8, 11, 12, 5, 7, 8, 11, 12, 3, 4};
        for (int i = 0; i < 11; i++) input[i] = (float)pat[i];
    } else if (feature_dim > 12) {
        input[7] = 100.0f; input[8] = 436.0f;
        input[11] = 250.0f; input[12] = 160.0f;
    }

    float prob = 0.0f;
    if (kadd_dl_model_predict(model, input, &prob) == KADD_SUCCESS) {
        printf("注入概率: %.2f%%\n", prob * 100);
        printf("判定结果: %s\n\n", prob >= 0.5f ? "SQL 注入" : "正常查询");
    }

    /* 4. 清理 */
    free(input);
    kadd_dl_model_free(model);
    kadd_config_free(&config);
    return 0;
}
