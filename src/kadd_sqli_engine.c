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

/**
 * @brief SQL 注入检测引擎内部配置（不对外暴露）
 */
typedef struct {
    char table_path[512];
    char state_path[512];
    char model_path[512];
    int feature_dim;
    int window_size;
    float threshold;
    int max_input_length;
    int max_output_dim;
    int batch_size;
} kadd_sqli_config_internal_t;

/**
 * @brief SQL 注入检测引擎内部结构
 */
struct kadd_sqli_engine_t {
    kadd_dfa_engine_t *dfa_engine;       /* DFA 分词引擎 */
    kadd_model_handle_t *ml_model;       /* ML 推理模型 */
    kadd_sqli_config_internal_t config;  /* 引擎配置 */
};

/* ========== 公共接口 ========== */

/**
 * @brief 创建 SQL 注入检测引擎（内部函数）
 */
static int kadd_sqli_engine_create_internal(kadd_sqli_engine_t **engine, const kadd_sqli_config_internal_t *config) {
    if (!engine || !config) {
        return KADD_SQLI_ERROR_INVALID_INPUT;
    }

    /* 检查配置有效性 */
    if (!config->table_path || !config->state_path) {
        fprintf(stderr, "[KADD_SQLI] Invalid config: table_path and state_path required\n");
        return KADD_SQLI_ERROR_CONFIG;
    }

    if (!config->model_path) {
        fprintf(stderr, "[KADD_SQLI] Invalid config: model_path is NULL\n");
        return KADD_SQLI_ERROR_CONFIG;
    }

    /* 分配引擎结构 */
    kadd_sqli_engine_t *e = (kadd_sqli_engine_t *)calloc(1, sizeof(kadd_sqli_engine_t));
    if (!e) {
        fprintf(stderr, "[KADD_SQLI] Memory allocation failed\n");
        return KADD_SQLI_ERROR_MEMORY;
    }

    /* 复制配置字符串到内部结构 */
    strncpy(e->config.table_path, config->table_path, sizeof(e->config.table_path) - 1);
    strncpy(e->config.state_path, config->state_path, sizeof(e->config.state_path) - 1);
    strncpy(e->config.model_path, config->model_path, sizeof(e->config.model_path) - 1);

    /* 复制数值配置 */
    e->config.feature_dim = config->feature_dim > 0 ? config->feature_dim : 28;
    e->config.window_size = config->window_size > 0 ? config->window_size : 5;
    e->config.threshold = config->threshold > 0.0f ? config->threshold : 0.5f;
    e->config.max_input_length = config->max_input_length > 0 ? config->max_input_length : 8192;
    e->config.max_output_dim = config->max_output_dim > 0 ? config->max_output_dim : 8192;
    e->config.batch_size = config->batch_size > 0 ? config->batch_size : 64;

    /* 创建 DFA 引擎 */
    if (kadd_dfa_engine_create(&e->dfa_engine,
                               e->config.table_path,
                               e->config.state_path,
                               e->config.max_input_length,
                               e->config.max_output_dim) != 0) {
        fprintf(stderr, "[KADD_SQLI] Failed to create DFA engine\n");
        free(e);
        return KADD_SQLI_ERROR_DFA;
    }

    /* 加载 ML 模型 */
    if (kadd_model_load(&e->ml_model, e->config.model_path, e->config.feature_dim) != 0) {
        fprintf(stderr, "[KADD_SQLI] Failed to load ML model: %s\n", e->config.model_path);
        kadd_dfa_engine_free(e->dfa_engine);
        free(e);
        return KADD_SQLI_ERROR_ML;
    }

    fprintf(stderr, "[KADD_SQLI] Engine created successfully\n");
    fprintf(stderr, "[KADD_SQLI]   - DFA table: %s\n", e->config.table_path);
    fprintf(stderr, "[KADD_SQLI]   - DFA state: %s\n", e->config.state_path);
    fprintf(stderr, "[KADD_SQLI]   - ML model: %s\n", e->config.model_path);
    fprintf(stderr, "[KADD_SQLI]   - Feature dim: %d\n", e->config.feature_dim);
    fprintf(stderr, "[KADD_SQLI]   - Window size: %d\n", e->config.window_size);
    fprintf(stderr, "[KADD_SQLI]   - Threshold: %.2f\n", e->config.threshold);
    fprintf(stderr, "[KADD_SQLI]   - Batch size: %d\n", e->config.batch_size);

    *engine = e;
    return KADD_SQLI_OK;
}

/**
 * @brief 从配置文件创建 SQL 注入检测引擎
 *
 * @param engine 引擎句柄指针的指针
 * @param config_file JSON 配置文件路径 (如 "config.json")
 *
 * @return KADD_SQLI_OK 成功, 其他值表示失败
 *
 * @note 配置文件需包含 kadd_ml_engine、kadd_dfa_engine、kadd_feature_engine、kadd_sqli_engine 等字段
 */
int kadd_sqli_engine_create_from_file(kadd_sqli_engine_t **engine, const char *config_file) {
    if (!engine || !config_file) {
        fprintf(stderr, "[KADD_SQLI] Invalid parameter: engine or config_file is NULL\n");
        return KADD_SQLI_ERROR_INVALID_INPUT;
    }

    /* 读取 JSON 文件 */
    FILE *fp = fopen(config_file, "r");
    if (!fp) {
        fprintf(stderr, "[KADD_SQLI] Failed to open config file: %s\n", config_file);
        return KADD_SQLI_ERROR_CONFIG;
    }

    /* 获取文件大小 */
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(fp);
        fprintf(stderr, "[KADD_SQLI] Invalid config file size\n");
        return KADD_SQLI_ERROR_CONFIG;
    }

    /* 读取文件内容 */
    char *json_content = (char *)malloc(file_size + 1);
    if (!json_content) {
        fclose(fp);
        return KADD_SQLI_ERROR_MEMORY;
    }

    size_t read_size = fread(json_content, 1, file_size, fp);
    json_content[read_size] = '\0';
    fclose(fp);

    /* 解析 JSON */
    cJSON *root = cJSON_Parse(json_content);
    free(json_content);

    if (!root) {
        fprintf(stderr, "[KADD_SQLI] Failed to parse config JSON\n");
        return KADD_SQLI_ERROR_CONFIG;
    }

    /* 辅助宏：从 JSON 获取字符串值 */
    #define GET_JSON_STRING(obj, key, dest, default_val) \
        do { \
            cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key); \
            if (item && cJSON_IsString(item)) { \
                strncpy(dest, item->valuestring, sizeof(dest) - 1); \
                dest[sizeof(dest) - 1] = '\0'; \
            } else { \
                strncpy(dest, default_val, sizeof(dest) - 1); \
                dest[sizeof(dest) - 1] = '\0'; \
            } \
        } while(0)

    /* 辅助宏：从 JSON 获取整数值 */
    #define GET_JSON_INT(obj, key, dest, default_val) \
        do { \
            cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key); \
            if (item && cJSON_IsNumber(item)) { \
                dest = (int)item->valuedouble; \
            } else { \
                dest = default_val; \
            } \
        } while(0)

    /* 辅助宏：从 JSON 获取浮点数值 */
    #define GET_JSON_FLOAT(obj, key, dest, default_val) \
        do { \
            cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key); \
            if (item && cJSON_IsNumber(item)) { \
                dest = (float)item->valuedouble; \
            } else { \
                dest = default_val; \
            } \
        } while(0)

    /* 构建配置结构 - 使用静态缓冲区存储字符串 */
    static char table_path_buf[512];
    static char state_path_buf[512];
    static char model_path_buf[512];

    kadd_sqli_config_internal_t config = {0};

    /* DFA 引擎配置 */
    cJSON *dfa_obj = cJSON_GetObjectItemCaseSensitive(root, "kadd_dfa_engine");
    if (dfa_obj) {
        GET_JSON_STRING(dfa_obj, "table_path", table_path_buf, "tools/py/profile_data/transition_table.bin");
        GET_JSON_STRING(dfa_obj, "state_path", state_path_buf, "tools/py/profile_data/states_meta.json");
        GET_JSON_INT(dfa_obj, "max_input_length", config.max_input_length, 8192);
        GET_JSON_INT(dfa_obj, "max_output_dim", config.max_output_dim, 8192);
    } else {
        /* 默认值 */
        strncpy(table_path_buf, "tools/py/profile_data/transition_table.bin", sizeof(table_path_buf) - 1);
        table_path_buf[sizeof(table_path_buf) - 1] = '\0';
        strncpy(state_path_buf, "tools/py/profile_data/states_meta.json", sizeof(state_path_buf) - 1);
        state_path_buf[sizeof(state_path_buf) - 1] = '\0';
        config.max_input_length = 8192;
        config.max_output_dim = 8192;
    }

    /* 复制路径到配置结构 */
    strncpy(config.table_path, table_path_buf, sizeof(config.table_path) - 1);
    config.table_path[sizeof(config.table_path) - 1] = '\0';
    strncpy(config.state_path, state_path_buf, sizeof(config.state_path) - 1);
    config.state_path[sizeof(config.state_path) - 1] = '\0';

    /* Feature 引擎配置 */
    cJSON *feature_obj = cJSON_GetObjectItemCaseSensitive(root, "kadd_feature_engine");
    if (feature_obj) {
        GET_JSON_INT(feature_obj, "window_size", config.window_size, 5);
    } else {
        config.window_size = 5;
    }

    /* ML 引擎配置 */
    cJSON *ml_obj = cJSON_GetObjectItemCaseSensitive(root, "kadd_ml_engine");
    if (ml_obj) {
        GET_JSON_STRING(ml_obj, "model_path", model_path_buf, "assets/model/model_tokens_rf.onnx");
        GET_JSON_INT(ml_obj, "feature_dim", config.feature_dim, 28);
    } else {
        strncpy(model_path_buf, "assets/model/model_tokens_rf.onnx", sizeof(model_path_buf) - 1);
        model_path_buf[sizeof(model_path_buf) - 1] = '\0';
        config.feature_dim = 28;
    }

    /* 复制模型路径到配置结构 */
    strncpy(config.model_path, model_path_buf, sizeof(config.model_path) - 1);
    config.model_path[sizeof(config.model_path) - 1] = '\0';

    /* SQLi 引擎配置 */
    cJSON *sqli_obj = cJSON_GetObjectItemCaseSensitive(root, "kadd_sqli_engine");
    if (sqli_obj) {
        GET_JSON_FLOAT(sqli_obj, "threshold", config.threshold, 0.5f);
        GET_JSON_INT(sqli_obj, "batch_size", config.batch_size, 64);
    } else {
        config.threshold = 0.5f;
        config.batch_size = 64;
    }

    /* 清理 JSON 对象 */
    cJSON_Delete(root);

    #undef GET_JSON_STRING
    #undef GET_JSON_INT
    #undef GET_JSON_FLOAT

    /* 创建引擎 */
    return kadd_sqli_engine_create_internal(engine, &config);
}

/**
 * @brief 检测单个输入
 */
int kadd_sqli_detect(kadd_sqli_engine_t *engine, const char *input, float *probability) {
    if (!engine || !input || !probability) {
        return KADD_SQLI_ERROR_INVALID_INPUT;
    }

    /* 初始化输出 */
    *probability = 0.0f;

    /* 步骤 1: DFA 分词 -> Token 索引序列 */
    int *token_output = (int *)malloc(engine->config.max_output_dim * sizeof(int));
    if (!token_output) {
        return KADD_SQLI_ERROR_MEMORY;
    }

    int token_count = 0;
    size_t input_len = strlen(input);
    int dfa_result = kadd_dfa_engine_tokenize(engine->dfa_engine, input, input_len, token_output, &token_count);
    if (dfa_result != 0) {
        free(token_output);
        return KADD_SQLI_ERROR_DFA;
    }

    /* 步骤 2: 特征提取 -> 固定维度特征向量 */
    int *features = (int *)calloc(engine->config.feature_dim, sizeof(int));
    if (!features) {
        free(token_output);
        return KADD_SQLI_ERROR_MEMORY;
    }

    if (kadd_seq_to_feature(token_output, token_count,
                            engine->config.window_size,
                            features, engine->config.feature_dim) != 0) {
        free(token_output);
        free(features);
        return KADD_SQLI_ERROR_FEATURE;
    }

    /* 步骤 3: ML 推理 -> 注入概率 */
    /* 将 int 特征转换为 float */
    /*
     * ARM 平台内存对齐注意事项：
     * ONNX Runtime 期望输入数据是 4 字节对齐的（float 对齐）
     * 使用标准的 malloc 可能返回未对齐的指针，导致 ARM 上读取数据错误
     *
     * 解决方案：使用 aligned_alloc 或 posix_memalign
     */
    float *float_features = NULL;
#if defined(_WIN32) || defined(__STDC_NO_THREADS__)
    /* Windows 或不支持 C11 的平台，使用 malloc（Windows 默认对齐） */
    float_features = (float *)malloc(engine->config.feature_dim * sizeof(float));
#else
    /* POSIX 平台（包括 ARM Linux），使用 aligned_alloc 确保对齐 */
    if (posix_memalign((void **)&float_features, 16, engine->config.feature_dim * sizeof(float)) != 0) {
        /* 16 字节对齐（NEON/SVE 的要求） */
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

    for (int i = 0; i < engine->config.feature_dim; i++) {
        float_features[i] = (float)features[i];
    }

    float prob = 0.0f;
    int ret = kadd_model_predict(engine->ml_model, float_features, &prob);

    free(token_output);
    free(features);
#if defined(_WIN32) || defined(__STDC_NO_THREADS__)
    free(float_features);
#else
    free(float_features);  /* posix_memalign 分配的内存可以用 free 释放 */
#endif

    if (ret != 0) {
        return KADD_SQLI_ERROR_ML;
    }

    /* 返回概率值 */
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

    /* 设置默认 batch_size */
    if (batch_size <= 0) {
        batch_size = engine->config.batch_size > 0 ? engine->config.batch_size : 64;
    }

    /* 限制 batch_size 范围 */
    if (batch_size > 1024) {
        batch_size = 1024;
    }
    if (batch_size < 1) {
        batch_size = 1;
    }

    /* 初始化概率数组 */
    for (int i = 0; i < count; i++) {
        probabilities[i] = -1.0f;  // -1.0 表示失败
    }

    int success_count = 0;

    /* 分块处理 */
    for (int batch_start = 0; batch_start < count; batch_start += batch_size) {
        int current_batch = (batch_start + batch_size < count) ? batch_size : (count - batch_start);

        /* 步骤 1: 批量 DFA 分词 */
        int *token_counts = (int *)calloc(current_batch, sizeof(int));
        int **all_token_outputs = (int **)calloc(current_batch, sizeof(int *));

        if (!token_counts || !all_token_outputs) {
            free(token_counts);
            free(all_token_outputs);
            continue;
        }

        int dfa_success = 0;
        for (int i = 0; i < current_batch; i++) {
            int idx = batch_start + i;
            all_token_outputs[i] = (int *)malloc(engine->config.max_output_dim * sizeof(int));
            if (all_token_outputs[i]) {
                size_t input_len = strlen(inputs[idx]);
                if (kadd_dfa_engine_tokenize(engine->dfa_engine, inputs[idx], input_len,
                                             all_token_outputs[i], &token_counts[i]) == 0) {
                    dfa_success++;
                } else {
                    free(all_token_outputs[i]);
                    all_token_outputs[i] = NULL;
                }
            }
        }

        if (dfa_success == 0) {
            free(token_counts);
            for (int i = 0; i < current_batch; i++) {
                if (all_token_outputs[i]) free(all_token_outputs[i]);
            }
            free(all_token_outputs);
            continue;
        }

        /* 步骤 2: 批量特征提取 */
        int *all_features = (int *)calloc(current_batch * engine->config.feature_dim, sizeof(int));
        if (!all_features) {
            free(token_counts);
            for (int i = 0; i < current_batch; i++) {
                if (all_token_outputs[i]) free(all_token_outputs[i]);
            }
            free(all_token_outputs);
            continue;
        }

        int feature_success = 0;
        for (int i = 0; i < current_batch; i++) {
            if (!all_token_outputs[i]) {
                continue;
            }

            int *feature_ptr = &all_features[i * engine->config.feature_dim];
            if (kadd_seq_to_feature(all_token_outputs[i], token_counts[i],
                                    engine->config.window_size,
                                    feature_ptr, engine->config.feature_dim) == 0) {
                feature_success++;
            }
        }

        /* 步骤 3: 批量 ML 推理 */
        if (feature_success > 0) {
            /* 将 int 特征转换为 float */
            int total_features = current_batch * engine->config.feature_dim;
            float *float_features = NULL;

#if defined(_WIN32) || defined(__STDC_NO_THREADS__)
            /* Windows 或不支持 C11 的平台 */
            float_features = (float *)malloc(total_features * sizeof(float));
#else
            /* POSIX 平台（包括 ARM Linux），使用 aligned_alloc 确保对齐 */
            if (posix_memalign((void **)&float_features, 16, total_features * sizeof(float)) != 0) {
                float_features = NULL;
            }
#endif

            if (float_features) {
                for (int i = 0; i < total_features; i++) {
                    float_features[i] = (float)all_features[i];
                }

                /* 调用 ML 引擎的批量推理 */
                float batch_probs[1024];  // 最多处理 1024 个样本
                int ml_result = kadd_model_predict_batch(engine->ml_model, float_features, current_batch, batch_probs, 64);

                if (ml_result > 0) {
                    /* 填充结果 */
                    for (int i = 0; i < current_batch; i++) {
                        int idx = batch_start + i;
                        if (i < ml_result) {
                            probabilities[idx] = batch_probs[i];
                            success_count++;
                        }
                    }
                }

                free(float_features);
            }
        }

        /* 清理当前批次资源 */
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

    /* 释放 ML 模型 */
    if (engine->ml_model) {
        kadd_model_free(engine->ml_model);
    }

    /* 释放 DFA 引擎 */
    if (engine->dfa_engine) {
        kadd_dfa_engine_free(engine->dfa_engine);
    }

    free(engine);
}

/**
 * @brief 获取错误描述
 */
const char *kadd_sqli_error_string(int error_code) {
    switch (error_code) {
        case KADD_SQLI_OK: return "Success";
        case KADD_SQLI_ERROR_MEMORY: return "Memory allocation failed";
        case KADD_SQLI_ERROR_INVALID_INPUT: return "Invalid input";
        case KADD_SQLI_ERROR_DFA: return "DFA engine error";
        case KADD_SQLI_ERROR_FEATURE: return "Feature extraction error";
        case KADD_SQLI_ERROR_ML: return "ML inference error";
        default: return "Unknown error";
    }
}
