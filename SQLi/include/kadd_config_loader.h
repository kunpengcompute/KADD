/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * All rights reserved.
 *
 * Licensed under the License;
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/license/bsd-3-clause
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/**
 * @file kadd_config_loader.h
 * @brief Configuration loader - read paths from config.json
 *
 * Configuration structure (absolute paths):
 * - transition_table, output_table, eod_table: DFA table paths
 * - model_rf, model_rf_onnx: RF model paths (SVE first, ONNX fallback)
 * - model_cnn, model_cnn_onnx: CNN model paths (SVE first, ONNX fallback)
 * - engine_mode: engine mode (ml/dl)
 *
 * Optional fields:
 * - model_hmac_key: 64 hex chars (32 bytes), the HMAC-SHA256 model key.
 *   Priority: env KADD_MODEL_HMAC_KEY / KADD_RF_HMAC_KEY > this field.
 *   When no env key is set, the loader materializes this value into
 *   KADD_MODEL_HMAC_KEY so the model auth code (kadd_model_auth.h) picks it up.
 *   SECURITY: a config carrying the key is sensitive material - chmod 600 it,
 *   never commit production keys; prefer env injection in production.
 *
 * config_path must be non-empty and shorter than KADD_CONFIG_PATH_MAX_LEN.
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
    /* 64 个十六进制字符 + NUL；长度以 kadd_model_auth.h 的
     * KADD_MODEL_HMAC_KEY_HEX_LEN 为准（.c 中有编译期断言），空 = 未配置 */
    char model_hmac_key[65];
} kadd_config_t;

int kadd_config_load(const char *config_path, kadd_config_t *config);
void kadd_config_free(kadd_config_t *config);

#ifdef __cplusplus
}
#endif

#endif // KADD_CONFIG_LOADER_H
