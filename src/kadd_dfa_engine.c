#include "kadd_dfa_engine.h"

#include <stdio.h>
#include <stdlib.h>

#define KADD_DFA_ID_ERROR 65535u
#define KADD_DFA_ID_ENTRY 0u

#define OUTPUT_ACCEPT_MASK 0x1Fu
#define OUTPUT_EMIT_SHIFT 5u
#define OUTPUT_TOKEN_MASK 0x1Fu
#define OUTPUT_SCAN_DELTA_SHIFT 10u
#define OUTPUT_SCAN_DELTA_MASK 0x03u
#define OUTPUT_START_DELTA_SHIFT 12u
#define OUTPUT_START_DELTA_MASK 0x03u

#define EOD_ACTION_EMIT 1u
#define EOD_ACTION_CLEAR 2u
#define EOD_ACTION_MASK 0x03u
#define EOD_TOKEN_SHIFT 2u
#define EOD_TOKEN_MASK 0x3Fu

struct kadd_dfa_engine_t {
    uint16_t *transition_table;
    uint16_t *output_table;
    uint16_t *eod_table;
    size_t max_input_length;
    size_t max_output_dim;
};

static uint32_t read_le32(const unsigned char bytes[4]) {
    return ((uint32_t)bytes[0]) |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static uint16_t read_le16(const unsigned char bytes[2]) {
    return (uint16_t)(((uint16_t)bytes[0]) | ((uint16_t)bytes[1] << 8));
}

static int load_u16_table(
    const char *path,
    uint32_t expected_states,
    size_t cells_per_state,
    uint32_t *num_states,
    uint16_t **out
) {
    FILE *fp;
    unsigned char header[4];
    uint32_t states;
    size_t total_cells;
    uint16_t *table;
    size_t i;

    if (path == NULL || num_states == NULL || out == NULL || cells_per_state == 0) {
        return KADD_DFA_ERROR_INVALID_INPUT;
    }

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return KADD_DFA_ERROR_FILE_NOT_FOUND;
    }

    if (fread(header, 1, sizeof(header), fp) != sizeof(header)) {
        fclose(fp);
        return KADD_DFA_ERROR_PARSE;
    }

    states = read_le32(header);
    if (states == 0 || (expected_states != 0 && states != expected_states)) {
        fclose(fp);
        return KADD_DFA_ERROR_PARSE;
    }

    total_cells = (size_t)states * cells_per_state;
    if (total_cells / cells_per_state != states) {
        fclose(fp);
        return KADD_DFA_ERROR_PARSE;
    }

    table = (uint16_t *)malloc(total_cells * sizeof(uint16_t));
    if (table == NULL) {
        fclose(fp);
        return KADD_DFA_ERROR_MEMORY;
    }

    for (i = 0; i < total_cells; i++) {
        unsigned char cell[2];
        if (fread(cell, 1, sizeof(cell), fp) != sizeof(cell)) {
            free(table);
            fclose(fp);
            return KADD_DFA_ERROR_PARSE;
        }
        table[i] = read_le16(cell);
    }

    fclose(fp);
    *num_states = states;
    *out = table;
    return KADD_DFA_OK;
}

static int kadd_dfa_engine_load_tables(kadd_dfa_engine_t *e, const kadd_dfa_table_paths_t *table_paths) {
    uint32_t states = 0;
    int rc;

    rc = load_u16_table(table_paths->transition_table_path, 0, 256, &states, &e->transition_table);
    if (rc != KADD_DFA_OK) {
        return rc;
    }

    rc = load_u16_table(table_paths->output_table_path, states, 256, &states, &e->output_table);
    if (rc != KADD_DFA_OK) {
        return rc;
    }

    return load_u16_table(table_paths->eod_table_path, states, 1, &states, &e->eod_table);
}

int kadd_dfa_engine_create(kadd_dfa_engine_t **engine,
                           const kadd_dfa_table_paths_t *table_paths,
                           size_t max_input_length,
                           size_t max_output_dim) {
    kadd_dfa_engine_t *e;
    int rc;

    if (engine == NULL || table_paths == NULL) {
        return KADD_ERROR_NULL_PTR;
    }

    if (table_paths->transition_table_path == NULL ||
        table_paths->output_table_path == NULL ||
        table_paths->eod_table_path == NULL ||
        table_paths->transition_table_path[0] == '\0' ||
        table_paths->output_table_path[0] == '\0' ||
        table_paths->eod_table_path[0] == '\0' ||
        max_input_length == 0 || max_output_dim == 0) {
        return KADD_DFA_ERROR_INVALID_INPUT;
    }

    *engine = NULL;
    e = (kadd_dfa_engine_t *)calloc(1, sizeof(kadd_dfa_engine_t));
    if (e == NULL) {
        return KADD_DFA_ERROR_MEMORY;
    }

    e->max_input_length = max_input_length;
    e->max_output_dim = max_output_dim;

    rc = kadd_dfa_engine_load_tables(e, table_paths);
    if (rc != KADD_DFA_OK) {
        kadd_dfa_engine_free(e);
        return rc;
    }

    *engine = e;
    return KADD_DFA_OK;
}

void kadd_dfa_engine_free(kadd_dfa_engine_t *e) {
    if (e == NULL) {
        return;
    }

    free(e->transition_table);
    free(e->output_table);
    free(e->eod_table);
    free(e);
}

int kadd_dfa_engine_tokenize(const kadd_dfa_engine_t *e,
                             const char *input,
                             size_t input_length,
                             int *output,
                             int *dim) {
    size_t pos = 0;
    size_t count = 0;
    const uint16_t *transition_table;
    const uint16_t *output_table;
    const uint16_t *eod_table;

    if (e == NULL || input == NULL || output == NULL || dim == NULL) {
        return KADD_ERROR_NULL_PTR;
    }

    if (e->transition_table == NULL || e->output_table == NULL || e->eod_table == NULL) {
        return KADD_DFA_ERROR_INVALID_INPUT;
    }

    if (input_length > e->max_input_length) {
        return KADD_DFA_ERROR_INVALID_LENGTH;
    }

    transition_table = e->transition_table;
    output_table = e->output_table;
    eod_table = e->eod_table;

    while (pos < input_length) {
        uint16_t state_id = KADD_DFA_ID_ENTRY;
        uint16_t accept_info = 0;
        size_t token_start_pos;
        const uint16_t *trans_row;
        const uint16_t *out_row;

        while (pos < input_length) {
            uint8_t code = (uint8_t)input[pos];
            if (code > 32u && code != 127u) {
                break;
            }
            pos++;
        }
        if (pos >= input_length) {
            break;
        }

        token_start_pos = pos;
        trans_row = transition_table;
        out_row = output_table;

        while (1) {
            if (pos < input_length) {
                uint8_t byte_code = (uint8_t)input[pos];
                uint16_t candidate_state = trans_row[byte_code];
                uint16_t output_code;
                uint16_t edge_accept_info;
                uint16_t emit_token_id;

                if (candidate_state == KADD_DFA_ID_ERROR) {
                    break;
                }

                output_code = out_row[byte_code];
                edge_accept_info = (uint16_t)(output_code & OUTPUT_ACCEPT_MASK);
                emit_token_id = (uint16_t)((output_code >> OUTPUT_EMIT_SHIFT) & OUTPUT_TOKEN_MASK);

                if (emit_token_id != 0) {
                    uint16_t scan_delta =
                        (uint16_t)((output_code >> OUTPUT_SCAN_DELTA_SHIFT) & OUTPUT_SCAN_DELTA_MASK);
                    int start_delta =
                        (int)((output_code >> OUTPUT_START_DELTA_SHIFT) & OUTPUT_START_DELTA_MASK) - 1;
                    size_t scan_pos = pos + (size_t)scan_delta;

                    if (scan_pos < pos || scan_pos > input_length) {
                        return KADD_DFA_ERROR_PARSE;
                    }
                    if (count >= e->max_output_dim) {
                        return KADD_DFA_ERROR_BUFFER_OVERFLOW;
                    }
                    output[count++] = (int)emit_token_id;

                    if (start_delta < 0) {
                        if (pos == 0) {
                            return KADD_DFA_ERROR_PARSE;
                        }
                        token_start_pos = pos - 1u;
                    } else {
                        token_start_pos = pos + (size_t)start_delta;
                        if (token_start_pos < pos || token_start_pos > input_length) {
                            return KADD_DFA_ERROR_PARSE;
                        }
                    }

                    state_id = candidate_state;
                    accept_info = edge_accept_info;
                    pos = scan_pos;
                    trans_row = transition_table + (size_t)state_id * 256u;
                    out_row = output_table + (size_t)state_id * 256u;
                    continue;
                }

                state_id = candidate_state;
                if (edge_accept_info != 0) {
                    accept_info = edge_accept_info;
                }
                pos++;
                trans_row = transition_table + (size_t)state_id * 256u;
                out_row = output_table + (size_t)state_id * 256u;
                continue;
            }

            {
                uint16_t eod_code = eod_table[state_id];
                uint16_t action = (uint16_t)(eod_code & EOD_ACTION_MASK);
                if (action == EOD_ACTION_CLEAR) {
                    accept_info = 0;
                } else if (action == EOD_ACTION_EMIT) {
                    accept_info = (uint16_t)((eod_code >> EOD_TOKEN_SHIFT) & EOD_TOKEN_MASK);
                }
            }
            break;
        }

        if (pos > token_start_pos && accept_info != 0) {
            if (count >= e->max_output_dim) {
                return KADD_DFA_ERROR_BUFFER_OVERFLOW;
            }
            output[count++] = (int)accept_info;
        } else if (pos < input_length) {
            pos++;
        }
    }

    *dim = (int)count;
    return KADD_DFA_OK;
}

int kadd_dfa_engine_tokenize_batch(const kadd_dfa_engine_t *e,
                                   kadd_dfa_batch_t *batch) {
    if (e == NULL || batch == NULL) {
        return KADD_ERROR_NULL_PTR;
    }

    if (batch->inputs == NULL || batch->outputs == NULL || batch->dims == NULL) {
        return KADD_ERROR_NULL_PTR;
    }

    if (batch->count <= 0) {
        return KADD_ERROR_INVALID_INPUT;
    }

    // 设置默认批次大小
    int actual_batch_size = batch->batch_size;
    if (actual_batch_size <= 0) {
        actual_batch_size = 64;
    }
    if (actual_batch_size > 1024) {
        actual_batch_size = 1024;
    }

    // 初始化输出
    for (int i = 0; i < batch->count; i++) {
        batch->outputs[i] = NULL;
        batch->dims[i] = -1;
    }

    int success_count = 0;

    // 分批处理
    for (int batch_start = 0; batch_start < batch->count; batch_start += actual_batch_size) {
        int current_batch = (batch_start + actual_batch_size < batch->count) ?
                           actual_batch_size : (batch->count - batch_start);

        // 处理当前批次
        for (int i = 0; i < current_batch; i++) {
            int idx = batch_start + i;
            const char *input = batch->inputs[idx];
            size_t input_len = (batch->lengths != NULL) ? batch->lengths[idx] : 0;

            if (input == NULL) {
                continue;
            }

            // 如果没有提供长度，自动计算
            if (batch->lengths == NULL) {
                input_len = strlen(input);
            }

            // 分配输出缓冲区
            int *output = (int *)malloc(e->max_output_dim * sizeof(int));
            if (output == NULL) {
                continue;
            }

            // 调用单条 tokenize
            int dim = 0;
            int result = kadd_dfa_engine_tokenize(e, input, input_len, output, &dim);

            if (result == KADD_DFA_OK && dim > 0) {
                batch->outputs[idx] = output;
                batch->dims[idx] = dim;
                success_count++;
            } else {
                free(output);
                batch->dims[idx] = -1;
            }
        }
    }

    // 返回成功数量（与 KADD_SUCCESS 区分）
    return (success_count == batch->count) ? KADD_SUCCESS : success_count;
}
