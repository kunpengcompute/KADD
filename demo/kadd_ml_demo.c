/**
 * @file kadd_ml_demo.c
 * @brief ML 推理引擎使用示例
 *
 * 演示如何使用推理引擎进行模型加载、单次推理、批量推理和资源释放
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kadd_ml_engine.h"
#include "tools.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

int main(int argc, char *argv[])
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

#ifdef _WIN32
    // 设置控制台输出为 UTF-8 编码
    SetConsoleOutputCP(CP_UTF8);
#endif

    /* 默认配置 */
    const char *default_model_paths[] = {
        "../../assets/model/model_tokens_rf.onnx",  // 从 build/demo 目录
        "../assets/model/model_tokens_rf.onnx",     // 从 build 目录
        "assets/model/model_tokens_rf.onnx",        // 从项目根目录
        "/usr/local/share/kadd/model_tokens_rf.onnx", // Linux 安装路径
        NULL
    };

    const char *model_path = NULL;
    if (argc > 1) {
        model_path = argv[1];
    } else {
        // 尝试找到存在的模型文件
        for (int i = 0; default_model_paths[i] != NULL; i++) {
            FILE *test_file = fopen(default_model_paths[i], "rb");
            if (test_file) {
                fclose(test_file);
                model_path = default_model_paths[i];
                break;
            }
        }
        // 如果都找不到，使用第一个作为默认值（让后续代码报错）
        if (!model_path) {
            model_path = default_model_paths[0];
        }
    }

    const int feature_dim = 28;
    const int batch_size = 64;
    const int benchmark_iterations = 1000;

    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║           ML 推理引擎使用演示 (kadd_ml_engine)              ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");

    // ========== 步骤 1: 模型加载 ==========
    printf("【步骤 1】加载 ONNX 模型\n");
    printf("─────────────────────────────────────────────────────────────\n");

    kadd_model_handle_t *model = NULL;

    printf("模型路径: %s\n", model_path);
    printf("特征维度: %d\n", feature_dim);
    printf("\n正在初始化 ONNX Runtime 并加载模型...\n");
    fflush(stdout);

    int load_result = kadd_model_load(&model, model_path, feature_dim);

    if (load_result != 0) {
        printf("\n✗ 模型加载失败 (错误代码: %d)\n", load_result);
        printf("\n可能的原因:\n");
        printf("  1. ONNX Runtime 版本不兼容 (当前库版本可能与头文件不匹配)\n");
        printf("  2. 模型文件损坏或格式不正确\n");
        printf("  3. 特征维度不匹配 (当前: %d)\n", feature_dim);
        printf("  4. 内存分配失败\n");
        printf("  5. 库文件未正确加载\n");

        printf("\n调试建议:\n");
#ifdef _WIN32
        printf("  - 确保 onnxruntime.dll 在程序目录中\n");
#else
        printf("  - 检查 LD_LIBRARY_PATH: %s\n",
               getenv("LD_LIBRARY_PATH") ? getenv("LD_LIBRARY_PATH") : "(未设置)");
        printf("  - 尝试: export LD_LIBRARY_PATH=.:$LD_LIBRARY_PATH\n");
        printf("  - 检查动态链接: ldd %s\n", argv[0]);
#endif
        printf("  - 验证模型文件: file %s\n", model_path);
        printf("  - 检查磁盘空间: df -h\n");

        printf("\n请确保模型文件存在于: %s\n", model_path);
        return 1;
    }

    printf("✓ 模型加载成功\n\n");

    // ========== 步骤 2: 单次推理 ==========
    printf("【步骤 2】单样本推理\n");
    printf("─────────────────────────────────────────────────────────────\n");

    // 准备测试特征
    float *features = (float *)calloc(feature_dim, sizeof(float));
    if (!features) {
        printf("✗ 内存分配失败\n");
        kadd_model_free(model);
        /* 配置已内置，无需释放 */
        return 1;
    }

    float temp_features[28] = {0.0f, 152.0f, 0.0f, 0.0f, 0.0f, 0.0f, 185.0f, 1717.0f, 4152.0f, 0.0f, 0.0f, 4494.0f, 190.0f,
                               0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 129.0f, 0.0f, 0.0f, 0.0f, 0.0f, 173.0f, 0.0f, 0.0f, 0.0f};
    int copy_size = (feature_dim < 28) ? feature_dim : 28;
    memcpy(features, temp_features, copy_size * sizeof(float));

    printf("输入特征: ");
    for (int i = 0; i < feature_dim; i++) {
        printf("%.0f", features[i]);
        if (i < feature_dim - 1) printf(", ");
    }
    printf("\n");

    float probability = 0.0f;
    void *timer = timer_start();

    if (kadd_model_predict(model, features, &probability) == 0) {
        double elapsed = timer_end(timer);
        printf("✓ 推理成功\n");
        printf("预测概率 (Label=1): %.4f (%.2f%%)\n", probability, probability * 100);
        printf("推理耗时: %.3f ms\n\n", elapsed);
    } else {
        printf("✗ 推理失败\n\n");
        free(features);
        kadd_model_free(model);
        /* 配置已内置，无需释放 */
        return 1;
    }

    // 注意：features 后面还会用到，不要在这里释放

    // ========== 步骤 3: 批量推理 ==========
    printf("【步骤 3】批量推理\n");
    printf("─────────────────────────────────────────────────────────────\n");
    float temp_batch[5][28] = {
        {0.0f, 152.0f, 0.0f, 0.0f, 0.0f, 0.0f, 185.0f, 1717.0f, 4152.0f, 0.0f, 0.0f, 4494.0f, 190.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 129.0f, 0.0f, 0.0f, 0.0f, 0.0f, 173.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1120.0f, 1456.0f, 0.0f, 0.0f, 2126.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 114.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 337.0f, 0.0f, 0.0f, 368.0f, 0.0f, 179.0f, 1424.0f, 3130.0f, 0.0f, 0.0f, 3350.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 129.0f, 0.0f, 0.0f, 0.0f, 0.0f, 173.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 926.0f, 1411.0f, 0.0f, 0.0f, 1568.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 114.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 712.0f, 0.0f, 0.0f, 368.0f, 0.0f, 179.0f, 1942.0f, 3605.0f, 0.0f, 0.0f, 4465.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 129.0f, 0.0f, 0.0f, 0.0f, 0.0f, 173.0f, 0.0f, 0.0f, 0.0f}
    };

    // 分配批量特征数组
    float *batch_features = (float *)malloc(batch_size * feature_dim * sizeof(float));
    if (!batch_features) {
        printf("✗ 内存分配失败\n");
        free(features);
        kadd_model_free(model);
        /* 配置已内置，无需释放 */
        return 1;
    }

    // 复制数据（最多复制5个样本，每个样本最多28维）
    int samples_to_copy = (batch_size < 5) ? batch_size : 5;
    int dims_to_copy = (feature_dim < 28) ? feature_dim : 28;
    for (int i = 0; i < samples_to_copy; i++) {
        for (int j = 0; j < dims_to_copy; j++) {
            batch_features[i * feature_dim + j] = temp_batch[i][j];
        }
    }

    // 准备输出概率数组
    float *probabilities = (float *)malloc(batch_size * sizeof(float));
    if (!probabilities) {
        printf("✗ 内存分配失败\n");
        free(batch_features);
        free(features);
        kadd_model_free(model);
        /* 配置已内置，无需释放 */
        return 1;
    }

    printf("批量大小: %d\n", batch_size);

    timer = timer_start();
    int count = kadd_model_predict_batch(model, (const float *)batch_features, samples_to_copy, probabilities, batch_size);
    double batch_elapsed = timer_end(timer);

    if (count > 0) {
        printf("✓ 批量推理成功 (%d/%d)\n", count, batch_size);
        printf("推理结果:\n");
        for (int i = 0; i < batch_size; i++) {
            printf("  样本 %d: %.4f (%.2f%%)\n", i + 1, probabilities[i], probabilities[i] * 100);
        }
        printf("总耗时: %.3f ms\n", batch_elapsed);
        printf("平均耗时: %.3f ms/样本\n", batch_elapsed / batch_size);
    } else {
        printf("✗ 批量推理失败\n");
    }

    free(probabilities);
    free(batch_features);

    // ========== 步骤 4: 性能基准测试 ==========
    {
        printf("\n【步骤 4】性能基准测试 (%d次推理)\n", benchmark_iterations);
        printf("─────────────────────────────────────────────────────────────\n");

        const int warmup_iterations = 10;
        float bench_prob;

        // 预热
        for (int i = 0; i < warmup_iterations; i++) {
            kadd_model_predict(model, features, &bench_prob);
        }

        // 基准测试
        timer = timer_start();
        for (int i = 0; i < benchmark_iterations; i++) {
            kadd_model_predict(model, features, &bench_prob);
        }
        double bench_elapsed = timer_end(timer);

        printf("总耗时: %.3f ms\n", bench_elapsed);
        printf("平均耗时: %.6f ms/次\n", bench_elapsed / benchmark_iterations);
        printf("吞吐量: %.0f 次/秒\n", 1000.0 / (bench_elapsed / benchmark_iterations));
    }

    // ========== 步骤 5: 资源释放 ==========
    printf("\n【步骤 5】释放模型资源\n");
    printf("─────────────────────────────────────────────────────────────\n");

    free(features);
    kadd_model_free(model);
    printf("✓ 模型资源已释放\n");

    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║                     演示完成                                ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");

    // ========== 交互式测试模式 ==========
    printf("\n【交互式测试模式】\n");
    printf("─────────────────────────────────────────────────────────────\n");
    printf("输入格式说明:\n");
    printf("  - 特征向量: 用逗号或空格分隔的浮点数，必须包含 %d 个值\n", feature_dim);
    printf("  - 例如: 0.0,152.0,0.0,0.0,0.0,0.0,185.0,1717.0,4152.0,0.0,0.0,4494.0,190.0,0.0,0.0,0.0,0.0,0.0,0.0,129.0,0.0,0.0,0.0,0.0,173.0,0.0,0.0,0.0\n");
    printf("  - 输入 'q' 或 'quit' 退出程序\n\n");

    // 重新加载模型用于交互测试（因为之前已经释放了）
    kadd_model_handle_t *interactive_model = NULL;
    if (kadd_model_load(&interactive_model, model_path, feature_dim) != 0) {
        printf("✗ 无法加载模型进行交互测试\n");
        return 0;
    }

    float *interactive_features = (float *)calloc(feature_dim, sizeof(float));
    if (!interactive_features) {
        printf("✗ 内存分配失败，无法进入交互模式\n");
        kadd_model_free(interactive_model);
        return 0;
    }

    char input[2048];
    while (1) {
        printf(">> 请输入特征向量 (%d维，或 'q' 退出): ", feature_dim);
        if (fgets(input, sizeof(input), stdin) == NULL) break;

        // 去除换行符
        input[strcspn(input, "\n")] = 0;

        // 检查退出命令
        if (strcmp(input, "q") == 0 || strcmp(input, "quit") == 0 ||
            strcmp(input, "Q") == 0 || strcmp(input, "QUIT") == 0) {
            printf("退出交互模式\n");
            break;
        }

        // 检查空输入
        if (strlen(input) == 0) {
            continue;
        }

        // 解析特征向量
        int parsed_count = 0;
        char *token = strtok(input, ", ");
        while (token != NULL && parsed_count < feature_dim) {
            if (sscanf(token, "%f", &interactive_features[parsed_count]) == 1) {
                parsed_count++;
            }
            token = strtok(NULL, ", ");
        }

        if (parsed_count != feature_dim) {
            printf("✗ 特征维度不匹配，需要 %d 个值，实际解析到 %d 个\n", feature_dim, parsed_count);
            continue;
        }

        // 执行推理
        printf("\n输入特征: ");
        for (int i = 0; i < feature_dim; i++) {
            printf("%.0f", interactive_features[i]);
            if (i < feature_dim - 1) printf(", ");
        }
        printf("\n");

        float probability = 0.0f;
        void *timer = timer_start();

        if (kadd_model_predict(interactive_model, interactive_features, &probability) == 0) {
            double elapsed = timer_end(timer);
            printf("✓ 推理成功\n");
            printf("预测概率 (Label=1): %.4f (%.2f%%)\n", probability, probability * 100);
            printf("推理耗时: %.3f ms\n", elapsed);
        } else {
            printf("✗ 推理失败\n");
        }

        printf("\n");
    }

    free(interactive_features);
    kadd_model_free(interactive_model);
    printf("✓ 模型资源已释放\n");

    /* 配置已内置，无需释放 */

    return 0;
}
