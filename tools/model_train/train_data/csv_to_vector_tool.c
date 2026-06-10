/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * @file csv_to_vector_tool.c
 * @brief CSV转向量工具 - 将训练数据集的Query列转换为28维特征向量
 *
 * 功能:
 * 1. 读取 total_training_dataset_v4_pinnacle.csv
 * 2. 对每行的 Query 列进行 DFA tokenize
 * 3. 调用特征引擎转换为28维向量
 * 4. 输出 training_vectors.csv (包含 feature_0~feature_27 和 label 列)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "kadd_dfa_engine.h"
#include "kadd_feature_engine.h"
#include "kadd_config_loader.h"
#include "kadd_errors.h"

/* ========== 配置常量 ========== */
#define FEATURE_DIM 28
#define WINDOW_SIZE 5
#define MAX_LINE_LENGTH 8192
#define MAX_QUERY_LENGTH 2048
#define MAX_TOKEN_COUNT 300

/* ========== 辅助函数 ========== */

/**
 * @brief 将CSV转义的引号转换为单个引号
 * @param src 源字符串（包含""转义）
 * @param dest 目标缓冲区
 * @param src_len 源字符串长度
 * @return 转换后的字符串长度
 */
static size_t unescape_csv_quotes(const char *src, char *dest, size_t src_len)
{
    size_t dest_len = 0;
    for (size_t i = 0; i < src_len; ) {
        if (src[i] == '"' && (i + 1 < src_len) && src[i + 1] == '"') {
            // 转义的引号 "" -> "
            dest[dest_len++] = '"';
            i += 2;
        } else {
            dest[dest_len++] = src[i++];
        }
    }
    dest[dest_len] = '\0';
    return dest_len;
}

/**
 * @brief 转义字符串中的引号用于CSV输出
 * @param src 源字符串
 * @return 转义后的字符串（需要调用者释放）
 */
static char* escape_for_csv(const char *src)
{
    if (!src) return NULL;

    // 计算需要的空间（每个引号变成两个引号）
    size_t quote_count = 0;
    size_t len = strlen(src);
    for (size_t i = 0; i < len; i++) {
        if (src[i] == '"') quote_count++;
    }

    // 分配缓冲区
    char *escaped = (char *)malloc(len + quote_count + 1);
    if (!escaped) return NULL;

    // 转义引号
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (src[i] == '"') {
            escaped[j++] = '"';
            escaped[j++] = '"';
        } else {
            escaped[j++] = src[i];
        }
    }
    escaped[j] = '\0';
    return escaped;
}

/**
 * @brief 去除字符串首尾空白
 */
static char* trim_whitespace(char *str)
{
    if (!str) {
        return NULL;
    }

    char *start = str;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }

    if (*start == '\0') {
        return start;
    }

    char *end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) {
        end--;
    }
    *(end + 1) = '\0';

    return start;
}

/**
 * @brief 解析CSV行，提取Query和Label列
 *
 * @param line CSV行内容
 * @param query_out 输出：Query内容（需要调用者释放）
 * @param label_out 输出：Label值
 * @return 0成功，<0失败
 */
static int parse_csv_line(char *line, char **query_out, int *label_out)
{
    if (!line || !query_out || !label_out) {
        return KADD_ERROR_NULL_PTR;
    }

    *query_out = NULL;
    *label_out = 0;

    // 查找Query列（第一列，跳过可能的BOM标记）
    char *query_start = line;
    if ((unsigned char)query_start[0] == 0xEF &&
        (unsigned char)query_start[1] == 0xBB &&
        (unsigned char)query_start[2] == 0xBF) {
        query_start += 3;
    }

    // 提取Query（可能被引号包围）
    int in_quotes = 0;
    char *query_end = query_start;

    if (*query_start == '"') {
        in_quotes = 1;
        query_start++;
        query_end = query_start;
    }

    char *label_start = NULL;
    while (*query_end) {
        if (in_quotes) {
            if (*query_end == '"') {
                if (*(query_end + 1) == '"') {
                    // 转义的引号 ""，跳过第二个引号
                    query_end += 2;
                    continue;
                } else {
                    // 结束引号
                    query_end++;
                    break;
                }
            }
            query_end++;
        } else {
            if (*query_end == ',') {
                break;  // 找到逗号就停止，不要移动指针
            }
            query_end++;
        }
    }

    // 分配Query缓冲区（考虑转义引号会减少长度）
    size_t raw_len = query_end - query_start;
    *query_out = (char *)malloc(raw_len + 1);  // 最多和原长度相同
    if (!*query_out) {
        return KADD_ERROR_OUT_OF_MEMORY;
    }
    // 将CSV转义的引号转换回单个引号
    size_t query_len = unescape_csv_quotes(query_start, *query_out, raw_len);

    // 查找Label列（query_end指向逗号，跳过它）
    label_start = query_end;
    if (*label_start == ',') {
        label_start++;
    }

    // 解析Label
    char *label_end = label_start;
    while (*label_end && !isspace((unsigned char)*label_end)) {
        label_end++;
    }
    *label_end = '\0';
    *label_out = atoi(label_start);

    return KADD_SUCCESS;
}

/**
 * @brief 处理单个Query，提取特征向量
 */
static int process_query(const kadd_dfa_engine_t *dfa_engine,
                         const char *query,
                         int *feature_vector)
{
    if (!dfa_engine || !query || !feature_vector) {
        return KADD_ERROR_NULL_PTR;
    }

    // 1. DFA tokenize
    int tokens[MAX_TOKEN_COUNT];
    int token_count = 0;

    int ret = kadd_dfa_engine_tokenize(dfa_engine, query, strlen(query),
                                       tokens, &token_count);
    if (ret != KADD_SUCCESS) {
        fprintf(stderr, "[ERROR] DFA tokenize failed for query: %.50s...\n", query);
        return ret;
    }

    if (token_count == 0) {
        // 空token序列，返回零向量
        for (int i = 0; i < FEATURE_DIM; i++) {
            feature_vector[i] = 0;
        }
        return KADD_SUCCESS;
    }

    // 2. 特征提取
    ret = kadd_seq_to_feature(tokens, token_count, WINDOW_SIZE,
                             feature_vector, FEATURE_DIM);
    if (ret != KADD_SUCCESS) {
        fprintf(stderr, "[ERROR] Feature extraction failed for query: %.50s...\n", query);
        return ret;
    }

    return KADD_SUCCESS;
}

/* ========== 主函数 ========== */

int main(int argc, char *argv[])
{
    const char *input_csv_path = "../total_training_dataset_v4_pinnacle.csv";
    const char *output_csv_path = "../training_vectors.csv";
    const char *config_path = "../../../../config.json";

    // 解析命令行参数
    if (argc >= 2) {
        input_csv_path = argv[1];
    }
    if (argc >= 3) {
        output_csv_path = argv[2];
    }
    if (argc >= 4) {
        config_path = argv[3];
    }

    printf("========== CSV转向量工具 ==========\n");
    printf("输入文件: %s\n", input_csv_path);
    printf("输出文件: %s\n", output_csv_path);
    printf("配置文件: %s\n", config_path);
    printf("特征维度: %d, 窗口大小: %d\n\n", FEATURE_DIM, WINDOW_SIZE);

    // 1. 加载配置
    printf("[1/4] 加载配置文件...\n");
    kadd_config_t config;
    int ret = kadd_config_load(config_path, &config);
    if (ret != KADD_SUCCESS) {
        fprintf(stderr, "[ERROR] Failed to load config: %s\n", config_path);
        return 1;
    }
    printf("  配置加载成功\n");

    // 2. 创建DFA引擎
    printf("\n[2/4] 创建DFA引擎...\n");
    kadd_dfa_engine_t *dfa_engine = NULL;
    kadd_dfa_table_paths_t table_paths = {
        config.dfa.transition_table_path,
        config.dfa.output_table_path,
        config.dfa.eod_table_path
    };

    ret = kadd_dfa_engine_create(&dfa_engine, &table_paths,
                                config.dfa.max_input_length,
                                config.dfa.max_output_dim);
    if (ret != KADD_SUCCESS) {
        fprintf(stderr, "[ERROR] Failed to create DFA engine\n");
        return 1;
    }
    printf("  DFA引擎创建成功\n");

    // 3. 打开文件
    printf("\n[3/4] 处理CSV文件...\n");
    FILE *input_fp = fopen(input_csv_path, "r");
    if (!input_fp) {
        fprintf(stderr, "[ERROR] Cannot open input file: %s\n", input_csv_path);
        kadd_dfa_engine_free(dfa_engine);
        return 1;
    }

    FILE *output_fp = fopen(output_csv_path, "w");
    if (!output_fp) {
        fprintf(stderr, "[ERROR] Cannot open output file: %s\n", output_csv_path);
        fclose(input_fp);
        kadd_dfa_engine_free(dfa_engine);
        return 1;
    }

    // 写入输出CSV header
    fprintf(output_fp, "Query");
    for (int i = 0; i < FEATURE_DIM; i++) {
        fprintf(output_fp, ",feature_%d", i);
    }
    fprintf(output_fp, ",Label\n");

    // 4. 处理每一行
    char line[MAX_LINE_LENGTH];
    int line_count = 0;
    int success_count = 0;
    int error_count = 0;
    int label_0_count = 0;
    int label_1_count = 0;

    // 跳过header行
    if (fgets(line, sizeof(line), input_fp)) {
        printf("  跳过header行: %.50s...\n", trim_whitespace(line));
    }

    while (fgets(line, sizeof(line), input_fp)) {
        line_count++;

        // 解析CSV行
        char *query = NULL;
        int label = 0;
        ret = parse_csv_line(line, &query, &label);

        if (ret != KADD_SUCCESS) {
            fprintf(stderr, "  [WARN] Line %d: 解析失败\n", line_count);
            error_count++;
            continue;
        }

        // 处理Query
        int feature_vector[FEATURE_DIM];
        ret = process_query(dfa_engine, query, feature_vector);

        if (ret != KADD_SUCCESS) {
            fprintf(stderr, "  [WARN] Line %d: 处理失败 - %s\n", line_count, query);
            free(query);
            error_count++;
            continue;
        }

        // 写入输出CSV
        char *escaped_query = escape_for_csv(query);
        fprintf(output_fp, "\"%s\"", escaped_query ? escaped_query : query);
        if (escaped_query) free(escaped_query);

        for (int i = 0; i < FEATURE_DIM; i++) {
            fprintf(output_fp, ",%d", feature_vector[i]);
        }
        fprintf(output_fp, ",%d\n", label);

        // 统计
        success_count++;
        if (label == 0) {
            label_0_count++;
        } else {
            label_1_count++;
        }

        free(query);

        // 进度显示
        if (line_count % 1000 == 0) {
            printf("  已处理: %d 行 (成功: %d, 失败: %d)\n",
                   line_count, success_count, error_count);
        }
    }

    // 5. 关闭文件和清理
    fclose(input_fp);
    fclose(output_fp);
    kadd_dfa_engine_free(dfa_engine);
    kadd_config_free(&config);

    // 6. 输出统计信息
    printf("\n[4/4] 处理完成\n");
    printf("========== 统计信息 ==========\n");
    printf("总行数: %d\n", line_count);
    printf("成功: %d\n", success_count);
    printf("失败: %d\n", error_count);
    printf("标签分布: Label 0 = %d, Label 1 = %d\n", label_0_count, label_1_count);
    printf("输出文件: %s\n", output_csv_path);
    printf("==============================\n");

    return (error_count > 0) ? 1 : 0;
}
