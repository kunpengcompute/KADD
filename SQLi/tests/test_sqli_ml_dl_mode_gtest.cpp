/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * @file test_sqli_ml_dl_mode_gtest.cpp
 * @brief SQLi engine ML/DL mode switching test
 */

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fstream>
#include <vector>
#include <chrono>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <unistd.h>

#include "kadd_sqli_engine.h"
#include "kadd_errors.h"
#include "kadd_dl_engine.h"
#include "test_model_hmac_key.h"   /* [FIX REPORT-2026-0000028] 注入 .krf/.kcn 共用 HMAC 开发密钥 */

class SQLiModeTest : public ::testing::Test {
protected:
    std::string assets_dir_;
    std::string dfa_trans_;
    std::string dfa_output_;
    std::string dfa_eod_;
    std::string model_rf_;
    std::string model_rf_onnx_;
    std::string model_cnn_;
    std::string model_cnn_onnx_;

    void SetUp() override {
        const char* search_paths[] = {
            "assets", "../assets", "../../assets", "../../../assets"
        };
        for (auto p : search_paths) {
            std::string test = std::string(p) + "/data/transition_table.bin";
            FILE* f = fopen(test.c_str(), "rb");
            if (f) { fclose(f); assets_dir_ = p; break; }
        }
        ASSERT_FALSE(assets_dir_.empty()) << "Cannot find assets directory";

        dfa_trans_  = assets_dir_ + "/data/transition_table.bin";
        dfa_output_ = assets_dir_ + "/data/output_table.bin";
        dfa_eod_    = assets_dir_ + "/data/eod_table.bin";
        model_rf_   = assets_dir_ + "/model/model_tokens_rf.krf";
        model_rf_onnx_ = assets_dir_ + "/model/model_tokens_rf.onnx";
        model_cnn_  = assets_dir_ + "/model/model_tokens_dl_cnn.kcn";
        model_cnn_onnx_ = assets_dir_ + "/model/model_tokens_dl_cnn.onnx";
    }

    std::string write_config(const std::string& mode,
                             const std::string& extra_model_rf = "",
                             const std::string& extra_model_rf_onnx = "",
                             const std::string& extra_model_cnn = "",
                             const std::string& extra_model_cnn_onnx = "") {
        std::string rf = extra_model_rf.empty() ? model_rf_ : extra_model_rf;
        std::string rf_onnx = extra_model_rf_onnx.empty() ? model_rf_onnx_ : extra_model_rf_onnx;
        std::string cnn = extra_model_cnn.empty() ? model_cnn_ : extra_model_cnn;
        std::string cnn_onnx = extra_model_cnn_onnx.empty() ? model_cnn_onnx_ : extra_model_cnn_onnx;

        char tmp[] = "/tmp/sqli_test_config_XXXXXX";
        int fd = mkstemp(tmp);
        EXPECT_GE(fd, 0);
        std::string path = std::string(tmp) + ".json";
        close(fd);
        /* [FIX REPORT-2026-0000025] 实际配置写入 path（tmp + ".json"），
         * mkstemp 创建的 tmp 文件本身从未被删除，每次调用泄漏一个临时文件 */
        if (fd >= 0) {
            unlink(tmp);
        }

        std::ofstream ofs(path);
        ofs << "{\n"
            << "  \"engine_mode\": \"" << mode << "\",\n"
            << "  \"transition_table\": \"" << dfa_trans_ << "\",\n"
            << "  \"output_table\": \"" << dfa_output_ << "\",\n"
            << "  \"eod_table\": \"" << dfa_eod_ << "\",\n"
            << "  \"model_rf\": \"" << rf << "\",\n"
            << "  \"model_rf_onnx\": \"" << rf_onnx << "\",\n"
            << "  \"model_cnn\": \"" << cnn << "\",\n"
            << "  \"model_cnn_onnx\": \"" << cnn_onnx << "\"\n"
            << "}\n";
        ofs.close();
        return path;
    }

    void remove_file(const std::string& path) {
        remove(path.c_str());
    }
};

TEST_F(SQLiModeTest, MLMode_LoadAndPredict) {
    std::string cfg = write_config("ml");
    kadd_sqli_engine_t* engine = nullptr;

    int ret = kadd_sqli_engine_create_from_file(&engine, cfg.c_str());
    ASSERT_EQ(ret, KADD_SUCCESS) << "ML mode engine creation failed";
    ASSERT_NE(engine, nullptr);

    float prob = -1.0f;
    ret = kadd_sqli_detect(engine, "SELECT * FROM users WHERE id=1", &prob);
    EXPECT_EQ(ret, KADD_SUCCESS);
    EXPECT_GE(prob, 0.0f);
    EXPECT_LE(prob, 1.0f);
    std::cout << "[ML mode] Normal SQL prob=" << prob << std::endl;

    float prob2 = -1.0f;
    ret = kadd_sqli_detect(engine, "id=1' OR '1'='1", &prob2);
    EXPECT_EQ(ret, KADD_SUCCESS);
    EXPECT_GE(prob2, 0.0f);
    EXPECT_LE(prob2, 1.0f);
    std::cout << "[ML mode] SQLi input prob=" << prob2 << std::endl;

    kadd_sqli_engine_free(engine);
    remove_file(cfg);
}

TEST_F(SQLiModeTest, MLMode_BatchPredict) {
    std::string cfg = write_config("ml");
    kadd_sqli_engine_t* engine = nullptr;

    int ret = kadd_sqli_engine_create_from_file(&engine, cfg.c_str());
    ASSERT_EQ(ret, KADD_SUCCESS);

    const char* inputs[] = {
        "SELECT 1",
        "id=1' OR '1'='1",
        "normal text",
        "'; DROP TABLE users;--",
        "hello world"
    };
    const int n = 5;
    float probs[n] = {};

    ret = kadd_sqli_detect_batch(engine, inputs, n, probs, 64);
    EXPECT_EQ(ret, KADD_SUCCESS);

    for (int i = 0; i < n; i++) {
        EXPECT_GE(probs[i], 0.0f);
        EXPECT_LE(probs[i], 1.0f);
        std::cout << "[ML batch] input[" << i << "] prob=" << probs[i] << std::endl;
    }

    kadd_sqli_engine_free(engine);
    remove_file(cfg);
}

TEST_F(SQLiModeTest, DLMode_LoadAndPredict) {
    std::string cfg = write_config("dl");
    kadd_sqli_engine_t* engine = nullptr;

    int ret = kadd_sqli_engine_create_from_file(&engine, cfg.c_str());
    ASSERT_EQ(ret, KADD_SUCCESS) << "DL mode engine creation failed";
    ASSERT_NE(engine, nullptr);

    float prob = -1.0f;
    ret = kadd_sqli_detect(engine, "SELECT * FROM users WHERE id=1", &prob);
    EXPECT_EQ(ret, KADD_SUCCESS);
    EXPECT_GE(prob, 0.0f);
    EXPECT_LE(prob, 1.0f);
    std::cout << "[DL mode] Normal SQL prob=" << prob << std::endl;

    float prob2 = -1.0f;
    ret = kadd_sqli_detect(engine, "id=1' OR '1'='1", &prob2);
    EXPECT_EQ(ret, KADD_SUCCESS);
    EXPECT_GE(prob2, 0.0f);
    EXPECT_LE(prob2, 1.0f);
    std::cout << "[DL mode] SQLi input prob=" << prob2 << std::endl;

    kadd_sqli_engine_free(engine);
    remove_file(cfg);
}

TEST_F(SQLiModeTest, DLMode_BatchPredict) {
    std::string cfg = write_config("dl");
    kadd_sqli_engine_t* engine = nullptr;

    int ret = kadd_sqli_engine_create_from_file(&engine, cfg.c_str());
    ASSERT_EQ(ret, KADD_SUCCESS);

    const char* inputs[] = {
        "SELECT 1",
        "id=1' OR '1'='1",
        "normal text",
        "'; DROP TABLE users;--",
        "hello world"
    };
    const int n = 5;
    float probs[n] = {};

    ret = kadd_sqli_detect_batch(engine, inputs, n, probs, 64);
    EXPECT_EQ(ret, KADD_SUCCESS);

    for (int i = 0; i < n; i++) {
        EXPECT_GE(probs[i], 0.0f);
        EXPECT_LE(probs[i], 1.0f);
        std::cout << "[DL batch] input[" << i << "] prob=" << probs[i] << std::endl;
    }

    kadd_sqli_engine_free(engine);
    remove_file(cfg);
}

TEST_F(SQLiModeTest, ML_vs_DL_OutputComparison) {
    std::string cfg_ml = write_config("ml");
    kadd_sqli_engine_t* ml_engine = nullptr;
    ASSERT_EQ(kadd_sqli_engine_create_from_file(&ml_engine, cfg_ml.c_str()), KADD_SUCCESS);

    std::string cfg_dl = write_config("dl");
    kadd_sqli_engine_t* dl_engine = nullptr;
    ASSERT_EQ(kadd_sqli_engine_create_from_file(&dl_engine, cfg_dl.c_str()), KADD_SUCCESS);

    const char* test_inputs[] = {
        "SELECT 1",
        "normal query",
        "' OR 1=1--",
        "UNION SELECT * FROM passwords",
        "hello",
        "id=1; DROP TABLE users",
        "admin'--",
        "search=test&page=1",
    };
    const int n = sizeof(test_inputs) / sizeof(test_inputs[0]);

    std::cout << "\n========== ML vs DL Output Comparison ==========\n";
    std::cout << std::left << std::setw(40) << "Input"
              << std::right << std::setw(10) << "ML"
              << std::setw(10) << "DL"
              << std::setw(10) << "Diff" << "\n";
    std::cout << std::string(70, '-') << "\n";

    for (int i = 0; i < n; i++) {
        float ml_prob = 0.0f, dl_prob = 0.0f;

        EXPECT_EQ(kadd_sqli_detect(ml_engine, test_inputs[i], &ml_prob), KADD_SUCCESS);
        EXPECT_EQ(kadd_sqli_detect(dl_engine, test_inputs[i], &dl_prob), KADD_SUCCESS);

        float diff = std::abs(ml_prob - dl_prob);
        std::cout << std::left << std::setw(40) << test_inputs[i]
                  << std::right << std::fixed << std::setprecision(4)
                  << std::setw(10) << ml_prob
                  << std::setw(10) << dl_prob
                  << std::setw(10) << diff << "\n";
    }

    kadd_sqli_engine_free(ml_engine);
    kadd_sqli_engine_free(dl_engine);
    remove_file(cfg_ml);
    remove_file(cfg_dl);
}

TEST_F(SQLiModeTest, ML_vs_DL_Performance) {
    std::string cfg_ml = write_config("ml");
    std::string cfg_dl = write_config("dl");

    kadd_sqli_engine_t* ml_engine = nullptr;
    kadd_sqli_engine_t* dl_engine = nullptr;
    ASSERT_EQ(kadd_sqli_engine_create_from_file(&ml_engine, cfg_ml.c_str()), KADD_SUCCESS);
    ASSERT_EQ(kadd_sqli_engine_create_from_file(&dl_engine, cfg_dl.c_str()), KADD_SUCCESS);

    const char* input = "SELECT * FROM users WHERE id=1' OR '1'='1";
    const int iterations = 100;

    float prob;
    auto start_ml = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        kadd_sqli_detect(ml_engine, input, &prob);
    }
    auto end_ml = std::chrono::high_resolution_clock::now();
    double ml_us = std::chrono::duration_cast<std::chrono::microseconds>(end_ml - start_ml).count();

    auto start_dl = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        kadd_sqli_detect(dl_engine, input, &prob);
    }
    auto end_dl = std::chrono::high_resolution_clock::now();
    double dl_us = std::chrono::duration_cast<std::chrono::microseconds>(end_dl - start_dl).count();

    std::cout << "\n========== Performance (" << iterations << " iterations) ==========\n";
    std::cout << "  ML mode: " << std::fixed << std::setprecision(1)
              << (ml_us / iterations) << " us/req\n";
    std::cout << "  DL mode: " << (dl_us / iterations) << " us/req\n";
    std::cout << "  Ratio:   " << std::setprecision(2)
              << (dl_us / ml_us) << "x\n";

    kadd_sqli_engine_free(ml_engine);
    kadd_sqli_engine_free(dl_engine);
    remove_file(cfg_ml);
    remove_file(cfg_dl);
}

TEST_F(SQLiModeTest, InvalidMode_DefaultsToML) {
    std::string cfg = write_config("invalid_mode");
    kadd_sqli_engine_t* engine = nullptr;

    int ret = kadd_sqli_engine_create_from_file(&engine, cfg.c_str());
    ASSERT_EQ(ret, KADD_SUCCESS) << "Invalid mode should fall back to ML";

    float prob = 0.0f;
    ret = kadd_sqli_detect(engine, "test", &prob);
    EXPECT_EQ(ret, KADD_SUCCESS);

    kadd_sqli_engine_free(engine);
    remove_file(cfg);
}

TEST_F(SQLiModeTest, MLMode_MissingModel_Fails) {
    std::string cfg = write_config("ml",
        "/nonexistent/rf.krf",
        "/nonexistent/rf.onnx",
        model_cnn_,
        model_cnn_onnx_);

    kadd_sqli_engine_t* engine = nullptr;
    int ret = kadd_sqli_engine_create_from_file(&engine, cfg.c_str());
    EXPECT_NE(ret, KADD_SUCCESS) << "ML mode with missing model should fail";

    remove_file(cfg);
}

TEST_F(SQLiModeTest, DLMode_MissingModel_Fails) {
    std::string cfg = write_config("dl",
        model_rf_,
        model_rf_onnx_,
        "/nonexistent/cnn.kcn",
        "/nonexistent/cnn.onnx");

    kadd_sqli_engine_t* engine = nullptr;
    int ret = kadd_sqli_engine_create_from_file(&engine, cfg.c_str());
    EXPECT_NE(ret, KADD_SUCCESS) << "DL mode with missing model should fail";

    remove_file(cfg);
}

TEST_F(SQLiModeTest, NullParameters) {
    EXPECT_NE(kadd_sqli_engine_create_from_file(nullptr, "config.json"), KADD_SUCCESS);

    kadd_sqli_engine_t* engine = nullptr;
    EXPECT_NE(kadd_sqli_engine_create_from_file(&engine, nullptr), KADD_SUCCESS);

    std::string cfg = write_config("ml");
    kadd_sqli_engine_t* eng = nullptr;
    ASSERT_EQ(kadd_sqli_engine_create_from_file(&eng, cfg.c_str()), KADD_SUCCESS);

    float prob;
    EXPECT_NE(kadd_sqli_detect(eng, nullptr, &prob), KADD_SUCCESS);
    EXPECT_NE(kadd_sqli_detect(eng, "test", nullptr), KADD_SUCCESS);

    kadd_sqli_engine_free(eng);
    remove_file(cfg);
}

TEST_F(SQLiModeTest, FreeNullEngine_Noop) {
    kadd_sqli_engine_free(nullptr);
    SUCCEED() << "free(nullptr) should be safe";
}

TEST_F(SQLiModeTest, MLMode_ONNXFallback) {
    std::string cfg = write_config("ml",
        "/nonexistent/rf.krf",
        model_rf_onnx_);

    kadd_sqli_engine_t* engine = nullptr;
    int ret = kadd_sqli_engine_create_from_file(&engine, cfg.c_str());
    ASSERT_EQ(ret, KADD_SUCCESS) << "ONNX fallback should work when SVE model missing";

    float prob = 0.0f;
    ret = kadd_sqli_detect(engine, "test input", &prob);
    EXPECT_EQ(ret, KADD_SUCCESS);

    kadd_sqli_engine_free(engine);
    remove_file(cfg);
}

TEST_F(SQLiModeTest, DLMode_ONNXFallback) {
    std::string cfg = write_config("dl",
        model_rf_, model_rf_onnx_,
        "/nonexistent/cnn.kcn",
        model_cnn_onnx_);

    kadd_sqli_engine_t* engine = nullptr;
    int ret = kadd_sqli_engine_create_from_file(&engine, cfg.c_str());
    ASSERT_EQ(ret, KADD_SUCCESS) << "DL ONNX fallback should work";

    float prob = 0.0f;
    ret = kadd_sqli_detect(engine, "test input", &prob);
    EXPECT_EQ(ret, KADD_SUCCESS);

    kadd_sqli_engine_free(engine);
    remove_file(cfg);
}


TEST_F(SQLiModeTest, DLModel_GetFeatureDim) {
    kadd_dl_model_t* model = nullptr;
    int ret = kadd_dl_model_load(&model, model_cnn_.c_str(), 0);
    if (ret != KADD_SUCCESS) {
        ret = kadd_dl_model_load(&model, model_cnn_onnx_.c_str(), 0);
    }
    if (ret == KADD_SUCCESS && model != nullptr) {
        int dim = kadd_dl_model_get_feature_dim(model);
        EXPECT_GT(dim, 0) << "feature_dim should be positive";
        kadd_dl_model_free(model);
    } else {
        GTEST_SKIP() << "DL model not available, skipping";
    }
}

TEST(DLModelTest, GetFeatureDim_NullModel) {
    int dim = kadd_dl_model_get_feature_dim(nullptr);
    EXPECT_EQ(dim, KADD_ERROR_NULL_PTR);
}
