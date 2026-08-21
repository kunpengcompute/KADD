/* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved. */
#ifndef KADD_ML_ENGINE_H
#define KADD_ML_ENGINE_H

#include <stddef.h>

/* ========== ML 配置上限 ========== */
#define KADD_ML_MAX_SAMPLE_COUNT    10000000    /* 批处理最大样本数 */
#define KADD_ML_MAX_BATCH_SIZE          1024    /* 批次大小上限 */
#define KADD_ML_DEFAULT_BATCH_SIZE        64    /* 默认批次大小 */
#define KADD_ML_DEFAULT_MAX_OUTPUT_DIM  300    /* 默认输出维度上限 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file kadd_ml_engine.h
 * @brief 统一 ML/RF 推理引擎接口
 *
 * 支持原生 RF（SVE 加速）和 ONNX 双后端，提供模型加载、推理和资源释放功能
 */

/**
 * @brief 模型句柄（不透明指针）
 */
typedef struct kadd_model_handle_t kadd_model_handle_t;

/**
 * @brief 加载 ML 模型（原生 RF 或 ONNX）
 *
 * @param handle 模型句柄指针的指针
 * @param model_path 模型文件路径（.krf 原生格式或 .onnx）
 * @param feature_dim 特征维度（0=从模型自动读取；>0=校验与模型维度是否一致）
 *
 * @return 0 成功，<0 失败（错误码）
 *
 * @note ONNX 后端加载完成后会执行一次预热推理，预热失败会导致加载失败
 * @note feature_dim=0 时从模型自动读取输入维度
 * @note feature_dim>0 时校验与模型维度是否一致
 *
 * @example
 * // 方式1：加载原生 RF 模型（SVE 可用时优先）
 * kadd_model_load(&model, "model_tokens_rf.krf", 0);
 *
 * // 方式2：加载 ONNX 模型
 * kadd_model_load(&model, "model_rf.onnx", 0);
 */
int kadd_model_load(kadd_model_handle_t **handle, const char *model_path, int feature_dim);

/**
 * @brief 单样本推理
 *
 * @param handle 模型句柄
 * @param features 特征向量 (feature_dim 维 float 数组)
 * @param probability 输出概率 [0, 1]
 *
 * @return 0 成功，<0 失败（错误码）
 */
int kadd_model_predict(kadd_model_handle_t *handle, const float *features, float *probability);

/**
 * @brief 批量推理（分块处理）
 *
 * @param handle 模型句柄
 * @param features 特征矩阵 (n x feature_dim)
 * @param n 样本数 建议根据实际内存判断，建议10,000,000以下
 * @param probabilities 输出概率数组
 * @param batch_size 批次大小（推荐值：32/64/128，范围：1-1024，>1024自动重置为默认值64）
 *
 * @return KADD_SUCCESS (0) 完全成功，<0 失败（错误码）
 *
 * @note 使用分块批量推理，每次处理 batch_size 个样本，避免内存峰值过高
 * @note 较大的 batch_size 性能更好，但需要更多内存
 * @note 如果 batch_size > 1024，将自动重置为默认值 64
 * @note 如果 batch_size <= 0 或 n <= 0，返回 KADD_ERROR_INVALID_PARAM
 * @note 任何批次处理失败会立即返回错误码，不会部分成功
 */
int kadd_model_predict_batch(kadd_model_handle_t *handle, const float *features,
                             int n, float *probabilities, int batch_size);

/**
 * @brief 释放模型
 *
 * @param handle 模型句柄
 */
void kadd_model_free(kadd_model_handle_t *handle);

/**
 * @brief 获取模型特征维度
 *
 * @param handle 模型句柄
 * @return 特征维度（>0 成功，<0 失败）
 */
int kadd_model_get_feature_dim(kadd_model_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif // KADD_ML_ENGINE_H
