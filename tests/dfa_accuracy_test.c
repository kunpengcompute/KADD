/**
 * @file dfa_accuracy_test.c
 * @brief DFA 引擎准确率测试 - 加载JSON数据，对比 expected vs actual tokens
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../include/kadd_dfa_engine.h"
#include "../include/kadd_config_loader.h"
#include "cJSON.h"

/* ========== 测试数据 ========== */

typedef struct {
    int line;
    char *uri;
    char *uri_decoded;
    int label;
    int *tokens;
    int token_count;
} TestItem;

typedef struct {
    TestItem *items;
    int count;
    int capacity;
} TestArray;

static void test_array_init(TestArray *a) {
    a->capacity = 8192;
    a->count = 0;
    a->items = malloc(sizeof(TestItem) * a->capacity);
}

static void test_array_push(TestArray *a, TestItem item) {
    if (a->count >= a->capacity) {
        a->capacity *= 2;
        a->items = realloc(a->items, sizeof(TestItem) * a->capacity);
    }
    a->items[a->count++] = item;
}

static void test_array_free(TestArray *a) {
    for (int i = 0; i < a->count; i++) {
        free(a->items[i].uri);
        free(a->items[i].uri_decoded);
        free(a->items[i].tokens);
    }
    free(a->items);
}

/* ========== 加载JSON ========== */

static int load_json(const char *path, TestArray *arr) {
    FILE *f = fopen(path, "r");
    if (!f) { printf("无法打开 %s\n", path); return 0; }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc(sz + 1);
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        printf("JSON解析失败\n");
        free(buf);
        return 0;
    }
    free(buf);

    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!data || !cJSON_IsArray(data)) data = (cJSON_IsArray(root)) ? root : NULL;
    if (!data) { cJSON_Delete(root); return 0; }

    int count = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, data) {
        cJSON *uri = cJSON_GetObjectItem(item, "uri");
        cJSON *decoded = cJSON_GetObjectItem(item, "uri_without_url_encode");
        cJSON *label = cJSON_GetObjectItem(item, "label");
        cJSON *line = cJSON_GetObjectItem(item, "line");
        cJSON *tokens = cJSON_GetObjectItem(item, "tokens_correct");

        if (!uri || !tokens) continue;

        TestItem ti;
        ti.line = line ? line->valueint : 0;
        ti.uri = strdup(uri->valuestring);
        ti.uri_decoded = (decoded && decoded->valuestring) ? strdup(decoded->valuestring) : NULL;
        ti.label = label ? label->valueint : 0;

        int tc = cJSON_GetArraySize(tokens);
        ti.tokens = malloc(sizeof(int) * tc);
        ti.token_count = 0;
        cJSON *tok;
        cJSON_ArrayForEach(tok, tokens) {
            if (cJSON_IsNumber(tok))
                ti.tokens[ti.token_count++] = tok->valueint;
        }

        test_array_push(arr, ti);
        count++;
    }

    cJSON_Delete(root);
    return count;
}

/* ========== DFA tokenize helper ========== */

static int *dfa_tokenize(const kadd_dfa_engine_t *engine, const char *s, int *out_count) {
    int buf[8192];
    int dim = 0;
    *out_count = 0;

    if (kadd_dfa_engine_tokenize(engine, s, strlen(s), buf, &dim) != 0 || dim <= 0)
        return NULL;

    int *result = malloc(sizeof(int) * dim);
    memcpy(result, buf, sizeof(int) * dim);
    *out_count = dim;
    return result;
}

/* ========== 统计结果 ========== */

typedef struct {
    int total;
    int correct;
    int total_tokens;
    int correct_tokens;
    int malicious;
    int malicious_correct;
} Stats;

/* ========== 运行测试 & 收集mismatch ========== */

static void run_test(const kadd_dfa_engine_t *engine, TestArray *arr,
                     Stats *stats, cJSON *mismatch_arr) {
    memset(stats, 0, sizeof(Stats));

    for (int i = 0; i < arr->count; i++) {
        TestItem *ti = &arr->items[i];
        const char *input = (ti->uri_decoded && strlen(ti->uri_decoded) > 0)
                            ? ti->uri_decoded : ti->uri;

        int actual_count = 0;
        int *actual = dfa_tokenize(engine, input, &actual_count);

        stats->total++;
        stats->total_tokens += ti->token_count;

        /* 比较 */
        int match = (actual_count == ti->token_count);
        if (match) {
            for (int j = 0; j < actual_count; j++) {
                if (actual[j] != ti->tokens[j]) { match = 0; break; }
            }
        }

        /* token级别统计 */
        int min_len = (actual_count < ti->token_count) ? actual_count : ti->token_count;
        int tok_correct = 0;
        for (int j = 0; j < min_len; j++) {
            if (actual[j] == ti->tokens[j]) tok_correct++;
        }

        if (match) {
            stats->correct++;
            stats->correct_tokens += ti->token_count;
            if (ti->label == 1) { stats->malicious++; stats->malicious_correct++; }
        } else {
            stats->correct_tokens += tok_correct;
            if (ti->label == 1) stats->malicious++;

            /* 添加到mismatch数组 */
            cJSON *obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(obj, "line", ti->line);
            cJSON_AddStringToObject(obj, "uri", input);
            cJSON_AddNumberToObject(obj, "label", ti->label);
            cJSON_AddNumberToObject(obj, "expected_count", ti->token_count);
            cJSON_AddNumberToObject(obj, "actual_count", actual_count);

            cJSON *exp = cJSON_CreateIntArray(ti->tokens, ti->token_count);
            cJSON_AddItemToObject(obj, "expected_tokens", exp);

            cJSON *act = cJSON_CreateIntArray(actual, actual_count);
            cJSON_AddItemToObject(obj, "actual_tokens", act);

            cJSON_AddItemToArray(mismatch_arr, obj);
        }

        free(actual);

        if ((i + 1) % 50000 == 0)
            printf("进度: %d/%d (%.1f%%)\n", i + 1, arr->count, (i+1)*100.0/arr->count);
    }
}

/* ========== 保存输出 ========== */

static void save_output(const char *path, Stats *s, cJSON *mismatches) {
    cJSON *root = cJSON_CreateObject();

    cJSON *summary = cJSON_CreateObject();
    cJSON_AddNumberToObject(summary, "total_tests", s->total);
    cJSON_AddNumberToObject(summary, "correct_tests", s->correct);
    cJSON_AddNumberToObject(summary, "error_tests", s->total - s->correct);
    cJSON_AddNumberToObject(summary, "sequence_accuracy",
        s->total > 0 ? (double)s->correct / s->total : 0.0);
    cJSON_AddNumberToObject(summary, "total_tokens", s->total_tokens);
    cJSON_AddNumberToObject(summary, "correct_tokens", s->correct_tokens);
    cJSON_AddNumberToObject(summary, "token_accuracy",
        s->total_tokens > 0 ? (double)s->correct_tokens / s->total_tokens : 0.0);
    cJSON_AddNumberToObject(summary, "malicious_samples", s->malicious);
    cJSON_AddNumberToObject(summary, "malicious_correct", s->malicious_correct);
    cJSON_AddNumberToObject(summary, "malicious_accuracy",
        s->malicious > 0 ? (double)s->malicious_correct / s->malicious : 0.0);

    cJSON_AddItemToObject(root, "summary", summary);
    cJSON_AddItemToObject(root, "mismatches", mismatches);

    char *json_str = cJSON_Print(root);
    FILE *f = fopen(path, "w");
    if (f) { fprintf(f, "%s", json_str); fclose(f); }
    free(json_str);
    cJSON_Delete(root);
}

/* ========== main ========== */

int main(void) {
    #ifdef _WIN32
    system("chcp 65001 > nul 2>&1");
    system("if not exist results mkdir results");
    #else
    system("mkdir -p results");
    #endif

    /* 加载测试数据 */
    TestArray arr;
    test_array_init(&arr);
    int loaded = load_json("assets/data/full_dfa_tokenize_results.json", &arr);
    printf("加载 %d 条测试数据\n", loaded);

    if (loaded == 0) { test_array_free(&arr); return 1; }

    /* 创建DFA引擎 */
    kadd_config_t config;
    if (kadd_config_load(NULL, &config) != 0) {
        printf("无法加载config.json\n");
        test_array_free(&arr);
        return 1;
    }

    kadd_dfa_table_paths_t paths = {
        config.dfa.transition_table_path,
        config.dfa.output_table_path,
        config.dfa.eod_table_path
    };

    kadd_dfa_engine_t *engine = NULL;
    if (kadd_dfa_engine_create(&engine, &paths,
                               config.dfa.max_input_length,
                               config.dfa.max_output_dim) != 0) {
        printf("无法创建DFA引擎\n");
        test_array_free(&arr);
        return 1;
    }

    /* 运行测试 */
    Stats stats;
    cJSON *mismatches = cJSON_CreateArray();
    clock_t t0 = clock();

    run_test(engine, &arr, &stats, mismatches);

    double elapsed = (double)(clock() - t0) / CLOCKS_PER_SEC;

    /* 保存结果 */
    save_output("results/dfa_accuracy_results.json", &stats, mismatches);

    /* 打印摘要 */
    printf("\n=== DFA准确率测试结果 ===\n");
    printf("总测试: %d | 正确: %d | 错误: %d\n", stats.total, stats.correct, stats.total - stats.correct);
    printf("序列准确率: %.4f | Token准确率: %.4f\n",
        stats.total > 0 ? (double)stats.correct / stats.total : 0.0,
        stats.total_tokens > 0 ? (double)stats.correct_tokens / stats.total_tokens : 0.0);
    printf("恶意样本: %d | 恶意正确: %d | 恶意准确率: %.4f\n",
        stats.malicious, stats.malicious_correct,
        stats.malicious > 0 ? (double)stats.malicious_correct / stats.malicious : 0.0);
    printf("耗时: %.2f秒 | 吞吐: %.0f条/秒\n",
        elapsed, elapsed > 0 ? stats.total / elapsed : 0.0);

    kadd_dfa_engine_free(engine);
    test_array_free(&arr);
    return 0;
}