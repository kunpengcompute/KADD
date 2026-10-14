/* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved. */
/**
 * @file kadd_model_auth.h
 * @brief KADD 模型文件的共享 HMAC-SHA256 认证逻辑（.krf / .kcn 通用）
 *
 * [FIX REPORT-2026-0000028] 统一 RF 与 CNN 两种原生模型格式的真实性校验：
 *   - 文件布局：header || payload || HMAC-SHA256(32)
 *   - flags bit0 = 1 表示已签名；未签名模型一律拒绝加载
 *   - 认证范围覆盖 header + payload，因此元数据也被密钥绑定
 *
 * 设计为 header-only 且全部函数为 static，目的是不改动
 * src/CMakeLists.txt 的 SOURCES 列表，从而对现有构建零影响。
 *
 * 密钥来源（优先级从高到低）：
 *   1. 环境变量 KADD_MODEL_HMAC_KEY   （推荐，通用名）
 *   2. 环境变量 KADD_RF_HMAC_KEY      （兼容旧名，仅 RF 历史遗留）
 *
 * 密钥为 32 字节，以 64 个十六进制字符提供。
 * 生产环境请自行生成并妥善保管，例如：
 *     export KADD_MODEL_HMAC_KEY=$(openssl rand -hex 32)
 */
#ifndef KADD_MODEL_AUTH_H
#define KADD_MODEL_AUTH_H

#include "kadd_hmac_sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========== 常量 ========== */

/* 推荐的通用环境变量名（RF / CNN 共用同一把密钥） */
#define KADD_MODEL_HMAC_KEY_ENV          "KADD_MODEL_HMAC_KEY"
/* 兼容旧名：早期实现只支持 RF，保留它避免破坏既有部署 */
#define KADD_MODEL_HMAC_KEY_LEGACY_ENV   "KADD_RF_HMAC_KEY"

/* flags bit0：模型已携带 HMAC-SHA256 尾部 */
#define KADD_MODEL_FLAG_HMAC_SHA256      0x00000001U
/* HMAC-SHA256 输出长度 */
#define KADD_MODEL_HMAC_SIZE             KADD_SHA256_DIGEST_SIZE
/* 密钥十六进制长度（32 字节 = 64 字符） */
#define KADD_MODEL_HMAC_KEY_HEX_LEN      (KADD_SHA256_DIGEST_SIZE * 2U)

/* header-only：所有符号均为 static，未被使用的翻译单元不应因此告警 */
#if defined(__GNUC__) || defined(__clang__)
#define KADD_MODEL_AUTH_MAYBE_UNUSED __attribute__((unused))
#else
#define KADD_MODEL_AUTH_MAYBE_UNUSED
#endif

/* ========== 密钥读取 ========== */

/**
 * @brief 读取并校验 HMAC 密钥
 *
 * @param key_out 输出 32 字节密钥
 * @return 1 = 已取得有效密钥；0 = 未配置任何密钥；-1 = 已配置但格式非法
 *
 * 优先读取 KADD_MODEL_HMAC_KEY；未设置时回退到旧名 KADD_RF_HMAC_KEY。
 * 任一已配置的变量格式非法都视为错误，不会静默降级为“无密钥”。
 */
static KADD_MODEL_AUTH_MAYBE_UNUSED int kadd_model_load_hmac_key(
    uint8_t key_out[KADD_SHA256_DIGEST_SIZE])
{
    const char *hex = getenv(KADD_MODEL_HMAC_KEY_ENV);
    const char *env_name = KADD_MODEL_HMAC_KEY_ENV;

    if (hex == NULL) {
        hex = getenv(KADD_MODEL_HMAC_KEY_LEGACY_ENV);
        env_name = KADD_MODEL_HMAC_KEY_LEGACY_ENV;
    }

    if (hex == NULL) {
        return 0;
    }

    if (strlen(hex) != (size_t)KADD_MODEL_HMAC_KEY_HEX_LEN ||
        kadd_hex_to_bytes(hex, (size_t)KADD_MODEL_HMAC_KEY_HEX_LEN,
                          key_out, KADD_SHA256_DIGEST_SIZE) != 0) {
        fprintf(stderr,
                "[kadd_model] ERROR: %s must be exactly %u hex characters "
                "(32 bytes)\n",
                env_name, (unsigned)KADD_MODEL_HMAC_KEY_HEX_LEN);
        return -1;
    }
    return 1;
}

/* ========== 真实性校验 ========== */

/**
 * @brief 校验模型文件的真实性（强制，fail-closed）
 *
 * @param file_bytes       整个文件内容
 * @param signed_region    被认证区域长度（header + payload）
 * @param is_authenticated 头部 flags 是否声明带 HMAC 尾部
 * @param engine_tag       日志前缀，例如 "kadd_rf" / "kadd_cnn"
 *
 * 策略矩阵（无任何放行未签名模型的路径）：
 *   未签名            -> 拒绝（旧格式不再支持）
 *   已签名 + 无密钥   -> 拒绝（无法验证就不加载）
 *   已签名 + 密钥非法 -> 拒绝（配置错误不得静默降级）
 *   已签名 + 密钥正确 -> 验证 HMAC，不符即拒绝
 */
static KADD_MODEL_AUTH_MAYBE_UNUSED int kadd_model_verify_authenticity(
    const unsigned char *file_bytes,
    size_t signed_region,
    int is_authenticated,
    const char *engine_tag)
{
    if (!is_authenticated) {
        fprintf(stderr,
                "[%s] ERROR: model carries no HMAC-SHA256 trailer "
                "(flags bit0 = 0); unsigned models are no longer supported - "
                "re-sign it with tools/rf_sign/rf_sign.py\n",
                engine_tag);
        return -1;
    }

    uint8_t key[KADD_SHA256_DIGEST_SIZE];
    kadd_secure_wipe(key, sizeof(key));
    const int key_state = kadd_model_load_hmac_key(key);

    if (key_state < 0) {
        /* 密钥格式非法：kadd_model_load_hmac_key 已打印具体原因。
         * 宁可拒绝加载，也不要在“以为受保护”的状态下运行 */
        kadd_secure_wipe(key, sizeof(key));
        return -1;
    }
    if (key_state == 0) {
        fprintf(stderr,
                "[%s] ERROR: %s (or legacy %s) is not set; refusing to load "
                "an authenticated model that cannot be verified\n",
                engine_tag,
                KADD_MODEL_HMAC_KEY_ENV,
                KADD_MODEL_HMAC_KEY_LEGACY_ENV);
        kadd_secure_wipe(key, sizeof(key));
        return -1;
    }

    uint8_t computed[KADD_SHA256_DIGEST_SIZE];
    kadd_hmac_sha256(key, sizeof(key), file_bytes, signed_region, computed);
    kadd_secure_wipe(key, sizeof(key));

    if (kadd_consttime_memcmp(computed, file_bytes + signed_region,
                              KADD_SHA256_DIGEST_SIZE) != 0) {
        fprintf(stderr,
                "[%s] ERROR: HMAC-SHA256 verification failed - model file has "
                "been tampered with, or the key does not match the signing key\n",
                engine_tag);
        kadd_secure_wipe(computed, sizeof(computed));
        return -1;
    }

    kadd_secure_wipe(computed, sizeof(computed));
    return 0;
}

#endif /* KADD_MODEL_AUTH_H */
