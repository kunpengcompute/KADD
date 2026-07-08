/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * @file kadd_errors.h
 * @brief KADD 统一错误码定义
 *
 * 返回值语义统一：
 * - 0  : 成功 (KADD_SUCCESS)
 * - <0 : 失败（错误码）
 *
 * 编号规则：
 * - 通用错误码 : -100 范围
 * - DFA 专用   : -10 范围
 * - Feature 专用: -200 范围
 * - SQLi 专用   : -300 范围
 */

#ifndef KADD_ERRORS_H
#define KADD_ERRORS_H

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 通用错误码 (-100 范围) ========== */

/** @brief 成功 */
#define KADD_SUCCESS                     (0)

/** @brief 通用错误 */
#define KADD_ERROR                       (-100)

/** @brief 无效输入 */
#define KADD_ERROR_INVALID_INPUT         (-101)

/** @brief 空指针 */
#define KADD_ERROR_NULL_PTR              (-102)

/** @brief 内存不足 */
#define KADD_ERROR_OUT_OF_MEMORY         (-103)

/** @brief 引擎未初始化 */
#define KADD_ERROR_NOT_INIT              (-104)

/** @brief 无效参数 */
#define KADD_ERROR_INVALID_PARAM         (-105)

/** @brief 缓冲区太小 */
#define KADD_ERROR_BUFFER_TOO_SMALL      (-106)

/** @brief 模型加载失败 */
#define KADD_ERROR_MODEL_LOAD_FAILED     (-107)

/** @brief 文件操作失败 */
#define KADD_ERROR_FILE_OPERATION_FAILED (-108)

/** @brief 配置文件错误或缺失必需字段 */
#define KADD_ERROR_CONFIG                (-109)

/** @brief 模型推理失败 */
#define KADD_ERROR_INFERENCE_FAILED      (-110)

/* ========== DFA 引擎专用错误码 (-10 范围) ========== */

/** @brief DFA 文件未找到 */
#define KADD_DFA_ERROR_FILE_NOT_FOUND    (-10)

/** @brief DFA 文件解析失败 */
#define KADD_DFA_ERROR_PARSE             (-11)

/** @brief DFA 缓冲区溢出 */
#define KADD_DFA_ERROR_BUFFER_OVERFLOW   (-12)

/** @brief DFA 输入长度无效 */
#define KADD_DFA_ERROR_INVALID_LENGTH    (-13)

/* ========== Feature 引擎专用错误码 (-200 范围) ========== */

/** @brief Feature 序列元素超出范围 */
#define KADD_FEATURE_ERROR_VALUE_OUT_OF_RANGE  (-200)

/** @brief Feature 滑窗计算失败 */
#define KADD_FEATURE_ERROR_WINDOW_FAILED       (-201)

/* ========== SQLi 引擎专用错误码 (-300 范围) ========== */

/** @brief SQLi DFA 子引擎错误 */
#define KADD_SQLI_ERROR_DFA              (-300)

/** @brief SQLi Feature 子引擎错误 */
#define KADD_SQLI_ERROR_FEATURE          (-301)

/** @brief SQLi ML 子引擎错误 */
#define KADD_SQLI_ERROR_ML               (-302)

#ifdef __cplusplus
}
#endif

#endif /* KADD_ERRORS_H */