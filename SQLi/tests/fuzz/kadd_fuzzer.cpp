/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *
 * @file kadd_fuzzer.cpp
 * @brief KADD项目 LibFuzzer 主入口
 *
 * 整合测试所有KADD核心引擎的鲁棒性：
 * - SQL注入检测引擎
 * - DFA分词引擎
 * - 特征提取引擎
 * - ML推理引擎（原生 RF SVE + ONNX）
 * - DL/CNN 推理引擎（原生 CNN SVE + ONNX）
 * - 推理引擎（ONNX RF/CNN）
 * - 配置加载器
 *
 * 参考了 KDADK-APPID/test/fuzz/kdadk_fuzzer.cpp 的设计
 *
 * 覆盖策略：
 * - 通过 selector 分派到 21 个专项 fuzz 用例
 * - 加载远端原生模型（.krf/.kcn）驱动 SVE 成功路径（真实特征维度）
 * - 多批量大小 + 边界/非法批量大小覆盖分批循环与 SVE 尾块
 * - 真实 SQL 注入载荷 + 随机输入覆盖检测分支
 * - 写临时 config JSON 覆盖配置校验分支（ml/dl 模式、字段缺失、畸形 JSON）
 */

#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <algorithm>  // 新增：用于std::min
#include <sstream>    // 新增：用于std::stringstream
#include <iomanip>    // 新增：用于std::hex, std::setfill, std::setw
#include <limits>     // 新增：用于 std::numeric_limits

#include "kadd_sqli_engine.h"
#include "kadd_dfa_engine.h"
#include "kadd_feature_engine.h"
#include "kadd_ml_engine.h"
#include "kadd_inference_engine.h"
#include "kadd_dl_engine.h"
#include "kadd_config_loader.h"
#include "kadd_rf_engine.h"    // 内部头：直接覆盖 kadd_rf_model_* 的校验/错误分支
#include "kadd_cnn_engine.h"   // 内部头：直接覆盖 kadd_cnn_model_* 的校验/错误分支
#include "fuzz_utils.h"

/* ========== 全局FUZZ上下文 ========== */

static struct {
    kadd_sqli_engine_t *sqli_engine;
    kadd_dfa_engine_t *dfa_engine;
    kadd_model_handle_t *ml_model;
    kadd_config_t config;
    bool initialized;
} g_fuzz_context = {nullptr, nullptr, nullptr, {}, false};

/* ========== 远端资源路径（硬编码回退候选） ========== */

static const char *kRfKrfPath  = "/opt/KADD/SQLi/assets/model/model_tokens_rf.krf";
static const char *kRfOnnxPath = "/opt/KADD/SQLi/assets/model/model_tokens_rf.onnx";
static const char *kCnnKcnPath = "/opt/KADD/SQLi/assets/model/model_tokens_dl_cnn.kcn";
static const char *kCnnOnnxPath = "/opt/KADD/SQLi/assets/model/model_tokens_dl_cnn.onnx";
static const char *kTranPath   = "/opt/KADD/SQLi/assets/data/transition_table.bin";
static const char *kOutPath    = "/opt/KADD/SQLi/assets/data/output_table.bin";
static const char *kEodPath    = "/opt/KADD/SQLi/assets/data/eod_table.bin";
static const char *kConfigAbs  = "/opt/KADD/SQLi/config.json";

static const char *kRfKrfRel  = "assets/model/model_tokens_rf.krf";
static const char *kCnnKcnRel = "assets/model/model_tokens_dl_cnn.kcn";

/* ========== 常用批量大小集合 ========== */

static const int kBatchSizes[] = {1, 2, 3, 4, 8, 16, 31, 32, 33, 63, 64,
                                   65, 127, 128, 129, 255, 256, 257,
                                   511, 512, 513};
#define K_BATCH_SIZE_COUNT (sizeof(kBatchSizes) / sizeof(kBatchSizes[0]))

/* ========== 引擎初始化 ========== */

/**
 * @brief 解析远端可用的配置文件路径（相对优先，绝对回退）
 */
static const char *ResolveConfigPath() {
    static char resolved[512] = {0};
    if (resolved[0] != '\0') {
        return resolved;
    }
    FILE *f = fopen("config.json", "rb");
    if (f) {
        fclose(f);
        const char *s = "config.json";
        for (size_t i = 0; i < sizeof(resolved) - 1 && s[i]; i++)
            resolved[i] = s[i];
        resolved[sizeof(resolved) - 1] = '\0';
        return resolved;
    }
    f = fopen(kConfigAbs, "rb");
    if (f) {
        fclose(f);
        for (size_t i = 0; i < sizeof(resolved) - 1 && kConfigAbs[i]; i++)
            resolved[i] = kConfigAbs[i];
        resolved[sizeof(resolved) - 1] = '\0';
        return resolved;
    }
    for (size_t i = 0; i < sizeof(resolved) - 1 && kConfigAbs[i]; i++)
        resolved[i] = kConfigAbs[i];
    resolved[sizeof(resolved) - 1] = '\0';
    return resolved;
}

/**
 * @brief 初始化所有引擎（仅执行一次）
 */
static void InitializeEngines() {
    if (g_fuzz_context.initialized) return;

    const char *config_path = ResolveConfigPath();

    // 加载配置
    if (kadd_config_load(config_path, &g_fuzz_context.config) != 0) {
        // 使用默认配置
        g_fuzz_context.initialized = true;
        return;
    }

    // 创建SQLi引擎
    kadd_sqli_engine_create_from_file(&g_fuzz_context.sqli_engine, config_path);

    // 创建DFA引擎
    kadd_dfa_table_paths_t dfa_paths = {
        g_fuzz_context.config.transition_table,
        g_fuzz_context.config.output_table,
        g_fuzz_context.config.eod_table
    };

    kadd_dfa_engine_create(&g_fuzz_context.dfa_engine, &dfa_paths,
                               KADD_DFA_MAX_INPUT_CHARS,
                               KADD_DFA_MAX_TOKENS);

    // 创建ML引擎（native RF 优先，feature_dim=0 从模型自动读取）
    if (g_fuzz_context.config.model_rf[0] != '\0') {
        kadd_model_load(&g_fuzz_context.ml_model, g_fuzz_context.config.model_rf, 0);
    }
    if (!g_fuzz_context.ml_model) {
        kadd_model_load(&g_fuzz_context.ml_model, kRfKrfPath, 0);
    }

    g_fuzz_context.initialized = true;
}

/* ========== SQL注入检测引擎 FUZZ测试 ========== */

/**
 * @brief FUZZ测试SQL注入检测引擎
 */
static void FuzzSQLiEngine(const std::string& input) {
    if (!g_fuzz_context.sqli_engine) return;

    // 测试1: 单次检测
    float probability;
    kadd_sqli_detect(g_fuzz_context.sqli_engine, input.c_str(), &probability);

    // 测试2: 批量检测
    const char *inputs[] = {input.c_str()};
    float probs[1];
    kadd_sqli_detect_batch(g_fuzz_context.sqli_engine, inputs, 1, probs, 32);

    // 测试3: 空指针和异常输入
    kadd_sqli_detect(nullptr, input.c_str(), &probability);
    kadd_sqli_detect(g_fuzz_context.sqli_engine, nullptr, &probability);
    kadd_sqli_detect(nullptr, nullptr, &probability);

    // 新增：轻量级大输入测试
    if (input.length() < 100) {
        std::string long_input(500, 'A');  // 适度大小
        kadd_sqli_detect(g_fuzz_context.sqli_engine, long_input.c_str(), &probability);
    }
}

/* ========== SQLi 引擎释放 FUZZ测试 ========== */

/**
 * @brief FUZZ测试 SQLi 引擎释放函数
 */
static void FuzzSQLiEngineFree(const uint8_t *data, size_t size)
{
    // 测试1: 空指针释放（应安全返回）
    kadd_sqli_engine_free(nullptr);

    // 测试2: 创建后正常释放
    kadd_sqli_engine_t *tmp_engine1 = nullptr;
    if (kadd_sqli_engine_create_from_file(&tmp_engine1, ResolveConfigPath()) == 0) {
        kadd_sqli_engine_free(tmp_engine1);
    }

    // 测试3: 使用无效配置文件路径测试错误处理路径
    if (size > 5) {
        kadd_sqli_engine_t *tmp_engine2 = nullptr;
        // 使用C++ string格式化，避免使用snprintf（类型安全、无缓冲区溢出风险）
        std::stringstream ss;
        ss << "/tmp/fuzz_" << std::hex << std::setfill('0') << std::setw(2)
           << static_cast<unsigned int>(data[0]) << ".json";
        std::string fuzz_path_str = ss.str();
        const char *fuzz_path = fuzz_path_str.c_str();
        // 创建失败时 tmp_engine2 应保持为 nullptr
        kadd_sqli_engine_create_from_file(&tmp_engine2, fuzz_path);
        // 即使创建失败，free(nullptr) 也应该安全
        kadd_sqli_engine_free(tmp_engine2);
    }

    // 测试4: 多次创建释放循环（覆盖内存池分配/释放路径）
    for (int i = 0; i < 3; i++) {
        kadd_sqli_engine_t *tmp_engine3 = nullptr;
        if (kadd_sqli_engine_create_from_file(&tmp_engine3, ResolveConfigPath()) == 0) {
            // 使用引擎一次
            float prob;
            kadd_sqli_detect(tmp_engine3, "test", &prob);
            // 然后释放
            kadd_sqli_engine_free(tmp_engine3);
        }
    }

    // 测试5: 使用全局引擎进行边界测试
    if (g_fuzz_context.sqli_engine) {
        // 测试在引擎使用后再释放（最后由全局析构处理）
        float prob;
        kadd_sqli_detect(g_fuzz_context.sqli_engine, "1' OR '1'='1", &prob);
    }
}

/* ========== DFA分词引擎 FUZZ测试 ========== */

/**
 * @brief FUZZ测试DFA分词引擎
 */
static void FuzzDFAEngine(const std::string& input) {
    if (!g_fuzz_context.dfa_engine) return;

    int tokens[300];
    int token_count;

    // 测试1: 单次分词
    kadd_dfa_engine_tokenize(g_fuzz_context.dfa_engine, input.c_str(),
                             input.length(), tokens, &token_count);

    // 测试2: 批量分词
    const char *batch_inputs[] = {input.c_str()};
    size_t lengths[] = {input.length()};
    int *outputs[1] = {nullptr};
    int dims[1];

    kadd_dfa_batch_t batch = {
        .inputs = batch_inputs,
        .lengths = lengths,
        .count = 1,
        .outputs = outputs,
        .dims = dims,
        .batch_size = 32
    };

    kadd_dfa_engine_tokenize_batch(g_fuzz_context.dfa_engine, &batch);
    if (outputs[0]) free(outputs[0]);

    // 测试3: 空指针和异常输入
    kadd_dfa_engine_tokenize(nullptr, input.c_str(), input.length(), tokens, &token_count);
    kadd_dfa_engine_tokenize(g_fuzz_context.dfa_engine, nullptr, 0, tokens, &token_count);
    kadd_dfa_engine_tokenize(g_fuzz_context.dfa_engine, input.c_str(), input.length(), nullptr, nullptr);
    kadd_dfa_engine_tokenize_batch(nullptr, &batch);
    kadd_dfa_engine_tokenize_batch(g_fuzz_context.dfa_engine, nullptr);
}

/* ========== 特征提取引擎 FUZZ测试 ========== */

/**
 * @brief FUZZ测试特征提取引擎
 */
static void FuzzFeatureEngine(const std::string& input) {
    if (!g_fuzz_context.dfa_engine) return;

    int tokens[300];
    int token_count = 0;

    // 先进行分词获取token序列
    kadd_dfa_engine_tokenize(g_fuzz_context.dfa_engine, input.c_str(),
                             input.length(), tokens, &token_count);

    if (token_count > 0) {
        int features[28];

        // 测试1: 单次特征转换
        kadd_seq_to_feature(tokens, token_count, 5, features, 28);

        // 测试2: 批量特征转换
        const int *seqs[] = {tokens};
        const int lens[] = {token_count};
        int batch_features[28];

        kadd_feature_batch_t feat_batch = {
            .seqs = seqs,
            .seq_lens = lens,
            .count = 1,
            .features = batch_features,
            .feature_dim = 28,
            .window_size = 5,
            .batch_size = 32
        };
        kadd_seq_to_feature_batch(&feat_batch);
    }

    // 测试3: 空指针和异常输入
    int features[28];
    kadd_seq_to_feature(nullptr, 10, 5, features, 28);
    kadd_seq_to_feature(tokens, token_count, 5, nullptr, 28);
    kadd_seq_to_feature(tokens, -1, 5, features, 28);
    kadd_seq_to_feature(tokens, token_count, 0, features, 28);
}

/* ========== ML推理引擎 FUZZ测试 ========== */

/**
 * @brief FUZZ测试ML推理引擎
 */
static void FuzzMLEngine(const std::string& input) {
    if (!g_fuzz_context.ml_model || !g_fuzz_context.dfa_engine) return;

    int tokens[300];
    int token_count = 0;

    // 构造特征向量（固定28维，与 RF 模型 feature_count=28 一致）
    kadd_dfa_engine_tokenize(g_fuzz_context.dfa_engine, input.c_str(),
                             input.length(), tokens, &token_count);

    if (token_count > 0) {
        int features[28];
        kadd_seq_to_feature(tokens, token_count, 5, features, 28);

        float float_features[28];
        for (int i = 0; i < 28; i++) {
            float_features[i] = static_cast<float>(features[i]);
        }

        // 测试1: 单样本推理
        float probability;
        kadd_model_predict(g_fuzz_context.ml_model, float_features, &probability);

        // 测试2: 批量推理（n=1）
        float batch_probs[1];
        kadd_model_predict_batch(g_fuzz_context.ml_model, float_features, 1,
                               batch_probs, 32);
    }

    // 测试 kadd_model_get_feature_dim
    int feat_dim = kadd_model_get_feature_dim(g_fuzz_context.ml_model);
    (void)feat_dim;
    kadd_model_get_feature_dim(nullptr);  // NULL 测试

    // 测试3: 空指针和异常输入
    float prob;
    float test_features[28] = {0};
    kadd_model_predict(nullptr, test_features, &prob);
    kadd_model_predict(g_fuzz_context.ml_model, nullptr, &prob);
    kadd_model_predict(g_fuzz_context.ml_model, test_features, nullptr);

    // 批量推理异常测试
    float test_probs[1];
    kadd_model_predict_batch(nullptr, test_features, 1, test_probs, 32);
    kadd_model_predict_batch(g_fuzz_context.ml_model, nullptr, 1, test_probs, 32);
    kadd_model_predict_batch(g_fuzz_context.ml_model, test_features, 0, test_probs, 32);
    kadd_model_predict_batch(g_fuzz_context.ml_model, test_features, -1, test_probs, 32);
    kadd_model_predict_batch(g_fuzz_context.ml_model, test_features, 1, nullptr, 32);
    kadd_model_predict_batch(g_fuzz_context.ml_model, test_features, 1, test_probs, 0);
    kadd_model_predict_batch(g_fuzz_context.ml_model, test_features, 1, test_probs, 2000);
}

/* ========== 边界情况和异常输入 FUZZ测试 ========== */

/**
 * @brief FUZZ测试边界情况和异常输入
 */
static void FuzzEdgeCases(const uint8_t* data, size_t size) {
    // 测试超长输入
    if (size > 4000) {
        std::string long_input(reinterpret_cast<const char*>(data), 4000);
        if (g_fuzz_context.sqli_engine) {
            float prob;
            kadd_sqli_detect(g_fuzz_context.sqli_engine, long_input.c_str(), &prob);
        }
    }

    // 测试特殊字符序列
    if (g_fuzz_context.sqli_engine) {
        float prob;

        // 包含空字符的输入
        std::string null_input = "test\x00input";
        kadd_sqli_detect(g_fuzz_context.sqli_engine, null_input.c_str(), &prob);

        // 包含高字节字符的输入
        std::string high_byte_input = "test\xff\xfe\xfd";
        kadd_sqli_detect(g_fuzz_context.sqli_engine, high_byte_input.c_str(), &prob);

        // 包含各种控制字符的输入
        std::string control_input = "test\x01\x02\x03\x1b\x1c";
        kadd_sqli_detect(g_fuzz_context.sqli_engine, control_input.c_str(), &prob);
    }

    // 测试空输入和极短输入
    if (g_fuzz_context.sqli_engine) {
        float prob;
        kadd_sqli_detect(g_fuzz_context.sqli_engine, "", &prob);
        kadd_sqli_detect(g_fuzz_context.sqli_engine, "a", &prob);
        kadd_sqli_detect(g_fuzz_context.sqli_engine, "ab", &prob);
    }
}

/* ========== 错误注入 FUZZ测试 ========== */

/**
 * @brief 模拟各种引擎初始化失败场景
 */
static void FuzzInitFailures(const uint8_t* data, size_t size) {
    if (size < 4) return;

    // 测试1: 模拟 ONNX API 加载/卸载（使用临时引擎，不释放全局引擎）
    if (data[0] % 10 == 0) {  // 10% 概率触发
        // 创建临时模型进行测试，不使用全局引擎
        kadd_model_handle_t *tmp_model = nullptr;
        if (kadd_model_load(&tmp_model,
                           g_fuzz_context.config.model_rf, 0) == 0) {
            // 测试后释放临时模型
            kadd_model_free(tmp_model);
        }
    }

    // 测试2: 模拟配置加载失败（通过无效路径）
    if (data[1] % 20 == 0) {
        kadd_config_t bad_config;
        kadd_config_load("nonexistent.json", &bad_config);
    }

    // 测试3: 模拟 DFA 表加载失败
    if (data[2] % 30 == 0) {
        kadd_dfa_table_paths_t bad_paths = {
            "bad_transition.bin",
            "bad_output.bin",
            "bad_eod.bin"
        };
        kadd_dfa_engine_t *bad_dfa = nullptr;
        kadd_dfa_engine_create(&bad_dfa, &bad_paths, 300, 300);
        if (bad_dfa) kadd_dfa_engine_free(bad_dfa);
    }

    // 测试4: 模拟特征维度不匹配
    if (data[3] % 40 == 0) {
        kadd_model_handle_t *bad_model = nullptr;
        // 使用错误维度加载模型（会导致维度不匹配错误）
        // 注意：kadd_model_load 失败时会自动清理资源，不需要手动 free
        int ret = kadd_model_load(&bad_model, g_fuzz_context.config.model_rf, 999);
        if (ret == KADD_SUCCESS && bad_model) {
            // 只有成功加载才需要释放
            kadd_model_free(bad_model);
        }
        // 失败时 bad_model 仍为 NULL，内部已清理
    }
}

/**
 * @brief 边界和极限测试（修复栈溢出版本）
 */
static void FuzzBoundaryConditions(const uint8_t* data, size_t size) {
    if (!g_fuzz_context.sqli_engine || !g_fuzz_context.dfa_engine) return;

    // 测试1: 批处理大小边界 - 简化测试
    if (size >= 4) {
        int batch_sizes[] = {0, 1, 32};  // 移除1024和1025
        for (int bs : batch_sizes) {
            const char *inputs[] = {"test"};
            float probs[1];
            kadd_sqli_detect_batch(g_fuzz_context.sqli_engine, inputs, 1, probs, bs);
        }
    }

    // 测试2: 输入数量边界 - 减少最大数量
    int counts[] = {0, 1, 10, 32};  // 从100减少到32
    for (int count : counts) {
        if (count <= 0) continue;

        std::vector<const char*> inputs(count, "test");
        std::vector<float> probs(count);

        kadd_sqli_detect_batch(g_fuzz_context.sqli_engine,
                               inputs.data(), count, probs.data(), 32);
    }

    // 测试3: 特征维度边界 - 简化测试
    if (size >= 28) {
        int test_tokens[300] = {0};
        int token_count = 50;

        int feature_dims[] = {0, 1, 28, 29};  // 减少测试维度
        for (int dim : feature_dims) {
            int features[100] = {0};  // 从1000减少到100
            kadd_seq_to_feature(test_tokens, token_count, 5, features, dim);
        }
    }
}

/**
 * @brief 多条目批处理检测测试（覆盖 process_batch_loop 分片路径）
 */
static void FuzzBatchDetection(const uint8_t* data, size_t size) {
    if (!g_fuzz_context.sqli_engine || size < 8) return;

    int count = 1 + (data[0] % std::min(64, static_cast<int>(size)));  // 1-64，但不超过 size
    int batch_size = 1 + (data[1] % 64);  // 1-64 批大小

    std::vector<std::string> inputs_str(count);
    std::vector<const char*> inputs(count);
    std::vector<float> probs(count);

    for (int i = 0; i < count; i++) {
        inputs_str[i] = CreateSafeString(data + i, (size - i > 4) ? 4 : size - i);
        inputs[i] = inputs_str[i].c_str();
    }

    kadd_sqli_detect_batch(g_fuzz_context.sqli_engine,
                           inputs.data(), count,
                           probs.data(), batch_size);
}

/**
 * @brief 长序列特征提取测试（覆盖 seq_len > 256 malloc 路径）
 */
static void FuzzFeatureEngineLong(const uint8_t* data, size_t size) {
    if (size < 4) return;

    int seq_len = 257 + (data[0] % 44);  // 257..300（不超过300上限）
    if (seq_len > 300) seq_len = 300;

    int *long_seq = (int *)calloc(seq_len, sizeof(int));
    if (!long_seq) return;

    for (int i = 0; i < seq_len; i++) {
        long_seq[i] = data[1 + i % (size - 1)] % 28;  // 保证 token < feature_dim
    }

    int features[28];
    kadd_seq_to_feature(long_seq, seq_len, 5, features, 28);
    free(long_seq);
}

/**
 * @brief token越界测试（覆盖 token >= feature_dim 路径）
 */
static void FuzzFeatureEngineOutOfRange(const uint8_t* data, size_t size) {
    if (size < 4) return;

    int seq_len = 5 + (data[0] % 20);
    int *seq = (int *)malloc(seq_len * sizeof(int));
    if (!seq) return;

    // 构造包含 >= 28 的越界 token 值
    for (int i = 0; i < seq_len; i++) {
        seq[i] = data[1 + i % (size - 1)] % 35;
    }

    int features[28];
    kadd_seq_to_feature(seq, seq_len, 5, features, 28);
    free(seq);
}

/**
 * @brief batch 错误路径测试（覆盖 NULL seqs、超限 batch_size 等）
 */
static void FuzzBatchErrorPaths() {
    int features[28] = {0};
    const int *seqs[1] = {nullptr};  // 用于测试 NULL seq_lens 的情况
    int lens[1] = {10};

    // NULL seqs
    kadd_feature_batch_t b1 = {nullptr, lens, 1, features, 28, 5, 64};
    kadd_seq_to_feature_batch(&b1);

    // NULL seq_lens
    kadd_feature_batch_t b2 = {seqs, nullptr, 1, features, 28, 5, 64};
    kadd_seq_to_feature_batch(&b2);

    // count <= 0
    kadd_feature_batch_t b3 = {seqs, lens, 0, features, 28, 5, 64};
    kadd_seq_to_feature_batch(&b3);

    // batch_size 超限 (> 1024)
    int valid_seq[10] = {1,2,3,4,5,6,7,8,9,10};
    const int *valid_seqs[1] = {valid_seq};
    int valid_lens[1] = {10};
    kadd_feature_batch_t b4 = {valid_seqs, valid_lens, 1, features, 28, 5, 2000};
    kadd_seq_to_feature_batch(&b4);

    // NULL seq pointer
    const int *null_seqs[1] = {nullptr};
    int null_lens[1] = {10};
    kadd_feature_batch_t b5 = {null_seqs, null_lens, 1, features, 28, 5, 64};
    kadd_seq_to_feature_batch(&b5);
}

/* ========== NULL 指针和边界情况 FUZZ测试 ========== */

/**
 * @brief FUZZ测试所有模块的 NULL 指针和边界情况
 * 覆盖之前遗漏的 29 个边界情况
 */
static void FuzzNullPointers(const uint8_t* data, size_t size)
{
    if (size < 4) return;

    uint8_t selector = data[0];
    switch (selector % 16) {
        case 0: // ML Engine - kadd_model_load NULL 测试
            {
                kadd_model_handle_t *tmp = nullptr;
                // 只测试 NULL 参数路径，不实际加载模型（避免高频加载导致 ONNX 内存泄漏累积）
                // model_path=NULL
                kadd_model_load(&tmp, nullptr, 28);
            }
            break;

        case 1: // ML Engine - kadd_model_predict NULL 测试
            {
                float test_features[28] = {0};
                float prob;
                // handle=NULL
                kadd_model_predict(nullptr, test_features, &prob);
                // features=NULL
                kadd_model_predict(g_fuzz_context.ml_model, nullptr, &prob);
                // probability=NULL
                kadd_model_predict(g_fuzz_context.ml_model, test_features, nullptr);
            }
            break;

        case 2: // ML Engine - kadd_model_predict_batch NULL 测试
            {
                float test_features[28] = {0};
                float probs[1];
                // handle=NULL
                kadd_model_predict_batch(nullptr, test_features, 1, probs, 32);
                // features=NULL
                kadd_model_predict_batch(g_fuzz_context.ml_model, nullptr, 1, probs, 32);
                // probabilities=NULL
                kadd_model_predict_batch(g_fuzz_context.ml_model, test_features, 1, nullptr, 32);
                // n<=0
                kadd_model_predict_batch(g_fuzz_context.ml_model, test_features, 0, probs, 32);
                // batch_size<=0
                kadd_model_predict_batch(g_fuzz_context.ml_model, test_features, 1, probs, 0);
                // n>10M
                kadd_model_predict_batch(g_fuzz_context.ml_model, test_features, 10000001, probs, 32);
            }
            break;

        case 3: // DFA Engine - kadd_dfa_engine_create NULL 测试
            {
                kadd_dfa_engine_t *tmp = nullptr;
                kadd_dfa_table_paths_t paths = {
                    g_fuzz_context.config.transition_table,
                    g_fuzz_context.config.output_table,
                    g_fuzz_context.config.eod_table
                };
                // engine=NULL（通过指针传递）
                kadd_dfa_engine_create(nullptr, &paths, 300, 300);
                // table_paths=NULL
                kadd_dfa_engine_create(&tmp, nullptr, 300, 300);
            }
            break;

        case 4: // DFA Engine - kadd_dfa_engine_tokenize_batch NULL 测试
            {
                const char *test_inputs[] = {"test"};
                size_t test_lens[] = {4};
                int *test_outputs[1] = {nullptr};
                int test_dims[1];

                kadd_dfa_batch_t batch = {
                    .inputs = test_inputs,
                    .lengths = test_lens,
                    .count = 1,
                    .outputs = test_outputs,
                    .dims = test_dims,
                    .batch_size = 32
                };

                // engine=NULL
                kadd_dfa_engine_tokenize_batch(nullptr, &batch);
                // batch=NULL
                kadd_dfa_engine_tokenize_batch(g_fuzz_context.dfa_engine, nullptr);
                // batch->inputs=NULL (已覆盖，但显式测试)
                kadd_dfa_batch_t null_batch = {nullptr, test_lens, 1, test_outputs, test_dims, 32};
                kadd_dfa_engine_tokenize_batch(g_fuzz_context.dfa_engine, &null_batch);
            }
            break;

        case 5: // Feature Engine - kadd_seq_to_feature NULL 测试
            {
                int test_tokens[10] = {1,2,3,4,5,6,7,8,9,10};
                int test_features[28] = {0};

                // seq=NULL
                kadd_seq_to_feature(nullptr, 10, 5, test_features, 28);
                // features=NULL
                kadd_seq_to_feature(test_tokens, 10, 5, nullptr, 28);
            }
            break;

        case 6: // Config Loader - kadd_config_load NULL 测试
            {
                kadd_config_t test_config;
                // config_path=NULL
                kadd_config_load(nullptr, &test_config);
                // config=NULL
                kadd_config_load("config.json", nullptr);
            }
            break;

        case 7: // SQLi Engine - kadd_sqli_engine_create_from_file NULL 测试
            {
                kadd_sqli_engine_t *tmp = nullptr;
                // engine=NULL
                kadd_sqli_engine_create_from_file(nullptr, ResolveConfigPath());
                // config_file=NULL
                kadd_sqli_engine_create_from_file(&tmp, nullptr);
            }
            break;

        case 8: // SQLi Engine - kadd_sqli_detect_batch NULL 和边界测试
            {
                const char *test_inputs[] = {"test"};
                float test_probs[1];

                // engine=NULL
                kadd_sqli_detect_batch(nullptr, test_inputs, 1, test_probs, 32);
                // inputs=NULL
                kadd_sqli_detect_batch(g_fuzz_context.sqli_engine, nullptr, 1, test_probs, 32);
                // probabilities=NULL
                kadd_sqli_detect_batch(g_fuzz_context.sqli_engine, test_inputs, 1, nullptr, 32);
                // count<=0
                kadd_sqli_detect_batch(g_fuzz_context.sqli_engine, test_inputs, 0, test_probs, 32);
                kadd_sqli_detect_batch(g_fuzz_context.sqli_engine, test_inputs, -1, test_probs, 32);
                // count>10M
                kadd_sqli_detect_batch(g_fuzz_context.sqli_engine, test_inputs, 10000001, test_probs, 32);
                // batch_size<=0
                kadd_sqli_detect_batch(g_fuzz_context.sqli_engine, test_inputs, 1, test_probs, 0);
                kadd_sqli_detect_batch(g_fuzz_context.sqli_engine, test_inputs, 1, test_probs, -999);
            }
            break;

        case 9: // Inference Engine - NULL 测试
            {
                kadd_inference_engine_free(nullptr);
                float prob = 0.0f;
                float feats[8] = {0.0f};
                kadd_inference_predict(nullptr, feats, &prob);
                kadd_inference_predict_batch(nullptr, feats, 1, &prob, 2);
                kadd_inference_predict_batch(nullptr, nullptr, 1, &prob, 2);
                kadd_inference_engine_get_feature_dim(nullptr);
                kadd_inference_engine_load(nullptr, "config.json", KADD_INFERENCE_TYPE_RF);
                kadd_inference_engine_load_from_path(nullptr, "x.onnx");
            }
            break;

        case 10: // DL Engine - NULL 测试
            {
                kadd_dl_model_free(nullptr);
                float prob = 0.0f;
                float feats[8] = {0.0f};
                kadd_dl_model_predict(nullptr, feats, &prob);
                kadd_dl_model_predict_batch(nullptr, feats, 1, &prob, 2);
                kadd_dl_model_get_feature_dim(nullptr);
                kadd_dl_model_load(nullptr, "x.kcn");
            }
            break;

        case 11: // Feature batch 无效参数边界
            {
                int valid_seq[4] = {1,2,3,4};
                const int *s1[1] = {valid_seq};
                int l1[1] = {4};
                int feats[28] = {0};
                // window_size<=0
                kadd_feature_batch_t wb = {s1, l1, 1, feats, 28, 0, 4};
                kadd_seq_to_feature_batch(&wb);
                // feature_dim 超限
                kadd_feature_batch_t db = {s1, l1, 1, feats, 1000, 5, 4};
                kadd_seq_to_feature_batch(&db);
                // count 超限
                kadd_feature_batch_t cb = {s1, l1, 10000001, feats, 28, 5, 4};
                kadd_seq_to_feature_batch(&cb);
            }
            break;

        default: break;
    }
}

/* ========== DL推理引擎 FUZZ测试 ========== */

/**
 * @brief FUZZ测试DL推理引擎（原生 CNN 优先）
 */
static void FuzzDLEngine(const std::string& input) {
    // 尝试加载 DL 模型（native .kcn 优先）
    kadd_dl_model_t *dl_model = nullptr;
    int ret = 1;
    if (g_fuzz_context.config.model_cnn[0] != '\0') {
        ret = kadd_dl_model_load(&dl_model, g_fuzz_context.config.model_cnn);
    }
    if (ret != 0) {
        ret = kadd_dl_model_load(&dl_model, kCnnKcnPath);
    }
    if (ret != 0) {
        ret = kadd_dl_model_load(&dl_model, kCnnKcnRel);
    }
    if (ret != 0) {
        ret = kadd_dl_model_load(&dl_model, g_fuzz_context.config.model_cnn_onnx);
    }
    if (ret != 0 || !dl_model) return;

    // 测试 kadd_dl_model_get_feature_dim
    int feature_dim = kadd_dl_model_get_feature_dim(dl_model);
    kadd_dl_model_get_feature_dim(nullptr);  // NULL 测试

    // 构造 DL 输入 (float 序列)
    int seq_len = (feature_dim > 0 && feature_dim < 3000) ? feature_dim : 300;
    std::vector<float> dl_input(seq_len, 0.0f);
    for (int i = 0; i < seq_len && i < (int)input.size(); i++) {
        // CNN 原生路径要求 token 为 [0,28] 的整数，这里映射到合法范围
        dl_input[i] = static_cast<float>(static_cast<unsigned char>(input[i]) % 28);
    }

    // 测试单次推理
    float probability = 0.0f;
    kadd_dl_model_predict(dl_model, dl_input.data(), &probability);

    // 非法输入（范围外）触发 CNN 校验错误分支
    std::vector<float> bad_input(seq_len, 999.0f);
    kadd_dl_model_predict(dl_model, bad_input.data(), &probability);

    // NULL 参数测试
    kadd_dl_model_predict(nullptr, dl_input.data(), &probability);
    kadd_dl_model_predict(dl_model, nullptr, &probability);
    kadd_dl_model_predict(dl_model, dl_input.data(), nullptr);

    // 批量推理（多批量大小，覆盖 CNN 分批循环）
    for (size_t bi = 0; bi < K_BATCH_SIZE_COUNT; bi++) {
        int b = kBatchSizes[bi];
        if (b > 128) continue;  // CNN 输入大，控制批量规模
        std::vector<float> batch_in(static_cast<size_t>(seq_len) * b, 0.0f);
        std::vector<float> batch_probs(b);
        for (size_t i = 0; i < batch_in.size(); i++) {
            batch_in[i] = static_cast<float>((int)(i % 28));
        }
        kadd_dl_model_predict_batch(dl_model, batch_in.data(), b, batch_probs.data(), b);
    }

    // 非法批量大小 / n
    float tprobs[1] = {0.0f};
    kadd_dl_model_predict_batch(dl_model, dl_input.data(), 0, tprobs, 2);
    kadd_dl_model_predict_batch(dl_model, dl_input.data(), -1, tprobs, 2);
    kadd_dl_model_predict_batch(dl_model, dl_input.data(), 1, tprobs, 0);
    kadd_dl_model_predict_batch(dl_model, dl_input.data(), 1, tprobs, 2000);

    kadd_dl_model_free(dl_model);
}

/* ========== 推理引擎 FUZZ 测试 ========== */

/**
 * @brief FUZZ测试推理引擎 (kadd_inference_engine.c)
 *
 * 覆盖函数:
 * - kadd_inference_engine_load
 * - kadd_inference_engine_load_from_path
 * - kadd_inference_predict
 * - kadd_inference_predict_batch
 * - kadd_inference_engine_get_feature_dim
 * - kadd_inference_engine_free
 */
static void FuzzInferenceEngine(const uint8_t* data, size_t size) {
    if (size < 8) return;

    // Test 1: NULL 指针测试
    kadd_inference_engine_free(nullptr);

    kadd_inference_engine_t* null_engine = nullptr;
    float null_prob = 0.0f;
    kadd_inference_predict(nullptr, nullptr, &null_prob);
    kadd_inference_predict(null_engine, nullptr, &null_prob);

    float features[10] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};
    kadd_inference_predict(nullptr, features, &null_prob);
    kadd_inference_predict(null_engine, features, nullptr);

    float probs[5] = {0.0f};
    kadd_inference_predict_batch(nullptr, features, 5, probs, 2);
    kadd_inference_predict_batch(null_engine, nullptr, 5, probs, 2);
    kadd_inference_predict_batch(null_engine, features, 5, nullptr, 2);

    kadd_inference_engine_get_feature_dim(nullptr);

    // 非法加载：config 路径 / 参数边界 / 非法 type
    kadd_inference_engine_t* bad_engine = nullptr;
    kadd_inference_engine_load(&bad_engine, "/nonexistent/config.json", KADD_INFERENCE_TYPE_RF);
    kadd_inference_engine_load(nullptr, "config.json", KADD_INFERENCE_TYPE_RF);
    kadd_inference_engine_load(&bad_engine, nullptr, KADD_INFERENCE_TYPE_RF);
    // 非法 type（< RF 或 > CNN）覆盖 INVALID_PARAM 分支
    kadd_inference_engine_load(&bad_engine, ResolveConfigPath(), (kadd_inference_type_t)99);
    if (bad_engine) { kadd_inference_engine_free(bad_engine); bad_engine = nullptr; }
    kadd_inference_engine_load(&bad_engine, ResolveConfigPath(), (kadd_inference_type_t)-1);

    // Test 2: 尝试加载 RF 推理引擎（先通过 config，失败则直接路径）
    kadd_inference_engine_t* rf_engine = nullptr;
    int rf_ret = kadd_inference_engine_load(&rf_engine, ResolveConfigPath(), KADD_INFERENCE_TYPE_RF);

    // 如果 config 加载失败，直接传入已知 ONNX 路径
    if (rf_ret != 0 || rf_engine == nullptr) {
        const char* rf_paths[] = {
            kRfOnnxPath,
            "assets/model/model_tokens_rf.onnx",
            "../assets/model/model_tokens_rf.onnx"
        };
        for (int p = 0; p < 3; p++) {
            rf_ret = kadd_inference_engine_load_from_path(&rf_engine, rf_paths[p]);
            if (rf_ret == 0 && rf_engine != nullptr) break;
        }
    }

    if (rf_ret == 0 && rf_engine != nullptr) {
        // 获取特征维度
        int feature_dim = kadd_inference_engine_get_feature_dim(rf_engine);

        if (feature_dim > 0 && feature_dim < 1000) {
            // 构造测试特征
            std::vector<float> test_features(feature_dim);
            for (int i = 0; i < feature_dim && i < (int)size; i++) {
                test_features[i] = static_cast<float>(data[i % size]) / 255.0f;
            }

            // 测试单次推理
            float probability = 0.0f;
            kadd_inference_predict(rf_engine, test_features.data(), &probability);

            // 测试批量推理（多组 batch_size）
            const int batch_sizes[] = {1, 2, 4, 32};
            for (int bs_idx = 0; bs_idx < 4; bs_idx++) {
                int bs = batch_sizes[bs_idx];
                std::vector<float> batch_features(feature_dim * bs);
                for (int i = 0; i < feature_dim * bs; i++) {
                    batch_features[i] = static_cast<float>(data[i % size]) / 255.0f;
                }
                float batch_probs[32] = {0.0f};
                kadd_inference_predict_batch(rf_engine, batch_features.data(),
                                           bs, batch_probs, bs);
            }

            // 测试边界条件
            float batch_probs_edge[8] = {0.0f};
            kadd_inference_predict_batch(rf_engine, test_features.data(), 0, batch_probs_edge, 2);
            kadd_inference_predict_batch(rf_engine, test_features.data(), -1, batch_probs_edge, 2);
            kadd_inference_predict_batch(rf_engine, test_features.data(), 1, batch_probs_edge, 0);
            kadd_inference_predict_batch(rf_engine, test_features.data(), 1, batch_probs_edge, 2000);
            kadd_inference_predict_batch(rf_engine, test_features.data(), 10000001, batch_probs_edge, 2);
            kadd_inference_predict_batch(rf_engine, nullptr, 1, batch_probs_edge, 2);
            kadd_inference_predict_batch(rf_engine, test_features.data(), 1, nullptr, 2);
        }

        kadd_inference_engine_free(rf_engine);
    }

    // Test 3: 尝试加载 CNN 推理引擎（同样 fallback 直接路径）
    kadd_inference_engine_t* cnn_engine = nullptr;
    int cnn_ret = kadd_inference_engine_load(&cnn_engine, ResolveConfigPath(), KADD_INFERENCE_TYPE_CNN);

    if (cnn_ret != 0 || cnn_engine == nullptr) {
        const char* cnn_paths[] = {
            kCnnOnnxPath,
            "assets/model/model_tokens_dl_cnn.onnx",
            "../assets/model/model_tokens_dl_cnn.onnx"
        };
        for (int p = 0; p < 3; p++) {
            cnn_ret = kadd_inference_engine_load_from_path(&cnn_engine, cnn_paths[p]);
            if (cnn_ret == 0 && cnn_engine != nullptr) break;
        }
    }

    if (cnn_ret == 0 && cnn_engine != nullptr) {
        int feature_dim = kadd_inference_engine_get_feature_dim(cnn_engine);

        if (feature_dim > 0 && feature_dim < 1000) {
            std::vector<float> test_features(feature_dim, 0.5f);
            float probability = 0.0f;
            kadd_inference_predict(cnn_engine, test_features.data(), &probability);

            // CNN 批量推理
            std::vector<float> batch_features(feature_dim * 4, 0.5f);
            float cnn_batch_probs[4] = {0.0f};
            kadd_inference_predict_batch(cnn_engine, batch_features.data(), 4, cnn_batch_probs, 2);
        }

        kadd_inference_engine_free(cnn_engine);
    }

    // Test 4: 从路径加载测试（使用无效路径）
    kadd_inference_engine_t* path_engine = nullptr;
    const char* invalid_paths[] = {
        "",
        "/nonexistent/path/model.onnx",
        "/tmp/invalid_model.onnx",
        "relative/path/model.onnx"
    };

    for (const char* path : invalid_paths) {
        kadd_inference_engine_load_from_path(&path_engine, path);
        if (path_engine != nullptr) {
            kadd_inference_engine_free(path_engine);
            path_engine = nullptr;
        }
    }

    kadd_inference_engine_load_from_path(nullptr, "test.onnx");
    kadd_inference_engine_load_from_path(&path_engine, nullptr);
}

/* ========== 配置加载器 FUZZ 测试 ========== */

// 写入临时配置文件
static void WriteConfigFile(const char *path, const std::string &content) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(content.c_str(), 1, content.size(), f);
    fclose(f);
}

/**
 * @brief FUZZ测试配置加载器 (kadd_config_loader.c)
 * 覆盖：合法 ml/dl 配置、字段缺失、畸形 JSON、错误路径
 * 额外覆盖 SQLi 引擎 DL 模式 create/detect 分支
 */
static void FuzzConfigLoader(const uint8_t* data, size_t size) {
    (void)data; (void)size;

    // ---- 工具：构造 JSON 键值片段 ----
    auto jstr = [](const char *key, const std::string &val) {
        return std::string("\"") + key + "\":\"" + val + "\"";
    };

    // 合法 ML 配置（所有字段齐全）
    {
        std::string ml_cfg = "{" +
            jstr("engine_mode", "ml") + "," +
            jstr("transition_table", kTranPath) + "," +
            jstr("output_table", kOutPath) + "," +
            jstr("eod_table", kEodPath) + "," +
            jstr("model_rf", kRfKrfPath) + "," +
            jstr("model_rf_onnx", kRfOnnxPath) + "," +
            jstr("model_cnn", kCnnKcnPath) + "," +
            jstr("model_cnn_onnx", kCnnOnnxPath) +
            "}";
        const char *ml_path = "/tmp/kadd_fuzz_ml.json";
        WriteConfigFile(ml_path, ml_cfg);

        kadd_config_t cfg;
        if (kadd_config_load(ml_path, &cfg) == 0) {
            // 合法 ML 配置解析成功
        }
        kadd_config_free(&cfg);
    }

    // 合法 DL 配置（覆盖 parse_engine_mode 的 dl 分支）
    {
        std::string dl_cfg = "{" +
            jstr("engine_mode", "dl") + "," +
            jstr("transition_table", kTranPath) + "," +
            jstr("output_table", kOutPath) + "," +
            jstr("eod_table", kEodPath) + "," +
            jstr("model_rf", kRfKrfPath) + "," +
            jstr("model_rf_onnx", kRfOnnxPath) + "," +
            jstr("model_cnn", kCnnKcnPath) + "," +
            jstr("model_cnn_onnx", kCnnOnnxPath) +
            "}";
        const char *dl_path = "/tmp/kadd_fuzz_dl.json";
        WriteConfigFile(dl_path, dl_cfg);

        kadd_config_t cfg;
        if (kadd_config_load(dl_path, &cfg) == 0) {
            // 用 DL 配置创建 SQLi 引擎，覆盖 DL 模式 create/detect 分支
            kadd_sqli_engine_t *dl_sqli = nullptr;
            if (kadd_sqli_engine_create_from_file(&dl_sqli, dl_path) == 0) {
                const char *payloads[] = {
                    "1' OR '1'='1", "UNION SELECT 1,2,3", "admin'--", "normal text"
                };
                for (const char *p : payloads) {
                    float prob = 0.0f;
                    kadd_sqli_detect(dl_sqli, p, &prob);
                }
                // DL 模式批量检测（逐条路径）
                const char *batch_in[] = {"a", "1 OR 1=1", "x"};
                float batch_probs[3] = {0.0f};
                kadd_sqli_detect_batch(dl_sqli, batch_in, 3, batch_probs, 2);
                kadd_sqli_engine_free(dl_sqli);
            }
        }
        kadd_config_free(&cfg);
    }

    // 缺失必需字段（transition_table）
    {
        std::string cfg_s = "{" +
            jstr("engine_mode", "ml") + "," +
            jstr("model_rf", kRfKrfPath) +
            "}";
        const char *path = "/tmp/kadd_fuzz_missing.json";
        WriteConfigFile(path, cfg_s);

        kadd_config_t cfg;
        kadd_config_load(path, &cfg);
        kadd_config_free(&cfg);
    }

    // ML 模式缺少模型（model_rf 与 model_rf_onnx 都缺失）
    {
        std::string cfg_s = "{" +
            jstr("engine_mode", "ml") + "," +
            jstr("transition_table", kTranPath) + "," +
            jstr("output_table", kOutPath) + "," +
            jstr("eod_table", kEodPath) +
            "}";
        const char *path = "/tmp/kadd_fuzz_mlnomodel.json";
        WriteConfigFile(path, cfg_s);

        kadd_config_t cfg;
        kadd_config_load(path, &cfg);
        kadd_config_free(&cfg);
    }

    // DL 模式缺少模型（model_cnn 与 model_cnn_onnx 都缺失）
    {
        std::string cfg_s = "{" +
            jstr("engine_mode", "dl") + "," +
            jstr("transition_table", kTranPath) + "," +
            jstr("output_table", kOutPath) + "," +
            jstr("eod_table", kEodPath) +
            "}";
        const char *path = "/tmp/kadd_fuzz_dlnomodel.json";
        WriteConfigFile(path, cfg_s);

        kadd_config_t cfg;
        kadd_config_load(path, &cfg);
        kadd_config_free(&cfg);
    }

    // 畸形 JSON
    {
        const char *bad_path = "/tmp/kadd_fuzz_bad.json";
        WriteConfigFile(bad_path, std::string("{ not valid json !!!"));

        kadd_config_t cfg;
        kadd_config_load(bad_path, &cfg);
        kadd_config_free(&cfg);
    }

    // engine_mode 非法字符串（默认回落 ML）
    {
        std::string cfg_s = "{" +
            jstr("engine_mode", "xyz") + "," +
            jstr("transition_table", kTranPath) + "," +
            jstr("output_table", kOutPath) + "," +
            jstr("eod_table", kEodPath) + "," +
            jstr("model_rf", kRfKrfPath) +
            "}";
        const char *path = "/tmp/kadd_fuzz_weirdmode.json";
        WriteConfigFile(path, cfg_s);

        kadd_config_t cfg;
        kadd_config_load(path, &cfg);
        kadd_config_free(&cfg);
    }

    // engine_mode 省略（默认 ML）
    {
        std::string cfg_s = "{" +
            jstr("transition_table", kTranPath) + "," +
            jstr("output_table", kOutPath) + "," +
            jstr("eod_table", kEodPath) + "," +
            jstr("model_rf", kRfKrfPath) +
            "}";
        const char *path = "/tmp/kadd_fuzz_nomode.json";
        WriteConfigFile(path, cfg_s);

        kadd_config_t cfg;
        kadd_config_load(path, &cfg);
        kadd_config_free(&cfg);
    }

    // 错误路径（文件不存在 / NULL）
    {
        kadd_config_t cfg;
        kadd_config_load("/nonexistent/kadd_config.json", &cfg);
        kadd_config_load(nullptr, &cfg);
        kadd_config_load("config.json", nullptr);
    }
}

/* ========== SQLi 丰富载荷检测 FUZZ 测试 ========== */

/**
 * @brief 使用真实 SQL 注入载荷 + fuzz 输入驱动检测分支
 * 覆盖阈值判断、长输入、特殊字符、批量变体大小
 */
static void FuzzSQLiEngineRich(const uint8_t* data, size_t size) {
    if (!g_fuzz_context.sqli_engine) return;

    // 真实 SQL 注入载荷集合
    std::vector<std::string> payloads = {
        "' OR 1=1 --",
        "' OR '1'='1",
        "UNION SELECT username,password FROM users",
        "1; DROP TABLE users; --",
        "admin'--",
        "1' AND '1'='1",
        "\"<script>alert(1)</script>\"",
        "%00",
        "'; WAITFOR DELAY '0:0:10'--",
        "1 OR 1=1",
        "INTO OUTFILE '/tmp/x'",
        "SELECT * FROM users WHERE id='1'",
        "x' UNION SELECT @@version--",
        "a\\'b",
        "http://example.com?id=1'",
        "admin' OR 'a'='a",
        "1;--",
        "long " + std::string(200, 'a')
    };

    // 并入 fuzz 输入（截断到合理长度）
    if (size > 0) {
        size_t take = (size > 200) ? 200 : size;
        payloads.push_back(CreateSafeString(data, take));
    }

    // 单个检测（覆盖各种阈值/分支）
    for (const std::string &p : payloads) {
        float prob = 0.0f;
        kadd_sqli_detect(g_fuzz_context.sqli_engine, p.c_str(), &prob);
    }

    // 批量检测（变化批量大小，覆盖 process_batch_loop 不同分片）
    std::vector<const char*> in_c(payloads.size());
    for (size_t i = 0; i < payloads.size(); i++) in_c[i] = payloads[i].c_str();
    std::vector<float> probs(payloads.size());

    const int batch_kinds[] = {1, 2, 3, 4, 8, 16, 32, 64, 128, 0, -1, 2000};
    for (int bk : batch_kinds) {
        int n = (int)in_c.size();
        // 边界情况使用大小足够的 probs 缓冲（n 非法时不读取）
        kadd_sqli_detect_batch(g_fuzz_context.sqli_engine, in_c.data(), n,
                               probs.data(), bk);
    }

    // count 边界
    float tprobs[1] = {0.0f};
    const char *one[1] = {"test"};
    kadd_sqli_detect_batch(g_fuzz_context.sqli_engine, one, 0, tprobs, 32);
    kadd_sqli_detect_batch(g_fuzz_context.sqli_engine, one, -1, tprobs, 32);
    kadd_sqli_detect_batch(g_fuzz_context.sqli_engine, one, 10000001, tprobs, 32);
}

/* ========== ML 原生 RF 大批量 FUZZ 测试 ========== */

/**
 * @brief 加载原生 RF 模型（.krf），驱动 SVE 推理成功路径
 * 覆盖 predict_batch_sve_chunk 的整块/尾块、多分片、批量边界
 */
static void FuzzMLEngineNative(const uint8_t* data, size_t size) {
    // 加载原生 RF 模型
    kadd_model_handle_t *m = nullptr;
    int ret = -1;
    if (g_fuzz_context.config.model_rf[0] != '\0' &&
        strstr(g_fuzz_context.config.model_rf, ".krf") != nullptr) {
        ret = kadd_model_load(&m, g_fuzz_context.config.model_rf, 0);
    }
    if (ret != 0) {
        ret = kadd_model_load(&m, kRfKrfPath, 0);
    }
    if (ret != 0) {
        ret = kadd_model_load(&m, kRfKrfRel, 0);
    }
    if (ret != 0 || !m) return;

    // 获取真实特征维度
    int model_dim = kadd_model_get_feature_dim(m);
    int fd = (model_dim > 0 && model_dim < 1000) ? model_dim : 28;

    // 构造单样本（用 data 填充）
    std::vector<float> sample(static_cast<size_t>(fd), 0.0f);
    for (int i = 0; i < fd; i++) {
        sample[i] = (size > 0) ? static_cast<float>(data[i % size]) / 255.0f
                               : static_cast<float>(i + 1) / 28.0f;
    }

    // 单样本
    float prob = 0.0f;
    kadd_model_predict(m, sample.data(), &prob);

    // 多批量大小（覆盖 SVE 整块/尾块/多分片）
    for (size_t bi = 0; bi < K_BATCH_SIZE_COUNT; bi++) {
        int b = kBatchSizes[bi];
        std::vector<float> mat(static_cast<size_t>(fd) * b);
        for (int i = 0; i < fd * b; i++) {
            mat[i] = static_cast<float>((i % 100)) / 100.0f;
        }
        std::vector<float> out(b);
        kadd_model_predict_batch(m, mat.data(), b, out.data(), b);
    }

    // 边界 / 非法批量
    float tprobs[1] = {0.0f};
    kadd_model_predict_batch(m, sample.data(), 0, tprobs, 32);
    kadd_model_predict_batch(m, sample.data(), -1, tprobs, 32);
    kadd_model_predict_batch(m, sample.data(), 1, tprobs, 0);
    kadd_model_predict_batch(m, sample.data(), 1, tprobs, 2000);
    kadd_model_predict_batch(m, sample.data(), 10000001, tprobs, 32);
    // NULL 参数（走 ml 引擎分发层）
    kadd_model_predict_batch(nullptr, sample.data(), 1, tprobs, 32);
    kadd_model_predict_batch(m, nullptr, 1, tprobs, 32);
    kadd_model_predict_batch(m, sample.data(), 1, nullptr, 32);

    kadd_model_free(m);
}

/* ========== DL 原生 CNN 大批量 FUZZ 测试 ========== */

/**
 * @brief 加载原生 CNN 模型（.kcn），驱动 SVE CNN 推理成功路径
 */
static void FuzzDLEngineNative(const uint8_t* data, size_t size) {
    (void)data; (void)size;
    kadd_dl_model_t *m = nullptr;
    int ret = -1;
    if (g_fuzz_context.config.model_cnn[0] != '\0' &&
        strstr(g_fuzz_context.config.model_cnn, ".kcn") != nullptr) {
        ret = kadd_dl_model_load(&m, g_fuzz_context.config.model_cnn);
    }
    if (ret != 0) {
        ret = kadd_dl_model_load(&m, kCnnKcnPath);
    }
    if (ret != 0) {
        ret = kadd_dl_model_load(&m, kCnnKcnRel);
    }
    if (ret != 0 || !m) return;

    int dim = kadd_dl_model_get_feature_dim(m);
    int fd = (dim > 0 && dim < 1000) ? dim : 300;

    // 合法 token 输入（[0,28] 整数，覆盖 CNN 成功路径）
    std::vector<float> sample(static_cast<size_t>(fd));
    for (int i = 0; i < fd; i++) {
        sample[i] = static_cast<float>(i % 28);
    }

    float prob = 0.0f;
    kadd_dl_model_predict(m, sample.data(), &prob);

    // 多批量大小
    for (size_t bi = 0; bi < K_BATCH_SIZE_COUNT; bi++) {
        int b = kBatchSizes[bi];
        if (b > 128) continue;  // 控制内存
        std::vector<float> mat(static_cast<size_t>(fd) * b);
        for (int i = 0; i < fd * b; i++) {
            mat[i] = static_cast<float>(i % 28);
        }
        std::vector<float> out(b);
        kadd_dl_model_predict_batch(m, mat.data(), b, out.data(), b);
    }

    // 边界 / 非法批量
    float tprobs[1] = {0.0f};
    kadd_dl_model_predict_batch(m, sample.data(), 0, tprobs, 2);
    kadd_dl_model_predict_batch(m, sample.data(), -1, tprobs, 2);
    kadd_dl_model_predict_batch(m, sample.data(), 1, tprobs, 0);
    kadd_dl_model_predict_batch(m, sample.data(), 1, tprobs, 2000);
    kadd_dl_model_predict_batch(nullptr, sample.data(), 1, tprobs, 2);
    kadd_dl_model_predict_batch(m, nullptr, 1, tprobs, 1);
    kadd_dl_model_predict_batch(m, sample.data(), 1, nullptr, 2);

    kadd_dl_model_free(m);
}

/* ========== 特征引擎批量/边界 FUZZ 测试 ========== */

/**
 * @brief 覆盖特征引擎批量转换 + window/seq_len/feature_dim 边界
 */
static void FuzzFeatureEngineBatch(const uint8_t* data, size_t size) {
    // 有效 token 序列（<28）
    int seq_a[30] = {0};
    size_t mod = (size > 0) ? size : 1;  // 防止 size==0 时除零
    int len_a = (size > 30) ? 30 : (int)size;
    if (len_a <= 0) len_a = 8;
    for (int i = 0; i < len_a; i++) {
        seq_a[i] = data[i % mod] % 28;
    }

    // 单条：window_size 边界
    {
        int feats[100] = {0};
        int windows[] = {0, 1, 2, 5, 99, 100, 101, -1};
        for (int w : windows) {
            int len = (len_a < 100) ? len_a : 100;
            kadd_seq_to_feature(seq_a, len, w, feats, 28);
        }
    }

    // seq_len 边界（0/1/300已覆盖，这里测 301 超限报错）
    {
        int feats[100] = {0};
        int over[301] = {0};
        kadd_seq_to_feature(over, 301, 5, feats, 28);
    }

    // feature_dim 边界
    {
        int feats[100] = {0};
        int dims[] = {0, 1, 27, 28, 29, 99, 100, 999, 1000, 1001};
        for (int d : dims) {
            int len = (len_a < 100) ? len_a : 100;
            // 只对合法 dim（>0 且 <=300）且 buffer 足够时执行
            if (d >= 0 && d <= 100) {
                kadd_seq_to_feature(seq_a, len, 5, feats, d);
            }
        }
    }

    // token >= feature_dim 错误路径（feature_dim 较小时）
    {
        int bad_tokens[8] = {0, 1, 28, 29, 100, 7, 3, 27};
        int feats[8] = {0};
        kadd_seq_to_feature(bad_tokens, 8, 5, feats, 8);
        kadd_seq_to_feature(bad_tokens, 8, 5, feats, 28);
    }

    // 批量：多条 + 不同 window/feature_dim
    {
        int seq_b[20] = {0};
        int len_b = (size > 20) ? 20 : (int)size;
        if (len_b <= 0) len_b = 6;
        for (int i = 0; i < len_b; i++) seq_b[i] = data[i % mod] % 28;

        const int *seqs[2] = {seq_a, seq_b};
        int lens[2] = {len_a, len_b};
        int feats[64] = {0};  // 2 * 28
        kadd_feature_batch_t valid_batch = {
            seqs, lens, 2, feats, 28, 5, 1
        };
        kadd_seq_to_feature_batch(&valid_batch);

        // window_size 变化
        kadd_feature_batch_t wb = {seqs, lens, 2, feats, 28, 1, 1};
        kadd_seq_to_feature_batch(&wb);
        kadd_feature_batch_t wb2 = {seqs, lens, 2, feats, 28, 100, 1};
        kadd_seq_to_feature_batch(&wb2);

        // 批量包含 NULL seq（触发错误分支）
        const int *seqs_null[2] = {nullptr, seq_b};
        kadd_feature_batch_t nb = {seqs_null, lens, 2, feats, 28, 5, 1};
        kadd_seq_to_feature_batch(&nb);

        // 批量 seq_len<=0（触发错误分支）
        int zero_lens[2] = {0, len_b};
        kadd_feature_batch_t zb = {seqs, zero_lens, 2, feats, 28, 5, 1};
        kadd_seq_to_feature_batch(&zb);
    }
}

/* ========== ONNX 推理引擎真实维度 FUZZ 测试 ========== */

/**
 * @brief 从配置文件加载 RF/CNN ONNX，使用真实特征维度做批量推理
 */
static void FuzzInferenceEngineONNX(const uint8_t* data, size_t size) {
    (void)data; (void)size;
    // RF ONNX
    kadd_inference_engine_t *rf = nullptr;
    int ret = kadd_inference_engine_load(&rf, ResolveConfigPath(), KADD_INFERENCE_TYPE_RF);
    if (ret != 0 || !rf) {
        ret = kadd_inference_engine_load_from_path(&rf, kRfOnnxPath);
    }

    if (ret == 0 && rf) {
        int fd = kadd_inference_engine_get_feature_dim(rf);
        if (fd > 0 && fd < 1000) {
            std::vector<float> sample(fd, 0.0f);
            float prob = 0.0f;
            kadd_inference_predict(rf, sample.data(), &prob);

            const int bs[] = {1, 2, 4, 8, 16, 32, 64};
            for (int b : bs) {
                std::vector<float> mat(fd * b, 0.1f);
                std::vector<float> out(b);
                kadd_inference_predict_batch(rf, mat.data(), b, out.data(), b);
            }
        }
        kadd_inference_engine_free(rf);
    }

    // CNN ONNX
    kadd_inference_engine_t *cnn = nullptr;
    ret = kadd_inference_engine_load(&cnn, ResolveConfigPath(), KADD_INFERENCE_TYPE_CNN);
    if (ret != 0 || !cnn) {
        ret = kadd_inference_engine_load_from_path(&cnn, kCnnOnnxPath);
    }

    if (ret == 0 && cnn) {
        int fd = kadd_inference_engine_get_feature_dim(cnn);
        if (fd > 0 && fd < 1000) {
            std::vector<float> sample(fd, 0.5f);
            float prob = 0.0f;
            kadd_inference_predict(cnn, sample.data(), &prob);

            const int bs[] = {1, 2, 4, 16};
            for (int b : bs) {
                std::vector<float> mat(fd * b, 0.5f);
                std::vector<float> out(b);
                kadd_inference_predict_batch(cnn, mat.data(), b, out.data(), b);
            }
        }
        kadd_inference_engine_free(cnn);
    }
}

/* ========== 原生模型文件构造辅助（用于覆盖加载错误分支） ========== */

// CRC-32 (IEEE, reflected 0xEDB88320)，与 kadd_rf_engine.c / kadd_cnn_engine.c 一致
static uint32_t FuzzCrc32(const unsigned char *data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static void FuzzPutU32LE(std::vector<unsigned char> &b, uint32_t v) {
    b.push_back((unsigned char)(v & 0xFFu));
    b.push_back((unsigned char)((v >> 8) & 0xFFu));
    b.push_back((unsigned char)((v >> 16) & 0xFFu));
    b.push_back((unsigned char)((v >> 24) & 0xFFu));
}

static void FuzzPutU64LE(std::vector<unsigned char> &b, uint64_t v) {
    FuzzPutU32LE(b, (uint32_t)(v & 0xFFFFFFFFu));
    FuzzPutU32LE(b, (uint32_t)((v >> 32) & 0xFFFFFFFFu));
}

static void FuzzPutF64LE(std::vector<unsigned char> &b, double v) {
    union {
        double   d;
        uint64_t u;
    } conv;
    conv.d = v;
    FuzzPutU64LE(b, conv.u);
}

/**
 * @brief 构造一个 KADD-RF v1 模型文件（最小 1 树 1 叶节点）。
 * feature_count 可变，crc_corrupt 为 true 时写入错误 CRC 以触发校验失败。
 * 返回文件总大小（供调用方参考）。
 */
static size_t FuzzWriteRfModel(const char *path, uint32_t feature_count,
                               bool crc_corrupt) {
    std::vector<unsigned char> f;
    const unsigned char magic[8] = {'K', 'A', 'D', 'D', 'R', 'F', '1', '\0'};
    f.insert(f.end(), magic, magic + 8);
    FuzzPutU32LE(f, 1u);   // version
    FuzzPutU32LE(f, 64u);  // header_size
    FuzzPutU32LE(f, 32u);  // node_size
    FuzzPutU32LE(f, feature_count);
    FuzzPutU32LE(f, 2u);   // class_count
    FuzzPutU32LE(f, 1u);   // positive_class_index
    FuzzPutU32LE(f, 1u);   // tree_count
    FuzzPutU32LE(f, 1u);   // node_count
    FuzzPutU32LE(f, 0u);   // flags
    const size_t offsets_size = (1u + 1u) * sizeof(uint32_t);
    const size_t payload_size = offsets_size + 1u * 32u;
    FuzzPutU64LE(f, (uint64_t)payload_size);
    const size_t crc_pos = f.size();
    FuzzPutU32LE(f, 0u);   // crc (占位，稍后回填)
    FuzzPutU32LE(f, 0u);   // reserved 56
    FuzzPutU32LE(f, 0u);   // reserved 60

    // payload: tree_offsets[0]=0, tree_offsets[1]=node_count(1)
    FuzzPutU32LE(f, 0u);
    FuzzPutU32LE(f, 1u);
    // 单个叶节点：feature=-1, left=right=UINT32_MAX, prob=0.5
    FuzzPutU32LE(f, 0xFFFFFFFFu);  // feature = -1 (int32)
    FuzzPutU32LE(f, 0xFFFFFFFFu);  // left
    FuzzPutU32LE(f, 0xFFFFFFFFu);  // right
    FuzzPutU32LE(f, 0u);           // reserved
    FuzzPutF64LE(f, 0.5);          // threshold (叶节点忽略)
    FuzzPutF64LE(f, 0.5);          // positive_probability

    uint32_t crc = FuzzCrc32(f.data() + 64, payload_size);
    if (crc_corrupt) {
        crc = 0x12345678u;
    }
    f[crc_pos + 0] = (unsigned char)(crc & 0xFFu);
    f[crc_pos + 1] = (unsigned char)((crc >> 8) & 0xFFu);
    f[crc_pos + 2] = (unsigned char)((crc >> 16) & 0xFFu);
    f[crc_pos + 3] = (unsigned char)((crc >> 24) & 0xFFu);

    FILE *fp = fopen(path, "wb");
    if (fp) {
        fwrite(f.data(), 1, f.size(), fp);
        fclose(fp);
    }
    return f.size();
}

/**
 * @brief 构造一个 KADD-CNN v1 模型文件（完整合法头）。用于触发错误分支。
 * 仅写 64 字节头 + 一个无效 payload（CRC 必错/长度必错）。
 */
static void FuzzWriteCnnHeaderFile(const char *path) {
    std::vector<unsigned char> f;
    const unsigned char magic[8] = {'K', 'A', 'D', 'D', 'C', 'N', '1', '\0'};
    f.insert(f.end(), magic, magic + 8);
    FuzzPutU32LE(f, 1u);   // version
    FuzzPutU32LE(f, 64u);  // header_size
    FuzzPutU32LE(f, 300u); // sequence_length
    FuzzPutU32LE(f, 29u);  // vocab_size
    FuzzPutU32LE(f, 32u);  // embedding_dim
    FuzzPutU32LE(f, 64u);  // channel_count
    FuzzPutU32LE(f, 4u);   // branch_count
    FuzzPutU32LE(f, 64u);  // hidden_dim
    FuzzPutU32LE(f, 2u);   // class_count
    FuzzPutU32LE(f, 28u);  // pad_id
    FuzzPutU64LE(f, 32u);  // payload_size (故意与实际不匹配)
    FuzzPutU32LE(f, 0u);   // crc
    FuzzPutU32LE(f, 0u);   // reserved 60
    f.resize(128, 0);      // 补足一些字节（>60），payload 大小不匹配 → 错误分支
    FILE *fp = fopen(path, "wb");
    if (fp) {
        fwrite(f.data(), 1, f.size(), fp);
        fclose(fp);
    }
}

/* ========== ML 引擎加载变体 FUZZ（覆盖 onnx 回退 + 特征维度不匹配） ========== */

/**
 * @brief 覆盖 kadd_model_load 的分发分支：
 * - .onnx 路径 → backend=0（onnx 回退，行 66-85）
 * - .krf 路径 → backend=1（RF 原生，行 56-61）
 * - 错误 feature_dim 触发 RF 不匹配（行 48-55）与 ONNX 不匹配（行 71-79）
 */
static void FuzzMLLoadVariants(const uint8_t *data, size_t size) {
    (void)data; (void)size;

    // backend=0：直接加载 ONNX 模型并驱动 predict/predict_batch/free
    {
        kadd_model_handle_t *m = nullptr;
        int ret = kadd_model_load(&m, kRfOnnxPath, 0);
        if (ret != 0 || m == nullptr) {
            ret = kadd_model_load(&m, "assets/model/model_tokens_rf.onnx", 0);
        }
        if (m) {
            int fd = kadd_model_get_feature_dim(m);
            int n = (fd > 0 && fd < 1000) ? fd : 28;
            std::vector<float> feats((size_t)n, 0.1f);
            float prob = 0.0f;
            kadd_model_predict(m, feats.data(), &prob);
            // 多批量大小覆盖 ONNX 分批
            const int bs[] = {1, 2, 8, 64, 0, -1, 2000};
            for (int b : bs) {
                std::vector<float> mat((size_t)n * (size_t)((b > 0 && b <= 1024) ? b : 4), 0.1f);
                std::vector<float> out((b > 0 && b <= 1024) ? (size_t)b : 4, 0.0f);
                kadd_model_predict_batch(m, mat.data(), (b > 0 && b <= 1024) ? b : 4, out.data(), b);
            }
            kadd_model_free(m);
        } else {
            // 加载失败也不会泄漏
        }
    }

    // backend=1：加载原生 RF，单样本 + 指定正确维度
    {
        kadd_model_handle_t *m = nullptr;
        if (kadd_model_load(&m, kRfKrfPath, 28) == 0 && m) {
            float feats[28] = {0.5f};
            float prob = 0.0f;
            kadd_model_predict(m, feats, &prob);
            kadd_model_free(m);
        }
    }

    // RF 特征维度不匹配（line 48-55）：带了+非0维度 != 28
    {
        kadd_model_handle_t *m = nullptr;
        int r = kadd_model_load(&m, kRfKrfPath, 99944);
        (void)r;
        if (m) kadd_model_free(m);
    }

    // ONNX 特征维度不匹配（line 71-79）
    {
        kadd_model_handle_t *m = nullptr;
        int r = kadd_model_load(&m, kRfOnnxPath, 99944);
        (void)r;
        if (m) kadd_model_free(m);
    }

    // 加载失败路径：不存在文件 → RF 失败后 ONNX 也失败（line 66-69）
    {
        kadd_model_handle_t *m = nullptr;
        kadd_model_load(&m, "/nonexistent/kadd_model.krf", 0);
        kadd_model_load(&m, "/nonexistent/kadd_model.onnx", 0);
    }

    // NULL 参数（line 33-35）
    {
        kadd_model_handle_t *m = nullptr;
        kadd_model_load(nullptr, kRfKrfPath, 0);
        kadd_model_load(&m, nullptr, 0);
    }
}

/* ========== RF 引擎直接 FUZZ（覆盖加载错误分支 + 直接预测校验） ========== */

/**
 * @brief 直接调用 kadd_rf_model_load / predict / predict_batch / free，
 * 覆盖坏 magic、短文件、feature_count=0、CRC 失配、以及 NULL 校验分支。
 */
static void FuzzRFEngineDirect(const uint8_t *data, size_t size) {
    (void)data; (void)size;

    // NULL 参数（kadd_rf_model_load 行 291-293）
    {
        kadd_rf_model_t *m = nullptr;
        kadd_rf_model_load(nullptr, kRfKrfPath);
        kadd_rf_model_load(&m, nullptr);
        kadd_rf_model_load(nullptr, nullptr);
    }
    // free / get_feature_dim NULL（行 617-619、634-636）
    kadd_rf_model_free(nullptr);
    kadd_rf_model_get_feature_dim(nullptr);

    // 直接 predict / predict_batch NULL 校验（行 429-431、582-584）
    {
        float f[28] = {0.0f};
        float prob = 0.0f;
        kadd_rf_model_predict(nullptr, f, &prob);
        kadd_rf_model_predict((kadd_rf_model_t *)1, nullptr, &prob);
        kadd_rf_model_predict((kadd_rf_model_t *)1, f, nullptr);
        kadd_rf_model_predict_batch(nullptr, f, 1, &prob, 1);
        kadd_rf_model_predict_batch((kadd_rf_model_t *)1, nullptr, 1, &prob, 1);
        kadd_rf_model_predict_batch((kadd_rf_model_t *)1, f, 1, nullptr, 1);
        // 非法 sample_count / batch_size（行 585-589）
        kadd_rf_model_predict_batch((kadd_rf_model_t *)1, f, 0, &prob, 1);
        kadd_rf_model_predict_batch((kadd_rf_model_t *)1, f, -1, &prob, 1);
        kadd_rf_model_predict_batch((kadd_rf_model_t *)1, f, 1, &prob, 0);
        kadd_rf_model_predict_batch((kadd_rf_model_t *)1, f, 10000001, &prob, 1);
    }

    // 不存在的文件（read_entire_file 行 122-126）
    {
        kadd_rf_model_t *m = nullptr;
        kadd_rf_model_load(&m, "/nonexistent/kadd_nope.krf");
    }
    // 短文件 < 64 字节（行 134-137）
    {
        WriteConfigFile("/tmp/kadd_fuzz_rf_short.bin", std::string(10, '\0'));
        kadd_rf_model_t *m = nullptr;
        kadd_rf_model_load(&m, "/tmp/kadd_fuzz_rf_short.bin");
    }
    // >=64 字节但坏 magic/version（行 302-310）
    {
        std::string bm(100, '\0');
        WriteConfigFile("/tmp/kadd_fuzz_rf_magic.bin", bm);
        kadd_rf_model_t *m = nullptr;
        kadd_rf_model_load(&m, "/tmp/kadd_fuzz_rf_magic.bin");
    }
    // 合法头但 feature_count=0（行 319-327）
    {
        FuzzWriteRfModel("/tmp/kadd_fuzz_rf_fcnt.bin", 0, false);
        kadd_rf_model_t *m = nullptr;
        kadd_rf_model_load(&m, "/tmp/kadd_fuzz_rf_fcnt.bin");
    }
    // 合法头+payload 但 CRC 失配（行 345-349）
    {
        FuzzWriteRfModel("/tmp/kadd_fuzz_rf_crc.bin", 28, true);
        kadd_rf_model_t *m = nullptr;
        kadd_rf_model_load(&m, "/tmp/kadd_fuzz_rf_crc.bin");
    }
    // 完全合法模型 → 成功路径 + 直接 predict/predict_batch（行 351-405）
    {
        FuzzWriteRfModel("/tmp/kadd_fuzz_rf_ok.bin", 28, false);
        kadd_rf_model_t *m = nullptr;
        if (kadd_rf_model_load(&m, "/tmp/kadd_fuzz_rf_ok.bin") == 0 && m) {
            float f[28] = {0.5f};
            for (int i = 0; i < 28; i++) f[i] = (float)(i % 7) / 7.0f;
            float prob = 0.0f;
            kadd_rf_model_predict(m, f, &prob);
            // 多种批量大小（含非 SVE 倍数尾块）
            const int bs[] = {1, 2, 31, 32, 33, 64, 65, 128};
            for (int b : bs) {
                if (b > 512) continue;
                std::vector<float> mat((size_t)28 * (size_t)b, 0.1f);
                std::vector<float> out((size_t)b, 0.0f);
                kadd_rf_model_predict_batch(m, mat.data(), b, out.data(), b);
            }
            // batch_size > 1024 → 重置为默认 64
            std::vector<float> mat2((size_t)28 * 4, 0.1f);
            float out2[4] = {0.0f};
            kadd_rf_model_predict_batch(m, mat2.data(), 4, out2, 2000);
            kadd_rf_model_free(m);
        } else if (m) {
            kadd_rf_model_free(m);
        }
    }
}

/* ========== CNN 引擎直接 FUZZ（覆盖加载错误分支 + token 校验） ========== */

/**
 * @brief 直接调用 kadd_cnn_model_load / predict / predict_batch，
 * 覆盖坏文件/短文件/坏 magic 加载失败，以及非法 token 校验和 NULL 分支。
 */
static void FuzzCNNEngineDirect(const uint8_t *data, size_t size) {
    (void)data; (void)size;

    // NULL 参数（load 行 437-441）
    {
        kadd_cnn_model_t *m = nullptr;
        kadd_cnn_model_load(nullptr, kCnnKcnPath);
        kadd_cnn_model_load(&m, nullptr);
        kadd_cnn_model_load(nullptr, nullptr);
    }
    kadd_cnn_model_free(nullptr);

    // 直接 predict / predict_batch NULL 校验（行 975-977、1020-1022）
    {
        float f[300] = {0.0f};
        float prob = 0.0f;
        kadd_cnn_model_predict(nullptr, f, &prob);
        kadd_cnn_model_predict((kadd_cnn_model_t *)1, nullptr, &prob);
        kadd_cnn_model_predict((kadd_cnn_model_t *)1, f, nullptr);
        kadd_cnn_model_predict_batch(nullptr, f, 1, &prob, 1);
        kadd_cnn_model_predict_batch((kadd_cnn_model_t *)1, nullptr, 1, &prob, 1);
        kadd_cnn_model_predict_batch((kadd_cnn_model_t *)1, f, 1, nullptr, 1);
        // 非法 sample_count / batch_size（行 1023-1026）
        kadd_cnn_model_predict_batch((kadd_cnn_model_t *)1, f, 0, &prob, 1);
        kadd_cnn_model_predict_batch((kadd_cnn_model_t *)1, f, -1, &prob, 1);
        kadd_cnn_model_predict_batch((kadd_cnn_model_t *)1, f, 1, &prob, 0);
        kadd_cnn_model_predict_batch((kadd_cnn_model_t *)1, f, 10000001, &prob, 1);
    }

    // 不存在的文件（read_entire_file 行 181-186）
    {
        kadd_cnn_model_t *m = nullptr;
        kadd_cnn_model_load(&m, "/nonexistent/kadd_nope.kcn");
    }
    // 短文件 < 64 字节（行 194-198）
    {
        WriteConfigFile("/tmp/kadd_fuzz_cnn_short.bin", std::string(10, '\0'));
        kadd_cnn_model_t *m = nullptr;
        kadd_cnn_model_load(&m, "/tmp/kadd_fuzz_cnn_short.bin");
    }
    // >=64 字节但坏 magic（行 459-474）
    {
        std::string bm(100, '\0');
        WriteConfigFile("/tmp/kadd_fuzz_cnn_magic.bin", bm);
        kadd_cnn_model_t *m = nullptr;
        kadd_cnn_model_load(&m, "/tmp/kadd_fuzz_cnn_magic.bin");
    }
    // 合法头但 payload 大小/CRC 不匹配（行 478-495）
    {
        FuzzWriteCnnHeaderFile("/tmp/kadd_fuzz_cnn_header.bin");
        kadd_cnn_model_t *m = nullptr;
        kadd_cnn_model_load(&m, "/tmp/kadd_fuzz_cnn_header.bin");
    }

    // 加载合法原生模型 → 成功路径 + token 校验 + 批量
    {
        kadd_cnn_model_t *m = nullptr;
        int ret = kadd_cnn_model_load(&m, kCnnKcnPath);
        if (ret != 0 || m == nullptr) {
            ret = kadd_cnn_model_load(&m, "assets/model/model_tokens_dl_cnn.kcn");
        }
        if (m) {
            const int dim = 300;
            // 合法 token [0,28] 整数
            std::vector<float> valid((size_t)dim);
            for (int i = 0; i < dim; i++) valid[(size_t)i] = (float)(i % 28);
            float prob = 0.0f;
            kadd_cnn_model_predict(m, valid.data(), &prob);

            // 非法 token：越界 / 非整数 / 负 / NaN（decode_tokens 行 526-538）
            std::vector<float> bad((size_t)dim, 999.0f);
            kadd_cnn_model_predict(m, bad.data(), &prob);
            std::fill(bad.begin(), bad.end(), 1.5f);
            kadd_cnn_model_predict(m, bad.data(), &prob);
            std::fill(bad.begin(), bad.end(), -1.0f);
            kadd_cnn_model_predict(m, bad.data(), &prob);
            std::fill(bad.begin(), bad.end(),
                      std::numeric_limits<float>::quiet_NaN());
            kadd_cnn_model_predict(m, bad.data(), &prob);

            // 批量（含尾块 + 边界 batch_size）
            float out[16] = {0.0f};
            for (int b : {1, 2, 3, 7, 8, 16}) {
                std::vector<float> mat((size_t)dim * (size_t)b);
                for (size_t i = 0; i < mat.size(); i++) mat[i] = (float)((int)(i % 28));
                kadd_cnn_model_predict_batch(m, mat.data(), b, out, b);
            }
            kadd_cnn_model_predict_batch(m, valid.data(), 4, out, 2000);
            kadd_cnn_model_free(m);
        } else if (m) {
            kadd_cnn_model_free(m);
        }
    }
}

/* ========== DL 引擎 ONNX 后端 FUZZ（backend=0） ========== */

/**
 * @brief 直接加载 DL .onnx 模型，覆盖 kadd_dl_model_load ONNX 回退
 * 以及 backend=0 的 predict / predict_batch / get_feature_dim / free。
 */
static void FuzzDLLoadOnnxBackend(const uint8_t *data, size_t size) {
    (void)data; (void)size;

    kadd_dl_model_t *m = nullptr;
    int ret = kadd_dl_model_load(&m, kCnnOnnxPath);
    if (ret != 0 || m == nullptr) {
        ret = kadd_dl_model_load(&m, "assets/model/model_tokens_dl_cnn.onnx");
    }
    // 无论 onnx 还是空路径都补测 NULL / 失败路径
    if (ret == 0 && m) {
        int fd = kadd_dl_model_get_feature_dim(m);
        int n = (fd > 0 && fd < 1000) ? fd : 300;
        std::vector<float> in((size_t)n, 0.5f);
        float prob = 0.0f;
        kadd_dl_model_predict(m, in.data(), &prob);
        // 多批量大小
        const int bs[] = {1, 2, 8, 64, 0, -1, 2000};
        for (int b : bs) {
            int nb = (b > 0 && b <= 1024) ? b : 4;
            std::vector<float> mat((size_t)n * (size_t)nb, 0.5f);
            std::vector<float> out((size_t)nb, 0.0f);
            kadd_dl_model_predict_batch(m, mat.data(), nb, out.data(), b);
        }
        kadd_dl_model_free(m);
    } else if (m) {
        kadd_dl_model_free(m);
    }

    // NULL 参数（load 行 37-39）
    kadd_dl_model_load(nullptr, kCnnOnnxPath);
    kadd_dl_model_load(nullptr, nullptr);
    float prob = 0.0f;
    float in2[8] = {0.0f};
    // 注：DL wrapper 在 NULL 校验后会解引用 model->backend，
    // 因此这里只对"指针为 NULL"的参数分支用假指针做 NULL 校验，
    // 非法 n/batch 已在上方真实模型块与 FuzzDLEngine 中覆盖。
    kadd_dl_model_predict((kadd_dl_model_t *)1, nullptr, &prob);
    kadd_dl_model_predict((kadd_dl_model_t *)1, in2, nullptr);
    kadd_dl_model_predict_batch((kadd_dl_model_t *)1, nullptr, 1, &prob, 1);
    kadd_dl_model_get_feature_dim(nullptr);
}

/* ========== LibFuzzer主入口函数 ========== */

/**
 * @brief LibFuzzer目标函数
 *
 * @param data FUZZ输入数据
 * @param size 数据大小
 * @return 0 表示成功
 */
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > 4096) {
        return 0;
    }

    InitializeEngines();

    // 用首字节选择执行哪个fuzz函数，避免每次全跑
    uint8_t selector = data[0];
    const uint8_t *payload = data + 1;
    size_t payload_size = size - 1;
    std::string input = CreateSafeString(payload, payload_size);

    switch (selector % 26) {  // 26个主分支
        case 0: FuzzSQLiEngine(input); break;
        case 1: FuzzDFAEngine(input); break;
        case 2: FuzzFeatureEngine(input); break;
        case 3: FuzzFeatureEngineLong(payload, payload_size); break;
        case 4: FuzzFeatureEngineOutOfRange(payload, payload_size); break;
        case 5: FuzzMLEngine(input); break;
        case 6: FuzzEdgeCases(payload, payload_size); break;
        case 7: FuzzBatchDetection(payload, payload_size); break;
        case 8: FuzzBoundaryConditions(payload, payload_size); break;
        case 9: FuzzInitFailures(payload, payload_size); break;
        case 10: FuzzSQLiEngineFree(data, size); break;
        case 11: FuzzBatchErrorPaths(); break;
        case 12: FuzzNullPointers(data, size); break;
        case 13: FuzzDLEngine(input); break;
        case 14: FuzzInferenceEngine(data, size); break;
        case 15: FuzzConfigLoader(payload, payload_size); break;
        case 16: FuzzSQLiEngineRich(payload, payload_size); break;
        case 17: FuzzMLEngineNative(payload, payload_size); break;
        case 18: FuzzDLEngineNative(payload, payload_size); break;
        case 19: FuzzFeatureEngineBatch(payload, payload_size); break;
        case 20: FuzzInferenceEngineONNX(payload, payload_size); break;
        case 21: FuzzMLLoadVariants(payload, payload_size); break;
        case 22: FuzzRFEngineDirect(payload, payload_size); break;
        case 23: FuzzCNNEngineDirect(payload, payload_size); break;
        case 24: FuzzDLLoadOnnxBackend(payload, payload_size); break;
        default: break;  // 防御性编程
    }

    // 附加调度：用后续字节给低覆盖引擎（ML/DL/SQLi/feature/config）增加触发权重，
    // 使这些引擎被高频调用以提升行覆盖率
    if (size > 1 && data[1] % 2 == 0) {
        FuzzMLEngineNative(payload, payload_size);
    }
    if (size > 2 && data[2] % 2 == 0) {
        FuzzDLEngineNative(payload, payload_size);
    }
    if (size > 3 && data[3] % 3 == 0) {
        FuzzSQLiEngineRich(payload, payload_size);
    }
    if (size > 4 && data[4] % 3 == 0) {
        FuzzFeatureEngineBatch(payload, payload_size);
    }
    if (size > 5 && data[5] % 5 == 0) {
        FuzzConfigLoader(payload, payload_size);
    }
    // 新增：高权重触发原生 RF/CNN 直接加载变体，覆盖 ML/RF/DL/CNN 缺失分支
    if (size > 6 && data[6] % 2 == 0) {
        FuzzRFEngineDirect(payload, payload_size);
    }
    if (size > 7 && data[7] % 2 == 0) {
        FuzzCNNEngineDirect(payload, payload_size);
    }
    if (size > 8 && data[8] % 3 == 0) {
        FuzzMLLoadVariants(payload, payload_size);
    }
    if (size > 9 && data[9] % 3 == 0) {
        FuzzDLLoadOnnxBackend(payload, payload_size);
    }

    return 0;
}

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv) {
    // 提前初始化引擎
    InitializeEngines();
    return 0;
}
