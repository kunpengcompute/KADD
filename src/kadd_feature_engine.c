/**
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
static void sliding_window_distance_shift_fast(const int *seq, int seq_len, int window_size, int *result)
{
    memset(result, 0, seq_len * sizeof(int));
    int *weighted = (int *)malloc(seq_len * sizeof(int));
    if (!weighted) return;

    for (int d = 1; d < window_size; d++) {
        const int weight = window_size - d;
        int i = 0;
        const int limit = seq_len & ~3;
        for (; i < limit; i += 4) {
            weighted[i] = seq[i] * weight;
            weighted[i + 1] = seq[i + 1] * weight;
            weighted[i + 2] = seq[i + 2] * weight;
            weighted[i + 3] = seq[i + 3] * weight;
        }
        for (; i < seq_len; i++) {
            weighted[i] = seq[i] * weight;
        }
        const int right_limit = (seq_len - d) & ~3;
        i = 0;
        for (; i < right_limit; i += 4) {
            result[i]     += weighted[i + d];
            result[i + 1] += weighted[i + d + 1];
            result[i + 2] += weighted[i + d + 2];
            result[i + 3] += weighted[i + d + 3];
        }
        for (; i < seq_len - d; i++) {
            result[i] += weighted[i + d];
        }
        const int left_limit = (seq_len - d) & ~3;
        i = d;
        for (; i < d + left_limit; i += 4) {
            result[i]     += weighted[i - d];
            result[i + 1] += weighted[i + 1 - d];
            result[i + 2] += weighted[i + 2 - d];
            result[i + 3] += weighted[i + 3 - d];
        }
        for (; i < seq_len; i++) {
            result[i] += weighted[i - d];
        }
    }
    free(weighted);
}

#if defined(__aarch64__) && defined(__ARM_FEATURE_SVE)
/**
 * @brief ARM SVE 向量化实现
 */
static void sliding_window_distance_sve(const int *seq, int seq_len, int window_size, int *result)
{
    memset(result, 0, seq_len * sizeof(int));
    int *weighted = (int *)malloc(seq_len * sizeof(int));
    if (!weighted) return;

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
    if (!seq || !feature || seq_len <= 0 || window_size <= 0 || feature_dim <= 0) {
        return -1;
    }

    // 分配临时结果数组
    int *result = (int *)malloc(seq_len * sizeof(int));
    if (!result) return -1;

    // 计算滑窗距离
#if defined(__aarch64__) && defined(__ARM_FEATURE_SVE)
    sliding_window_distance_sve(seq, seq_len, window_size, result);
#else
    sliding_window_distance_shift_fast(seq, seq_len, window_size, result);
#endif

    // 映射到固定维度特征向量
    memset(feature, 0, feature_dim * sizeof(int));
    for (int i = 0; i < seq_len; i++) {
        if (seq[i] >= 0 && seq[i] < feature_dim) {
            feature[seq[i]] += result[i];
        }
    }

    free(result);
    return 0;
}
