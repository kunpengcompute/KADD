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
 * - 通过 selector 分层分派：公共 API 层 ≈85% / 内部 API 层 ≈12% / 自由输入层 ≈3%
 * - 所有模型类用例内部为完整生命周期：load → predict → predict_batch → free（1:1:1:1）
 * - 加载远端原生模型（.krf/.kcn）驱动 SVE 成功路径（真实特征维度）
 * - 多批量大小 + 边界/非法批量大小覆盖分批循环与 SVE 尾块
 * - 真实 SQL 注入载荷 + 随机输入覆盖检测分支
 * - 写临时 config JSON 覆盖配置校验分支（ml/dl 模式、字段缺失、畸形 JSON）
 * - KADD_FUZZ_EQUAL_RANDOM=1：selector 随机 fuzz 全保留，另叠加 21 个可长跑
 *   开放接口的等频真实随机调用层（生命周期类 17 接口豁免，见下方说明）
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
#include <chrono>     /* 吞吐自监控：稳态 exec/s 与初始化耗时 */
#if defined(_WIN32)
#include <process.h>  /* _getpid：状态文件名按进程区分 */
#else
#include <unistd.h>   /* getpid：状态文件名按进程区分（多 worker 并行互不覆盖） */
#endif

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
#include "test_model_hmac_key.h"   /* [FIX REPORT-2026-0000028] 注入 .krf/.kcn 共用 HMAC 开发密钥 */
#include "kadd_errors.h"
#include "kadd_model_auth.h"   /* 合成 .krf 模型的 HMAC-SHA256 签名辅助 */

/* ORT 故障注入（可选，默认 ON）：把推理引擎源文件内联进本 TU，
 * 配合 kadd_inference_fault_inject.h 覆盖 ORT 错误状态清理分支（约 25 行）。
 * 覆盖率构建为静态库：归档不会重复拉取 kadd_inference_engine.c.o，无重复定义；
 * 动态库构建下可执行文件符号优先，行为一致。构建异常时可用
 * -DKADD_FUZZ_INFERENCE_INJECT=OFF 关闭（coverage_all.sh 会自动重试）。 */
#ifdef KADD_FUZZ_INFERENCE_INJECT
#include "kadd_inference_engine.c"
#include "kadd_inference_fault_inject.h"
#endif

/* UT/FUZZ 共用错误路径用例（feature/config/dfa/rf/cnn/dl/sqli 的参数校验、
 * 文件解析、HMAC 认证失败分支；由本地 gcov 实测验证过的用例转换而来，
 * tests/test_error_paths_gtest.cpp 包含同一文件以保证 UT 报告同样命中） */
/* 声明本 TU 是 FUZZ 侧：error_paths_cases.inc 里的「真实资产闸门」
 * FzAllowRealAsset() 据此对重用例按 1/64 放行（UT 侧不定义此宏，恒放行）。
 * 必须在 include 之前定义。 */
#define KADD_ERROR_PATHS_IN_FUZZER 1
#include "error_paths_cases.inc"

/* ========== 全局FUZZ上下文 ========== */

static struct {
    kadd_sqli_engine_t *sqli_engine;
    kadd_dfa_engine_t *dfa_engine;
    kadd_model_handle_t *ml_model;
    kadd_dl_model_t *dl_model;
    kadd_rf_model_t *rf_model;                 /* 等频随机层：原生 RF 句柄 */
    kadd_cnn_model_t *cnn_model;               /* 等频随机层：原生 CNN 句柄 */
    kadd_inference_engine_t *inference_engine; /* 等频随机层：ONNX 推理句柄 */
    kadd_config_t config;
    bool initialized;
} g_fuzz_context = {nullptr, nullptr, nullptr, nullptr,
                    nullptr, nullptr, nullptr, {}, false};

/* 等频随机长稳模式（随机 fuzz 优先，不替代随机测试）：
 *   KADD_FUZZ_EQUAL_RANDOM=1 时，每次迭代在完整 selector / heavy /
 *   fault-inject 随机 fuzz 之外，额外执行 FzEqualRandomApiLayer()：
 *   21 个可长跑的开放函数各 1 次真实随机调用（有效句柄 + 随机输入）。
 *   17 个 load/create/free 生命周期类接口因文件 I/O、模型解析、
 *   ONNX 会话创建或释放语义不适合 3000 万次长跑，明确豁免，
 *   由普通 selector / heavy 路径正常覆盖。 */
static bool g_fuzz_equal_random = false;

/* 等频随机层句柄是否全部就绪：缺句柄时禁止退化为 NULL 调用凑数，
 * 直接不计数，让 api_min=0 在长稳报告里暴露出来。 */
static bool g_fz_equal_ready = false;

/* 21 个等频随机接口的独立调用计数。这里只统计等频随机层的保底调用：
 * 每次迭代 21 个接口各真实随机调用 1 次，api_min >= 目标值即可证明
 * 每个接口都达到验收次数。selector / heavy / fault-inject 里的额外
 * 调用不重复计数；覆盖率 HTML 的原始执行计数只会更高。 */
enum FzOpenApi {
    FZ_API_DFA_ENGINE_TOKENIZE = 0,
    FZ_API_DFA_ENGINE_TOKENIZE_BATCH,
    FZ_API_SEQ_TO_FEATURE,
    FZ_API_SEQ_TO_FEATURE_BATCH,
    FZ_API_MODEL_PREDICT,
    FZ_API_MODEL_PREDICT_BATCH,
    FZ_API_MODEL_GET_FEATURE_DIM,
    FZ_API_DL_MODEL_PREDICT,
    FZ_API_DL_MODEL_PREDICT_BATCH,
    FZ_API_DL_MODEL_GET_FEATURE_DIM,
    FZ_API_SQLI_DETECT,
    FZ_API_SQLI_DETECT_BATCH,
    FZ_API_CNN_MODEL_PREDICT,
    FZ_API_CNN_MODEL_PREDICT_BATCH,
    FZ_API_CNN_MODEL_GET_FEATURE_DIM,
    FZ_API_RF_MODEL_PREDICT,
    FZ_API_RF_MODEL_PREDICT_BATCH,
    FZ_API_RF_MODEL_GET_FEATURE_DIM,
    FZ_API_INFERENCE_PREDICT,
    FZ_API_INFERENCE_PREDICT_BATCH,
    FZ_API_INFERENCE_ENGINE_GET_FEATURE_DIM,
    FZ_API_COUNT
};

static const char *const kFzApiNames[FZ_API_COUNT] = {
    "dfa_engine_tokenize", "dfa_engine_tokenize_batch",
    "seq_to_feature", "seq_to_feature_batch",
    "model_predict", "model_predict_batch", "model_get_feature_dim",
    "dl_model_predict", "dl_model_predict_batch", "dl_model_get_feature_dim",
    "sqli_detect", "sqli_detect_batch",
    "cnn_model_predict", "cnn_model_predict_batch", "cnn_model_get_feature_dim",
    "rf_model_predict", "rf_model_predict_batch", "rf_model_get_feature_dim",
    "inference_predict", "inference_predict_batch",
    "inference_engine_get_feature_dim"
};

static uint64_t g_fz_api_n[FZ_API_COUNT] = {0};

static inline void FzCountApi(FzOpenApi api) {
    ++g_fz_api_n[api];
}

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

    // 创建 ML/DL 引擎：按构建后端选择原生或 ONNX，供普通 selector 用例复用
#if KADD_HAVE_SVE
    const char *ml_model_path = (g_fuzz_context.config.model_rf[0] != '\0')
        ? g_fuzz_context.config.model_rf : kRfKrfPath;
    const char *dl_model_path = (g_fuzz_context.config.model_cnn[0] != '\0')
        ? g_fuzz_context.config.model_cnn
        : FzPickPath(kCnnKcnPath, kCnnKcnRel);
#else
    const char *ml_model_path = (g_fuzz_context.config.model_rf_onnx[0] != '\0')
        ? g_fuzz_context.config.model_rf_onnx : kRfOnnxPath;
    const char *dl_model_path = (g_fuzz_context.config.model_cnn_onnx[0] != '\0')
        ? g_fuzz_context.config.model_cnn_onnx
        : FzPickPath(kCnnOnnxPath, "assets/model/model_tokens_dl_cnn.onnx");
#endif
    if (ml_model_path != nullptr && ml_model_path[0] != '\0') {
        kadd_model_load(&g_fuzz_context.ml_model, ml_model_path, 0);
    }
    if (dl_model_path != nullptr && dl_model_path[0] != '\0') {
        kadd_dl_model_load(&g_fuzz_context.dl_model, dl_model_path, 0);
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

    /* 真实资产闸门 1/64：3 次 create_from_file(ResolveConfigPath()) 各读一遍真实 10.4MB DFA 表（见 FzAllowRealAsset） */
    if (!FzAllowRealAsset(2)) { return; }
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
                kadd_dl_model_load(nullptr, "x.kcn", 0);
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

    /* 真实资产闸门 1/64：kadd_dl_model_load(config.model_cnn_onnx) 会建真实 ONNX 会话（见 FzAllowRealAsset） */
    if (!FzAllowRealAsset(3)) { return; }
    // 尝试加载 DL 模型（native .kcn 优先）
    kadd_dl_model_t *dl_model = nullptr;
    int ret = 1;
    if (g_fuzz_context.config.model_cnn[0] != '\0') {
        ret = kadd_dl_model_load(&dl_model, g_fuzz_context.config.model_cnn, 0);
    }
    if (ret != 0) {
        ret = kadd_dl_model_load(&dl_model, kCnnKcnPath, 0);
    }
    if (ret != 0) {
        ret = kadd_dl_model_load(&dl_model, kCnnKcnRel, 0);
    }
    if (ret != 0) {
        ret = kadd_dl_model_load(&dl_model, g_fuzz_context.config.model_cnn_onnx, 0);
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

    /* 真实资产闸门 1/64：对真实 config / .onnx 做 RF+CNN 引擎完整生命周期（2 个 ONNX 会话）（见 FzAllowRealAsset） */
    if (!FzAllowRealAsset(7)) { return; }
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
            /* 真实 10.4MB 表 + CNN ONNX 会话，单独降频 1/64；
             * 本函数其余十几个 config_load 分支仍每迭代执行。 */
            kadd_sqli_engine_t *dl_sqli = nullptr;
            if (FzAllowRealAsset(4) &&
                kadd_sqli_engine_create_from_file(&dl_sqli, dl_path) == 0) {
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

    // 坏 transition_table 路径 → DFA table 加载失败（覆盖 dfa_engine.c 的
    // 文件不存在/坏 header/坏 state count/size overflow 等错误路径）
    {
        std::string bad_tr = "{" +
            jstr("engine_mode", "ml") + "," +
            jstr("transition_table", "/nonexistent/tran.bin") + "," +
            jstr("output_table", kOutPath) + "," +
            jstr("eod_table", kEodPath) + "," +
            jstr("model_rf_onnx", kRfOnnxPath) +
            "}";
        WriteConfigFile("/tmp/kadd_fuzz_badtran.json", bad_tr);
        kadd_sqli_engine_t *eng = nullptr;
        kadd_sqli_engine_create_from_file(&eng, "/tmp/kadd_fuzz_badtran.json");
        kadd_sqli_engine_free(eng);

        // 用真实 output_table 内容写到坏路径 → 覆盖 DFA 读取但 state count 不匹配
        // （transition_table 和 output_table 格式不同，读 output_table 当 transition 会 header 不匹配）
        std::string bad_out = "{" +
            jstr("engine_mode", "ml") + "," +
            jstr("transition_table", kOutPath) + "," +
            jstr("output_table", kEodPath) + "," +
            jstr("eod_table", kOutPath) + "," +
            jstr("model_rf_onnx", kRfOnnxPath) +
            "}";
        WriteConfigFile("/tmp/kadd_fuzz_badout.json", bad_out);
        eng = nullptr;
        kadd_sqli_engine_create_from_file(&eng, "/tmp/kadd_fuzz_badout.json");
        kadd_sqli_engine_free(eng);
    }

    // 空 config 文件（覆盖 config_loader.c 的 file_size==0 校验）
    {
        const char *empty_path = "/tmp/kadd_fuzz_empty.json";
        WriteConfigFile(empty_path, std::string(""));
        kadd_config_t cfg;
        kadd_config_load(empty_path, &cfg);
        kadd_config_free(&cfg);
    }

    // 超长字符串值（覆盖 config_loader.c 的 strlen 截断路径）
    {
        std::string long_val(300, 'x');
        std::string cfg_s = "{" +
            jstr("engine_mode", "ml") + "," +
            jstr("transition_table", long_val) + "," +
            jstr("output_table", kOutPath) + "," +
            jstr("eod_table", kEodPath) + "," +
            jstr("model_rf_onnx", kRfOnnxPath) +
            "}";
        WriteConfigFile("/tmp/kadd_fuzz_longval.json", cfg_s);
        kadd_config_t cfg;
        kadd_config_load("/tmp/kadd_fuzz_longval.json", &cfg);
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
        ret = kadd_dl_model_load(&m, g_fuzz_context.config.model_cnn, 0);
    }
    if (ret != 0) {
        ret = kadd_dl_model_load(&m, kCnnKcnPath, 0);
    }
    if (ret != 0) {
        ret = kadd_dl_model_load(&m, kCnnKcnRel, 0);
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
    /* [FIX REPORT-2026-0000018] size==0 时 data 无有效元素，
     * 原先 mod=1 会让 data[i % mod] 退化为 data[0] 造成越界读；
     * 改为无输入字节时使用确定性 token，既保留覆盖又消除越界 */
    int len_a = (size > 30) ? 30 : (int)size;
    if (len_a <= 0) len_a = 8;
    for (int i = 0; i < len_a; i++) {
        seq_a[i] = (size > 0) ? (data[(size_t)i % size] % 28) : (i % 28);
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
        /* [FIX REPORT-2026-0000019] 下方 feature_dim=28 的调用会写 feats[0..27]，
         * 缓冲区必须按最大 feature_dim 分配，否则栈越界写 */
        int feats[28] = {0};
        kadd_seq_to_feature(bad_tokens, 8, 5, feats, 8);
        kadd_seq_to_feature(bad_tokens, 8, 5, feats, 28);
    }

    // 批量：多条 + 不同 window/feature_dim
    {
        int seq_b[20] = {0};
        int len_b = (size > 20) ? 20 : (int)size;
        if (len_b <= 0) len_b = 6;
        for (int i = 0; i < len_b; i++) seq_b[i] = (size > 0) ? (data[(size_t)i % size] % 28) : (i % 28);

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

    /* 真实资产闸门 1/64：2 次 load_from_path 真实 RF/CNN .onnx（2 个 ONNX 会话）（见 FzAllowRealAsset） */
    if (!FzAllowRealAsset(8)) { return; }
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

/* ==========================================================================
 * .krf 合成模型构造（带 HMAC-SHA256 签名）
 *
 * [FIX] 自 [REPORT-2026-0000028] 起原生模型强制 HMAC-SHA256 认证：flags bit0=0
 * 的未签名文件一律 fail-closed 拒绝。原 FuzzWriteRfModel() 写的是 flags=0 的
 * 未签名模型，因此加载器在 kadd_rf_verify_authenticity() 处就返回，
 * 其后的 CRC 校验、节点解析、validate_tree()、predict_tree()、
 * predict_batch_scalar() 全部永远走不到（ONNX 构建下 rf_engine 有 5 个函数
 * 从未被进入，函数覆盖率只有 9/14）。
 *
 * 这里让合成模型自带合法签名（密钥与加载器取值逻辑完全一致：优先环境变量，
 * 否则用仓库公开开发密钥），使这些分支重新可达，并额外构造树结构非法变体
 * 覆盖 validate_tree() 的三个拒绝分支。
 * ========================================================================== */

/* 单个 RF 节点的磁盘布局（32 字节，与 kadd_rf_node_t 一致） */
struct FuzzRfNodeSpec {
    int32_t  feature;       /* -1 = 叶节点 */
    uint32_t left;
    uint32_t right;
    uint32_t reserved;
    double   threshold;
    double   probability;
};

static FuzzRfNodeSpec FuzzRfLeaf(double prob) {
    FuzzRfNodeSpec n;
    n.feature = -1;
    n.left = UINT32_MAX;
    n.right = UINT32_MAX;
    n.reserved = 0u;
    n.threshold = 0.0;
    n.probability = prob;
    return n;
}

static FuzzRfNodeSpec FuzzRfInternal(int32_t feature, double threshold,
                                     uint32_t left, uint32_t right) {
    FuzzRfNodeSpec n;
    n.feature = feature;
    n.left = left;
    n.right = right;
    n.reserved = 0u;
    n.threshold = threshold;
    n.probability = 0.0;
    return n;
}

/* 取与加载器完全相同的密钥：优先环境变量（CI 自有密钥），否则仓库公开开发密钥 */
static bool FuzzModelHmacKey(uint8_t key[KADD_SHA256_DIGEST_SIZE]) {
    if (kadd_model_load_hmac_key(key) == 1) {
        return true;
    }
    return kadd_hex_to_bytes(kadd_test::kModelHmacDevKeyHex,
                             KADD_MODEL_HMAC_KEY_HEX_LEN, key,
                             KADD_SHA256_DIGEST_SIZE) == 0;
}

/**
 * @brief 按规格构造并写出一个 KADD-RF v1 模型文件
 *
 * @param tree_offsets      长度必须 = tree_count + 1
 * @param nodes             全部节点（按 tree_offsets 划分归属）
 * @param sign_model        true = 置 flags bit0 并追加 HMAC-SHA256 尾部
 * @param crc_corrupt       true = 写入错误 CRC（签名仍合法，用于触发 CRC 分支）
 * @param tamper_payload    true = 签名后篡改 payload（触发 HMAC 校验失败分支）
 * @param extra_tail_byte   true = 文件尾多写 1 字节（触发文件长度校验分支）
 * @return 写出的文件字节数
 */
static size_t FuzzBuildRfModel(const char *path,
                               uint32_t feature_count,
                               const std::vector<uint32_t> &tree_offsets,
                               const std::vector<FuzzRfNodeSpec> &nodes,
                               bool sign_model,
                               bool crc_corrupt,
                               bool tamper_payload,
                               bool extra_tail_byte) {
    const uint32_t tree_count =
        tree_offsets.empty() ? 0u : (uint32_t)(tree_offsets.size() - 1u);
    const uint32_t node_count = (uint32_t)nodes.size();

    std::vector<unsigned char> f;
    const unsigned char magic[8] = {'K', 'A', 'D', 'D', 'R', 'F', '1', '\0'};
    f.insert(f.end(), magic, magic + 8);
    FuzzPutU32LE(f, 1u);              // version          @8
    FuzzPutU32LE(f, 64u);             // header_size      @12
    FuzzPutU32LE(f, 32u);             // node_size        @16
    FuzzPutU32LE(f, feature_count);   // feature_count    @20
    FuzzPutU32LE(f, 2u);              // class_count      @24
    FuzzPutU32LE(f, 1u);              // positive_class   @28
    FuzzPutU32LE(f, tree_count);      // tree_count       @32
    FuzzPutU32LE(f, node_count);      // node_count       @36
    FuzzPutU32LE(f, sign_model ? 1u : 0u);   // flags     @40（bit0 = 已签名）
    const size_t offsets_size = tree_offsets.size() * sizeof(uint32_t);
    const size_t payload_size = offsets_size + (size_t)node_count * 32u;
    FuzzPutU64LE(f, (uint64_t)payload_size);  // payload_size @44
    const size_t crc_pos = f.size();
    FuzzPutU32LE(f, 0u);              // crc              @52（占位，稍后回填）
    FuzzPutU32LE(f, 0u);              // reserved         @56
    FuzzPutU32LE(f, 0u);              // reserved         @60

    for (size_t i = 0; i < tree_offsets.size(); ++i) {
        FuzzPutU32LE(f, tree_offsets[i]);
    }
    for (size_t i = 0; i < nodes.size(); ++i) {
        const FuzzRfNodeSpec &n = nodes[i];
        FuzzPutU32LE(f, (uint32_t)n.feature);
        FuzzPutU32LE(f, n.left);
        FuzzPutU32LE(f, n.right);
        FuzzPutU32LE(f, n.reserved);
        FuzzPutF64LE(f, n.threshold);
        FuzzPutF64LE(f, n.probability);
    }

    uint32_t crc = FuzzCrc32(f.data() + 64, payload_size);
    if (crc_corrupt) {
        crc = 0x12345678u;
    }
    f[crc_pos + 0] = (unsigned char)(crc & 0xFFu);
    f[crc_pos + 1] = (unsigned char)((crc >> 8) & 0xFFu);
    f[crc_pos + 2] = (unsigned char)((crc >> 16) & 0xFFu);
    f[crc_pos + 3] = (unsigned char)((crc >> 24) & 0xFFu);

    const size_t signed_region = 64u + payload_size;
    if (sign_model) {
        uint8_t key[KADD_SHA256_DIGEST_SIZE];
        uint8_t mac[KADD_SHA256_DIGEST_SIZE];
        if (FuzzModelHmacKey(key)) {
            kadd_hmac_sha256(key, sizeof(key), f.data(), signed_region, mac);
            kadd_secure_wipe(key, sizeof(key));
            f.insert(f.end(), mac, mac + KADD_SHA256_DIGEST_SIZE);
        }
    }
    if (tamper_payload && f.size() > signed_region) {
        f[signed_region - 1] ^= 0xFFu;   /* 签名之后再改，HMAC 必然失配 */
    }
    if (extra_tail_byte) {
        f.push_back(0x00u);
    }

    FILE *fp = fopen(path, "wb");
    if (fp) {
        fwrite(f.data(), 1, f.size(), fp);
        fclose(fp);
    }
    return f.size();
}

/**
 * @brief 构造 1 树 1 叶的最小合法模型（保持原有调用方语义）
 *
 * 与旧实现的唯一区别：默认带合法 HMAC 签名，否则加载器会在认证阶段
 * fail-closed，成功路径与其后的所有分支都无法覆盖。
 */
static size_t FuzzWriteRfModel(const char *path, uint32_t feature_count,
                               bool crc_corrupt) {
    std::vector<uint32_t> offsets;
    offsets.push_back(0u);
    offsets.push_back(1u);
    std::vector<FuzzRfNodeSpec> nodes;
    nodes.push_back(FuzzRfLeaf(0.5));
    return FuzzBuildRfModel(path, feature_count, offsets, nodes,
                            /*sign_model=*/true, crc_corrupt,
                            /*tamper_payload=*/false,
                            /*extra_tail_byte=*/false);
}

/* 加载一个合成模型并跑一遍 predict / predict_batch / free（成功路径全覆盖） */
static void FuzzRfLoadAndExercise(const char *path) {
    kadd_rf_model_t *m = nullptr;
    if (kadd_rf_model_load(&m, path) != 0 || !m) {
        if (m) kadd_rf_model_free(m);
        return;
    }
    const int fd = kadd_rf_model_get_feature_dim(m);
    const int dim = (fd > 0 && fd <= 4096) ? fd : 1;
    std::vector<float> one((size_t)dim, 0.25f);
    float prob = 0.0f;
    kadd_rf_model_predict(m, one.data(), &prob);

    const int bs[] = {1, 2, 3, 31, 32, 33, 64, 65, 128};
    for (size_t i = 0; i < sizeof(bs) / sizeof(bs[0]); ++i) {
        const int b = bs[i];
        std::vector<float> mat((size_t)dim * (size_t)b, 0.1f);
        std::vector<float> out((size_t)b, 0.0f);
        kadd_rf_model_predict_batch(m, mat.data(), b, out.data(), b);
    }
    /* batch_size 越界 -> 回退默认值 */
    std::vector<float> mat4((size_t)dim * 4u, 0.1f);
    float out4[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    kadd_rf_model_predict_batch(m, mat4.data(), 4, out4, 2000);
    /* 非有限特征 -> KADD_ERROR_INVALID_PARAM */
    std::vector<float> bad((size_t)dim, 0.0f);
    bad[0] = std::numeric_limits<float>::quiet_NaN();
    kadd_rf_model_predict(m, bad.data(), &prob);
    kadd_rf_model_predict_batch(m, bad.data(), 1, &prob, 1);
    kadd_rf_model_free(m);
}

/* 树结构 / 签名 / CRC / 长度 的非法变体：覆盖 validate_tree 与加载器的拒绝分支 */
static void FuzzRFModelStructureVariants() {
    const std::string dir = "/tmp/";
    std::vector<uint32_t> o1;  o1.push_back(0u); o1.push_back(1u);
    std::vector<uint32_t> o2;  o2.push_back(0u); o2.push_back(2u); o2.push_back(2u);
    std::vector<uint32_t> o3;  o3.push_back(0u); o3.push_back(5u); o3.push_back(3u);
    std::vector<uint32_t> o4;  o4.push_back(3u); o4.push_back(4u);
    std::vector<uint32_t> o5;  o5.push_back(0u); o5.push_back(3u);
    std::vector<FuzzRfNodeSpec> leaf_ok;   leaf_ok.push_back(FuzzRfLeaf(0.5));
    std::vector<FuzzRfNodeSpec> leaf_two;  leaf_two.push_back(FuzzRfLeaf(0.25));
    leaf_two.push_back(FuzzRfLeaf(0.75));
    std::vector<FuzzRfNodeSpec> leaf_three; leaf_three = leaf_two;
    leaf_three.push_back(FuzzRfLeaf(0.5));
    std::vector<FuzzRfNodeSpec> leaf_badprob = leaf_ok;
    leaf_badprob[0].probability = 2.0;                    /* 越界概率 */
    std::vector<FuzzRfNodeSpec> leaf_badfeat = leaf_ok;
    leaf_badfeat[0].feature = -2;                         /* 非法叶标记 */
    std::vector<FuzzRfNodeSpec> leaf_badnan = leaf_ok;
    leaf_badnan[0].probability =
        std::numeric_limits<double>::quiet_NaN();         /* 非有限概率 */
    std::vector<FuzzRfNodeSpec> node_badidx;              /* feature 越界 */
    node_badidx.push_back(FuzzRfInternal(28, 0.5, 1u, 2u));
    node_badidx.push_back(FuzzRfLeaf(0.25));
    node_badidx.push_back(FuzzRfLeaf(0.75));
    std::vector<FuzzRfNodeSpec> node_badresv = node_badidx;
    node_badresv[0] = FuzzRfInternal(0, 0.5, 1u, 2u);
    node_badresv[0].reserved = 7u;                        /* reserved 非 0 */
    std::vector<FuzzRfNodeSpec> node_badleft = node_badidx;
    node_badleft[0] = FuzzRfInternal(0, 0.5, 0u, 2u);     /* left <= index */
    std::vector<FuzzRfNodeSpec> node_badthr = node_badidx;
    node_badthr[0] = FuzzRfInternal(0, std::numeric_limits<double>::infinity(),
                                    1u, 2u);              /* 非有限阈值 */
    std::vector<FuzzRfNodeSpec> node_ok;
    node_ok.push_back(FuzzRfInternal(0, 0.5, 1u, 2u));
    node_ok.push_back(FuzzRfLeaf(0.25));
    node_ok.push_back(FuzzRfLeaf(0.75));

    struct Case { const char *name; uint32_t fc; const std::vector<uint32_t> *off;
                  const std::vector<FuzzRfNodeSpec> *nodes; bool sign;
                  bool crc; bool tamper; bool extra; };
    const Case cases[] = {
        {"rf_var_unsigned.bin",   28, &o1, &leaf_ok,      false, false, false, false},
        {"rf_var_tampered.bin",   28, &o1, &leaf_ok,      true,  false, true,  false},
        {"rf_var_extratail.bin",  28, &o1, &leaf_ok,      true,  false, false, true },
        {"rf_var_beginend.bin",   28, &o2, &leaf_two,     true,  false, false, false},
        {"rf_var_endgt.bin",      28, &o3, &leaf_three,   true,  false, false, false},
        {"rf_var_off0.bin",       28, &o4, &leaf_ok,      true,  false, false, false},
        {"rf_var_prob2.bin",      28, &o1, &leaf_badprob, true,  false, false, false},
        {"rf_var_featm2.bin",     28, &o1, &leaf_badfeat, true,  false, false, false},
        {"rf_var_probnan.bin",    28, &o1, &leaf_badnan,  true,  false, false, false},
        {"rf_var_idx28.bin",      28, &o5, &node_badidx,  true,  false, false, false},
        {"rf_var_resv7.bin",      28, &o5, &node_badresv, true,  false, false, false},
        {"rf_var_left0.bin",      28, &o5, &node_badleft, true,  false, false, false},
        {"rf_var_thrinf.bin",     28, &o5, &node_badthr,  true,  false, false, false},
        {"rf_var_tree_ok.bin",    28, &o5, &node_ok,      true,  false, false, false},
        {"rf_var_fcount0.bin",     0, &o1, &leaf_ok,      true,  false, false, false},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const std::string path = dir + cases[i].name;
        FuzzBuildRfModel(path.c_str(), cases[i].fc, *cases[i].off,
                         *cases[i].nodes, cases[i].sign, cases[i].crc,
                         cases[i].tamper, cases[i].extra);
        kadd_rf_model_t *m = nullptr;
        (void)kadd_rf_model_load(&m, path.c_str());
        if (m) kadd_rf_model_free(m);
    }
    /* CRC 失配（签名合法）：必须走到 crc32_bytes 比较之后才拒绝 */
    {
        const std::string p = dir + "rf_var_crcbad.bin";
        FuzzBuildRfModel(p.c_str(), 28, o1, leaf_ok, true, true, false, false);
        kadd_rf_model_t *m = nullptr;
        (void)kadd_rf_model_load(&m, p.c_str());
        if (m) kadd_rf_model_free(m);
    }
    /* 目录当模型文件：fread 失败分支（read_entire_file） */
    {
        kadd_rf_model_t *m = nullptr;
        (void)kadd_rf_model_load(&m, "/tmp");
        if (m) kadd_rf_model_free(m);
    }
}

/* 直接加载仓库自带的已签名 .krf：ONNX 构建下 ML 分发层不会走原生加载器，
 * 必须由 fuzz 直接调用，否则真实模型的成功路径（含 validate_tree 全量循环、
 * predict_tree、predict_batch_scalar）在 ONNX 构建中完全没有覆盖。 */
static void FuzzRFEnginePristineModel() {
    kadd_rf_model_t *m = nullptr;
    int ret = kadd_rf_model_load(&m, kRfKrfPath);
    if (ret != 0 || !m) {
        if (m) kadd_rf_model_free(m);
        m = nullptr;
        ret = kadd_rf_model_load(&m, kRfKrfRel);
    }
    if (ret != 0 || !m) {
        if (m) kadd_rf_model_free(m);
        return;
    }
    const int fd = kadd_rf_model_get_feature_dim(m);
    const int dim = (fd > 0 && fd <= 4096) ? fd : 28;
    std::vector<float> one((size_t)dim);
    for (int i = 0; i < dim; ++i) {
        one[(size_t)i] = (float)(i % 7) / 7.0f;
    }
    float prob = 0.0f;
    kadd_rf_model_predict(m, one.data(), &prob);
    const int bs[] = {1, 2, 31, 32, 33, 64, 65, 128};
    for (size_t i = 0; i < sizeof(bs) / sizeof(bs[0]); ++i) {
        const int b = bs[i];
        std::vector<float> mat((size_t)dim * (size_t)b, 0.1f);
        std::vector<float> out((size_t)b, 0.0f);
        kadd_rf_model_predict_batch(m, mat.data(), b, out.data(), b);
    }
    kadd_rf_model_free(m);
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

    /* 真实资产闸门 1/64：3 次 kadd_model_load 真实 rf.onnx（3 个 ONNX 会话）（见 FzAllowRealAsset） */
    if (!FzAllowRealAsset(5)) { return; }
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
        // 非法 batch_size（行 613-615：batch_size<=0 回退默认，需配合 NULL inputs 拦截）
        kadd_rf_model_predict_batch((kadd_rf_model_t *)1, nullptr, 1, &prob, 0);
        // 非法 sample_count（行 608-610）
        kadd_rf_model_predict_batch(nullptr, f, 0, &prob, 1);
        kadd_rf_model_predict_batch(nullptr, f, -1, &prob, 1);
        kadd_rf_model_predict_batch(nullptr, f, 10000001, &prob, 1);
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

    /* [COV] 已签名合成模型的成功路径 + 树结构/签名/CRC/长度非法变体 +
     * 仓库真实 .krf 生命周期：覆盖 HMAC 认证之后才可达的 crc32 校验、
     * 节点解析、validate_tree、predict_tree、predict_batch_scalar 分支
     * （ONNX 构建下这些分支此前完全未覆盖，rf 函数覆盖率仅 9/14） */
    FuzzRfLoadAndExercise("/tmp/kadd_fuzz_rf_ok.bin");
    FuzzRFModelStructureVariants();
    FuzzRFEnginePristineModel();
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
    kadd_cnn_model_get_feature_dim(nullptr);  // NULL 测试

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
        // 非法 batch_size（batch_size<=0 回退默认，需配合 NULL inputs 拦截）
        kadd_cnn_model_predict_batch((kadd_cnn_model_t *)1, nullptr, 1, &prob, 0);
        // 非法 sample_count
        kadd_cnn_model_predict_batch(nullptr, f, 0, &prob, 1);
        kadd_cnn_model_predict_batch(nullptr, f, -1, &prob, 1);
        kadd_cnn_model_predict_batch(nullptr, f, 10000001, &prob, 1);
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
        }
        /* [FIX REPORT-2026-0000020] 移除恒假的 else if (m) 分支：
         * 上方 if (m) 已覆盖全部 m != nullptr 情形，该分支为死代码 */
    }
}

/* ========== DL 引擎 ONNX 后端 FUZZ（backend=0） ========== */

/**
 * @brief 直接加载 DL .onnx 模型，覆盖 kadd_dl_model_load ONNX 回退
 * 以及 backend=0 的 predict / predict_batch / get_feature_dim / free。
 */
static void FuzzDLLoadOnnxBackend(const uint8_t *data, size_t size) {

    /* 真实资产闸门 1/64：2 次 kadd_dl_model_load 真实 cnn.onnx（2 个 ONNX 会话）（见 FzAllowRealAsset） */
    if (!FzAllowRealAsset(6)) { return; }
    (void)data; (void)size;

    kadd_dl_model_t *m = nullptr;
    int ret = kadd_dl_model_load(&m, kCnnOnnxPath, 0);
    if (ret != 0 || m == nullptr) {
        ret = kadd_dl_model_load(&m, "assets/model/model_tokens_dl_cnn.onnx", 0);
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
    kadd_dl_model_load(nullptr, kCnnOnnxPath, 0);
    kadd_dl_model_load(nullptr, nullptr, 0);
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

/* ========== 覆盖率导向的生命周期调度辅助 ========== */

/**
 * @brief 推理引擎公共入口参数校验 + 真实模型完整生命周期
 *
 * 覆盖 kadd_inference_engine.c：
 *   428-429 / 433-434 / 439-440  engine_load 的 NULL / 非法 type / config 错误
 *   455-456                      load_from_path NULL
 *   476-481                      predict_batch NULL / n 非法
 *   501-503                      非有限特征拒绝
 *   553-554 / 561-562            get_feature_dim / free 的 NULL 分支
 * NULL 引擎即可安全触达全部校验分支，不解引用任何 ORT 内部指针。
 */
static void FuzzInferencePublicValidation() {

    /* 真实资产闸门 1/64：config 成功路径 + load_from_path 真实 rf.onnx（2 个 ONNX 会话）（见 FzAllowRealAsset） */
    if (!FzAllowRealAsset(9)) { return; }
    kadd_inference_engine_t *e = nullptr;
    float feats[28] = {0.0f};
    float prob = 0.0f;

    /* engine_load：NULL / 非法 type / config 加载失败 */
    (void)kadd_inference_engine_load(nullptr, "config.json", KADD_INFERENCE_TYPE_RF);
    (void)kadd_inference_engine_load(&e, nullptr, KADD_INFERENCE_TYPE_RF);
    (void)kadd_inference_engine_load(&e, "config.json", (kadd_inference_type_t)99);
    (void)kadd_inference_engine_load(&e, "/nonexistent/kadd_fz_cfg.json",
                                     KADD_INFERENCE_TYPE_RF);
    if (e) { kadd_inference_engine_free(e); e = nullptr; }

    /* load_from_path：NULL 参数 */
    (void)kadd_inference_engine_load_from_path(nullptr, kRfOnnxPath);
    (void)kadd_inference_engine_load_from_path(&e, nullptr);
    if (e) { kadd_inference_engine_free(e); e = nullptr; }

    /* predict / predict_batch：NULL 引擎与非法参数（不解引用 ORT） */
    (void)kadd_inference_predict(nullptr, feats, &prob);
    (void)kadd_inference_predict_batch(nullptr, feats, 1, &prob, 1);
    (void)kadd_inference_predict_batch(nullptr, nullptr, 1, nullptr, 1);
    (void)kadd_inference_engine_get_feature_dim(nullptr);
    kadd_inference_engine_free(nullptr);

    /* config 成功路径（437-449）+ 完整生命周期 */
    if (kadd_inference_engine_load(&e, FzResolveConfig(), KADD_INFERENCE_TYPE_RF) == 0 && e) {
        int dim = kadd_inference_engine_get_feature_dim(e);
        if (dim > 0 && dim <= 4096) {
            std::vector<float> v((size_t)dim, 0.1f);
            (void)kadd_inference_predict(e, v.data(), &prob);
            v[0] = std::numeric_limits<float>::quiet_NaN();
            (void)kadd_inference_predict(e, v.data(), &prob);  /* 501-503 非有限拒绝 */
            v[0] = 0.1f;
            std::vector<float> mat((size_t)dim * 4u, 0.1f);
            float out[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            (void)kadd_inference_predict_batch(e, mat.data(), 4, out, 4);
            (void)kadd_inference_predict_batch(e, mat.data(), 0, out, 4);
            (void)kadd_inference_predict_batch(e, mat.data(), -1, out, 4);
            (void)kadd_inference_predict_batch(e, mat.data(), 10000001, out, 4);
        }
        kadd_inference_engine_free(e);
        e = nullptr;
    } else if (e) {
        kadd_inference_engine_free(e);
        e = nullptr;
    }

    /* load_from_path 真实模型生命周期（452-459 + predict/batch/free） */
    const char *mp = FzPickPath(kRfOnnxPath, "assets/model/model_tokens_rf.onnx");
    if (kadd_inference_engine_load_from_path(&e, mp) == 0 && e) {
        int dim = kadd_inference_engine_get_feature_dim(e);
        if (dim > 0 && dim <= 4096) {
            std::vector<float> v((size_t)dim, 0.2f);
            (void)kadd_inference_predict(e, v.data(), &prob);
            std::vector<float> mat((size_t)dim * 2u, 0.2f);
            float out[2] = {0.0f, 0.0f};
            (void)kadd_inference_predict_batch(e, mat.data(), 2, out, 2);
        }
        kadd_inference_engine_free(e);
    } else if (e) {
        kadd_inference_engine_free(e);
    }
}

#ifdef KADD_FUZZ_INFERENCE_INJECT
/* ORT 故障注入：覆盖正常模型加载永远不可达的错误清理分支
 *   - check_ort_status / get_model_input_dim / get_model_output_dim（10 个场景）
 *   - create_ort_session：options / 执行模式 / 图优化 / 线程数告警 /
 *     CreateSession 失败 / CreateMemoryInfo 失败 → cleanup_session（6 个场景）
 *   - get_io_name：allocator / output_count / input_name 失败、name==NULL 防御、
 *     AllocatorFree 返错（5 个场景）
 *   - predict_batch：CreateTensor / Run / GetTensorData 失败与 output_dim<2 防御（4 个场景）
 *   - init_ort_env：CreateEnv 失败 → load_engine_internal 上抛（1 个场景）
 * 合计 26 个场景。仅当本 TU 内联了 kadd_inference_engine.c 时可用。 */
static void FuzzInferenceFaultInjection() {
    (void)kadd_inject::RunAllScenarios();

    /* release_engine_resources 的 NULL 防御（367-369）：公共接口
     * kadd_inference_engine_free 在 engine==NULL 时提前返回（561-563），
     * 只有直接调用内部函数才能触达 */
    release_engine_resources(nullptr);

    /* 线程数环境变量分支（276-291）：本 TU 已内联 .c，可直接调用
     * static ort_thread_count_from_env，无需重复加载模型，长稳运行零额外开销。
     * coverage_all.sh 恒定导出合法值，未设置/非法值分支只能在此临时改写后触达。 */
    static const char *const kEnvNames[] = {KADD_ORT_INTRA_OP_ENV, KADD_ORT_INTER_OP_ENV};
    static const char *const kEnvVals[] = {
        nullptr, "", "abc", " 8", "8x", "0", "-1", "1025", "8", "1024"
    };
    for (size_t n = 0; n < sizeof(kEnvNames) / sizeof(kEnvNames[0]); ++n) {
        const char *prev = getenv(kEnvNames[n]);
        const std::string saved = (prev != nullptr) ? std::string(prev) : std::string();
        for (size_t v = 0; v < sizeof(kEnvVals) / sizeof(kEnvVals[0]); ++v) {
            FzSetEnv(kEnvNames[n], kEnvVals[v]);
            (void)ort_thread_count_from_env(kEnvNames[n],
                                            KADD_ORT_DEFAULT_INTRA_OP_THREADS);
            (void)ort_thread_count_from_env(kEnvNames[n],
                                            KADD_ORT_DEFAULT_INTER_OP_THREADS);
        }
        /* 精确复原，避免污染 coverage_all.sh 导出的调优值与后续迭代 */
        FzSetEnv(kEnvNames[n], (prev != nullptr) ? saved.c_str() : nullptr);
    }
}
#else
static void FuzzInferenceFaultInjection() {}
#endif

/**
 * @brief 构造 fuzz 专用合成资产（进程内只执行一次）
 *
 * - 微型 DFA 三表：state0 --'a'--> state1，eod 使 "a" 稳定产出 2 个 token
 * - 微型已签名 RF 模型（4 特征 1 树 1 叶）：供内部 RF 生命周期高频调用
 * - 合成 config.json：供 config_load 成功路径高频调用
 */
static void BuildSyntheticAssets() {
    static bool ready = false;
    if (ready) return;
    ready = true;

    std::vector<unsigned char> v;
    /* transition：2 状态 × 256 cell，'a'(97) → 1，其余 ID_ERROR(0xFFFF) */
    FzPutU32LE(v, 2u);
    for (size_t i = 0; i < 2u * 256u; ++i) {
        FzPutU16LE(v, (i == 97u) ? (uint16_t)1 : (uint16_t)0xFFFFu);
    }
    FzWriteBytes("/tmp/kadd_fz_dfa_tr.bin", v);
    /* output：全 0（只前进不发射） */
    v.clear();
    FzPutU32LE(v, 2u);
    for (size_t i = 0; i < 2u * 256u; ++i) FzPutU16LE(v, 0);
    FzWriteBytes("/tmp/kadd_fz_dfa_out.bin", v);
    /* eod：state1 = EMIT_TWO(3) | 5<<2 | 7<<8 */
    v.clear();
    FzPutU32LE(v, 2u);
    FzPutU16LE(v, 0);
    FzPutU16LE(v, (uint16_t)(3u | (5u << 2) | (7u << 8)));
    FzWriteBytes("/tmp/kadd_fz_dfa_eod.bin", v);

    /* 微型已签名 RF 模型 */
    FuzzWriteRfModel("/tmp/kadd_fz_rf_tiny.krf", 4, false);

    /* 合成 config（ml 模式，指向上述合成资产） */
    std::string json = std::string("{\"engine_mode\":\"ml\",")
        + "\"transition_table\":\"/tmp/kadd_fz_dfa_tr.bin\","
        + "\"output_table\":\"/tmp/kadd_fz_dfa_out.bin\","
        + "\"eod_table\":\"/tmp/kadd_fz_dfa_eod.bin\","
        + "\"model_rf\":\"/tmp/kadd_fz_rf_tiny.krf\"}";
    WriteConfigFile("/tmp/kadd_fz_cfg.json", json);
}

/**
 * @brief 初始化等频随机层所需的 3 个内部引擎句柄（进程内仅一次）
 *
 * 候选顺序：config 指定的资产 → /opt/KADD 绝对路径 → 仓库相对路径
 * → （仅 RF）BuildSyntheticAssets() 生成的已签名微型模型兜底。
 * 任一必需句柄缺失时 g_fz_equal_ready=false：等频层整体不执行、
 * 不计数，长稳报告以 api_min=0 暴露问题，绝不用 NULL 调用凑数。
 */
static void FzInitEqualRandomHandles() {
    const char *rf_cands[] = {g_fuzz_context.config.model_rf, kRfKrfPath,
                              kRfKrfRel, "/tmp/kadd_fz_rf_tiny.krf"};
    for (const char *p : rf_cands) {
        if (p == nullptr || p[0] == '\0') continue;
        if (kadd_rf_model_load(&g_fuzz_context.rf_model, p) == 0 &&
            g_fuzz_context.rf_model != nullptr) {
            break;
        }
        g_fuzz_context.rf_model = nullptr;
    }

    const char *cnn_cands[] = {g_fuzz_context.config.model_cnn,
                               kCnnKcnPath, kCnnKcnRel};
    for (const char *p : cnn_cands) {
        if (p == nullptr || p[0] == '\0') continue;
        if (kadd_cnn_model_load(&g_fuzz_context.cnn_model, p) == 0 &&
            g_fuzz_context.cnn_model != nullptr) {
            break;
        }
        g_fuzz_context.cnn_model = nullptr;
    }

    if (kadd_inference_engine_load(&g_fuzz_context.inference_engine,
                                   ResolveConfigPath(),
                                   KADD_INFERENCE_TYPE_RF) != 0 ||
        g_fuzz_context.inference_engine == nullptr) {
        const char *inf_cands[] = {kRfOnnxPath,
                                   "assets/model/model_tokens_rf.onnx"};
        for (const char *p : inf_cands) {
            if (p == nullptr || p[0] == '\0') continue;
            if (kadd_inference_engine_load_from_path(
                    &g_fuzz_context.inference_engine, p) == 0 &&
                g_fuzz_context.inference_engine != nullptr) {
                break;
            }
            g_fuzz_context.inference_engine = nullptr;
        }
    }

    g_fz_equal_ready = (g_fuzz_context.dfa_engine != nullptr &&
                        g_fuzz_context.ml_model != nullptr &&
                        g_fuzz_context.dl_model != nullptr &&
                        g_fuzz_context.sqli_engine != nullptr &&
                        g_fuzz_context.rf_model != nullptr &&
                        g_fuzz_context.cnn_model != nullptr &&
                        g_fuzz_context.inference_engine != nullptr);
    if (!g_fz_equal_ready) {
        /* 初始化日志未被静音（重定向发生在其后），失败必须可见 */
        (void)fprintf(stderr,
                      "[FZ][ERROR] equal-random 句柄缺失: dfa=%d ml=%d dl=%d "
                      "sqli=%d rf=%d cnn=%d inference=%d\n",
                      g_fuzz_context.dfa_engine != nullptr,
                      g_fuzz_context.ml_model != nullptr,
                      g_fuzz_context.dl_model != nullptr,
                      g_fuzz_context.sqli_engine != nullptr,
                      g_fuzz_context.rf_model != nullptr,
                      g_fuzz_context.cnn_model != nullptr,
                      g_fuzz_context.inference_engine != nullptr);
    }
}

/**
 * @brief 等频随机层：每次迭代对 21 个可长跑开放函数各做 1 次真实随机调用
 *
 * 与旧「NULL 配额」方案的本质区别：
 *  - 全部使用有效持久句柄（初始化一次），绝不靠空指针 early-return 凑数；
 *  - 输入由本次 libFuzzer 变异数据派生（xorshift 种子），逐迭代随机：
 *    SQL/DFA 随机字符串、随机 token 序列、随机特征向量、随机批量参数；
 *  - predict wrapper 单样本真实推理，batch 用 1~4 个随机样本，
 *    load→predict→predict_batch→free 生命周期由 selector/heavy 层承担；
 *  - 17 个 load/create/free 接口因文件 I/O / 模型解析 / ONNX 会话创建 /
 *    释放语义不适合千万次长跑而豁免（报告单列），本层不调用；
 *  - 本层叠加在完整 selector / heavy / fault-inject 随机 fuzz 之上，
 *    覆盖率仍由随机测试主导，计数只是保底下限。
 */
static void FzEqualRandomApiLayer(const uint8_t *data, size_t size) {
    if (!g_fz_equal_ready) return;

    /* 种子取自本次输入：libFuzzer 变异驱动，逐迭代不同 */
    uint32_t st = 0x9E3779B9u;
    for (size_t i = 0; i < size; ++i) {
        st = st * 131u + data[i];
    }
    if (st == 0u) { st = 1u; }
    auto rnd = [&st]() {
        st ^= st << 13; st ^= st >> 17; st ^= st << 5;
        return st;
    };

    const int n = 1 + (int)(rnd() % 4u);     /* 批量样本数 1..4 */
    const int bs = 1 + (int)(rnd() % 8u);    /* 分批大小 1..8 */

    /* ---- DFA：随机字符串分词（1/2） ---- */
    char sql1[129];
    const size_t len1 = 1 + (size_t)(rnd() % 128u);
    for (size_t i = 0; i < len1; ++i) {
        sql1[i] = (char)(32 + (rnd() % 95u));
    }
    sql1[len1] = '\0';
    int tokens[KADD_DFA_MAX_TOKENS];
    int token_count = 0;
    (void)kadd_dfa_engine_tokenize(g_fuzz_context.dfa_engine, sql1, len1,
                                   tokens, &token_count);
    FzCountApi(FZ_API_DFA_ENGINE_TOKENIZE);

    /* ---- DFA：随机批量分词（2/2，成功时 outputs 由调用方释放） ---- */
    char sqls[4][65];
    const char *db_inputs[4];
    size_t db_lens[4];
    int *db_outs[4] = {nullptr, nullptr, nullptr, nullptr};
    int db_dims[4] = {-1, -1, -1, -1};
    for (int i = 0; i < n; ++i) {
        const size_t l = 1 + (size_t)(rnd() % 64u);
        for (size_t k = 0; k < l; ++k) {
            sqls[i][k] = (char)(32 + (rnd() % 95u));
        }
        sqls[i][l] = '\0';
        db_inputs[i] = sqls[i];
        db_lens[i] = l;
    }
    kadd_dfa_batch_t db = {db_inputs, db_lens, n, db_outs, db_dims, bs};
    (void)kadd_dfa_engine_tokenize_batch(g_fuzz_context.dfa_engine, &db);
    for (int i = 0; i < n; ++i) {
        free(db_outs[i]);
    }
    FzCountApi(FZ_API_DFA_ENGINE_TOKENIZE_BATCH);

    /* ---- Feature：随机 token 序列（1/2） ---- */
    int seq[64];
    const int seq_len = 1 + (int)(rnd() % 64u);
    for (int i = 0; i < seq_len; ++i) {
        seq[i] = (int)(rnd() % 29u);
    }
    int feat[KADD_FEATURE_DIM] = {0};
    (void)kadd_seq_to_feature(seq, seq_len, (int)(rnd() % 101u), feat,
                              KADD_FEATURE_DIM);
    FzCountApi(FZ_API_SEQ_TO_FEATURE);

    /* ---- Feature：随机批量（2/2） ---- */
    int seqs4[4][64];
    const int *fb_seqs[4];
    int fb_lens[4];
    int fb_feats[4 * KADD_FEATURE_DIM] = {0};
    for (int i = 0; i < n; ++i) {
        const int l = 1 + (int)(rnd() % 64u);
        for (int k = 0; k < l; ++k) {
            seqs4[i][k] = (int)(rnd() % 29u);
        }
        fb_seqs[i] = seqs4[i];
        fb_lens[i] = l;
    }
    kadd_feature_batch_t fb = {fb_seqs, fb_lens, n, fb_feats,
                               KADD_FEATURE_DIM, (int)(rnd() % 101u), bs};
    (void)kadd_seq_to_feature_batch(&fb);
    FzCountApi(FZ_API_SEQ_TO_FEATURE_BATCH);

    /* ---- 公共 ML（SVE=原生 RF，ONNX=RF ONNX） ---- */
    const int ml_dim = kadd_model_get_feature_dim(g_fuzz_context.ml_model);
    FzCountApi(FZ_API_MODEL_GET_FEATURE_DIM);
    float probs[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (ml_dim > 0 && ml_dim <= 4096) {
        float fv[4 * 4096];
        for (int i = 0; i < ml_dim; ++i) {
            fv[i] = (float)(rnd() % 10000u) * 0.0001f;
        }
        (void)kadd_model_predict(g_fuzz_context.ml_model, fv, probs);
        FzCountApi(FZ_API_MODEL_PREDICT);
        for (int i = 0; i < ml_dim * n; ++i) {
            fv[i] = (float)(rnd() % 10000u) * 0.0001f;
        }
        (void)kadd_model_predict_batch(g_fuzz_context.ml_model, fv, n,
                                       probs, bs);
        FzCountApi(FZ_API_MODEL_PREDICT_BATCH);
    }

    /* ---- 公共 DL（CNN：token 为 [0,28] 整数） ---- */
    const int dl_dim = kadd_dl_model_get_feature_dim(g_fuzz_context.dl_model);
    FzCountApi(FZ_API_DL_MODEL_GET_FEATURE_DIM);
    if (dl_dim > 0 && dl_dim <= 4096) {
        float tv[4 * 4096];
        for (int i = 0; i < dl_dim; ++i) {
            tv[i] = (float)(rnd() % 29u);
        }
        (void)kadd_dl_model_predict(g_fuzz_context.dl_model, tv, probs);
        FzCountApi(FZ_API_DL_MODEL_PREDICT);
        for (int i = 0; i < dl_dim * n; ++i) {
            tv[i] = (float)(rnd() % 29u);
        }
        (void)kadd_dl_model_predict_batch(g_fuzz_context.dl_model, tv, n,
                                          probs, bs);
        FzCountApi(FZ_API_DL_MODEL_PREDICT_BATCH);
    }

    /* ---- SQLi 顶层检测：随机 SQL 字符串 ---- */
    (void)kadd_sqli_detect(g_fuzz_context.sqli_engine, sql1, probs);
    FzCountApi(FZ_API_SQLI_DETECT);
    const char *sd_inputs[4];
    for (int i = 0; i < n; ++i) {
        sd_inputs[i] = sqls[i];
    }
    (void)kadd_sqli_detect_batch(g_fuzz_context.sqli_engine, sd_inputs, n,
                                 probs, bs);
    FzCountApi(FZ_API_SQLI_DETECT_BATCH);

    /* ---- 内部 CNN：原生 .kcn（token 为 [0,28] 整数） ---- */
    const int cnn_dim = kadd_cnn_model_get_feature_dim(g_fuzz_context.cnn_model);
    FzCountApi(FZ_API_CNN_MODEL_GET_FEATURE_DIM);
    if (cnn_dim > 0 && cnn_dim <= 4096) {
        float cv[4 * 4096];
        for (int i = 0; i < cnn_dim; ++i) {
            cv[i] = (float)(rnd() % 29u);
        }
        (void)kadd_cnn_model_predict(g_fuzz_context.cnn_model, cv, probs);
        FzCountApi(FZ_API_CNN_MODEL_PREDICT);
        for (int i = 0; i < cnn_dim * n; ++i) {
            cv[i] = (float)(rnd() % 29u);
        }
        (void)kadd_cnn_model_predict_batch(g_fuzz_context.cnn_model, cv, n,
                                           probs, bs);
        FzCountApi(FZ_API_CNN_MODEL_PREDICT_BATCH);
    }

    /* ---- 内部 RF：原生 .krf ---- */
    const int rf_dim = kadd_rf_model_get_feature_dim(g_fuzz_context.rf_model);
    FzCountApi(FZ_API_RF_MODEL_GET_FEATURE_DIM);
    if (rf_dim > 0 && rf_dim <= 4096) {
        float rv[4 * 4096];
        for (int i = 0; i < rf_dim; ++i) {
            rv[i] = (float)(rnd() % 10000u) * 0.0001f;
        }
        (void)kadd_rf_model_predict(g_fuzz_context.rf_model, rv, probs);
        FzCountApi(FZ_API_RF_MODEL_PREDICT);
        for (int i = 0; i < rf_dim * n; ++i) {
            rv[i] = (float)(rnd() % 10000u) * 0.0001f;
        }
        (void)kadd_rf_model_predict_batch(g_fuzz_context.rf_model, rv, n,
                                          probs, bs);
        FzCountApi(FZ_API_RF_MODEL_PREDICT_BATCH);
    }

    /* ---- 内部 Inference：ONNX RF ---- */
    const int inf_dim =
        kadd_inference_engine_get_feature_dim(g_fuzz_context.inference_engine);
    FzCountApi(FZ_API_INFERENCE_ENGINE_GET_FEATURE_DIM);
    if (inf_dim > 0 && inf_dim <= 4096) {
        float iv[4 * 4096];
        for (int i = 0; i < inf_dim; ++i) {
            iv[i] = (float)(rnd() % 10000u) * 0.0001f;
        }
        (void)kadd_inference_predict(g_fuzz_context.inference_engine, iv,
                                     probs);
        FzCountApi(FZ_API_INFERENCE_PREDICT);
        for (int i = 0; i < inf_dim * n; ++i) {
            iv[i] = (float)(rnd() % 10000u) * 0.0001f;
        }
        (void)kadd_inference_predict_batch(g_fuzz_context.inference_engine,
                                           iv, n, probs, bs);
        FzCountApi(FZ_API_INFERENCE_PREDICT_BATCH);
    }
}

/*
 * 真实资产重量级生命周期的触发间隔（每 N 次迭代执行一个相位）。
 *
 * 每个相位都会真实打开文件并创建 ONNX 会话（毫秒~数十毫秒级）。
 * 普通 fuzz 与等频长稳模式都会执行本函数（长稳模式不跳过随机测试）。
 * 原 1/64 在常规覆盖率跑中也会形成明显长尾，降到 1/1024 后每个相位仍有
 * 足够执行次数，同时把吞吐让给 selector 的廉价生命周期用例。
 * 调频只需改本常量：触发判定与相位轮转均由它推导，不会失步。
 */
static const uint64_t kFzHeavyInterval = 1024u;

/**
 * @brief 真实资产重量级生命周期（每 kFzHeavyInterval 次迭代执行一个相位）
 *
 * 相位 0: ML 公共 API    kadd_model_load(.onnx) → predict → predict_batch → free
 * 相位 1: DL 公共 API    kadd_dl_model_load(.kcn/.onnx) → predict → predict_batch → free
 * 相位 2: SQLi 公共 API  create_from_file → detect → detect_batch → free
 * 相位 3: 内部原生       真实 .krf/.kcn load → predict → predict_batch → free
 * 每个相位内部 load:predict:predict_batch:free = 1:1:1:1。
 */
static void FzHeavyRealLifecycle(uint64_t it) {
    const uint64_t phase = (it / kFzHeavyInterval) % 4u;
    if (phase == 0) {
        const char *mp = FzPickPath(kRfOnnxPath, "assets/model/model_tokens_rf.onnx");
        kadd_model_handle_t *h = nullptr;
        if (kadd_model_load(&h, mp, 0) == 0 && h) {
            int dim = kadd_model_get_feature_dim(h);
            if (dim <= 0 || dim > 4096) dim = 28;
            std::vector<float> v((size_t)dim * 4u, 0.1f);
            float p1 = 0.0f;
            float out[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            (void)kadd_model_predict(h, v.data(), &p1);
            (void)kadd_model_predict_batch(h, v.data(), 4, out, 4);
            kadd_model_free(h);
        } else if (h) {
            kadd_model_free(h);
        }
    } else if (phase == 1) {
        const char *cands[] = {kCnnKcnPath, kCnnKcnRel, kCnnOnnxPath,
                               "assets/model/model_tokens_dl_cnn.onnx"};
        kadd_dl_model_t *m = nullptr;
        for (size_t c = 0; c < sizeof(cands) / sizeof(cands[0]) && !m; ++c) {
            if (kadd_dl_model_load(&m, cands[c], 0) != 0 && m) {
                kadd_dl_model_free(m);
                m = nullptr;
            }
        }
        if (!m) return;
        int dim = kadd_dl_model_get_feature_dim(m);
        if (dim <= 0 || dim > 4096) { kadd_dl_model_free(m); return; }
        std::vector<float> v((size_t)dim * 4u);
        for (size_t k = 0; k < v.size(); ++k) v[k] = (float)(k % 13u);
        float p1 = 0.0f;
        float out[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        (void)kadd_dl_model_predict(m, v.data(), &p1);
        (void)kadd_dl_model_predict_batch(m, v.data(), 4, out, 4);
        kadd_dl_model_free(m);
    } else if (phase == 2) {
        kadd_sqli_engine_t *e = nullptr;
        if (kadd_sqli_engine_create_from_file(&e, ResolveConfigPath()) == 0 && e) {
            float pr = 0.0f;
            (void)kadd_sqli_detect(e, "SELECT * FROM users WHERE id=1 OR 1=1--", &pr);
            const char *ins[2] = {"id=1 OR 1=1", "plain text"};
            float ps[2] = {0.0f, 0.0f};
            (void)kadd_sqli_detect_batch(e, ins, 2, ps, 2);
            kadd_sqli_engine_free(e);
        } else if (e) {
            kadd_sqli_engine_free(e);
        }
    } else {
        /* 内部原生：真实 .krf + .kcn 生命周期 */
        FuzzRFEnginePristineModel();
        const char *cp = FzPickPath(kCnnKcnPath, kCnnKcnRel);
        kadd_cnn_model_t *m = nullptr;
        if (kadd_cnn_model_load(&m, cp) == 0 && m) {
            int dim = kadd_cnn_model_get_feature_dim(m);
            if (dim <= 0 || dim > 4096) dim = 28;
            std::vector<float> v((size_t)dim);
            for (int i = 0; i < dim; ++i) v[(size_t)i] = (float)(i % 17);
            std::vector<float> mat((size_t)dim * 4u);
            for (size_t k = 0; k < mat.size(); ++k) mat[k] = (float)(k % 13u);
            float p1 = 0.0f;
            float out[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            (void)kadd_cnn_model_predict(m, v.data(), &p1);
            (void)kadd_cnn_model_predict_batch(m, mat.data(), 4, out, 4);
            kadd_cnn_model_free(m);
        } else if (m) {
            kadd_cnn_model_free(m);
        }
    }
}

/* ========== 吞吐自监控（单行状态文件；stdout/stderr 默认静音） ==========
 * libFuzzer 的 stat::average_exec_per_sec = 已执行单元数 / 进程启动至今秒数，
 * 分母含 LLVMFuzzerInitialize（-max_total_time 同样从进程启动计时），且口径是
 * 迭代数而非对外接口调用次数，不能用来判断快慢与长稳达标。故自报：
 *   init_s 初始化耗时 | win_exec_s 稳态吞吐 | all_exec_s 与 libFuzzer 同口径
 *   api_calls 21 个等频随机接口的调用总和（参考值）
 *   api=[dfa_engine_tokenize ... inference_engine_get_feature_dim] 21 个接口
 *        独立计数（selector/heavy 的额外调用不重复计入）
 *   api_min 等频随机接口最小计数——长稳达标口径：单接口 ≥ 目标值（3000 万）
 *   eq_ok   等频随机层句柄是否全部就绪（0 = 缺资产，api_min 会归零）
 * 写入时机有四个，保证任何时长/任何停止点都有真实计数——只有 10s 定时的
 * 话，快机器上迭代可能不足 10s，文件停留在 init 的 iter=0 行，长稳报告会
 * 误读成「未执行」；libFuzzer 的停止计数与 TestOneInput 调用数还有 ±1 的
 * 口径差，精确等值判断同样会漏掉最后一次写入：
 *   1) 每 10s（kStatIntervalSec）    2) 每 4096 次迭代（kStatIterInterval）
 *   3) -runs 目标前 4096 次迭代起逐次写（kStatTailWindow 尾段窗口）
 *   4) -runs=<N> 跑满的最后一次迭代（g_fz_runs_target，从 argv 解析）
 * 上报方式是「覆盖写单行状态文件」而不是打印：日志量与迭代数无关，单函数 3000 万次
 * 长稳每个 worker 始终只有 1 行。默认路径 ./kadd_fuzz_stat_<pid>.txt，
 * KADD_FUZZ_STAT_FILE 可指定（多路并行时给每路一个独立路径）。
 * KADD_FUZZ_KEEP_STDOUT=1 时额外打到 stderr；KADD_FUZZ_QUIET_STATS=1 全关。
 */
static const std::chrono::steady_clock::time_point g_fz_t0 =
    std::chrono::steady_clock::now();   /* 静态初始化 ≈ 进程启动 */
static const double kStatIntervalSec = 10.0;
static const uint64_t kStatIterInterval = 4096;  /* 迭代心跳：与时间上报互补 */
static const uint64_t kStatTailWindow = 4096;    /* -runs 尾段：进入窗口后逐次写入 */
static uint64_t g_fz_runs_target = 0;            /* -runs=N；跑满时写最终统计 */
static double g_fz_init_sec   = 0.0;
static uint64_t g_fz_last_iter = 0;
static double g_fz_last_sec   = 0.0;
static bool g_fz_quiet_stats  = false;
static bool g_fz_echo_stats   = false;   /* 未静音时同时打 stderr，便于交互式观察 */
static std::string g_fz_stat_path;        /* 空串 = 不写文件 */

static double FzNowSec() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_fz_t0).count();
}

static int FzPid() {
#if defined(_WIN32)
    return _getpid();
#else
    return (int)getpid();
#endif
}

static uint64_t FzApiMin() {
    uint64_t m = g_fz_api_n[0];
    for (int i = 1; i < FZ_API_COUNT; ++i) {
        if (g_fz_api_n[i] < m) { m = g_fz_api_n[i]; }
    }
    return m;
}

/* [FIX 安全函数整改] 使用 C++ string 拼接，避免 snprintf（类型安全、无截断） */
static void FzAppendApiToken(std::string &line, const char *name, uint64_t value) {
    line += name;
    line += '=';
    line += std::to_string(value);
    line += ' ';
}

/* 与原 snprintf 的 %.Nf 输出保持一致（fuzz_longrun.sh 按此格式解析） */
static std::string FzFormatDouble(double v, int precision) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(precision) << v;
    return os.str();
}

static void FzReportStats(uint64_t it) {
    if (g_fz_quiet_stats) { return; }
    const double now = FzNowSec();
    const double win = now - g_fz_last_sec;
    const uint64_t dit = it - g_fz_last_iter;
    const double win_rate = (win > 1e-9) ? (double)dit / win : 0.0;
    const double all_rate = (now > 1e-9) ? (double)it / now : 0.0;
    uint64_t api_calls = 0;
    for (int i = 0; i < FZ_API_COUNT; ++i) {
        api_calls += g_fz_api_n[i];
    }
    std::string line = "kadd_fuzz_stat: pid=" + std::to_string(FzPid()) +
                       " iter=" + std::to_string(it) +
                       " up_s=" + FzFormatDouble(now, 0) +
                       " init_s=" + FzFormatDouble(g_fz_init_sec, 1) +
                       " win_exec_s=" + FzFormatDouble(win_rate, 0) +
                       " all_exec_s=" + FzFormatDouble(all_rate, 0) +
                       " eq_ok=" + std::to_string(g_fz_equal_ready ? 1 : 0) +
                       " api_calls>=" + std::to_string(api_calls) + " api=[";

    for (int i = 0; i < FZ_API_COUNT; ++i) {
        FzAppendApiToken(line, kFzApiNames[i], g_fz_api_n[i]);
    }
    line += "] api_min>=" + std::to_string(FzApiMin()) + "\n";
    if (!g_fz_stat_path.empty()) {
        FILE *f = fopen(g_fz_stat_path.c_str(), "w");   /* "w"：每次覆盖，文件恒为 1 行 */
        if (f != nullptr) {
            (void)fputs(line.c_str(), f);
            (void)fclose(f);
        }
    }
    if (g_fz_echo_stats) {
        (void)fputs(line.c_str(), stderr);
        (void)fflush(stderr);
    }
    g_fz_last_iter = it;
    g_fz_last_sec  = now;
}

/* ========== LibFuzzer主入口函数 ========== */

/**
 * @brief LibFuzzer目标函数
 *
 * @param data FUZZ输入数据
 * @param size 数据大小
 * @return 0 表示成功
 */
#if defined(__clang__) && defined(__llvm_profile__)
/* clang -fprofile-instr-generate: libFuzzer 用 syscall 退出跳过 atexit，
 * profraw 恒为 0 字节。在每次迭代末尾定期 flush profile 数据。
 * 仅在启用 profile instrumentation 时链接 __llvm_profile_write_file，
 * 否则（纯 -fsanitize=fuzzer 构建）该符号不存在，会 undefined reference。 */
extern "C" void __llvm_profile_write_file(void);
static int g_fuzz_iter_count = 0;
#define FUZZ_PROFILE_FLUSH_INTERVAL 500
static inline void LLVMFuzzerTestOneInput_flush_helper() {
    if (++g_fuzz_iter_count % FUZZ_PROFILE_FLUSH_INTERVAL == 0) {
        __llvm_profile_write_file();
    }
}
#else
static inline void LLVMFuzzerTestOneInput_flush_helper() {}
#endif

/* ========== 分层调度（覆盖率权重设计） ==========
 *
 * selector = data[0]（0..255）划分三层：
 *   [0,216]   公共 API 层（include 目录公共头导出接口）         217/256 ≈ 84.8%
 *   [217,247] 内部 API 层（include/internal 目录内部头接口）     31/256 ≈ 12.1%
 *   [248,255] 自由输入层（原始随机字节 → 公共检测接口）      8/256 ≈  3.1%
 * 公共 + 自由层合计 ≈ 87.9%、内部层 ≈ 12.1%，约等于 85% : 15% 的目标比重
 *（对外接口通过 include 公共头文件判别，内部接口来自 include/internal）。
 *
 * 公共层内部按引擎分 7 组，每组 31 个槽位：
 *   SQLi / DFA / Feature / Config / ML / DL / 综合边界。
 * 所有模型类用例内部均为完整生命周期：load → predict → predict_batch → free，
 * 四个动作 1:1:1:1 同频执行，普通 fuzz 运行下不会出现频次失衡或句柄累积。
 *
 * 全局节奏（静态迭代计数器驱动）：
 *   selector 分层；每 8 次迭代执行合成 RF 生命周期，每 1024 次迭代执行
 *   FzHeavyRealLifecycle()，每 256 次迭代执行 ORT 故障注入。
 *   KADD_FUZZ_EQUAL_RANDOM=1 时，以上随机 fuzz 全部保留，并额外叠加
 *   FzEqualRandomApiLayer()：21 个可长跑开放函数各 1 次真实随机调用。
 */
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static uint64_t s_fz_iter = 0;

    InitializeEngines();

    /* 等频随机层叠加在完整随机 fuzz 之上（不替代、不跳过 selector） */
    if (g_fuzz_equal_random) {
        FzEqualRandomApiLayer(data, size);
    }

    if (size == 0 || size > 4096) {
        return 0;
    }

    // 用首字节选择执行层与分组，避免每次全跑
    const uint8_t selector = data[0];
    const uint8_t *payload = data + 1;
    size_t payload_size = size - 1;
    std::string input = CreateSafeString(payload, payload_size);

    if (selector <= 216) {
        /* ---- 公共 API 层（include 公共头），7 组 × 31 槽位 ---- */
        const int u = selector % 217;
        if (u < 31) {                 /* SQLi 组（公共顶层检测接口） */
            switch (u % 4) {
                case 0: FuzzSQLiEngine(input); break;
                case 1: FuzzSQLiEngineRich(payload, payload_size); break;
                case 2: FuzzBatchDetection(payload, payload_size);
                        FuzzSqliErrorPaths(); break;
                default: FuzzSQLiEngineFree(data, size);
                         FuzzBatchErrorPaths(); break;
            }
        } else if (u < 62) {          /* DFA 组 */
            switch (u % 3) {
                case 0: FuzzDFAEngine(input); break;
                case 1: FuzzDfaErrorPaths(); break;
                default: FuzzDfaBatchErrorPaths(); break;
            }
        } else if (u < 93) {          /* Feature 组 */
            switch (u % 4) {
                case 0: FuzzFeatureEngine(input); break;
                case 1: FuzzFeatureEngineLong(payload, payload_size); break;
                case 2: FuzzFeatureEngineOutOfRange(payload, payload_size); break;
                default: FuzzFeatureEngineBatch(payload, payload_size);
                         FuzzFeatureErrorPaths(); break;
            }
        } else if (u < 124) {         /* Config 组 */
            if (u % 2 == 0) {
                FuzzConfigLoader(payload, payload_size);
            } else {
                FuzzConfigErrorPaths();
            }
        } else if (u < 155) {         /* ML 组（公共 kadd_model_* 生命周期） */
            switch (u % 3) {
                case 0: FuzzMLEngine(input); break;
                case 1: FuzzMLEngineNative(payload, payload_size); break;
                default: FuzzMLLoadVariants(payload, payload_size); break;
            }
        } else if (u < 186) {         /* DL 组（公共 kadd_dl_model_* 生命周期） */
            switch (u % 4) {
                case 0: FuzzDLEngine(input); break;
                case 1: FuzzDLEngineNative(payload, payload_size); break;
                case 2: FuzzDLLoadOnnxBackend(payload, payload_size); break;
                default: FuzzDlErrorPaths(); break;
            }
        } else {                      /* 综合边界组（公共 API 边界/NULL/初始化失败） */
            switch (u % 4) {
                case 0: FuzzEdgeCases(payload, payload_size); break;
                case 1: FuzzBoundaryConditions(payload, payload_size); break;
                case 2: FuzzInitFailures(payload, payload_size); break;
                default: FuzzNullPointers(data, size); break;
            }
        }
    } else if (selector <= 247) {
        /* ---- 内部 API 层（include/internal 内部头：rf/cnn/inference 直连） ---- */
        const int u = selector - 217;
        switch (u % 6) {
            case 0: FuzzRFEngineDirect(payload, payload_size); break;
            case 1: FuzzCNNEngineDirect(payload, payload_size); break;
            case 2: FuzzRfRealModelPaths(); break;
            case 3: FuzzCnnRealModelPaths(); break;
            case 4: FuzzInferenceEngine(data, size);
                    FuzzInferencePublicValidation(); break;
            default: FuzzInferenceEngineONNX(payload, payload_size); break;
        }
    } else {
        /* ---- 自由输入层：原始随机字节直接喂公共检测接口 ---- */
        FuzzSQLiEngine(input);
    }

    const uint64_t it = ++s_fz_iter;

    if (FzNowSec() - g_fz_last_sec >= kStatIntervalSec ||
        (it % kStatIterInterval) == 0) {
        FzReportStats(it);   /* 按时间/迭代上报：短跑与长跑都有真实计数 */
    }

    if ((it % 8) == 0) {
        /* 内部合成 RF 微型模型生命周期（1/8 频率） */
        FuzzRfLoadAndExercise("/tmp/kadd_fz_rf_tiny.krf");
    }
    if ((it % kFzHeavyInterval) == 0) {
        /* 真实资产重量级生命周期（1/kFzHeavyInterval 频率，4 相位轮转） */
        FzHeavyRealLifecycle(it);
    }
    if ((it % 256) == 0) {
        FuzzInferenceFaultInjection();  /* ORT 故障注入（inject 关闭时为空） */
    }

    if (g_fz_runs_target != 0 && it + kStatTailWindow >= g_fz_runs_target) {
        FzReportStats(it);   /* 尾段逐次写入：无论停止口径差多少，真实终值必落盘 */
    }

#if defined(__clang__)
    LLVMFuzzerTestOneInput_flush_helper();
#endif
    return 0;
}

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv) {
    const char *equal_random = getenv("KADD_FUZZ_EQUAL_RANDOM");
    g_fuzz_equal_random = (equal_random != nullptr &&
                           strcmp(equal_random, "1") == 0);

    /* 解析 -runs=<N>：跑满最后一次迭代时补写最终统计。仅靠定时上报时，
     * 快机器上 11.7 万次迭代可能不足 10s，状态文件会停留在 init 的 iter=0
     * 行，长稳报告误读成「未执行 / api_calls=0」。 */
    if (argc != nullptr && argv != nullptr && *argv != nullptr) {
        for (int i = 1; i < *argc; ++i) {
            const char *a = (*argv)[i];
            if (a != nullptr && strncmp(a, "-runs=", 6) == 0) {
                const unsigned long long v = strtoull(a + 6, nullptr, 10);
                if (v > 0) { g_fz_runs_target = (uint64_t)v; }
            }
        }
    }
    // 初始化引擎 + 构造合成资产（微型 DFA 表 / 签名 RF 模型 / 合成 config）。
    // 等频随机模式在此基础上补 3 个内部句柄（rf/cnn/inference），
    // 全部走真实资产，缺资产时 fail-visible（eq_ok=0，不跑 NULL 凑数）。
    InitializeEngines();
    BuildSyntheticAssets();
    if (g_fuzz_equal_random) {
        FzInitEqualRandomHandles();
    }
    FuzzInferenceThreadEnvBranches();

    /* 吞吐自监控基准：记录初始化耗时（libFuzzer 会把它算进
     * stat::average_exec_per_sec 的分母，也算进 -max_total_time 的预算） */
    g_fz_init_sec  = FzNowSec();
    g_fz_last_sec  = g_fz_init_sec;
    g_fz_last_iter = 0;
    g_fz_quiet_stats = (getenv("KADD_FUZZ_QUIET_STATS") != nullptr);

    /* ---- 长稳静音：必须放在初始化之后 ----
     * 库的成功路径 INFO 走 stdout，错误路径用例触发的 ERROR/WARN 走 stderr，
     * 两者都随迭代线性增长（单函数 3000 万次长稳可达 GB 级），既淹没 fuzz 输出也拖慢
     * 吞吐，因此 stdout 与 stderr 一并重定向到 /dev/null，进度改看单行状态文件。
     * 初始化只发生一次，其日志必须保留——否则「忘了 source kadd_env.sh 导致
     * HMAC 密钥缺失、模型全部加载失败」这类问题会静默退化成只跑错误路径。
     * KADD_FUZZ_KEEP_STDOUT=1 保留全部输出：交互式验证、复现崩溃样本时用
     *   KADD_FUZZ_KEEP_STDOUT=1 ./kadd_fuzzer <artifact_prefix>/crash-xxx -runs=1
     * 崩溃输入由 libFuzzer 落到 -artifact_prefix，不受静音影响。 */
    const bool keep_output = (getenv("KADD_FUZZ_KEEP_STDOUT") != nullptr);
#if defined(__linux__)
    if (!keep_output) {
        (void)freopen("/dev/null", "w", stdout);
        (void)freopen("/dev/null", "w", stderr);
    }
    g_fz_echo_stats = keep_output;
#else
    (void)keep_output;   /* 非 Linux 不做重定向（仅本地语法校验用） */
    g_fz_echo_stats = true;
#endif

    /* 状态文件路径：默认按 pid 命名，多 worker 同目录并行时互不覆盖 */
    const char *stat_env = getenv("KADD_FUZZ_STAT_FILE");
    if (stat_env != nullptr && stat_env[0] != '\0') {
        /* [FIX 安全函数整改] std::string 按需增长，替代定长 snprintf 拷贝 */
        g_fz_stat_path = stat_env;
    } else {
        std::ostringstream path_os;
        path_os << "kadd_fuzz_stat_" << FzPid() << ".txt";
        g_fz_stat_path = path_os.str();
    }
    FzReportStats(0);
    return 0;
}
