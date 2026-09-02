/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * @file kadd_cnn_engine.c
 * @brief Portable TextCNN inference with an AArch64 SVE channel-blocked path.
 *
 * The AArch64 layout follows the same principles used by oneDNN's SVE
 * convolution and inner-product kernels: output-channel blocking by the
 * hardware vector length, packed contiguous weights, register accumulation,
 * and fused ReLU/max reduction. This is a model-specific C implementation and
 * does not copy or link oneDNN code.
 */

#include "kadd_cnn_engine.h"
#include "kadd_sve.h"
#include "kadd_errors.h"

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__aarch64__) && defined(__ARM_FEATURE_SVE)
#include <arm_sve.h>
#define KADD_CNN_HAVE_SVE 1
#else
#define KADD_CNN_HAVE_SVE 0
#endif

#define KADD_CNN_HEADER_SIZE 64U
#define KADD_CNN_VERSION 1U
#define KADD_CNN_SEQUENCE_LENGTH 300U
#define KADD_CNN_VOCAB_SIZE 29U
#define KADD_CNN_EMBEDDING_DIM 32U
#define KADD_CNN_CHANNEL_COUNT 64U
#define KADD_CNN_BRANCH_COUNT 4U
#define KADD_CNN_HIDDEN_DIM 64U
#define KADD_CNN_CLASS_COUNT 2U
#define KADD_CNN_PAD_ID 28U
#define KADD_CNN_FC1_INPUT_DIM \
    (KADD_CNN_CHANNEL_COUNT * KADD_CNN_BRANCH_COUNT)
#define KADD_CNN_TOTAL_KERNEL_WIDTH 14U
#define KADD_CNN_DEFAULT_BATCH_SIZE 64
#define KADD_CNN_MAX_BATCH_SIZE 1024
#define KADD_CNN_MAX_SAMPLE_COUNT 10000000

static const unsigned char g_kadd_cnn_magic[8] = {
    'K', 'A', 'D', 'D', 'C', 'N', '1', '\0'
};

static const uint32_t g_kernel_widths[KADD_CNN_BRANCH_COUNT] = {
    2U, 3U, 4U, 5U
};

struct kadd_cnn_model_t {
    /*
     * Contribution table:
     * [sum(kernel_width)][token][output_channel].
     * Each entry is dot(embedding[token], conv_weight[channel, :, kpos]).
     * The 29-token vocabulary makes this load-time transformation compact
     * (~104 KiB) and removes both Gather and the 32-wide embedding dot product
     * from the inference hot path.
     */
    float *token_contributions;
    size_t branch_contribution_offsets[KADD_CNN_BRANCH_COUNT];
    float conv_bias[KADD_CNN_BRANCH_COUNT * KADD_CNN_CHANNEL_COUNT];

    /* FC1 is transposed to [input=256][output=64] for contiguous SVE loads. */
    float *fc1_weights_input_major;
    float fc1_bias[KADD_CNN_HIDDEN_DIM];

    /* FC2 remains [class=2][hidden=64] for two vector reductions. */
    float fc2_weights[KADD_CNN_CLASS_COUNT * KADD_CNN_HIDDEN_DIM];
    float fc2_bias[KADD_CNN_CLASS_COUNT];
    int pad_embedding_is_zero;
};

#if KADD_CNN_HAVE_SVE && defined(KADD_CNN_EXPECTED_SVE_BITS)
/*
 * Unlike svcntb(), the CNTB inline assembly is not folded to the
 * -msve-vector-bits compile-time value.  This lets fixed-width builds reject
 * a process whose actual SVE vector length does not match the generated code.
 */
static size_t runtime_sve_vector_bits(void)
{
    size_t vector_bytes;
    __asm__ volatile("cntb %0" : "=r"(vector_bytes));
    return vector_bytes * 8U;
}

static int runtime_sve_width_is_compatible(void)
{
    return runtime_sve_vector_bits() ==
           (size_t)KADD_CNN_EXPECTED_SVE_BITS;
}
// LCOV_EXCL_STOP
#endif

static uint32_t read_u32_le(const unsigned char *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

static uint64_t read_u64_le(const unsigned char *p)
{
    return (uint64_t)read_u32_le(p) |
           ((uint64_t)read_u32_le(p + 4U) << 32U);
}

static float read_f32_le(const unsigned char *p)
{
    union {
        uint32_t u;
        float    f;
    } conv;
    conv.u = read_u32_le(p);
    return conv.f;
}

static uint32_t crc32_bytes(const unsigned char *data, size_t size)
{
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t mask = (uint32_t)(-(int32_t)(crc & 1U));
            crc = (crc >> 1U) ^
                  (UINT32_C(0xEDB88320) & mask);
        }
    }
    return ~crc;
}

static int checked_multiply_size(size_t left, size_t right, size_t *result)
{
    if (left != 0U && right > SIZE_MAX / left) {
        return -1;
    }
    *result = left * right;
    return 0;
}

static size_t expected_payload_float_count(void)
{
    size_t count =
        KADD_CNN_VOCAB_SIZE * KADD_CNN_EMBEDDING_DIM;
    for (size_t branch = 0; branch < KADD_CNN_BRANCH_COUNT; ++branch) {
        count += KADD_CNN_CHANNEL_COUNT *
                 KADD_CNN_EMBEDDING_DIM *
                 g_kernel_widths[branch];
        count += KADD_CNN_CHANNEL_COUNT;
    }
    count += KADD_CNN_HIDDEN_DIM * KADD_CNN_FC1_INPUT_DIM;
    count += KADD_CNN_HIDDEN_DIM;
    count += KADD_CNN_CLASS_COUNT * KADD_CNN_HIDDEN_DIM;
    count += KADD_CNN_CLASS_COUNT;
    return count;
}

static int read_entire_file(const char *path,
                            unsigned char **bytes_out,
                            size_t *size_out)
{
    FILE *stream = NULL;
    unsigned char *bytes = NULL;
    long file_size_long;

    if (!path || !bytes_out || !size_out) {
        return KADD_ERROR_NULL_PTR;
    }
    *bytes_out = NULL;
    *size_out = 0U;

    stream = fopen(path, "rb");
    if (!stream) {
        fprintf(stderr, "[kadd_cnn] ERROR: Cannot open '%s': %s\n",
                path, strerror(errno));
        return KADD_ERROR_FILE_OPERATION_FAILED;
    }
    if (fseek(stream, 0, SEEK_END) != 0 ||
        (file_size_long = ftell(stream)) < 0 ||
        fseek(stream, 0, SEEK_SET) != 0) {
        (void)fclose(stream);
        return KADD_ERROR_FILE_OPERATION_FAILED;
    }
    const size_t file_size = (size_t)file_size_long;
    if ((long)file_size != file_size_long ||
        file_size < KADD_CNN_HEADER_SIZE) {
        (void)fclose(stream);
        return KADD_ERROR_MODEL_LOAD_FAILED;
    }

    bytes = (unsigned char *)malloc(file_size);
    if (!bytes) {
        (void)fclose(stream);
        return KADD_ERROR_OUT_OF_MEMORY;
    }
    if (fread(bytes, 1U, file_size, stream) != file_size) {
        free(bytes);
        (void)fclose(stream);
        return KADD_ERROR_FILE_OPERATION_FAILED;
    }
    (void)fclose(stream);
    *bytes_out = bytes;
    *size_out = file_size;
    return KADD_SUCCESS;
}

static int decode_payload_floats(const unsigned char *payload,
                                 size_t float_count,
                                 float *values)
{
    for (size_t i = 0; i < float_count; ++i) {
        values[i] = read_f32_le(payload + i * sizeof(float));
        if (!isfinite(values[i])) {
            return KADD_ERROR_MODEL_LOAD_FAILED;
        }
    }
    return KADD_SUCCESS;
}

static int validate_prepared_numeric_bounds(
    const kadd_cnn_model_t *model)
{
    double pooled_upper[KADD_CNN_FC1_INPUT_DIM];
    double hidden_upper[KADD_CNN_HIDDEN_DIM];

    /*
     * Reject finite-on-disk weights that could overflow a float32 hot-path
     * accumulator.  The bounds are deliberately conservative and are
     * evaluated only once while loading the model.
     */
    for (size_t branch = 0; branch < KADD_CNN_BRANCH_COUNT; ++branch) {
        const size_t kernel_width = g_kernel_widths[branch];
        const size_t table_base =
            model->branch_contribution_offsets[branch];
        for (size_t channel = 0;
             channel < KADD_CNN_CHANNEL_COUNT;
             ++channel) {
            double upper = fabs(
                (double)model->conv_bias[
                    branch * KADD_CNN_CHANNEL_COUNT + channel]);
            for (size_t kpos = 0; kpos < kernel_width; ++kpos) {
                double maximum = 0.0;
                for (size_t token = 0;
                     token < KADD_CNN_VOCAB_SIZE;
                     ++token) {
                    const float contribution =
                        model->token_contributions[
                            table_base +
                            (kpos * KADD_CNN_VOCAB_SIZE + token) *
                                KADD_CNN_CHANNEL_COUNT +
                            channel];
                    if (!isfinite(contribution)) {
                        return KADD_ERROR_MODEL_LOAD_FAILED;
                    }
                    const double magnitude = fabs((double)contribution);
                    if (magnitude > maximum) {
                        maximum = magnitude;
                    }
                }
                upper += maximum;
            }
            if (!isfinite(upper) || upper > (double)FLT_MAX) {
                return KADD_ERROR_MODEL_LOAD_FAILED;
            }
            pooled_upper[
                branch * KADD_CNN_CHANNEL_COUNT + channel] = upper;
        }
    }

    for (size_t output = 0;
         output < KADD_CNN_HIDDEN_DIM;
         ++output) {
        double upper = fabs((double)model->fc1_bias[output]);
        for (size_t input = 0;
             input < KADD_CNN_FC1_INPUT_DIM;
             ++input) {
            upper +=
                pooled_upper[input] *
                fabs((double)model->fc1_weights_input_major[
                    input * KADD_CNN_HIDDEN_DIM + output]);
        }
        if (!isfinite(upper) || upper > (double)FLT_MAX) {
            return KADD_ERROR_MODEL_LOAD_FAILED;
        }
        hidden_upper[output] = upper;
    }

    for (size_t class_index = 0;
         class_index < KADD_CNN_CLASS_COUNT;
         ++class_index) {
        double upper = fabs((double)model->fc2_bias[class_index]);
        for (size_t input = 0;
             input < KADD_CNN_HIDDEN_DIM;
             ++input) {
            upper +=
                hidden_upper[input] *
                fabs((double)model->fc2_weights[
                    class_index * KADD_CNN_HIDDEN_DIM + input]);
        }
        if (!isfinite(upper) || upper > (double)FLT_MAX) {
            return KADD_ERROR_MODEL_LOAD_FAILED;
        }
    }
    return KADD_SUCCESS;
}

static int prepare_model(kadd_cnn_model_t *model,
                         const float *raw,
                         size_t raw_count)
{
    const size_t contribution_count =
        KADD_CNN_TOTAL_KERNEL_WIDTH *
        KADD_CNN_VOCAB_SIZE *
        KADD_CNN_CHANNEL_COUNT;
    const size_t fc1_weight_count =
        KADD_CNN_FC1_INPUT_DIM * KADD_CNN_HIDDEN_DIM;

    model->token_contributions =
        (float *)malloc(contribution_count * sizeof(float));
    model->fc1_weights_input_major =
        (float *)malloc(fc1_weight_count * sizeof(float));
    if (!model->token_contributions ||
        !model->fc1_weights_input_major) {
        return KADD_ERROR_OUT_OF_MEMORY;
    }

    size_t cursor = 0U;
    const float *embedding = raw + cursor;
    cursor += KADD_CNN_VOCAB_SIZE * KADD_CNN_EMBEDDING_DIM;

    model->pad_embedding_is_zero = 1;
    for (size_t dim = 0; dim < KADD_CNN_EMBEDDING_DIM; ++dim) {
        if (embedding[KADD_CNN_PAD_ID * KADD_CNN_EMBEDDING_DIM + dim] !=
            0.0F) {
            model->pad_embedding_is_zero = 0;
            break;
        }
    }

    size_t contribution_offset = 0U;
    for (size_t branch = 0; branch < KADD_CNN_BRANCH_COUNT; ++branch) {
        const size_t kernel_width = g_kernel_widths[branch];
        const size_t weight_count =
            KADD_CNN_CHANNEL_COUNT *
            KADD_CNN_EMBEDDING_DIM *
            kernel_width;
        const float *weights = raw + cursor;
        cursor += weight_count;
        const float *bias = raw + cursor;
        cursor += KADD_CNN_CHANNEL_COUNT;

        model->branch_contribution_offsets[branch] =
            contribution_offset;
        for (size_t ch = 0; ch < KADD_CNN_CHANNEL_COUNT; ++ch) {
            model->conv_bias[branch * KADD_CNN_CHANNEL_COUNT + ch] = bias[ch];
        }

        for (size_t kpos = 0; kpos < kernel_width; ++kpos) {
            for (size_t token = 0; token < KADD_CNN_VOCAB_SIZE; ++token) {
                for (size_t channel = 0;
                     channel < KADD_CNN_CHANNEL_COUNT;
                     ++channel) {
                    float sum = 0.0F;
                    for (size_t dim = 0;
                         dim < KADD_CNN_EMBEDDING_DIM;
                         ++dim) {
                        const float input =
                            embedding[token * KADD_CNN_EMBEDDING_DIM + dim];
                        const float weight =
                            weights[(channel * KADD_CNN_EMBEDDING_DIM + dim) *
                                        kernel_width +
                                    kpos];
                        sum += input * weight;
                    }
                    model->token_contributions[
                        contribution_offset +
                        (kpos * KADD_CNN_VOCAB_SIZE + token) *
                            KADD_CNN_CHANNEL_COUNT +
                        channel] = sum;
                }
            }
        }
        contribution_offset +=
            kernel_width *
            KADD_CNN_VOCAB_SIZE *
            KADD_CNN_CHANNEL_COUNT;
    }

    const float *fc1_weights = raw + cursor;
    cursor += fc1_weight_count;
    for (size_t output = 0; output < KADD_CNN_HIDDEN_DIM; ++output) {
        for (size_t input = 0; input < KADD_CNN_FC1_INPUT_DIM; ++input) {
            model->fc1_weights_input_major[
                input * KADD_CNN_HIDDEN_DIM + output] =
                fc1_weights[output * KADD_CNN_FC1_INPUT_DIM + input];
        }
    }
    for (size_t i = 0; i < KADD_CNN_HIDDEN_DIM; ++i) {
        model->fc1_bias[i] = (raw + cursor)[i];
    }
    cursor += KADD_CNN_HIDDEN_DIM;

    for (size_t i = 0; i < (size_t)KADD_CNN_CLASS_COUNT * KADD_CNN_HIDDEN_DIM; ++i) {
        model->fc2_weights[i] = (raw + cursor)[i];
    }
    cursor += KADD_CNN_CLASS_COUNT * KADD_CNN_HIDDEN_DIM;
    for (size_t i = 0; i < KADD_CNN_CLASS_COUNT; ++i) {
        model->fc2_bias[i] = (raw + cursor)[i];
    }
    cursor += KADD_CNN_CLASS_COUNT;

    if (cursor != raw_count) {
        return KADD_ERROR_MODEL_LOAD_FAILED;
    }
    return validate_prepared_numeric_bounds(model);
}

int kadd_cnn_model_load(kadd_cnn_model_t **model_out,
                        const char *model_path)
{
    unsigned char *file_bytes = NULL;
    size_t file_size = 0U;
    float *raw = NULL;
    kadd_cnn_model_t *model = NULL;
    int result;

    if (!model_out || !model_path) {
        return KADD_ERROR_NULL_PTR;
    }
    *model_out = NULL;

#if KADD_CNN_HAVE_SVE
    /*
     * 纵深防护：上层分发层已做 HWCAP 检测，这里再次确认——
     * runtime_sve_width_is_compatible() 使用的 cntb 指令本身要求硬件支持 SVE，
     * 在无 SVE 的 CPU 上执行即 SIGILL，必须先用 HWCAP 确认硬件能力，
     * 不支持则返回失败，由上层回退 ONNX 后端。
     */
    if (!kadd_sve_runtime_available()) {
        fprintf(stderr,
                "[kadd_cnn] ERROR: CPU does not support SVE, "
                "native CNN backend unavailable\n");
        return KADD_ERROR_MODEL_LOAD_FAILED;
    }
#endif

#if KADD_CNN_HAVE_SVE && defined(KADD_CNN_EXPECTED_SVE_BITS)
    if (!runtime_sve_width_is_compatible()) {
        fprintf(
            stderr,
            "[kadd_cnn] ERROR: This binary requires %d-bit SVE, "
            "but the process is using %zu-bit SVE\n",
            KADD_CNN_EXPECTED_SVE_BITS,
            runtime_sve_vector_bits());
        return KADD_ERROR_MODEL_LOAD_FAILED;
    }
#endif

    result = read_entire_file(model_path, &file_bytes, &file_size);
    if (result != KADD_SUCCESS) {
        return result;
    }
    const unsigned char *header = file_bytes;
    if (memcmp(header, g_kadd_cnn_magic,
               sizeof(g_kadd_cnn_magic)) != 0 ||
        read_u32_le(header + 8U) != KADD_CNN_VERSION ||
        read_u32_le(header + 12U) != KADD_CNN_HEADER_SIZE ||
        read_u32_le(header + 16U) != KADD_CNN_SEQUENCE_LENGTH ||
        read_u32_le(header + 20U) != KADD_CNN_VOCAB_SIZE ||
        read_u32_le(header + 24U) != KADD_CNN_EMBEDDING_DIM ||
        read_u32_le(header + 28U) != KADD_CNN_CHANNEL_COUNT ||
        read_u32_le(header + 32U) != KADD_CNN_BRANCH_COUNT ||
        read_u32_le(header + 36U) != KADD_CNN_HIDDEN_DIM ||
        read_u32_le(header + 40U) != KADD_CNN_CLASS_COUNT ||
        read_u32_le(header + 44U) != KADD_CNN_PAD_ID ||
        read_u32_le(header + 60U) != 0U) {
        free(file_bytes);
        return KADD_ERROR_MODEL_LOAD_FAILED;
    }

    const size_t float_count = expected_payload_float_count();
    size_t expected_payload_size = 0U;
    if (checked_multiply_size(float_count, sizeof(float),
                              &expected_payload_size) != 0 ||
        read_u64_le(header + 48U) !=
            (uint64_t)expected_payload_size ||
        expected_payload_size >
            SIZE_MAX - KADD_CNN_HEADER_SIZE ||
        file_size != KADD_CNN_HEADER_SIZE + expected_payload_size) {
        free(file_bytes);
        return KADD_ERROR_MODEL_LOAD_FAILED;
    }
    const unsigned char *payload =
        file_bytes + KADD_CNN_HEADER_SIZE;
    if (crc32_bytes(payload, expected_payload_size) !=
        read_u32_le(header + 56U)) {
        fprintf(stderr, "[kadd_cnn] ERROR: Model checksum mismatch\n");
        free(file_bytes);
        return KADD_ERROR_MODEL_LOAD_FAILED;
    }

    raw = (float *)malloc(float_count * sizeof(float));
    model = (kadd_cnn_model_t *)calloc(1U, sizeof(*model));
    if (!raw || !model) {
        free(raw);
        free(model);
        free(file_bytes);
        return KADD_ERROR_OUT_OF_MEMORY;
    }
    result = decode_payload_floats(payload, float_count, raw);
    free(file_bytes);
    if (result == KADD_SUCCESS) {
        result = prepare_model(model, raw, float_count);
    }
    free(raw);
    if (result != KADD_SUCCESS) {
        kadd_cnn_model_free(model);
        return result;
    }

    *model_out = model;
    return KADD_SUCCESS;
}

static int decode_tokens(const float *input,
                         uint8_t tokens[KADD_CNN_SEQUENCE_LENGTH],
                         size_t *active_length)
{
    size_t last_non_padding = 0U;
    for (size_t i = 0; i < KADD_CNN_SEQUENCE_LENGTH; ++i) {
        const float value = input[i];
        if (!isfinite(value) ||
            value < 0.0F ||
            value > (float)(KADD_CNN_VOCAB_SIZE - 1U) ||
            truncf(value) != value) {
            return KADD_ERROR_INVALID_PARAM;
        }
        const uint8_t token = (uint8_t)value;
        tokens[i] = token;
        if (token != KADD_CNN_PAD_ID) {
            last_non_padding = i + 1U;
        }
    }
    *active_length = last_non_padding;
    return KADD_SUCCESS;
}

static size_t branch_position_count(const kadd_cnn_model_t *model,
                                    size_t active_length,
                                    size_t kernel_width)
{
    const size_t all_positions =
        KADD_CNN_SEQUENCE_LENGTH - kernel_width + 1U;
    if (!model->pad_embedding_is_zero) {
        return all_positions;
    }

    /*
     * Windows after active_length contain padding only. Their convolution
     * value is the bias, so evaluating the first such window is sufficient
     * for global max pooling. Windows that cross into the padding suffix are
     * retained.
     */
    const size_t useful_positions = active_length + 1U;
    return useful_positions < all_positions
               ? useful_positions
               : all_positions;
}

#if !KADD_CNN_HAVE_SVE
static void pool_convolutions_scalar(const kadd_cnn_model_t *model,
                                     const uint8_t *tokens,
                                     size_t active_length,
                                     float *pooled)
{
    _Alignas(64) float values[KADD_CNN_CHANNEL_COUNT];
    for (size_t branch = 0; branch < KADD_CNN_BRANCH_COUNT; ++branch) {
        const size_t kernel_width = g_kernel_widths[branch];
        const size_t position_count =
            branch_position_count(model, active_length, kernel_width);
        const float *bias =
            model->conv_bias +
            branch * KADD_CNN_CHANNEL_COUNT;
        const size_t table_base =
            model->branch_contribution_offsets[branch];
        float *maximum =
            pooled + branch * KADD_CNN_CHANNEL_COUNT;
        for (size_t ch = 0; ch < KADD_CNN_CHANNEL_COUNT; ++ch) {
            maximum[ch] = 0.0f;
        }

        for (size_t position = 0;
             position < position_count;
             ++position) {
            for (size_t ch = 0; ch < KADD_CNN_CHANNEL_COUNT; ++ch) {
                values[ch] = bias[ch];
            }
            for (size_t kpos = 0;
                 kpos < kernel_width;
                 ++kpos) {
                const float *contribution =
                    model->token_contributions +
                    table_base +
                    (kpos * KADD_CNN_VOCAB_SIZE +
                     tokens[position + kpos]) *
                        KADD_CNN_CHANNEL_COUNT;
                for (size_t channel = 0;
                     channel < KADD_CNN_CHANNEL_COUNT;
                     ++channel) {
                    values[channel] += contribution[channel];
                }
            }
            for (size_t channel = 0;
                 channel < KADD_CNN_CHANNEL_COUNT;
                 ++channel) {
                if (values[channel] > maximum[channel]) {
                    maximum[channel] = values[channel];
                }
            }
        }
    }
}

static void fully_connected_scalar(const kadd_cnn_model_t *model,
                                   const float *pooled,
                                   float *hidden,
                                   float logits[KADD_CNN_CLASS_COUNT])
{
    for (size_t i = 0; i < KADD_CNN_HIDDEN_DIM; ++i) {
        hidden[i] = model->fc1_bias[i];
    }
    for (size_t input = 0; input < KADD_CNN_FC1_INPUT_DIM; ++input) {
        const float input_value = pooled[input];
        const float *weights =
            model->fc1_weights_input_major +
            input * KADD_CNN_HIDDEN_DIM;
        for (size_t output = 0;
             output < KADD_CNN_HIDDEN_DIM;
             ++output) {
            hidden[output] += input_value * weights[output];
        }
    }
    for (size_t output = 0;
         output < KADD_CNN_HIDDEN_DIM;
         ++output) {
        if (hidden[output] < 0.0F) {
            hidden[output] = 0.0F;
        }
    }

    for (size_t class_index = 0;
         class_index < KADD_CNN_CLASS_COUNT;
         ++class_index) {
        float value = model->fc2_bias[class_index];
        for (size_t input = 0; input < KADD_CNN_HIDDEN_DIM; ++input) {
            value += hidden[input] *
                     model->fc2_weights[
                         class_index * KADD_CNN_HIDDEN_DIM + input];
        }
        logits[class_index] = value;
    }
}
#endif

#if KADD_CNN_HAVE_SVE
#define KADD_CNN_SVE256_LOAD_ACCUMULATOR(INDEX, OFFSET) \
    svfloat32_t a##INDEX = svld1_f32(pg, bias + (OFFSET)); \
    svfloat32_t m##INDEX = svdup_f32(0.0F)

#define KADD_CNN_SVE256_ADD_CONTRIBUTION(INDEX, OFFSET) \
    a##INDEX = svadd_f32_x( \
        pg, a##INDEX, svld1_f32(pg, contribution + (OFFSET)))

#define KADD_CNN_SVE256_UPDATE_MAXIMUM(INDEX) \
    m##INDEX = svmax_f32_x( \
        pg, m##INDEX, svmax_n_f32_x(pg, a##INDEX, 0.0F))

#define KADD_CNN_SVE256_STORE_MAXIMUM(INDEX, OFFSET) \
    svst1_f32(pg, output + (OFFSET), m##INDEX)

static void pool_branch_sve256(const kadd_cnn_model_t *model,
                               const uint8_t *tokens,
                               size_t branch,
                               size_t position_count,
                               float *output)
{
    const svbool_t pg = svptrue_b32();
    const size_t kernel_width = g_kernel_widths[branch];
    const float *bias =
        model->conv_bias +
        branch * KADD_CNN_CHANNEL_COUNT;
    const size_t table_base =
        model->branch_contribution_offsets[branch];

    KADD_CNN_SVE256_LOAD_ACCUMULATOR(0, 0U);
    KADD_CNN_SVE256_LOAD_ACCUMULATOR(1, 8U);
    KADD_CNN_SVE256_LOAD_ACCUMULATOR(2, 16U);
    KADD_CNN_SVE256_LOAD_ACCUMULATOR(3, 24U);
    KADD_CNN_SVE256_LOAD_ACCUMULATOR(4, 32U);
    KADD_CNN_SVE256_LOAD_ACCUMULATOR(5, 40U);
    KADD_CNN_SVE256_LOAD_ACCUMULATOR(6, 48U);
    KADD_CNN_SVE256_LOAD_ACCUMULATOR(7, 56U);

    for (size_t position = 0;
         position < position_count;
         ++position) {
        a0 = svld1_f32(pg, bias + 0U);
        a1 = svld1_f32(pg, bias + 8U);
        a2 = svld1_f32(pg, bias + 16U);
        a3 = svld1_f32(pg, bias + 24U);
        a4 = svld1_f32(pg, bias + 32U);
        a5 = svld1_f32(pg, bias + 40U);
        a6 = svld1_f32(pg, bias + 48U);
        a7 = svld1_f32(pg, bias + 56U);

        for (size_t kpos = 0; kpos < kernel_width; ++kpos) {
            const float *contribution =
                model->token_contributions +
                table_base +
                (kpos * KADD_CNN_VOCAB_SIZE +
                 tokens[position + kpos]) *
                    KADD_CNN_CHANNEL_COUNT;
            KADD_CNN_SVE256_ADD_CONTRIBUTION(0, 0U);
            KADD_CNN_SVE256_ADD_CONTRIBUTION(1, 8U);
            KADD_CNN_SVE256_ADD_CONTRIBUTION(2, 16U);
            KADD_CNN_SVE256_ADD_CONTRIBUTION(3, 24U);
            KADD_CNN_SVE256_ADD_CONTRIBUTION(4, 32U);
            KADD_CNN_SVE256_ADD_CONTRIBUTION(5, 40U);
            KADD_CNN_SVE256_ADD_CONTRIBUTION(6, 48U);
            KADD_CNN_SVE256_ADD_CONTRIBUTION(7, 56U);
        }
        KADD_CNN_SVE256_UPDATE_MAXIMUM(0);
        KADD_CNN_SVE256_UPDATE_MAXIMUM(1);
        KADD_CNN_SVE256_UPDATE_MAXIMUM(2);
        KADD_CNN_SVE256_UPDATE_MAXIMUM(3);
        KADD_CNN_SVE256_UPDATE_MAXIMUM(4);
        KADD_CNN_SVE256_UPDATE_MAXIMUM(5);
        KADD_CNN_SVE256_UPDATE_MAXIMUM(6);
        KADD_CNN_SVE256_UPDATE_MAXIMUM(7);
    }

    KADD_CNN_SVE256_STORE_MAXIMUM(0, 0U);
    KADD_CNN_SVE256_STORE_MAXIMUM(1, 8U);
    KADD_CNN_SVE256_STORE_MAXIMUM(2, 16U);
    KADD_CNN_SVE256_STORE_MAXIMUM(3, 24U);
    KADD_CNN_SVE256_STORE_MAXIMUM(4, 32U);
    KADD_CNN_SVE256_STORE_MAXIMUM(5, 40U);
    KADD_CNN_SVE256_STORE_MAXIMUM(6, 48U);
    KADD_CNN_SVE256_STORE_MAXIMUM(7, 56U);
}

// LCOV_EXCL_START: VLA fallback only compiled when not fixed at 256-bit SVE
#if KADD_CNN_HAVE_SVE && !defined(KADD_CNN_SVE256_ONLY)
static void pool_branch_sve_vla(const kadd_cnn_model_t *model,
                                const uint8_t *tokens,
                                size_t branch,
                                size_t position_count,
                                float *output)
{
    const size_t kernel_width = g_kernel_widths[branch];
    const float *bias =
        model->conv_bias +
        branch * KADD_CNN_CHANNEL_COUNT;
    const size_t table_base =
        model->branch_contribution_offsets[branch];
    const size_t vector_length = svcntw();

    for (size_t channel = 0;
         channel < KADD_CNN_CHANNEL_COUNT;
         channel += vector_length) {
        const svbool_t pg =
            svwhilelt_b32((uint64_t)channel,
                          (uint64_t)KADD_CNN_CHANNEL_COUNT);
        svfloat32_t maximum = svdup_f32(0.0F);
        for (size_t position = 0;
             position < position_count;
             ++position) {
            svfloat32_t value =
                svld1_f32(pg, bias + channel);
            for (size_t kpos = 0;
                 kpos < kernel_width;
                 ++kpos) {
                const float *contribution =
                    model->token_contributions +
                    table_base +
                    (kpos * KADD_CNN_VOCAB_SIZE +
                     tokens[position + kpos]) *
                        KADD_CNN_CHANNEL_COUNT +
                    channel;
                value = svadd_f32_x(
                    pg, value, svld1_f32(pg, contribution));
            }
            maximum = svmax_f32_x(
                pg, maximum,
                svmax_n_f32_x(pg, value, 0.0F));
        }
        svst1_f32(pg, output + channel, maximum);
    }
}
#endif  // KADD_CNN_HAVE_SVE && !KADD_CNN_SVE256_ONLY

static void pool_convolutions_sve(const kadd_cnn_model_t *model,
                                  const uint8_t *tokens,
                                  size_t active_length,
                                  float *pooled)
{
#if KADD_CNN_HAVE_SVE && !defined(KADD_CNN_SVE256_ONLY)
    const int is_sve256 = svcntw() == 8U;
#endif
    for (size_t branch = 0;
         branch < KADD_CNN_BRANCH_COUNT;
         ++branch) {
        const size_t position_count =
            branch_position_count(
                model, active_length, g_kernel_widths[branch]);
        float *output =
            pooled + branch * KADD_CNN_CHANNEL_COUNT;
#if KADD_CNN_HAVE_SVE && defined(KADD_CNN_SVE256_ONLY)
        pool_branch_sve256(
            model, tokens, branch, position_count, output);
#else
        if (is_sve256) {
            pool_branch_sve256(
                model, tokens, branch, position_count, output);
        } else {
            pool_branch_sve_vla(
                model, tokens, branch, position_count, output);
        }
#endif
    }
}

#define KADD_CNN_SVE256_FC1_INIT(INDEX, OFFSET) \
    svfloat32_t a##INDEX = \
        svld1_f32(pg, model->fc1_bias + (OFFSET))

#define KADD_CNN_SVE256_FC1_ACCUMULATE(INDEX, OFFSET) \
    a##INDEX = svmla_f32_x( \
        pg, a##INDEX, input_value, \
        svld1_f32(pg, weights + (OFFSET)))

#define KADD_CNN_SVE256_FC1_STORE(INDEX, OFFSET) \
    svst1_f32( \
        pg, hidden + (OFFSET), \
        svmax_n_f32_x(pg, a##INDEX, 0.0F))

static void fc1_sve256(const kadd_cnn_model_t *model,
                       const float *pooled,
                       float *hidden)
{
    const svbool_t pg = svptrue_b32();
    KADD_CNN_SVE256_FC1_INIT(0, 0U);
    KADD_CNN_SVE256_FC1_INIT(1, 8U);
    KADD_CNN_SVE256_FC1_INIT(2, 16U);
    KADD_CNN_SVE256_FC1_INIT(3, 24U);
    KADD_CNN_SVE256_FC1_INIT(4, 32U);
    KADD_CNN_SVE256_FC1_INIT(5, 40U);
    KADD_CNN_SVE256_FC1_INIT(6, 48U);
    KADD_CNN_SVE256_FC1_INIT(7, 56U);

    for (size_t input = 0;
         input < KADD_CNN_FC1_INPUT_DIM;
         ++input) {
        const svfloat32_t input_value =
            svdup_f32(pooled[input]);
        const float *weights =
            model->fc1_weights_input_major +
            input * KADD_CNN_HIDDEN_DIM;
        KADD_CNN_SVE256_FC1_ACCUMULATE(0, 0U);
        KADD_CNN_SVE256_FC1_ACCUMULATE(1, 8U);
        KADD_CNN_SVE256_FC1_ACCUMULATE(2, 16U);
        KADD_CNN_SVE256_FC1_ACCUMULATE(3, 24U);
        KADD_CNN_SVE256_FC1_ACCUMULATE(4, 32U);
        KADD_CNN_SVE256_FC1_ACCUMULATE(5, 40U);
        KADD_CNN_SVE256_FC1_ACCUMULATE(6, 48U);
        KADD_CNN_SVE256_FC1_ACCUMULATE(7, 56U);
    }

    KADD_CNN_SVE256_FC1_STORE(0, 0U);
    KADD_CNN_SVE256_FC1_STORE(1, 8U);
    KADD_CNN_SVE256_FC1_STORE(2, 16U);
    KADD_CNN_SVE256_FC1_STORE(3, 24U);
    KADD_CNN_SVE256_FC1_STORE(4, 32U);
    KADD_CNN_SVE256_FC1_STORE(5, 40U);
    KADD_CNN_SVE256_FC1_STORE(6, 48U);
    KADD_CNN_SVE256_FC1_STORE(7, 56U);
}

// LCOV_EXCL_STOP

// LCOV_EXCL_START: VLA fallback only compiled when not fixed at 256-bit SVE
#if KADD_CNN_HAVE_SVE && !defined(KADD_CNN_SVE256_ONLY)
static void fc1_sve_vla(const kadd_cnn_model_t *model,
                        const float *pooled,
                        float *hidden)
{
    const size_t vector_length = svcntw();
    for (size_t output = 0;
         output < KADD_CNN_HIDDEN_DIM;
         output += vector_length) {
        const svbool_t pg =
            svwhilelt_b32((uint64_t)output,
                          (uint64_t)KADD_CNN_HIDDEN_DIM);
        svfloat32_t value =
            svld1_f32(pg, model->fc1_bias + output);
        for (size_t input = 0;
             input < KADD_CNN_FC1_INPUT_DIM;
             ++input) {
            const svfloat32_t input_value =
                svdup_f32(pooled[input]);
            const svfloat32_t weight =
                svld1_f32(
                    pg,
                    model->fc1_weights_input_major +
                        input * KADD_CNN_HIDDEN_DIM +
                        output);
            value = svmla_f32_x(
                pg, value, input_value, weight);
        }
        value = svmax_n_f32_x(pg, value, 0.0F);
        svst1_f32(pg, hidden + output, value);
    }
}
#endif  // KADD_CNN_HAVE_SVE && !KADD_CNN_SVE256_ONLY

// LCOV_EXCL_STOP

static void fully_connected_sve(const kadd_cnn_model_t *model,
                                const float *pooled,
                                float *hidden,
                                float logits[KADD_CNN_CLASS_COUNT])
{
#if KADD_CNN_HAVE_SVE && defined(KADD_CNN_SVE256_ONLY)
    fc1_sve256(model, pooled, hidden);
#else
    if (svcntw() == 8U) {
        fc1_sve256(model, pooled, hidden);
    } else {
        fc1_sve_vla(model, pooled, hidden);
    }
#endif

    const size_t vector_length = svcntw();
    svfloat32_t sum0 = svdup_f32(0.0F);
    svfloat32_t sum1 = svdup_f32(0.0F);
    for (size_t input = 0;
         input < KADD_CNN_HIDDEN_DIM;
         input += vector_length) {
        const svbool_t pg =
            svwhilelt_b32((uint64_t)input,
                          (uint64_t)KADD_CNN_HIDDEN_DIM);
        const svfloat32_t value =
            svld1_f32(pg, hidden + input);
        sum0 = svmla_f32_m(
            pg, sum0, value,
            svld1_f32(pg, model->fc2_weights + input));
        sum1 = svmla_f32_m(
            pg, sum1, value,
            svld1_f32(
                pg,
                model->fc2_weights +
                    KADD_CNN_HIDDEN_DIM +
                    input));
    }
    logits[0] =
        model->fc2_bias[0] +
        svaddv_f32(svptrue_b32(), sum0);
    logits[1] =
        model->fc2_bias[1] +
        svaddv_f32(svptrue_b32(), sum1);
}
#endif

static float binary_softmax_positive(
    const float logits[KADD_CNN_CLASS_COUNT])
{
    const float difference = logits[1] - logits[0];
    if (difference >= 0.0F) {
        const float exponent = expf(-difference);
        return 1.0F / (1.0F + exponent);
    }
    const float exponent = expf(difference);
    return exponent / (1.0F + exponent);
}

int kadd_cnn_model_predict(const kadd_cnn_model_t *model,
                           const float *input,
                           float *positive_probability)
{
    _Alignas(64) uint8_t tokens[KADD_CNN_SEQUENCE_LENGTH];
    _Alignas(64) float pooled[KADD_CNN_FC1_INPUT_DIM];
    _Alignas(64) float hidden[KADD_CNN_HIDDEN_DIM];
    float logits[KADD_CNN_CLASS_COUNT];
    size_t active_length = 0U;

    if (!model || !input || !positive_probability) {
        return KADD_ERROR_NULL_PTR;
    }
#if KADD_CNN_HAVE_SVE && defined(KADD_CNN_EXPECTED_SVE_BITS)
    if (!runtime_sve_width_is_compatible()) {
        return KADD_ERROR_INFERENCE_FAILED;
    }
#endif
    const int result =
        decode_tokens(input, tokens, &active_length);
    if (result != KADD_SUCCESS) {
        return result;
    }

#if KADD_CNN_HAVE_SVE
    pool_convolutions_sve(
        model, tokens, active_length, pooled);
    fully_connected_sve(
        model, pooled, hidden, logits);
#else
    pool_convolutions_scalar(
        model, tokens, active_length, pooled);
    fully_connected_scalar(
        model, pooled, hidden, logits);
#endif
    if (!isfinite(logits[0]) || !isfinite(logits[1])) {
        return KADD_ERROR_INFERENCE_FAILED;
    }
    const float probability =
        binary_softmax_positive(logits);
    if (!isfinite(probability) ||
        probability < 0.0F ||
        probability > 1.0F) {
        return KADD_ERROR_INFERENCE_FAILED;
    }
    *positive_probability = probability;
    return KADD_SUCCESS;
}

int kadd_cnn_model_predict_batch(const kadd_cnn_model_t *model,
                                 const float *inputs,
                                 int sample_count,
                                 float *positive_probabilities,
                                 int batch_size)
{
    if (!model || !inputs || !positive_probabilities) {
        return KADD_ERROR_NULL_PTR;
    }
    if (sample_count <= 0 ||
        sample_count > KADD_CNN_MAX_SAMPLE_COUNT) {
        return KADD_ERROR_INVALID_PARAM;
    }
    int effective_batch = (batch_size <= 0 || batch_size > KADD_CNN_MAX_BATCH_SIZE)
                          ? KADD_CNN_DEFAULT_BATCH_SIZE : batch_size;

    for (int begin = 0;
         begin < sample_count;
         begin += effective_batch) {
        const int count =
            begin + effective_batch < sample_count
                ? effective_batch
                : sample_count - begin;
        for (int row = 0; row < count; ++row) {
            const int result =
                kadd_cnn_model_predict(
                    model,
                    inputs +
                        (size_t)(begin + row) *
                            KADD_CNN_SEQUENCE_LENGTH,
                    positive_probabilities + begin + row);
            if (result != KADD_SUCCESS) {
                return result;
            }
        }
    }
    return KADD_SUCCESS;
}

int kadd_cnn_model_get_feature_dim(const kadd_cnn_model_t *model)
{
    if (!model) {
        return KADD_ERROR_NULL_PTR;
    }
    return KADD_CNN_SEQUENCE_LENGTH;
}

void kadd_cnn_model_free(kadd_cnn_model_t *model)
{
    if (!model) {
        return;
    }
    free(model->token_contributions);
    free(model->fc1_weights_input_major);
    free(model);
}
