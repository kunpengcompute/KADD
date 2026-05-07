/**
 * @file kadd_errors.h
 * @brief KADD 统一错误码定义
 *
 * 返回值语义统一：
 * - 0  : 完全成功
 * - >0 : 部分成功（返回成功处理的项目数量）
 * - <0 : 完全失败（错误码）
 */

#ifndef KADD_ERRORS_H
#define KADD_ERRORS_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 成功 */
#define KADD_SUCCESS                0

/** @brief 通用错误 */
#define KADD_ERROR                 -100

/** @brief 无效输入 */
#define KADD_ERROR_INVALID_INPUT   -101

/** @brief 空指针 */
#define KADD_ERROR_NULL_PTR        -102

/** @brief 内存不足 */
#define KADD_ERROR_OUT_OF_MEMORY   -103

/** @brief 引擎未初始化 */
#define KADD_ERROR_NOT_INIT       -104

/** @brief 无效参数 */
#define KADD_ERROR_INVALID_PARAM  -105

/** @brief 缓冲区太小 */
#define KADD_ERROR_BUFFER_TOO_SMALL -106

/** @brief 模型加载失败 */
#define KADD_ERROR_MODEL_LOAD_FAILED -107

/** @brief 文件操作失败 */
#define KADD_ERROR_FILE_OPERATION_FAILED -108

#ifdef __cplusplus
}
#endif

#endif /* KADD_ERRORS_H */
