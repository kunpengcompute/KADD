/* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved. */
/**
 * @file test_model_data.cpp
 * @brief load_csv_samples 实现（逻辑取自 demo/kadd_dl_bench_demo.c）。
 */

#include "test_model_data.h"

#include "kadd_dfa_engine.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace {

constexpr int kDfaTokens = 300;

void unescape_csv_quotes(const char* src, char* dest, size_t src_len) {
    size_t di = 0, i = 0;
    for (; i < src_len;) {
        if (src[i] == '"' && i + 1 < src_len && src[i + 1] == '"') {
            dest[di++] = '"'; i += 2;
        } else {
            dest[di++] = src[i++];
        }
    }
    dest[di] = '\0';
}

/* 解析一行 CSV：first 列 query，末列 label。返回 0 成功。 */
int parse_csv_line(char* line, char** query_out, int* label_out) {
    *query_out = nullptr; *label_out = 0;
    char* qs = line;
    if ((unsigned char)qs[0] == 0xEF && (unsigned char)qs[1] == 0xBB &&
        (unsigned char)qs[2] == 0xBF) qs += 3;

    int in_quotes = 0;
    char* qe = qs;
    if (*qs == '"') { in_quotes = 1; qs++; qe = qs; }
    while (*qe) {
        if (in_quotes) {
            if (*qe == '"') {
                if (*(qe + 1) == '"') { qe += 2; continue; }
                break;
            }
            qe++;
        } else {
            if (*qe == ',') break;
            qe++;
        }
    }
    size_t raw_len = static_cast<size_t>(qe - qs);
    char* q = static_cast<char*>(std::malloc(raw_len + 1));
    if (!q) return -103;
    unescape_csv_quotes(qs, q, raw_len);

    char* ls = qe;
    if (*ls == '"') ls++;
    if (*ls == ',') ls++;
    char* le = ls;
    while (*le && !std::isspace(static_cast<unsigned char>(*le))) le++;
    *le = '\0';
    *label_out = std::atoi(ls);
    *query_out = q;
    return 0;
}

}  // namespace

bool load_csv_samples(const char* csv_path, const kadd_dfa_engine_t* dfa,
                      std::vector<Sample>& out) {
    std::FILE* f = std::fopen(csv_path, "r");
    if (!f) return false;
    char line[8192];
    if (!std::fgets(line, sizeof(line), f)) { std::fclose(f); return false; }  /* 表头 */

    while (std::fgets(line, sizeof(line), f)) {
        char* query = nullptr;
        int label = 0;
        if (parse_csv_line(line, &query, &label) != 0 || !query) continue;

        int toks[kDfaTokens] = {0};
        int tc = 0;
        int ret = kadd_dfa_engine_tokenize(dfa, query, std::strlen(query), toks, &tc);
        std::free(query);
        if (ret != KADD_SUCCESS || tc <= 0) continue;

        Sample s;
        s.tokens.assign(toks, toks + tc);
        s.label = (label == 1) ? 1 : 0;
        out.push_back(std::move(s));
    }
    std::fclose(f);
    return !out.empty();
}