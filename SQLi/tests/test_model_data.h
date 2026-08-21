/* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved. */
/**
 * @file test_model_data.h
 * @brief CNN/RF 引擎 gtest 共享的数据加载：CSV 解析 + DFA 分词。
 *
 * 逻辑精简自 SQLi/demo/kadd_dl_bench_demo.c（parse_csv_line + load_csv_tokens），
 * 供 cnn/rf 两个 UT 复用，避免重复实现。每样本保留原始 token 长度：
 * CNN 消费 pad 到 300 的 token，RF 用真实 token 数算 28 维特征。
 */
#ifndef TEST_MODEL_DATA_H
#define TEST_MODEL_DATA_H

#include <vector>

typedef struct kadd_dfa_engine_t kadd_dfa_engine_t;

struct Sample {
    std::vector<int> tokens;   /* DFA 分词结果，保留原始长度 */
    int label;                 /* 0 正常 / 1 SQLi */
};

/* 读取 CSV（跳过表头），逐行 DFA 分词成 tokens + label。
 * 返回 true 表示加载到样本；解析失败的整行跳过。 */
bool load_csv_samples(const char* csv_path, const kadd_dfa_engine_t* dfa,
                      std::vector<Sample>& out);

#endif /* TEST_MODEL_DATA_H */