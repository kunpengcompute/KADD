/**
 * @file kadd_config_loader.c
 * @brief 配置文件加载器实现
 */

#define _POSIX_C_SOURCE 200809L
#include "kadd_config_loader.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int kadd_config_load(const char *config_path, kadd_config_t *config) {
    if (!config) return -1;

    // 使用默认路径或指定路径
    const char *path = config_path ? config_path : "config.json";

    // 读取文件
    FILE *file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "无法打开配置文件: %s\n", path);
        return -1;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *content = (char *)malloc(file_size + 1);
    if (!content) {
        fclose(file);
        return -1;
    }

    fread(content, 1, file_size, file);
    content[file_size] = '\0';
    fclose(file);

    // 解析JSON
    cJSON *root = cJSON_Parse(content);
    free(content);

    if (!root) {
        fprintf(stderr, "配置文件JSON解析失败\n");
        return -1;
    }

    // 初始化默认值
    memset(config, 0, sizeof(kadd_config_t));

    // 解析DFA引擎配置
    cJSON *dfa = cJSON_GetObjectItem(root, "kadd_dfa_engine");
    if (dfa) {
        cJSON *item;

        item = cJSON_GetObjectItem(dfa, "transition_table_path");
        if (item && item->valuestring) {
            strncpy(config->dfa.table_paths.transition_table_path,
                    item->valuestring, 511);
        }

        item = cJSON_GetObjectItem(dfa, "output_table_path");
        if (item && item->valuestring) {
            strncpy(config->dfa.table_paths.output_table_path,
                    item->valuestring, 511);
        }

        item = cJSON_GetObjectItem(dfa, "eod_table_path");
        if (item && item->valuestring) {
            strncpy(config->dfa.table_paths.eod_table_path,
                    item->valuestring, 511);
        }

        item = cJSON_GetObjectItem(dfa, "max_input_length");
        if (item && cJSON_IsNumber(item)) {
            config->dfa.max_input_length = item->valueint;
        } else {
            config->dfa.max_input_length = 8192;  // 默认值
        }

        item = cJSON_GetObjectItem(dfa, "max_output_dim");
        if (item && cJSON_IsNumber(item)) {
            config->dfa.max_output_dim = item->valueint;
        } else {
            config->dfa.max_output_dim = 8192;  // 默认值
        }
    }

    // 解析ML引擎配置
    cJSON *ml = cJSON_GetObjectItem(root, "kadd_ml_engine");
    if (ml) {
        cJSON *item;

        item = cJSON_GetObjectItem(ml, "model_path");
        if (item && item->valuestring) {
            strncpy(config->ml.model_path, item->valuestring, 511);
        }

        item = cJSON_GetObjectItem(ml, "feature_dim");
        if (item && cJSON_IsNumber(item)) {
            config->ml.feature_dim = item->valueint;
        } else {
            config->ml.feature_dim = 28;  // 默认值
        }

        item = cJSON_GetObjectItem(ml, "window_size");
        if (item && cJSON_IsNumber(item)) {
            config->ml.window_size = item->valueint;
        } else {
            config->ml.window_size = 5;  // 默认值
        }
    }

    // 解析SQLi引擎配置（如果有）
    cJSON *sqli = cJSON_GetObjectItem(root, "kadd_sqli_engine");
    if (sqli) {
        cJSON *item;

        item = cJSON_GetObjectItem(sqli, "transition_table_path");
        if (item && item->valuestring) {
            strncpy(config->sqli.transition_table_path, item->valuestring, 511);
        }

        item = cJSON_GetObjectItem(sqli, "output_table_path");
        if (item && item->valuestring) {
            strncpy(config->sqli.output_table_path, item->valuestring, 511);
        }

        item = cJSON_GetObjectItem(sqli, "eod_table_path");
        if (item && item->valuestring) {
            strncpy(config->sqli.eod_table_path, item->valuestring, 511);
        }

        item = cJSON_GetObjectItem(sqli, "ml_model_path");
        if (item && item->valuestring) {
            strncpy(config->sqli.ml_model_path, item->valuestring, 511);
        }

        item = cJSON_GetObjectItem(sqli, "feature_dim");
        if (item && cJSON_IsNumber(item)) {
            config->sqli.feature_dim = item->valueint;
        } else {
            config->sqli.feature_dim = 28;  // 默认值
        }

        item = cJSON_GetObjectItem(sqli, "window_size");
        if (item && cJSON_IsNumber(item)) {
            config->sqli.window_size = item->valueint;
        } else {
            config->sqli.window_size = 5;  // 默认值
        }

        item = cJSON_GetObjectItem(sqli, "max_input_length");
        if (item && cJSON_IsNumber(item)) {
            config->sqli.max_input_length = item->valueint;
        } else {
            config->sqli.max_input_length = 8192;  // 默认值
        }

        item = cJSON_GetObjectItem(sqli, "max_output_dim");
        if (item && cJSON_IsNumber(item)) {
            config->sqli.max_output_dim = item->valueint;
        } else {
            config->sqli.max_output_dim = 8192;  // 默认值
        }
    }

    // Feature引擎配置（复用ML配置）
    cJSON *feature = cJSON_GetObjectItem(root, "kadd_feature_engine");
    if (feature) {
        cJSON *item;

        item = cJSON_GetObjectItem(feature, "ml_model_path");
        if (item && item->valuestring) {
            strncpy(config->feature.ml_model_path, item->valuestring, 511);
        }

        item = cJSON_GetObjectItem(feature, "feature_dim");
        if (item && cJSON_IsNumber(item)) {
            config->feature.feature_dim = item->valueint;
        } else {
            config->feature.feature_dim = 28;  // 默认值
        }

        item = cJSON_GetObjectItem(feature, "window_size");
        if (item && cJSON_IsNumber(item)) {
            config->feature.window_size = item->valueint;
        } else {
            config->feature.window_size = 5;  // 默认值
        }
    } else {
        // 如果没有单独的feature配置，从ml配置复制
        cJSON *ml_item = cJSON_GetObjectItem(root, "kadd_ml_engine");
        if (ml_item) {
            cJSON *item;

            item = cJSON_GetObjectItem(ml_item, "model_path");
            if (item && item->valuestring) {
                strncpy(config->feature.ml_model_path, item->valuestring, 511);
            }

            item = cJSON_GetObjectItem(ml_item, "feature_dim");
            if (item && cJSON_IsNumber(item)) {
                config->feature.feature_dim = item->valueint;
            }

            item = cJSON_GetObjectItem(ml_item, "window_size");
            if (item && cJSON_IsNumber(item)) {
                config->feature.window_size = item->valueint;
            }
        }
    }

    cJSON_Delete(root);
    return 0;
}

void kadd_config_free(kadd_config_t *config) {
    // 目前没有动态分配的内存，不需要释放
    (void)config;
}
