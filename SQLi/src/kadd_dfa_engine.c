/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 */
#include "kadd_dfa_engine.h"
#include "kadd_errors.h"
#include "kadd_config_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KADD_DFA_ID_ERROR 65535u
#define KADD_DFA_ID_ENTRY 0u

#define OUTPUT_ACCEPT_MASK 0x1Fu
#define OUTPUT_EMIT_SHIFT 5u
#define OUTPUT_TOKEN_MASK 0x1Fu
#define OUTPUT_SCAN_DELTA_SHIFT 10u
#define OUTPUT_SCAN_DELTA_MASK 0x03u
#define OUTPUT_START_DELTA_SHIFT 12u
#define OUTPUT_START_DELTA_MASK 0x03u
#define OUTPUT_END_DELTA_SHIFT 14u
#define OUTPUT_END_DELTA_MASK 0x03u
#define OUTPUT_EMIT_PAIR_FLAG 3u

#define EOD_ACTION_EMIT 1u
#define EOD_ACTION_CLEAR 2u
#define EOD_ACTION_EMIT_TWO 3u
#define EOD_ACTION_MASK 0x03u
#define EOD_TOKEN_SHIFT 2u
#define EOD_TOKEN_MASK 0x3Fu
#define EOD_TOKEN2_SHIFT 8u

#define KADD_DFA_BYTE_TABLE_SIZE 256u
#define KADD_DFA_EOD_CEILS_PER_STATE 1
#define KADD_DFA_BATCH_CLEANUP_ALL (-1)
#define KADD_DFA_WHITESPACE_MAX 32u
#define KADD_DFA_DEL_CODE 127u
#define KADD_LE_BYTE_SHIFT_1 8
#define KADD_LE_BYTE_SHIFT_2 16
#define KADD_LE_BYTE_SHIFT_3 24
#define KADD_LE_BYTE0_IDX 0
#define KADD_LE_BYTE1_IDX 1
#define KADD_LE_BYTE2_IDX 2
#define KADD_LE_BYTE3_IDX 3

struct kadd_dfa_engine_t {
    uint16_t *transition_table;
    uint16_t *output_table;
    uint16_t *eod_table;
    size_t max_input_chars;   /* 最大输入SQL字符数 */
    size_t max_tokens;        /* 最大token输出数量 */
};

/* ========== 内部辅助函数 ========== */

/**
 * @brief 清理批处理输出缓冲区
 */
// LCOV_EXCL_START: cleanup helper, covered by test via direct include
static void cleanup_dfa_batch(kadd_dfa_batch_t *batch, int up_to_idx)
{
    if (!batch || !batch->outputs) {
        return;
    }

    int limit = (up_to_idx < 0) ? batch->count : up_to_idx;
    for (int i = 0; i < limit; i++) {
        if (batch->outputs[i]) {
            free(batch->outputs[i]);
            batch->outputs[i] = NULL;
        }
    }
}
// LCOV_EXCL_STOP

static uint32_t read_le32(const unsigned char bytes[4])
{
    return ((uint32_t)bytes[KADD_LE_BYTE0_IDX]) |
           ((uint32_t)bytes[KADD_LE_BYTE1_IDX] << KADD_LE_BYTE_SHIFT_1) |
           ((uint32_t)bytes[KADD_LE_BYTE2_IDX] << KADD_LE_BYTE_SHIFT_2) |
           ((uint32_t)bytes[KADD_LE_BYTE3_IDX] << KADD_LE_BYTE_SHIFT_3);
}

static uint16_t read_le16(const unsigned char bytes[2])
{
    return (uint16_t)(((uint16_t)bytes[KADD_LE_BYTE0_IDX]) |
                      ((uint16_t)bytes[KADD_LE_BYTE1_IDX] << KADD_LE_BYTE_SHIFT_1));
}

static int validate_table_header(const char *path, uint32_t expected_states,
                                 FILE **fp_out, uint32_t *states_out)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        fprintf(stderr, "[kadd_dfa] ERROR: Failed to open table file: %s\n", path);
        return KADD_DFA_ERROR_FILE_NOT_FOUND;
    }

    unsigned char header[4];
    if (fread(header, 1, sizeof(header), fp) != sizeof(header)) {
        fprintf(stderr, "[kadd_dfa] ERROR: Failed to read table header from: %s\n", path);
        fclose(fp);
        return KADD_DFA_ERROR_PARSE;
    }

    uint32_t states = read_le32(header);
    if (states == 0 || (expected_states != 0 && states != expected_states)) {
        fprintf(stderr,
                "[kadd_dfa] ERROR: Invalid state count %u (expected %u) in: %s\n",
                states, expected_states, path);
        fclose(fp);
        return KADD_DFA_ERROR_PARSE;
    }

    *fp_out = fp;
    *states_out = states;
    return KADD_SUCCESS;
}

static int load_u16_table(const char *path, uint32_t expected_states, size_t cells_per_state, uint32_t *num_states,
                          uint16_t **out)
{
    if (path == NULL || num_states == NULL || out == NULL || cells_per_state == 0) {
        fprintf(stderr, "[kadd_dfa] ERROR: Invalid parameters for load_u16_table\n");
        return KADD_ERROR_INVALID_INPUT;
    }

    FILE *fp = NULL;
    uint32_t states = 0;
    int rc = validate_table_header(path, expected_states, &fp, &states);
    if (rc != KADD_SUCCESS) {
        return rc;
    }

    size_t total_cells = (size_t)states * cells_per_state;
    if (total_cells / cells_per_state != states) {
        fprintf(stderr,
                "[kadd_dfa] ERROR: Table size overflow (states=%u, cells/state=%zu) in: %s\n",
                states, cells_per_state, path);
        fclose(fp);
        return KADD_DFA_ERROR_PARSE;
    }

    uint16_t *table = (uint16_t *)calloc(total_cells, sizeof(uint16_t));
    if (table == NULL) {
        fprintf(stderr, "[kadd_dfa] ERROR: Failed to allocate table memory (%zu cells) for: %s\n", total_cells, path);
        fclose(fp);
        return KADD_ERROR_OUT_OF_MEMORY;
    }

    for (size_t i = 0; i < total_cells; i++) {
        unsigned char cell[2];
        if (fread(cell, 1, sizeof(cell), fp) != sizeof(cell)) {
            fprintf(stderr, "[kadd_dfa] ERROR: Incomplete table data at cell %zu in: %s\n", i, path);
            free(table);
            fclose(fp);
            return KADD_DFA_ERROR_PARSE;
        }
        table[i] = read_le16(cell);
    }

    if (fclose(fp) != 0) {
        fprintf(stderr, "[kadd_dfa] ERROR: Failed to close table file: %s\n", path);
        free(table);
        return KADD_DFA_ERROR_PARSE;
    }

    *num_states = states;
    *out = table;
    return KADD_SUCCESS;
}

static int kadd_dfa_engine_load_tables(kadd_dfa_engine_t *engine, const kadd_dfa_table_paths_t *table_paths)
{
    uint32_t states = 0;
    int rc;

    rc = load_u16_table(table_paths->transition_table_path, 0,
                        KADD_DFA_BYTE_TABLE_SIZE, &states, &engine->transition_table);
    if (rc != KADD_SUCCESS) {
        return rc;
    }

    rc = load_u16_table(table_paths->output_table_path, states,
                        KADD_DFA_BYTE_TABLE_SIZE, &states, &engine->output_table);
    if (rc != KADD_SUCCESS) {
        return rc;
    }

    return load_u16_table(table_paths->eod_table_path, states,
                          KADD_DFA_EOD_CEILS_PER_STATE, &states, &engine->eod_table);
}

int kadd_dfa_engine_create(kadd_dfa_engine_t **engine, const kadd_dfa_table_paths_t *table_paths,
                           size_t max_input_chars, size_t max_tokens)
{
    // 空指针检查
    if (engine == NULL || table_paths == NULL) {
        fprintf(stderr, "[kadd_dfa] ERROR: NULL pointer (engine=%s, table_paths=%s)\n",
                (engine ? "valid" : "NULL"), (table_paths ? "valid" : "NULL"));
        return KADD_ERROR_NULL_PTR;
    }

    // 路径指针和空路径检查
    if (table_paths->transition_table_path == NULL || table_paths->output_table_path == NULL ||
        table_paths->eod_table_path == NULL || table_paths->transition_table_path[0] == '\0' ||
        table_paths->output_table_path[0] == '\0' || table_paths->eod_table_path[0] == '\0' ||
        max_input_chars == 0 || max_tokens == 0) {
        fprintf(stderr, "[kadd_dfa] ERROR: Invalid table paths or parameters (max_input_chars=%zu, max_tokens=%zu)\n",
                max_input_chars, max_tokens);
        return KADD_ERROR_INVALID_INPUT;
    }

    *engine = NULL;
    kadd_dfa_engine_t *eng = NULL;
    eng = (kadd_dfa_engine_t *)calloc(1, sizeof(kadd_dfa_engine_t));
    if (eng == NULL) {
        fprintf(stderr, "[kadd_dfa] ERROR: Failed to allocate DFA engine\n");
        return KADD_ERROR_OUT_OF_MEMORY;
    }

    eng->max_input_chars = max_input_chars;
    eng->max_tokens = max_tokens;

    int rc = kadd_dfa_engine_load_tables(eng, table_paths);
    if (rc != KADD_SUCCESS) {
        kadd_dfa_engine_free(eng);
        return rc;
    }

    *engine = eng;
    return KADD_SUCCESS;
}

void kadd_dfa_engine_free(kadd_dfa_engine_t *engine)
{
    if (engine == NULL) {
        return;
    }

    free(engine->transition_table);
    engine->transition_table = NULL;
    free(engine->output_table);
    engine->output_table = NULL;
    free(engine->eod_table);
    engine->eod_table = NULL;
    free(engine);
    engine = NULL;
}

static int kadd_dfa_emit_token(size_t max_output_dim, size_t input_length, size_t *pos, size_t *token_start_pos,
                               uint16_t *accept_info, int *output, size_t *count, uint16_t candidate_state,
                               uint16_t output_code, const uint16_t **trans_row, const uint16_t **out_row,
                               const uint16_t *transition_table, const uint16_t *output_table)
{
    uint16_t emit_token_id = (uint16_t)((output_code >> OUTPUT_EMIT_SHIFT) & OUTPUT_TOKEN_MASK);
    uint16_t edge_accept_info = (uint16_t)(output_code & OUTPUT_ACCEPT_MASK);
    uint16_t scan_delta = (uint16_t)((output_code >> OUTPUT_SCAN_DELTA_SHIFT) & OUTPUT_SCAN_DELTA_MASK);
    int start_delta = (int)((output_code >> OUTPUT_START_DELTA_SHIFT) & OUTPUT_START_DELTA_MASK) - 1;
    uint16_t end_delta = (uint16_t)((output_code >> OUTPUT_END_DELTA_SHIFT) & OUTPUT_END_DELTA_MASK);
    size_t scan_pos = *pos + (size_t)scan_delta;

    if (scan_pos < *pos || scan_pos > input_length) {
        return KADD_DFA_ERROR_PARSE;
    }
    if (*count >= max_output_dim) {
        return KADD_DFA_ERROR_BUFFER_OVERFLOW;
    }
    output[(*count)++] = (int)emit_token_id;
    if (end_delta == OUTPUT_EMIT_PAIR_FLAG && edge_accept_info != 0) {
        output[(*count)++] = (int)edge_accept_info;
        edge_accept_info = 0;
    }

    if (start_delta < 0) {
        if (*pos == 0) {
            return KADD_DFA_ERROR_PARSE;
        }
        *token_start_pos = *pos - 1u;
    } else {
        *token_start_pos = *pos + (size_t)start_delta;
        if (*token_start_pos < *pos || *token_start_pos > input_length) {
            return KADD_DFA_ERROR_PARSE;
        }
    }

    *accept_info = edge_accept_info;
    *pos = scan_pos;
    *trans_row = transition_table + (size_t)candidate_state * KADD_DFA_BYTE_TABLE_SIZE;
    *out_row = output_table + (size_t)candidate_state * KADD_DFA_BYTE_TABLE_SIZE;

    return KADD_SUCCESS;
}

static int kadd_dfa_handle_eod(const uint16_t *eod_table, uint16_t state_id, size_t max_output_dim,
                               size_t pos, size_t *token_start_pos, uint16_t *accept_info,
                               int *output, size_t *count)
{
    uint16_t eod_code = eod_table[state_id];
    uint16_t action = (uint16_t)(eod_code & EOD_ACTION_MASK);

    if (action == EOD_ACTION_CLEAR) {
        *accept_info = 0;
    } else if (action == EOD_ACTION_EMIT) {
        *accept_info = (uint16_t)((eod_code >> EOD_TOKEN_SHIFT) & EOD_TOKEN_MASK);
    } else if (action == EOD_ACTION_EMIT_TWO) {
        if (*count + 2u > max_output_dim) {
            return KADD_DFA_ERROR_BUFFER_OVERFLOW;
        }
        uint16_t first_token = (uint16_t)((eod_code >> EOD_TOKEN_SHIFT) & EOD_TOKEN_MASK);
        uint16_t second_token = (uint16_t)((eod_code >> EOD_TOKEN2_SHIFT) & EOD_TOKEN_MASK);
        output[(*count)++] = (int)first_token;
        output[(*count)++] = (int)second_token;
        *accept_info = 0;
        *token_start_pos = pos;
    }
    return KADD_SUCCESS;
}

static int kadd_dfa_engine_match_next(const uint16_t *transition_table, const uint16_t *output_table,
                                      const uint16_t *eod_table, size_t max_output_dim, const char *input,
                                      size_t input_length, size_t *pos, size_t *token_start_pos, uint16_t *accept_info,
                                      int *output, size_t *count)
{
    uint16_t state_id = KADD_DFA_ID_ENTRY;
    const uint16_t *trans_row = transition_table;
    const uint16_t *out_row = output_table;

    while (1) {
        uint8_t byte_code = (*pos < input_length) ? (uint8_t)input[*pos] : 0;
        uint16_t candidate_state = (*pos < input_length) ? trans_row[byte_code] : KADD_DFA_ID_ERROR;

        if (*pos >= input_length) {
            int rc = kadd_dfa_handle_eod(eod_table, state_id, max_output_dim, *pos,
                                         token_start_pos, accept_info, output, count);
            if (rc != KADD_SUCCESS) {
                return rc;
            }
            break;
        }

        if (candidate_state == KADD_DFA_ID_ERROR) {
            break;
        }

        uint16_t output_code = out_row[byte_code];
        uint16_t edge_accept_info = (uint16_t)(output_code & OUTPUT_ACCEPT_MASK);
        uint16_t emit_token_id = (uint16_t)((output_code >> OUTPUT_EMIT_SHIFT) & OUTPUT_TOKEN_MASK);

        if (emit_token_id != 0) {
            int rc = kadd_dfa_emit_token(max_output_dim, input_length, pos, token_start_pos, accept_info, output,
                                         count, candidate_state, output_code, &trans_row, &out_row,
                                         transition_table, output_table);
            state_id = candidate_state;
            if (rc != KADD_SUCCESS) {
                return rc;
            }
            continue;
        }

        state_id = candidate_state;
        if (edge_accept_info != 0) {
            *accept_info = edge_accept_info;
        }
        (*pos)++;
        trans_row = transition_table + (size_t)state_id * KADD_DFA_BYTE_TABLE_SIZE;
        out_row = output_table + (size_t)state_id * KADD_DFA_BYTE_TABLE_SIZE;
        continue;
    }

    return KADD_SUCCESS;
}

static int kadd_dfa_validate_input(const kadd_dfa_engine_t *engine, const char *input, size_t input_length,
                                   int *output, int *token_count)
{
    if (engine == NULL || input == NULL || output == NULL || token_count == NULL) {
        fprintf(stderr, "[kadd_dfa] ERROR: NULL pointer in validate_input\n");
        return KADD_ERROR_NULL_PTR;
    }

    if (engine->transition_table == NULL || engine->output_table == NULL || engine->eod_table == NULL) {
        fprintf(stderr, "[kadd_dfa] ERROR: Engine tables not initialized\n");
        return KADD_ERROR_NOT_INIT;
    }

    if (input_length > engine->max_input_chars) {
        fprintf(stderr, "[kadd_dfa] ERROR: Input length %zu exceeds max %zu\n", input_length, engine->max_input_chars);
        return KADD_DFA_ERROR_INVALID_LENGTH;
    }
    return KADD_SUCCESS;
}

int kadd_dfa_engine_tokenize(const kadd_dfa_engine_t *engine, const char *input,
                             size_t input_length, int *output, int *token_count)
{
    size_t pos = 0;
    size_t count = 0;

    int rc = kadd_dfa_validate_input(engine, input, input_length, output, token_count);
    if (rc != KADD_SUCCESS) {
        return rc;
    }

    while (pos < input_length) {
        uint16_t accept_info = 0;
        size_t token_start_pos;

        while (pos < input_length) {
            uint8_t code = (uint8_t)input[pos];
            if (code > KADD_DFA_WHITESPACE_MAX && code != KADD_DFA_DEL_CODE) {
                break;
            }
            pos++;
        }
        if (pos >= input_length) {
            break;
        }

        token_start_pos = pos;
        rc = kadd_dfa_engine_match_next(engine->transition_table,
                                        engine->output_table, engine->eod_table,
                                        engine->max_tokens, input,
                                        input_length, &pos, &token_start_pos,
                                        &accept_info, output, &count);
        if (rc != KADD_SUCCESS) {
            return rc;
        }

        if (pos > token_start_pos && accept_info != 0) {
            if (count >= engine->max_tokens) {
                return KADD_DFA_ERROR_BUFFER_OVERFLOW;
            }
            output[count++] = (int)accept_info;
        } else if (pos < input_length) {
            pos++;
        }
    }

    *token_count = (int)count;
    return KADD_SUCCESS;
}

static int kadd_dfa_tokenize_batch_item(const kadd_dfa_engine_t *engine, kadd_dfa_batch_t *batch, int idx)
{
    const char *input = batch->inputs[idx];
    if (input == NULL) {
        fprintf(stderr, "[kadd_dfa] ERROR: NULL input at batch index %d\n", idx);
        cleanup_dfa_batch(batch, KADD_DFA_BATCH_CLEANUP_ALL);
        return KADD_ERROR_NULL_PTR;
    }
    size_t input_len = (batch->lengths != NULL) ? batch->lengths[idx] : strlen(input);
    // 分配输出缓冲区
    int *output = (int *)calloc(engine->max_tokens, sizeof(int));
    if (output == NULL) {
        fprintf(stderr, "[kadd_dfa] ERROR: Failed to allocate output buffer at batch index %d\n", idx);
        cleanup_dfa_batch(batch, KADD_DFA_BATCH_CLEANUP_ALL);
        return KADD_ERROR_OUT_OF_MEMORY;
    }
    // 调用单条 tokenize
    int token_count = 0;
    int result = kadd_dfa_engine_tokenize(engine, input, input_len, output, &token_count);
    if (result == KADD_SUCCESS && token_count > 0) {
        batch->outputs[idx] = output;
        batch->dims[idx] = token_count;
    } else {
        fprintf(stderr, "[kadd_dfa] ERROR: Tokenize failed at batch index %d (result=%d, token_count=%d)\n",
                idx, result, token_count);
        free(output);
        cleanup_dfa_batch(batch, KADD_DFA_BATCH_CLEANUP_ALL);
        return KADD_DFA_ERROR_PARSE;
    }
    return KADD_SUCCESS;
}

int kadd_dfa_engine_tokenize_batch(const kadd_dfa_engine_t *engine, kadd_dfa_batch_t *batch)
{
    if (engine == NULL || batch == NULL || batch->inputs == NULL || batch->outputs == NULL || batch->dims == NULL) {
        fprintf(stderr, "[kadd_dfa] ERROR: NULL pointer in tokenize_batch\n");
        return KADD_ERROR_NULL_PTR;
    }
    if (batch->count <= 0) {
        fprintf(stderr, "[kadd_dfa] ERROR: Invalid batch count: %d\n", batch->count);
        return KADD_ERROR_INVALID_INPUT;
    }
    if (batch->count > KADD_DFA_MAX_SAMPLE_COUNT) {
        fprintf(stderr,
                "[kadd_dfa] ERROR: Batch count %d exceeds limit %d\n",
                batch->count, KADD_DFA_MAX_SAMPLE_COUNT);
        return KADD_ERROR_INVALID_PARAM;
    }

    int actual_batch_size = batch->batch_size;
    if (actual_batch_size <= 0 || actual_batch_size > KADD_DFA_MAX_BATCH_SIZE) {
        actual_batch_size = KADD_DFA_DEFAULT_BATCH_SIZE;
    }

    for (int i = 0; i < batch->count; i++) {
        batch->outputs[i] = NULL;
        batch->dims[i] = -1;
    }

    for (int batch_start = 0; batch_start < batch->count; batch_start += actual_batch_size) {
        int current_batch = (batch_start + actual_batch_size < batch->count) ?
                           actual_batch_size : (batch->count - batch_start);
        for (int i = 0; i < current_batch; i++) {
            int rc = kadd_dfa_tokenize_batch_item(engine, batch, batch_start + i);
            if (rc != KADD_SUCCESS) {
                return rc;
            }
        }
    }

    return KADD_SUCCESS;
}
