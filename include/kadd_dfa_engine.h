#ifndef KADD_DFA_ENGINE_H
#define KADD_DFA_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief DFA 引擎结构
 */
typedef struct kadd_dfa_engine_t kadd_dfa_engine_t;

/**
 * @brief 创建 DFA 引擎
 *
 * @param engine 输出参数，返回创建的引擎指针
 * @param table_path 转移表文件路径
 * @param state_path 状态元数据文件路径
 * @param max_input_length 最大输入字符长度
 * @param max_output_dim 最大输出特征维度
 * @return 0 成功，非0 失败
 */
int kadd_dfa_engine_create(kadd_dfa_engine_t **engine,
                           const char *table_path,
                           const char *state_path,
                           size_t max_input_length,
                           size_t max_output_dim);

/**
 * @brief 释放 DFA 引擎
 *
 * @param e DFA 引擎指针
 */
void kadd_dfa_engine_free(kadd_dfa_engine_t *e);

/**
 * @brief 分词输入字符串
 *
 * @param e DFA 引擎
 * @param input 输入字符串
 * @param input_length 输入字符串长度
 * @param output 输出数组（存储token索引）
 * @param dim 输出参数，返回实际的token数量
 * @return 0 成功，非0 失败
 */
int kadd_dfa_engine_tokenize(const kadd_dfa_engine_t *e,
                             const char *input,
                             size_t input_length,
                             int *output,
                             int *dim);

#ifdef __cplusplus
}
#endif

#endif /* KADD_DFA_ENGINE_H */
