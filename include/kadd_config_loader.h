/**
 * @file kadd_config_loader.h
 * @brief 配置文件加载器 - 从config.json读取引擎配置
 */

#ifndef KADD_CONFIG_LOADER_H
#define KADD_CONFIG_LOADER_H

#include "kadd_dfa_engine.h"
#include "kadd_ml_engine.h"
#include "kadd_sqli_engine.h"
#include "kadd_feature_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== DFA引擎配置 ========== */

typedef struct {
    kadd_dfa_table_paths_t table_paths;
    int max_input_length;
    int max_output_dim;
} kadd_dfa_config_t;

/* ========== ML引擎配置 ========== */

typedef struct {
    char model_path[512];
    int feature_dim;
    int window_size;
} kadd_ml_config_t;

/* ========== SQLi引擎配置 ========== */

typedef struct {
    char transition_table_path[512];
    char output_table_path[512];
    char eod_table_path[512];
    char ml_model_path[512];
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
