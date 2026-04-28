/**
 * @file kadd_ml_demo.c
 * @brief ML 推理引擎交互式演示
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

// 默认模型路径
const char *default_model_paths[] = {
    "../../assets/model/model_tokens_rf.onnx",
    "../assets/model/model_tokens_rf.onnx",
    "assets/model/model_tokens_rf.onnx",
    "/usr/local/share/kadd/model_tokens_rf.onnx",
    NULL
};

// 查找模型文件
const char *find_model_path() {
    for (int i = 0; default_model_paths[i] != NULL; i++) {
        FILE *f = fopen(default_model_paths[i], "rb");
        if (f) {
            fclose(f);
            return default_model_paths[i];
        }
    }
    return default_model_paths[0];
}

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

    printf("请输入特征向量 (%d维，用逗号或空格分隔，直接回车使用默认值): ", feature_dim);

    char input[2048];
    fgets(input, sizeof(input), stdin);
    input[strcspn(input, "\n")] = 0;

    // 使用默认值或解析输入
    if (strlen(input) == 0) {
        float defaults[] = {0.0f, 152.0f, 0.0f, 0.0f, 0.0f, 0.0f, 185.0f, 1717.0f, 4152.0f, 0.0f, 0.0f, 4494.0f, 190.0f,
                           0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 129.0f, 0.0f, 0.0f, 0.0f, 0.0f, 173.0f, 0.0f, 0.0f, 0.0f};
        memcpy(features, defaults, feature_dim * sizeof(float));
        printf("使用默认特征向量\n");
    } else {
        int count = parse_features(input, features, feature_dim);
        if (count != feature_dim) {
            printf("错误: 需要 %d 个值，实际解析到 %d 个\n", feature_dim, count);
            free(features);
            return;
        }
    }

    float probability = 0.0f;
    void *timer = timer_start();

    if (kadd_model_predict(model, features, &probability) == 0) {
        double elapsed = timer_end(timer);
        printf("结果: %.4f (%.2f%%), 耗时: %.3f ms\n", probability, probability * 100, elapsed);
    } else {
        printf("推理失败\n");
    }

    free(features);
}

// 批量推理测试
void test_batch_predict(kadd_model_handle_t *model, int feature_dim) {
    int batch_size = 0;
    printf("请输入批量大小 (1-100, 直接回车使用默认值10): ");

    char input[64];
    fgets(input, sizeof(input), stdin);
    input[strcspn(input, "\n")] = 0;

    if (strlen(input) == 0) {
        batch_size = 10;
        printf("使用默认批量大小: 10\n");
    } else if (sscanf(input, "%d", &batch_size) != 1 || batch_size < 1 || batch_size > 100) {
        printf("批量大小无效，使用默认值10\n");
        batch_size = 10;
    }

    float *features = (float *)calloc(batch_size * feature_dim, sizeof(float));
    float *probabilities = (float *)calloc(batch_size, sizeof(float));
    if (!features || !probabilities) {
        printf("内存分配失败\n");
        free(features);
        free(probabilities);
        return;
    }

    // 填充特征数据（使用默认值）
    float defaults[] = {0.0f, 152.0f, 0.0f, 0.0f, 0.0f, 0.0f, 185.0f, 1717.0f, 4152.0f, 0.0f, 0.0f, 4494.0f, 190.0f,
                       0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 129.0f, 0.0f, 0.0f, 0.0f, 0.0f, 173.0f, 0.0f, 0.0f, 0.0f};
    for (int i = 0; i < batch_size; i++) {
        memcpy(features + i * feature_dim, defaults, feature_dim * sizeof(float));
    }

    void *timer = timer_start();
    int count = kadd_model_predict_batch(model, features, batch_size, probabilities, batch_size);
    double elapsed = timer_end(timer);

    if (count > 0) {
        printf("批量推理完成: %d/%d, 总耗时: %.3f ms, 平均: %.3f ms/样本\n",
               count, batch_size, elapsed, elapsed / batch_size);
        printf("结果:\n");
        for (int i = 0; i < batch_size; i++) {
            printf("  [%2d] %.4f (%.2f%%)\n", i + 1, probabilities[i], probabilities[i] * 100);
        }
    } else {
        printf("批量推理失败\n");
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

    kadd_model_handle_t *model = NULL;
    int feature_dim = 28;
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

                const char *model_path = find_model_path();
                printf("使用模型路径: %s\n", model_path);
                printf("请输入特征维度 (直接回车使用默认值28): ");

                char dim_input[64];
                fgets(dim_input, sizeof(dim_input), stdin);
                dim_input[strcspn(dim_input, "\n")] = 0;

                if (strlen(dim_input) > 0 && sscanf(dim_input, "%d", &feature_dim) == 1) {
                    if (feature_dim <= 0 || feature_dim > 1024) {
                        printf("特征维度无效，使用默认值28\n");
                        feature_dim = 28;
                    }
                } else {
                    feature_dim = 28;
                    printf("使用默认特征维度: 28\n");
                }

                if (kadd_model_load(&model, model_path, feature_dim) == 0) {
                    printf("模型加载成功\n");
                } else {
                    printf("模型加载失败\n");
                }
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
