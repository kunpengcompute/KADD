/* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved. */
#ifndef KADD_CNN_ENGINE_H
#define KADD_CNN_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kadd_cnn_model_t kadd_cnn_model_t;

/**
 * Load a portable KADD-CNN v1 TextCNN model.
 *
 * The model is fixed-width little-endian and can be exported on x86-64 and
 * consumed unchanged on x86-64 or AArch64. Model loading validates the file
 * and pre-packs its weights; inference performs no heap allocation.
 */
int kadd_cnn_model_load(kadd_cnn_model_t **model, const char *model_path);

/**
 * Run one TextCNN inference.
 *
 * tokens must contain 300 finite, integer-valued float32 token IDs in [0, 28].
 * The returned value is the probability of class 1.
 */
int kadd_cnn_model_predict(const kadd_cnn_model_t *model,
                           const float *tokens,
                           float *positive_probability);

/**
 * Run n TextCNN inferences. batch_size controls API chunking and is retained
 * for parity with the common KADD model API.
 */
int kadd_cnn_model_predict_batch(const kadd_cnn_model_t *model,
                                 const float *tokens,
                                 int sample_count,
                                 float *positive_probabilities,
                                 int batch_size);

int kadd_cnn_model_get_feature_dim(const kadd_cnn_model_t *model);

/** Return the compiled hot-path backend: "sve" or "scalar". */
const char *kadd_cnn_runtime_backend(void);

/** Return the active SVE vector width in bits, or 0 for scalar builds. */
int kadd_cnn_runtime_vector_bits(void);

/** Return a stable name for the compiled TextCNN kernel. */
const char *kadd_cnn_runtime_kernel(void);

void kadd_cnn_model_free(kadd_cnn_model_t *model);

/** Return non-zero when model_path starts with the KADD-CNN v1 magic. */
int kadd_cnn_model_is_file(const char *model_path);

#ifdef __cplusplus
}
#endif

#endif /* KADD_CNN_ENGINE_H */
