#ifndef KADD_FEATURE_ENGINE_H
#define KADD_FEATURE_ENGINE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif // KADD_FEATURE_ENGINE_H
