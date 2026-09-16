/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *
 * @file kadd_ml_demo.c
 * @brief KADD ML/RF 推理引擎 - 最小示例
 *
 * ===== config.json 配置说明（ML 模式）=====
 *
 *   "engine_mode": "ml"
 *
 *   模型路径（二选一或同时配置）：
 *     "model_rf":      "model.krf"     <- 原生 RF 二进制（SVE 加速，ARM Kunpeng）
 *     "model_rf_onnx": "model.onnx"    <- ONNX Runtime（通用回退，x86/ARM 均可）
 *
 *   SVE <-> ONNX 切换：
 *     ONNX -> SVE : model_rf 填写 .krf 路径，编译加 -DENABLE_ARM_SVE=ON
 *     SVE -> ONNX : model_rf 改 .onnx 路径（或用 model_rf_onnx），默认编译即可
 *     ML -> DL    : engine_mode 改为 "dl"，配置 model_cnn / model_cnn_onnx
 *
 *   完整示例：
 *   {
 *       "engine_mode": "ml",
 *       "transition_table": "/opt/KADD/SQLi/assets/data/transition_table.bin",
 *       "output_table":     "/opt/KADD/SQLi/assets/data/output_table.bin",
 *       "eod_table":        "/opt/KADD/SQLi/assets/data/eod_table.bin",
 *       "model_rf":         "/opt/KADD/SQLi/assets/model/model_tokens_rf.krf",
 *       "model_rf_onnx":    "/opt/KADD/SQLi/assets/model/model_tokens_rf.onnx"
 *   }
 *
 * 构建：cmake .. && make
 * 运行：cd build/demo && ./kadd_ml_demo [config.json路径]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kadd_ml_engine.h"
#include "kadd_config_loader.h"

int main(int argc, char *argv[])
{
    const char *config_path = (argc >= 2) ? argv[1] : "../config.json";
    printf("=== KADD ML 推理示例 ===\n\n");

    /* 1. 加载配置 */
    kadd_config_t config;
    if (kadd_config_load(config_path, &config) != KADD_SUCCESS) {
        printf("配置加载失败\n");
        return 1;
    }

    /* 2. 加载模型（优先原生 RF，失败回退 ONNX） */
    kadd_model_handle_t *model = NULL;
    const char *primary  = config.model_rf[0]      ? config.model_rf      : NULL;
    const char *fallback = config.model_rf_onnx[0]  ? config.model_rf_onnx : NULL;

    if ((primary  && kadd_model_load(&model, primary, 0) == KADD_SUCCESS) ||
        (fallback && kadd_model_load(&model, fallback, 0) == KADD_SUCCESS)) {
        printf("模型加载成功\n");
    } else {
        printf("模型加载失败\n");
        kadd_config_free(&config);
        return 1;
    }

    int feature_dim = kadd_model_get_feature_dim(model);
    printf("特征维度: %d\n\n", feature_dim);

    /* 3. 单样本推理 */
    /* [FIX REPORT-2026-0000016] feature_dim 来自模型文件，calloc 可能失败或为 0 长度 */
    float *features = (feature_dim > 0) ? (float *)calloc((size_t)feature_dim, sizeof(float)) : NULL;
    if (features == NULL) {
        printf("特征缓冲区分配失败\n");
        kadd_model_free(&model);
        kadd_config_free(&config);
        return 1;
    }
    if (feature_dim > 12) {
        features[7]  = 100.0f;
        features[8]  = 436.0f;
        features[11] = 250.0f;
        features[12] = 160.0f;
    }

    float prob = 0.0f;
    if (kadd_model_predict(model, features, &prob) == KADD_SUCCESS) {
        printf("注入概率: %.2f%%\n", prob * 100);
        printf("判定结果: %s\n\n", prob >= 0.5f ? "SQL 注入" : "正常查询");
    }

    /* 4. 清理 */
    free(features);
    kadd_model_free(&model);
    kadd_config_free(&config);
    return 0;
}
