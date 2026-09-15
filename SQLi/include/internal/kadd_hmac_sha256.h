/* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved. */
/**
 * @file kadd_hmac_sha256.h
 * @brief 自包含的 SHA-256 / HMAC-SHA256 实现（header-only，无外部依赖）
 *
 * [FIX REPORT-2026-0000028] 用于 .krf 模型文件的真实性认证。
 * 原格式仅有 CRC32（多项式 0xEDB88320），只能检测损坏、不能抵抗篡改：
 * 持有模型写权限者可改写叶节点概率后重算 CRC32，从而绕过 SQLi 检测。
 * HMAC-SHA256 引入密钥，使攻击者无法在不知道密钥的情况下重新生成有效认证码。
 *
 * 设计为 header-only 且全部函数为 static，目的是不改动 src/CMakeLists.txt 的
 * SOURCES 列表，从而对现有构建零影响。全部运算使用 uint32_t/uint64_t，
 * 不涉及有符号溢出，与 -ftrapv 编译选项兼容。
 *
 * 已用 NIST FIPS 180-4 / RFC 4231 标准测试向量验证（见 tests/test_rf_engine_gtest.cpp）。
 */
#ifndef KADD_HMAC_SHA256_H
#define KADD_HMAC_SHA256_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* header-only：所有符号均为 static，未被使用的翻译单元不应因此告警 */
#if defined(__GNUC__) || defined(__clang__)
#define KADD_HMAC_MAYBE_UNUSED __attribute__((unused))
#else
#define KADD_HMAC_MAYBE_UNUSED
#endif

#define KADD_SHA256_DIGEST_SIZE 32U
#define KADD_SHA256_BLOCK_SIZE  64U

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  buf[KADD_SHA256_BLOCK_SIZE];
    size_t   buf_len;
} kadd_sha256_ctx;

static KADD_HMAC_MAYBE_UNUSED const uint32_t g_kadd_sha256_k[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

static KADD_HMAC_MAYBE_UNUSED uint32_t kadd_rotr32(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32U - n));
}

static KADD_HMAC_MAYBE_UNUSED uint32_t kadd_sha256_ch(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) ^ ((~x) & z);
}

static KADD_HMAC_MAYBE_UNUSED uint32_t kadd_sha256_maj(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) ^ (x & z) ^ (y & z);
}

static KADD_HMAC_MAYBE_UNUSED uint32_t kadd_sha256_sigma0(uint32_t x)
{
    return kadd_rotr32(x, 2) ^ kadd_rotr32(x, 13) ^ kadd_rotr32(x, 22);
}

static KADD_HMAC_MAYBE_UNUSED uint32_t kadd_sha256_sigma1(uint32_t x)
{
    return kadd_rotr32(x, 6) ^ kadd_rotr32(x, 11) ^ kadd_rotr32(x, 25);
}

static KADD_HMAC_MAYBE_UNUSED uint32_t kadd_sha256_gamma0(uint32_t x)
{
    return kadd_rotr32(x, 7) ^ kadd_rotr32(x, 18) ^ (x >> 3);
}

static KADD_HMAC_MAYBE_UNUSED uint32_t kadd_sha256_gamma1(uint32_t x)
{
    return kadd_rotr32(x, 17) ^ kadd_rotr32(x, 19) ^ (x >> 10);
}

static KADD_HMAC_MAYBE_UNUSED uint32_t kadd_read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | ((uint32_t)p[3]);
}

static KADD_HMAC_MAYBE_UNUSED void kadd_write_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

static KADD_HMAC_MAYBE_UNUSED void kadd_sha256_transform(kadd_sha256_ctx *ctx,
                                  const uint8_t block[KADD_SHA256_BLOCK_SIZE])
{
    uint32_t w[64];
    uint32_t i;

    for (i = 0; i < 16U; i++) {
        w[i] = kadd_read_be32(block + (size_t)i * 4U);
    }
    for (i = 16U; i < 64U; i++) {
        w[i] = w[i - 16] + kadd_sha256_gamma0(w[i - 15]) +
               w[i - 7] + kadd_sha256_gamma1(w[i - 2]);
    }

    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];
    uint32_t f = ctx->state[5];
    uint32_t g = ctx->state[6];
    uint32_t h = ctx->state[7];

    for (i = 0; i < 64U; i++) {
        const uint32_t t1 = h + kadd_sha256_sigma1(e) + kadd_sha256_ch(e, f, g) +
                            g_kadd_sha256_k[i] + w[i];
        const uint32_t t2 = kadd_sha256_sigma0(a) + kadd_sha256_maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

/**
 * @brief 擦除敏感内存（volatile 逐字节写，防止编译器把清零优化掉）
 * [FIX 安全函数整改] 统一替代 memset 清零敏感数据，与项目其他代码一致
 */
static KADD_HMAC_MAYBE_UNUSED void kadd_secure_wipe(void *p, size_t len)
{
    volatile uint8_t *vp = (volatile uint8_t *)p;
    if (vp == NULL) {
        return;
    }
    for (size_t i = 0; i < len; i++) {
        vp[i] = 0U;
    }
}

static KADD_HMAC_MAYBE_UNUSED void kadd_sha256_init(kadd_sha256_ctx *ctx)
{
    ctx->state[0] = 0x6a09e667U;
    ctx->state[1] = 0xbb67ae85U;
    ctx->state[2] = 0x3c6ef372U;
    ctx->state[3] = 0xa54ff53aU;
    ctx->state[4] = 0x510e527fU;
    ctx->state[5] = 0x9b05688cU;
    ctx->state[6] = 0x1f83d9abU;
    ctx->state[7] = 0x5be0cd19U;
    ctx->bitlen = 0U;
    ctx->buf_len = 0U;
}

static KADD_HMAC_MAYBE_UNUSED void kadd_sha256_update(kadd_sha256_ctx *ctx, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        ctx->buf[ctx->buf_len++] = data[i];
        if (ctx->buf_len == KADD_SHA256_BLOCK_SIZE) {
            kadd_sha256_transform(ctx, ctx->buf);
            ctx->bitlen += 512U;
            ctx->buf_len = 0U;
        }
    }
}

static KADD_HMAC_MAYBE_UNUSED void kadd_sha256_final(kadd_sha256_ctx *ctx,
                              uint8_t out[KADD_SHA256_DIGEST_SIZE])
{
    const uint64_t total_bits = ctx->bitlen + (uint64_t)ctx->buf_len * 8U;

    ctx->buf[ctx->buf_len++] = 0x80U;
    if (ctx->buf_len > 56U) {
        while (ctx->buf_len < KADD_SHA256_BLOCK_SIZE) {
            ctx->buf[ctx->buf_len++] = 0x00U;
        }
        kadd_sha256_transform(ctx, ctx->buf);
        ctx->buf_len = 0U;
    }
    while (ctx->buf_len < 56U) {
        ctx->buf[ctx->buf_len++] = 0x00U;
    }
    for (uint32_t i = 0; i < 8U; i++) {
        ctx->buf[56U + (size_t)i] = (uint8_t)(total_bits >> (56U - 8U * i));
    }
    kadd_sha256_transform(ctx, ctx->buf);

    for (uint32_t i = 0; i < 8U; i++) {
        kadd_write_be32(out + (size_t)i * 4U, ctx->state[i]);
    }

    kadd_secure_wipe(ctx, sizeof(*ctx));
}

static KADD_HMAC_MAYBE_UNUSED void kadd_sha256(const uint8_t *data, size_t len,
                        uint8_t out[KADD_SHA256_DIGEST_SIZE])
{
    kadd_sha256_ctx ctx;
    kadd_sha256_init(&ctx);
    kadd_sha256_update(&ctx, data, len);
    kadd_sha256_final(&ctx, out);
}

/**
 * @brief RFC 2104 HMAC-SHA256
 */
static KADD_HMAC_MAYBE_UNUSED void kadd_hmac_sha256(const uint8_t *key, size_t key_len,
                             const uint8_t *msg, size_t msg_len,
                             uint8_t out[KADD_SHA256_DIGEST_SIZE])
{
    uint8_t k_block[KADD_SHA256_BLOCK_SIZE];
    uint8_t ipad[KADD_SHA256_BLOCK_SIZE];
    uint8_t opad[KADD_SHA256_BLOCK_SIZE];
    uint8_t inner[KADD_SHA256_DIGEST_SIZE];

    kadd_secure_wipe(k_block, sizeof(k_block));
    if (key_len > KADD_SHA256_BLOCK_SIZE) {
        kadd_sha256(key, key_len, k_block);   /* 长密钥先哈希 */
    } else if (key_len > 0U && key != NULL) {
        /* [FIX 安全函数整改] key_len <= KADD_SHA256_BLOCK_SIZE 已由分支保证，
         * 有界逐字节拷贝替代 memcpy（k_block 容量即循环上界约束） */
        for (size_t i = 0; i < key_len; i++) {
            k_block[i] = key[i];
        }
    }

    for (uint32_t i = 0; i < KADD_SHA256_BLOCK_SIZE; i++) {
        ipad[i] = k_block[i] ^ 0x36U;
        opad[i] = k_block[i] ^ 0x5cU;
    }

    kadd_sha256_ctx ctx;
    kadd_sha256_init(&ctx);
    kadd_sha256_update(&ctx, ipad, KADD_SHA256_BLOCK_SIZE);
    kadd_sha256_update(&ctx, msg, msg_len);
    kadd_sha256_final(&ctx, inner);

    kadd_sha256_init(&ctx);
    kadd_sha256_update(&ctx, opad, KADD_SHA256_BLOCK_SIZE);
    kadd_sha256_update(&ctx, inner, KADD_SHA256_DIGEST_SIZE);
    kadd_sha256_final(&ctx, out);

    kadd_secure_wipe(k_block, sizeof(k_block));
    kadd_secure_wipe(ipad, sizeof(ipad));
    kadd_secure_wipe(opad, sizeof(opad));
    kadd_secure_wipe(inner, sizeof(inner));
}

/**
 * @brief 定长比较，避免通过短路时机泄露 MAC 字节
 * @return 0 表示相等
 */
static KADD_HMAC_MAYBE_UNUSED int kadd_consttime_memcmp(const void *a, const void *b, size_t len)
{
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    uint8_t diff = 0U;
    for (size_t i = 0; i < len; i++) {
        diff |= (uint8_t)(pa[i] ^ pb[i]);
    }
    return (diff == 0U) ? 0 : 1;
}

/**
 * @brief 十六进制字符串转字节（大小写均可，不接受 0x 前缀与空白）
 * @return 0 成功，-1 非法字符或长度不匹配
 */
static KADD_HMAC_MAYBE_UNUSED int kadd_hex_to_bytes(const char *hex, size_t hex_len,
                             uint8_t *out, size_t out_len)
{
    if (hex == NULL || out == NULL || hex_len != out_len * 2U) {
        return -1;
    }
    for (size_t i = 0; i < out_len; i++) {
        uint32_t v = 0U;
        for (uint32_t j = 0; j < 2U; j++) {
            const char c = hex[i * 2U + (size_t)j];
            uint32_t d;
            if (c >= '0' && c <= '9') {
                d = (uint32_t)(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                d = (uint32_t)(c - 'a') + 10U;
            } else if (c >= 'A' && c <= 'F') {
                d = (uint32_t)(c - 'A') + 10U;
            } else {
                return -1;
            }
            v = (v << 4) | d;
        }
        out[i] = (uint8_t)v;
    }
    return 0;
}

#endif /* KADD_HMAC_SHA256_H */
