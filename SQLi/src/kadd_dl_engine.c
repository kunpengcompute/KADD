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

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* token 词汇表大小（与 kadd_cnn_engine.c 的 KADD_CNN_VOCAB_SIZE 一致） */
#define KADD_DL_VOCAB_SIZE 29

/* [FIX REPORT-2026-0000021] 批处理样本数上限，与后端 KADD_CNN_MAX_SAMPLE_COUNT /
 * KADD_INFERENCE_MAX_SAMPLE_COUNT 保持一致，用于在分发层先界定校验循环规模 */
#define KADD_DL_MAX_SAMPLE_COUNT 10000000

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

int kadd_dl_model_load(kadd_dl_model_t **model_out, const char *model_path, int feature_dim)
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
    /*
     * 先确认运行环境 CPU 真的支持 SVE（编译期宏无法证明硬件能力），
     * 不支持则直接走 ONNX 后端，避免执行 SVE 指令触发 SIGILL。
     */
    if (kadd_sve_runtime_available()) {
        /* 硬件支持 SVE：优先尝试原生 CNN 二进制格式 */
        kadd_cnn_model_t *cnn = NULL;
        if (kadd_cnn_model_load(&cnn, model_path) == KADD_SUCCESS) {
            int cnn_dim = kadd_cnn_model_get_feature_dim(cnn);
            if (feature_dim != 0 && feature_dim != cnn_dim) {
                fprintf(stderr,
                        "[kadd_dl] ERROR: feature_dim mismatch: user=%d, model=%d\n",
                        feature_dim, cnn_dim);
                kadd_cnn_model_free(cnn);
                free(h);
                return KADD_ERROR_INVALID_PARAM;
            }
            h->backend = 1;
            h->cnn_model = cnn;
            h->feature_dim = cnn_dim;
            *model_out = h;
            return KADD_SUCCESS;
        }
        /* CNN 加载失败（非原生格式），回退 ONNX */
    } else {
        fprintf(stderr,
                "[kadd_dl] CPU does not support SVE, using ONNX backend\n");
    }
#endif

    /* ONNX 路径 */
    if (kadd_inference_engine_load_from_path(&h->onnx_engine, model_path) != KADD_SUCCESS) {
        free(h);
        return KADD_ERROR_MODEL_LOAD_FAILED;
    }
    int onnx_dim = kadd_inference_engine_get_feature_dim(h->onnx_engine);
    if (feature_dim != 0 && feature_dim != onnx_dim) {
        fprintf(stderr,
                "[kadd_dl] ERROR: feature_dim mismatch: user=%d, model=%d\n",
                feature_dim, onnx_dim);
        kadd_inference_engine_free(h->onnx_engine);
        free(h);
        return KADD_ERROR_INVALID_PARAM;
    }
    h->backend = 0;
    h->feature_dim = onnx_dim;

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

    /* [FIX REPORT-2026-0000021] 原实现为 `i < n * model->feature_dim`（int 乘法）：
     *   - n 未校验，n <= 0 时要等到后端才拒绝，分发层语义不一致；
     *   - n * feature_dim 在 int 下回绕为负数会整段跳过 token 校验，
     *     使非法/越界输入直接流入 SVE 或 ONNX 后端，造成越界读。
     * 现改为：先按与后端一致的样本数上限校验 n，再用 size_t 做溢出安全的元素总数计算。 */
    if (n <= 0 || n > KADD_DL_MAX_SAMPLE_COUNT || model->feature_dim <= 0 ||
        (size_t)n > SIZE_MAX / (size_t)model->feature_dim) {
        return KADD_ERROR_INVALID_PARAM;
    }
    const size_t total_inputs = (size_t)n * (size_t)model->feature_dim;

    /* 统一 token 校验（与 kadd_cnn_engine 的 decode_tokens 一致）：
     * 保证 SVE 原生后端与 ONNX 后端对非法输入的返回码一致 */
    for (size_t i = 0; i < total_inputs; i++) {
        const float value = inputs[i];
        if (!isfinite(value) ||
            value < 0.0F ||
            value >= (float)KADD_DL_VOCAB_SIZE ||
            truncf(value) != value) {
            return KADD_ERROR_INVALID_PARAM;
        }
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
