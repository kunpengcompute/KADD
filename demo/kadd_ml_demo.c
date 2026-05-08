/**
 * @file kadd_ml_demo.c
 * @brief ML 推理引擎交互式演示
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kadd_ml_engine.h"
#include "kadd_config_loader.h"
#include "tools.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

// 从字符串解析特征向量
int parse_features(const char *input, float *features, int feature_dim) {
    int count = 0;
    char *token = strtok((char *)input, ", ");
    while (token != NULL && count < feature_dim) {
        if (sscanf(token, "%f", &features[count]) == 1) {
            count++;
        }
        token = strtok(NULL, ", ");
    }
    return count;
}

// 单次推理测试
void test_single_predict(kadd_model_handle_t *model, int feature_dim) {
    float *features = (float *)calloc(feature_dim, sizeof(float));
    if (!features) {
        printf("内存分配失败\n");
        return;
    }

    printf("请输入特征向量 (%d维，用逗号或空格分隔): ", feature_dim);

    char input[2048];
    fgets(input, sizeof(input), stdin);
    input[strcspn(input, "\n")] = 0;

    parse_features(input, features, feature_dim);

    float probability = 0.0f;
    void *timer = timer_start();
    int ret = kadd_model_predict(model, features, &probability);
    double elapsed = timer_end(timer);

    printf("返回值: %d\n", ret);

    if (ret >= 0) {
        printf("概率: %.4f (%.2f%%), 耗时: %.2f μs\n", probability, probability * 100, elapsed);
    }

    free(features);
}

// 批量推理测试
void test_batch_predict(kadd_model_handle_t *model, int feature_dim) {
    int batch_size;
    printf("请输入批量大小: ");
    scanf("%d", &batch_size);
    getchar();

    float *features = (float *)calloc(batch_size * feature_dim, sizeof(float));
    float *probabilities = (float *)calloc(batch_size, sizeof(float));
    if (!features || !probabilities) {
        printf("内存分配失败\n");
        free(features);
        free(probabilities);
        return;
    }

    printf("请输入 %d 个特征向量:\n", batch_size);
    for (int i = 0; i < batch_size; i++) {
        printf("[%d]> ", i + 1);
        char input[2048];
        fgets(input, sizeof(input), stdin);
        input[strcspn(input, "\n")] = 0;
        parse_features(input, features + i * feature_dim, feature_dim);
    }

    void *timer = timer_start();
    int count = kadd_model_predict_batch(model, features, batch_size, probabilities, batch_size);
    double elapsed = timer_end(timer);

    printf("返回值: %d\n", count);

    if (count >= 0) {
        printf("总耗时: %.2f μs, 平均: %.2f μs/样本\n", elapsed, elapsed / batch_size);
        for (int i = 0; i < batch_size; i++) {
            printf("  [%2d] %.4f (%.2f%%)\n", i + 1, probabilities[i], probabilities[i] * 100);
        }
    }

    free(features);
    free(probabilities);
}

// 主菜单
void show_main_menu() {
    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║              ML 推理引擎交互式测试                           ║\n");
    printf("╠════════════════════════════════════════════════════════════╣\n");
    printf("║  1. 加载模型 (kadd_model_load)                              ║\n");
    printf("║  2. 单次推理 (kadd_model_predict)                           ║\n");
    printf("║  3. 批量推理 (kadd_model_predict_batch)                     ║\n");
    printf("║  4. 释放模型 (kadd_model_free)                             ║\n");
    printf("║  0. 退出程序                                               ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
}

int main() {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    // 加载配置
    kadd_config_t config;
    if (kadd_config_load(NULL, &config) != 0) {
        printf("警告: 无法加载 config.json，使用默认配置\n");
        config.ml.feature_dim = 28;
        strcpy(config.ml.model_path, "assets/model/model_tokens_rf.onnx");
    }

    kadd_model_handle_t *model = NULL;
    int feature_dim = config.ml.feature_dim;
    int running = 1;

    while (running) {
        show_main_menu();

        // 显示当前状态
        if (model) {
            printf("[状态] 模型已加载 (特征维度: %d)\n", feature_dim);
        } else {
            printf("[状态] 未加载模型\n");
        }

        printf("请选择操作 (0-4): ");

        char input[64];
        if (fgets(input, sizeof(input), stdin) == NULL) break;

        int choice;
        if (sscanf(input, "%d", &choice) != 1) {
            printf("输入无效，请输入数字\n");
            continue;
        }

        switch (choice) {
            case 1: {
                if (model) {
                    printf("模型已加载，请先释放现有模型\n");
                    break;
                }

                const char *model_path = config.ml.model_path;
                printf("使用模型路径: %s\n", model_path);

                printf("请输入特征维度: ");
                scanf("%d", &feature_dim);
                getchar();

                int ret = kadd_model_load(&model, model_path, feature_dim);
                printf("返回值: %d\n", ret);
                break;
            }
            case 2: {
                if (!model) {
                    printf("请先加载模型\n");
                    break;
                }
                test_single_predict(model, feature_dim);
                break;
            }
            case 3: {
                if (!model) {
                    printf("请先加载模型\n");
                    break;
                }
                test_batch_predict(model, feature_dim);
                break;
            }
            case 4: {
                if (!model) {
                    printf("没有已加载的模型\n");
                    break;
                }
                kadd_model_free(model);
                model = NULL;
                printf("模型已释放\n");
                break;
            }
            case 0: {
                running = 0;
                printf("退出程序\n");
                break;
            }
            default: {
                printf("无效选择，请输入 0-4\n");
                break;
            }
        }
    }

    if (model) {
        kadd_model_free(model);
    }

    return 0;
}
