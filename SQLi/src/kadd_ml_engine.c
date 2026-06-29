/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * @file kadd_ml_engine.c
 * @brief ONNX 推理引擎实现
 *
 * 支持平台：Linux (x86_64, aarch64)
 * 依赖：ONNX Runtime 1.16+
 */

#include "kadd_ml_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kadd_errors.h"
#include "kadd_config_loader.h"
#include "onnxruntime_c_api.h"

/* ========== 内部结构体 ========== */
struct kadd_model_handle_t {
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
static int g_ort_env_ref_count = 0;  /* 全局环境引用计数 */

#define KADD_ORT_API_MIN_VERSION 15
#define KADD_MODEL_OUTPUT_CLASS_NUM 2
#define KADD_ORT_LOG_SEVERITY_ERROR 2

/* ========== 内部错误处理辅助函数 ========== */

/**
 * 安全释放分配器分配的内存
 * @param api ONNX Runtime API
 * @param allocator 分配器
 * @param ptr 要释放的指针
 */
static void safe_free_ort_alloc(const OrtApi *api, OrtAllocator *allocator, char *ptr)
{
    if (!ptr || !api || !allocator) {
        return;
    }

    OrtStatus *status = api->AllocatorFree(allocator, ptr);
    if (status) {
        api->ReleaseStatus(status);
    }
}

/**
 * @brief 检查 OrtStatus 并处理错误（无资源清理）
 * @param api ONNX Runtime API
 * @param status OrtStatus 指针
 * @param err_msg 错误描述前缀
 * @return 成功返回 0，失败返回 KADD_ERROR
 */
static inline int check_ort_status_no_cleanup(const OrtApi *api, OrtStatus *status, const char *err_msg)
{
    if (!status) {
        return 0;
    }
    const char *err_msg_detail = (const char *)api->GetErrorMessage(status);
    fprintf(stderr, "[kadd_ml] ERROR: %s: %s\n", err_msg, err_msg_detail);
    api->ReleaseStatus(status);
    return KADD_ERROR;
}

/**
 * @brief 检查 OrtStatus 并处理错误（带 SessionOptions 清理）
 * @param api ONNX Runtime API
 * @param status OrtStatus 指针
 * @param options 要清理的 OrtSessionOptions
 * @param err_msg 错误描述前缀
 * @return 成功返回 0，失败返回 KADD_ERROR
 */
static inline int check_ort_status_with_options_cleanup(const OrtApi *api, OrtStatus *status,
                                                       OrtSessionOptions *options, const char *err_msg)
{
    if (!status) {
        return 0;
    }
    const char *err_msg_detail = (const char *)api->GetErrorMessage(status);
    fprintf(stderr, "[kadd_ml] ERROR: %s: %s\n", err_msg, err_msg_detail);
    api->ReleaseStatus(status);
    if (options) {
        api->ReleaseSessionOptions(options);
    }
    return KADD_ERROR;
}

/* ========== 重复代码消除辅助函数 ========== */

/**
 * @brief 检查函数调用返回值并打印错误
 * @param ret 函数返回值
 * @param error_msg 错误信息
 * @return KADD_SUCCESS 或 KADD_ERROR
 */
static inline int check_call(int ret, const char *error_msg)
{
    if (ret != KADD_SUCCESS) {
        fprintf(stderr, "[kadd_ml] ERROR: %s\n", error_msg);
        return KADD_ERROR;
    }
    return KADD_SUCCESS;
}

/**
 * @brief 检查 OrtStatus 并处理错误（简单版，仅释放 Status）
 * @param api ONNX Runtime API
 * @param status OrtStatus 指针
 * @param error_msg 错误信息前缀
 * @return KADD_SUCCESS 或 KADD_ERROR
 */
static inline int check_ort_status_simple(const OrtApi *api, OrtStatus *status, const char *error_msg)
{
    if (status) {
        const char *err_msg = api->GetErrorMessage ? (const char *)api->GetErrorMessage(status) : "unknown";
        fprintf(stderr, "[kadd_ml] ERROR: %s: %s\n", error_msg, err_msg);
        api->ReleaseStatus(status);
        return KADD_ERROR;
    }
    return KADD_SUCCESS;
}

/* ========== 内部函数 ========== */
static int init_ort_api(kadd_model_handle_t *handle)
{
    if (!g_ort_api) {
        const OrtApiBase *api_base = OrtGetApiBase();
        if (!api_base) {
            fprintf(stderr, "[kadd_ml] ERROR: Failed to get ONNX Runtime API base\n");
            return KADD_ERROR;
        }

        // ONNX Runtime 1.16+ 支持 API 版本 15-17, 1.20+ 支持 API 版本 18-20
        // 从编译时最高版本向下回退，找到运行时支持的最高版
        for (int version = ORT_API_VERSION; version >= KADD_ORT_API_MIN_VERSION; version--) {
            g_ort_api = api_base->GetApi(version);
            if (g_ort_api) {
                break;
            }
        }

        if (!g_ort_api) {
            fprintf(stderr, "[kadd_ml] ERROR: No compatible ONNX Runtime API version found (required: %d-%d)\n",
                    KADD_ORT_API_MIN_VERSION, ORT_API_VERSION);
            return KADD_ERROR;
        }
    }
    handle->api = g_ort_api;
    return KADD_SUCCESS;
}

static int init_ort_env(kadd_model_handle_t *handle)
{
    // 验证 API 结构的关键函数指针不为 NULL
    if (!handle->api || !handle->api->CreateEnv || !handle->api->ReleaseStatus) {
        fprintf(stderr, "[kadd_ml] ERROR: Invalid ONNX Runtime API structure (missing required functions)\n");
        return KADD_ERROR;
    }

    if (!g_ort_env) {
        OrtStatus *status = handle->api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "inference", &g_ort_env);
        if (check_ort_status_simple(handle->api, status, "Failed to create ONNX environment") != KADD_SUCCESS) {
            return KADD_ERROR;
        }
    }
    g_ort_env_ref_count++;  /* 增加引用计数 */
    handle->env = g_ort_env;
    return KADD_SUCCESS;
}

static int config_session_options(const OrtApi *api, OrtSessionOptions **options_out)
{
    OrtSessionOptions *options = NULL;
    OrtStatus *status = api->CreateSessionOptions(&options);

    if (status || !options) {
        if (status) {
            const char *err_msg = (const char *)api->GetErrorMessage(status);
            fprintf(stderr, "[kadd_ml] ERROR: Failed to create session options: %s\n", err_msg);
            api->ReleaseStatus(status);
        } else {
            fprintf(stderr, "[kadd_ml] ERROR: Session options is NULL without error status\n");
        }
        return KADD_ERROR;
    }

    status = api->SetSessionExecutionMode(options, ORT_SEQUENTIAL);
    if (check_ort_status_with_options_cleanup(api, status, options,
                                              "Failed to set execution mode") != KADD_SUCCESS) {
        return KADD_ERROR;
    }

    status = api->SetSessionGraphOptimizationLevel(options, ORT_ENABLE_ALL);
    if (check_ort_status_with_options_cleanup(api, status, options,
                                              "Failed to set optimization level") != KADD_SUCCESS) {
        return KADD_ERROR;
    }

    // 设置日志严重级别为 ERROR ，抑制 ORT 内部 warning（如 API version 不匹配提示）
    status = api->SetSessionLogSeverityLevel(options, KADD_ORT_LOG_SEVERITY_ERROR);
    if (check_ort_status_with_options_cleanup(api, status, options,
                                              "Failed to set log severity level") != KADD_SUCCESS) {
        return KADD_ERROR;
    }

    *options_out = options;
    return KADD_SUCCESS;
}

static int create_ort_session(kadd_model_handle_t *handle, const char *model_path)
{
    OrtSessionOptions *options = NULL;
    if (config_session_options(handle->api, &options) != 0) {
        fprintf(stderr, "[kadd_ml] ERROR: Failed to configure session options for model: %s\n", model_path);
        return KADD_ERROR;
    }

    OrtStatus *status = handle->api->CreateSession(handle->env, model_path, options, &handle->session);
    handle->api->ReleaseSessionOptions(options);

    if (check_ort_status_simple(handle->api, status, "Failed to create session for model") != KADD_SUCCESS) {
        return KADD_ERROR;
    }

    status = handle->api->CreateMemoryInfo("Cpu", OrtArenaAllocator, 0, OrtMemTypeDefault, &handle->memory_info);
    if (check_ort_status_no_cleanup(handle->api, status, "Failed to create memory info") != KADD_SUCCESS) {
        return KADD_ERROR;
    }

    return KADD_SUCCESS;
}

/* IO 名称获取回调类型定义 */
typedef OrtStatus *(*NameCountFunc)(const OrtSession *, size_t *);
typedef OrtStatus *(*NameGetFunc)(const OrtSession *, size_t, OrtAllocator *, char **);

/**
 * 获取 ONNX Runtime allocator
 * @param api ONNX Runtime API
 * @param allocator_out 输出 allocator 指针
 * @return 成功返回 0,失败返回 KADD_ERROR
 */
static int get_io_allocator(const OrtApi *api, OrtAllocator **allocator_out)
{
    OrtAllocator *allocator = NULL;
    OrtStatus *status = api->GetAllocatorWithDefaultOptions(&allocator);

    if (status) {
        const char *err_msg = api->GetErrorMessage ? (const char *)api->GetErrorMessage(status) : "unknown";
        fprintf(stderr, "[kadd_ml] ERROR: Failed to get IO allocator: %s\n", err_msg);
        if (api->ReleaseStatus) {
            api->ReleaseStatus(status);
        }
        return KADD_ERROR;
    }

    if (!allocator) {
        fprintf(stderr, "[kadd_ml] ERROR: Allocator is NULL after GetAllocatorWithDefaultOptions\n");
        return KADD_ERROR;
    }

    *allocator_out = allocator;
    return KADD_SUCCESS;
}

/**
 * 检查名称是否匹配任一模式
 * @param name 待检查的名称
 * @param patterns 模式数组(NULL 终止)
 * @return 匹配返回 1,否则返回 0
 */
static int is_name_matched(const char *name, const char *patterns[])
{
    for (int p = 0; patterns[p] != NULL; p++) {
        if (strstr(name, patterns[p])) {
            return 1;
        }
    }
    return 0;
}

/**
 * 在 IO 列表中查找匹配的名称
 * @param handle 模型句柄
 * @param count_func 获取数量的函数指针
 * @param get_func 获取名称的函数指针
 * @param patterns 名称匹配模式数组
 * @param first_name_out 输出第一个名称
 * @param matched_name_out 输出匹配的名称
 * @return 找到匹配返回 KADD_SUCCESS,未找到返回 KADD_ERROR
 */
static int try_match_single_io_name(kadd_model_handle_t *handle,
                                     OrtAllocator *allocator,
                                     NameGetFunc get_func,
                                     const char *patterns[],
                                     size_t idx,
                                     char **first_name,
                                     char **matched_name_out)
{
    char *name = NULL;
    OrtStatus *name_status = get_func(handle->session, idx, allocator, &name);

    if (name_status || !name) {
        if (name_status) {
            handle->api->ReleaseStatus(name_status);
        }
        return 0;
    }

    if (*first_name == NULL) {
        *first_name = strdup(name);
        if (!*first_name) {
            fprintf(stderr, "[kadd_ml] ERROR: Failed to duplicate IO name\n");
            safe_free_ort_alloc(handle->api, allocator, name);
            return -1;
        }
    }

    if (is_name_matched(name, patterns)) {
        *matched_name_out = strdup(name);
        safe_free_ort_alloc(handle->api, allocator, name);
        if (!(*matched_name_out)) {
            fprintf(stderr, "[kadd_ml] ERROR: Failed to duplicate matched IO name\n");
            return -1;
        }
        return 1;
    }

    safe_free_ort_alloc(handle->api, allocator, name);
    return 0;
}

static int find_matched_name(kadd_model_handle_t *handle,
                              NameCountFunc count_func,
                              NameGetFunc get_func,
                              const char *patterns[],
                              char **first_name_out,
                              char **matched_name_out)
{
    OrtAllocator *allocator = NULL;
    char *first_name = NULL;
    size_t num_items = 0;

    if (get_io_allocator(handle->api, &allocator) != KADD_SUCCESS) {
        fprintf(stderr, "[kadd_ml] ERROR: Failed to get allocator for IO name detection\n");
        return KADD_ERROR;
    }

    OrtStatus *count_status = count_func(handle->session, &num_items);
    if (check_ort_status_simple(handle->api, count_status, "Failed to get IO count") != KADD_SUCCESS) {
        return KADD_ERROR;
    }

    int result = KADD_ERROR;
    for (size_t i = 0; i < num_items; i++) {
        int match = try_match_single_io_name(handle, allocator, get_func,
                                              patterns, i, &first_name, matched_name_out);
        if (match == 1) {
            result = KADD_SUCCESS;
            goto cleanup;
        }
        if (match < 0) {
            result = KADD_ERROR;
            goto cleanup;
        }
    }

    *first_name_out = first_name;
    return KADD_ERROR;

cleanup:
    if (first_name) {
        free(first_name);
    }
    return result;
}

/**
 * 通用 IO 名称检测函数
 * @param handle 模型句柄
 * @param count_func 获取数量的函数指针
 * @param get_func 获取名称的函数指针
 * @param patterns 名称匹配模式数组(NULL 终止)
 * @param default_name 默认名称
 * @param target_ptr 目标字段指针
 */
static int detect_io_name_generic(kadd_model_handle_t *handle,
                                    NameCountFunc count_func,
                                    NameGetFunc get_func,
                                    const char *patterns[],
                                    const char *default_name,
                                    char **target_ptr)
{
    char *first_name = NULL;
    char *matched_name = NULL;

    // 查找匹配的名称
    int find_result = find_matched_name(handle, count_func, get_func,
                                         patterns, &first_name, &matched_name);
    if (find_result == KADD_SUCCESS && matched_name) {
        // 找到匹配项
        *target_ptr = matched_name;
        if (first_name) {
            free(first_name);
        }
        return KADD_SUCCESS;
    }

    // 使用默认名称或第一个名称（合并自 set_default_name）
    if (first_name) {
        *target_ptr = first_name;
    } else {
        *target_ptr = strdup(default_name);
    }

    if (!(*target_ptr)) {
        fprintf(stderr, "[kadd_ml] ERROR: Failed to allocate memory for name\n");
        return KADD_ERROR;
    }

    return KADD_SUCCESS;
}

static int detect_input_name(kadd_model_handle_t *handle)
{
    const char *patterns[] = {"input", "feature", "data", "float_input", NULL};
    return detect_io_name_generic(handle, handle->api->SessionGetInputCount,
                                  handle->api->SessionGetInputName, patterns,
                                  "float_input", &handle->input_name);
}

static int detect_output_name(kadd_model_handle_t *handle)
{
    const char *patterns[] = {"probabilities", "probability", "output", "prob", "predict", NULL};
    return detect_io_name_generic(handle, handle->api->SessionGetOutputCount,
                                  handle->api->SessionGetOutputName, patterns,
                                  "probabilities", &handle->output_name);
}

static int create_session(kadd_model_handle_t *handle, const char *model_path)
{
    int ret;

    ret = check_call(init_ort_api(handle), "Failed to initialize ONNX Runtime API");
    if (ret != KADD_SUCCESS) return ret;

    ret = check_call(init_ort_env(handle), "Failed to initialize ONNX Runtime environment");
    if (ret != KADD_SUCCESS) return ret;

    ret = check_call(create_ort_session(handle, model_path), "Failed to create ONNX session");
    if (ret != KADD_SUCCESS) return ret;

    ret = check_call(detect_input_name(handle), "Failed to detect input name");
    if (ret != KADD_SUCCESS) return ret;

    ret = check_call(detect_output_name(handle), "Failed to detect output name");
    if (ret != KADD_SUCCESS) return ret;

    return KADD_SUCCESS;
}

/* ========== 公共接口 ========== */
int kadd_model_load(kadd_model_handle_t **handle, const char *model_path, int feature_dim)
{
    if (!handle || !model_path) {
        fprintf(stderr, "[kadd_ml] ERROR: Invalid parameters (handle=%s, model_path=%s)\n",
                (handle ? "valid" : "NULL"), (model_path ? model_path : "NULL"));
        return KADD_ERROR_NULL_PTR;
    }
    if (feature_dim <= 0) {
        fprintf(stderr, "[kadd_ml] ERROR: Invalid feature_dim: %d (must be > 0)\n", feature_dim);
        return KADD_ERROR_INVALID_PARAM;
    }
    kadd_model_handle_t *h = (kadd_model_handle_t *)calloc(1, sizeof(kadd_model_handle_t));
    if (!h) {
        fprintf(stderr, "[kadd_ml] ERROR: Failed to allocate memory for model handle (feature_dim=%d)\n", feature_dim);
        return KADD_ERROR_OUT_OF_MEMORY;
    }

    h->feature_dim = feature_dim;

    if (create_session(h, model_path) != KADD_SUCCESS) {
        fprintf(stderr, "[kadd_ml] ERROR: Failed to create session for model: %s (feature_dim=%d)\n",
                model_path, feature_dim);
        free(h);
        return KADD_ERROR_MODEL_LOAD_FAILED;
    }

    *handle = h;

    // warmup: 用全零特征做一次 dummy 推理，让 ONNX Runtime 完成内核选择和内存池初始化
    float *warmup_features;
    warmup_features = (float *)calloc(feature_dim, sizeof(float));
    if (warmup_features) {
        float warmup_prob = 0.0f;
        int warmup_ret;
        warmup_ret = kadd_model_predict(h, warmup_features, &warmup_prob);
        free(warmup_features);
        if (warmup_ret != KADD_SUCCESS) {
            fprintf(stderr, "[kadd_ml] ERROR: Warmup inference failed for model=%s (feature_dim=%d)\n",
                    model_path, feature_dim);
            kadd_model_free(h);
            return KADD_ERROR_MODEL_LOAD_FAILED;
        }
    }

    return KADD_SUCCESS;
}

int kadd_model_predict(kadd_model_handle_t *handle, const float *features, float *probability)
{
    if (!handle || !features || !probability) {
        fprintf(stderr, "[kadd_ml] ERROR: Invalid parameters (handle=%s, features=%s, probability=%s)\n",
                (handle ? "valid" : "NULL"), (features ? "valid" : "NULL"), (probability ? "valid" : "NULL"));
        return KADD_ERROR_NULL_PTR;
    }
    int64_t shape[] = {1, handle->feature_dim};
    OrtValue *input = NULL;
    OrtStatus *status = handle->api->CreateTensorWithDataAsOrtValue(
        handle->memory_info, (void *)features, handle->feature_dim * sizeof(float),
        shape, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input);

    if (check_ort_status_simple(handle->api, status, "Failed to create input tensor") != KADD_SUCCESS) {
        return KADD_ERROR_INFERENCE_FAILED;
    }

    const char *input_names[] = {handle->input_name};
    const char *output_names[] = {handle->output_name};
    OrtValue *output = NULL;

    OrtValue *inputs[] = {input};
    const OrtValue *inputs_const[] = {input};
    status = handle->api->Run(handle->session, NULL, input_names, inputs_const, 1, output_names, 1, &output);
    if (status) {
        const char *err_msg = handle->api->GetErrorMessage ? (const char *)handle->api->GetErrorMessage(status) : "unknown";
        fprintf(stderr, "[kadd_ml] ERROR: Run failed (input='%s', output='%s'): %s\n",
                handle->input_name, handle->output_name, err_msg);
        handle->api->ReleaseValue(input);
        handle->api->ReleaseStatus(status);
        return KADD_ERROR_INFERENCE_FAILED;
    }
    handle->api->ReleaseValue(input);

    /* 直接获取数据 */
    float *data = NULL;
    OrtStatus *data_status = handle->api->GetTensorMutableData(output, (void **)&data);
    if (data_status) {
        const char *err_msg = handle->api->GetErrorMessage ? (const char *)handle->api->GetErrorMessage(data_status) : "unknown";
        fprintf(stderr, "[kadd_ml] ERROR: Failed to get tensor data: %s\n", err_msg);
        handle->api->ReleaseStatus(data_status);
        handle->api->ReleaseValue(output);
        return KADD_ERROR_INFERENCE_FAILED;
    }
    if (!data) {
        fprintf(stderr, "[kadd_ml] ERROR: Got NULL tensor data\n");
        handle->api->ReleaseValue(output);
        return KADD_ERROR_INFERENCE_FAILED;
    }

    /*
     * ONNX 模型输出格式: (batch_size, 2)
     * data[0] = 负类概率, data[1] = 正类概率
     * 对于形状 (1, 2): data[0][0], data[0][1] 在内存中连续存储
     * 所以 data[1] 就是正类概率
     */

    *probability = data[1];

    handle->api->ReleaseValue(output);
    return KADD_SUCCESS;
}

static int infer_single_batch(kadd_model_handle_t *handle, const float *batch_features,
                              int current_batch, float *prob_out)
{
    int64_t shape[] = {current_batch, handle->feature_dim};
    OrtValue *input = NULL;

    OrtStatus *status = handle->api->CreateTensorWithDataAsOrtValue(
        handle->memory_info,
        (void *)batch_features,
        (size_t)current_batch * handle->feature_dim * sizeof(float),
        shape, 2,
        ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
        &input
    );
    if (check_ort_status_simple(handle->api, status, "Batch inference failed to create input tensor") != KADD_SUCCESS) {
        return KADD_ERROR_INFERENCE_FAILED;
    }

    // 执行推理
    const char *input_names[] = {handle->input_name};
    const char *output_names[] = {handle->output_name};
    OrtValue *output = NULL;

    OrtValue *inputs[] = {input};
    const OrtValue *inputs_const[] = {input};
    status = handle->api->Run(handle->session, NULL, input_names, inputs_const, 1, output_names, 1, &output);
    if (status) {
        const char *err_msg = handle->api->GetErrorMessage ? (const char *)handle->api->GetErrorMessage(status) : "unknown";
        fprintf(stderr, "[kadd_ml] ERROR: Batch run failed (batch=%d, input='%s', output='%s'): %s\n",
                current_batch, handle->input_name, handle->output_name, err_msg);
        handle->api->ReleaseValue(input);
        handle->api->ReleaseStatus(status);
        return KADD_ERROR_INFERENCE_FAILED;
    }
    handle->api->ReleaseValue(input);

    // 获取输出数据
    float *data = NULL;
    status = handle->api->GetTensorMutableData(output, (void **)&data);
    if (check_ort_status_simple(handle->api, status, "Batch inference failed to get tensor data") != KADD_SUCCESS) {
        handle->api->ReleaseValue(output);
        return KADD_ERROR_INFERENCE_FAILED;
    }

    // 假设输出形状为 [current_batch, 2]（每个样本输出2个值）
    for (int j = 0; j < current_batch; j++) {
        prob_out[j] = data[j * KADD_MODEL_OUTPUT_CLASS_NUM + 1];
    }

    handle->api->ReleaseValue(output);
    return KADD_SUCCESS;
}

int kadd_model_predict_batch(kadd_model_handle_t *handle, const float *features, int n,
                             float *probabilities, int batch_size)
{
    if (!handle || !features || !probabilities) {
        fprintf(stderr, "[kadd_ml] ERROR: Invalid parameters (handle=%s, features=%s, probabilities=%s)\n",
                (handle ? "valid" : "NULL"), (features ? "valid" : "NULL"), (probabilities ? "valid" : "NULL"));
        return KADD_ERROR_NULL_PTR;
    }
    if (n <= 0 || batch_size <= 0) {
        fprintf(stderr,
                "[kadd_ml] ERROR: Invalid batch parameters (n=%d, batch_size=%d), both must be > 0\n",
                n, batch_size);
        return KADD_ERROR_INVALID_PARAM;
    }
    if (n > KADD_ML_MAX_SAMPLE_COUNT) {
        fprintf(stderr, "[kadd_ml] ERROR: Sample count %d exceeds limit %d\n",
                n, KADD_ML_MAX_SAMPLE_COUNT);
        return KADD_ERROR_INVALID_PARAM;
    }
    if (handle->feature_dim > KADD_ML_DEFAULT_MAX_OUTPUT_DIM) {
        fprintf(stderr,
                "[kadd_ml] ERROR: Feature dim %d exceeds limit %d\n",
                handle->feature_dim, KADD_ML_DEFAULT_MAX_OUTPUT_DIM);
        return KADD_ERROR_INVALID_PARAM;
    }

    int effective_batch = batch_size;
    if (effective_batch <= 0 || effective_batch > KADD_ML_MAX_BATCH_SIZE) {
        effective_batch = KADD_ML_DEFAULT_BATCH_SIZE;
    }

    // 分块批量处理
    for (int i = 0; i < n; i += effective_batch) {
        int current_batch = (i + effective_batch < n) ? effective_batch : (n - i);
        const float *batch_features = features + (size_t)i * handle->feature_dim;

        if (infer_single_batch(handle, batch_features, current_batch, probabilities + i) != KADD_SUCCESS) {
            fprintf(stderr, "[kadd_ml] ERROR: Batch inference failed at chunk %d (batch=%d, total=%d)\n",
                    i / effective_batch, current_batch, n);
            return KADD_ERROR_INFERENCE_FAILED;
        }
    }

    return KADD_SUCCESS;
}

void kadd_model_free(kadd_model_handle_t *handle)
{
    if (!handle) {
        return;
    }
    if (handle->input_name) {
        free(handle->input_name);
        handle->input_name = NULL;
    }
    if (handle->output_name) {
        free(handle->output_name);
        handle->output_name = NULL;
    }
    // 释放 ONNX Runtime 资源（这些需要 api）
    if (handle->api) {
        if (handle->memory_info) {
            handle->api->ReleaseMemoryInfo(handle->memory_info);
            handle->memory_info = NULL;
        }
        if (handle->session) {
            handle->api->ReleaseSession(handle->session);
            handle->session = NULL;
        }
    }

    // 减少引用计数
    g_ort_env_ref_count--;
    if (g_ort_env_ref_count == 0 && g_ort_env) {
        // 最后一个模型，释放全局环境
        g_ort_api->ReleaseEnv(g_ort_env);
        g_ort_env = NULL;
    }

    free(handle);
}
