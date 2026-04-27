/**
 * @file kadd_ml_demo_debug.c
 * @brief ML 推理引擎调试版本 - 显示详细错误信息
 *
 * 专门用于诊断 Linux 上的模型加载问题
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "kadd_ml_engine.h"
#include "tools.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#include <unistd.h>
#endif

/**
 * @brief 检查文件是否存在并可读
 */
static int check_file_accessible(const char *path) {
    if (!path) return 0;

#ifdef _WIN32
    FILE *f = fopen(path, "rb");
    if (f) {
        fclose(f);
        return 1;
    }
    return 0;
#else
    return access(path, R_OK) == 0;
#endif
}

/**
 * @brief 获取详细错误信息
 */
static const char* get_error_string(int error_code) {
    switch (error_code) {
        case 0:  return "成功";
        case -1: return "内存分配失败";
        case -2: return "无效输入参数";
        case -3: return "文件不存在或无法访问";
        case -4: return "ONNX Runtime 初始化失败";
        case -5: return "模型加载失败";
        case -6: return "推理失败";
        default: return "未知错误";
    }
}

int main(int argc, char *argv[])
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║        ML 推理引擎调试演示 (kadd_ml_engine_debug)           ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");

    const int feature_dim = 28;

    // ========== 步骤 0: 环境检查 ==========
    printf("【步骤 0】环境检查\n");
    printf("─────────────────────────────────────────────────────────────\n");

    printf("当前工作目录: ");
    fflush(stdout);
#ifdef _WIN32
    system("cd");
#else
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
    } else {
        printf("无法获取\n");
    }
#endif

    printf("\n检查库文件:\n");
    const char *lib_paths[] = {
        "./libonnxruntime.so",
        "./libkadd_ml_engine.so",
        "../src/libkadd_ml_engine.so",
        NULL
    };

    for (int i = 0; lib_paths[i]; i++) {
        int exists = check_file_accessible(lib_paths[i]);
        printf("  %s: %s\n", lib_paths[i], exists ? "✓ 存在" : "✗ 不存在");
    }

    // ========== 步骤 1: 查找模型文件 ==========
    printf("\n【步骤 1】查找模型文件\n");
    printf("─────────────────────────────────────────────────────────────\n");

    const char *model_paths[] = {
        (argc > 1) ? argv[1] : NULL,
        "../../assets/model/model_tokens_rf.onnx",
        "../assets/model/model_tokens_rf.onnx",
        "assets/model/model_tokens_rf.onnx",
        "/usr/local/share/kadd/model_tokens_rf.onnx",
        NULL
    };

    const char *model_path = NULL;

    // 如果命令行指定了路径，使用它
    if (argc > 1) {
        model_path = argv[1];
        printf("使用命令行指定的路径: %s\n", model_path);
    } else {
        // 尝试找到存在的模型文件
        for (int i = 1; model_paths[i] != NULL; i++) {
            printf("检查: %s ... ", model_paths[i]);
            fflush(stdout);

            if (check_file_accessible(model_paths[i])) {
                printf("✓ 找到\n");
                model_path = model_paths[i];
                break;
            } else {
                printf("✗ 不存在\n");
            }
        }
    }

    if (!model_path) {
        printf("\n✗ 错误: 未找到模型文件\n");
        printf("\n请尝试以下方法之一:\n");
        printf("  1. 将模型文件放在以下位置之一:\n");
        printf("     - assets/model/model_tokens_rf.onnx\n");
        printf("     - /usr/local/share/kadd/model_tokens_rf.onnx\n");
        printf("  2. 指定模型路径:\n");
        printf("     %s <path-to-model.onnx>\n", argv[0]);
        return 1;
    }

    printf("\n使用模型路径: %s\n", model_path);

    // 检查文件大小
    FILE *model_file = fopen(model_path, "rb");
    if (model_file) {
        fseek(model_file, 0, SEEK_END);
        long file_size = ftell(model_file);
        fclose(model_file);
        printf("模型文件大小: %ld bytes\n", file_size);

        if (file_size < 1000) {
            printf("⚠ 警告: 文件大小异常，可能不是有效的模型文件\n");
        }
    } else {
        printf("✗ 错误: 无法打开模型文件: %s\n", strerror(errno));
        return 1;
    }

    // ========== 步骤 2: 加载模型 ==========
    printf("\n【步骤 2】加载 ONNX 模型\n");
    printf("─────────────────────────────────────────────────────────────\n");

    kadd_model_handle_t *model = NULL;
    printf("特征维度: %d\n", feature_dim);

    int load_result = kadd_model_load(&model, model_path, feature_dim);

    if (load_result != 0) {
        printf("\n✗ 模型加载失败\n");
        printf("错误代码: %d\n", load_result);
        printf("错误描述: %s\n", get_error_string(load_result));

        printf("\n可能的原因:\n");
        printf("  1. ONNX Runtime 库未正确加载\n");
        printf("  2. 模型文件损坏或格式不正确\n");
        printf("  3. 特征维度不匹配\n");
        printf("  4. ONNX Runtime 版本不兼容\n");

        printf("\n调试建议:\n");
#ifdef _WIN32
        printf("  1. 确保 onnxruntime.dll 在程序目录或系统路径中\n");
#else
        printf("  1. 检查 LD_LIBRARY_PATH: %s\n",
               getenv("LD_LIBRARY_PATH") ? getenv("LD_LIBRARY_PATH") : "(未设置)");
        printf("  2. 尝试: export LD_LIBRARY_PATH=.:$LD_LIBRARY_PATH\n");
        printf("  3. 检查动态链接: ldd %s\n", argv[0]);
#endif
        printf("  4. 查看 ONNX Runtime 错误日志（如有）\n");

        return 1;
    }

    printf("✓ 模型加载成功\n");

    // ========== 步骤 3: 简单测试 ==========
    printf("\n【步骤 3】简单推理测试\n");
    printf("─────────────────────────────────────────────────────────────\n");

    float *features = (float *)calloc(feature_dim, sizeof(float));
    if (!features) {
        printf("✗ 内存分配失败\n");
        kadd_model_free(model);
        return 1;
    }

    // 简单的测试特征
    features[1] = 152.0f;
    features[6] = 185.0f;
    features[7] = 1717.0f;
    features[8] = 4152.0f;
    features[11] = 4494.0f;
    features[12] = 190.0f;
    features[18] = 129.0f;
    features[24] = 173.0f;

    float probability = 0.0f;
    int predict_result = kadd_model_predict(model, features, &probability);

    if (predict_result == 0) {
        printf("✓ 推理成功\n");
        printf("预测概率: %.4f (%.2f%%)\n", probability, probability * 100);
    } else {
        printf("✗ 推理失败\n");
        printf("错误代码: %d\n", predict_result);
    }

    // 清理
    free(features);
    kadd_model_free(model);

    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║                     测试完成                                ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");

    return 0;
}
