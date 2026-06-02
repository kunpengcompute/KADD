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

/* 配置项默认值 */
#define KADD_CONFIG_DEFAULT_MAX_INPUT_LENGTH 300
#define KADD_CONFIG_DEFAULT_MAX_OUTPUT_DIM 300
#define KADD_CONFIG_DEFAULT_FEATURE_DIM 28
#define KADD_CONFIG_DEFAULT_WINDOW_SIZE 5

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

/* ========== 配置默认值初始化 ========== */

static void init_default_config(kadd_config_t *config)
{
    *config = (kadd_config_t){0};
    config->dfa.max_input_length = KADD_CONFIG_DEFAULT_MAX_INPUT_LENGTH;
    config->dfa.max_output_dim = KADD_CONFIG_DEFAULT_MAX_OUTPUT_DIM;
    config->ml.feature_dim = KADD_CONFIG_DEFAULT_FEATURE_DIM;
    config->ml.window_size = KADD_CONFIG_DEFAULT_WINDOW_SIZE;
    config->sqli.feature_dim = KADD_CONFIG_DEFAULT_FEATURE_DIM;
    config->sqli.window_size = KADD_CONFIG_DEFAULT_WINDOW_SIZE;
    config->sqli.max_input_length = KADD_CONFIG_DEFAULT_MAX_INPUT_LENGTH;
    config->sqli.max_output_dim = KADD_CONFIG_DEFAULT_MAX_OUTPUT_DIM;
    config->feature.feature_dim = KADD_CONFIG_DEFAULT_FEATURE_DIM;
    config->feature.window_size = KADD_CONFIG_DEFAULT_WINDOW_SIZE;
}

/* ========== 各引擎配置解析 ========== */

static void parse_dfa_config(cJSON *parent, kadd_dfa_config_t *config)
{
    cJSON *item = cJSON_GetObjectItem(parent, "transition_table_path");
    if (item && item->valuestring) {
        copy_str_field(config->transition_table_path, item->valuestring,
                       KADD_CONFIG_PATH_MAX_LEN);
    }

    item = cJSON_GetObjectItem(parent, "output_table_path");
    if (item && item->valuestring) {
        copy_str_field(config->output_table_path, item->valuestring,
                       KADD_CONFIG_PATH_MAX_LEN);
    }

    item = cJSON_GetObjectItem(parent, "eod_table_path");
    if (item && item->valuestring) {
        copy_str_field(config->eod_table_path, item->valuestring,
                       KADD_CONFIG_PATH_MAX_LEN);
    }

    item = cJSON_GetObjectItem(parent, "max_input_length");
    if (item && cJSON_IsNumber(item)) {
        config->max_input_length = item->valueint;
    }

    item = cJSON_GetObjectItem(parent, "max_output_dim");
    if (item && cJSON_IsNumber(item)) {
        config->max_output_dim = item->valueint;
    }
}

static void parse_ml_config(cJSON *parent, kadd_ml_config_t *config)
{
    cJSON *item = cJSON_GetObjectItem(parent, "model_path");
    if (item && item->valuestring) {
        copy_str_field(config->model_path, item->valuestring,
                       KADD_CONFIG_PATH_MAX_LEN);
    }

    item = cJSON_GetObjectItem(parent, "feature_dim");
    if (item && cJSON_IsNumber(item)) {
        config->feature_dim = item->valueint;
    }

    item = cJSON_GetObjectItem(parent, "window_size");
    if (item && cJSON_IsNumber(item)) {
        config->window_size = item->valueint;
    }
}

static void parse_sqli_config(cJSON *parent, kadd_sqli_config_t *config)
{
    cJSON *item;

    item = cJSON_GetObjectItem(parent, "transition_table_path");
    if (item && item->valuestring) {
        copy_str_field(config->transition_table_path, item->valuestring,
                       KADD_CONFIG_PATH_MAX_LEN);
    }

    item = cJSON_GetObjectItem(parent, "output_table_path");
    if (item && item->valuestring) {
        copy_str_field(config->output_table_path, item->valuestring,
                       KADD_CONFIG_PATH_MAX_LEN);
    }

    item = cJSON_GetObjectItem(parent, "eod_table_path");
    if (item && item->valuestring) {
        copy_str_field(config->eod_table_path, item->valuestring,
                       KADD_CONFIG_PATH_MAX_LEN);
    }

    item = cJSON_GetObjectItem(parent, "ml_model_path");
    if (item && item->valuestring) {
        copy_str_field(config->ml_model_path, item->valuestring,
                       KADD_CONFIG_PATH_MAX_LEN);
    }

    item = cJSON_GetObjectItem(parent, "feature_dim");
    if (item && cJSON_IsNumber(item)) {
        config->feature_dim = item->valueint;
    }

    item = cJSON_GetObjectItem(parent, "window_size");
    if (item && cJSON_IsNumber(item)) {
        config->window_size = item->valueint;
    }

    item = cJSON_GetObjectItem(parent, "max_input_length");
    if (item && cJSON_IsNumber(item)) {
        config->max_input_length = item->valueint;
    }

    item = cJSON_GetObjectItem(parent, "max_output_dim");
    if (item && cJSON_IsNumber(item)) {
        config->max_output_dim = item->valueint;
    }
}

static void parse_feature_config(cJSON *root, kadd_feature_config_t *config)
{
    cJSON *feature = cJSON_GetObjectItem(root, "kadd_feature_engine");
    if (feature) {
        cJSON *item;

        item = cJSON_GetObjectItem(feature, "ml_model_path");
        if (item && item->valuestring) {
            copy_str_field(config->ml_model_path, item->valuestring,
                           KADD_CONFIG_PATH_MAX_LEN);
        }

        item = cJSON_GetObjectItem(feature, "feature_dim");
        if (item && cJSON_IsNumber(item)) {
            config->feature_dim = item->valueint;
        }

        item = cJSON_GetObjectItem(feature, "window_size");
        if (item && cJSON_IsNumber(item)) {
            config->window_size = item->valueint;
        }
    } else {
        // 如果没有单独的feature配置，从ml配置复制
        cJSON *ml_item = cJSON_GetObjectItem(root, "kadd_ml_engine");
        if (ml_item) {
            cJSON *item;

            item = cJSON_GetObjectItem(ml_item, "model_path");
            if (item && item->valuestring) {
                copy_str_field(config->ml_model_path, item->valuestring,
                               KADD_CONFIG_PATH_MAX_LEN);
            }

            item = cJSON_GetObjectItem(ml_item, "feature_dim");
            if (item && cJSON_IsNumber(item)) {
                config->feature_dim = item->valueint;
            }

            item = cJSON_GetObjectItem(ml_item, "window_size");
            if (item && cJSON_IsNumber(item)) {
                config->window_size = item->valueint;
            }
        }
    }
}

/* 从 JSON root 对象填充配置结构 */
static void parse_all_configs(cJSON *root, kadd_config_t *config)
{
    init_default_config(config);

    cJSON *dfa = cJSON_GetObjectItem(root, "kadd_dfa_engine");
    if (dfa) {
        parse_dfa_config(dfa, &config->dfa);
    }

    cJSON *ml = cJSON_GetObjectItem(root, "kadd_ml_engine");
    if (ml) {
        parse_ml_config(ml, &config->ml);
    }

    cJSON *sqli = cJSON_GetObjectItem(root, "kadd_sqli_engine");
    if (sqli) {
        parse_sqli_config(sqli, &config->sqli);
    }

    parse_feature_config(root, &config->feature);
}

/* ========== 文件读取 ========== */

static int read_file_to_string(const char *path, char **out_content)
{
    FILE *file = fopen(path, "r");
    if (!file) {
        return -1;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }
    long file_size = ftell(file);
    if (file_size <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }

    char *content = (char *)malloc(file_size + 1);
    if (!content) {
        fclose(file);
        return -1;
    }

    size_t bytes_read = fread(content, 1, file_size, file);
    content[file_size] = '\0';
    (void)fclose(file);

    if (bytes_read != (size_t)file_size) {
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
        return -1;
    }

    const char *path = config_path ? config_path : "config.json";

    char *content;
    if (read_file_to_string(path, &content) != 0) {
        fprintf(stderr, "无法打开配置文件：%s\n", path);
        return -1;
    }
    
    cJSON *root = cJSON_Parse(content);
    free(content);

    if (!root) {
        fprintf(stderr, "配置文件JSON解析失败\n");
        return -1;
    }

    if (root->type != cJSON_Object) {
        fprintf(stderr, "配置文件JSON必须是对象\n");
        cJSON_Delete(root);
        return -1;
    }

    parse_all_configs(root, config);
    cJSON_Delete(root);
    return 0;
}

void kadd_config_free(kadd_config_t *config)
{
    // 目前没有动态分配的内存，不需要释放
    (void)config;
}
