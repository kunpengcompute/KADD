/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *
 * @file kadd_sqli_engine.c
 * @brief SQL 注入检测引擎实现
 *
 * 整合 DFA 引擎和 ML 引擎，提供完整的 SQL 注入检测功能
 * 流程: 输入 -> DFA 分词 -> 特征提取 -> ML 推理 -> 结果
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#include "kadd_sqli_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kadd_dfa_engine.h"
#include "kadd_feature_engine.h"
#include "kadd_ml_engine.h"
#include "kadd_config_loader.h"
#include "kadd_dl_engine.h"
#include "kadd_sve.h"

/* ========== 常量 ========== */
#define KADD_ALIGN_BYTES             16
#define KADD_DL_SEQUENCE_LENGTH      300

/* ========== 内部结构体 ========== */

/* 引擎初始化参数 */
typedef struct {
    int feature_dim;
    int window_size;
    int max_tokens;
    int max_input_chars;
} kadd_engine_init_params_t;

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
    kadd_engine_mode_t engine_mode;
    kadd_model_handle_t *ml_model;
    int feature_dim;
    int window_size;
    int max_tokens;
    int max_input_chars;

    /* 内存池 - 避免每次检测都分配内存 */
    int *token_buffer;           /* DFA 分词输出缓冲区 */
    kadd_dl_model_t *dl_model;
    float *dl_input_buffer;
    int *feature_buffer;         /* 特征向量缓冲区 */
    float *float_feature_buffer; /* 浮点特征缓冲区 */
};

// ========== 辅助函数：特征转换 ==========

// 统一的特征转换函数：将整型特征转换为浮点特征
static void convert_int_to_float_features(const int *int_features, float *float_features, int feature_dim)
{
    for (int i = 0; i < feature_dim; i++) {
        float_features[i] = (float)int_features[i];
    }
}

// ========== 辅助函数：缓冲区管理 ==========

// 释放单个缓冲区（如果已设置）
static void free_buffer_if_set(void **buffer_ptr)
{
    if (*buffer_ptr) {
        free(*buffer_ptr);
        *buffer_ptr = NULL;
    }
}

// 释放引擎内存池
static void release_engine_buffers(kadd_sqli_engine_t *e)
{
    free_buffer_if_set((void **)&e->token_buffer);
    free_buffer_if_set((void **)&e->feature_buffer);
    free_buffer_if_set((void **)&e->float_feature_buffer);
    free_buffer_if_set((void **)&e->dl_input_buffer);
}

// ========== 辅助函数：批处理缓冲区管理 ==========

// 清理 SQLi 批处理临时缓冲区
static void cleanup_sqli_batch_buffers(int **dfa_outputs, int *dfa_dims,
                                       int *features, float *float_features,
                                       int count)
{
    if (dfa_outputs) {
        for (int i = 0; i < count; i++) {
            if (dfa_outputs[i]) {
                free(dfa_outputs[i]);
                dfa_outputs[i] = NULL;
            }
        }
        free(dfa_outputs);
        dfa_outputs = NULL;
    }
    if (dfa_dims) {
        free(dfa_dims);
        dfa_dims = NULL;
    }
    if (features) {
        free(features);
        features = NULL;
    }
    if (float_features) {
        free(float_features);
        float_features = NULL;
    }
}

// 分配引擎内存池
static int allocate_engine_memory_pool(kadd_sqli_engine_t *e)
{
    e->token_buffer = (int *)calloc(e->max_tokens, sizeof(int));
    if (!e->token_buffer) { release_engine_buffers(e); return KADD_ERROR_OUT_OF_MEMORY; }

    if (e->engine_mode == KADD_ENGINE_MODE_ML) {
        e->feature_buffer = (int *)calloc(e->feature_dim, sizeof(int));
        if (posix_memalign((void **)&e->float_feature_buffer, KADD_ALIGN_BYTES, e->feature_dim * sizeof(float)) != 0)
            e->float_feature_buffer = NULL;
        if (!e->feature_buffer || !e->float_feature_buffer) { release_engine_buffers(e); return KADD_ERROR_OUT_OF_MEMORY; }
    }

    if (e->engine_mode == KADD_ENGINE_MODE_DL) {
        if (posix_memalign((void **)&e->dl_input_buffer, KADD_ALIGN_BYTES, KADD_DL_SEQUENCE_LENGTH * sizeof(float)) != 0)
            e->dl_input_buffer = NULL;
        if (!e->dl_input_buffer) { release_engine_buffers(e); return KADD_ERROR_OUT_OF_MEMORY; }
    }
    return KADD_SUCCESS;
}

static int create_engine_internal(kadd_sqli_engine_t **engine,
                                   const kadd_dfa_table_paths_t *table_paths,
                                   const kadd_config_t *config)
{
    if (!engine || !table_paths || !config) {
        fprintf(stderr, "[kadd_sqli] ERROR: Invalid parameters\n");
        return KADD_ERROR_INVALID_INPUT;
    }

    kadd_sqli_engine_t *e = (kadd_sqli_engine_t *)calloc(1, sizeof(kadd_sqli_engine_t));
    if (!e) {
        fprintf(stderr, "[kadd_sqli] ERROR: Failed to allocate sqli engine\n");
        return KADD_ERROR_OUT_OF_MEMORY;
    }

    e->engine_mode = config->engine_mode;
    e->max_tokens = KADD_DFA_MAX_TOKENS;
    e->max_input_chars = KADD_DFA_MAX_INPUT_CHARS;
    e->feature_dim = KADD_FEATURE_DIM;
    e->window_size = KADD_FEATURE_WINDOW_SIZE;

    int ret = allocate_engine_memory_pool(e);
    if (ret != KADD_SUCCESS) {
        free(e);
        return ret;
    }

    if (kadd_dfa_engine_create(&e->dfa_engine, table_paths, e->max_input_chars, e->max_tokens) != 0) {
        fprintf(stderr, "[kadd_sqli] ERROR: Failed to create DFA engine\n");
        release_engine_buffers(e);
        free(e);
        return KADD_SQLI_ERROR_DFA;
    }

    /* Load ML model */
    if (e->engine_mode == KADD_ENGINE_MODE_ML) {
#if KADD_HAVE_SVE
        const char *model_path = (strlen(config->model_rf) > 0) ? config->model_rf : config->model_rf_onnx;
        if (kadd_model_load(&e->ml_model, model_path, e->feature_dim) != 0) {
            /* Fallback to ONNX if native failed and they are different */
            if (strlen(config->model_rf) > 0 && strlen(config->model_rf_onnx) > 0 &&
                strcmp(config->model_rf, config->model_rf_onnx) != 0) {
                fprintf(stdout, "[kadd_sqli] WARN: native ML model load failed, trying ONNX: %s\n", config->model_rf_onnx);
                if (kadd_model_load(&e->ml_model, config->model_rf_onnx, e->feature_dim) != 0) {
                    fprintf(stderr, "[kadd_sqli] ERROR: Failed to load ML model (both native and ONNX)\n");
                    kadd_dfa_engine_free(e->dfa_engine);
                    release_engine_buffers(e);
                    free(e);
                    return KADD_SQLI_ERROR_ML;
                }
            } else {
                fprintf(stderr, "[kadd_sqli] ERROR: Failed to load ML model: %s\n", model_path);
                kadd_dfa_engine_free(e->dfa_engine);
                release_engine_buffers(e);
                free(e);
                return KADD_SQLI_ERROR_ML;
            }
        }
#else
        /* SVE unavailable: use ONNX model directly */
        const char *model_path = (strlen(config->model_rf_onnx) > 0) ? config->model_rf_onnx : config->model_rf;
        if (kadd_model_load(&e->ml_model, model_path, e->feature_dim) != 0) {
            fprintf(stderr, "[kadd_sqli] ERROR: Failed to load ML model: %s\n", model_path);
            kadd_dfa_engine_free(e->dfa_engine);
            release_engine_buffers(e);
            free(e);
            return KADD_SQLI_ERROR_ML;
        }
#endif
        e->feature_dim = kadd_model_get_feature_dim(e->ml_model);
    }

    /* Load DL model */
    if (e->engine_mode == KADD_ENGINE_MODE_DL) {
#if KADD_HAVE_SVE
        const char *model_path = (strlen(config->model_cnn) > 0) ? config->model_cnn : config->model_cnn_onnx;
        if (kadd_dl_model_load(&e->dl_model, model_path) != 0) {
            /* Fallback to ONNX if native failed and they are different */
            if (strlen(config->model_cnn) > 0 && strlen(config->model_cnn_onnx) > 0 &&
                strcmp(config->model_cnn, config->model_cnn_onnx) != 0) {
                fprintf(stdout, "[kadd_sqli] WARN: native DL model load failed, trying ONNX: %s\n", config->model_cnn_onnx);
                if (kadd_dl_model_load(&e->dl_model, config->model_cnn_onnx) != 0) {
                    fprintf(stderr, "[kadd_sqli] ERROR: Failed to load DL model (both native and ONNX)\n");
                    kadd_dfa_engine_free(e->dfa_engine);
                    release_engine_buffers(e);
                    free(e);
                    return KADD_ERROR_MODEL_LOAD_FAILED;
                }
            } else {
                fprintf(stderr, "[kadd_sqli] ERROR: Failed to load DL model: %s\n", model_path);
                kadd_dfa_engine_free(e->dfa_engine);
                release_engine_buffers(e);
                free(e);
                return KADD_ERROR_MODEL_LOAD_FAILED;
            }
        }
#else
        /* SVE unavailable: use ONNX model directly */
        const char *model_path = (strlen(config->model_cnn_onnx) > 0) ? config->model_cnn_onnx : config->model_cnn;
        if (kadd_dl_model_load(&e->dl_model, model_path) != 0) {
            fprintf(stderr, "[kadd_sqli] ERROR: Failed to load DL model: %s\n", model_path);
            kadd_dfa_engine_free(e->dfa_engine);
            release_engine_buffers(e);
            free(e);
            return KADD_ERROR_MODEL_LOAD_FAILED;
        }
#endif
    }

    *engine = e;
    return KADD_SUCCESS;
}
// 从配置文件创建 SQL 注入检测引擎
int kadd_sqli_engine_create_from_file(kadd_sqli_engine_t **engine, const char *config_file)
{
    if (!engine || !config_file) {
        fprintf(stderr, "[kadd_sqli] ERROR: Invalid parameters\n");
        return KADD_ERROR_INVALID_INPUT;
    }

    kadd_config_t config;
    if (kadd_config_load(config_file, &config) != 0) {
        fprintf(stderr, "[kadd_sqli] ERROR: Failed to load config file: %s\n", config_file);
        return KADD_ERROR_CONFIG;
    }

    kadd_dfa_table_paths_t table_paths = {
        .transition_table_path = config.transition_table,
        .output_table_path = config.output_table,
        .eod_table_path = config.eod_table
    };

    int result = create_engine_internal(engine, &table_paths, &config);
    kadd_config_free(&config);
    return result;
}
// 检测单个输入
int kadd_sqli_detect(kadd_sqli_engine_t *engine, const char *input, float *probability)
{
    if (!engine || !input || !probability) {
        fprintf(stderr, "[kadd_sqli] ERROR: Invalid parameters\n");
        return KADD_ERROR_INVALID_INPUT;
    }

    size_t input_len = strlen(input);
    if (input_len == 0) {
        *probability = 0.0f;
        return KADD_SUCCESS;
    }

    /* Step 1: DFA tokenize */
    int token_count = 0;
    if (kadd_dfa_engine_tokenize(engine->dfa_engine, input, input_len,
                                 engine->token_buffer, &token_count) != 0) {
        fprintf(stderr, "[kadd_sqli] ERROR: DFA tokenize failed\n");
        return KADD_SQLI_ERROR_DFA;
    }

    float prob = 0.0f;

    if (engine->engine_mode == KADD_ENGINE_MODE_ML) {
        /* Step 2a: Feature extraction -> ML inference */
        if (kadd_seq_to_feature(engine->token_buffer, token_count, engine->window_size,
                                 engine->feature_buffer, engine->feature_dim) != 0) {
            fprintf(stderr, "[kadd_sqli] ERROR: Feature extraction failed\n");
            return KADD_SQLI_ERROR_FEATURE;
        }
        convert_int_to_float_features(engine->feature_buffer, engine->float_feature_buffer, engine->feature_dim);
        int ret = kadd_model_predict(engine->ml_model, engine->float_feature_buffer, &prob);
        if (ret != 0) {
            fprintf(stderr, "[kadd_sqli] ERROR: ML prediction failed\n");
            return KADD_SQLI_ERROR_ML;
        }
    } else if (engine->engine_mode == KADD_ENGINE_MODE_DL) {
        /* Step 2b: Tokens -> DL inference */
        for (int i = 0; i < KADD_DL_SEQUENCE_LENGTH; i++){
            engine->dl_input_buffer[i] = 0.0f;
        }     
        int copy_len = (token_count < KADD_DL_SEQUENCE_LENGTH) ? token_count : KADD_DL_SEQUENCE_LENGTH;
        for (int i = 0; i < copy_len; i++)
            engine->dl_input_buffer[i] = (float)engine->token_buffer[i];
        int ret = kadd_dl_model_predict(engine->dl_model, engine->dl_input_buffer, &prob);
        if (ret != 0) {
            fprintf(stderr, "[kadd_sqli] ERROR: DL prediction failed\n");
            return KADD_SQLI_ERROR_DL;
        }
    }

    *probability = prob;
    return KADD_SUCCESS;
}
// 分配批处理缓冲区
static int allocate_batch_buffers(kadd_sqli_engine_t *engine, int batch_size,
                                   kadd_batch_buffers_t *buffers)
{
    if (!engine || !buffers) {
        fprintf(stderr, "[kadd_sqli] ERROR: Invalid parameters for allocate_batch_buffers (engine=%s, buffers=%s)\n",
                (engine ? "valid" : "NULL"), (buffers ? "valid" : "NULL"));
        return KADD_ERROR_INVALID_INPUT;
    }

    int **dfa_outputs = (int **)calloc(batch_size, sizeof(int *));
    int *dfa_dims = (int *)calloc(batch_size, sizeof(int));
    int *features = (int *)calloc(batch_size * engine->feature_dim, sizeof(int));
    float *float_features = (float *)calloc(batch_size * engine->feature_dim, sizeof(float));

    if (!dfa_outputs || !dfa_dims || !features || !float_features) {
        fprintf(stderr, "[kadd_sqli] ERROR: Failed to allocate batch buffers (batch_size=%d, feature_dim=%d)\n",
                batch_size, engine->feature_dim);
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
        fprintf(stderr, "[kadd_sqli] ERROR: Batch DFA tokenize failed (result=%d)\n", dfa_result);
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
        fprintf(stderr, "[kadd_sqli] ERROR: Batch feature extraction failed (result=%d)\n", feat_result);
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
    convert_int_to_float_features(buffers->features, buffers->float_features, total_features);

    float batch_probs[KADD_ML_MAX_BATCH_SIZE];
    int ml_result = kadd_model_predict_batch(params->engine->ml_model, buffers->float_features,
                                                params->current_batch, batch_probs, params->current_batch);
    if (ml_result != KADD_SUCCESS) {
        fprintf(stderr, "[kadd_sqli] ERROR: Batch ML prediction failed (result=%d)\n", ml_result);
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
        fprintf(stderr, "[kadd_sqli] ERROR: Invalid batch detect params (out_batch_size=%s, probs=%s)\n",
                (out_actual_batch_size ? "valid" : "NULL"), (probabilities ? "valid" : "NULL"));
        return KADD_ERROR_INVALID_INPUT;
    }

    *out_actual_batch_size = batch_size;
    if (*out_actual_batch_size <= 0) {
        *out_actual_batch_size = KADD_ML_DEFAULT_BATCH_SIZE;
    }
    if (*out_actual_batch_size > KADD_ML_MAX_BATCH_SIZE) {
        *out_actual_batch_size = KADD_ML_MAX_BATCH_SIZE;
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
        fprintf(stderr, "[kadd_sqli] ERROR: Invalid batch parameters (engine=%s, inputs=%s, probs=%s, count=%d)\n",
                (engine ? "valid" : "NULL"), (inputs ? "valid" : "NULL"), (probabilities ? "valid" : "NULL"), count);
        return KADD_ERROR_INVALID_INPUT;
    }
    if (count > KADD_ML_MAX_SAMPLE_COUNT) {
        fprintf(stderr, "[kadd_sqli] ERROR: Batch count %d exceeds limit %d\n", count, KADD_ML_MAX_SAMPLE_COUNT);
        return KADD_ERROR_INVALID_PARAM;
    }

    /* DL 模式不支持批处理（ONNX 不支持 batch），逐条调用 kadd_sqli_detect */
    if (engine->engine_mode == KADD_ENGINE_MODE_DL) {
        for (int i = 0; i < count; i++) {
            int ret = kadd_sqli_detect(engine, inputs[i], &probabilities[i]);
            if (ret != KADD_SUCCESS) {
                probabilities[i] = 0.0f;
            }
        }
        return KADD_SUCCESS;
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

    // 释放缓冲区（只释放数组指针，不释放内容）
    cleanup_sqli_batch_buffers(buffers.dfa_outputs, buffers.dfa_dims,
                               buffers.features, buffers.float_features, 0);

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
    if (engine->dl_model) {
        kadd_dl_model_free(engine->dl_model);
        engine->dl_model = NULL;
    }
    if (engine->dfa_engine) {
        kadd_dfa_engine_free(engine->dfa_engine);
        engine->dfa_engine = NULL;
    }

    /* 释放内存池 */
    release_engine_buffers(engine);

    free(engine);
}
