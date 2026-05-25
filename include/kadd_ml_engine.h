#ifndef KADD_ML_ENGINE_H
#define KADD_ML_ENGINE_H

#include <stddef.h>

/*
 * 调试宏定义
 * 取消注释以启用 ML 引擎调试输出
 */
// #define KADD_DEBUG_ML

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file kadd_ml_engine.h
 * @brief ONNX 推理引擎接口
 *
 * 提供模型加载、推理和资源释放功能
 */

/**
 * @brief 模型句柄（不透明指针）
 */
typedef struct kadd_model_handle_t kadd_model_handle_t;

/**
 * @brief 加载 ONNX 模型
 *
 * @param handle 模型句柄指针的指针
 * @param model_path 模型文件路径 (.onnx)
 * @param feature_dim 特征维度
 *
 * @return 0 成功，<0 失败（错误码）
 *
 * @example
 * kadd_model_handle_t *model = NULL;
 * if (kadd_model_load(&model, "assets/model/model_tokens_rf.onnx", 28) == KADD_SUCCESS) {
 *     // 使用模型
 *     kadd_model_free(model);
 * }
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
 * @param n 样本数
 * @param probabilities 输出概率数组
 * @param batch_size 批次大小（推荐值：32/64/128，范围：1-1024）
 *
 * @return KADD_SUCCESS (0) 完全成功，<0 失败（错误码）
 *
 * @note 使用分块批量推理，每次处理 batch_size 个样本，避免内存峰值过高
 * @note 较大的 batch_size 性能更好，但需要更多内存
 * @note 如果 batch_size > 1024，将自动限制为 1024
 * @note 如果 batch_size <= 0 或 n <= 0，返回 KADD_ERROR_INVALID_PARAM
 * @note 任何批次处理失败会立即返回 KADD_ERROR_INFERENCE_FAILED，不会部分成功
 */
int kadd_model_predict_batch(kadd_model_handle_t *handle, const float *features, int n, float *probabilities, int batch_size);

/**
 * @brief 释放模型
 *
 * @param handle 模型句柄
 */
void kadd_model_free(kadd_model_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif // KADD_ML_ENGINE_H
