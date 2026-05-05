/**
 * @file kadd_sqli_engine.c
 * @brief SQL 注入检测引擎实现
 *
 * 整合 DFA 引擎和 ML 引擎，提供完整的 SQL 注入检测功能
 * 流程: 输入 -> DFA 分词 -> 特征提取 -> ML 推理 -> 结果
 */

#include "kadd_sqli_engine.h"
#include "kadd_dfa_engine.h"
#include "kadd_feature_engine.h"
#include "kadd_ml_engine.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========== 内部结构体 ========== */

struct kadd_sqli_engine_t {
    kadd_dfa_engine_t *dfa_engine;
    kadd_model_handle_t *ml_model;
    int feature_dim;
    int window_size;
    int max_output_dim;
    int max_input_length;
};

/* ========== 公共接口 ========== */

static int create_engine_internal(kadd_sqli_engine_t **engine,
                                   const char *table_path, const char *state_path,
                                   const char *model_path, int feature_dim,
                                   int window_size, int max_output_dim, int max_input_length) {
    if (!engine || !table_path || !state_path || !model_path) {
        return KADD_SQLI_ERROR_INVALID_INPUT;
    }

    kadd_sqli_engine_t *e = (kadd_sqli_engine_t *)calloc(1, sizeof(kadd_sqli_engine_t));
    if (!e) {
        return KADD_SQLI_ERROR_MEMORY;
    }

    e->feature_dim = feature_dim;
    e->window_size = window_size;
    e->max_output_dim = max_output_dim;
    e->max_input_length = max_input_length;

    if (kadd_dfa_engine_create(&e->dfa_engine, table_path, state_path, e->max_input_length, e->max_output_dim) != 0) {
        free(e);
        return KADD_SQLI_ERROR_DFA;
    }

    if (kadd_model_load(&e->ml_model, model_path, e->feature_dim) != 0) {
        kadd_dfa_engine_free(e->dfa_engine);
        free(e);
        return KADD_SQLI_ERROR_ML;
    }

    *engine = e;
    return KADD_SQLI_OK;
}

/**
 * @brief 从配置文件创建 SQL 注入检测引擎
 */
int kadd_sqli_engine_create_from_file(kadd_sqli_engine_t **engine, const char *config_file) {
    if (!engine || !config_file) {
        return KADD_SQLI_ERROR_INVALID_INPUT;
    }

    FILE *fp = fopen(config_file, "r");
    if (!fp) {
        return KADD_SQLI_ERROR_CONFIG;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(fp);
        return KADD_SQLI_ERROR_CONFIG;
    }

    char *json_content = (char *)malloc(file_size + 1);
    if (!json_content) {
        fclose(fp);
        return KADD_SQLI_ERROR_MEMORY;
    }

    size_t read_size = fread(json_content, 1, file_size, fp);
    json_content[read_size] = '\0';
    fclose(fp);

    cJSON *root = cJSON_Parse(json_content);
    free(json_content);

    if (!root) {
        return KADD_SQLI_ERROR_CONFIG;
    }

    /* DFA 配置 */
    cJSON *dfa_obj = cJSON_GetObjectItemCaseSensitive(root, "kadd_dfa_engine");
    if (!dfa_obj) {
        cJSON_Delete(root);
        return KADD_SQLI_ERROR_CONFIG;
    }

    cJSON *dfa_table = cJSON_GetObjectItemCaseSensitive(dfa_obj, "table_path");
    cJSON *dfa_state = cJSON_GetObjectItemCaseSensitive(dfa_obj, "state_path");
    if (!cJSON_IsString(dfa_table) || !cJSON_IsString(dfa_state)) {
        cJSON_Delete(root);
        return KADD_SQLI_ERROR_CONFIG;
    }

    /* ML 配置 */
    cJSON *ml_obj = cJSON_GetObjectItemCaseSensitive(root, "kadd_ml_engine");
    if (!ml_obj) {
        cJSON_Delete(root);
        return KADD_SQLI_ERROR_CONFIG;
    }

    cJSON *ml_model = cJSON_GetObjectItemCaseSensitive(ml_obj, "model_path");
    if (!cJSON_IsString(ml_model)) {
        cJSON_Delete(root);
        return KADD_SQLI_ERROR_CONFIG;
    }

    cJSON *ml_dim = cJSON_GetObjectItemCaseSensitive(ml_obj, "feature_dim");
    int feature_dim = cJSON_IsNumber(ml_dim) ? (int)ml_dim->valuedouble : 28;

    cJSON *ml_window = cJSON_GetObjectItemCaseSensitive(ml_obj, "window_size");
    int window_size = cJSON_IsNumber(ml_window) ? (int)ml_window->valuedouble : 5;

    cJSON *dfa_max_out = cJSON_GetObjectItemCaseSensitive(dfa_obj, "max_output_dim");
    int max_output_dim = cJSON_IsNumber(dfa_max_out) ? (int)dfa_max_out->valuedouble : 8192;

    cJSON *dfa_max_in = cJSON_GetObjectItemCaseSensitive(dfa_obj, "max_input_length");
    int max_input_length = cJSON_IsNumber(dfa_max_in) ? (int)dfa_max_in->valuedouble : 8192;

    int result = create_engine_internal(engine, dfa_table->valuestring, dfa_state->valuestring,
                                        ml_model->valuestring, feature_dim, window_size,
                                        max_output_dim, max_input_length);
    cJSON_Delete(root);

    return result;
}

/**
 * @brief 检测单个输入
 */
int kadd_sqli_detect(kadd_sqli_engine_t *engine, const char *input, float *probability) {
    if (!engine || !input || !probability) {
        return KADD_SQLI_ERROR_INVALID_INPUT;
    }

    *probability = 0.0f;

    /* 步骤 1: DFA 分词 */
    int *token_output = (int *)malloc(engine->max_output_dim * sizeof(int));
    if (!token_output) {
        return KADD_SQLI_ERROR_MEMORY;
    }

    int token_count = 0;
    if (kadd_dfa_engine_tokenize(engine->dfa_engine, input, strlen(input), token_output, &token_count) != 0) {
        free(token_output);
        return KADD_SQLI_ERROR_DFA;
    }

    fprintf(stderr, "[SQLi_DEBUG] DFA tokens (%d): ", token_count);
    for (int i = 0; i < (token_count < 10 ? token_count : 10); i++) {
        fprintf(stderr, "%d ", token_output[i]);
    }
    if (token_count > 10) fprintf(stderr, "...");
    fprintf(stderr, "\n");

    /* 步骤 2: 特征提取 */
    int *features = (int *)calloc(engine->feature_dim, sizeof(int));
    if (!features) {
        free(token_output);
        return KADD_SQLI_ERROR_MEMORY;
    }

    if (kadd_seq_to_feature(token_output, token_count, engine->window_size, features, engine->feature_dim) != 0) {
        free(token_output);
        free(features);
        return KADD_SQLI_ERROR_FEATURE;
    }

    fprintf(stderr, "[SQLi_DEBUG] Features (first 10): ");
    int nonzero = 0;
    for (int i = 0; i < (engine->feature_dim < 10 ? engine->feature_dim : 10); i++) {
        fprintf(stderr, "%d ", features[i]);
        if (features[i] > 0) nonzero++;
    }
    if (engine->feature_dim > 10) fprintf(stderr, "...");
    fprintf(stderr, " (nonzero: %d)\n", nonzero);

    /* 步骤 3: ML 推理 */
    float *float_features = NULL;
#if defined(_WIN32)
    float_features = (float *)malloc(engine->feature_dim * sizeof(float));
#else
    if (posix_memalign((void **)&float_features, 16, engine->feature_dim * sizeof(float)) != 0) {
        free(token_output);
        free(features);
        return KADD_SQLI_ERROR_MEMORY;
    }
#endif

    if (!float_features) {
        free(token_output);
        free(features);
        return KADD_SQLI_ERROR_MEMORY;
    }

    for (int i = 0; i < engine->feature_dim; i++) {
        float_features[i] = (float)features[i];
    }

    float prob = 0.0f;
    int ret = kadd_model_predict(engine->ml_model, float_features, &prob);

    fprintf(stderr, "[SQLi_DEBUG] ML result: prob=%.6f, ret=%d\n", prob, ret);

    free(token_output);
    free(features);
    free(float_features);

    if (ret != 0) {
        return KADD_SQLI_ERROR_ML;
    }

    *probability = prob;
    return KADD_SQLI_OK;
}

/**
 * @brief 批量检测
 */
int kadd_sqli_detect_batch(kadd_sqli_engine_t *engine, const char **inputs, int count, float *probabilities, int batch_size) {
    if (!engine || !inputs || !probabilities || count <= 0) {
        return KADD_SQLI_ERROR_INVALID_INPUT;
    }

    if (batch_size <= 0) batch_size = 64;
    if (batch_size > 1024) batch_size = 1024;

    for (int i = 0; i < count; i++) {
        probabilities[i] = -1.0f;
    }

    int success_count = 0;

    for (int batch_start = 0; batch_start < count; batch_start += batch_size) {
        int current_batch = (batch_start + batch_size < count) ? batch_size : (count - batch_start);

        int *token_counts = (int *)calloc(current_batch, sizeof(int));
        int **all_token_outputs = (int **)calloc(current_batch, sizeof(int *));

        if (!token_counts || !all_token_outputs) {
            free(token_counts);
            free(all_token_outputs);
            continue;
        }

        for (int i = 0; i < current_batch; i++) {
            all_token_outputs[i] = (int *)malloc(engine->max_output_dim * sizeof(int));
            if (all_token_outputs[i]) {
                if (kadd_dfa_engine_tokenize(engine->dfa_engine, inputs[batch_start + i],
                                             strlen(inputs[batch_start + i]),
                                             all_token_outputs[i], &token_counts[i]) != 0) {
                    free(all_token_outputs[i]);
                    all_token_outputs[i] = NULL;
                }
            }
        }

        int *all_features = (int *)calloc(current_batch * engine->feature_dim, sizeof(int));
        if (!all_features) {
            free(token_counts);
            for (int i = 0; i < current_batch; i++) {
                if (all_token_outputs[i]) free(all_token_outputs[i]);
            }
            free(all_token_outputs);
            continue;
        }

        for (int i = 0; i < current_batch; i++) {
            if (all_token_outputs[i]) {
                kadd_seq_to_feature(all_token_outputs[i], token_counts[i], engine->window_size,
                                   &all_features[i * engine->feature_dim], engine->feature_dim);
            }
        }

        int total_features = current_batch * engine->feature_dim;
        float *float_features = NULL;
#if defined(_WIN32)
        float_features = (float *)malloc(total_features * sizeof(float));
#else
        posix_memalign((void **)&float_features, 16, total_features * sizeof(float));
#endif

        if (float_features) {
            for (int i = 0; i < total_features; i++) {
                float_features[i] = (float)all_features[i];
            }

            float batch_probs[1024];
            int ml_result = kadd_model_predict_batch(engine->ml_model, float_features, current_batch, batch_probs, 64);

            if (ml_result > 0) {
                for (int i = 0; i < ml_result; i++) {
                    probabilities[batch_start + i] = batch_probs[i];
                    success_count++;
                }
            }

            free(float_features);
        }

        free(token_counts);
        for (int i = 0; i < current_batch; i++) {
            if (all_token_outputs[i]) free(all_token_outputs[i]);
        }
        free(all_token_outputs);
        free(all_features);
    }

    return success_count;
}

/**
 * @brief 释放引擎
 */
void kadd_sqli_engine_free(kadd_sqli_engine_t *engine) {
    if (!engine) return;

    if (engine->ml_model) {
        kadd_model_free(engine->ml_model);
    }
    if (engine->dfa_engine) {
        kadd_dfa_engine_free(engine->dfa_engine);
    }
    free(engine);
}
