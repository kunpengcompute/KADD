/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * @file kadd_seq_to_feature.c
 * @brief 特征转换接口实现
 *
 * 将不定长序列转换为固定维度特征向量
 * 基于 sliding_window 算法，支持 ARM SVE 加速
 */

#include "kadd_feature_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include "kadd_errors.h"
#include "kadd_config_loader.h"

#if defined(__aarch64__) && defined(__ARM_FEATURE_SVE)
#include <arm_sve.h>
#endif

/* ========== 内部函数：滑窗距离计算 ========== */

/**
 * @brief 滑窗距离计算（简化版，无硬编码限制）
 *
 * 核心逻辑：
 * 对每个偏移距离 d (1 到 window_size-1)：
 *   权重 = window_size - d
 *   右侧累加：result[i] += seq[i+d] * weight
 *   左侧累加：result[i] += seq[i-d] * weight
 */
static int sliding_window_distance_shift_fast(const int *seq, int seq_len, int window_size, int *result)
{
    /* 初始化结果数组 */
    for (int i = 0; i < seq_len; i++) {
        result[i] = 0;
    }

    /* 对每个偏移距离计算加权和 */
    for (int d = 1; d < window_size; d++) {
        const int weight = window_size - d;

        /* 右侧：result[i] += seq[i+d] * weight */
        for (int i = 0; i < seq_len - d; i++) {
            result[i] += seq[i + d] * weight;
        }

        /* 左侧：result[i] += seq[i-d] * weight */
        for (int i = d; i < seq_len; i++) {
            result[i] += seq[i - d] * weight;
        }
    }

    return KADD_SUCCESS;
}

#if defined(__aarch64__) && defined(__ARM_FEATURE_SVE)
/**
 * @brief ARM SVE 向量化实现
 */
static int sliding_window_distance_sve(const int *seq, int seq_len, int window_size, int *result)
{
    int *weighted = (int *)calloc(seq_len, sizeof(int));
    if (!weighted) {
        fprintf(stderr, "[kadd_feature] ERROR: Failed to allocate weighted array (seq_len=%d)\n", seq_len);
        return KADD_ERROR_OUT_OF_MEMORY;
    }

    for (int d = 1; d < window_size; d++) {
        const int weight = window_size - d;

        const int vl = svcntw();
        int i = 0;
        const int seq_len_vl = seq_len & ~(vl - 1);
        for (; i < seq_len_vl; i += vl) {
            svbool_t pg = svwhilelt_b32_s32(i, seq_len);
            svint32_t v_seq = svld1_s32(pg, seq + i);
            svint32_t v_weighted = svmul_n_s32_z(pg, v_seq, weight);
            svst1_s32(pg, weighted + i, v_weighted);
        }
        for (; i < seq_len; i++) {
            weighted[i] = seq[i] * weight;
        }

        const int right_len = seq_len - d;
        i = 0;
        const int right_vl = right_len & ~(vl - 1);
        for (; i < right_vl; i += vl) {
            svbool_t pg = svwhilelt_b32_s32(i, right_len);
            svint32_t v_weighted = svld1(pg, weighted + d + i);
            svint32_t v_result = svld1(pg, result + i);
            svst1(pg, result + i, svadd_s32_z(pg, v_result, v_weighted));
        }
        for (; i < right_len; i++) {
            result[i] += weighted[i + d];
        }

        i = d;
        const int left_len = seq_len - d;
        const int left_vl = left_len & ~(vl - 1);
        for (; i < d + left_vl; i += vl) {
            svbool_t pg = svwhilelt_b32_s32(i - d, left_len);
            svint32_t v_weighted = svld1(pg, weighted + i - d);
            svint32_t v_result = svld1(pg, result + i);
            svst1(pg, result + i, svadd_s32_z(pg, v_result, v_weighted));
        }
        for (; i < seq_len; i++) {
            result[i] += weighted[i - d];
        }
    }
    free(weighted);
    return KADD_SUCCESS;
}
#endif

/* ========== 公共接口 ========== */

/**
 * @brief 将不定长序列转换为固定维度特征向量
 *
 * @param seq 输入序列（整数数组）
 * @param seq_len 序列长度
 * @param window_size 滑动窗口大小
 * @param feature 输出特征向量（需预先分配）
 * @param feature_dim 输出特征维度
 *
 * @return 0 成功，<0 失败（错误码）
 */
int kadd_seq_to_feature(const int *seq, int seq_len, int window_size,
                   int *feature, int feature_dim)
{
    if (!seq || !feature) {
        fprintf(stderr, "[kadd_feature] ERROR: NULL pointer (seq=%s, feature=%s)\n",
                (seq ? "valid" : "NULL"), (feature ? "valid" : "NULL"));
        return KADD_ERROR_NULL_PTR;
    }
    if (seq_len < 0 || window_size < 0 || feature_dim <= 0 ||
        seq_len > KADD_FEATURE_DEFAULT_MAX_INPUT_LENGTH || feature_dim > KADD_FEATURE_DEFAULT_MAX_OUTPUT_DIM) {
        fprintf(stderr, "[kadd_feature] ERROR: Invalid parameters (seq_len=%d, window_size=%d, feature_dim=%d)\n",
                seq_len, window_size, feature_dim);
        return KADD_ERROR_INVALID_PARAM;
    }

    // 校验 seq[i] 范围：token ID 必须 < feature_dim
    for (int i = 0; i < seq_len; i++) {
        if (seq[i] < 0 || seq[i] >= feature_dim) {
            fprintf(stderr, "[kadd_feature] ERROR: Token value %d out of range at index %d (feature_dim=%d)\n",
                    seq[i], i, feature_dim);
            return KADD_FEATURE_ERROR_VALUE_OUT_OF_RANGE;
        }
    }

    // 分配临时结果数组
    int *result = (int *)calloc(seq_len, sizeof(int));
    if (!result) {
        fprintf(stderr, "[kadd_feature] ERROR: Failed to allocate result array (seq_len=%d)\n", seq_len);
        return KADD_ERROR_OUT_OF_MEMORY;
    }

    // 计算滑窗距离
    int window_ret;
#if defined(__aarch64__) && defined(__ARM_FEATURE_SVE)
    window_ret = sliding_window_distance_sve(seq, seq_len, window_size, result);
#else
    window_ret = sliding_window_distance_shift_fast(seq, seq_len, window_size, result);
#endif
    if (window_ret != KADD_SUCCESS) {
        free(result);
        return window_ret;
    }

    // 映射到固定维度特征向量
    for (int i = 0; i < feature_dim; i++) {
        feature[i] = 0;
    }
    for (int i = 0; i < seq_len; i++) {
        if (seq[i] >= 0 && seq[i] < feature_dim) {
            feature[seq[i]] += result[i];
        }
    }

    free(result);
    return 0;
}

static int validate_feature_batch_params(const kadd_feature_batch_t *batch)
{
    if (batch == NULL) {
        fprintf(stderr, "[kadd_feature] ERROR: batch pointer is NULL\n");
        return KADD_ERROR_NULL_PTR;
    }

    if (batch->seqs == NULL || batch->seq_lens == NULL || batch->features == NULL) {
        fprintf(stderr,
                "[kadd_feature] ERROR: NULL batch array (seqs=%s, seq_lens=%s, features=%s)\n",
                (batch->seqs ? "valid" : "NULL"),
                (batch->seq_lens ? "valid" : "NULL"),
                (batch->features ? "valid" : "NULL"));
        return KADD_ERROR_NULL_PTR;
    }

    if (batch->count <= 0 || batch->feature_dim <= 0 || batch->window_size <= 0) {
        fprintf(stderr,
                "[kadd_feature] ERROR: Invalid batch params (count=%d, feature_dim=%d, window_size=%d)\n",
                batch->count, batch->feature_dim, batch->window_size);
        return KADD_ERROR_INVALID_PARAM;
    }
    if (batch->count > KADD_FEATURE_MAX_SAMPLE_COUNT) {
        fprintf(stderr,
                "[kadd_feature] ERROR: Batch count %d exceeds limit %d\n",
                batch->count, KADD_FEATURE_MAX_SAMPLE_COUNT);
        return KADD_ERROR_INVALID_PARAM;
    }
    if (batch->feature_dim > KADD_FEATURE_DEFAULT_MAX_OUTPUT_DIM) {
        fprintf(stderr,
                "[kadd_feature] ERROR: Feature dim %d exceeds limit %d\n",
                batch->feature_dim, KADD_FEATURE_DEFAULT_MAX_OUTPUT_DIM);
        return KADD_ERROR_INVALID_PARAM;
    }

    return KADD_SUCCESS;
}

int kadd_seq_to_feature_batch(kadd_feature_batch_t *batch)
{
    int ret = validate_feature_batch_params(batch);
    if (ret != KADD_SUCCESS) {
        return ret;
    }

    int actual_batch_size = batch->batch_size;
    if (actual_batch_size <= 0 || actual_batch_size > KADD_FEATURE_MAX_BATCH_SIZE) {
        actual_batch_size = KADD_FEATURE_DEFAULT_BATCH_SIZE;
    }

    // 初始化输出特征为0
    size_t total = (size_t)batch->count * (size_t)batch->feature_dim;
    // 溢出检查：防止32位系统上 size_t 溢出
    if (total / (size_t)batch->count != (size_t)batch->feature_dim) {
        fprintf(stderr, "[kadd_feature] ERROR: Integer overflow in total features (count=%d, feature_dim=%d)\n",
                batch->count, batch->feature_dim);
        return KADD_ERROR_INVALID_PARAM;
    }
    for (size_t i = 0; i < total; i++) {
        batch->features[i] = 0;
    }

    // 分批处理
    for (int batch_start = 0; batch_start < batch->count; batch_start += actual_batch_size) {
        int current_batch = (batch_start + actual_batch_size < batch->count) ?
                           actual_batch_size : (batch->count - batch_start);

        // 处理当前批次
        for (int i = 0; i < current_batch; i++) {
            int idx = batch_start + i;
            const int *seq = batch->seqs[idx];
            int seq_len = batch->seq_lens[idx];

            if (seq == NULL || seq_len <= 0) {
                fprintf(stderr, "[kadd_feature] ERROR: Invalid sequence at index %d (seq=%s, seq_len=%d)\n",
                        idx, (seq ? "valid" : "NULL"), seq_len);
                return KADD_ERROR_INVALID_INPUT;
            }

            // 获取当前序列的特征输出位置
            int *feature = batch->features + idx * batch->feature_dim;

            // 调用单条特征提取
            int result = kadd_seq_to_feature(seq, seq_len, batch->window_size,
                                            feature, batch->feature_dim);
            if (result != 0) {
                // 特征提取失败，立即返回错误
                return result;
            }
        }
    }

    // 全部成功
    return KADD_SUCCESS;
}
