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
 * @return 成功返回0，失败返回-1
 */
static int parse_string_field(cJSON *parent, const char *field_name,
                              char *dest, size_t max_len, const char *section_name)
{
    cJSON *item = cJSON_GetObjectItem(parent, field_name);
    if (!item || !item->valuestring || !item->valuestring[0]) {
        fprintf(stderr, "[kadd_config] ERROR: Missing or invalid '%s' in %s\n",
                field_name, section_name);
        return -1;
    }
    copy_str_field(dest, item->valuestring, max_len);
    return 0;
}

/**
 * @brief 解析整数字段
 * @param parent JSON父对象
 * @param field_name 字段名
 * @param dest 目标整数指针
 * @param section_name 所属配置节名称（用于错误消息）
 * @return 成功返回0，失败返回-1
 */
static int parse_int_field(cJSON *parent, const char *field_name,
                          int *dest, const char *section_name)
{
    cJSON *item = cJSON_GetObjectItem(parent, field_name);
    if (!item || !cJSON_IsNumber(item)) {
        fprintf(stderr, "[kadd_config] ERROR: Missing or invalid '%s' in %s\n",
                field_name, section_name);
        return -1;
    }
    *dest = item->valueint;
    return 0;
}

/* ========== 各引擎配置解析 ========== */

static int parse_dfa_config(cJSON *parent, kadd_dfa_config_t *config)
{
    if (parse_string_field(parent, "transition_table_path", config->transition_table_path,
                         KADD_CONFIG_PATH_MAX_LEN, "kadd_dfa_engine") != 0) return -1;
    if (parse_string_field(parent, "output_table_path", config->output_table_path,
                         KADD_CONFIG_PATH_MAX_LEN, "kadd_dfa_engine") != 0) return -1;
    if (parse_string_field(parent, "eod_table_path", config->eod_table_path,
                         KADD_CONFIG_PATH_MAX_LEN, "kadd_dfa_engine") != 0) return -1;
    if (parse_int_field(parent, "max_input_length", &config->max_input_length,
                       "kadd_dfa_engine") != 0) return -1;
    if (parse_int_field(parent, "max_output_dim", &config->max_output_dim,
                       "kadd_dfa_engine") != 0) return -1;
    return 0;
}

static int parse_ml_config(cJSON *parent, kadd_ml_config_t *config)
{
    if (parse_string_field(parent, "model_path", config->model_path,
                         KADD_CONFIG_PATH_MAX_LEN, "kadd_ml_engine") != 0) return -1;
    if (parse_int_field(parent, "feature_dim", &config->feature_dim,
                       "kadd_ml_engine") != 0) return -1;
    if (parse_int_field(parent, "window_size", &config->window_size,
                       "kadd_ml_engine") != 0) return -1;
    return 0;
}

static int parse_sqli_config(cJSON *parent, kadd_sqli_config_t *config)
{
    if (parse_string_field(parent, "transition_table_path", config->transition_table_path,
                         KADD_CONFIG_PATH_MAX_LEN, "kadd_sqli_engine") != 0) return -1;
    if (parse_string_field(parent, "output_table_path", config->output_table_path,
                         KADD_CONFIG_PATH_MAX_LEN, "kadd_sqli_engine") != 0) return -1;
    if (parse_string_field(parent, "eod_table_path", config->eod_table_path,
                         KADD_CONFIG_PATH_MAX_LEN, "kadd_sqli_engine") != 0) return -1;
    if (parse_string_field(parent, "ml_model_path", config->ml_model_path,
                         KADD_CONFIG_PATH_MAX_LEN, "kadd_sqli_engine") != 0) return -1;
    if (parse_int_field(parent, "feature_dim", &config->feature_dim,
                       "kadd_sqli_engine") != 0) return -1;
    if (parse_int_field(parent, "window_size", &config->window_size,
                       "kadd_sqli_engine") != 0) return -1;
    if (parse_int_field(parent, "max_input_length", &config->max_input_length,
                       "kadd_sqli_engine") != 0) return -1;
    if (parse_int_field(parent, "max_output_dim", &config->max_output_dim,
                       "kadd_sqli_engine") != 0) return -1;
    return 0;
}

static int parse_feature_config(cJSON *root, kadd_feature_config_t *config)
{
    cJSON *feature = cJSON_GetObjectItem(root, "kadd_feature_engine");
    if (!feature) {
        fprintf(stderr, "[kadd_config] ERROR: Missing 'kadd_feature_engine' section in config\n");
        return -1;
    }

    if (parse_string_field(feature, "ml_model_path", config->ml_model_path,
                         KADD_CONFIG_PATH_MAX_LEN, "kadd_feature_engine") != 0) return -1;
    if (parse_int_field(feature, "feature_dim", &config->feature_dim,
                       "kadd_feature_engine") != 0) return -1;
    if (parse_int_field(feature, "window_size", &config->window_size,
                       "kadd_feature_engine") != 0) return -1;
    return 0;
}

/* 从 JSON root 对象填充配置结构 */
static int parse_all_configs(cJSON *root, kadd_config_t *config)
{
    *config = (kadd_config_t){0};

    cJSON *dfa = cJSON_GetObjectItem(root, "kadd_dfa_engine");
    if (!dfa) {
        fprintf(stderr, "[kadd_config] ERROR: Missing 'kadd_dfa_engine' section in config\n");
        return -1;
    }
    if (parse_dfa_config(dfa, &config->dfa) != 0) {
        return -1;
    }

    cJSON *ml = cJSON_GetObjectItem(root, "kadd_ml_engine");
    if (!ml) {
        fprintf(stderr, "[kadd_config] ERROR: Missing 'kadd_ml_engine' section in config\n");
        return -1;
    }
    if (parse_ml_config(ml, &config->ml) != 0) {
        return -1;
    }

    cJSON *sqli = cJSON_GetObjectItem(root, "kadd_sqli_engine");
    if (!sqli) {
        fprintf(stderr, "[kadd_config] ERROR: Missing 'kadd_sqli_engine' section in config\n");
        return -1;
    }
    if (parse_sqli_config(sqli, &config->sqli) != 0) {
        return -1;
    }

    if (parse_feature_config(root, &config->feature) != 0) {
        return -1;
    }

    return 0;
}

/* ========== 文件读取 ========== */

static int read_file_to_string(const char *path, char **out_content)
{
    FILE *file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "[kadd_config] ERROR: Failed to open file: %s\n", path);
        return -1;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "[kadd_config] ERROR: Failed to seek file: %s\n", path);
        fclose(file);
        return -1;
    }
    long file_size = ftell(file);
    // 配置文件不应超过 10MB，防止恶意大文件导致内存问题
    if (file_size <= 0 || file_size > KADD_CONFIG_MAX_FILE_SIZE) {
        fprintf(stderr, "[kadd_config] ERROR: Invalid file size: %ld (max: %d)\n",
                file_size, KADD_CONFIG_MAX_FILE_SIZE);
        fclose(file);
        return -1;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "[kadd_config] ERROR: Failed to seek file: %s\n", path);
        fclose(file);
        return -1;
    }

    char *content = (char *)malloc(file_size + 1);
    if (!content) {
        fprintf(stderr, "[kadd_config] ERROR: Failed to allocate memory for file content (%ld bytes)\n", file_size);
        fclose(file);
        return -1;
    }

    size_t bytes_read = fread(content, 1, file_size, file);
    content[file_size] = '\0';
    (void)fclose(file);

    if (bytes_read != (size_t)file_size) {
        fprintf(stderr,
                "[kadd_config] ERROR: Incomplete file read: expected %ld, got %zu bytes\n",
                file_size, bytes_read);
        free(content);
        return -1;
    }

    *out_content = content;
    return 0;
}

/* ========== 公共API ========== */

int kadd_config_load(const char *config_path, kadd_config_t *config)
{
    if (!config) {
        fprintf(stderr, "[kadd_config] ERROR: config pointer is NULL\n");
        return -1;
    }

    if (!config_path) {
        fprintf(stderr, "[kadd_config] ERROR: config_path is NULL (required parameter)\n");
        return -1;
    }

    char *content;
    if (read_file_to_string(config_path, &content) != 0) {
        fprintf(stderr, "[kadd_config] ERROR: Failed to read config file: %s\n", config_path);
        return -1;
    }

    cJSON *root = cJSON_Parse(content);
    free(content);

    if (!root) {
        fprintf(stderr, "[kadd_config] ERROR: JSON parse failed for file: %s\n", config_path);
        return -1;
    }

    if (root->type != cJSON_Object) {
        fprintf(stderr, "[kadd_config] ERROR: JSON root must be an object in file: %s\n", config_path);
        cJSON_Delete(root);
        return -1;
    }

    if (parse_all_configs(root, config) != 0) {
        cJSON_Delete(root);
        return -1;
    }

    cJSON_Delete(root);
    return 0;
}

void kadd_config_free(kadd_config_t *config)
{
    // 目前没有动态分配的内存，不需要释放
    (void)config;
}
