/**
 * @file inference_engine.c
 * @brief ONNX 推理引擎实现
 */

#include "kadd_ml_engine.h"
#include "kadd_errors.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// MinGW 环境需要临时取消 _WIN32 宏避免与 onnxruntime 头文件冲突
#if defined(_WIN32) && defined(__GNUC__)
#undef _WIN32
#define _ONNX_TEMP_WIN32_UNDEF_
#include "onnxruntime/onnxruntime_c_api.h"
#define _WIN32
#else
#include "onnxruntime/onnxruntime_c_api.h"
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <stdlib.h>  // for setenv
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
    // 获取 API - 支持版本回退以兼容不同版本的 ONNX Runtime
    if (!g_ort_api) {
        const OrtApiBase *api_base = OrtGetApiBase();
        if (!api_base) return -1;

        // 尝试从当前版本开始，逐步回退以兼容不同版本的 ONNX Runtime
        // ONNX Runtime 1.16+ 支持 API 版本 15-17
        // ONNX Runtime 1.20+ 支持 API 版本 18-20
        for (int version = ORT_API_VERSION; version >= 15; version--) {
            g_ort_api = api_base->GetApi(version);
            if (g_ort_api) {
                break;
            }
        }

        if (!g_ort_api) {
            return -1;
        }
    }
    handle->api = g_ort_api;

    // 创建环境
    // 注意：ONNX Runtime 1.20+ 需要 CreateEnvWithGlobalThreadPools
    // 但由于我们不使用 DisablePerSessionThreads，所以可以直接使用 CreateEnv
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

    // 设置执行模式为顺序执行（适用于小型模型）
    status = handle->api->SetSessionExecutionMode(options, ORT_SEQUENTIAL);
    if (status) {
        handle->api->ReleaseSessionOptions(options);
        handle->api->ReleaseStatus(status);
        return -1;
    }

    // 设置图优化级别为全优化，让 ONNX Runtime 在 session 创建时完成所有优化
    // 避免首次推理时仍需执行内核选择等延迟操作
    status = handle->api->SetSessionGraphOptimizationLevel(options, ORT_ENABLE_ALL);
    if (status) {
        handle->api->ReleaseSessionOptions(options);
        handle->api->ReleaseStatus(status);
        return -1;
    }

    // 对于 ONNX Runtime 1.20+，设置环境变量以启用受限 opset 支持
    // 注意：这需要在加载会话之前设置环境变量
    #ifdef _WIN32
    SetEnvironmentVariableA("ORT_UNRELEASED_OPS", "1");
    SetEnvironmentVariableA("ORT_ENABLE_OPTIONAL_OPS", "1");
    #else
    setenv("ORT_UNRELEASED_OPS", "1", 1);
    setenv("ORT_ENABLE_OPTIONAL_OPS", "1", 1);
    #endif

    // 创建会话
#ifdef _WIN32
    int len = MultiByteToWideChar(CP_UTF8, 0, model_path, -1, NULL, 0);
    wchar_t *wide_path = (wchar_t*)malloc(len * sizeof(wchar_t));
    if (!wide_path) {
        return KADD_ERROR_OUT_OF_MEMORY;
    }
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
    OrtStatus *alloc_status = handle->api->GetAllocatorWithDefaultOptions(&allocator);
    if (!alloc_status && allocator) {
        size_t num_outputs = 0;
        OrtStatus *count_status = handle->api->SessionGetOutputCount(handle->session, &num_outputs);
        if (!count_status) {
            for (size_t i = 0; i < num_outputs; i++) {
                char *name = NULL;
                OrtStatus *name_status = handle->api->SessionGetOutputName(handle->session, i, allocator, &name);
                if (!name_status && name) {
                    if (strstr(name, "probability")) {
                        handle->output_name = strdup(name);
                        handle->api->AllocatorFree(allocator, name);
                        if (!handle->output_name) {
                            handle->api->ReleaseStatus(count_status);
                            return -1;
                        }
                        break;
                    }
                    handle->api->AllocatorFree(allocator, name);
                } else if (name_status) {
                    handle->api->ReleaseStatus(name_status);
                }
            }
        } else if (count_status) {
            handle->api->ReleaseStatus(count_status);
        }
    } else if (alloc_status) {
        handle->api->ReleaseStatus(alloc_status);
    }

    if (!handle->output_name) {
        handle->output_name = strdup("probabilities");
        if (!handle->output_name) {
            return -1;
        }
    }

    return 0;
}

/* ========== 公共接口 ========== */
int kadd_model_load(kadd_model_handle_t **handle, const char *model_path, int feature_dim) {
    if (!handle || !model_path) return KADD_ERROR_NULL_PTR;
    if (feature_dim <= 0) return KADD_ERROR_INVALID_PARAM;

    kadd_model_handle_t *h = (kadd_model_handle_t *)calloc(1, sizeof(kadd_model_handle_t));
    if (!h) return KADD_ERROR_OUT_OF_MEMORY;

    h->feature_dim = feature_dim;

    if (create_session(h, model_path) != 0) {
        free(h);
        return KADD_ERROR_MODEL_LOAD_FAILED;
    }

    *handle = h;

    // warmup: 用全零特征做一次 dummy 推理，让 ONNX Runtime 完成内核选择和内存池初始化
    // 将首次推理延迟移到模型加载阶段，后续真实推理不再有首次延迟
    float *warmup_features = (float *)calloc(feature_dim, sizeof(float));
    if (warmup_features) {
        float warmup_prob = 0.0f;
        kadd_model_predict(h, warmup_features, &warmup_prob);
        free(warmup_features);
    }

    return 0;
}

int kadd_model_predict(kadd_model_handle_t *handle, const float *features, float *probability) {
    if (!handle || !features || !probability) return KADD_ERROR_NULL_PTR;

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
    const OrtValue *inputs_const[] = {input};
    status = handle->api->Run(handle->session, NULL, input_names, inputs_const, 1, output_names, 1, &output);
    if (status) {
        handle->api->ReleaseValue(input);
        handle->api->ReleaseStatus(status);
        return -1;
    }
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

    /*
     * ONNX 模型输出格式: (batch_size, 2)
     * data[0] = 负类概率, data[1] = 正类概率
     * 对于形状 (1, 2): data[0][0], data[0][1] 在内存中连续存储
     * 所以 data[1] 就是正类概率
     */

    *probability = data[1];

    handle->api->ReleaseValue(output);
    return 0;
}

int kadd_model_predict_batch(kadd_model_handle_t *handle, const float *features, int n, float *probabilities, int batch_size) {
    if (!handle || !features || !probabilities) return KADD_ERROR_NULL_PTR;
    if (n <= 0) return KADD_ERROR_INVALID_PARAM;

    // 限制 batch_size 范围，防止过大或过小
    if (batch_size > 1024) {
        batch_size = 1024;  // 最大批次限制
    }
    if (batch_size <= 1) {
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
        const OrtValue *inputs_const[] = {input};
        status = handle->api->Run(handle->session, NULL, input_names, inputs_const, 1, output_names, 1, &output);
        if (status) {
            handle->api->ReleaseValue(input);
            handle->api->ReleaseStatus(status);
            // 当前批次推理失败，跳过继续处理下一批次
            continue;
        }
        handle->api->ReleaseValue(input);

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
