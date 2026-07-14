/* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved. */
#ifndef KADD_DFA_ENGINE_H
#define KADD_DFA_ENGINE_H

#include <stddef.h>
#include <stdint.h>
#include "kadd_errors.h"

/* ========== DFA 批处理配置 ========== */
#define KADD_DFA_MAX_SAMPLE_COUNT   10000000  /* 批处理最大样本数 */
#define KADD_DFA_MAX_BATCH_SIZE     1024      /* 批次大小上限 */
#define KADD_DFA_DEFAULT_BATCH_SIZE 64        /* 默认批次大小 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief DFA 引擎结构
 */
typedef struct kadd_dfa_engine_t kadd_dfa_engine_t;

/**
 * @brief DFA 三张表路径
 */
typedef struct {
    const char *transition_table_path;  /**< 状态转移表路径 */
    const char *output_table_path;      /**< 边输出表路径 */
    const char *eod_table_path;         /**< 输入结束输出表路径 */
} kadd_dfa_table_paths_t;

/**
 * @brief DFA 批处理上下文
 */
typedef struct {
    const char **inputs;      /**< 输入字符串数组 */
    const size_t *lengths;    /**< 长度数组 */
    int count;                /**< 输入数量 */
    int **outputs;            /**< 输出token数组 */
    int *dims;                /**< 每个的token数量 */
    int batch_size;           /**< 批次大小（0或负数表示默认64，>1024自动限制为1024） */
} kadd_dfa_batch_t;

/**
 * @brief 创建 DFA 引擎
 *
 * @param engine 输出参数，返回创建的引擎指针
 * @param table_paths DFA 三张表路径
 * @param max_input_length 最大输入字符长度（必须 > 0）
 * @param max_output_dim 最大输出特征维度（必须 > 0）
 * @return 0 成功，<0 失败（错误码）
 */
int kadd_dfa_engine_create(kadd_dfa_engine_t **engine,
                           const kadd_dfa_table_paths_t *table_paths,
                           size_t max_input_length,
                           size_t max_output_dim);

/**
 * @brief 释放 DFA 引擎
 *
 * @param engine DFA 引擎指针
 */
void kadd_dfa_engine_free(kadd_dfa_engine_t *engine);

/**
 * @brief 分词输入字符串
 *
 * @param engine DFA 引擎
 * @param input 输入字符串
 * @param input_length 输入字符串长度
 * @param output 输出数组
 * @param token_count 输出参数，返回实际的token数量
 * @return 0 成功，<0 失败（错误码）
 */
int kadd_dfa_engine_tokenize(const kadd_dfa_engine_t *engine,
                             const char *input,
                             size_t input_length,
                             int *output,
                             int *token_count);

/**
 * @brief 批量分词多个字符串
 *
 * @param engine DFA 引擎
 * @param batch 批处理上下文
 * @return KADD_SUCCESS (0) 完全成功，<0 失败（错误码）
 *
 * @note 批处理是"全有或全无"设计：任何输入失败都会立即返回错误码
 * @note 失败时已处理的 outputs[i] 会被自动释放，调用者无需手动释放
 * @note 失败时 dims[i] 保持初始值 -1，outputs[i] 保持 NULL
 */
int kadd_dfa_engine_tokenize_batch(const kadd_dfa_engine_t *engine,
                                   kadd_dfa_batch_t *batch);

#ifdef __cplusplus
}
#endif

#endif /* KADD_DFA_ENGINE_H */
