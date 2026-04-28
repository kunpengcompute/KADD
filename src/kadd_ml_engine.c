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
                fprintf(stderr, "KADD ML: Using ONNX Runtime API version %d\n", version);
                break;
            }
        }

        if (!g_ort_api) {
            fprintf(stderr, "KADD ML: Failed to get ONNX Runtime API (tried versions %d down to 15)\n", ORT_API_VERSION);
            fprintf(stderr, "KADD ML: ONNX Runtime 1.16+ requires API version 15 or higher\n");
            fprintf(stderr, "KADD ML: Please ensure ONNX Runtime library is compatible\n");
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

    // 设置图优化级别
    status = handle->api->SetSessionGraphOptimizationLevel(options, ORT_ENABLE_BASIC);
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
    MultiByteToWideChar(CP_UTF8, 0, model_path, -1, wide_path, len);
    status = handle->api->CreateSession(handle->env, (const char *)wide_path, options, &handle->session);
    free(wide_path);
#else
    status = handle->api->CreateSession(handle->env, model_path, options, &handle->session);
#endif
    handle->api->ReleaseSessionOptions(options);

    if (status) {
        const char *msg = handle->api->GetErrorMessage(status);
        fprintf(stderr, "KADD ML: Failed to create session: %s\n", msg ? msg : "Unknown error");
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

    // 调试: 检查输入特征
    #ifdef KADD_DEBUG_ML
    float max_feat = 0.0f;
    float sum_feat = 0.0f;
    for (int i = 0; i < handle->feature_dim; i++) {
        if (features[i] > max_feat) max_feat = features[i];
        sum_feat += features[i];
    }
    fprintf(stderr, "[KADD_ML_DEBUG] Input features: max=%.2f, sum=%.2f, dim=%d\n",
            max_feat, sum_feat, handle->feature_dim);
    #endif

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

    /* 获取输出张量信息，确认形状 */
    OrtTypeInfo *type_info = NULL;
    OrtTensorTypeAndShapeInfo *tensor_info = NULL;
    size_t output_dim_count = 0;
    int64_t output_shape[4] = {0};

    OrtStatus *type_status = handle->api->GetTypeInfo(output, &type_info);
    if (!type_status && type_info) {
        OrtStatus *tensor_status = handle->api->GetTensorTypeAndShape(type_info, &tensor_info);
        if (!tensor_status && tensor_info) {
            OrtStatus *dim_status = handle->api->GetDimensionsCount(tensor_info, &output_dim_count);
            if (!dim_status && output_dim_count <= 4) {
                OrtStatus *shape_status = handle->api->GetDimensions(tensor_info, output_shape, 4);
                if (shape_status) {
                    handle->api->ReleaseStatus(shape_status);
                }
            } else if (dim_status) {
                handle->api->ReleaseStatus(dim_status);
            }
            handle->api->ReleaseTensorTypeAndShapeInfo(tensor_info);
        } else if (tensor_status) {
            handle->api->ReleaseStatus(tensor_status);
        }
        handle->api->ReleaseTypeInfo(type_info);
    } else if (type_status) {
        handle->api->ReleaseStatus(type_status);
    }

    /*
     * ONNX 模型输出格式: (batch_size, 2)
     * data[0] = 负类概率, data[1] = 正类概率
     * 对于形状 (1, 2): data[0][0], data[0][1] 在内存中连续存储
     * 所以 data[1] 就是正类概率
     */

    // 调试: 打印原始输出
    #ifdef KADD_DEBUG_ML
    fprintf(stderr, "[KADD_ML_DEBUG] Output shape dims: %zu\n", output_dim_count);
    if (output_dim_count >= 2) {
        fprintf(stderr, "[KADD_ML_DEBUG] Output shape: [%ld, %ld]\n", output_shape[0], output_shape[1]);
    }
    fprintf(stderr, "[KADD_ML_DEBUG] Raw output: data[0]=%.6f, data[1]=%.6f\n", data[0], data[1]);
    #endif

    *probability = data[1];

    // 调试: 打印最终结果
    #ifdef KADD_DEBUG_ML
    fprintf(stderr, "[KADD_ML_DEBUG] Final probability: %.6f\n", *probability);
    #endif

    handle->api->ReleaseValue(output);
    return 0;
}

int kadd_model_predict_batch(kadd_model_handle_t *handle, const float *features, int n, float *probabilities, int batch_size) {
    if (!handle || !features || !probabilities || n <= 0) return -1;

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
        status = handle->api->Run(handle->session, NULL, input_names, inputs, 1, output_names, 1, &output);
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
