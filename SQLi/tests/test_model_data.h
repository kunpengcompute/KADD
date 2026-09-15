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

#include <string>
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

/* 校验数据集样本数是否满足用例最低要求。
 * 满足 -> 返回空串；不足 -> 返回可直接喂给 GTEST_SKIP 的原因说明。
 *
 * 背景：train_data.csv 由 Git LFS 管理，未执行 git lfs pull 时工作区里只是一个
 * 约 130 字节的指针文本（3 行）。load_csv_samples 会把后两行当成 2 条样本解析成功，
 * 于是准确率用例以 n=2 "空转通过"，而按下标取样本的批量用例直接越界 -> SegFault，
 * 崩溃进程不会执行 atexit，gcov/llvm-cov 数据整份丢失。本函数用于在两种后果之前拦截。
 */
std::string check_dataset_size(const char* csv_path,
                               const std::vector<Sample>& samples,
                               size_t required);

#endif /* TEST_MODEL_DATA_H */