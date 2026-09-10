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

/**
 * Predict one sample.
 *
 * Buffer contract: features must point to at least
 * kadd_rf_model_get_feature_dim(model) contiguous finite floats.
 */
int kadd_rf_model_predict(const kadd_rf_model_t *model,
                          const float *features,
                          float *positive_probability);

/**
 * Predict multiple samples.
 *
 * Buffer contract: features must contain sample_count rows, each with
 * kadd_rf_model_get_feature_dim(model) contiguous finite floats.
 * positive_probabilities must contain at least sample_count floats.
 * batch_size only controls internal chunking; it does not change the total
 * number of input or output elements.
 */
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
