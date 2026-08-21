/* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved. */
#ifndef KADD_RF_ENGINE_H
#define KADD_RF_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kadd_rf_model_t kadd_rf_model_t;

/**
 * Load a KADD-RF v1 model exported from sklearn.
 *
 * The file format is explicitly little-endian and uses fixed-width fields, so
 * the same model can be consumed by x86-64 and AArch64 inference runtimes.
 */
int kadd_rf_model_load(kadd_rf_model_t **model, const char *model_path);

int kadd_rf_model_predict(const kadd_rf_model_t *model,
                          const float *features,
                          float *positive_probability);

int kadd_rf_model_predict_batch(const kadd_rf_model_t *model,
                                const float *features,
                                int sample_count,
                                float *positive_probabilities,
                                int batch_size);

void kadd_rf_model_free(kadd_rf_model_t *model);

/**
 * Return the feature dimension of a loaded RF model.
 */
int kadd_rf_model_get_feature_dim(const kadd_rf_model_t *model);

#ifdef __cplusplus
}
#endif

#endif /* KADD_RF_ENGINE_H */
