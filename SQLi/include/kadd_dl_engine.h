/* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved. */
/**
 * @file kadd_dl_engine.h
 * @brief 统一 DL/CNN 推理引擎接口
 *
 * 编译时根据 SVE 支持自动选择后端：
 * - SVE 可用：优先加载原生 CNN 二进制格式，失败回退 ONNX
 * - SVE 不可用：使用 ONNX Runtime
 */

#ifndef KADD_DL_ENGINE_H
#define KADD_DL_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kadd_dl_model_t kadd_dl_model_t;

/**
 * @brief 加载 DL/CNN 模型
 *
 * SVE 可用时先尝试原生 CNN 格式，失败回退 ONNX。
 * SVE 不可用时直接使用 ONNX Runtime。
 *
 * @param model 句柄指针的指针
 * @param model_path 模型文件路径（.kcn 原生格式或 .onnx）
 * @param feature_dim 特征维度（0=从模型自动读取；>0=校验与模型维度是否一致）
 * @return 0 成功，<0 失败
 */
int kadd_dl_model_load(kadd_dl_model_t **model, const char *model_path, int feature_dim);

/**
 * @brief 单样本推理
 * @param input CNN: 300 维 float token ID 数组；ONNX: feature_dim 维 float 数组
 * @param probability 输出正类概率 [0, 1]
 */
int kadd_dl_model_predict(kadd_dl_model_t *model, const float *input, float *probability);

/**
 * @brief 批量推理
 * @param inputs 输入矩阵 (n x input_dim)
 * @param batch_size <=0 或 >1024 时自动使用默认值 64
 */
int kadd_dl_model_predict_batch(kadd_dl_model_t *model, const float *inputs,
                                int n, float *probabilities, int batch_size);

/** @brief 获取输入维度（CNN: 300；ONNX: 从模型读取） */
int kadd_dl_model_get_feature_dim(kadd_dl_model_t *model);

/**
 * @brief 释放模型并清空调用者句柄
 * @param model 模型句柄指针的指针；释放成功后 *model 为 NULL
 */
void kadd_dl_model_free(kadd_dl_model_t **model);

#ifdef __cplusplus
}
#endif

#endif /* KADD_DL_ENGINE_H */
