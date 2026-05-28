/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *
 * @file kadd_sqli_engine.c
 * @brief SQL 注入检测引擎实现
 *
 * 整合 DFA 引擎和 ML 引擎，提供完整的 SQL 注入检测功能
 * 流程: 输入 -> DFA 分词 -> 特征提取 -> ML 推理 -> 结果
 */

#include "kadd_sqli_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kadd_dfa_engine.h"
#include "kadd_feature_engine.h"
#include "kadd_ml_engine.h"
#include "cJSON.h"

/* ========== 默认配置常量 ========== */
#define KADD_DEFAULT_FEATURE_DIM     28
#define KADD_DEFAULT_WINDOW_SIZE     5
#define KADD_DEFAULT_MAX_OUTPUT_DIM  8192
#define KADD_DEFAULT_MAX_INPUT_LEN   8192
#define KADD_MIN_BATCH_SIZE          64
#define KADD_MAX_BATCH_SIZE          1024
#define KADD_ALIGN_BYTES             16

/* ========== 内部结构体 ========== */

/* 引擎初始化参数 */
typedef struct {
    int feature_dim;
    int window_size;
    int max_output_dim;
    int max_input_length;
} kadd_engine_init_params_t;

/* 配置提取结果 */
typedef struct {
    char *model_path;
    kadd_dfa_table_paths_t table_paths;
    int feature_dim;
    int window_size;
    int max_output_dim;
    int max_input_length;
} kadd_config_result_t;

/* 批处理缓冲区 */
typedef struct {
    int **dfa_outputs;
    int *dfa_dims;
    int *features;
    float *float_features;
} kadd_batch_buffers_t;

/* 批次处理参数 - DFA 和 Feature 提取阶段 */
typedef struct {
    kadd_sqli_engine_t *engine;
    const char **inputs;
    int batch_start;
    int current_batch;
    kadd_batch_buffers_t *buffers;
} kadd_batch_dfa_params_t;

/* 批次处理参数 - ML 推理阶段 */
typedef struct {
    kadd_sqli_engine_t *engine;
    int current_batch;
    int batch_start;
    int *dfa_dims;
    float *probabilities;
} kadd_batch_ml_params_t;

struct kadd_sqli_engine_t {
    kadd_dfa_engine_t *dfa_engine;
    kadd_model_handle_t *ml_model;
    int feature_dim;
    int window_size;
    int max_output_dim;
    int max_input_length;

    /* 内存池 - 避免每次检测都分配内存 */
    int *token_buffer;           /* DFA 分词输出缓冲区 */
    int *feature_buffer;         /* 特征向量缓冲区 */
    float *float_feature_buffer; /* 浮点特征缓冲区 */
};

/* ========== 内部辅助函数 ========== */

// 分配引擎内存池
static int allocate_engine_memory_pool(kadd_sqli_engine_t *e)
{
    /* 分配内存池 */
    e->token_buffer = (int *)malloc(e->max_output_dim * sizeof(int));
    e->feature_buffer = (int *)calloc(e->feature_dim, sizeof(int));
    if (posix_memalign((void **)&e->float_feature_buffer, KADD_ALIGN_BYTES, e->feature_dim * sizeof(float)) != 0) {
        e->float_feature_buffer = NULL;
    }

    if (!e->token_buffer || !e->feature_buffer || !e->float_feature_buffer) {
        /* 内存分配失败，清理已分配的资源 */
        if (e->token_buffer) {
            free(e->token_buffer);
            e->token_buffer = NULL;
        }
        if (e->feature_buffer) {
            free(e->feature_buffer);
            e->feature_buffer = NULL;
        }
        if (e->float_feature_buffer) {
            free(e->float_feature_buffer);
            e->float_feature_buffer = NULL;
        }
        return KADD_ERROR_OUT_OF_MEMORY;
    }
    return KADD_SUCCESS;
}

// 清理 SQLi 批处理临时缓冲区
static void cleanup_sqli_batch_buffers(int **dfa_outputs, int *dfa_dims,
                                       int *features, float *float_features,
                                       int count)
{
    if (dfa_outputs) {
        for (int i = 0; i < count; i++) {
            if (dfa_outputs[i]) {
                free(dfa_outputs[i]);
            }
        }
        free(dfa_outputs);
    }
    if (dfa_dims) {
        free(dfa_dims);
    }
    if (features) {
        free(features);
    }
    if (float_features) {
        free(float_features);
    }
}

/* ========== 公共接口 ========== */

static int create_engine_internal(kadd_sqli_engine_t **engine,
                                   const kadd_dfa_table_paths_t *table_paths,
                                   const char *model_path, int feature_dim,
                                   int window_size, int max_output_dim, int max_input_length)
{
    if (!engine || !table_paths || !model_path) {
        return KADD_ERROR_INVALID_INPUT;
    }

    kadd_sqli_engine_t *e = (kadd_sqli_engine_t *)calloc(1, sizeof(kadd_sqli_engine_t));
    if (!e) {
        return KADD_ERROR_OUT_OF_MEMORY;
    }

    e->feature_dim = feature_dim;
    e->window_size = window_size;
    e->max_output_dim = max_output_dim;
    e->max_input_length = max_input_length;

    /* 分配内存池 */
    int ret = allocate_engine_memory_pool(e);
    if (ret != KADD_SUCCESS) {
        free(e);
        return ret;
    }

    if (kadd_dfa_engine_create(&e->dfa_engine, table_paths, e->max_input_length, e->max_output_dim) != 0) {
        if (e->token_buffer) {
            free(e->token_buffer);
        }
        if (e->feature_buffer) {
            free(e->feature_buffer);
        }
        if (e->float_feature_buffer) {
            free(e->float_feature_buffer);
        }
        free(e);
        return KADD_SQLI_ERROR_DFA;
    }

    if (kadd_model_load(&e->ml_model, model_path, e->feature_dim) != 0) {
        kadd_dfa_engine_free(e->dfa_engine);
        if (e->token_buffer) {
            free(e->token_buffer);
        }
        if (e->feature_buffer) {
            free(e->feature_buffer);
        }
        if (e->float_feature_buffer) {
            free(e->float_feature_buffer);
        }
        free(e);
        return KADD_SQLI_ERROR_ML;
    }

    *engine = e;
    return KADD_SUCCESS;
}

// 从配置文件读取 JSON 内容
static int read_config_file_content(const char *config_file, char **out_content, long *out_size)
{
    if (!config_file || !out_content || !out_size) {
        return KADD_ERROR_INVALID_INPUT;
    }

    FILE *fp = fopen(config_file, "r");
    if (!fp) {
        return KADD_ERROR_CONFIG;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return KADD_ERROR_CONFIG;
    }
    long file_size = ftell(fp);
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return KADD_ERROR_CONFIG;
    }

    if (file_size <= 0) {
        fclose(fp);
        return KADD_ERROR_CONFIG;
    }

    char *json_content = (char *)malloc(file_size + 1);
    if (!json_content) {
        fclose(fp);
        return KADD_ERROR_OUT_OF_MEMORY;
    }

    size_t read_size = fread(json_content, 1, file_size, fp);
    if (read_size != (size_t)file_size) {
        free(json_content);
        fclose(fp);
        return KADD_ERROR_CONFIG;
    }
    if (ferror(fp)) {
        free(json_content);
        fclose(fp);
        return KADD_ERROR_CONFIG;
    }
    json_content[read_size] = '\0';
    if (fclose(fp) != 0) {
        free(json_content);
        return KADD_ERROR_CONFIG;
    }

    *out_content = json_content;
    *out_size = file_size;
    return KADD_SUCCESS;
}

// 从配置 JSON 提取配置参数
static int extract_config_params(cJSON *root, kadd_config_result_t *result)
{
    if (!root || !result) {
        return KADD_ERROR_INVALID_INPUT;
    }

    /* DFA 配置 */
    cJSON *dfa_obj = cJSON_GetObjectItemCaseSensitive(root, "kadd_dfa_engine");
    if (!dfa_obj) {
        return KADD_ERROR_CONFIG;
    }

    cJSON *dfa_transition = cJSON_GetObjectItemCaseSensitive(dfa_obj, "transition_table_path");
    cJSON *dfa_output = cJSON_GetObjectItemCaseSensitive(dfa_obj, "output_table_path");
    cJSON *dfa_eod = cJSON_GetObjectItemCaseSensitive(dfa_obj, "eod_table_path");
    if (!cJSON_IsString(dfa_transition) || !cJSON_IsString(dfa_output) || !cJSON_IsString(dfa_eod)) {
        return KADD_ERROR_CONFIG;
    }

    /* ML 配置 */
    cJSON *ml_obj = cJSON_GetObjectItemCaseSensitive(root, "kadd_ml_engine");
    if (!ml_obj) {
        return KADD_ERROR_CONFIG;
    }

    cJSON *ml_model = cJSON_GetObjectItemCaseSensitive(ml_obj, "model_path");
    if (!cJSON_IsString(ml_model)) {
        return KADD_ERROR_CONFIG;
    }

    cJSON *ml_dim = cJSON_GetObjectItemCaseSensitive(ml_obj, "feature_dim");
    result->feature_dim = cJSON_IsNumber(ml_dim) ? (int)ml_dim->valuedouble : KADD_DEFAULT_FEATURE_DIM;

    cJSON *ml_window = cJSON_GetObjectItemCaseSensitive(ml_obj, "window_size");
    result->window_size = cJSON_IsNumber(ml_window) ? (int)ml_window->valuedouble : KADD_DEFAULT_WINDOW_SIZE;

    cJSON *dfa_max_out = cJSON_GetObjectItemCaseSensitive(dfa_obj, "max_output_dim");
    result->max_output_dim = cJSON_IsNumber(dfa_max_out) ? (int)dfa_max_out->valuedouble : KADD_DEFAULT_MAX_OUTPUT_DIM;

    cJSON *dfa_max_in = cJSON_GetObjectItemCaseSensitive(dfa_obj, "max_input_length");
    result->max_input_length = cJSON_IsNumber(dfa_max_in) ? (int)dfa_max_in->valuedouble : KADD_DEFAULT_MAX_INPUT_LEN;

    result->model_path = ml_model->valuestring;
    result->table_paths.transition_table_path = dfa_transition->valuestring;
    result->table_paths.output_table_path = dfa_output->valuestring;
    result->table_paths.eod_table_path = dfa_eod->valuestring;

    return KADD_SUCCESS;
}

// 从配置文件创建 SQL 注入检测引擎
int kadd_sqli_engine_create_from_file(kadd_sqli_engine_t **engine, const char *config_file)
{
    if (!engine || !config_file) {
        return KADD_ERROR_INVALID_INPUT;
    }

    char *json_content = NULL;
    long file_size = 0;
    int ret = read_config_file_content(config_file, &json_content, &file_size);
    if (ret != KADD_SUCCESS) {
        return ret;
    }

    cJSON *root = cJSON_Parse(json_content);
    free(json_content);

    if (!root) {
        return KADD_ERROR_CONFIG;
    }

    kadd_config_result_t config_result = {0};

    ret = extract_config_params(root, &config_result);
    if (ret != KADD_SUCCESS) {
        cJSON_Delete(root);
        return ret;
    }

    int result = create_engine_internal(engine, &config_result.table_paths,
                                        config_result.model_path, config_result.feature_dim,
                                        config_result.window_size, config_result.max_output_dim,
                                        config_result.max_input_length);
    cJSON_Delete(root);

    return result;
}

// 检测单个输入
int kadd_sqli_detect(kadd_sqli_engine_t *engine, const char *input, float *probability)
{
    if (!engine || !input || !probability) {
        return KADD_ERROR_INVALID_INPUT;
    }

    *probability = 0.0f;

    /* 步骤 1: DFA 分词 - 使用内存池 */
    int token_count = 0;
    if (kadd_dfa_engine_tokenize(engine->dfa_engine, input, strlen(input),
                                 engine->token_buffer, &token_count) != 0) {
        return KADD_SQLI_ERROR_DFA;
    }

    /* 步骤 2: 特征提取 - 使用内存池 */
    if (kadd_seq_to_feature(engine->token_buffer, token_count, engine->window_size,
                             engine->feature_buffer, engine->feature_dim) != 0) {
        return KADD_SQLI_ERROR_FEATURE;
    }

    /* 步骤 3: ML 推理 - 使用内存池 */
    for (int i = 0; i < engine->feature_dim; i++) {
        engine->float_feature_buffer[i] = (float)engine->feature_buffer[i];
    }

    float prob = 0.0f;
    int ret = kadd_model_predict(engine->ml_model, engine->float_feature_buffer, &prob);
    if (ret != 0) {
        return KADD_SQLI_ERROR_ML;
    }

    *probability = prob;
    return KADD_SUCCESS;
}

// 分配批处理缓冲区
static int allocate_batch_buffers(kadd_sqli_engine_t *engine, int batch_size,
                                   kadd_batch_buffers_t *buffers)
{
    if (!engine || !buffers) {
        return KADD_ERROR_INVALID_INPUT;
    }

    int **dfa_outputs = (int **)calloc(batch_size, sizeof(int *));
    int *dfa_dims = (int *)calloc(batch_size, sizeof(int));
    int *features = (int *)calloc(batch_size * engine->feature_dim, sizeof(int));
    float *float_features = (float *)malloc(batch_size * engine->feature_dim * sizeof(float));

    if (!dfa_outputs || !dfa_dims || !features || !float_features) {
        cleanup_sqli_batch_buffers(dfa_outputs, dfa_dims, features, float_features, 0);
        return KADD_ERROR_OUT_OF_MEMORY;
    }

    buffers->dfa_outputs = dfa_outputs;
    buffers->dfa_dims = dfa_dims;
    buffers->features = features;
    buffers->float_features = float_features;
    return KADD_SUCCESS;
}

// 处理单批次 DFA 和 Feature 提取
static int process_single_batch(kadd_batch_dfa_params_t *params)
{
    // DFA 批量 tokenize
    kadd_dfa_batch_t dfa_batch = {
        .inputs = &params->inputs[params->batch_start],
        .lengths = NULL,
        .count = params->current_batch,
        .outputs = params->buffers->dfa_outputs,
        .dims = params->buffers->dfa_dims,
        .batch_size = params->current_batch
    };

    int dfa_result = kadd_dfa_engine_tokenize_batch(params->engine->dfa_engine, &dfa_batch);
    if (dfa_result != KADD_SUCCESS) {
        return KADD_SQLI_ERROR_DFA;
    }

    // Feature 批量提取特征
    kadd_feature_batch_t feat_batch = {
        .seqs = (const int **)params->buffers->dfa_outputs,
        .seq_lens = params->buffers->dfa_dims,
        .count = params->current_batch,
        .features = params->buffers->features,
        .feature_dim = params->engine->feature_dim,
        .window_size = params->engine->window_size,
        .batch_size = params->current_batch
    };

    int feat_result = kadd_seq_to_feature_batch(&feat_batch);
    if (feat_result != KADD_SUCCESS) {
        return KADD_SQLI_ERROR_FEATURE;
    }

    return KADD_SUCCESS;
}

// 处理单批次 ML 推理和结果更新
static int process_ml_batch_and_update(kadd_batch_ml_params_t *params,
                                        kadd_batch_buffers_t *buffers)
{
    // 转换为浮点特征
    int total_features = params->current_batch * params->engine->feature_dim;
    for (int i = 0; i < total_features; i++) {
        buffers->float_features[i] = (float)buffers->features[i];
    }

    float batch_probs[KADD_MAX_BATCH_SIZE];
    int ml_result = kadd_model_predict_batch(params->engine->ml_model, buffers->float_features,
                                                params->current_batch, batch_probs, params->current_batch);
    if (ml_result != KADD_SUCCESS) {
        return KADD_SQLI_ERROR_ML;
    }

    // 更新概率
    for (int i = 0; i < params->current_batch; i++) {
        if (params->dfa_dims[i] > 0) {
            params->probabilities[params->batch_start + i] = batch_probs[i];
        }
    }

    return KADD_SUCCESS;
}

// 初始化批量检测参数
static int init_batch_detect_params(int batch_size, int *out_actual_batch_size,
                                     float *probabilities, int count)
{
    if (!out_actual_batch_size || !probabilities) {
        return KADD_ERROR_INVALID_INPUT;
    }

    *out_actual_batch_size = batch_size;
    if (*out_actual_batch_size <= 0) {
        *out_actual_batch_size = KADD_MIN_BATCH_SIZE;
    }
    if (*out_actual_batch_size > KADD_MAX_BATCH_SIZE) {
        *out_actual_batch_size = KADD_MAX_BATCH_SIZE;
    }

    // 初始化所有概率为0（表示失败）
    for (int i = 0; i < count; i++) {
        probabilities[i] = 0.0f;
    }

    return KADD_SUCCESS;
}

// 执行单批次检测循环
static int process_batch_loop(kadd_sqli_engine_t *engine, const char **inputs,
                              int count, int actual_batch_size, kadd_batch_buffers_t *buffers,
                              float *probabilities)
{
    for (int batch_start = 0; batch_start < count; batch_start += actual_batch_size) {
        int current_batch = (batch_start + actual_batch_size < count) ? actual_batch_size : (count - batch_start);

        kadd_batch_dfa_params_t dfa_params = {
            .engine = engine,
            .inputs = inputs,
            .batch_start = batch_start,
            .current_batch = current_batch,
            .buffers = buffers
        };

        int ret = process_single_batch(&dfa_params);
        if (ret != KADD_SUCCESS) {
            cleanup_sqli_batch_buffers(buffers->dfa_outputs, buffers->dfa_dims,
                                       buffers->features, buffers->float_features, current_batch);
            return ret;
        }

        kadd_batch_ml_params_t ml_params = {
            .engine = engine,
            .current_batch = current_batch,
            .batch_start = batch_start,
            .dfa_dims = buffers->dfa_dims,
            .probabilities = probabilities
        };

        ret = process_ml_batch_and_update(&ml_params, buffers);
        if (ret != KADD_SUCCESS) {
            cleanup_sqli_batch_buffers(buffers->dfa_outputs, buffers->dfa_dims,
                                       buffers->features, buffers->float_features, current_batch);
            return ret;
        }

        // 清理 DFA 输出
        for (int i = 0; i < current_batch; i++) {
            if (buffers->dfa_outputs[i]) {
                free(buffers->dfa_outputs[i]);
                buffers->dfa_outputs[i] = NULL;
            }
        }
    }
    return KADD_SUCCESS;
}

// 批量检测
int kadd_sqli_detect_batch(kadd_sqli_engine_t *engine, const char **inputs, int count,
                            float *probabilities, int batch_size)
{
    if (!engine || !inputs || !probabilities || count <= 0) {
        return KADD_ERROR_INVALID_INPUT;
    }

    int actual_batch_size = 0;
    int ret = init_batch_detect_params(batch_size, &actual_batch_size, probabilities, count);
    if (ret != KADD_SUCCESS) {
        return ret;
    }

    // 分配批处理缓冲区
    kadd_batch_buffers_t buffers = {0};

    ret = allocate_batch_buffers(engine, actual_batch_size, &buffers);
    if (ret != KADD_SUCCESS) {
        return ret;
    }

    ret = process_batch_loop(engine, inputs, count, actual_batch_size, &buffers, probabilities);
    if (ret != KADD_SUCCESS) {
        return ret;
    }

    // 释放缓冲区
    free(buffers.dfa_outputs);
    buffers.dfa_outputs = NULL;
    free(buffers.dfa_dims);
    buffers.dfa_dims = NULL;
    free(buffers.features);
    buffers.features = NULL;
    free(buffers.float_features);
    buffers.float_features = NULL;

    return KADD_SUCCESS;
}

// 释放引擎
void kadd_sqli_engine_free(kadd_sqli_engine_t *engine)
{
    if (!engine) {
        return;
    }

    if (engine->ml_model) {
        kadd_model_free(engine->ml_model);
        engine->ml_model = NULL;
    }
    if (engine->dfa_engine) {
        kadd_dfa_engine_free(engine->dfa_engine);
        engine->dfa_engine = NULL;
    }

    /* 释放内存池 */
    if (engine->token_buffer) {
        free(engine->token_buffer);
        engine->token_buffer = NULL;
    }
    if (engine->feature_buffer) {
        free(engine->feature_buffer);
        engine->feature_buffer = NULL;
    }
    if (engine->float_feature_buffer) {
        free(engine->float_feature_buffer);
        engine->float_feature_buffer = NULL;
    }

    free(engine);
}
