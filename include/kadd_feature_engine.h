/* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved. */
#ifndef KADD_FEATURE_ENGINE_H
#define KADD_FEATURE_ENGINE_H

#include <stddef.h>
#include "kadd_errors.h"

#define MAX_SEQ_LEN 300
#define MAX_FEATURE_DIM 300

/* 循环展开次数 */
#define SIMD_UNROLL_FACTOR 4
/* 循环展开偏移量 */
#define SIMD_UNROLL_OFFSET_1 1
#define SIMD_UNROLL_OFFSET_2 2
#define SIMD_UNROLL_OFFSET_3 3
/* 默认批次大小 */
#define KADD_FEATURE_DEFAULT_BATCH_SIZE 64
/* 最大批次大小 */
#define KADD_FEATURE_MAX_BATCH_SIZE 1024

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 特征批处理上下文
 */
typedef struct {
    const int **seqs;         /**< token序列数组 */
    const int *seq_lens;      /**< 序列长度数组 */
    int count;                /**< 序列数量 */
    int *features;            /**< 输出特征（调用者分配，二维展平） */
    int feature_dim;          /**< 特征维度 */
    int window_size;          /**< 滑动窗口大小 */
    int batch_size;           /**< 批次大小（0或负数表示默认64） */
} kadd_feature_batch_t;

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
 *
 * @note 支持 ARM SVE 加速（如果编译时启用）
 *
 * @example
 * #define FEATURE_DIM 28
 * #include "kadd_feature_engine.h"
 *
 * int seq[] = {7, 11, 8, 12, 8, 11, 8};
 * int feature[FEATURE_DIM] = {0};
 * kadd_seq_to_feature(seq, 7, 5, feature, FEATURE_DIM);
 */
int kadd_seq_to_feature(const int *seq, int seq_len, int window_size,
                        int *feature, int feature_dim);

/**
 * @brief 批量将token序列转换为特征向量
 *
 * @param batch 批处理上下文
 * @return KADD_SUCCESS (0) 完全成功，<0 失败（错误码）
 *
 * @note 任何序列失败都会立即返回错误，不会部分成功
 * @note 失败的序列对应位置特征保持为 0
 */
int kadd_seq_to_feature_batch(kadd_feature_batch_t *batch);

#ifdef __cplusplus
}
#endif

#endif // KADD_FEATURE_ENGINE_H
