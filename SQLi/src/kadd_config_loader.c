/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * @file kadd_config_loader.c
 * @brief Configuration loader implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"
#include "kadd_config_loader.h"
#include "kadd_errors.h"

typedef struct {
    const char *name;
    size_t offset;
    int required;
} field_map_t;

#define FIELD_OFFSET(f) offsetof(kadd_config_t, f)

static const field_map_t g_field_map[] = {
    {"transition_table", FIELD_OFFSET(transition_table), 1},
    {"output_table",     FIELD_OFFSET(output_table),     1},
    {"eod_table",        FIELD_OFFSET(eod_table),        1},
    {"model_rf",         FIELD_OFFSET(model_rf),         0},
    {"model_rf_onnx",    FIELD_OFFSET(model_rf_onnx),    0},
    {"model_cnn",        FIELD_OFFSET(model_cnn),        0},
    {"model_cnn_onnx",   FIELD_OFFSET(model_cnn_onnx),   0},
};
#define FIELD_COUNT (sizeof(g_field_map) / sizeof(g_field_map[0]))

static void safe_copy(char *dest, const char *src, size_t max_len)
{
    size_t len = strlen(src);
    if (len >= max_len) {
        len = max_len - 1;
    }
    for (size_t i = 0; i < len; i++) {
        dest[i] = src[i];
    }
    dest[len] = '\0';
}

static int read_file(const char *path, char **out_content)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "[kadd_config] ERROR: Failed to open file: %s\n", path);
        return KADD_ERROR_FILE_OPERATION_FAILED;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "[kadd_config] ERROR: fseek SEEK_END failed\n");
        fclose(file);
        return KADD_ERROR_FILE_OPERATION_FAILED;
    }

    long file_size = ftell(file);
    if (file_size <= 0 || file_size > KADD_CONFIG_MAX_FILE_SIZE) {
        fclose(file);
        fprintf(stderr, "[kadd_config] ERROR: Invalid file size: %ld\n", file_size);
        return KADD_ERROR_CONFIG;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "[kadd_config] ERROR: fseek SEEK_SET failed\n");
        fclose(file);
        return KADD_ERROR_FILE_OPERATION_FAILED;
    }

    char *content = (char *)calloc(file_size + 1, sizeof(char));
    if (!content) {
        fprintf(stderr, "[kadd_config] ERROR: Failed to allocate memory (%ld bytes)\n", file_size);
        fclose(file);
        return KADD_ERROR_OUT_OF_MEMORY;
    }

    size_t bytes_read = fread(content, 1, file_size, file);
    fclose(file);

    if (bytes_read != (size_t)file_size) {
        fprintf(stderr, "[kadd_config] ERROR: Read incomplete: expected %ld, got %zu\n",
                file_size, bytes_read);
        free(content);
        return KADD_ERROR_FILE_OPERATION_FAILED;
    }

    content[file_size] = '\0';
    *out_content = content;
    return KADD_SUCCESS;
}

static kadd_engine_mode_t parse_engine_mode(const char *mode_str)
{
    if (!mode_str || strlen(mode_str) == 0) {
        return KADD_ENGINE_MODE_ML;
    }
    
    if (strcmp(mode_str, "dl") == 0) {
        return KADD_ENGINE_MODE_DL;
    }
    return KADD_ENGINE_MODE_ML;
}

int kadd_config_load(const char *config_path, kadd_config_t *config)
{
    if (!config || !config_path) {
        fprintf(stderr, "[kadd_config] ERROR: Invalid parameters\n");
        return KADD_ERROR_NULL_PTR;
    }

    *config = (kadd_config_t){0};
    config->engine_mode = KADD_ENGINE_MODE_ML;

    char *content;
    int ret = read_file(config_path, &content);
    if (ret != KADD_SUCCESS) {
        return ret;
    }

    cJSON *root = cJSON_Parse(content);
    free(content);

    if (!root) {
        fprintf(stderr, "[kadd_config] ERROR: JSON parse failed: %s\n", config_path);
        return KADD_ERROR_CONFIG;
    }

    cJSON *mode_item = cJSON_GetObjectItem(root, "engine_mode");
    if (mode_item && mode_item->valuestring) {
        config->engine_mode = parse_engine_mode(mode_item->valuestring);
    }
    fprintf(stdout, "[kadd_config] engine_mode = %d (%s)\n", 
            config->engine_mode,
            mode_item && mode_item->valuestring ? mode_item->valuestring : "ml (default)");

    for (size_t i = 0; i < FIELD_COUNT; i++) {
        const field_map_t *m = &g_field_map[i];
        cJSON *item = cJSON_GetObjectItem(root, m->name);

        if (item && item->valuestring) {
            char *dest = (char *)((uintptr_t)config + m->offset);
            safe_copy(dest, item->valuestring, KADD_CONFIG_PATH_MAX_LEN);
        } else if (m->required) {
            fprintf(stderr, "[kadd_config] ERROR: Missing required field '%s'\n", m->name);
            cJSON_Delete(root);
            return KADD_ERROR_CONFIG;
        }
    }

    switch (config->engine_mode) {
    case KADD_ENGINE_MODE_ML:
        if (strlen(config->model_rf) == 0 && strlen(config->model_rf_onnx) == 0) {
            fprintf(stderr, "[kadd_config] ERROR: ML mode requires model_rf or model_rf_onnx\n");
            cJSON_Delete(root);
            return KADD_ERROR_CONFIG;
        }
        break;
    case KADD_ENGINE_MODE_DL:
        if (strlen(config->model_cnn) == 0 && strlen(config->model_cnn_onnx) == 0) {
            fprintf(stderr, "[kadd_config] ERROR: DL mode requires model_cnn or model_cnn_onnx\n");
            cJSON_Delete(root);
            return KADD_ERROR_CONFIG;
        }
        break;
    }

    cJSON_Delete(root);
    return KADD_SUCCESS;
}

void kadd_config_free(kadd_config_t *config)
{
    (void)config;
}
