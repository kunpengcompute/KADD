/**
 * @file test_onnx_20.c
 * @brief ONNX Runtime 1.20+ 兼容性测试
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/utsname.h>
#endif

// ONNX Runtime C API
#ifdef _WIN32
#ifdef __GNUC__
#undef _WIN32
#include "onnxruntime/onnxruntime_c_api.h"
#define _WIN32
#else
#include "onnxruntime/onnxruntime_c_api.h"
#endif
#else
#include "onnxruntime/onnxruntime_c_api.h"
#endif

int main(int argc, char *argv[]) {
    printf("========================================\n");
    printf("  ONNX Runtime 1.20+ 兼容性测试\n");
    printf("========================================\n\n");

    // 设置环境变量以启用受限 opset 支持
    #ifdef _WIN32
    SetEnvironmentVariableA("ORT_UNRELEASED_OPS", "1");
    SetEnvironmentVariableA("ORT_ENABLE_OPTIONAL_OPS", "1");
    #else
    setenv("ORT_UNRELEASED_OPS", "1", 1);
    setenv("ORT_ENABLE_OPTIONAL_OPS", "1", 1);
    #endif

    printf("系统信息:\n");
    printf("────────────────────────────────────\n");
    #ifdef _WIN32
    printf("  平台: Windows\n");
    #else
    struct utsname uname_info;
    if (uname(&uname_info) == 0) {
        printf("  系统: %s\n", uname_info.sysname);
        printf("  架构: %s\n", uname_info.machine);
    }
    #endif
    printf("  头文件 ORT_API_VERSION: %d\n\n", ORT_API_VERSION);

    // 获取 API 基础对象
    printf("API 版本测试:\n");
    printf("────────────────────────────────────\n");
    const OrtApiBase *api_base = OrtGetApiBase();
    if (!api_base) {
        printf("✗ 无法获取 OrtApiBase\n");
        return 1;
    }
    printf("✓ OrtApiBase 获取成功\n\n");

    // 测试不同 API 版本
    int working_version = 0;
    printf("尝试加载不同 API 版本:\n");
    for (int v = ORT_API_VERSION; v >= 15; v--) {
        const OrtApi *api = api_base->GetApi(v);
        if (api) {
            printf("✓ API 版本 %2d: 可用\n", v);
            if (!working_version) working_version = v;
        } else {
            printf("✗ API 版本 %2d: 不可用\n", v);
        }
    }

    if (!working_version) {
        printf("\n✗ 错误: 没有找到可用的 API 版本\n");
        return 1;
    }

    printf("\n使用 API 版本: %d\n", working_version);

    // 获取工作 API
    const OrtApi *api = api_base->GetApi(working_version);

    // 创建环境
    // 注意：ONNX Runtime 1.20+ 需要 CreateEnvWithGlobalThreadPools
    // 但由于我们不使用 DisablePerSessionThreads，所以可以直接使用 CreateEnv
    printf("\n创建环境:\n");
    printf("────────────────────────────────────\n");
    OrtEnv *env = NULL;
    OrtStatus *status = api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "test_20", &env);

    if (status || !env) {
        printf("✗ 创建环境失败\n");
        if (status) {
            const char *msg = api->GetErrorMessage(status);
            printf("  错误: %s\n", msg ? msg : "未知错误");
            api->ReleaseStatus(status);
        }
        return 1;
    }
    printf("✓ 环境创建成功\n");

    // 创建会话选项
    printf("\n创建会话选项:\n");
    printf("────────────────────────────────────\n");
    OrtSessionOptions *options = NULL;
    status = api->CreateSessionOptions(&options);
    if (status || !options) {
        printf("✗ 创建会话选项失败\n");
        if (status) {
            const char *msg = api->GetErrorMessage(status);
            printf("  错误: %s\n", msg ? msg : "未知错误");
            api->ReleaseStatus(status);
        }
        api->ReleaseEnv(env);
        return 1;
    }
    printf("✓ 会话选项创建成功\n");

    // 设置会话选项
    api->SetSessionExecutionMode(options, ORT_SEQUENTIAL);
    api->SetSessionGraphOptimizationLevel(options, ORT_ENABLE_BASIC);
    api->AddSessionConfigEntry(options, "session.allow_released_ops_only", "1");
    printf("✓ 会话选项配置完成\n");

    // 如果提供了模型文件，测试加载
    if (argc > 1) {
        const char *model_path = argv[1];
        printf("\n加载模型:\n");
        printf("────────────────────────────────────\n");
        printf("模型路径: %s\n", model_path);

        OrtSession *session = NULL;
        #ifdef _WIN32
        int len = MultiByteToWideChar(CP_UTF8, 0, model_path, -1, NULL, 0);
        wchar_t *wide_path = (wchar_t*)malloc(len * sizeof(wchar_t));
        MultiByteToWideChar(CP_UTF8, 0, model_path, -1, wide_path, len);
        status = api->CreateSession(env, (const char *)wide_path, options, &session);
        free(wide_path);
        #else
        status = api->CreateSession(env, model_path, options, &session);
        #endif

        if (status || !session) {
            printf("✗ 创建会话失败\n");
            if (status) {
                const char *msg = api->GetErrorMessage(status);
                printf("  错误: %s\n", msg ? msg : "未知错误");
                api->ReleaseStatus(status);
            }
            api->ReleaseSessionOptions(options);
            api->ReleaseEnv(env);
            return 1;
        }
        printf("✓ 会话创建成功\n");

        // 获取输入输出信息
        OrtAllocator *allocator = NULL;
        api->GetAllocatorWithDefaultOptions(&allocator);

        size_t num_inputs = 0;
        api->SessionGetInputCount(session, &num_inputs);
        printf("  输入数量: %zu\n", num_inputs);

        size_t num_outputs = 0;
        api->SessionGetOutputCount(session, &num_outputs);
        printf("  输出数量: %zu\n", num_outputs);

        // 清理
        #ifndef _WIN32
        api->ReleaseSession(session);
        #endif
    }

    // 清理
    api->ReleaseSessionOptions(options);
    api->ReleaseEnv(env);

    printf("\n========================================\n");
    printf("  测试完成\n");
    printf("========================================\n");

    return 0;
}
