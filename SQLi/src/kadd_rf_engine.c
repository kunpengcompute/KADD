/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * @file kadd_rf_engine.c
 * @brief Portable sklearn RandomForest inference, with an AArch64 SVE batch path.
 */

#include "kadd_rf_engine.h"
#include "kadd_sve.h"
#include "kadd_errors.h"
/* [FIX REPORT-2026-0000028] 模型真实性认证（HMAC-SHA256），header-only 无外部依赖 */
#include "kadd_model_auth.h"

#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__aarch64__) && defined(__ARM_FEATURE_SVE)
#include <arm_sve.h>
#define KADD_RF_HAVE_SVE 1
#else
#define KADD_RF_HAVE_SVE 0
#endif

#define KADD_RF_HEADER_SIZE 64U
#define KADD_RF_NODE_SIZE 32U
#define KADD_RF_VERSION 1U
#define KADD_RF_CLASS_COUNT 2U
#define KADD_RF_POSITIVE_CLASS_INDEX 1U
#define KADD_RF_MAX_FEATURE_COUNT 4096U
#define KADD_RF_MAX_TREE_COUNT 1000000U
#define KADD_RF_MAX_NODE_COUNT 100000000U
#define KADD_RF_DEFAULT_BATCH_SIZE 64
#define KADD_RF_MAX_BATCH_SIZE 1024
#define KADD_RF_MAX_SAMPLE_COUNT 10000000

/* [FIX REPORT-2026-0000028] .krf 认证格式（强制，不再兼容未签名旧格式）
 *   flags bit0 = 1 : 布局 header(64) || payload || HMAC-SHA256(32)，
 *                    认证范围覆盖 header + payload，因此 feature_count /
 *                    node_count / crc 等元数据也一并被密钥绑定
 *   flags bit0 = 0 : 未签名的旧格式模型，一律拒绝加载
 * 其余 flags 位仍视为非法，保持原有严格校验语义。
 * 存量模型用 tools/rf_sign/rf_sign.py 重新签名后即可加载。 */
/* [FIX REPORT-2026-0000028] RF 与 CNN 共用同一套认证常量与密钥 */
#define KADD_RF_FLAG_HMAC_SHA256 KADD_MODEL_FLAG_HMAC_SHA256
#define KADD_RF_HMAC_SIZE KADD_MODEL_HMAC_SIZE

static const unsigned char g_kadd_rf_magic[8] = {
    'K', 'A', 'D', 'D', 'R', 'F', '1', '\0'
};

typedef struct {
    int32_t feature;
    uint32_t left;
    uint32_t right;
    uint32_t reserved;
    double threshold;
    double positive_probability;
} kadd_rf_node_t;

struct kadd_rf_model_t {
    uint32_t feature_count;
    uint32_t tree_count;
    uint32_t node_count;
    uint32_t *tree_offsets;
    kadd_rf_node_t *nodes;
#if KADD_RF_HAVE_SVE
    /*
     * Structure-of-arrays view used only by the SVE traversal. Keeping the
     * hot fields in separate compact arrays turns each node lookup into one
     * 32-bit gather instead of several 64-bit gathers from a 32-byte AoS node.
     */
    uint32_t *sve_features;
    uint32_t *sve_left;
    uint32_t *sve_right;
    float *sve_thresholds;
    double *sve_probabilities;
#endif
};

typedef char kadd_rf_node_size_must_be_32[
    sizeof(kadd_rf_node_t) == KADD_RF_NODE_SIZE ? 1 : -1
];

static uint32_t read_u32_le(const unsigned char *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

static uint64_t read_u64_le(const unsigned char *p)
{
    return ((uint64_t)read_u32_le(p)) |
           ((uint64_t)read_u32_le(p + 4) << 32U);
}

static double read_f64_le(const unsigned char *p)
{
    union {
        uint64_t u;
        double   d;
    } conv;
    conv.u = read_u64_le(p);
    return conv.d;
}

static uint32_t crc32_bytes(const unsigned char *data, size_t size)
{
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t mask = (uint32_t)(-(int32_t)(crc & 1U));
            crc = (crc >> 1U) ^ (UINT32_C(0xEDB88320) & mask);
        }
    }
    return ~crc;
}

static int read_entire_file(const char *path, unsigned char **bytes_out, size_t *size_out)
{
    FILE *file = NULL;
    unsigned char *bytes = NULL;
    long file_size_long;
    size_t file_size;

    if (!path || !bytes_out || !size_out) {
        return KADD_ERROR_NULL_PTR;
    }
    *bytes_out = NULL;
    *size_out = 0;

    file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "[kadd_rf] ERROR: Cannot open '%s': %s\n", path, strerror(errno));
        return KADD_ERROR_FILE_OPERATION_FAILED;
    }
    if (fseek(file, 0, SEEK_END) != 0 ||
        (file_size_long = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        (void)fclose(file);
        return KADD_ERROR_FILE_OPERATION_FAILED;
    }
    file_size = (size_t)file_size_long;
    if ((long)file_size != file_size_long || file_size < KADD_RF_HEADER_SIZE) {
        (void)fclose(file);
        return KADD_ERROR_MODEL_LOAD_FAILED;
    }

    bytes = (unsigned char *)malloc(file_size);
    if (!bytes) {
        (void)fclose(file);
        return KADD_ERROR_OUT_OF_MEMORY;
    }
    if (fread(bytes, 1, file_size, file) != file_size) {
        free(bytes);
        (void)fclose(file);
        return KADD_ERROR_FILE_OPERATION_FAILED;
    }
    (void)fclose(file);
    *bytes_out = bytes;
    *size_out = file_size;
    return KADD_SUCCESS;
}

static int validate_tree(const kadd_rf_model_t *model, uint32_t tree_index)
{
    const uint32_t begin = model->tree_offsets[tree_index];
    const uint32_t end = model->tree_offsets[tree_index + 1U];

    if (begin >= end || end > model->node_count) {
        return KADD_ERROR_MODEL_LOAD_FAILED;
    }
    for (uint32_t index = begin; index < end; ++index) {
        const kadd_rf_node_t *node = &model->nodes[index];
        if (node->feature < 0) {
            if (node->feature != -1 ||
                node->left != UINT32_MAX ||
                node->right != UINT32_MAX ||
                !isfinite(node->positive_probability) ||
                node->positive_probability < 0.0 ||
                node->positive_probability > 1.0) {
                return KADD_ERROR_MODEL_LOAD_FAILED;
            }
            continue;
        }
        if (node->reserved != 0U ||
            (uint32_t)node->feature >= model->feature_count ||
            !isfinite(node->threshold) ||
            node->left <= index || node->right <= index ||
            node->left < begin || node->left >= end ||
            node->right < begin || node->right >= end) {
            return KADD_ERROR_MODEL_LOAD_FAILED;
        }
    }
    return KADD_SUCCESS;
}

#if KADD_RF_HAVE_SVE
static float adjacent_f32(float value, int toward_positive)
{
    union {
        float    f;
        uint32_t u;
    } conv;
    conv.f = value;
    uint32_t bits = conv.u;

    if (toward_positive) {
        if (bits == UINT32_C(0x7F800000)) { /* +infinity */
            return value;
        }
        if ((bits & UINT32_C(0x7FFFFFFF)) == 0U) {
            bits = UINT32_C(0x00000001); /* +smallest subnormal */
        } else if ((bits & UINT32_C(0x80000000)) == 0U) {
            ++bits;
        } else {
            --bits;
        }
    } else {
        if (bits == UINT32_C(0xFF800000)) { /* -infinity */
            return value;
        }
        if ((bits & UINT32_C(0x7FFFFFFF)) == 0U) {
            bits = UINT32_C(0x80000001); /* -smallest subnormal */
        } else if ((bits & UINT32_C(0x80000000)) == 0U) {
            --bits;
        } else {
            ++bits;
        }
    }
    conv.u = bits;
    return conv.f;
}

/*
 * Return the greatest float32 value c for which (double)c <= threshold.
 * Comparing a float32 input with c is therefore exactly equivalent to the
 * sklearn rule (double)input <= double_threshold for every finite float32
 * input, even when the original threshold itself is not representable as
 * float32.
 */
static float make_f32_threshold_cutoff(double threshold)
{
    float cutoff = (float)threshold;
    if ((double)cutoff > threshold) {
        cutoff = adjacent_f32(cutoff, 0);
    }

    /*
     * This also makes the conversion independent of a non-default floating
     * point rounding mode. A correctly rounded cast needs at most one step,
     * but the loop is bounded by the adjacent representable values around the
     * converted result in all standard rounding modes.
     */
    for (;;) {
        const float next = adjacent_f32(cutoff, 1);
        if ((double)next > threshold) {
            break;
        }
        cutoff = next;
    }
    return cutoff;
}

static int prepare_sve_layout(kadd_rf_model_t *model)
{
    const size_t u32_bytes = (size_t)model->node_count * sizeof(uint32_t);
    const size_t f32_bytes = (size_t)model->node_count * sizeof(float);
    const size_t f64_bytes = (size_t)model->node_count * sizeof(double);

    model->sve_features = (uint32_t *)malloc(u32_bytes);
    model->sve_left = (uint32_t *)malloc(u32_bytes);
    model->sve_right = (uint32_t *)malloc(u32_bytes);
    model->sve_thresholds = (float *)malloc(f32_bytes);
    model->sve_probabilities = (double *)malloc(f64_bytes);
    if (!model->sve_features || !model->sve_left ||
        !model->sve_right || !model->sve_thresholds ||
        !model->sve_probabilities) {
        return KADD_ERROR_OUT_OF_MEMORY;
    }

    for (uint32_t index = 0; index < model->node_count; ++index) {
        const kadd_rf_node_t *node = &model->nodes[index];
        model->sve_features[index] =
            node->feature < 0 ? UINT32_MAX : (uint32_t)node->feature;
        model->sve_left[index] = node->left;
        model->sve_right[index] = node->right;
        model->sve_thresholds[index] =
            node->feature < 0 ? 0.0F
                              : make_f32_threshold_cutoff(node->threshold);
        model->sve_probabilities[index] =
            node->positive_probability;
    }
    return KADD_SUCCESS;
}
#endif

/* ========== [FIX REPORT-2026-0000028] 模型真实性认证（RF/CNN 共用实现） ========== */

/**
 * @brief 校验 .krf 模型的真实性（强制，fail-closed）
 *
 * 实际实现位于 include/internal/kadd_model_auth.h，
 * RF 与 CNN 共用同一把密钥与同一套校验逻辑。
 */
static int kadd_rf_verify_authenticity(const unsigned char *file_bytes,
                                       size_t signed_region,
                                       int is_authenticated)
{
    if (kadd_model_verify_authenticity(file_bytes, signed_region,
                                       is_authenticated, "kadd_rf") != 0) {
        return KADD_ERROR_MODEL_LOAD_FAILED;
    }
    return KADD_SUCCESS;
}

int kadd_rf_model_load(kadd_rf_model_t **model_out, const char *model_path)
{
    unsigned char *file_bytes = NULL;
    size_t file_size = 0;
    kadd_rf_model_t *model = NULL;
    int ret;

    if (!model_out || !model_path) {
        return KADD_ERROR_NULL_PTR;
    }
    *model_out = NULL;

#if KADD_RF_HAVE_SVE
    /*
     * 纵深防护：上层分发层已做 HWCAP 检测，这里再次确认——
     * prepare_sve_layout() 及推理路径使用 SVE 指令，在无 SVE 的 CPU 上
     * 执行即 SIGILL，必须先用 HWCAP 确认硬件能力，
     * 不支持则返回失败，由上层回退 ONNX 后端。
     */
    if (!kadd_sve_runtime_available()) {
        fprintf(stderr,
                "[kadd_rf] ERROR: CPU does not support SVE, "
                "native RF backend unavailable\n");
        return KADD_ERROR_MODEL_LOAD_FAILED;
    }
#endif

    ret = read_entire_file(model_path, &file_bytes, &file_size);
    if (ret != KADD_SUCCESS) {
        return ret;
    }

    const unsigned char *header = file_bytes;
    if (memcmp(header, g_kadd_rf_magic, sizeof(g_kadd_rf_magic)) != 0 ||
        read_u32_le(header + 8) != KADD_RF_VERSION ||
        read_u32_le(header + 12) != KADD_RF_HEADER_SIZE ||
        read_u32_le(header + 16) != KADD_RF_NODE_SIZE ||
        read_u32_le(header + 24) != KADD_RF_CLASS_COUNT ||
        read_u32_le(header + 28) != KADD_RF_POSITIVE_CLASS_INDEX) {
        free(file_bytes);
        return KADD_ERROR_MODEL_LOAD_FAILED;
    }

    const uint32_t feature_count = read_u32_le(header + 20);
    const uint32_t tree_count = read_u32_le(header + 32);
    const uint32_t node_count = read_u32_le(header + 36);
    const uint32_t flags = read_u32_le(header + 40);
    const uint64_t payload_size_u64 = read_u64_le(header + 44);
    const uint32_t expected_crc = read_u32_le(header + 52);

    /* [FIX REPORT-2026-0000028] bit0 = 带 HMAC-SHA256 尾部；其余位仍非法 */
    const int is_authenticated = (flags & KADD_RF_FLAG_HMAC_SHA256) != 0U;

    if (feature_count == 0 || feature_count > KADD_RF_MAX_FEATURE_COUNT ||
        tree_count == 0 || tree_count > KADD_RF_MAX_TREE_COUNT ||
        node_count == 0 || node_count > KADD_RF_MAX_NODE_COUNT ||
        (flags & ~KADD_RF_FLAG_HMAC_SHA256) != 0U ||
        read_u32_le(header + 56) != 0U ||
        read_u32_le(header + 60) != 0U ||
        payload_size_u64 > SIZE_MAX) {
        free(file_bytes);
        return KADD_ERROR_MODEL_LOAD_FAILED;
    }

    const size_t offsets_size = ((size_t)tree_count + 1U) * sizeof(uint32_t);
    if (node_count > (SIZE_MAX - offsets_size) / KADD_RF_NODE_SIZE) {
        free(file_bytes);
        return KADD_ERROR_MODEL_LOAD_FAILED;
    }
    const size_t expected_payload_size =
        offsets_size + (size_t)node_count * KADD_RF_NODE_SIZE;
    /* [FIX REPORT-2026-0000028] 先预留 header 与可能的 HMAC 尾部空间再做加法，
     * 避免 size_t 回绕；认证文件的实际大小必须恰好多出 32 字节 */
    if (expected_payload_size > SIZE_MAX - KADD_RF_HEADER_SIZE - KADD_RF_HMAC_SIZE) {
        free(file_bytes);
        return KADD_ERROR_MODEL_LOAD_FAILED;
    }
    const size_t signed_region_size = KADD_RF_HEADER_SIZE + expected_payload_size;
    const size_t expected_file_size =
        signed_region_size + (is_authenticated ? KADD_RF_HMAC_SIZE : 0U);
    if ((size_t)payload_size_u64 != expected_payload_size ||
        file_size != expected_file_size) {
        free(file_bytes);
        return KADD_ERROR_MODEL_LOAD_FAILED;
    }

    /* [FIX REPORT-2026-0000028] 先做真实性认证，再做 CRC32 损坏检测。
     * CRC32 多项式 0xEDB88320 公开且无需密钥，攻击者改写叶节点概率后可直接重算，
     * 因此它只能防损坏、不能防篡改；只有 HMAC-SHA256 才提供来源认证。 */
    ret = kadd_rf_verify_authenticity(file_bytes, signed_region_size, is_authenticated);
    if (ret != KADD_SUCCESS) {
        free(file_bytes);
        return ret;
    }

    const unsigned char *payload = file_bytes + KADD_RF_HEADER_SIZE;
    if (crc32_bytes(payload, expected_payload_size) != expected_crc) {
        fprintf(stderr, "[kadd_rf] ERROR: Model checksum mismatch\n");
        free(file_bytes);
        return KADD_ERROR_MODEL_LOAD_FAILED;
    }

    model = (kadd_rf_model_t *)calloc(1, sizeof(*model));
    if (!model) {
        free(file_bytes);
        return KADD_ERROR_OUT_OF_MEMORY;
    }
    model->feature_count = feature_count;
    model->tree_count = tree_count;
    model->node_count = node_count;
    model->tree_offsets = (uint32_t *)malloc(offsets_size);
    model->nodes = (kadd_rf_node_t *)malloc((size_t)node_count * sizeof(kadd_rf_node_t));
    if (!model->tree_offsets || !model->nodes) {
        kadd_rf_model_free(model);
        free(file_bytes);
        return KADD_ERROR_OUT_OF_MEMORY;
    }

    for (uint32_t i = 0; i <= tree_count; ++i) {
        model->tree_offsets[i] = read_u32_le(payload + (size_t)i * 4U);
    }
    if (model->tree_offsets[0] != 0U ||
        model->tree_offsets[tree_count] != node_count) {
        kadd_rf_model_free(model);
        free(file_bytes);
        return KADD_ERROR_MODEL_LOAD_FAILED;
    }

    const unsigned char *node_bytes = payload + offsets_size;
    for (uint32_t i = 0; i < node_count; ++i) {
        const unsigned char *src = node_bytes + (size_t)i * KADD_RF_NODE_SIZE;
        kadd_rf_node_t *dst = &model->nodes[i];
        dst->feature = (int32_t)read_u32_le(src);
        dst->left = read_u32_le(src + 4);
        dst->right = read_u32_le(src + 8);
        dst->reserved = read_u32_le(src + 12);
        dst->threshold = read_f64_le(src + 16);
        dst->positive_probability = read_f64_le(src + 24);
    }
    free(file_bytes);

    for (uint32_t tree = 0; tree < tree_count; ++tree) {
        if (validate_tree(model, tree) != KADD_SUCCESS) {
            kadd_rf_model_free(model);
            return KADD_ERROR_MODEL_LOAD_FAILED;
        }
    }

#if KADD_RF_HAVE_SVE
    if (prepare_sve_layout(model) != KADD_SUCCESS) {
        kadd_rf_model_free(model);
        return KADD_ERROR_OUT_OF_MEMORY;
    }
#endif

    *model_out = model;
    return KADD_SUCCESS;
}

/*
 * predict_tree() / kadd_rf_model_predict() 不是 SVE 回退路径：两者均无 #if
 * 守卫，SVE ON/OFF 两种构建都会编译；且 kadd_rf_model_predict() 是
 * kadd_rf_engine.h 导出的对外单样本接口，UT/FUZZ 均会调用并覆盖。
 * 此处原先误加的 LCOV_EXCL_START 未配对 STOP（下一个 STOP 在
 * predict_batch_scalar 之后），会把这段已覆盖的对外接口整段排除；
 * 当前 coverage_all.sh 走 llvm-cov（不识别 LCOV_EXCL 标记）故未影响
 * 数字，一旦改用 lcov 就会凭空挖掉真实覆盖。故删除，仅保留下方
 * predict_batch_scalar 那对准确标记（它确实仅在非 SVE 构建编译）。
 */
static double predict_tree(const kadd_rf_model_t *model,
                           uint32_t root,
                           const float *features)
{
    uint32_t index = root;
    for (;;) {
        const kadd_rf_node_t *node = &model->nodes[index];
        if (node->feature < 0) {
            return node->positive_probability;
        }
        index = ((double)features[node->feature] <= node->threshold)
                    ? node->left
                    : node->right;
    }
}

int kadd_rf_model_predict(const kadd_rf_model_t *model,
                          const float *features,
                          float *positive_probability)
{
    if (!model || !features || !positive_probability) {
        return KADD_ERROR_NULL_PTR;
    }
    for (uint32_t i = 0; i < model->feature_count; ++i) {
        if (!isfinite(features[i])) {
            return KADD_ERROR_INVALID_PARAM;
        }
    }
    double sum = 0.0;
    for (uint32_t tree = 0; tree < model->tree_count; ++tree) {
        sum += predict_tree(model, model->tree_offsets[tree], features);
    }
    double probability = sum / (double)model->tree_count;
    if (probability < 0.0) {
        probability = 0.0;
    } else if (probability > 1.0) {
        probability = 1.0;
    }
    *positive_probability = (float)probability;
    return KADD_SUCCESS;
}

// LCOV_EXCL_START: scalar fallback, only compiled when SVE is unavailable
// (kept out of SVE builds so it does not appear as uncovered in coverage)
#if !KADD_RF_HAVE_SVE
static void predict_batch_scalar(const kadd_rf_model_t *model,
                                 const float *features,
                                 int sample_count,
                                 float *probabilities)
{
    for (int row = 0; row < sample_count; ++row) {
        (void)kadd_rf_model_predict(model,
                                    features + (size_t)row * model->feature_count,
                                    &probabilities[row]);
    }
}
#endif  // !KADD_RF_HAVE_SVE
// LCOV_EXCL_STOP

#if KADD_RF_HAVE_SVE
/*
 * Traverse svcntw() samples together using 32-bit lanes. The precomputed
 * threshold cutoff preserves sklearn's double-threshold branch decisions,
 * while leaf probabilities are accumulated in 64-bit SVE lanes in tree order
 * so the returned float is identical to scalar inference.
 *
 * This hot path performs no heap allocation and no input type conversion.
 *
 * NOTE: Unlike kadd_cnn_engine which uses fixed-width SVE assumptions
 * (and thus requires runtime_sve_width_is_compatible()), this RF SVE
 * implementation is fully width-adaptive via svcntw()/svcntd() and works
 * correctly on any SVE vector length.  No compile-time bit-width check
 * is needed.
 *
 * [FIX 白盒-VLA] SVE 规范向量长度上限 2048bit，故 svcntw() ≤ 64。
 * lane_sums 原为 VLA（长度取自 svcntw() 运行时值），改为编译期
 * 定长数组；若运行在超出规范上限的实现上（正常不可达，定长数组
 * 放不下整条向量），防御性逐样本回退到 kadd_rf_model_predict。
 */
#define KADD_RF_SVE_MAX_LANES 64u

static void predict_batch_sve_chunk(const kadd_rf_model_t *model,
                                    const float *features,
                                    int sample_count,
                                    float *probabilities)
{
    const double inverse_tree_count = 1.0 / (double)model->tree_count;
    if ((uint32_t)svcntw() > KADD_RF_SVE_MAX_LANES) {
        for (int row = 0; row < sample_count; ++row) {
            (void)kadd_rf_model_predict(
                model,
                features + (size_t)row * model->feature_count,
                &probabilities[row]);
        }
        return;
    }
    const uint32_t lane_capacity = (uint32_t)svcntw();
    const uint32_t f64_lane_capacity = (uint32_t)svcntd();
    double lane_sums[KADD_RF_SVE_MAX_LANES];

    for (uint32_t row = 0; row < (uint32_t)sample_count;
         row += lane_capacity) {
        const svbool_t pg =
            svwhilelt_b32(row, (uint32_t)sample_count);
        const svbool_t pg_low = svunpklo_b(pg);
        const svbool_t pg_high = svunpkhi_b(pg);
        const uint32_t lane_count =
            row + lane_capacity <= (uint32_t)sample_count
                ? lane_capacity
                : (uint32_t)sample_count - row;
        const svuint32_t rows = svindex_u32(row, 1U);
        svfloat64_t sums_low = svdup_f64(0.0);
        svfloat64_t sums_high = svdup_f64(0.0);

        for (uint32_t tree = 0; tree < model->tree_count; ++tree) {
            svuint32_t nodes = svdup_u32(model->tree_offsets[tree]);
            svbool_t active = pg;
            uint32_t traversal_steps = 0U;
            const uint32_t maximum_steps =
                model->tree_offsets[tree + 1U] -
                model->tree_offsets[tree];

            while (svptest_any(pg, active)) {
                if (++traversal_steps > maximum_steps) {
                    /*
                     * 纵深防御，正常不可达：validate_tree() 已强制内部节点的
                     * left/right 严格大于自身下标且落在 [begin, end) 内，
                     * 树内不可能成环，最长路径不超过 maximum_steps - 1 步。
                     *
                     * 此处原为 "#if !KADD_RF_HAVE_SVE → predict_batch_scalar"，
                     * 但本函数整体已在 #if KADD_RF_HAVE_SVE 内，取反后恒不成立、
                     * 任何构建都编不进来；且 predict_batch_scalar 仅在
                     * #if !KADD_RF_HAVE_SVE 内定义，SVE 构建下并不存在。
                     * 若剔除矛盾守卫后只裸 return，本 chunk 的 probabilities
                     * 将一个都不写，而 kadd_rf_model_predict_batch() 仍返回
                     * KADD_SUCCESS —— 静默输出未初始化脏数据（fail-open）。
                     * 现改为确定性写 0.0F 后返回（fail-safe），可达路径行为不变。
                     */
                    for (int idx = 0; idx < sample_count; ++idx) {
                        probabilities[idx] = 0.0F;
                    }
                    return;
                }

                const svuint32_t feature =
                    svld1_gather_u32index_u32(
                        active, model->sve_features, nodes);
                active =
                    svcmpne_n_u32(active, feature, UINT32_MAX);
                if (!svptest_any(pg, active)) {
                    break;
                }

                const svuint32_t flat_indices =
                    svadd_u32_x(
                        active,
                        svmul_n_u32_x(active, rows,
                                      model->feature_count),
                        feature);
                const svfloat32_t values =
                    svld1_gather_u32index_f32(
                        active, features, flat_indices);
                const svfloat32_t thresholds =
                    svld1_gather_u32index_f32(
                        active, model->sve_thresholds, nodes);
                const svbool_t go_left =
                    svcmple_f32(active, values, thresholds);
                const svuint32_t left =
                    svld1_gather_u32index_u32(
                        active, model->sve_left, nodes);
                const svuint32_t right =
                    svld1_gather_u32index_u32(
                        active, model->sve_right, nodes);
                const svuint32_t next_nodes =
                    svsel_u32(go_left, left, right);
                nodes = svsel_u32(active, next_nodes, nodes);
            }

            const svuint64_t leaf_nodes_low =
                svunpklo_u64(nodes);
            const svuint64_t leaf_nodes_high =
                svunpkhi_u64(nodes);
            const svfloat64_t probabilities_low =
                svld1_gather_u64index_f64(
                    pg_low, model->sve_probabilities,
                    leaf_nodes_low);
            const svfloat64_t probabilities_high =
                svld1_gather_u64index_f64(
                    pg_high, model->sve_probabilities,
                    leaf_nodes_high);
            sums_low =
                svadd_f64_x(pg_low, sums_low, probabilities_low);
            sums_high =
                svadd_f64_x(pg_high, sums_high, probabilities_high);
        }

        svst1_f64(pg_low, lane_sums, sums_low);
        svst1_f64(pg_high, lane_sums + f64_lane_capacity,
                  sums_high);
        for (uint32_t lane = 0; lane < lane_count; ++lane) {
            double probability =
                lane_sums[lane] * inverse_tree_count;
            if (probability < 0.0) {
                probability = 0.0;
            } else if (probability > 1.0) {
                probability = 1.0;
            }
            probabilities[row + lane] = (float)probability;
        }
    }
}
#endif

int kadd_rf_model_predict_batch(const kadd_rf_model_t *model,
                                const float *features,
                                int sample_count,
                                float *positive_probabilities,
                                int batch_size)
{
    if (!model || !features || !positive_probabilities) {
        return KADD_ERROR_NULL_PTR;
    }
    if (sample_count <= 0 ||
        sample_count > KADD_RF_MAX_SAMPLE_COUNT ) {
        return KADD_ERROR_INVALID_PARAM;
    }
    int effective_batch = batch_size;
    if (effective_batch <= 0 || effective_batch > KADD_RF_MAX_BATCH_SIZE) {
        effective_batch = KADD_RF_DEFAULT_BATCH_SIZE;
    }

    for (int begin = 0; begin < sample_count; begin += effective_batch) {
        const int count =
            begin + effective_batch < sample_count ? effective_batch : sample_count - begin;
        const float *chunk_features =
            features + (size_t)begin * model->feature_count;
#if KADD_RF_HAVE_SVE
        predict_batch_sve_chunk(model,
                                chunk_features,
                                count,
                                positive_probabilities + begin);
#else
        predict_batch_scalar(model,
                             chunk_features,
                             count,
                             positive_probabilities + begin);
#endif
    }
    return KADD_SUCCESS;
}

void kadd_rf_model_free(kadd_rf_model_t *model)
{
    if (!model) {
        return;
    }
#if KADD_RF_HAVE_SVE
    free(model->sve_features);
    free(model->sve_left);
    free(model->sve_right);
    free(model->sve_thresholds);
    free(model->sve_probabilities);
#endif
    free(model->tree_offsets);
    free(model->nodes);
    free(model);
}

int kadd_rf_model_get_feature_dim(const kadd_rf_model_t *model)
{
    if (!model) {
        return KADD_ERROR_NULL_PTR;
    }
    return (int)model->feature_count;
}
