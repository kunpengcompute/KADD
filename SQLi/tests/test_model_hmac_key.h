// Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
/**
 * @file test_model_hmac_key.h
 * @brief 在测试进程启动前注入 .krf / .kcn 模型共用的 HMAC 开发密钥
 *
 * [FIX REPORT-2026-0000028] kadd_rf_model_load() 与 kadd_cnn_model_load()
 * 现在都强制要求 HMAC-SHA256 认证，未签名的旧格式模型一律拒绝加载，
 * 且密钥优先从环境变量 KADD_MODEL_HMAC_KEY 读取（兼容旧名 KADD_RF_HMAC_KEY）。
 * 仓库自带的 assets/model/model_tokens_rf.krf 与 model_tokens_dl_cnn.kcn
 * 都已用下面这个公开的开发密钥签名，因此所有会加载原生模型的测试可执行文件
 * 都必须包含本头文件，否则模型加载会 fail-closed 失败、用例整体不可用。
 *
 * 实现方式：利用 inline 变量的动态初始化在 main() 之前执行（C++17）注入环境变量，
 * 因此不依赖自定义 main —— 各测试目标都直接链接 gtest_main。
 *
 * 若环境中已存在 KADD_MODEL_HMAC_KEY（例如 CI 用自己的密钥重签了模型），
 * 则不覆盖，使同一套测试既能跑开发密钥也能跑 CI 密钥。
 *
 * 安全提示：下面的密钥是公开的开发密钥，仅用于构建与测试，不具备任何保密性。
 * 生产部署必须用 tools/rf_sign/rf_sign.py 以自己的密钥重新签名模型，
 * 并通过 KMS / 受限权限 env 文件注入 KADD_MODEL_HMAC_KEY。
 */
#ifndef KADD_TEST_MODEL_HMAC_KEY_H
#define KADD_TEST_MODEL_HMAC_KEY_H

#include <cstdlib>
#include <string>

namespace kadd_test {

/* 开发密钥 = SHA-256("KADD-RF-DEV-KEY-DO-NOT-USE-IN-PRODUCTION")，公开、非机密 */
constexpr const char* kModelHmacDevKeyHex =
    "74fab58d87dc235ee049b6faf21b44825dba1d9dc05bbc14aeded49cb211a1db";
constexpr const char* kModelHmacKeyEnv = "KADD_MODEL_HMAC_KEY";
constexpr const char* kModelHmacKeyLegacyEnv = "KADD_RF_HMAC_KEY";

class ModelHmacKeyInstaller {
public:
    ModelHmacKeyInstaller() {
        /* 若 CI / 用户已经设置了主变量或旧名变量，则不覆盖，
         * 避免把外部密钥静默替换成开发密钥。 */
        if (std::getenv(kModelHmacKeyEnv) != nullptr ||
            std::getenv(kModelHmacKeyLegacyEnv) != nullptr) {
            return;
        }
#ifdef _WIN32
        const std::string entry =
            std::string(kModelHmacKeyEnv) + "=" + kModelHmacDevKeyHex;
        _putenv(entry.c_str());
#else
        setenv(kModelHmacKeyEnv, kModelHmacDevKeyHex, 1);
#endif
    }
};

inline const ModelHmacKeyInstaller g_kadd_model_hmac_key_installer{};

}  // namespace kadd_test

#endif  // KADD_TEST_MODEL_HMAC_KEY_H
