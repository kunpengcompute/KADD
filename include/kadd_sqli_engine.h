#ifndef KADD_SQLI_ENGINE_H
#define KADD_SQLI_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file kadd_sqli_engine.h
 * @brief SQL 注入检测引擎接口
 *
 * 整合 DFA 引擎和 ML 引擎，提供完整的 SQL 注入检测功能
 * 流程: 输入 -> DFA 分词 -> 特征提取 -> ML 推理 -> 结果
 */

/**
 * @brief SQL 注入检测引擎错误代码
 */
typedef enum {
    KADD_SQLI_OK = 0,                   /**< 成功 */
    KADD_SQLI_ERROR_MEMORY = -1,         /**< 内存分配失败 */
    KADD_SQLI_ERROR_INVALID_INPUT = -2,  /**< 输入参数无效 */
    KADD_SQLI_ERROR_DFA = -3,            /**< DFA引擎错误 */
    KADD_SQLI_ERROR_FEATURE = -4,        /**< 特征提取错误 */
    KADD_SQLI_ERROR_ML = -5,             /**< ML推理错误 */
    KADD_SQLI_ERROR_CONFIG = -6,         /**< 配置错误 */
} kadd_sqli_error_code_t;

/**
 * @brief SQL 注入检测引擎句柄（不透明指针）
 */
typedef struct kadd_sqli_engine_t kadd_sqli_engine_t;

/**
 * @brief 从配置文件创建 SQL 注入检测引擎
 *
 * @param engine 引擎句柄指针的指针
 * @param config_file JSON 配置文件路径 (如 "config.json")
 *
 * @return KADD_SQLI_OK 成功, 其他值表示失败
 *
 * @note 配置文件需包含 kadd_dfa_engine、kadd_ml_engine、kadd_feature_engine、kadd_sqli_engine 等字段
 * @note 引擎句柄由函数分配，使用完毕后需调用 kadd_sqli_engine_free 释放
 *
 * @example
 * @code
 * kadd_sqli_engine_t *engine = NULL;
 * if (kadd_sqli_engine_create_from_file(&engine, "config.json") == KADD_SQLI_OK) {
 *     float probability;
 *     if (kadd_sqli_detect(engine, "id=1' OR '1'='1", &probability) == KADD_SQLI_OK) {
 *         if (probability >= 0.5f) {
 *             printf("SQL 注入 detected! Probability: %.2f\n", probability);
 *         }
 *     }
 *     kadd_sqli_engine_free(engine);
 * }
 * @endcode
 */
int kadd_sqli_engine_create_from_file(kadd_sqli_engine_t **engine, const char *config_file);

/**
 * @brief 检测单个输入
 *
 * @param engine 引擎句柄
 * @param input 输入字符串（如 URL 参数）
 * @param probability 输出概率值指针 [0, 1]
 *
 * @return KADD_SQLI_OK 成功, 其他值表示失败
 *
 * @note 概率值 >= 0.5 时通常判定为 SQL 注入（可通过配置文件调整阈值）
 *
 * @example
 * @code
 * float probability;
 * if (kadd_sqli_detect(engine, "id=1' OR '1'='1", &probability) == KADD_SQLI_OK) {
 *     printf("Probability: %.2f%%\n", probability * 100);
 * }
 * @endcode
 */
int kadd_sqli_detect(kadd_sqli_engine_t *engine, const char *input, float *probability);

/**
 * @brief 批量检测
 *
 * @param engine 引擎句柄
 * @param inputs 输入字符串数组
 * @param count 输入数量
 * @param probabilities 输出概率数组（需预先分配，至少 count 个元素）
 * @param batch_size ML推理批次大小（建议值：32/64/128，0表示自动选择）
 *
 * @return 成功检测的数量，负值表示失败
 *
 * @note 使用 ML 引擎的批量推理能力，性能优于逐个检测
 * @note 即使某个输入失败也会继续处理其他输入
 * @note 失败的输入对应概率值设为 -1.0
 *
 * @example
 * @code
 * const char *inputs[] = {"id=1", "id=1' OR '1'='1", "search=test"};
 * float probabilities[3];
 *
 * int detected = kadd_sqli_detect_batch(engine, inputs, 3, probabilities, 64);
 * printf("成功检测: %d/3\n", detected);
 *
 * for (int i = 0; i < 3; i++) {
 *     if (probabilities[i] >= 0) {
 *         printf("Input %d: %.4f (%s)\n", i, probabilities[i],
 *                probabilities[i] >= 0.5f ? "SQLi" : "Normal");
 *     }
 * }
 * @endcode
 */
int kadd_sqli_detect_batch(kadd_sqli_engine_t *engine, const char **inputs, int count, float *probabilities, int batch_size);

/**
 * @brief 释放引擎
 *
 * @param engine 引擎句柄
 *
 * @note 释放后引擎句柄不可再使用
 */
void kadd_sqli_engine_free(kadd_sqli_engine_t *engine);

#ifdef __cplusplus
}
#endif

#endif // KADD_SQLI_ENGINE_H

