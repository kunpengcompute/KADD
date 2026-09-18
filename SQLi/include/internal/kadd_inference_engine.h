/* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved. */
#ifndef KADD_INFERENCE_ENGINE_H
#define KADD_INFERENCE_ENGINE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file kadd_inference_engine.h
 * @brief ONNX 推理引擎接口
 *
 * 提供 ONNX 模型加载、推理和资源释放功能
 * 支持 RF/CNN 等不同模型类型
 */

/* ========== 推理引擎类型 ========== */
typedef enum {
    KADD_INFERENCE_TYPE_RF  = 0,   /* Random Forest */
    KADD_INFERENCE_TYPE_CNN = 1,   /* DL_CNN */
} kadd_inference_type_t;

/* ========== 推理引擎上限 ========== */
#define KADD_INFERENCE_MAX_SAMPLE_COUNT    10000000
#define KADD_INFERENCE_MAX_BATCH_SIZE          1024
#define KADD_INFERENCE_DEFAULT_BATCH_SIZE        64

/**
 * @brief 推理引擎句柄（不透明指针）
 */
#define KADD_INFERENCE_MAX_FEATURE_DIM      4096   /* [FIX REPORT-2026-0000014] */
typedef struct kadd_inference_engine_t kadd_inference_engine_t;

/**
 * @brief 从配置文件加载模型
 *
 * @param engine 引擎句柄指针的指针
 * @param config_file JSON 配置文件路径
 * @param type 模型类型（RF / CNN）
 *
 * @return 0 成功，<0 失败（错误码）
 *
 * @note 加载完成后会执行一次预热推理
 * @note feature_dim 从模型文件自动读取，加载期要求 1..4096
 *
 * @example
 * kadd_inference_engine_t *engine = NULL;
 * if (kadd_inference_engine_load(&engine, "config.json", KADD_INFERENCE_TYPE_RF) == 0) {
 *     float features[28] = {...};
 *     float prob = 0.0f;
 *     kadd_inference_predict(engine, features, &prob);
 *     kadd_inference_engine_free(&engine);
 * }
 */
int kadd_inference_engine_load(kadd_inference_engine_t **engine,
                                const char *config_file,
                                kadd_inference_type_t type);

/**
 * @brief 直接从模型路径加载（不需要配置文件）
 *
 * @param engine 引擎句柄指针的指针
 * @param model_path 模型文件路径 (.onnx)
 *
 * @return 0 成功，<0 失败（错误码）
 */
int kadd_inference_engine_load_from_path(kadd_inference_engine_t **engine,
                                          const char *model_path);

/**
 * @brief 单样本推理
 *
 * @param engine 推理引擎句柄
 * @param features 特征向量（维度从模型读取）
 * @param probability 输出概率 [0, 1]
 *
 * @return 0 成功，<0 失败（错误码）
 */
int kadd_inference_predict(kadd_inference_engine_t *engine,
                           const float *features,
                           float *probability);

/**
 * @brief 批量推理（分块处理）
 *
 * @param engine 推理引擎句柄
 * @param features 特征矩阵 (n x feature_dim)
 * @param n 样本数
 * @param probabilities 输出概率数组
 * @param batch_size 批次大小（<=0 自动 64，>1024 截断 1024）
 *
 * @return 0 成功，<0 失败（错误码）
 */
int kadd_inference_predict_batch(kadd_inference_engine_t *engine,
                                  const float *features,
                                  int n,
                                  float *probabilities,
                                  int batch_size);

/**
 * @brief 获取模型输入维度
 *
 * @param engine 推理引擎句柄
 * @return 输入维度（>0）；engine 为 NULL 时返回 KADD_ERROR_NULL_PTR
 */
int kadd_inference_engine_get_feature_dim(kadd_inference_engine_t *engine);

/**
 * @brief 释放推理引擎并清空调用者句柄
 *
 * @param engine 推理引擎句柄指针的指针；释放成功后 *engine 为 NULL
 *
 * @note 传入 NULL 或「指向 NULL 句柄的地址」均为空操作，消除悬空句柄导致的
 *       use-after-free / double-free
 */
void kadd_inference_engine_free(kadd_inference_engine_t **engine);

#ifdef __cplusplus
}
#endif

#endif /* KADD_INFERENCE_ENGINE_H */
