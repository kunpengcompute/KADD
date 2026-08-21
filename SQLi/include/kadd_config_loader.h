/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * @file kadd_config_loader.h
 * @brief Configuration loader - read paths from config.json
 *
 * Configuration structure (absolute paths):
 * - transition_table, output_table, eod_table: DFA table paths
 * - model_rf, model_rf_onnx: RF model paths (SVE first, ONNX fallback)
 * - model_cnn, model_cnn_onnx: CNN model paths (SVE first, ONNX fallback)
 * - engine_mode: engine mode (ml/dl)
 */

#ifndef KADD_CONFIG_LOADER_H
#define KADD_CONFIG_LOADER_H

#include "kadd_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KADD_CONFIG_PATH_MAX_LEN 512
#define KADD_CONFIG_MAX_FILE_SIZE (10 * 1024 * 1024)

typedef enum {
    KADD_ENGINE_MODE_ML = 0,
    KADD_ENGINE_MODE_DL = 1
} kadd_engine_mode_t;

typedef struct {
    char transition_table[KADD_CONFIG_PATH_MAX_LEN];
    char output_table[KADD_CONFIG_PATH_MAX_LEN];
    char eod_table[KADD_CONFIG_PATH_MAX_LEN];
    kadd_engine_mode_t engine_mode;
    char model_rf[KADD_CONFIG_PATH_MAX_LEN];
    char model_rf_onnx[KADD_CONFIG_PATH_MAX_LEN];
    char model_cnn[KADD_CONFIG_PATH_MAX_LEN];
    char model_cnn_onnx[KADD_CONFIG_PATH_MAX_LEN];
} kadd_config_t;

int kadd_config_load(const char *config_path, kadd_config_t *config);
void kadd_config_free(kadd_config_t *config);

#ifdef __cplusplus
}
#endif

#endif // KADD_CONFIG_LOADER_H
