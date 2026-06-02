/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * @file kadd_seq_to_feature.c
 * @brief 特征转换接口实现
 *
 * 将不定长序列转换为固定维度特征向量
 * 基于 sliding_window 算法，支持 ARM SVE 加速
 */

#include "kadd_feature_engine.h"
#include <string.h>
#include <stdlib.h>

#if defined(__aarch64__) && defined(__ARM_FEATURE_SVE)
#include <arm_sve.h>
#endif

/* ========== 内部函数：滑窗距离计算 ========== */

/**
 * @brief 循环展开优化版滑窗距离
 */
static int sliding_window_distance_shift_fast(const int *seq, int seq_len, int window_size, int *result)
{
    int *weighted = (int *)malloc(seq_len * sizeof(int));
    if (!weighted) return KADD_ERROR_OUT_OF_MEMORY;

    for (int d = 1; d < window_size; d++) {
        const int weight = window_size - d;
        const int unroll_mask = SIMD_UNROLL_FACTOR-1;
        int i = 0;
        const int limit = seq_len & ~unroll_mask;
        for (; i < limit; i += SIMD_UNROLL_FACTOR) {
            weighted[i] = seq[i] * weight;
            weighted[i + SIMD_UNROLL_OFFSET_1] = seq[i + SIMD_UNROLL_OFFSET_1] * weight;
            weighted[i + SIMD_UNROLL_OFFSET_2] = seq[i + SIMD_UNROLL_OFFSET_2] * weight;
            weighted[i + SIMD_UNROLL_OFFSET_3] = seq[i + SIMD_UNROLL_OFFSET_3] * weight;
        }
        for (; i < seq_len; i++) {
            weighted[i] = seq[i] * weight;
        }
        const int right_limit = (seq_len - d) & ~unroll_mask;
        i = 0;
        for (; i < right_limit; i += SIMD_UNROLL_FACTOR) {
            result[i] += weighted[i + d];
            result[i + SIMD_UNROLL_OFFSET_1] += weighted[i + d + SIMD_UNROLL_OFFSET_1];
            result[i + SIMD_UNROLL_OFFSET_2] += weighted[i + d + SIMD_UNROLL_OFFSET_2];
            result[i + SIMD_UNROLL_OFFSET_3] += weighted[i + d + SIMD_UNROLL_OFFSET_3];
        }
        for (; i < seq_len - d; i++) {
            result[i] += weighted[i + d];
        }
        const int left_limit = (seq_len - d) & ~unroll_mask;
        i = d;
        for (; i < d + left_limit; i += SIMD_UNROLL_FACTOR) {
            result[i] += weighted[i - d];
            result[i + SIMD_UNROLL_OFFSET_1] += weighted[i + SIMD_UNROLL_OFFSET_1 - d];
            result[i + SIMD_UNROLL_OFFSET_2] += weighted[i + SIMD_UNROLL_OFFSET_2 - d];
            result[i + SIMD_UNROLL_OFFSET_3] += weighted[i + SIMD_UNROLL_OFFSET_3 - d];
        }
        for (; i < seq_len; i++) {
            result[i] += weighted[i - d];
        }
    }
    free(weighted);
    return KADD_SUCCESS;
}

#if defined(__aarch64__) && defined(__ARM_FEATURE_SVE)
/**
 * @brief ARM SVE 向量化实现
 */
static int sliding_window_distance_sve(const int *seq, int seq_len, int window_size, int *result)
{
    int *weighted = (int *)malloc(seq_len * sizeof(int));
    if (!weighted) return KADD_ERROR_OUT_OF_MEMORY;

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
 * @return 0 成功, -1 失败
 */
int kadd_seq_to_feature(const int *seq, int seq_len, int window_size,
                   int *feature, int feature_dim)
{
    if (!seq || !feature)
        return KADD_ERROR_NULL_PTR;
    if (seq_len < 0 || window_size < 0 || feature_dim <= 0 ||
        seq_len > MAX_SEQ_LEN || feature_dim > MAX_FEATURE_DIM)
        return KADD_ERROR_INVALID_PARAM;

    // 校验 seq[i] 范围：token ID 必须 < feature_dim
    for (int i = 0; i < seq_len; i++) {
        if (seq[i] < 0 || seq[i] >= feature_dim)
            return KADD_FEATURE_ERROR_VALUE_OUT_OF_RANGE;
    }

    // 分配临时结果数组
    int *result = (int *)calloc(seq_len, sizeof(int));
    if (!result) return KADD_ERROR_OUT_OF_MEMORY;

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

int kadd_seq_to_feature_batch(kadd_feature_batch_t *batch)
{
    if (batch == NULL) {
        return KADD_ERROR_NULL_PTR;
    }

    if (batch->seqs == NULL || batch->seq_lens == NULL || batch->features == NULL) {
        return KADD_ERROR_NULL_PTR;
    }

    if (batch->count <= 0 || batch->feature_dim <= 0 || batch->window_size <= 0) {
        return KADD_ERROR_INVALID_PARAM;
    }

    // 设置默认批次大小
    int actual_batch_size = batch->batch_size;
    if (actual_batch_size <= 0) {
        actual_batch_size = KADD_FEATURE_DEFAULT_BATCH_SIZE;
    }
    if (actual_batch_size > KADD_FEATURE_MAX_BATCH_SIZE) {
        actual_batch_size = KADD_FEATURE_MAX_BATCH_SIZE;
    }

    // 初始化输出特征为0
    int total = batch->count * batch->feature_dim;
    for (int i = 0; i < total; i++) {
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
                // NULL 序列或无效长度，返回错误
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
