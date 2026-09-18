/* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved. */
/**
 * @file kadd_ml_engine.c
 * @brief 统一 ML/RF 推理引擎 — 薄分发层
 *
 * 优先尝试原生 RF 二进制格式（kadd_rf_engine），
 * 失败则回退 kadd_inference_engine（ONNX Runtime）。
 * SVE 可用时 RF 推理走 SVE 加速，否则走标量回退。
 * 本文件不包含任何推理逻辑，仅做分发。
 */

#include "kadd_ml_engine.h"
#include "kadd_sve.h"
#include "kadd_errors.h"
#include "kadd_rf_engine.h"
#include "kadd_inference_engine.h"

#include <stdio.h>
#include <stdlib.h>

/* ========== 内部结构体 ========== */
struct kadd_model_handle_t {
    int backend;  /* 0 = ONNX (inference_engine), 1 = RF native */
    int feature_dim;
    kadd_rf_model_t *rf_model;
    kadd_inference_engine_t *onnx_engine;
};

/* ========== 公共 API ========== */

int kadd_model_load(kadd_model_handle_t **handle, const char *model_path, int feature_dim)
{
    if (!handle || !model_path) {
        return KADD_ERROR_NULL_PTR;
    }
    *handle = NULL;

    kadd_model_handle_t *h = (kadd_model_handle_t *)calloc(1, sizeof(*h));
    if (!h) {
        return KADD_ERROR_OUT_OF_MEMORY;
    }

#if KADD_HAVE_SVE
    /*
     * 先确认运行环境 CPU 真的支持 SVE（编译期宏无法证明硬件能力），
     * 不支持则直接走 ONNX 后端，避免执行 SVE 指令触发 SIGILL。
     */
    if (kadd_sve_runtime_available()) {
        /* 硬件支持 SVE：优先尝试原生 RF 二进制格式（.krf） */
        kadd_rf_model_t *rf = NULL;
        if (kadd_rf_model_load(&rf, model_path) == KADD_SUCCESS) {
            int rf_dim = kadd_rf_model_get_feature_dim(rf);
            if (feature_dim != 0 && feature_dim != rf_dim) {
                fprintf(stderr,
                        "[kadd_ml] ERROR: feature_dim mismatch: user=%d, model=%d\n",
                        feature_dim, rf_dim);
                kadd_rf_model_free(&rf);
                free(h);
                return KADD_ERROR_INVALID_PARAM;
            }
            h->backend = 1;
            h->rf_model = rf;
            h->feature_dim = rf_dim;
            *handle = h;
            return KADD_SUCCESS;
        }
        /* RF 加载失败（非原生格式），回退 ONNX */
    } else {
        fprintf(stderr,
                "[kadd_ml] CPU does not support SVE, using ONNX backend\n");
    }
#endif

    /* ONNX 路径：委托 inference_engine */
    if (kadd_inference_engine_load_from_path(&h->onnx_engine, model_path) != KADD_SUCCESS) {
        free(h);
        return KADD_ERROR_MODEL_LOAD_FAILED;
    }

    int onnx_dim = kadd_inference_engine_get_feature_dim(h->onnx_engine);
    if (feature_dim != 0 && feature_dim != onnx_dim) {
        fprintf(stderr,
                "[kadd_ml] ERROR: feature_dim mismatch: user=%d, model=%d\n",
                feature_dim, onnx_dim);
        kadd_inference_engine_free(&h->onnx_engine);
        free(h);
        return KADD_ERROR_INVALID_PARAM;
    }

    h->backend = 0;
    h->feature_dim = onnx_dim;
    *handle = h;
    return KADD_SUCCESS;
}

int kadd_model_predict(kadd_model_handle_t *handle, const float *features, float *probability)
{
    return kadd_model_predict_batch(handle, features, 1, probability, 1);
}

int kadd_model_predict_batch(kadd_model_handle_t *handle, const float *features, int n,
                             float *probabilities, int batch_size)
{
    if (!handle || !features || !probabilities) {
        return KADD_ERROR_NULL_PTR;
    }

#if KADD_HAVE_SVE
    if (handle->backend == 1) {
        return kadd_rf_model_predict_batch(handle->rf_model, features, n,
                                           probabilities, batch_size);
    }
#endif

    return kadd_inference_predict_batch(handle->onnx_engine, features, n,
                                        probabilities, batch_size);
}

int kadd_model_get_feature_dim(kadd_model_handle_t *handle)
{
    if (!handle) return KADD_ERROR_NULL_PTR;
    return handle->feature_dim;  /* load 时已设置，两种后端统一 */
}

void kadd_model_free(kadd_model_handle_t **handle)
{
    if (!handle || !*handle) return;

    kadd_model_handle_t *h = *handle;

#if KADD_HAVE_SVE
    if (h->backend == 1) {
        kadd_rf_model_free(&h->rf_model);
        free(h);
        *handle = NULL;   /* 清空调用者句柄，消除 UAF / double-free */
        return;
    }
#endif

    kadd_inference_engine_free(&h->onnx_engine);
    free(h);
    *handle = NULL;
}
