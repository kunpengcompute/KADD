/* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved. */
/**
 * @file test_error_paths_gtest.cpp
 * @brief 引擎错误路径单元测试（与 FUZZ 共享 error_paths_cases.inc）
 *
 * 这些用例与 kadd_fuzzer 分层调度中调用的错误路径用例完全同源
 *（tests/error_paths_cases.inc 同时被两者包含），保证 UT 与 FUZZ 两份
 * 覆盖率报告都能命中 config / dfa / feature / rf / cnn / dl / sqli 的
 * 参数校验、表文件解析、HMAC 认证失败与 native→ONNX 回退分支。
 *
 * 说明：.inc 中的函数以「分支触达」为目标，遵循 fuzz 语义不做结果断言
 *（返回值以 (void) 丢弃）；本文件的 TEST 验证所有错误路径可无崩溃执行，
 * 具体返回码断言由各引擎既有 gtest（dfa_engine_test / rf_engine_gtest 等）承担。
 */
#include <gtest/gtest.h>

#include "error_paths_cases.inc"

TEST(ErrorPathsTest, FeatureErrorPaths) {
    FuzzFeatureErrorPaths();
}

TEST(ErrorPathsTest, ConfigErrorPaths) {
    FuzzConfigErrorPaths();
}

TEST(ErrorPathsTest, DfaErrorPaths) {
    FuzzDfaErrorPaths();
}

TEST(ErrorPathsTest, DfaBatchErrorPaths) {
    FuzzDfaBatchErrorPaths();
}

TEST(ErrorPathsTest, RfRealModelPaths) {
    FuzzRfRealModelPaths();
}

TEST(ErrorPathsTest, CnnRealModelPaths) {
    FuzzCnnRealModelPaths();
}

TEST(ErrorPathsTest, DlErrorPaths) {
    FuzzDlErrorPaths();
}

TEST(ErrorPathsTest, SqliErrorPaths) {
    FuzzSqliErrorPaths();
}

/* ORT 线程数环境变量分支：coverage_all.sh 恒定导出合法值，
 * 「未设置 / 空串 / 非法取值回退默认」三类分支只能由本用例临时改写
 * 环境变量后真实加载模型才能触达（kadd_inference_engine.c 276-291）。 */
TEST(ErrorPathsTest, InferenceThreadEnvBranches) {
    FuzzInferenceThreadEnvBranches();
}
