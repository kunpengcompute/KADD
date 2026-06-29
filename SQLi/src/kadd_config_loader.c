/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * @file kadd_config_loader.c
 * @brief 配置文件加载器实现
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "kadd_config_loader.h"
#include "kadd_errors.h"

/* 安全字符串拷贝：逐字符拷贝，保证null结尾 */
static void copy_str_field(char *dest, const char *src, size_t max_len)
{
    size_t i = 0;
    while (i < max_len - 1 && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

/* ========== 配置解析辅助函数 ========== */

/**
 * @brief 解析字符串字段
 * @param parent JSON父对象
 * @param field_name 字段名
 * @param dest 目标缓冲区
 * @param max_len 缓冲区最大长度
 * @param section_name 所属配置节名称（用于错误消息）
 * @return 成功返回 KADD_SUCCESS，失败返回 KADD_ERROR_CONFIG
 */
static int parse_string_field(cJSON *parent, const char *field_name,
                              char *dest, size_t max_len, const char *section_name)
{
    cJSON *item = cJSON_GetObjectItem(parent, field_name);
    if (!item || !item->valuestring || !item->valuestring[0]) {
        fprintf(stderr, "[kadd_config] ERROR: Missing or invalid '%s' in %s\n",
                field_name, section_name);
        return KADD_ERROR_CONFIG;
    }
    copy_str_field(dest, item->valuestring, max_len);
    return KADD_SUCCESS;
}

/**
 * @brief 解析无符号整数字段（必须为正数）
 * @param parent JSON父对象
 * @param field_name 字段名
 * @param dest 目标整数指针
 * @param section_name 所属配置节名称（用于错误消息）
 * @param min_val 最小值（包含）
 * @param max_val 最大值（包含）
 * @return 成功返回 KADD_SUCCESS，失败返回 KADD_ERROR_INVALID_PARAM 或 KADD_ERROR_CONFIG
 */
static int parse_uint_field(cJSON *parent, const char *field_name,
                            int *dest, const char *section_name,
                            int min_val, int max_val)
{
    cJSON *item = cJSON_GetObjectItem(parent, field_name);
    if (!item || !cJSON_IsNumber(item)) {
        fprintf(stderr, "[kadd_config] ERROR: Missing or invalid '%s' in %s\n",
                field_name, section_name);
        return KADD_ERROR_CONFIG;
    }

    int value = item->valueint;
    if (value < min_val || value > max_val) {
        fprintf(stderr, "[kadd_config] ERROR: '%s' in %s must be between %d and %d (got: %d)\n",
                field_name, section_name, min_val, max_val, value);
        return KADD_ERROR_INVALID_PARAM;
    }

    *dest = value;
    return KADD_SUCCESS;
}

/* ========== 各引擎配置解析 ========== */

static int parse_dfa_config(cJSON *parent, kadd_dfa_config_t *config)
{
    int ret;
    if ((ret = parse_string_field(parent, "transition_table_path", config->transition_table_path,
                         KADD_CONFIG_PATH_MAX_LEN, "kadd_dfa_engine")) != KADD_SUCCESS)
        return ret;
    if ((ret = parse_string_field(parent, "output_table_path", config->output_table_path,
                         KADD_CONFIG_PATH_MAX_LEN, "kadd_dfa_engine")) != KADD_SUCCESS)
        return ret;
    if ((ret = parse_string_field(parent, "eod_table_path", config->eod_table_path,
                         KADD_CONFIG_PATH_MAX_LEN, "kadd_dfa_engine")) != KADD_SUCCESS)
        return ret;
    // max_input_length 必须 >= 1，最大使用头文件定义的常量
    if ((ret = parse_uint_field(parent, "max_input_length", &config->max_input_length,
                         "kadd_dfa_engine", 1, KADD_CONFIG_MAX_INPUT_LENGTH)) != KADD_SUCCESS)
        return ret;
    // max_output_dim 必须 >= 1，最大使用头文件定义的常量
    if ((ret = parse_uint_field(parent, "max_output_dim", &config->max_output_dim,
                         "kadd_dfa_engine", 1, KADD_CONFIG_MAX_OUTPUT_DIM)) != KADD_SUCCESS)
        return ret;
    return KADD_SUCCESS;
}

static int parse_ml_config(cJSON *parent, kadd_ml_config_t *config)
{
    int ret;
    if ((ret = parse_string_field(parent, "model_path", config->model_path,
                         KADD_CONFIG_PATH_MAX_LEN, "kadd_ml_engine")) != KADD_SUCCESS)
        return ret;
    // feature_dim 必须 >= 1，最大使用头文件定义的常量
    if ((ret = parse_uint_field(parent, "feature_dim", &config->feature_dim,
                         "kadd_ml_engine", 1, KADD_CONFIG_MAX_FEATURE_DIM)) != KADD_SUCCESS)
        return ret;
    // window_size 必须 >= 1，最大使用头文件定义的常量
    if ((ret = parse_uint_field(parent, "window_size", &config->window_size,
                         "kadd_ml_engine", 1, KADD_CONFIG_MAX_WINDOW_SIZE)) != KADD_SUCCESS)
        return ret;
    return KADD_SUCCESS;
}

static int parse_sqli_config(cJSON *parent, kadd_sqli_config_t *config)
{
    int ret;
    if ((ret = parse_string_field(parent, "transition_table_path", config->transition_table_path,
                         KADD_CONFIG_PATH_MAX_LEN, "kadd_sqli_engine")) != KADD_SUCCESS)
        return ret;
    if ((ret = parse_string_field(parent, "output_table_path", config->output_table_path,
                         KADD_CONFIG_PATH_MAX_LEN, "kadd_sqli_engine")) != KADD_SUCCESS)
        return ret;
    if ((ret = parse_string_field(parent, "eod_table_path", config->eod_table_path,
                         KADD_CONFIG_PATH_MAX_LEN, "kadd_sqli_engine")) != KADD_SUCCESS)
        return ret;
    if ((ret = parse_string_field(parent, "ml_model_path", config->ml_model_path,
                         KADD_CONFIG_PATH_MAX_LEN, "kadd_sqli_engine")) != KADD_SUCCESS)
        return ret;
    // feature_dim 必须 >= 1，最大使用头文件定义的常量
    if ((ret = parse_uint_field(parent, "feature_dim", &config->feature_dim,
                         "kadd_sqli_engine", 1, KADD_CONFIG_MAX_FEATURE_DIM)) != KADD_SUCCESS)
        return ret;
    // window_size 必须 >= 1，最大使用头文件定义的常量
    if ((ret = parse_uint_field(parent, "window_size", &config->window_size,
                         "kadd_sqli_engine", 1, KADD_CONFIG_MAX_WINDOW_SIZE)) != KADD_SUCCESS)
        return ret;
    // max_input_length 必须 >= 1，最大使用头文件定义的常量
    if ((ret = parse_uint_field(parent, "max_input_length", &config->max_input_length,
                         "kadd_sqli_engine", 1, KADD_CONFIG_MAX_INPUT_LENGTH)) != KADD_SUCCESS)
        return ret;
    // max_output_dim 必须 >= 1，最大使用头文件定义的常量
    if ((ret = parse_uint_field(parent, "max_output_dim", &config->max_output_dim,
                         "kadd_sqli_engine", 1, KADD_CONFIG_MAX_OUTPUT_DIM)) != KADD_SUCCESS)
        return ret;
    return KADD_SUCCESS;
}

static int parse_feature_config(cJSON *root, kadd_feature_config_t *config)
{
    cJSON *feature = cJSON_GetObjectItem(root, "kadd_feature_engine");
    if (!feature) {
        fprintf(stderr, "[kadd_config] ERROR: Missing 'kadd_feature_engine' section in config\n");
        return KADD_ERROR_CONFIG;
    }

    int ret;
    if ((ret = parse_string_field(feature, "ml_model_path", config->ml_model_path,
                         KADD_CONFIG_PATH_MAX_LEN, "kadd_feature_engine")) != KADD_SUCCESS)
        return ret;
    // feature_dim 必须 >= 1，最大使用头文件定义的常量
    if ((ret = parse_uint_field(feature, "feature_dim", &config->feature_dim,
                         "kadd_feature_engine", 1, KADD_CONFIG_MAX_FEATURE_DIM)) != KADD_SUCCESS)
        return ret;
    // window_size 必须 >= 1，最大使用头文件定义的常量
    if ((ret = parse_uint_field(feature, "window_size", &config->window_size,
                         "kadd_feature_engine", 1, KADD_CONFIG_MAX_WINDOW_SIZE)) != KADD_SUCCESS)
        return ret;
    return KADD_SUCCESS;
}

/* 从 JSON root 对象填充配置结构 */
static int parse_all_configs(cJSON *root, kadd_config_t *config)
{
    *config = (kadd_config_t){0};

    cJSON *dfa = cJSON_GetObjectItem(root, "kadd_dfa_engine");
    if (!dfa) {
        fprintf(stderr, "[kadd_config] ERROR: Missing 'kadd_dfa_engine' section in config\n");
        return KADD_ERROR_CONFIG;
    }
    int ret;
    if ((ret = parse_dfa_config(dfa, &config->dfa)) != KADD_SUCCESS) {
        return ret;
    }

    cJSON *ml = cJSON_GetObjectItem(root, "kadd_ml_engine");
    if (!ml) {
        fprintf(stderr, "[kadd_config] ERROR: Missing 'kadd_ml_engine' section in config\n");
        return KADD_ERROR_CONFIG;
    }
    if ((ret = parse_ml_config(ml, &config->ml)) != KADD_SUCCESS) {
        return ret;
    }

    cJSON *sqli = cJSON_GetObjectItem(root, "kadd_sqli_engine");
    if (!sqli) {
        fprintf(stderr, "[kadd_config] ERROR: Missing 'kadd_sqli_engine' section in config\n");
        return KADD_ERROR_CONFIG;
    }
    if ((ret = parse_sqli_config(sqli, &config->sqli)) != KADD_SUCCESS) {
        return ret;
    }

    if ((ret = parse_feature_config(root, &config->feature)) != KADD_SUCCESS) {
        return ret;
    }

    return KADD_SUCCESS;
}

/* ========== 文件读取 ========== */

static int read_file_to_string(const char *path, char **out_content)
{
    FILE *file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "[kadd_config] ERROR: Failed to open file: %s\n", path);
        return KADD_ERROR_FILE_OPERATION_FAILED;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "[kadd_config] ERROR: Failed to seek file: %s\n", path);
        fclose(file);
        return KADD_ERROR_FILE_OPERATION_FAILED;
    }
    long file_size = ftell(file);
    // 配置文件不应超过 10MB，防止恶意大文件导致内存问题
    if (file_size <= 0) {
        fprintf(stderr, "[kadd_config] ERROR: Invalid file size: %ld (max: %d)\n",
                file_size, KADD_CONFIG_MAX_FILE_SIZE);
        fclose(file);
        return KADD_ERROR_CONFIG;
    }
    if (file_size > KADD_CONFIG_MAX_FILE_SIZE) {
        fprintf(stderr, "[kadd_config] ERROR: File size exceeds maximum: %ld (max: %d)\n",
                file_size, KADD_CONFIG_MAX_FILE_SIZE);
        fclose(file);
        return KADD_ERROR_FILE_OPERATION_FAILED;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "[kadd_config] ERROR: Failed to seek file: %s\n", path);
        fclose(file);
        return KADD_ERROR_FILE_OPERATION_FAILED;
    }

    char *content = (char *)calloc(file_size + 1, sizeof(char));
    if (!content) {
        fprintf(stderr, "[kadd_config] ERROR: Failed to allocate memory for file content (%ld bytes)\n", file_size);
        fclose(file);
        return KADD_ERROR_OUT_OF_MEMORY;
    }

    size_t bytes_read = fread(content, 1, file_size, file);
    content[file_size] = '\0';
    (void)fclose(file);

    if (bytes_read != (size_t)file_size) {
        fprintf(stderr,
                "[kadd_config] ERROR: Incomplete file read: expected %ld, got %zu bytes\n",
                file_size, bytes_read);
        free(content);
        return KADD_ERROR_FILE_OPERATION_FAILED;
    }

    *out_content = content;
    return KADD_SUCCESS;
}

/* ========== 公共API ========== */

int kadd_config_load(const char *config_path, kadd_config_t *config)
{
    if (!config) {
        fprintf(stderr, "[kadd_config] ERROR: config pointer is NULL\n");
        return KADD_ERROR_NULL_PTR;
    }

    if (!config_path) {
        fprintf(stderr, "[kadd_config] ERROR: config_path is NULL (required parameter)\n");
        return KADD_ERROR_INVALID_INPUT;
    }

    char *content;
    int ret;
    if ((ret = read_file_to_string(config_path, &content)) != KADD_SUCCESS) {
        fprintf(stderr, "[kadd_config] ERROR: Failed to read config file: %s\n", config_path);
        return ret;
    }

    cJSON *root = cJSON_Parse(content);
    free(content);

    if (!root) {
        fprintf(stderr, "[kadd_config] ERROR: JSON parse failed for file: %s\n", config_path);
        return KADD_ERROR_CONFIG;
    }

    if (root->type != cJSON_Object) {
        fprintf(stderr, "[kadd_config] ERROR: JSON root must be an object in file: %s\n", config_path);
        cJSON_Delete(root);
        return KADD_ERROR_CONFIG;
    }

    if ((ret = parse_all_configs(root, config)) != KADD_SUCCESS) {
        cJSON_Delete(root);
        return ret;
    }

    cJSON_Delete(root);
    return KADD_SUCCESS;
}

void kadd_config_free(kadd_config_t *config)
{
    // 目前没有动态分配的内存，不需要释放
    (void)config;
}
