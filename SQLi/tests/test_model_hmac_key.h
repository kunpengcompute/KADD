/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * All rights reserved.
 *
 * Licensed under the License;
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/license/bsd-3-clause
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/**
 * @file test_model_hmac_key.h
 * @brief 在测试进程启动前注入 .krf / .kcn 模型共用的 HMAC 开发密钥
 *
 * [FIX REPORT-2026-0000028] kadd_rf_model_load() 与 kadd_cnn_model_load()
 * 现在都强制要求 HMAC-SHA256 认证，未签名的旧格式模型一律拒绝加载，
 * 且密钥优先从环境变量 KADD_MODEL_HMAC_KEY 读取（兼容旧名 KADD_RF_HMAC_KEY）。
 * 仓库自带的 assets/model/model_tokens_rf.krf 与 model_tokens_dl_cnn.kcn
 * 都已用公开的开发密钥签名（= SHA-256(公开短语)，运行时派生，源码不含密钥
 * 字面量），因此所有会加载原生模型的测试可执行文件都必须包含本头文件，
 * 否则模型加载会 fail-closed 失败、用例整体不可用。
 *
 * 实现方式：利用 inline 变量的动态初始化在 main() 之前执行（C++17）注入环境变量，
 * 因此不依赖自定义 main —— 各测试目标都直接链接 gtest_main。
 *
 * 若环境中已存在 KADD_MODEL_HMAC_KEY（例如 CI 用自己的密钥重签了模型），
 * 则不覆盖，使同一套测试既能跑开发密钥也能跑 CI 密钥。
 *
 * 安全提示：开发密钥由公开短语派生，仅用于构建与测试，不具备任何保密性。
 * 生产部署必须用 tools/rf_sign/rf_sign.py 以自己的密钥重新签名模型，
 * 并通过 KMS / 受限权限 env 文件注入 KADD_MODEL_HMAC_KEY。
 */
#ifndef KADD_TEST_MODEL_HMAC_KEY_H
#define KADD_TEST_MODEL_HMAC_KEY_H

#include <cstdio>
#include <cstdlib>

#include "kadd_hmac_sha256.h"

namespace kadd_test {

/* 开发密钥 = SHA-256(公开短语)，main() 前运行时派生到下方缓冲区：
 * 源码不出现密钥字面量（避免明文密钥入库 / 触发敏感信息扫描），
 * 派生结果与仓库 assets 的签名密钥一致，测试可直接加载原生模型。
 * 短语公开、非机密；生产必须用自有密钥重签模型。 */
constexpr const char kModelHmacDevKeyPhrase[] =
    "KADD-RF-DEV-KEY-DO-NOT-USE-IN-PRODUCTION";
inline char kModelHmacDevKeyHexBuf[65];
/* 指针为编译期常量（保持既有 constexpr 用法），内容在 main() 前填充 */
constexpr const char* kModelHmacDevKeyHex = kModelHmacDevKeyHexBuf;
constexpr const char* kModelHmacKeyEnv = "KADD_MODEL_HMAC_KEY";
constexpr const char* kModelHmacKeyLegacyEnv = "KADD_RF_HMAC_KEY";

class ModelHmacKeyInstaller {
public:
    ModelHmacKeyInstaller() {
        /* 派生开发密钥：SHA-256(公开短语) -> 64 个十六进制字符 */
        uint8_t digest[KADD_SHA256_DIGEST_SIZE];
        kadd_sha256(reinterpret_cast<const uint8_t*>(kModelHmacDevKeyPhrase),
                    sizeof(kModelHmacDevKeyPhrase) - 1U, digest);
        for (size_t i = 0; i < KADD_SHA256_DIGEST_SIZE; ++i) {
            snprintf(kModelHmacDevKeyHexBuf + 2U * i, 3, "%02x", digest[i]);
        }
        kadd_secure_wipe(digest, sizeof(digest));

        /* 若 CI / 用户已经设置了主变量或旧名变量，则不覆盖，
         * 避免把外部密钥静默替换成开发密钥。 */
        if (std::getenv(kModelHmacKeyEnv) != nullptr ||
            std::getenv(kModelHmacKeyLegacyEnv) != nullptr) {
            return;
        }
        setenv(kModelHmacKeyEnv, kModelHmacDevKeyHex, 1);
    }
};

inline const ModelHmacKeyInstaller g_kadd_model_hmac_key_installer{};

}  // namespace kadd_test

#endif  // KADD_TEST_MODEL_HMAC_KEY_H
