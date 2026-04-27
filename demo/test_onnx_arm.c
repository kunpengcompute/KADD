/**
 * @file test_onnx_arm.c
 * @brief ARM + ONNX Runtime 1.16 专用测试程序
 *
 * 用于诊断 ARM 架构上 ONNX Runtime 1.16 的兼容性问题
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

/**
 * @brief 打印系统信息
 */
static void print_system_info(void) {
    printf("系统信息:\n");
    printf("─────────────────────────────────────────────────────────────\n");

#ifdef _WIN32
    printf("  平台: Windows\n");
    SYSTEM_INFO sysInfo;
    GetNativeSystemInfo(&sysInfo);
    printf("  架构: x%d\n", sysInfo.wProcessorArchitecture);
#else
    struct utsname uname_info;
    if (uname(&uname_info) == 0) {
        printf("  系统: %s\n", uname_info.sysname);
        printf("  版本: %s\n", uname_info.version);
        printf("  架构: %s\n", uname_info.machine);
    }

    // 检查是否为 ARM
    #if defined(__aarch64__) || defined(__arm__)
        printf("  编译架构: ARM (%s)\n",
               #ifdef __aarch64__
               "ARM64"
               #else
               "ARM32"
               #endif
              );
    #else
        printf("  编译架构: 非 ARM\n");
    #endif
#endif

    printf("  头文件 ORT_API_VERSION: %d\n", ORT_API_VERSION);
    printf("\n");
}

/**
 * @brief 测试 ONNX Runtime API 版本兼容性
 */
static int test_api_versions(void) {
    printf("API 版本测试:\n");
    printf("─────────────────────────────────────────────────────────────\n");

    const OrtApiBase *api_base = OrtGetApiBase();
    if (!api_base) {
        printf("✗ 无法获取 OrtApiBase\n");
        return -1;
    }
    printf("✓ OrtApiBase 获取成功\n\n");

    // 测试不同 API 版本
    int working_version = 0;
    for (int v = 17; v >= 10; v--) {
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
        return -1;
    }

    printf("\n推荐使用 API 版本: %d\n", working_version);
    return working_version;
}

/**
 * @brief 测试创建环境和会话
 */
static int test_session_creation(const OrtApi *api, const char *model_path) {
    printf("\n会话创建测试:\n");
    printf("─────────────────────────────────────────────────────────────\n");
    printf("模型路径: %s\n", model_path);

    // 创建环境
    OrtEnv *env = NULL;
    OrtStatus *status = api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "test_arm", &env);
    if (status || !env) {
        printf("✗ 创建环境失败\n");
        if (status) {
            const char *msg = api->GetErrorMessage(status);
            printf("  错误: %s\n", msg ? msg : "未知错误");
            api->ReleaseStatus(status);
        }
        return -1;
    }
    printf("✓ 环境创建成功\n");

    // 创建会话选项
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
        return -1;
    }
    printf("✓ 会话选项创建成功\n");

    // 创建会话
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

        printf("\n可能的原因:\n");
        printf("  1. 模型文件损坏或格式不正确\n");
        printf("  2. ONNX Runtime 版本与模型不兼容\n");
        printf("  3. ARM 架构特定的优化问题\n");
        printf("  4. 内存不足\n");

        api->ReleaseSessionOptions(options);
        api->ReleaseEnv(env);
        return -1;
    }
    printf("✓ 会话创建成功\n");

    // 清理
    api->ReleaseSession(session);
    api->ReleaseSessionOptions(options);
    api->ReleaseEnv(env);

    return 0;
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║       ARM + ONNX Runtime 1.16 诊断工具                    ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");

    // 打印系统信息
    print_system_info();

    // 测试 API 版本
    int api_version = test_api_versions();
    if (api_version < 0) {
        printf("\n✗ API 版本测试失败\n");
        return 1;
    }

    // 获取 API
    const OrtApiBase *api_base = OrtGetApiBase();
    const OrtApi *api = api_base->GetApi(api_version);

    // 确定模型路径
    const char *model_path = NULL;
    if (argc > 1) {
        model_path = argv[1];
    } else {
        const char *default_paths[] = {
            "../../assets/model/model_tokens_rf.onnx",
            "../assets/model/model_tokens_rf.onnx",
            "assets/model/model_tokens_rf.onnx",
            NULL
        };
        for (int i = 0; default_paths[i]; i++) {
            FILE *f = fopen(default_paths[i], "rb");
            if (f) {
                fclose(f);
                model_path = default_paths[i];
                break;
            }
        }
    }

    if (!model_path) {
        printf("\n✗ 未找到模型文件\n");
        printf("用法: %s <model_path.onnx>\n", argv[0]);
        return 1;
    }

    // 测试会话创建
    if (test_session_creation(api, model_path) != 0) {
        printf("\n✗ 会话创建测试失败\n");
        return 1;
    }

    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║                   所有测试通过                             ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");

    printf("\n建议:\n");
    printf("  1. ONNX Runtime 1.16 在 ARM 上应该可以正常工作\n");
    printf("  2. 确保使用正确版本的 .so 文件\n");
    printf("  3. 如果仍有问题，尝试使用 CPU 执行提供程序\n");

    return 0;
}
