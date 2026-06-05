/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * @file kadd_config_loader.h
 * @brief 配置文件加载器 - 从config.json读取引擎配置
 */

#ifndef KADD_CONFIG_LOADER_H
#define KADD_CONFIG_LOADER_H

#ifdef __cplusplus
extern "C" {
#endif

/* 配置结构中路径字段的缓冲区大小 */
#define KADD_CONFIG_PATH_MAX_LEN 512

/* ========== 批处理配置常量 ========== */
#define KADD_CONFIG_MAX_SAMPLE_COUNT 10000000      /* 批处理最大样本数（防止 int 溢出） */

/* ========== 批处理大小约束 ========== */
#define KADD_DEFAULT_BATCH_SIZE   64               /* 默认批次大小 */
#define KADD_MAX_BATCH_SIZE       1024             /* 最大批次大小 */

/* ========== 配置项默认值 ========== */
#define KADD_CONFIG_DEFAULT_MAX_INPUT_LENGTH 300
#define KADD_CONFIG_DEFAULT_MAX_OUTPUT_DIM   300
#define KADD_CONFIG_DEFAULT_FEATURE_DIM      28
#define KADD_CONFIG_DEFAULT_WINDOW_SIZE      5

/* ========== 文件大小限制 ========== */
#define KADD_CONFIG_MAX_FILE_SIZE    (10 * 1024 * 1024)  /* 配置文件最大大小（10MB） */

/* ========== DFA引擎配置 ========== */

typedef struct {
    char transition_table_path[KADD_CONFIG_PATH_MAX_LEN];
    char output_table_path[KADD_CONFIG_PATH_MAX_LEN];
    char eod_table_path[KADD_CONFIG_PATH_MAX_LEN];
    int max_input_length;
    int max_output_dim;
} kadd_dfa_config_t;

/* ========== ML引擎配置 ========== */

typedef struct {
    char model_path[KADD_CONFIG_PATH_MAX_LEN];
    int feature_dim;
    int window_size;
} kadd_ml_config_t;

/* ========== SQLi引擎配置 ========== */

typedef struct {
    char transition_table_path[KADD_CONFIG_PATH_MAX_LEN];
    char output_table_path[KADD_CONFIG_PATH_MAX_LEN];
    char eod_table_path[KADD_CONFIG_PATH_MAX_LEN];
    char ml_model_path[KADD_CONFIG_PATH_MAX_LEN];
    int feature_dim;
    int window_size;
    int max_input_length;
    int max_output_dim;
} kadd_sqli_config_t;

/* ========== Feature引擎配置 ========== */

typedef struct {
    char ml_model_path[512];
    int feature_dim;
    int window_size;
} kadd_feature_config_t;

/* ========== 统一配置结构 ========== */

typedef struct {
    kadd_dfa_config_t dfa;
    kadd_ml_config_t ml;
    kadd_sqli_config_t sqli;
    kadd_feature_config_t feature;
} kadd_config_t;

/* ========== API函数 ========== */

/**
 * @brief 从config.json加载配置
 * @param config_path 配置文件路径（NULL表示使用默认"config.json"）
 * @param config 输出配置结构
 * @return 0成功，非0失败
 */
int kadd_config_load(const char *config_path, kadd_config_t *config);

/**
 * @brief 释放配置资源
 */
void kadd_config_free(kadd_config_t *config);

#ifdef __cplusplus
}
#endif

#endif // KADD_CONFIG_LOADER_H
