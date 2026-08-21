/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * @file kadd_inference_engine.c
 * @brief ONNX 推理引擎实现
 *
 * 支持平台：Linux (x86_64, aarch64), Windows (x64)
 * 依赖：ONNX Runtime 1.18+ (API v17+)
 */
#include "kadd_inference_engine.h"
#include "kadd_config_loader.h"
#include "kadd_errors.h"
#include "onnxruntime_c_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========== 内部结构体 ========== */
struct kadd_inference_engine_t {
    OrtSession *session;
    OrtEnv *env;
    OrtMemoryInfo *memory_info;
    const OrtApi *api;
    char *input_name;
    char *output_name;
    int feature_dim;
};

static OrtEnv *g_ort_env = NULL;
static const OrtApi *g_ort_api = NULL;
static int g_ort_env_ref_count = 0;

/* ONNX Runtime 最小 API 版本（v17 对应 ONNX Runtime 1.18+） */
#define ORT_MIN_API_VERSION 17

/* 张量最大维度数 */
#define MAX_TENSOR_DIMS 4

/* ========== 错误处理 ========== */
static int check_ort_status(const OrtApi *api, OrtStatus *status, const char *msg)
{
    if (!status) {
        return 0;
    }
    fprintf(stderr, "[kadd_inference] ERROR: %s: %s\n", msg, api->GetErrorMessage(status));
    (void)api->ReleaseStatus(status);
    return -1;
}

/* ========== 初始化 ========== */
static int init_ort_api(kadd_inference_engine_t *e)
{
    if (!g_ort_api) {
        const OrtApiBase *api_base = OrtGetApiBase();
        if (!api_base) {
            return -1;
        }

        // ONNX Runtime 1.18+ 支持 API 版本 17+
        // 从编译时最高版本向下回退，找到运行时支持的最高版本
        for (int version = ORT_API_VERSION; version >= ORT_MIN_API_VERSION; version--) {
            g_ort_api = api_base->GetApi(version);
            if (g_ort_api) {
                break;
            }
        }

        if (!g_ort_api) {
            return -1;
        }
    }
    e->api = g_ort_api;
    return 0;
}

static int init_ort_env(kadd_inference_engine_t *e)
{
    if (!g_ort_env) {
        OrtStatus *status = e->api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "inference", &g_ort_env);
        if (check_ort_status(e->api, status, "CreateEnv")) {
            return -1;
        }
    }
    g_ort_env_ref_count++;
    e->env = g_ort_env;
    return 0;
}

/* ========== IO 名称检测 ========== */
static char *get_io_name(kadd_inference_engine_t *e, int is_output)
{
    OrtAllocator *allocator = NULL;
    OrtStatus *status = e->api->GetAllocatorWithDefaultOptions(&allocator);
    if (status) {
        return NULL;
    }

    char *name = NULL;
    if (is_output) {
        /* 获取输出数量，取最后一个输出（如 TreeEnsembleClassifier 输出 [label, probabilities]，取 probabilities） */
        size_t output_count = 0;
        status = e->api->SessionGetOutputCount(e->session, &output_count);
        if (status) {
            return NULL;
        }
        size_t idx = (output_count > 0) ? (output_count - 1) : 0;
        status = e->api->SessionGetOutputName(e->session, idx, allocator, &name);
    } else {
        status = e->api->SessionGetInputName(e->session, 0, allocator, &name);
    }
    if (status) {
        return NULL;
    }

    char *result = strdup(name);
    (void)e->api->AllocatorFree(allocator, name);
    return result;
}

static int detect_io_names(kadd_inference_engine_t *e)
{
    e->input_name = get_io_name(e, 0);
    e->output_name = get_io_name(e, 1);
    return (e->input_name && e->output_name) ? 0 : -1;
}

/* ========== 获取模型输入维度 ========== */
static int get_model_input_dim(kadd_inference_engine_t *e)
{
    OrtTypeInfo *type_info = NULL;
    const OrtTensorTypeAndShapeInfo *tensor_info = NULL;
    OrtStatus *status = NULL;
    size_t dim_count = 0;
    int64_t dims[MAX_TENSOR_DIMS] = {0};

    status = e->api->SessionGetInputTypeInfo(e->session, 0, &type_info);
    if (check_ort_status(e->api, status, "GetInputTypeInfo")) {
        return -1;
    }

    /* 使用 CastTypeInfoToTensorInfo（ONNX Runtime 1.17+，API v17+） */
    status = e->api->CastTypeInfoToTensorInfo(type_info, &tensor_info);
    if (status) {
            goto cleanup;
        }

    status = e->api->GetDimensionsCount(tensor_info, &dim_count);
    if (status || dim_count < 2) {
            goto cleanup;
        }

    status = e->api->GetDimensions(tensor_info, dims, dim_count);
    if (status) {
            goto cleanup;
        }

    e->feature_dim = (int)dims[1];
    (void)e->api->ReleaseTypeInfo(type_info);
    return 0;

cleanup:
    if (status) {
            (void)e->api->ReleaseStatus(status);
        }
    fprintf(stderr, "[kadd_inference] ERROR: Failed to cast type info to tensor info\n");
    (void)e->api->ReleaseTypeInfo(type_info);
    return -1;
}

/* ========== 创建 ONNX Runtime Session ========== */
static int create_ort_session(kadd_inference_engine_t *e, const char *model_path)
{
    OrtSessionOptions *options = NULL;
    OrtStatus *status = e->api->CreateSessionOptions(&options);
    if (status) {
            goto cleanup;
        }

    status = e->api->SetSessionExecutionMode(options, ORT_SEQUENTIAL);
    if (status) {
            goto cleanup_options;
        }

    status = e->api->SetSessionGraphOptimizationLevel(options, ORT_ENABLE_ALL);
    if (status) {
            goto cleanup_options;
        }

    status = e->api->CreateSession(e->env, model_path, options, &e->session);
    (void)e->api->ReleaseSessionOptions(options);
    if (status) {
            goto cleanup;
        }

    status = e->api->CreateMemoryInfo("Cpu", OrtArenaAllocator, 0, OrtMemTypeDefault, &e->memory_info);
    if (status) {
            goto cleanup_session;
        }

    return 0;

cleanup_session:
    (void)e->api->ReleaseSession(e->session);
    e->session = NULL;
    return -1;

cleanup_options:
    (void)e->api->ReleaseSessionOptions(options);
cleanup:
    if (status) {
        fprintf(stderr, "[kadd_inference] ERROR: %s\n", e->api->GetErrorMessage(status));
        (void)e->api->ReleaseStatus(status);
    }
    return -1;
}

/* ========== 清理引擎资源 ========== */
static void release_engine_resources(kadd_inference_engine_t *e)
{
    if (!e) {
        return;
    }

    if (e->input_name) { free(e->input_name); e->input_name = NULL; }
    if (e->output_name) { free(e->output_name); e->output_name = NULL; }

    if (e->api) {
        if (e->memory_info) { (void)e->api->ReleaseMemoryInfo(e->memory_info); e->memory_info = NULL; }
        if (e->session) { (void)e->api->ReleaseSession(e->session); e->session = NULL; }
    }
}

/* ========== 公共接口 ========== */

/**
 * 内部加载核心：分配句柄 + 初始化 ONNX + warmup
 * model_path 必须非 NULL。
 */
static int load_engine_internal(kadd_inference_engine_t **engine,
                                const char *model_path)
{
    kadd_inference_engine_t *e = (kadd_inference_engine_t *)calloc(1, sizeof(*e));
    if (!e) {
        return KADD_ERROR_OUT_OF_MEMORY;
    }

    if (init_ort_api(e) != 0 || init_ort_env(e) != 0 || create_ort_session(e, model_path) != 0) {
        free(e);
        return KADD_ERROR_MODEL_LOAD_FAILED;
    }

    if (detect_io_names(e) != 0 || get_model_input_dim(e) != 0) {
        release_engine_resources(e);
        free(e);
        return KADD_ERROR_MODEL_LOAD_FAILED;
    }

    /* warmup */
    float *warmup_features = (float *)calloc(e->feature_dim, sizeof(float));
    if (warmup_features) {
        float warmup_prob = 0.0f;
        int warmup_ret = kadd_inference_predict(e, warmup_features, &warmup_prob);
        free(warmup_features);
        if (warmup_ret != KADD_SUCCESS) {
            fprintf(stderr, "[kadd_inference] ERROR: Warmup inference failed (feature_dim=%d)\n",
                    e->feature_dim);
            release_engine_resources(e);
            free(e);
            return KADD_ERROR_MODEL_LOAD_FAILED;
        }
    }

    *engine = e;
    return KADD_SUCCESS;
}

int kadd_inference_engine_load(kadd_inference_engine_t **engine,
                                const char *config_file,
                                kadd_inference_type_t type)
{
    if (!engine || !config_file) {
        return KADD_ERROR_NULL_PTR;
    }
    *engine = NULL;

    if (type < KADD_INFERENCE_TYPE_RF || type > KADD_INFERENCE_TYPE_CNN) {
        return KADD_ERROR_INVALID_PARAM;
    }

    kadd_config_t config;
    int ret = kadd_config_load(config_file, &config);
    if (ret != 0) {
        return KADD_ERROR_CONFIG;
    }

    const char *model_path = (type == KADD_INFERENCE_TYPE_RF)
                                 ? config.model_rf_onnx
                                 : config.model_cnn_onnx;

    ret = load_engine_internal(engine, model_path);
    kadd_config_free(&config);
    return ret;
}

int kadd_inference_engine_load_from_path(kadd_inference_engine_t **engine,
                                          const char *model_path)
{
    if (!engine || !model_path) {
        return KADD_ERROR_NULL_PTR;
    }
    *engine = NULL;
    return load_engine_internal(engine, model_path);
}

int kadd_inference_predict(kadd_inference_engine_t *engine,
                           const float *features,
                           float *probability)
{
    /* 单样本推理是批量推理的特例：n=1, batch_size=1 */
    return kadd_inference_predict_batch(engine, features, 1, probability, 1);
}

int kadd_inference_predict_batch(kadd_inference_engine_t *engine,
                                  const float *features,
                                  int n,
                                  float *probabilities,
                                  int batch_size)
{
    if (!engine || !features || !probabilities) {
        return KADD_ERROR_NULL_PTR;
    }
    if (n <= 0 || n > KADD_INFERENCE_MAX_SAMPLE_COUNT) {
        return KADD_ERROR_INVALID_PARAM;
    }

    if (batch_size <= 0) {
        return KADD_ERROR_INVALID_PARAM;
    }
    int eff_batch = (batch_size > KADD_INFERENCE_MAX_BATCH_SIZE)
                    ? KADD_INFERENCE_DEFAULT_BATCH_SIZE : batch_size;

    for (int i = 0; i < n; i += eff_batch) {
        int cur = (i + eff_batch < n) ? eff_batch : (n - i);

        int64_t shape[] = {cur, engine->feature_dim};
        OrtValue *input = NULL;
        OrtStatus *status = engine->api->CreateTensorWithDataAsOrtValue(
            engine->memory_info, (void *)(features + i * engine->feature_dim),
            (size_t)cur * engine->feature_dim * sizeof(float),
            shape, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input);
        if (check_ort_status(engine->api, status, "CreateTensor")) {
            return KADD_ERROR_INFERENCE_FAILED;
        }

        const char *input_names[] = {engine->input_name};
        const char *output_names[] = {engine->output_name};
        OrtValue *output = NULL;

        status = engine->api->Run(engine->session, NULL, input_names,
                                (const OrtValue *const *)&input, 1,
                                output_names, 1, &output);
        (void)engine->api->ReleaseValue(input);
        if (check_ort_status(engine->api, status, "Run")) {
            return KADD_ERROR_INFERENCE_FAILED;
        }

        float *data = NULL;
        status = engine->api->GetTensorMutableData(output, (void **)&data);
        if (check_ort_status(engine->api, status, "GetTensorData")) {
            (void)engine->api->ReleaseValue(output);
            return KADD_ERROR_INFERENCE_FAILED;
        }

        for (int j = 0; j < cur; j++) {
            probabilities[i + j] = data[j * 2 + 1];
        }
        (void)engine->api->ReleaseValue(output);
    }

    return KADD_SUCCESS;
}

int kadd_inference_engine_get_feature_dim(kadd_inference_engine_t *engine)
{
    if (!engine) {
        return KADD_ERROR_NULL_PTR;
    }
    return engine->feature_dim;
}

void kadd_inference_engine_free(kadd_inference_engine_t *engine)
{
    if (!engine) {
        return;
    }

    release_engine_resources(engine);

    g_ort_env_ref_count--;
    if (g_ort_env_ref_count == 0 && g_ort_env) {
        (void)g_ort_api->ReleaseEnv(g_ort_env);
        g_ort_env = NULL;
    }

    free(engine);
}