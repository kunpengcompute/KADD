/* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved. */
/**
 * @file kadd_dl_engine.c
 * @brief 统一 DL/CNN 推理引擎 — 薄分发层
 *
 * SVE 可用时优先调用 kadd_cnn_engine（原生二进制 + SVE 加速），
 * 失败则回退 kadd_inference_engine（ONNX Runtime）。
 * 本文件不包含任何推理逻辑，仅做分发。
 */

#include "kadd_dl_engine.h"
#include "kadd_sve.h"
#include "kadd_errors.h"

#if KADD_HAVE_SVE
#include "kadd_cnn_engine.h"
#endif
#include "kadd_inference_engine.h"

#include <stdio.h>
#include <stdlib.h>

/* ========== 内部结构体 ========== */
struct kadd_dl_model_t {
    int backend;  /* 0 = ONNX (inference_engine), 1 = CNN native */
    int feature_dim;
#if KADD_HAVE_SVE
    kadd_cnn_model_t *cnn_model;
#endif
    kadd_inference_engine_t *onnx_engine;
};

/* ========== 公共 API ========== */

int kadd_dl_model_load(kadd_dl_model_t **model_out, const char *model_path)
{
    if (!model_out || !model_path) {
        return KADD_ERROR_NULL_PTR;
    }
    *model_out = NULL;

    kadd_dl_model_t *h = (kadd_dl_model_t *)calloc(1, sizeof(*h));
    if (!h) {
        return KADD_ERROR_OUT_OF_MEMORY;
    }

#if KADD_HAVE_SVE
    /* SVE 可用：优先尝试原生 CNN 二进制格式 */
    {
        kadd_cnn_model_t *cnn = NULL;
        if (kadd_cnn_model_load(&cnn, model_path) == KADD_SUCCESS) {
            h->backend = 1;
            h->cnn_model = cnn;
            h->feature_dim = 300; /* KADD_CNN_SEQUENCE_LENGTH */
            *model_out = h;
            return KADD_SUCCESS;
        }
        /* CNN 加载失败（非原生格式），回退 ONNX */
    }
#endif

    /* ONNX 路径 */
    if (kadd_inference_engine_load_from_path(&h->onnx_engine, model_path) != KADD_SUCCESS) {
        free(h);
        return KADD_ERROR_MODEL_LOAD_FAILED;
    }
    h->backend = 0;
    h->feature_dim = kadd_inference_engine_get_feature_dim(h->onnx_engine);

    *model_out = h;
    return KADD_SUCCESS;
}

int kadd_dl_model_predict(kadd_dl_model_t *model, const float *input, float *probability)
{
    return kadd_dl_model_predict_batch(model, input, 1, probability, 1);
}

int kadd_dl_model_predict_batch(kadd_dl_model_t *model, const float *inputs,
                                int n, float *probabilities, int batch_size)
{
    if (!model || !inputs || !probabilities) {
        return KADD_ERROR_NULL_PTR;
    }

#if KADD_HAVE_SVE
    if (model->backend == 1) {
        return kadd_cnn_model_predict_batch(model->cnn_model, inputs, n,
                                            probabilities, batch_size);
    }
#endif

    return kadd_inference_predict_batch(model->onnx_engine, inputs, n,
                                        probabilities, batch_size);
}

int kadd_dl_model_get_feature_dim(kadd_dl_model_t *model)
{
    if (!model) return KADD_ERROR_NULL_PTR;
    return model->feature_dim;
}

void kadd_dl_model_free(kadd_dl_model_t *model)
{
    if (!model) return;

#if KADD_HAVE_SVE
    if (model->backend == 1) {
        kadd_cnn_model_free(model->cnn_model);
        free(model);
        return;
    }
#endif

    kadd_inference_engine_free(model->onnx_engine);
    free(model);
}
