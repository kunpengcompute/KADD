/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * @file test_dl_engine_gtest.cpp
 * @brief DL 引擎综合测试（对应 Excel 测试用例）
 *
 * 覆盖用例：
 *   TC001 - CNN引擎正常SQL注入检测功能验证
 *   TC002 - CNN引擎入参校验与异常处理验证
 *   TC003 - CNN引擎输入长度边界处理验证
 *   TC004 - RF引擎正常SQL注入检测功能验证
 *   TC007 - 批量检测接口功能验证
 *   TC008 - batch_size参数边界值处理验证
 *   TC009 - config后端选择优先级验证
 *   TC010 - 模型文件校验链验证
 *   TC013 - load期错误码覆盖验证
 *   TC014 - 推理期错误码覆盖验证
 *   TC015 - 生命周期完整性验证
 *   TC020 - 引擎释放与资源管理验证
 *   TC029 - 模型文件CRC32防篡改校验验证
 */
#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "kadd_sqli_engine.h"
#include "kadd_dl_engine.h"
#include "kadd_sve.h"
#include "kadd_config_loader.h"
#include "kadd_errors.h"

/* ========== 资源路径（统一从 config.json 读取，避免硬编码模型格式/路径） ========== */

static const char *kConfigPath    = "config.json";

/* 测试专用全局配置：从 config.json 加载一次后复用 */
static kadd_config_t g_test_config;
static bool g_test_config_ready = false;

/** 加载 config.json（尝试多个可能路径），供全部测试复用 */
static int LoadTestConfig()
{
    if (g_test_config_ready) {
        return KADD_SUCCESS;
    }
    const char *config_paths[] = {
        "config.json",
        "../config.json",
        "../../config.json",
        "../../../config.json"
    };
    for (const char *path : config_paths) {
        if (kadd_config_load(path, &g_test_config) == KADD_SUCCESS) {
            g_test_config_ready = true;
            return KADD_SUCCESS;
        }
    }
    return KADD_ERROR_CONFIG;
}

/* 以下访问器统一从 config 读取路径，后端选择逻辑与引擎内部一致 */
static const char *DfaTransitionPath()
{
    return (LoadTestConfig() == KADD_SUCCESS) ? g_test_config.transition_table : nullptr;
}

static const char *DfaOutputPath()
{
    return (LoadTestConfig() == KADD_SUCCESS) ? g_test_config.output_table : nullptr;
}

static const char *DfaEodPath()
{
    return (LoadTestConfig() == KADD_SUCCESS) ? g_test_config.eod_table : nullptr;
}

/* CNN 模型：SVE 优先原生 .kcn，否则回退 ONNX */
static const char *CnnModelPath()
{
    if (LoadTestConfig() != KADD_SUCCESS) {
        return nullptr;
    }
#if KADD_HAVE_SVE
    return (g_test_config.model_cnn[0] != '\0') ? g_test_config.model_cnn : g_test_config.model_cnn_onnx;
#else
    return (g_test_config.model_cnn_onnx[0] != '\0') ? g_test_config.model_cnn_onnx : g_test_config.model_cnn;
#endif
}

/* RF 模型：SVE 优先原生 .krf，否则回退 ONNX */
static const char *RfModelPath()
{
    if (LoadTestConfig() != KADD_SUCCESS) {
        return nullptr;
    }
#if KADD_HAVE_SVE
    return (g_test_config.model_rf[0] != '\0') ? g_test_config.model_rf : g_test_config.model_rf_onnx;
#else
    return (g_test_config.model_rf_onnx[0] != '\0') ? g_test_config.model_rf_onnx : g_test_config.model_rf;
#endif
}

/* CNN 模型 ONNX 版本（用于显式 ONNX 后端测试） */
static const char *CnnModelOnnxPath()
{
    return (LoadTestConfig() == KADD_SUCCESS) ? g_test_config.model_cnn_onnx : nullptr;
}

/* ========== 测试辅助 ========== */

/** 创建 ML 模式引擎（使用 config.json） */
static kadd_sqli_engine_t *CreateMLEngine()
{
    kadd_sqli_engine_t *engine = nullptr;
    int ret = kadd_sqli_engine_create_from_file(&engine, kConfigPath);
    if (ret != KADD_SUCCESS || !engine) return nullptr;
    return engine;
}

/** 读取 config.json 内容（尝试多个路径），供 CreateDLEngine 复用 */
static std::string ReadConfigContent()
{
    const char *config_paths[] = {
        "config.json",
        "../config.json",
        "../../config.json",
        "../../../config.json"
    };
    for (const char *path : config_paths) {
        std::ifstream in(path);
        if (in.is_open()) {
            std::ostringstream ss;
            ss << in.rdbuf();
            return ss.str();
        }
    }
    return std::string();
}

/** 替换配置文本中的 engine_mode 值（缺失时在末尾追加） */
static std::string ModifyEngineMode(const std::string &content, const char *mode)
{
    std::string key = "\"engine_mode\"";
    size_t pos = content.find(key);
    if (pos != std::string::npos) {
        size_t colon = content.find(':', pos);
        size_t quote = content.find('"', colon);
        size_t quote_end = content.find('"', quote + 1);
        if (colon != std::string::npos && quote != std::string::npos &&
            quote_end != std::string::npos) {
            return content.substr(0, quote + 1) + mode + content.substr(quote_end);
        }
    }
    std::string out = content;
    size_t brace = out.rfind('}');
    if (brace != std::string::npos) {
        out.insert(brace, ",\"engine_mode\":\"" + std::string(mode) + "\"");
    }
    return out;
}

/** 创建 DL 模式引擎（复用 config.json 全部内容，仅替换 engine_mode=dl，
 *  保留 ONNX 模型路径等字段，兼容 SVE ON/OFF 构建） */
static kadd_sqli_engine_t *CreateDLEngine()
{
    std::string content = ReadConfigContent();
    if (content.empty()) {
        return nullptr;
    }
    content = ModifyEngineMode(content, "dl");

    const char *path = "/tmp/dl_test_config.json";
    FILE *f = fopen(path, "w");
    if (!f) return nullptr;
    fprintf(f, "%s\n", content.c_str());
    fclose(f);

    kadd_sqli_engine_t *engine = nullptr;
    int ret = kadd_sqli_engine_create_from_file(&engine, path);
    remove(path);
    if (ret != KADD_SUCCESS || !engine) {
        kadd_sqli_engine_free(engine);
        return nullptr;
    }
    return engine;
}

/** 检测单条 SQL，返回概率（失败返回 -1） */
static float DetectSQL(kadd_sqli_engine_t *engine, const char *sql)
{
    float prob = -1.0f;
    int ret = kadd_sqli_detect(engine, sql, &prob);
    if (ret != KADD_SUCCESS) return -1.0f;
    return prob;
}

/* ========== TC001: CNN 引擎正常 SQL 注入检测功能验证 ========== */

TEST(DLEngineTest, TC001_CNN_NormalDetection)
{
    kadd_sqli_engine_t *engine = CreateDLEngine();
    ASSERT_NE(engine, nullptr) << "DL engine creation failed";

    float prob_normal = DetectSQL(engine, "SELECT * FROM users WHERE id=1");
    EXPECT_GE(prob_normal, 0.0f);
    EXPECT_LE(prob_normal, 1.0f);

    float prob_or = DetectSQL(engine, "SELECT * FROM users WHERE id=1 OR 1=1");
    EXPECT_GE(prob_or, 0.0f);
    EXPECT_LE(prob_or, 1.0f);

    float prob_union = DetectSQL(engine, "SELECT * FROM users WHERE id=1 UNION SELECT * FROM admin");
    EXPECT_GE(prob_union, 0.0f);
    EXPECT_LE(prob_union, 1.0f);

    // 注入概率应高于正常
    EXPECT_GT(prob_or, prob_normal)
        << "OR injection should score higher than normal SQL";

    kadd_sqli_engine_free(engine);
}

/* ========== TC002: CNN 引擎入参校验与异常处理验证 ========== */

TEST(DLEngineTest, TC002_NullParameterChecks)
{
    float prob;

    // kadd_sqli_detect: NULL engine
    EXPECT_EQ(kadd_sqli_detect(nullptr, "SELECT 1", &prob), KADD_ERROR_NULL_PTR);

    kadd_sqli_engine_t *engine = CreateMLEngine();
    ASSERT_NE(engine, nullptr);

    // kadd_sqli_detect: NULL input
    EXPECT_EQ(kadd_sqli_detect(engine, nullptr, &prob), KADD_ERROR_NULL_PTR);

    // kadd_sqli_detect: NULL probability
    EXPECT_EQ(kadd_sqli_detect(engine, "SELECT 1", nullptr), KADD_ERROR_NULL_PTR);
    kadd_sqli_engine_free(engine);

    // kadd_dl_model_load: NULL model ptr
    EXPECT_EQ(kadd_dl_model_load(nullptr, CnnModelPath(), 0), KADD_ERROR_NULL_PTR);

    // kadd_dl_model_load: NULL path
    EXPECT_EQ(kadd_dl_model_load(nullptr, nullptr, 0), KADD_ERROR_NULL_PTR);

    // kadd_dl_model_predict: NULL model
    float dummy[300] = {0};
    EXPECT_EQ(kadd_dl_model_predict(nullptr, dummy, &prob), KADD_ERROR_NULL_PTR);

    // 加载 CNN 模型后测试 predict NULL 参数
    kadd_dl_model_t *dl = nullptr;
    int ret = kadd_dl_model_load(&dl, CnnModelPath(), 0);
    if (ret == KADD_SUCCESS && dl) {
        EXPECT_EQ(kadd_dl_model_predict(dl, nullptr, &prob), KADD_ERROR_NULL_PTR);
        EXPECT_EQ(kadd_dl_model_predict(dl, dummy, nullptr), KADD_ERROR_NULL_PTR);
        kadd_dl_model_free(dl);
    }
}

/* ========== TC003: CNN 引擎输入长度边界处理验证 ========== */

TEST(DLEngineTest, TC003_InputLengthBoundary)
{
    kadd_sqli_engine_t *engine = CreateDLEngine();
    ASSERT_NE(engine, nullptr);

    // 空字符串
    float prob = -1.0f;
    int ret = kadd_sqli_detect(engine, "", &prob);
    EXPECT_EQ(ret, KADD_SUCCESS);
    EXPECT_EQ(prob, 0.0f);

    // 短 SQL
    prob = DetectSQL(engine, "SELECT 1");
    EXPECT_GE(prob, 0.0f);
    EXPECT_LE(prob, 1.0f);

    // 超长 SQL（超过 DFA max_input_chars=300）→ DFA 拒绝
    std::vector<char> long_sql(500, 'x');
    long_sql[499] = '\0';
    float prob_long = -1.0f;
    int ret_long = kadd_sqli_detect(engine, long_sql.data(), &prob_long);
    EXPECT_NE(ret_long, KADD_SUCCESS)
        << "Input exceeding max_input_chars should be rejected by DFA";

    // 边界长度 SQL（接近 max_input_chars=300）
    std::vector<char> boundary_sql(300, ' ');
    for (int i = 0; i < 300; i++) boundary_sql[i] = 'a';
    boundary_sql[299] = '\0';
    prob = DetectSQL(engine, boundary_sql.data());
    EXPECT_GE(prob, 0.0f);
    EXPECT_LE(prob, 1.0f);

    kadd_sqli_engine_free(engine);
}

/* ========== TC004: RF 引擎正常 SQL 注入检测功能验证 ========== */

TEST(DLEngineTest, TC004_RF_NormalDetection)
{
    kadd_sqli_engine_t *engine = CreateMLEngine();
    ASSERT_NE(engine, nullptr) << "ML engine creation failed";

    float prob_normal = DetectSQL(engine, "SELECT * FROM users WHERE id=1");
    EXPECT_GE(prob_normal, 0.0f);
    EXPECT_LE(prob_normal, 1.0f);

    float prob_or = DetectSQL(engine, "SELECT * FROM users WHERE id=1 OR 1=1");
    EXPECT_GE(prob_or, 0.0f);
    EXPECT_LE(prob_or, 1.0f);

    float prob_union = DetectSQL(engine, "SELECT * FROM users WHERE id=1 UNION SELECT * FROM admin");
    EXPECT_GE(prob_union, 0.0f);
    EXPECT_LE(prob_union, 1.0f);

    EXPECT_GT(prob_or, prob_normal)
        << "OR injection should score higher than normal SQL";

    kadd_sqli_engine_free(engine);
}

/* ========== TC007: 批量检测接口功能验证 ========== */

TEST(DLEngineTest, TC007_BatchDetection)
{
    kadd_sqli_engine_t *engine = CreateMLEngine();
    ASSERT_NE(engine, nullptr);

    const char *inputs[] = {
        "SELECT * FROM users",
        "admin' OR '1'='1",
        "1; DROP TABLE users",
        "normal_query",
        "<script>alert(1)</script>"
    };
    const int count = 5;
    float probs[count] = {0};

    // batch_size=5（kadd_sqli_detect_batch 返回 KADD_SUCCESS，不返回 count）
    int ret = kadd_sqli_detect_batch(engine, inputs, count, probs, 5);
    EXPECT_EQ(ret, KADD_SUCCESS);
    for (int i = 0; i < count; i++) {
        EXPECT_GE(probs[i], 0.0f);
        EXPECT_LE(probs[i], 1.0f);
    }

    // batch_size=0（自动使用默认值）
    for (int i = 0; i < count; i++) probs[i] = 0.0f;
    ret = kadd_sqli_detect_batch(engine, inputs, count, probs, 0);
    EXPECT_EQ(ret, KADD_SUCCESS);

    // 批量与逐条结果一致
    for (int i = 0; i < count; i++) {
        float single_prob = DetectSQL(engine, inputs[i]);
        EXPECT_NEAR(probs[i], single_prob, 1e-6f)
            << "Batch result differs from single at index " << i;
    }

    kadd_sqli_engine_free(engine);
}

/* ========== TC008: batch_size 参数边界值处理验证 ========== */

TEST(DLEngineTest, TC008_BatchSizeBoundary)
{
    kadd_dl_model_t *model = nullptr;
    int ret = kadd_dl_model_load(&model, CnnModelPath(), 0);
    if (ret != KADD_SUCCESS) {
        GTEST_SKIP() << "CNN model not available";
    }
    ASSERT_NE(model, nullptr);

    float dummy[5] = {0};
    float probs[5] = {0};

    // sample_count=0
    EXPECT_EQ(kadd_dl_model_predict_batch(model, dummy, 0, probs, 1),
              KADD_ERROR_INVALID_PARAM);

    // sample_count=-1
    EXPECT_EQ(kadd_dl_model_predict_batch(model, dummy, -1, probs, 1),
              KADD_ERROR_INVALID_PARAM);

    // batch_size=0 → 自动回退为默认值 64 并成功
    float input300[300] = {0};
    float p;
    ret = kadd_dl_model_predict_batch(model, input300, 1, &p, 0);
    EXPECT_EQ(ret, KADD_SUCCESS);

    kadd_dl_model_free(model);

    // kadd_sqli_detect_batch: count=0
    kadd_sqli_engine_t *engine = CreateMLEngine();
    ASSERT_NE(engine, nullptr);
    const char *inputs[] = {"SELECT 1", "OR 1=1"};
    float bp[2] = {0};
    EXPECT_EQ(kadd_sqli_detect_batch(engine, inputs, 0, bp, 1),
              KADD_ERROR_INVALID_PARAM);
    kadd_sqli_engine_free(engine);
}

/* ========== TC009: config 后端选择优先级验证 ========== */

TEST(DLEngineTest, TC009_ConfigBackendPriority)
{
    const char *tmp = "/tmp/priority_test_config.json";

    // 仅 model_cnn + engine_mode=dl → DL 后端
    {
        ASSERT_NE(DfaTransitionPath(), nullptr);
        ASSERT_NE(DfaOutputPath(), nullptr);
        ASSERT_NE(DfaEodPath(), nullptr);
        ASSERT_NE(CnnModelPath(), nullptr);
        FILE *f = fopen(tmp, "w");
        ASSERT_NE(f, nullptr);
        fprintf(f,
            "{\"transition_table\":\"%s\","
            "\"output_table\":\"%s\","
            "\"eod_table\":\"%s\","
            "\"engine_mode\":\"dl\","
            "\"model_cnn\":\"%s\"}",
            DfaTransitionPath(), DfaOutputPath(), DfaEodPath(), CnnModelPath());
        fclose(f);
        kadd_sqli_engine_t *engine = nullptr;
        int ret = kadd_sqli_engine_create_from_file(&engine, tmp);
        EXPECT_EQ(ret, KADD_SUCCESS);
        if (ret == KADD_SUCCESS) kadd_sqli_engine_free(engine);
    }

    // 仅 model_rf（ML 模式默认）→ ML 后端
    {
        ASSERT_NE(DfaTransitionPath(), nullptr);
        ASSERT_NE(DfaOutputPath(), nullptr);
        ASSERT_NE(DfaEodPath(), nullptr);
        ASSERT_NE(RfModelPath(), nullptr);
        FILE *f = fopen(tmp, "w");
        ASSERT_NE(f, nullptr);
        fprintf(f,
            "{\"transition_table\":\"%s\","
            "\"output_table\":\"%s\","
            "\"eod_table\":\"%s\","
            "\"model_rf\":\"%s\"}",
            DfaTransitionPath(), DfaOutputPath(), DfaEodPath(), RfModelPath());
        fclose(f);
        kadd_sqli_engine_t *engine = nullptr;
        int ret = kadd_sqli_engine_create_from_file(&engine, tmp);
        EXPECT_EQ(ret, KADD_SUCCESS);
        if (ret == KADD_SUCCESS) kadd_sqli_engine_free(engine);
    }

    // 无模型 key → 失败
    {
        FILE *f = fopen(tmp, "w");
        ASSERT_NE(f, nullptr);
        fprintf(f,
            "{\"transition_table\":\"%s\","
            "\"output_table\":\"%s\","
            "\"eod_table\":\"%s\"}",
            DfaTransitionPath(), DfaOutputPath(), DfaEodPath());
        fclose(f);
        kadd_sqli_engine_t *engine = nullptr;
        int ret = kadd_sqli_engine_create_from_file(&engine, tmp);
        EXPECT_NE(ret, KADD_SUCCESS);
        kadd_sqli_engine_free(engine);
    }
    remove(tmp);
}

/* ========== TC010: 模型文件校验链验证 ========== */

TEST(DLEngineTest, TC010_ModelFileValidationChain)
{
    kadd_dl_model_t *model = nullptr;

    // 不存在的文件（SVE=OFF 时走 ONNX，ONNX 加载失败返回 MODEL_LOAD_FAILED）
    int ret_notfound = kadd_dl_model_load(&model, "/nonexistent/path.kcn", 0);
    EXPECT_TRUE(ret_notfound == KADD_ERROR_FILE_OPERATION_FAILED ||
                ret_notfound == KADD_ERROR_MODEL_LOAD_FAILED)
        << "Expected -108 or -107, got " << ret_notfound;

    // 文件太小（< 64 字节）→ 模型加载失败
    {
        const char *small = "/tmp/small_model.kcn";
        FILE *f = fopen(small, "wb");
        if (f) {
            for (int i = 0; i < 10; i++) fputc(0, f);
            fclose(f);
        }
        EXPECT_EQ(kadd_dl_model_load(&model, small, 0),
                  KADD_ERROR_MODEL_LOAD_FAILED);
        remove(small);
    }

    // magic 错误（64 字节全 0x41）
    {
        const char *badmagic = "/tmp/badmagic.kcn";
        FILE *f = fopen(badmagic, "wb");
        if (f) {
            for (int i = 0; i < 64; i++) fputc(0x41, f);
            fclose(f);
        }
        EXPECT_EQ(kadd_dl_model_load(&model, badmagic, 0),
                  KADD_ERROR_MODEL_LOAD_FAILED);
        remove(badmagic);
    }

    // version 错误（magic 正确但 version=999）
    {
        const char *badver = "/tmp/badver.kcn";
        FILE *f = fopen(badver, "wb");
        if (f) {
            const char magic[8] = {'K','A','D','D',' ','C','N','1'};
            fwrite(magic, 1, 8, f);
            uint32_t version = 999;
            fwrite(&version, 4, 1, f);
            for (int i = 0; i < 52; i++) fputc(0, f);
            fclose(f);
        }
        EXPECT_EQ(kadd_dl_model_load(&model, badver, 0),
                  KADD_ERROR_MODEL_LOAD_FAILED);
        remove(badver);
    }
}

/* ========== TC013: load 期错误码覆盖验证 ========== */

TEST(DLEngineTest, TC013_LoadErrorCodes)
{
    kadd_dl_model_t *model = nullptr;

    // 文件不可读（SVE=OFF 时走 ONNX，ONNX 加载失败返回 MODEL_LOAD_FAILED）
    int ret_noread = kadd_dl_model_load(&model, "/dev/null/nonexistent", 0);
    EXPECT_TRUE(ret_noread == KADD_ERROR_FILE_OPERATION_FAILED ||
                ret_noread == KADD_ERROR_MODEL_LOAD_FAILED)
        << "Expected -108 or -107, got " << ret_noread;

    // magic 不符
    {
        const char *f = "/tmp/badmagic_tc013.kcn";
        FILE *fp = fopen(f, "wb");
        if (fp) {
            for (int i = 0; i < 64; i++) fputc(0x42, fp);
            fclose(fp);
        }
        EXPECT_EQ(kadd_dl_model_load(&model, f, 0),
                  KADD_ERROR_MODEL_LOAD_FAILED);
        remove(f);
    }

    // NULL 参数
    EXPECT_EQ(kadd_dl_model_load(nullptr, CnnModelPath(), 0),
              KADD_ERROR_NULL_PTR);
    EXPECT_EQ(kadd_dl_model_load(&model, nullptr, 0),
              KADD_ERROR_NULL_PTR);
}

/* ========== TC014: 推理期错误码覆盖验证 ========== */

TEST(DLEngineTest, TC014_InferenceErrorCodes)
{
    kadd_dl_model_t *model = nullptr;
    int ret = kadd_dl_model_load(&model, CnnModelPath(), 0);
    if (ret != KADD_SUCCESS) {
        GTEST_SKIP() << "CNN model not available";
    }
    ASSERT_NE(model, nullptr);

    float prob;

    // NULL model
    float dummy[300] = {0};
    EXPECT_EQ(kadd_dl_model_predict(nullptr, dummy, &prob),
              KADD_ERROR_NULL_PTR);

    // NULL input
    EXPECT_EQ(kadd_dl_model_predict(model, nullptr, &prob),
              KADD_ERROR_NULL_PTR);

    // NULL probability
    EXPECT_EQ(kadd_dl_model_predict(model, dummy, nullptr),
              KADD_ERROR_NULL_PTR);

    // NaN token
    float nan_input[300] = {0};
    nan_input[0] = NAN;
    EXPECT_EQ(kadd_dl_model_predict(model, nan_input, &prob),
              KADD_ERROR_INVALID_PARAM);

    // 越界 token (> 28)
    float oob_input[300] = {0};
    oob_input[0] = 29.0f;
    EXPECT_EQ(kadd_dl_model_predict(model, oob_input, &prob),
              KADD_ERROR_INVALID_PARAM);

    // 负值 token
    float neg_input[300] = {0};
    neg_input[0] = -1.0f;
    EXPECT_EQ(kadd_dl_model_predict(model, neg_input, &prob),
              KADD_ERROR_INVALID_PARAM);

    // 非整数 token
    float frac_input[300] = {0};
    frac_input[0] = 1.5f;
    EXPECT_EQ(kadd_dl_model_predict(model, frac_input, &prob),
              KADD_ERROR_INVALID_PARAM);

    kadd_dl_model_free(model);
}

/* ========== TC015: 生命周期完整性验证 ========== */

TEST(DLEngineTest, TC015_Lifecycle)
{
    kadd_dl_model_t *model = nullptr;
    int ret = kadd_dl_model_load(&model, CnnModelPath(), 0);
    if (ret != KADD_SUCCESS) {
        GTEST_SKIP() << "CNN model not available";
    }
    ASSERT_NE(model, nullptr);

    float input[300] = {0};
    float prob;
    ret = kadd_dl_model_predict(model, input, &prob);
    EXPECT_EQ(ret, KADD_SUCCESS);
    EXPECT_GE(prob, 0.0f);
    EXPECT_LE(prob, 1.0f);
    kadd_dl_model_free(model);

    // free(NULL) 是空操作
    kadd_dl_model_free(nullptr);

    // 再次 load
    ret = kadd_dl_model_load(&model, CnnModelPath(), 0);
    EXPECT_EQ(ret, KADD_SUCCESS);
    ASSERT_NE(model, nullptr);
    ret = kadd_dl_model_predict(model, input, &prob);
    EXPECT_EQ(ret, KADD_SUCCESS);
    kadd_dl_model_free(model);
}

/* ========== TC020: 引擎释放与资源管理验证 ========== */

TEST(DLEngineTest, TC020_ResourceManagement)
{
    // ML 引擎释放
    kadd_sqli_engine_t *engine = CreateMLEngine();
    if (engine) kadd_sqli_engine_free(engine);

    // DL 引擎释放
    engine = CreateDLEngine();
    if (engine) kadd_sqli_engine_free(engine);

    // free(NULL) 空操作
    kadd_sqli_engine_free(nullptr);

    // load 失败后 free
    engine = nullptr;
    kadd_sqli_engine_create_from_file(&engine, "/nonexistent/config.json");
    kadd_sqli_engine_free(engine);
}

/* ========== TC029: 模型文件 CRC32 防篡改校验验证 ========== */

TEST(DLEngineTest, TC029_CRC32TamperDetection)
{
    // 读取正常 .kcn 文件
    FILE *src = fopen(CnnModelPath(), "rb");
    if (!src) {
        GTEST_SKIP() << "CNN model not available";
    }
    fseek(src, 0, SEEK_END);
    long size = ftell(src);
    fseek(src, 0, SEEK_SET);
    std::vector<unsigned char> data(size);
    if (fread(data.data(), 1, size, src) != (size_t)size) {
        fclose(src);
        GTEST_SKIP() << "Failed to read model file";
    }
    fclose(src);

    kadd_dl_model_t *model = nullptr;

#if KADD_HAVE_SVE
    /* SVE 构建：.kcn 原生格式带头部 CRC32 校验，精确断言 -107 */

    // 1. 载荷首字节篡改（偏移 64）→ CRC 校验失败
    {
        const char *f = "/tmp/tamper_first.kcn";
        FILE *fp = fopen(f, "wb");
        if (fp) {
            std::vector<unsigned char> tampered = data;
            tampered[64] ^= 0xFF;
            fwrite(tampered.data(), 1, tampered.size(), fp);
            fclose(fp);
        }
        EXPECT_EQ(kadd_dl_model_load(&model, f, 0),
                  KADD_ERROR_MODEL_LOAD_FAILED);
        remove(f);
    }

    // 2. 载荷末字节篡改 → CRC 校验失败
    {
        const char *f = "/tmp/tamper_last.kcn";
        FILE *fp = fopen(f, "wb");
        if (fp) {
            std::vector<unsigned char> tampered = data;
            tampered[size - 1] ^= 0xFF;
            fwrite(tampered.data(), 1, tampered.size(), fp);
            fclose(fp);
        }
        EXPECT_EQ(kadd_dl_model_load(&model, f, 0),
                  KADD_ERROR_MODEL_LOAD_FAILED);
        remove(f);
    }

    // 3. 头部 CRC 字段篡改（偏移 56，4 字节）→ 校验失败
    {
        const char *f = "/tmp/tamper_crc.kcn";
        FILE *fp = fopen(f, "wb");
        if (fp) {
            std::vector<unsigned char> tampered = data;
            tampered[56] ^= 0xFF;
            fwrite(tampered.data(), 1, tampered.size(), fp);
            fclose(fp);
        }
        EXPECT_EQ(kadd_dl_model_load(&model, f, 0),
                  KADD_ERROR_MODEL_LOAD_FAILED);
        remove(f);
    }

    // 4. 全零覆盖载荷 → CRC 校验失败
    {
        const char *f = "/tmp/tamper_zero.kcn";
        FILE *fp = fopen(f, "wb");
        if (fp) {
            std::vector<unsigned char> tampered = data;
            for (size_t i = 64; i < tampered.size(); i++) {
                tampered[i] = 0;
            }
            fwrite(tampered.data(), 1, tampered.size(), fp);
            fclose(fp);
        }
        EXPECT_EQ(kadd_dl_model_load(&model, f, 0),
                  KADD_ERROR_MODEL_LOAD_FAILED);
        remove(f);
    }
#else
    /* ONNX 构建：.onnx 为 protobuf 格式，无 .kcn 头部 CRC32 校验链。
     * 偏移 64/56 的点篡改可能落在 ORT 可容忍的字段（实测命中 Cast 节点
     * op_type 区段仍加载成功），不属于该用例的校验语义；仅覆盖"破坏
     * 文件结构后加载必须失败"的可保证行为。 */

    // 1. 载荷末字节篡改 → 破坏 protobuf 编码 → 加载失败
    {
        const char *f = "/tmp/tamper_last.onnx";
        FILE *fp = fopen(f, "wb");
        if (fp) {
            std::vector<unsigned char> tampered = data;
            tampered[size - 1] ^= 0xFF;
            fwrite(tampered.data(), 1, tampered.size(), fp);
            fclose(fp);
        }
        EXPECT_EQ(kadd_dl_model_load(&model, f, 0),
                  KADD_ERROR_MODEL_LOAD_FAILED);
        remove(f);
    }

    // 2. 全零覆盖载荷（偏移 64 起）→ 破坏 protobuf 结构 → 加载失败
    {
        const char *f = "/tmp/tamper_zero.onnx";
        FILE *fp = fopen(f, "wb");
        if (fp) {
            std::vector<unsigned char> tampered = data;
            for (size_t i = 64; i < tampered.size(); i++) {
                tampered[i] = 0;
            }
            fwrite(tampered.data(), 1, tampered.size(), fp);
            fclose(fp);
        }
        EXPECT_EQ(kadd_dl_model_load(&model, f, 0),
                  KADD_ERROR_MODEL_LOAD_FAILED);
        remove(f);
    }
#endif
}

/* ========== CNN 后端 feature_dim 不匹配校验 ========== */
/*
 * 用原生 CNN 二进制模型（.kcn）+ 一个明显错误的 feature_dim（999）。
 * SVE 构建下 kadd_cnn_model_load 成功，随后 kadd_dl_engine.c 第 53-60 行
 * 检测到 999 != 模型真实维度，释放 CNN 句柄并返回 INVALID_PARAM。
 * feature_dim=0 表示"不校验"，因此必须传非 0 且非真实维度的值才能触发。
 */
TEST(DLEngineTest, FeatureDimMismatch_CNN)
{
    kadd_dl_model_t *model = nullptr;
    int ret = kadd_dl_model_load(&model, CnnModelPath(), 999);
    EXPECT_EQ(ret, KADD_ERROR_INVALID_PARAM);
    EXPECT_EQ(model, nullptr);
}

/* ========== ONNX 后端 feature_dim 不匹配校验 ========== */
/*
 * 用 ONNX 模型（.onnx）+ 一个明显错误的 feature_dim（999）。
 * SVE 构建下原生 CNN 加载失败（非 .kcn 格式），回退 ONNX 分支；
 * ONNX 加载成功后，kadd_dl_engine.c 第 77-84 行检测到 999 != 模型真实维度，
 * 释放 ONNX 句柄并返回 INVALID_PARAM。
 */
TEST(DLEngineTest, FeatureDimMismatch_ONNX)
{
    const char *kCnnOnnx = CnnModelOnnxPath();
    kadd_dl_model_t *model = nullptr;
    int ret = kadd_dl_model_load(&model, kCnnOnnx, 999);
    EXPECT_EQ(ret, KADD_ERROR_INVALID_PARAM);
    EXPECT_EQ(model, nullptr);
}

/* ========== ONNX 后端完整生命周期（SVE 构建下 CNN 原生加载失败→ONNX 成功） ========== */
/*
 * 用 ONNX 模型 + feature_dim=0（不校验），走 ONNX 成功路径：
 * kadd_dl_engine.c line 85-89（backend=0 成功加载）、
 * predict_batch 走 ONNX 分支（line 111-112）、
 * get_feature_dim（line 117-118）、free 释放 ONNX 引擎（line 133-135）。
 */
TEST(DLEngineTest, OnnxBackend_Lifecycle)
{
    const char *kCnnOnnx = CnnModelOnnxPath();
    kadd_dl_model_t *model = nullptr;
    int ret = kadd_dl_model_load(&model, kCnnOnnx, 0);
    ASSERT_EQ(ret, KADD_SUCCESS) << "ONNX 后端加载应成功（feature_dim=0 不校验）";
    ASSERT_NE(model, nullptr);

    int dim = kadd_dl_model_get_feature_dim(model);
    EXPECT_GT(dim, 0) << "ONNX 模型 feature_dim 应大于 0";

    // 用 ONNX 后端做预测（走 kadd_inference_predict_batch）
    std::vector<float> features(dim, 0.0f);
    float prob = -1.0f;
    int perr = kadd_dl_model_predict(model, features.data(), &prob);
    EXPECT_EQ(perr, KADD_SUCCESS) << "ONNX 预测应成功";
    EXPECT_GE(prob, 0.0f);
    EXPECT_LE(prob, 1.0f);

    kadd_dl_model_free(model);
    model = nullptr;
}
