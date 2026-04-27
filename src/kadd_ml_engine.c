/**
 * @file inference_engine.c
 * @brief ONNX 推理引擎实现
 */

#include "kadd_ml_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifdef __GNUC__
#undef _WIN32
#define _ONNX_TEMP_WIN32_UNDEF_
#include "onnxruntime/onnxruntime_c_api.h"
#define _WIN32
#else
#include "onnxruntime/onnxruntime_c_api.h"
#endif
#else
#include "onnxruntime/onnxruntime_c_api.h"
#endif

#ifdef _WIN32
#include <windows.h>
#endif

/* ========== 内部结构体 ========== */
struct kadd_model_handle_t {
    OrtSession *session;
    OrtEnv *env;
    OrtMemoryInfo *memory_info;
    const OrtApi *api;
    char *output_name;
    int feature_dim;
};

static OrtEnv *g_ort_env = NULL;
static const OrtApi *g_ort_api = NULL;

/* ========== 内部函数 ========== */
static int create_session(kadd_model_handle_t *handle, const char *model_path) {
    // 获取 API
    if (!g_ort_api) {
        g_ort_api = OrtGetApiBase()->GetApi(ORT_API_VERSION);
        if (!g_ort_api) return -1;
    }
    handle->api = g_ort_api;

    // 创建环境
    if (!g_ort_env) {
        OrtStatus *status = handle->api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "inference", &g_ort_env);
        if (status) {
            handle->api->ReleaseStatus(status);
            return -1;
        }
    }
    handle->env = g_ort_env;

    // 创建会话选项
    OrtSessionOptions *options = NULL;
    OrtStatus *status = handle->api->CreateSessionOptions(&options);
    if (status || !options) {
        if (status) handle->api->ReleaseStatus(status);
        return -1;
    }

    // 创建会话
#ifdef _WIN32
    int len = MultiByteToWideChar(CP_UTF8, 0, model_path, -1, NULL, 0);
    wchar_t *wide_path = (wchar_t*)malloc(len * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, model_path, -1, wide_path, len);
    status = handle->api->CreateSession(handle->env, (const char *)wide_path, options, &handle->session);
    free(wide_path);
#else
    status = handle->api->CreateSession(handle->env, model_path, options, &handle->session);
#endif
    handle->api->ReleaseSessionOptions(options);

    if (status) {
        handle->api->ReleaseStatus(status);
        return -1;
    }

    // 创建内存信息
    status = handle->api->CreateMemoryInfo("Cpu", OrtArenaAllocator, 0, OrtMemTypeDefault, &handle->memory_info);
    if (status) {
        handle->api->ReleaseStatus(status);
        return -1;
    }

    // 检测输出节点名称
    OrtAllocator *allocator = NULL;
    handle->api->GetAllocatorWithDefaultOptions(&allocator);
    if (allocator) {
        size_t num_outputs = 0;
        handle->api->SessionGetOutputCount(handle->session, &num_outputs);
        for (size_t i = 0; i < num_outputs; i++) {
            char *name = NULL;
            if (handle->api->SessionGetOutputName(handle->session, i, allocator, &name) == NULL && name) {
                if (strstr(name, "probability")) {
                    handle->output_name = strdup(name);
                    allocator->Free(allocator, name);
                    break;
                }
                allocator->Free(allocator, name);
            }
        }
    }

    if (!handle->output_name) {
        handle->output_name = strdup("probabilities");
    }

    return 0;
}

/* ========== 公共接口 ========== */
int kadd_model_load(kadd_model_handle_t **handle, const char *model_path, int feature_dim) {
    if (!handle || !model_path || feature_dim <= 0) return -1;

    kadd_model_handle_t *h = (kadd_model_handle_t *)calloc(1, sizeof(kadd_model_handle_t));
    if (!h) return -1;

    h->feature_dim = feature_dim;

    if (create_session(h, model_path) != 0) {
        free(h);
        return -1;
    }

    *handle = h;
    return 0;
}

int kadd_model_predict(kadd_model_handle_t *handle, const float *features, float *probability) {
    if (!handle || !features || !probability) return -1;

    int64_t shape[] = {1, handle->feature_dim};
    OrtValue *input = NULL;
    OrtStatus *status = handle->api->CreateTensorWithDataAsOrtValue(
        handle->memory_info, (void *)features, handle->feature_dim * sizeof(float),
        shape, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input);

    if (status) {
        handle->api->ReleaseStatus(status);
        return -1;
    }

    const char *input_names[] = {"float_input"};
    const char *output_names[] = {handle->output_name};
    OrtValue *output = NULL;

    OrtValue *inputs[] = {input};
    status = handle->api->Run(handle->session, NULL, input_names, inputs, 1, output_names, 1, &output);
    handle->api->ReleaseValue(input);

    if (status) {
        handle->api->ReleaseStatus(status);
        return -1;
    }

    /* 直接获取数据 */
    float *data = NULL;
    OrtStatus *data_status = handle->api->GetTensorMutableData(output, (void **)&data);
    if (data_status || !data) {
        if (data_status) handle->api->ReleaseStatus(data_status);
        handle->api->ReleaseValue(output);
        return -1;
    }

    /* 根据模型输出格式选择正确的概率值 */
    *probability = data[1];  /* 使用第二个值（正类概率） */

    handle->api->ReleaseValue(output);
    return 0;
}

int kadd_model_predict_batch(kadd_model_handle_t *handle, const float *features, int n, float *probabilities, int batch_size) {
    if (!handle || !features || !probabilities || n <= 0) return -1;

    // 如果 batch_size 无效，使用默认值
    if (batch_size <= 0) {
        batch_size = 64;  // 默认批次大小
    }

    // 限制 batch_size 范围，防止过大或过小
    if (batch_size > 1024) {
        batch_size = 1024;  // 最大批次限制
    }
    if (batch_size < 1) {
        batch_size = 1;     // 最小批次限制
    }

    int count = 0;

    // 分块批量处理
    for (int i = 0; i < n; i += batch_size) {
        // 计算当前批次大小
        int current_batch = (i + batch_size < n) ? batch_size : (n - i);

        // 创建形状为 [current_batch, feature_dim] 的 tensor
        int64_t shape[] = {current_batch, handle->feature_dim};
        OrtValue *input = NULL;

        // 指向当前批次的数据
        const float *batch_features = features + i * handle->feature_dim;

        OrtStatus *status = handle->api->CreateTensorWithDataAsOrtValue(
            handle->memory_info,
            (void *)batch_features,
            current_batch * handle->feature_dim * sizeof(float),
            shape, 2,
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
            &input
        );

        if (status) {
            handle->api->ReleaseStatus(status);
            // 当前批次失败，跳过继续处理下一批次
            continue;
        }

        // 执行推理
        const char *input_names[] = {"float_input"};
        const char *output_names[] = {handle->output_name};
        OrtValue *output = NULL;

        OrtValue *inputs[] = {input};
        status = handle->api->Run(handle->session, NULL, input_names, inputs, 1, output_names, 1, &output);
        handle->api->ReleaseValue(input);

        if (status) {
            handle->api->ReleaseStatus(status);
            // 当前批次推理失败，跳过继续处理下一批次
            continue;
        }

        // 获取输出数据
        float *data = NULL;
        status = handle->api->GetTensorMutableData(output, (void **)&data);
        if (status) {
            handle->api->ReleaseStatus(status);
            handle->api->ReleaseValue(output);
            // 当前批次获取数据失败，跳过继续处理下一批次
            continue;
        }

        // 将当前批次的结果复制到输出数组
        // 假设输出形状为 [current_batch, 2]（每个样本输出2个值）
        // 我们只需要第二个值（probability）
        int output_stride = 2;  // 根据模型输出调整
        for (int j = 0; j < current_batch; j++) {
            probabilities[i + j] = data[j * output_stride + 1];
        }

        handle->api->ReleaseValue(output);
        count += current_batch;
    }

    return count;
}

void kadd_model_free(kadd_model_handle_t *handle) {
    if (!handle) return;

    if (handle->output_name) free(handle->output_name);
    if (handle->memory_info) handle->api->ReleaseMemoryInfo(handle->memory_info);
    if (handle->session) handle->api->ReleaseSession(handle->session);
    free(handle);
}
