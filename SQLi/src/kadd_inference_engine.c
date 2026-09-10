/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * @file kadd_inference_engine.c
 * @brief ONNX 推理引擎实现
 *
 * 支持平台：Linux (x86_64, aarch64), Windows (x64)
 * 依赖：ONNX Runtime 1.28.0+ (API v28+)
 */
#include "kadd_inference_engine.h"
#include "kadd_config_loader.h"
#include "kadd_errors.h"
#include "onnxruntime_c_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
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
    int output_dim;
};

static OrtEnv *g_ort_env = NULL;
static const OrtApi *g_ort_api = NULL;
static int g_ort_env_ref_count = 0;

/* [FIX REPORT-2026-0000008] Cross-platform mutex for thread-safe init/free */
#ifdef _WIN32
#include <windows.h>
static CRITICAL_SECTION g_ort_mutex;
static volatile LONG g_ort_mutex_init_done = 0;
static void ensure_mutex_init(void) {
    if (InterlockedCompareExchange(&g_ort_mutex_init_done, 1, 0) == 0) {
        InitializeCriticalSection(&g_ort_mutex);
    }
}
#define LOCK_ORT()   do { ensure_mutex_init(); EnterCriticalSection(&g_ort_mutex); } while(0)
#define UNLOCK_ORT() LeaveCriticalSection(&g_ort_mutex)
#else
#include <pthread.h>
static pthread_mutex_t g_ort_mutex = PTHREAD_MUTEX_INITIALIZER;
#define LOCK_ORT()   pthread_mutex_lock(&g_ort_mutex)
#define UNLOCK_ORT() pthread_mutex_unlock(&g_ort_mutex)
#endif

/* ONNX Runtime 最小 API 版本（v28 对应 ONNX Runtime 1.28.0+） */
#define ORT_MIN_API_VERSION 28

#if ORT_API_VERSION < ORT_MIN_API_VERSION
#error "ONNX Runtime 1.28.0 or newer (C API v28+) is required"
#endif

/* 张量最大维度数 */
#define MAX_TENSOR_DIMS 4

static int tensor_dim_count_is_supported(size_t dim_count)
{
    return dim_count >= 2U && dim_count <= (size_t)MAX_TENSOR_DIMS;
}

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

        // ONNX Runtime 1.28.0+ 支持 API 版本 28+
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
    LOCK_ORT();  /* [FIX REPORT-2026-0000008] Thread-safe ref counting */
    if (!g_ort_env) {
        OrtStatus *status = e->api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "inference", &g_ort_env);
        if (check_ort_status(e->api, status, "CreateEnv")) {
            UNLOCK_ORT();
            return -1;
        }
    }
    g_ort_env_ref_count++;
    e->env = g_ort_env;
    UNLOCK_ORT();
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

    /* [HARDEN white_box_test_047] 指针返回值必须先校验再使用：
     * 1) strdup(NULL) 为未定义行为，ORT 虽约定 status 成功时 name 非空，仍加防御；
     * 2) strdup 堆耗尽时返回 NULL，记录日志后返回 NULL，由唯一调用方
     *    detect_io_names 判空兜底（返回 -1 → 加载 fail-closed），契约不再依赖隐式约定。 */
    if (name == NULL) {
        return NULL;
    }
    char *result = strdup(name);
    if (result == NULL) {
        fprintf(stderr, "[kadd_inference] ERROR: strdup failed for %s name\n",
                is_output ? "output" : "input");
    }
    /* AllocatorFree 带 warn_unused_result 属性，(void) 强转无法消除告警，需接收并释放返回状态 */
    OrtStatus *free_status = e->api->AllocatorFree(allocator, name);
    if (free_status) {
        (void)e->api->ReleaseStatus(free_status);
    }
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

    status = e->api->CastTypeInfoToTensorInfo(type_info, &tensor_info);
    if (status) {
        goto cleanup;
    }

    status = e->api->GetDimensionsCount(tensor_info, &dim_count);
    if (status || !tensor_dim_count_is_supported(dim_count)) {
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

/* ========== 获取模型输出维度 ========== */
static void get_model_output_dim(kadd_inference_engine_t *e)
{
    OrtTypeInfo *out_type_info = NULL;
    const OrtTensorTypeAndShapeInfo *out_tensor_info = NULL;
    OrtStatus *out_status = NULL;
    size_t out_dim_count = 0;
    int64_t out_dims[MAX_TENSOR_DIMS] = {0};

    e->output_dim = 2;  /* 默认 fallback */

    out_status = e->api->SessionGetOutputTypeInfo(e->session, 0, &out_type_info);
    if (out_status) {
        (void)e->api->ReleaseStatus(out_status);
        return;
    }

    out_status = e->api->CastTypeInfoToTensorInfo(out_type_info, &out_tensor_info);
    if (out_status) {
        (void)e->api->ReleaseStatus(out_status);
        (void)e->api->ReleaseTypeInfo(out_type_info);
        return;
    }

    out_status = e->api->GetDimensionsCount(out_tensor_info, &out_dim_count);
    if (!out_status && tensor_dim_count_is_supported(out_dim_count)) {
        out_status = e->api->GetDimensions(out_tensor_info, out_dims, out_dim_count);
        if (!out_status && out_dims[1] > 0) {
            e->output_dim = (int)out_dims[1];
        }
    }
    if (out_status) { (void)e->api->ReleaseStatus(out_status); }
    (void)e->api->ReleaseTypeInfo(out_type_info);
}

/*
 * ONNX Runtime 线程数配置
 *
 * 背景：ORT 默认把 intra_op 线程数取为物理核数。但本引擎的 ONNX 模型很小
 * （单次 Run 的 batch <= 64、feature_dim = 300），计算量远小于线程派发与同步
 * 开销，默认配置下绝大部分时间耗在 ORT 内部的线程调度而非算子计算上，
 * 实测 CNN 批量吞吐仅约 1.6k samples/sec。
 *
 * 经 intra_op 1-32 全量扫描 + 算子级 profiling 验证：intra=8 / inter=1 最优，
 * CNN 批量吞吐提升至约 13.6k-17.7k samples/sec（9-12 倍）。因此在此显式固定，
 * 不再依赖 ORT 各版本的默认值（该默认值曾随小版本变化而显著影响性能）。
 *
 * 允许用环境变量覆盖，便于不同核数机型调优而无需重新编译：
 *   KADD_ORT_INTRA_OP_THREADS  默认 8
 *   KADD_ORT_INTER_OP_THREADS  默认 1
 * 取值非法时忽略并回退默认值——线程数属于性能调优项，绝不因配置错误导致模型加载失败。
 */
#define KADD_ORT_DEFAULT_INTRA_OP_THREADS 8
#define KADD_ORT_DEFAULT_INTER_OP_THREADS 1
#define KADD_ORT_INTRA_OP_ENV             "KADD_ORT_INTRA_OP_THREADS"
#define KADD_ORT_INTER_OP_ENV             "KADD_ORT_INTER_OP_THREADS"

static int ort_thread_count_from_env(const char *name, int fallback)
{
    const char *value = getenv(name);
    if (value == NULL || value[0] == '\0') {
        return fallback;
    }

    char *end = NULL;
    const long parsed = strtol(value, &end, 10);
    /* 只接受纯十进制整数（不允许前后空白与任何后缀），文法与文档保持一致 */
    if (value[0] < '0' || value[0] > '9' ||
        end == value || end == NULL || *end != '\0' ||
        parsed < 1L || parsed > 1024L) {
        fprintf(stderr, "[kadd_inference] WARN: 忽略非法的 %s=\"%s\"，回退默认值 %d\n",
                name, value, fallback);
        return fallback;
    }
    return (int)parsed;
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

    /* 线程数：设置失败不视为致命错误，仅告警并沿用 ORT 默认值，
     * 避免因个别 ORT 版本不接受该配置而导致模型完全不可用。 */
    {
        OrtStatus *thread_status = e->api->SetIntraOpNumThreads(
            options,
            ort_thread_count_from_env(KADD_ORT_INTRA_OP_ENV,
                                      KADD_ORT_DEFAULT_INTRA_OP_THREADS));
        if (thread_status) {
            fprintf(stderr, "[kadd_inference] WARN: SetIntraOpNumThreads 失败: %s\n",
                    e->api->GetErrorMessage(thread_status));
            (void)e->api->ReleaseStatus(thread_status);
        }

        thread_status = e->api->SetInterOpNumThreads(
            options,
            ort_thread_count_from_env(KADD_ORT_INTER_OP_ENV,
                                      KADD_ORT_DEFAULT_INTER_OP_THREADS));
        if (thread_status) {
            fprintf(stderr, "[kadd_inference] WARN: SetInterOpNumThreads 失败: %s\n",
                    e->api->GetErrorMessage(thread_status));
            (void)e->api->ReleaseStatus(thread_status);
        }
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

    /* 分步初始化：init_ort_api 不涉及全局引用计数，失败可直接释放 */
    if (init_ort_api(e) != 0 || init_ort_env(e) != 0) {
        free(e);
        return KADD_ERROR_MODEL_LOAD_FAILED;
    }

    if (create_ort_session(e, model_path) != 0 || detect_io_names(e) != 0 || get_model_input_dim(e) != 0) {
        kadd_inference_engine_free(e);
        return KADD_ERROR_MODEL_LOAD_FAILED;
    }
    get_model_output_dim(e);  /* 非致命，失败则使用默认值 */

    /* warmup */
    float *warmup_features = (float *)calloc(e->feature_dim, sizeof(float));
    if (warmup_features) {
        float warmup_prob = 0.0f;
        int warmup_ret = kadd_inference_predict(e, warmup_features, &warmup_prob);
        free(warmup_features);
        if (warmup_ret != KADD_SUCCESS) {
            fprintf(stderr, "[kadd_inference] ERROR: Warmup inference failed (feature_dim=%d)\n",
                    e->feature_dim);
            kadd_inference_engine_free(e);
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

    int eff_batch = (batch_size <= 0 || batch_size > KADD_INFERENCE_MAX_BATCH_SIZE)
                    ? KADD_INFERENCE_DEFAULT_BATCH_SIZE : batch_size;

    for (int i = 0; i < n; i += eff_batch) {
        int cur = (i + eff_batch < n) ? eff_batch : (n - i);

        /* [FIX REPORT-2026-0000014] Integer overflow checks */
        if (engine->feature_dim <= 0 || engine->feature_dim > KADD_INFERENCE_MAX_FEATURE_DIM) {
            fprintf(stderr, "[kadd_inference] ERROR: Invalid feature_dim=%d\n", engine->feature_dim);
            return KADD_ERROR_INVALID_PARAM;
        }
        if ((size_t)cur > SIZE_MAX / (size_t)engine->feature_dim) {
            fprintf(stderr, "[kadd_inference] ERROR: cur*feature_dim overflow (cur=%d, dim=%d)\n",
                    cur, engine->feature_dim);
            return KADD_ERROR_INVALID_PARAM;
        }
        size_t total_elems = (size_t)cur * (size_t)engine->feature_dim;
        for (size_t j = 0; j < total_elems; j++) {
            if (!isfinite(features[(size_t)i * engine->feature_dim + j])) {
                return KADD_ERROR_INVALID_PARAM;
            }
        }

        int64_t shape[] = {cur, engine->feature_dim};
        OrtValue *input = NULL;
        OrtStatus *status = engine->api->CreateTensorWithDataAsOrtValue(
            engine->memory_info, (void *)(features + (size_t)i * engine->feature_dim),
            total_elems * sizeof(float),
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

        /* [FIX REPORT-2026-0000011] Validate output_dim for safe column access */
        if (engine->output_dim < 2) {
            fprintf(stderr, "[kadd_inference] ERROR: output_dim=%d < 2, cannot access column 1\n",
                    engine->output_dim);
            (void)engine->api->ReleaseValue(output);
            return KADD_ERROR_INFERENCE_FAILED;
        }
        for (int j = 0; j < cur; j++) {
            probabilities[i + j] = data[j * engine->output_dim + 1];
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

    LOCK_ORT();  /* [FIX REPORT-2026-0000008] Thread-safe ref counting */
    g_ort_env_ref_count--;
    if (g_ort_env_ref_count == 0 && g_ort_env) {
        (void)g_ort_api->ReleaseEnv(g_ort_env);
        g_ort_env = NULL;
    }
    UNLOCK_ORT();

    free(engine);
}

