/**
 * @file dfa_accuracy_test.c
 * @brief DFA 引擎准确率测试 - 测试 parsed_results.json 中所有数据，保存对比结果为JSON
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <stdint.h>
#include "../include/kadd_dfa_engine.h"

#include "cJSON.h"
#include "libinjection_sqli.h"

// JSON字符串转义函数
static char* json_escape_string(const char *str) {
    if (!str) return strdup("");

    size_t len = strlen(str);
    // 最多需要2倍空间（每个字符可能转义）
    char *escaped = malloc(len * 2 + 1);
    if (!escaped) return NULL;

    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = str[i];
        switch (c) {
            case '"':  escaped[j++] = '\\'; escaped[j++] = '"'; break;
            case '\\': escaped[j++] = '\\'; escaped[j++] = '\\'; break;
            case '\b': escaped[j++] = '\\'; escaped[j++] = 'b'; break;
            case '\f': escaped[j++] = '\\'; escaped[j++] = 'f'; break;
            case '\n': escaped[j++] = '\\'; escaped[j++] = 'n'; break;
            case '\r': escaped[j++] = '\\'; escaped[j++] = 'r'; break;
            case '\t': escaped[j++] = '\\'; escaped[j++] = 't'; break;
            default:
                // 控制字符转为 \uXXXX 格式
                if (c < 0x20) {
                    j += sprintf(escaped + j, "\\u%04x", c);
                } else {
                    escaped[j++] = c;
                }
                break;
        }
    }
    escaped[j] = '\0';
    return escaped;
}

// 兼容性别名
typedef kadd_dfa_engine_t dfa_engine_t;

// 兼容性函数包装器
static dfa_engine_t *dfa_engine_create_wrapper(const char *table_dir) {
    kadd_dfa_engine_t *engine = NULL;
    // 构建完整路径 - 直接使用绝对路径或更简单的相对路径
    char table_path[512];
    char state_path[512];

    // 尝试多个可能的路径
    const char *paths[] = {
        "C:/Users/60907/Desktop/test0/c/tools/profile_data/transition_table.bin",
        "../tools/profile_data/transition_table.bin",
        "../../tools/profile_data/transition_table.bin",
        NULL
    };

    const char *state_paths[] = {
        "C:/Users/60907/Desktop/test0/c/tools/profile_data/states_meta.json",
        "../tools/profile_data/states_meta.json",
        "../../tools/profile_data/states_meta.json",
        NULL
    };

    // 尝试每个路径组合
    for (int i = 0; paths[i]; i++) {
        FILE *f = fopen(paths[i], "rb");
        if (f) {
            fclose(f);
            // 找到有效的table路径，现在找对应的state路径
            for (int j = 0; state_paths[j]; j++) {
                FILE *sf = fopen(state_paths[j], "r");
                if (sf) {
                    fclose(sf);
                    // 两个文件都存在，使用这对路径
                    strncpy(table_path, paths[i], sizeof(table_path) - 1);
                    strncpy(state_path, state_paths[j], sizeof(state_path) - 1);

                    if (kadd_dfa_engine_create(&engine, table_path, state_path, 8192, 8192) == 0) {
                        return (dfa_engine_t *)engine;
                    }
                }
            }
        }
    }

    return NULL;
}

#define dfa_engine_create dfa_engine_create_wrapper

static void dfa_engine_destroy(dfa_engine_t *engine) {
    kadd_dfa_engine_free((kadd_dfa_engine_t *)engine);
}

static int *dfa_engine_tokenize_to_indices(const dfa_engine_t *engine, const char *s, size_t *count) {
    if (!engine || !s || !count) return NULL;

    size_t input_len = strlen(s);
    int temp_output[8192];
    int temp_dim = 0;

    if (kadd_dfa_engine_tokenize((const kadd_dfa_engine_t *)engine, s, input_len, temp_output, &temp_dim) != 0) {
        *count = 0;
        return NULL;
    }

    if (temp_dim <= 0) {
        *count = 0;
        return NULL;
    }

    int *result = malloc(temp_dim * sizeof(int));
    if (!result) {
        *count = 0;
        return NULL;
    }

    memcpy(result, temp_output, temp_dim * sizeof(int));
    *count = (size_t)temp_dim;
    return result;
}

// 测试数据结构
typedef struct {
    int line;
    char *uri;
    char *uri_without_url_encode;  // 解码后的URI
    int label;
    int *tokens_correct;  // libinjection 生成的正确 tokens
    int tokens_correct_count;
} TestData;

// 单个测试结果
typedef struct {
    int line;
    char *uri;
    int label;
    int *expected_tokens;
    int expected_count;
    int *actual_tokens;
    int actual_count;
    int is_match;
    double token_accuracy;
    int correct_tokens;
} TestResult;

// 准确率测试结果汇总
typedef struct {
    int total_tests;
    int correct_tests;
    int total_tokens;
    int correct_tokens;
    int error_count;
    int malicious_count;
    int malicious_correct;
} AccuracyResult;

// 动态数组
typedef struct {
    TestData *data;
    size_t size;
    size_t capacity;
} TestDataArray;

typedef struct {
    TestResult *results;
    size_t size;
    size_t capacity;
} TestResultArray;

// 初始化测试数据数组
void init_test_array(TestDataArray *array) {
    array->capacity = 10000;
    array->size = 0;
    array->data = (TestData *)malloc(sizeof(TestData) * array->capacity);
}

// 添加测试数据
void add_test_data(TestDataArray *array, TestData test) {
    if (array->size >= array->capacity) {
        array->capacity *= 2;
        array->data = (TestData *)realloc(array->data, sizeof(TestData) * array->capacity);
    }
    array->data[array->size++] = test;
}

// 释放测试数据数组
void free_test_array(TestDataArray *array) {
    for (size_t i = 0; i < array->size; i++) {
        if (array->data[i].uri) free(array->data[i].uri);
        if (array->data[i].uri_without_url_encode) free(array->data[i].uri_without_url_encode);
        if (array->data[i].tokens_correct) free(array->data[i].tokens_correct);
    }
    free(array->data);
    array->data = NULL;
    array->size = 0;
    array->capacity = 0;
}

// 初始化测试结果数组
void init_result_array(TestResultArray *array) {
    array->capacity = 10000;
    array->size = 0;
    array->results = (TestResult *)malloc(sizeof(TestResult) * array->capacity);
}

// 添加测试结果
void add_test_result(TestResultArray *array, TestResult result) {
    if (array->size >= array->capacity) {
        array->capacity *= 2;
        array->results = (TestResult *)realloc(array->results, sizeof(TestResult) * array->capacity);
    }
    array->results[array->size++] = result;
}

// 释放测试结果数组
void free_result_array(TestResultArray *array) {
    for (size_t i = 0; i < array->size; i++) {
        if (array->results[i].uri) free(array->results[i].uri);
        if (array->results[i].expected_tokens) free(array->results[i].expected_tokens);
        if (array->results[i].actual_tokens) free(array->results[i].actual_tokens);
    }
    
    free(array->results);
    array->results = NULL;
    array->size = 0;
    array->capacity = 0;
}

// 从JSON文件加载测试数据 - 使用cJSON库
int load_test_data_from_json(const char *filepath, TestDataArray *array) {
    FILE *file = fopen(filepath, "r");
    if (!file) {
        printf("警告: 无法打开文件 %s\n", filepath);
        return 0;
    }

    // 获取文件大小
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // 读取整个文件
    char *content = (char *)malloc(file_size + 1);
    size_t read_size = fread(content, 1, file_size, file);
    content[read_size] = '\0';
    fclose(file);

    printf("文件大小: %ld bytes (%.2f MB)\n", file_size, file_size / 1024.0 / 1024.0);
    printf("开始解析 JSON...\n");
    fflush(stdout);

    // 使用cJSON解析
    clock_t parse_start = clock();
    cJSON *root = cJSON_Parse(content);
    clock_t parse_end = clock();

    if (!root) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            printf("错误: JSON解析失败，位置: %s\n", error_ptr);
        } else {
            printf("错误: JSON解析失败\n");
        }
        free(content);
        return 0;
    }

    printf("JSON 解析完成，耗时: %.2f 秒\n", (double)(parse_end - parse_start) / CLOCKS_PER_SEC);
    fflush(stdout);

    // 获取data数组
    cJSON *data_array = cJSON_GetObjectItem(root, "data");
    if (!data_array || !cJSON_IsArray(data_array)) {
        // 如果没有data字段，可能整个根就是数组
        if (cJSON_IsArray(root)) {
            data_array = root;
        } else {
            printf("错误: 未找到data数组\n");
            cJSON_Delete(root);
            free(content);
            return 0;
        }
    }

    // 遍历数据数组
    int count = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, data_array) {
        // 获取各个字段
        cJSON *line_item = cJSON_GetObjectItem(item, "line");
        cJSON *uri_item = cJSON_GetObjectItem(item, "uri");
        cJSON *uri_decoded_item = cJSON_GetObjectItem(item, "uri_without_url_encode");
        cJSON *label_item = cJSON_GetObjectItem(item, "label");
        cJSON *tokens_item = cJSON_GetObjectItem(item, "tokens_correct");

        // 检查必需字段
        if (!uri_item || !tokens_item) {
            continue;
        }

        // 创建测试数据
        TestData test;
        test.line = line_item ? line_item->valueint : 0;
        test.uri = strdup(uri_item->valuestring);
        test.uri_without_url_encode = uri_decoded_item && uri_decoded_item->valuestring
                                      ? strdup(uri_decoded_item->valuestring) : NULL;
        test.label = label_item ? label_item->valueint : 0;

        // 解析tokens_correct数组 (使用libinjection生成的正确tokens)
        if (cJSON_IsArray(tokens_item)) {
            int token_count = cJSON_GetArraySize(tokens_item);
            test.tokens_correct = (int *)malloc(sizeof(int) * token_count);
            test.tokens_correct_count = 0;

            cJSON *token_item = NULL;
            cJSON_ArrayForEach(token_item, tokens_item) {
                if (cJSON_IsNumber(token_item)) {
                    test.tokens_correct[test.tokens_correct_count++] = token_item->valueint;
                }
            }
        } else {
            test.tokens_correct = NULL;
            test.tokens_correct_count = 0;
        }

        add_test_data(array, test);
        count++;

        if (count <= 5 || count % 10000 == 0) {
            printf("已加载 %d 条数据...\n", count);
            fflush(stdout);
        }
    }

    cJSON_Delete(root);
    free(content);

    printf("成功加载 %d 条测试数据\n", count);
    return count;
}

// 比较两个token数组
int compare_tokens(const int *actual, int actual_count,
                   const int *expected, int expected_count) {
    if (actual_count != expected_count) {
        return 0;
    }

    for (int i = 0; i < actual_count; i++) {
        if (actual[i] != expected[i]) {
            return 0;
        }
    }

    return 1;
}

// 计算token级别准确率
double calculate_token_accuracy(const int *actual, int actual_count,
                                const int *expected, int expected_count) {
    int min_len = actual_count < expected_count ? actual_count : expected_count;
    int correct = 0;

    for (int i = 0; i < min_len; i++) {
        if (actual[i] == expected[i]) {
            correct++;
        }
    }

    return expected_count > 0 ? (double)correct / expected_count : 0.0;
}

// URL解码函数 - 解析百分号编码（完整版）
// 例如：%20 -> 空格，%27 -> '，%28 -> (，%29 -> )
// 支持：大写和小写十六进制数字、加号解码为空格
static char *url_decode(const char *str) {
    if (!str) return NULL;

    size_t len = strlen(str);
    char *decoded = malloc(len + 1);
    if (!decoded) return NULL;

    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '+') {
            /* 加号解码为空格（application/x-www-form-urlencoded格式）*/
            decoded[j++] = ' ';
        } else if (str[i] == '%' && i + 2 < len &&
            isxdigit((unsigned char)str[i + 1]) && isxdigit((unsigned char)str[i + 2])) {
            /* 解码百分号编码 */
            char hex[3] = {str[i + 1], str[i + 2], 0};
            decoded[j++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else {
            decoded[j++] = str[i];
        }
    }
    decoded[j] = '\0';

    return decoded;
}

// 将libinjection的token类型映射到索引（与Python dfa_engine_copy.py一致）
static int libinjection_type_to_index(char type) {
    static const int8_t map[256] = {
        [0 ... 255] = 24,  // 默认 TYPE_UNKNOWN
        ['0'] = 0,     // TYPE_NONE
        ['k'] = 1,     // TYPE_KEYWORD
        ['U'] = 2,     // TYPE_UNION
        ['B'] = 3,     // TYPE_GROUP
        ['E'] = 4,     // TYPE_EXPRESSION
        ['t'] = 5,     // TYPE_SQLTYPE
        ['f'] = 6,     // TYPE_FUNCTION
        ['n'] = 7,     // TYPE_BAREWORD
        ['1'] = 8,     // TYPE_NUMBER
        ['v'] = 9,     // TYPE_VARIABLE
        ['s'] = 10,    // TYPE_STRING
        ['o'] = 11,    // TYPE_OPERATOR
        ['&'] = 12,    // TYPE_LOGIC_OPERATOR (独立类型)
        ['c'] = 13,    // TYPE_COMMENT
        ['A'] = 14,    // TYPE_COLLATE
        ['('] = 15,    // TYPE_LEFTPARENS (独立类型)
        [')'] = 16,    // TYPE_RIGHTPARENS (独立类型)
        ['{'] = 17,    // TYPE_LEFTBRACE (独立类型)
        ['}'] = 18,    // TYPE_RIGHTBRACE (独立类型)
        ['.'] = 19,    // TYPE_DOT
        [','] = 20,    // TYPE_COMMA (独立类型)
        [':'] = 21,    // TYPE_COLON (独立类型)
        [';'] = 22,    // TYPE_SEMICOLON
        ['T'] = 23,    // TYPE_STATEMENT
        ['?'] = 24,    // TYPE_UNKNOWN
        ['X'] = 25,    // TYPE_EVIL
        ['F'] = 26,    // TYPE_FINGERPRINT
        ['\\'] = 27,   // TYPE_BACKSLASH (独立类型)
    };

    if (type >= 0 && type < 256) {
        return map[(unsigned char)type];
    }
    return 24;  // TYPE_UNKNOWN
}

// 使用libinjection对字符串进行分词（作为期望结果）
static int *libinjection_tokenize_to_indices(const char *s, size_t *count) {
    if (!s || !count) return NULL;

    size_t input_len = strlen(s);
    int temp_output[8192];
    int temp_dim = 0;

    // 初始化libinjection状态
    // 使用MySQL模式以匹配生成的tokens_correct
    struct libinjection_sqli_state sf;
    libinjection_sqli_init(&sf, s, input_len, FLAG_QUOTE_NONE | FLAG_SQL_MYSQL);

    // 循环获取token
    while (libinjection_sqli_tokenize(&sf) && temp_dim < 8192) {
        char token_type = sf.current->type;
        temp_output[temp_dim++] = libinjection_type_to_index(token_type);
    }

    if (temp_dim <= 0) {
        *count = 0;
        return NULL;
    }

    int *result = malloc(temp_dim * sizeof(int));
    if (!result) {
        *count = 0;
        return NULL;
    }

    memcpy(result, temp_output, temp_dim * sizeof(int));
    *count = (size_t)temp_dim;
    return result;
}

// 运行准确率测试
void run_accuracy_test(dfa_engine_t *engine, TestDataArray *tests,
                      AccuracyResult *result, TestResultArray *results,
                      int verbose, int max_tests) {
    memset(result, 0, sizeof(AccuracyResult));

    int test_count = max_tests > 0 ? (max_tests < (int)tests->size ? max_tests : (int)tests->size) : (int)tests->size;

    printf("开始测试 %d 条数据...\n", test_count);
    printf("对比方式: Expected (libinjection) vs Actual (DFA)\n", test_count);

    for (int i = 0; i < test_count; i++) {
        TestData *test = &tests->data[i];

        // 使用 uri_without_url_encode 作为输入（如果存在），否则使用原始uri
        const char *input_uri = (test->uri_without_url_encode && strlen(test->uri_without_url_encode) > 0)
                               ? test->uri_without_url_encode
                               : test->uri;

        // 使用预存的tokens_correct作为期望的tokens（来自JSON文件）
        size_t expected_count = test->tokens_correct_count;
        int *expected_tokens = NULL;
        if (test->tokens_correct && expected_count > 0) {
            // 复制预存的tokens
            expected_tokens = (int *)malloc(sizeof(int) * expected_count);
            memcpy(expected_tokens, test->tokens_correct, sizeof(int) * expected_count);
        }

        // libinjection 生成的 tokens
        expected_count = 0;
        expected_tokens = libinjection_tokenize_to_indices(input_uri, &expected_count);

        // 使用 DFA 生成实际的 tokens
        size_t actual_count = 0;
        int *actual_tokens = dfa_engine_tokenize_to_indices(engine, input_uri, &actual_count);

        result->total_tests++;
        result->total_tokens += (int)expected_count;

        // 比较 libinjection (expected) 和 DFA (actual) 的结果
        int match = compare_tokens(actual_tokens, (int)actual_count, expected_tokens, (int)expected_count);

        // 创建测试结果
        TestResult test_result;
        test_result.line = test->line;
        // 使用 uri_without_url_encode 作为输出的uri
        test_result.uri = strdup(input_uri);
        test_result.label = test->label;

        // 复制期望tokens (libinjection结果)
        test_result.expected_tokens = (int *)malloc(sizeof(int) * expected_count);
        memcpy(test_result.expected_tokens, expected_tokens, sizeof(int) * expected_count);
        test_result.expected_count = (int)expected_count;

        // 复制实际tokens (DFA结果)
        test_result.actual_tokens = (int *)malloc(sizeof(int) * actual_count);
        memcpy(test_result.actual_tokens, actual_tokens, sizeof(int) * actual_count);
        test_result.actual_count = (int)actual_count;

        test_result.is_match = match;

        if (match) {
            result->correct_tests++;
            result->correct_tokens += (int)expected_count;
            test_result.token_accuracy = 1.0;
            test_result.correct_tokens = (int)expected_count;
            if (test->label == 1) {
                result->malicious_count++;
                result->malicious_correct++;
            }
        } else {
            result->error_count++;

            // 计算token级别准确率
            double token_acc = calculate_token_accuracy(actual_tokens, (int)actual_count,
                                                       expected_tokens, (int)expected_count);
            test_result.token_accuracy = token_acc;

            // 计算正确token数
            int min_len = (int)actual_count < (int)expected_count ? (int)actual_count : (int)expected_count;
            int correct = 0;
            for (int j = 0; j < min_len; j++) {
                if (actual_tokens[j] == expected_tokens[j]) {
                    correct++;
                }
            }
            test_result.correct_tokens = correct;

            if (test->label == 1) {
                result->malicious_count++;
            }

            result->correct_tokens += (int)(token_acc * expected_count);

            if (verbose && result->error_count <= 20) {
                printf("\n--- 测试 #%d (line %d) ---\n", i, test->line);
                printf("URI: %s\n", input_uri);
                printf("标签: %s\n", test->label == 1 ? "恶意" : "正常");

                printf("Expected (libinjection): [");
                for (size_t j = 0; j < expected_count && j < 20; j++) {
                    printf("%d", expected_tokens[j]);
                    if (j < expected_count - 1 && j < 19) printf(", ");
                }
                if (expected_count > 20) printf("...");
                printf("] (共%zu个)\n", expected_count);

                printf("Actual (DFA): [");
                for (size_t j = 0; j < actual_count && j < 20; j++) {
                    printf("%d", actual_tokens[j]);
                    if (j < actual_count - 1 && j < 19) printf(", ");
                }
                if (actual_count > 20) printf("...");
                printf("] (共%zu个)\n", actual_count);
                printf("Token准确率: %.2f%%\n", token_acc * 100.0);
            }
        }

        // 保存结果
        add_test_result(results, test_result);

        free(expected_tokens);
        free(actual_tokens);

        // 每10000个测试显示进度
        if ((i + 1) % 10000 == 0) {
            printf("进度: %d/%d (%.1f%%)\n", i + 1, test_count, (i + 1) * 100.0 / test_count);
        }
    }
}

// 保存结果到JSON文件
void save_results_to_json(const char *filepath, AccuracyResult *result, TestResultArray *results) {
    FILE *file = fopen(filepath, "w");
    if (!file) {
        printf("错误: 无法创建输出文件 %s\n", filepath);
        return;
    }

    printf("正在保存结果到 %s...\n", filepath);

    // 写入JSON文件头
    fprintf(file, "{\n");
    fprintf(file, "  \"summary\": {\n");
    fprintf(file, "    \"total_tests\": %d,\n", result->total_tests);
    fprintf(file, "    \"correct_tests\": %d,\n", result->correct_tests);
    fprintf(file, "    \"error_tests\": %d,\n", result->error_count);
    fprintf(file, "    \"sequence_accuracy\": %.4f,\n", result->total_tests > 0 ?
           (double)result->correct_tests / result->total_tests : 0.0);
    fprintf(file, "    \"total_tokens\": %d,\n", result->total_tokens);
    fprintf(file, "    \"correct_tokens\": %d,\n", result->correct_tokens);
    fprintf(file, "    \"token_accuracy\": %.4f,\n", result->total_tokens > 0 ?
           (double)result->correct_tokens / result->total_tokens : 0.0);
    fprintf(file, "    \"malicious_samples\": %d,\n", result->malicious_count);
    fprintf(file, "    \"malicious_correct\": %d,\n", result->malicious_correct);
    fprintf(file, "    \"malicious_accuracy\": %.4f\n", result->malicious_count > 0 ?
           (double)result->malicious_correct / result->malicious_count : 0.0);
    fprintf(file, "  },\n");
    fprintf(file, "  \"results\": [\n");

    // 写入每个测试结果
    for (size_t i = 0; i < results->size; i++) {
        TestResult *r = &results->results[i];

        fprintf(file, "    {\n");
        fprintf(file, "      \"line\": %d,\n", r->line);
        char *escaped_uri = json_escape_string(r->uri);
        fprintf(file, "      \"uri\": \"%s\",\n", escaped_uri);
        free(escaped_uri);
        fprintf(file, "      \"label\": %d,\n", r->label);
        fprintf(file, "      \"is_match\": %s,\n", r->is_match ? "true" : "false");
        fprintf(file, "      \"token_accuracy\": %.4f,\n", r->token_accuracy);
        fprintf(file, "      \"correct_tokens\": %d,\n", r->correct_tokens);

        // 期望tokens
        fprintf(file, "      \"expected_tokens\": [");
        for (int j = 0; j < r->expected_count; j++) {
            fprintf(file, "%d", r->expected_tokens[j]);
            if (j < r->expected_count - 1) fprintf(file, ", ");
        }
        fprintf(file, "],\n");

        // 实际tokens
        fprintf(file, "      \"actual_tokens\": [");
        for (int j = 0; j < r->actual_count; j++) {
            fprintf(file, "%d", r->actual_tokens[j]);
            if (j < r->actual_count - 1) fprintf(file, ", ");
        }
        fprintf(file, "]");

        if (i < results->size - 1) {
            fprintf(file, "},\n");
        } else {
            fprintf(file, "}\n");
        }
    }

    fprintf(file, "  ]\n");
    fprintf(file, "}\n");

    fclose(file);
    printf("结果已保存到 %s\n", filepath);
}

// 保存不一致的token到单独的JSON文件
void save_mismatches_to_json(const char *filepath, TestResultArray *results) {
    FILE *file = fopen(filepath, "w");
    if (!file) {
        printf("错误: 无法创建不匹配文件 %s\n", filepath);
        return;
    }

    printf("正在保存不匹配token到 %s...\n", filepath);

    // 统计不匹配数量
    int mismatch_count = 0;
    for (size_t i = 0; i < results->size; i++) {
        if (!results->results[i].is_match) {
            mismatch_count++;
        }
    }

    fprintf(file, "{\n");
    fprintf(file, "  \"total_mismatches\": %d,\n", mismatch_count);
    fprintf(file, "  \"mismatches\": [\n");

    int idx = 0;
    for (size_t i = 0; i < results->size; i++) {
        TestResult *r = &results->results[i];

        // 只保存不匹配的结果
        if (r->is_match) continue;

        fprintf(file, "    {\n");
        fprintf(file, "      \"index\": %d,\n", idx++);
        fprintf(file, "      \"line\": %d,\n", r->line);
        char *escaped_uri = json_escape_string(r->uri);
        fprintf(file, "      \"uri\": \"%s\",\n", escaped_uri);
        free(escaped_uri);
        fprintf(file, "      \"label\": %d,\n", r->label);
        fprintf(file, "      \"is_match\": %s,\n", r->is_match ? "true" : "false");
        fprintf(file, "      \"token_accuracy\": %.4f,\n", r->token_accuracy);
        fprintf(file, "      \"correct_tokens\": %d,\n", r->correct_tokens);
        fprintf(file, "      \"expected_count\": %d,\n", r->expected_count);
        fprintf(file, "      \"actual_count\": %d,\n", r->actual_count);

        fprintf(file, "      \"expected_tokens\": [");
        for (int j = 0; j < r->expected_count; j++) {
            fprintf(file, "%d", r->expected_tokens[j]);
            if (j < r->expected_count - 1) fprintf(file, ", ");
        }
        fprintf(file, "],\n");

        fprintf(file, "      \"actual_tokens\": [");
        for (int j = 0; j < r->actual_count; j++) {
            fprintf(file, "%d", r->actual_tokens[j]);
            if (j < r->actual_count - 1) fprintf(file, ", ");
        }
        fprintf(file, "],\n");

        // 生成diff信息
        fprintf(file, "      \"diff\": {\n");
        int max_len = r->expected_count > r->actual_count ? r->expected_count : r->actual_count;
        int diff_start = -1;
        for (int j = 0; j < max_len; j++) {
            int exp = (j < r->expected_count) ? r->expected_tokens[j] : -999;
            int act = (j < r->actual_count) ? r->actual_tokens[j] : -999;
            if (exp != act) {
                if (diff_start == -1) diff_start = j;
            }
        }

        if (diff_start >= 0) {
            fprintf(file, "        \"diff_start\": %d,\n", diff_start);
            // 显示差异的上下文
            int context_start = diff_start > 5 ? diff_start - 5 : 0;
            int context_end = diff_start + 10 < max_len ? diff_start + 10 : max_len;

            fprintf(file, "        \"expected_context\": [");
            for (int j = context_start; j < context_end && j < r->expected_count; j++) {
                fprintf(file, "%d", r->expected_tokens[j]);
                if (j < context_end - 1 && j < r->expected_count - 1) fprintf(file, ", ");
            }
            fprintf(file, "],\n");

            fprintf(file, "        \"actual_context\": [");
            for (int j = context_start; j < context_end && j < r->actual_count; j++) {
                fprintf(file, "%d", r->actual_tokens[j]);
                if (j < context_end - 1 && j < r->actual_count - 1) fprintf(file, ", ");
            }
            fprintf(file, "]\n");
        } else {
            fprintf(file, "        \"note\": \"length mismatch only\"\n");
        }
        fprintf(file, "      }\n");

        if (idx < mismatch_count) {
            fprintf(file, "    },\n");
        } else {
            fprintf(file, "    }\n");
        }
    }

    fprintf(file, "  ]\n");
    fprintf(file, "}\n");

    fclose(file);
    printf("不匹配token已保存到 %s (共%d条)\n", filepath, mismatch_count);
}

int main(void) {
    // 设置 Windows 控制台为 UTF-8 编码（解决中文乱码）
    #ifdef _WIN32
    system("chcp 65001 > nul 2>&1");
    #endif

    // 确保results目录存在
    #ifdef _WIN32
    system("if not exist results mkdir results");
    #else
    system("mkdir -p results");
    #endif

    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║                 DFA 引擎准确率测试                          ║\n");
    printf("║         对比 libinjection (Expected) vs DFA (Actual)        ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");

    // 加载测试数据
    TestDataArray test_array;
    init_test_array(&test_array);

    // 尝试多个可能的路径
    const char *paths[] = {
        "../tools/profile_data/full_dfa_tokenize_results.json",
        "C:/Users/60907/Desktop/test0/c/tools/profile_data/full_dfa_tokenize_results.json",
        NULL
    };

    int loaded = 0;
    for (int i = 0; paths[i]; i++) {
        loaded = load_test_data_from_json(paths[i], &test_array);
        if (loaded > 0) {
            printf("从文件加载: %s\n", paths[i]);
            break;
        }
    }

    printf("成功加载 %d 条测试数据\n\n", loaded);

    if (loaded == 0) {
        printf("错误: 未能加载测试数据\n");
        return 1;
    }

    // 创建 DFA 引擎 (使用正确的相对路径)
    // dfa_engine_create 会自动添加 tools/profile_data/ 路径
    dfa_engine_t *engine = dfa_engine_create("..");
    if (!engine) {
        printf("错误: 无法创建 DFA 引擎\n");
        free_test_array(&test_array);
        return 1;
    }

    printf("DFA 引擎创建成功\n\n");

    // 初始化结果数组
    TestResultArray result_array;
    init_result_array(&result_array);

    // 运行测试（0表示测试所有数据）
    AccuracyResult result;
    clock_t start_time = clock();

    // 可以通过修改下面的参数来限制测试数量，例如：
    // int max_tests = 1000;  // 只测试前1000条
    int max_tests = 1000000;  // 默认只测试前1000条，避免卡死
    // int max_tests = 0;  // 0表示测试所有数据（需要很长时间）

    run_accuracy_test(engine, &test_array, &result, &result_array, 1, max_tests);

    clock_t end_time = clock();
    double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;

    // 保存结果到JSON文件
    save_results_to_json("../tests/results/dfa_accuracy_results.json", &result, &result_array);
    save_mismatches_to_json("../tests/results/dfa_mismatches.json", &result_array);

    // 打印结果
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║                      测试结果汇总                           ║\n");
    printf("╠════════════════════════════════════════════════════════════╣\n");
    printf("║  总测试数:       %10d                                      ║\n", result.total_tests);
    printf("║  正确数:         %10d                                      ║\n", result.correct_tests);
    printf("║  错误数:         %10d                                      ║\n", result.error_count);
    printf("║  序列准确率:     %10.2f%%                                  ║\n", (result.total_tests > 0 ?
           (double)result.correct_tests / result.total_tests * 100.0 : 0.0));
    printf("║  Token准确率:    %10.2f%%                                  ║\n", (result.total_tokens > 0 ?
           (double)result.correct_tokens / result.total_tokens * 100.0 : 0.0));
    printf("║  恶意样本数:     %10d                                      ║\n", result.malicious_count);
    printf("║  恶意样本正确:   %10d                                      ║\n", result.malicious_correct);
    if (result.malicious_count > 0) {
        printf("║  恶意样本准确率: %9.2f%%                                  ║\n",
               (double)result.malicious_correct / result.malicious_count * 100.0);
    }
    printf("║  耗时:           %10.2f 秒                                  ║\n", elapsed_time);
    if (elapsed_time > 0) {
        printf("║  吞吐量:         %10.0f 条/秒                             ║\n", result.total_tests / elapsed_time);
    }
    printf("╚════════════════════════════════════════════════════════════╝\n");

    // 销毁引擎和释放数据
    dfa_engine_destroy(engine);
    free_test_array(&test_array);
    free_result_array(&result_array);

    return 0;
}
