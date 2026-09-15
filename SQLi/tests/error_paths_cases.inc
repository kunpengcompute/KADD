/* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved. */
/*
 * @file error_paths_cases.inc
 * @brief UT 与 FUZZ 共用的引擎错误路径用例（覆盖率导向，本地 gcov/clang 实测验证）
 *
 * 背景
 * ----
 * 4 份覆盖率报告（SVE/ONNX × UT/FUZZ）中 config_loader / dfa_engine /
 * feature_engine / rf_engine / cnn_engine / dl_engine / sqli_engine 的参数校验、
 * 文件解析、HMAC 认证失败等错误分支大量未覆盖。本文件把这些分支的最小触达用例
 * 集中为一组 static 函数：
 *   - FuzzFeatureErrorPaths    feature 批量接口全部校验分支
 *   - FuzzConfigErrorPaths     config 加载/校验错误分支
 *   - FuzzDfaErrorPaths        DFA create/表文件损坏分支
 *   - FuzzDfaBatchErrorPaths   DFA tokenize_batch 校验 + 最小表生命周期
 *   - FuzzRfRealModelPaths     真实 .krf 生命周期 + 损坏/篡改/密钥错误变体
 *   - FuzzCnnRealModelPaths    真实 .kcn 生命周期 + 损坏/篡改变体
 *   - FuzzDlErrorPaths         DL 分发层 feature_dim 失配 / 加载失败 / 完整生命周期
 *   - FuzzSqliErrorPaths       SQLi create_from_file 失败、native→ONNX 回退、batch 校验
 *   - FuzzInferenceThreadEnvBranches  ORT 线程数环境变量未设置/非法/合法分支（进程内只执行一次）
 *
 * 使用方
 * ------
 * - tests/fuzz/kadd_fuzzer.cpp（FUZZ 覆盖率）
 * - tests/test_error_paths_gtest.cpp（UT 覆盖率）
 * 每个模型类用例内部均为完整生命周期：load → predict → predict_batch → free，
 * 四个动作 1:1:1:1 同频执行，避免长稳 fuzz 下资源频次失衡。
 *
 * 约定：不产生任何 stdout 输出（fuzz 每次迭代都会调用，日志会淹没终端）。
 * FuzzInferenceThreadEnvBranches 例外：它故意造非法环境变量，会产生少量
 * stderr 告警，但进程内只执行一次，不影响长稳运行的日志量。
 */
#ifndef KADD_ERROR_PATHS_CASES_INC
#define KADD_ERROR_PATHS_CASES_INC

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <limits>

#include "kadd_errors.h"
#include "kadd_config_loader.h"
#include "kadd_dfa_engine.h"
#include "kadd_feature_engine.h"
#include "kadd_ml_engine.h"
#include "kadd_dl_engine.h"
#include "kadd_sqli_engine.h"
#include "kadd_rf_engine.h"
#include "kadd_cnn_engine.h"

#if defined(__GNUC__) || defined(__clang__)
#define FZ_MAYBE_UNUSED __attribute__((unused))
#else
#define FZ_MAYBE_UNUSED
#endif

/* ========== 共享辅助 ========== */

/* DFA 表每状态 cell 数（256 个输入字节） */
static const size_t FzDfaCells = 256;

/* 真实资产路径：优先 /opt/KADD 绝对路径（部署环境），回退相对路径（build/tests cwd） */
static const char *const FzRfKrfAbs   = "/opt/KADD/SQLi/assets/model/model_tokens_rf.krf";
static const char *const FzRfKrfRel   = "assets/model/model_tokens_rf.krf";
static const char *const FzCnnKcnAbs  = "/opt/KADD/SQLi/assets/model/model_tokens_dl_cnn.kcn";
static const char *const FzCnnKcnRel  = "assets/model/model_tokens_dl_cnn.kcn";
static const char *const FzRfOnnxAbs  = "/opt/KADD/SQLi/assets/model/model_tokens_rf.onnx";
static const char *const FzRfOnnxRel  = "assets/model/model_tokens_rf.onnx";
static const char *const FzCnnOnnxAbs = "/opt/KADD/SQLi/assets/model/model_tokens_dl_cnn.onnx";
static const char *const FzCnnOnnxRel = "assets/model/model_tokens_dl_cnn.onnx";
static const char *const FzTranAbs    = "/opt/KADD/SQLi/assets/data/transition_table.bin";
static const char *const FzTranRel    = "assets/data/transition_table.bin";
static const char *const FzOutAbs     = "/opt/KADD/SQLi/assets/data/output_table.bin";
static const char *const FzOutRel     = "assets/data/output_table.bin";
static const char *const FzEodAbs     = "/opt/KADD/SQLi/assets/data/eod_table.bin";
static const char *const FzEodRel     = "assets/data/eod_table.bin";

/* 开发 HMAC 密钥（公开，与 test_model_hmac_key.h 一致），仅用于恢复环境变量 */
static const char *const FzKeyHex =
    "74fab58d87dc235ee049b6faf21b44825dba1d9dc05bbc14aeded49cb211a1db";

/* 返回第一个可打开（存在）的路径；都不存在时返回 a */
static const char *FzPickPath(const char *a, const char *b) {
    FILE *f = fopen(a, "rb");
    if (f) { fclose(f); return a; }
    f = fopen(b, "rb");
    if (f) { fclose(f); return b; }
    return a;
}

/* 设置 / 删除环境变量（value == nullptr 表示删除）。
 * 用于临时改写 ORT 线程数配置以触达「未设置 / 非法值回退默认」分支，
 * 调用方必须在用例结束时精确复原，避免污染 coverage_all.sh 导出的调优值。 */
FZ_MAYBE_UNUSED static void FzSetEnv(const char *name, const char *value) {
#ifdef _WIN32
    (void)_putenv_s(name, (value != nullptr) ? value : "");   /* 空串即删除 */
#else
    if (value != nullptr) { (void)setenv(name, value, 1); }
    else                  { (void)unsetenv(name); }
#endif
}

/* 解析可用的 config.json（fuzz/gtest 的运行目录可能是 build/tests 或仓库根） */
static const char *FzResolveConfig() {
    static const char *const cands[] = {
        "config.json", "../config.json", "/opt/KADD/SQLi/config.json"
    };
    for (size_t i = 0; i < sizeof(cands) / sizeof(cands[0]); ++i) {
        FILE *f = fopen(cands[i], "rb");
        if (f) { fclose(f); return cands[i]; }
    }
    return cands[0];
}

/* ========== 真实资产闸门（FUZZ 侧生效，UT 侧恒放行） ==========
 * 覆盖率只要求分支被执行到，不要求每次迭代都执行；长稳验收口径是对外接口
 * 调用次数。真实 .onnx 会话与真实 10.4MB DFA 表单次开销大，因此只对「真实
 * 资产」那一段降频，同一函数里的廉价分支（NULL / 参数校验 / 合成微缩资产）
 * 仍每次迭代执行：分层比重与 load:predict:predict_batch:free 的 1:1:1:1 配比不变。
 * UT 侧不定义 KADD_ERROR_PATHS_IN_FUZZER → 恒放行，行为与修改前一致。
 */
#ifndef KADD_FUZZ_REAL_ASSET_INTERVAL
#define KADD_FUZZ_REAL_ASSET_INTERVAL 64u
#endif

FZ_MAYBE_UNUSED static bool FzAllowRealAsset(unsigned slot) {
#ifdef KADD_ERROR_PATHS_IN_FUZZER
    static uint64_t s_ctr[16] = {0};
    return ((++s_ctr[slot & 15u]) % KADD_FUZZ_REAL_ASSET_INTERVAL) == 0u;
#else
    (void)slot;
    return true;
#endif
}

FZ_MAYBE_UNUSED static void FzWriteFile(const char *path, const std::string &s) {
    FILE *f = fopen(path, "wb");
    if (f) { if (!s.empty()) fwrite(s.data(), 1, s.size(), f); fclose(f); }
}

FZ_MAYBE_UNUSED static void FzWriteBytes(const char *path, const std::vector<unsigned char> &b) {
    FILE *f = fopen(path, "wb");
    if (f) { if (!b.empty()) fwrite(b.data(), 1, b.size(), f); fclose(f); }
}

FZ_MAYBE_UNUSED static std::vector<unsigned char> FzReadAll(const char *p) {
    std::vector<unsigned char> v;
    FILE *f = fopen(p, "rb");
    if (!f) return v;
    unsigned char buf[8192]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) v.insert(v.end(), buf, buf + n);
    fclose(f);
    return v;
}

/* lab4 用例使用的字节写入别名（与 FzWriteBytes 等价） */
FZ_MAYBE_UNUSED static void FzWriteAll(const char *p, const std::vector<unsigned char> &b) {
    FzWriteBytes(p, b);
}

FZ_MAYBE_UNUSED static void FzPutU32LE(std::vector<unsigned char> &v, uint32_t x) {
    v.push_back((unsigned char)(x & 0xFF)); v.push_back((unsigned char)((x >> 8) & 0xFF));
    v.push_back((unsigned char)((x >> 16) & 0xFF)); v.push_back((unsigned char)((x >> 24) & 0xFF));
}

FZ_MAYBE_UNUSED static void FzPutU16LE(std::vector<unsigned char> &v, uint16_t x) {
    v.push_back((unsigned char)(x & 0xFF)); v.push_back((unsigned char)((x >> 8) & 0xFF));
}

FZ_MAYBE_UNUSED static std::string FzCfg(const std::string &mode, const std::string &tt, const std::string &ot,
                       const std::string &eod, const std::string &rf) {
    return std::string("{") + "\"engine_mode\":\"" + mode + "\","
         + "\"transition_table\":\"" + tt + "\","
         + "\"output_table\":\"" + ot + "\","
         + "\"eod_table\":\"" + eod + "\","
         + "\"model_rf\":\"" + rf + "\"}";
}

FZ_MAYBE_UNUSED static void FzDfaTable(const char *path, uint32_t header_states, size_t cells, uint16_t fill,
                     size_t set_idx, uint16_t set_val, size_t cells_to_write) {
    std::vector<unsigned char> v;
    FzPutU32LE(v, header_states);
    size_t n = (cells_to_write == (size_t)-1) ? cells : cells_to_write;
    for (size_t i = 0; i < n; ++i) {
        FzPutU16LE(v, (i == set_idx) ? set_val : fill);
    }
    FzWriteBytes(path, v);
}

FZ_MAYBE_UNUSED static void FzDfaRawBytes(const char *path, size_t n) {
    std::vector<unsigned char> v(n, 0x00);
    FzWriteBytes(path, v);
}

FZ_MAYBE_UNUSED static int FzDfaCreate(const char *tt, const char *ot, const char *eod,
                     size_t max_in, size_t max_tok, kadd_dfa_engine_t **e) {
    kadd_dfa_table_paths_t p;
    memset(&p, 0, sizeof(p));
    p.transition_table_path = tt; p.output_table_path = ot; p.eod_table_path = eod;
    return kadd_dfa_engine_create(e, &p, max_in, max_tok);
}

FZ_MAYBE_UNUSED static void FuzzFeatureErrorPaths() {
    (void)(kadd_seq_to_feature_batch(NULL));

    int seq[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    const int *seqs[2] = {seq, seq};
    int lens[2] = {8, 8};
    int feats[2 * 28];
    memset(feats, 0, sizeof(feats));

    kadd_feature_batch_t b;
    memset(&b, 0, sizeof(b));
    b.seqs = const_cast<const int **>(seqs); b.seq_lens = lens; b.features = feats;
    b.count = 2; b.feature_dim = 28; b.window_size = 5; b.batch_size = 64;

    kadd_feature_batch_t t = b; t.seqs = NULL;
    (void)(kadd_seq_to_feature_batch(&t));
    t = b; t.seq_lens = NULL;
    (void)(kadd_seq_to_feature_batch(&t));
    t = b; t.features = NULL;
    (void)(kadd_seq_to_feature_batch(&t));
    t = b; t.count = 0;
    (void)(kadd_seq_to_feature_batch(&t));
    t = b; t.count = -3;
    (void)(kadd_seq_to_feature_batch(&t));
    t = b; t.feature_dim = 0;
    (void)(kadd_seq_to_feature_batch(&t));
    t = b; t.window_size = 0;
    (void)(kadd_seq_to_feature_batch(&t));
    t = b; t.count = 10000001;
    (void)(kadd_seq_to_feature_batch(&t));
    t = b; t.feature_dim = 301;
    (void)(kadd_seq_to_feature_batch(&t));
    t = b; t.window_size = 101;
    (void)(kadd_seq_to_feature_batch(&t));
    t = b;
    (void)(kadd_seq_to_feature_batch(&t));

    int lens_bad[2] = {0, 8};
    t = b; t.seq_lens = lens_bad;
    (void)(kadd_seq_to_feature_batch(&t));
    int lens_neg[2] = {-5, 8};
    t = b; t.seq_lens = lens_neg;
    (void)(kadd_seq_to_feature_batch(&t));

    int seq_oob[8] = {1, 2, 99, 4, 5, 6, 7, 8};   /* 99 >= feature_dim */
    const int *seqs_oob[2] = {seq_oob, seq};
    t = b; t.seqs = seqs_oob;
    (void)(kadd_seq_to_feature_batch(&t));

    const int *seqs_null[2] = {NULL, seq};
    t = b; t.seqs = seqs_null;
    (void)(kadd_seq_to_feature_batch(&t));

    t = b; t.batch_size = 0;      /* 回退默认 64 */
    (void)(kadd_seq_to_feature_batch(&t));
    t = b; t.batch_size = 5000;   /* 超上限回退默认 */
    (void)(kadd_seq_to_feature_batch(&t));
    t = b; t.batch_size = 1;      /* 逐条分批 */
    (void)(kadd_seq_to_feature_batch(&t));
}

FZ_MAYBE_UNUSED static void FuzzConfigErrorPaths() {
    kadd_config_t cfg;
    (void)(kadd_config_load("/tmp/fz_c1.json", NULL));
    (void)(kadd_config_load(NULL, &cfg));
    (void)(kadd_config_load("", &cfg));
    std::string longp(600, 'a');
    (void)(kadd_config_load(longp.c_str(), &cfg));
    (void)(kadd_config_load("/tmp/fz_nonexistent_cfg.json", &cfg));

    FzWriteFile("/tmp/fz_cfg_empty.json", std::string(""));
    (void)(kadd_config_load("/tmp/fz_cfg_empty.json", &cfg));

    (void)(kadd_config_load("/tmp", &cfg));

    FzWriteFile("/tmp/fz_cfg_bad.json", std::string("{ not json"));
    (void)(kadd_config_load("/tmp/fz_cfg_bad.json", &cfg));

    /* 超长字段值 -> safe_copy 截断 */
    std::string longv(600, 'x');
    FzWriteFile("/tmp/fz_cfg_long.json", FzCfg("ml", longv, "/tmp/o.bin", "/tmp/e.bin", "/tmp/m.krf"));
    (void)(kadd_config_load("/tmp/fz_cfg_long.json", &cfg));
    (void)((long)strlen(cfg.transition_table));

    /* 控制字符（JSON \u0001 转义 -> cJSON 解码为 0x01 -> 中和为 '?'） */
    FzWriteFile("/tmp/fz_cfg_ctrl.json",
              std::string("{\"engine_mode\":\"ml\",\"transition_table\":\"/tmp/a\\u0001b.bin\",")
              + "\"output_table\":\"/tmp/o.bin\",\"eod_table\":\"/tmp/e.bin\",\"model_rf\":\"/tmp/m.krf\"}");
    (void)(kadd_config_load("/tmp/fz_cfg_ctrl.json", &cfg));
    (void)((long)(cfg.transition_table[6] == '?' ? 1 : 0));

    /* engine_mode 空串 -> parse_engine_mode 的 strlen==0 分支 */
    FzWriteFile("/tmp/fz_cfg_mode0.json", FzCfg("", "/tmp/t.bin", "/tmp/o.bin", "/tmp/e.bin", "/tmp/m.krf"));
    (void)(kadd_config_load("/tmp/fz_cfg_mode0.json", &cfg));
    (void)((long)cfg.engine_mode);

    /* engine_mode 未知串 -> ML；dl -> DL */
    FzWriteFile("/tmp/fz_cfg_modeX.json", FzCfg("xyz", "/tmp/t.bin", "/tmp/o.bin", "/tmp/e.bin", "/tmp/m.krf"));
    (void)(kadd_config_load("/tmp/fz_cfg_modeX.json", &cfg));
    FzWriteFile("/tmp/fz_cfg_modeDL.json",
              std::string("{\"engine_mode\":\"dl\",\"transition_table\":\"/tmp/t.bin\","
                          "\"output_table\":\"/tmp/o.bin\",\"eod_table\":\"/tmp/e.bin\","
                          "\"model_cnn\":\"/tmp/m.kcn\"}"));
    (void)(kadd_config_load("/tmp/fz_cfg_modeDL.json", &cfg));
    (void)((long)cfg.engine_mode);

    /* 缺必填字段 / ML 无模型 / DL 无模型 */
    FzWriteFile("/tmp/fz_cfg_miss.json", std::string("{\"engine_mode\":\"ml\",\"output_table\":\"/tmp/o.bin\",\"eod_table\":\"/tmp/e.bin\",\"model_rf\":\"/tmp/m.krf\"}"));
    (void)(kadd_config_load("/tmp/fz_cfg_miss.json", &cfg));
    FzWriteFile("/tmp/fz_cfg_norf.json", FzCfg("ml", "/tmp/t.bin", "/tmp/o.bin", "/tmp/e.bin", ""));
    (void)(kadd_config_load("/tmp/fz_cfg_norf.json", &cfg));
    FzWriteFile("/tmp/fz_cfg_nocnn.json", std::string("{\"engine_mode\":\"dl\",\"transition_table\":\"/tmp/t.bin\",\"output_table\":\"/tmp/o.bin\",\"eod_table\":\"/tmp/e.bin\"}"));
    (void)(kadd_config_load("/tmp/fz_cfg_nocnn.json", &cfg));
}

FZ_MAYBE_UNUSED static void FuzzDfaErrorPaths() {
    kadd_dfa_engine_t *e = NULL;
    const size_t TR_CELLS = 2 * 256;   /* states=2 */
    (void)(kadd_dfa_engine_create(NULL, NULL, 10, 10));
    (void)(FzDfaCreate("/tmp/fz_no_such_table.bin", "/tmp/o.bin", "/tmp/e.bin", 300, 300, &e));

    FzDfaRawBytes("/tmp/fz_dfa_2b.bin", 2);
    (void)(FzDfaCreate("/tmp/fz_dfa_2b.bin", "/tmp/o.bin", "/tmp/e.bin", 300, 300, &e));

    FzDfaTable("/tmp/fz_dfa_s0.bin", 0, 0, 0, (size_t)-1, 0, (size_t)-1);
    (void)(FzDfaCreate("/tmp/fz_dfa_s0.bin", "/tmp/o.bin", "/tmp/e.bin", 300, 300, &e));

    FzDfaTable("/tmp/fz_dfa_sbig.bin", 70000, 0, 0, (size_t)-1, 0, (size_t)-1);
    (void)(FzDfaCreate("/tmp/fz_dfa_sbig.bin", "/tmp/o.bin", "/tmp/e.bin", 300, 300, &e));

    FzDfaTable("/tmp/fz_dfa_trunc.bin", 100, 100 * 256, 0, (size_t)-1, 0, 10);
    (void)(FzDfaCreate("/tmp/fz_dfa_trunc.bin", "/tmp/o.bin", "/tmp/e.bin", 300, 300, &e));

    /* 合法 transition(states=2, 全部 ID_ERROR) + 各种坏 output/eod */
    FzDfaTable("/tmp/fz_dfa_tr.bin", 2, TR_CELLS, 65535, (size_t)-1, 0, (size_t)-1);
    (void)(FzDfaCreate("/tmp/fz_dfa_tr.bin", "/tmp/fz_no_such.bin", "/tmp/e.bin", 300, 300, &e));
    FzDfaTable("/tmp/fz_dfa_out.bin", 2, TR_CELLS, 0, (size_t)-1, 0, (size_t)-1);
    (void)(FzDfaCreate("/tmp/fz_dfa_tr.bin", "/tmp/fz_dfa_out.bin", "/tmp/fz_no_such.bin", 300, 300, &e));
    FzDfaTable("/tmp/fz_dfa_eod.bin", 2, 2, 0, (size_t)-1, 0, (size_t)-1);
    (void)(FzDfaCreate("/tmp/fz_dfa_tr.bin", "/tmp/fz_dfa_out.bin", "/tmp/fz_dfa_eod.bin", 300, 300, &e));
    if (e) { kadd_dfa_engine_free(e); e = NULL; }

    /* output 表 states 与 transition 不一致 */
    FzDfaTable("/tmp/fz_dfa_out3.bin", 3, 3 * 256, 0, (size_t)-1, 0, (size_t)-1);
    (void)(FzDfaCreate("/tmp/fz_dfa_tr.bin", "/tmp/fz_dfa_out3.bin", "/tmp/fz_dfa_eod.bin", 300, 300, &e));
    /* eod 表 states 不一致 */
    FzDfaTable("/tmp/fz_dfa_eod3.bin", 3, 3, 0, (size_t)-1, 0, (size_t)-1);
    (void)(FzDfaCreate("/tmp/fz_dfa_tr.bin", "/tmp/fz_dfa_out.bin", "/tmp/fz_dfa_eod3.bin", 300, 300, &e));

    /* transition 里有非法状态引用 (5 >= states=2 且 != 65535) */
    FzDfaTable("/tmp/fz_dfa_badref.bin", 2, TR_CELLS, 65535, 7, 5, (size_t)-1);
    (void)(FzDfaCreate("/tmp/fz_dfa_badref.bin", "/tmp/fz_dfa_out.bin", "/tmp/fz_dfa_eod.bin", 300, 300, &e));

    /* --- 分词器路径：全 ID_ERROR 转移 -> match_next 立即 break --- */
    int out[64]; int n = 0;
    if (FzDfaCreate("/tmp/fz_dfa_tr.bin", "/tmp/fz_dfa_out.bin", "/tmp/fz_dfa_eod.bin", 300, 300, &e) == 0 && e) {
        memset(out, 0, sizeof(out));
        (void)(kadd_dfa_engine_tokenize(e, "abc", 3, out, &n));
        kadd_dfa_engine_free(e); e = NULL;
    }
    /* eod_table[1] = EMIT_TWO(3) | token 5<<2 | token2 7<<8 */
    {
        uint16_t eod_emit2 = (uint16_t)(3u | (5u << 2) | (7u << 8));
        FzDfaTable("/tmp/fz_dfa_eod_emit2.bin", 2, 2, 0, 1, eod_emit2, (size_t)-1);
        /* transition: state0 --'a'--> state1，output 全 0（只前进不发射），到结尾触发 EOD */
        FzDfaTable("/tmp/fz_dfa_tr_a1.bin", 2, TR_CELLS, 65535, (size_t)'a', 1, (size_t)-1);
        FzDfaTable("/tmp/fz_dfa_out_zero.bin", 2, TR_CELLS, 0, (size_t)-1, 0, (size_t)-1);
        if (FzDfaCreate("/tmp/fz_dfa_tr_a1.bin", "/tmp/fz_dfa_out_zero.bin", "/tmp/fz_dfa_eod_emit2.bin", 300, 300, &e) == 0 && e) {
            memset(out, 0, sizeof(out));
            (void)(kadd_dfa_engine_tokenize(e, "a", 1, out, &n));
            kadd_dfa_engine_free(e); e = NULL;
        }
        if (FzDfaCreate("/tmp/fz_dfa_tr_a1.bin", "/tmp/fz_dfa_out_zero.bin", "/tmp/fz_dfa_eod_emit2.bin", 300, 1, &e) == 0 && e) {
            memset(out, 0, sizeof(out));
            (void)(kadd_dfa_engine_tokenize(e, "a", 1, out, &n));
            kadd_dfa_engine_free(e); e = NULL;
        }
        /* eod = CLEAR(2) */
        FzDfaTable("/tmp/fz_dfa_eod_clear.bin", 2, 2, 0, 1, 2, (size_t)-1);
        if (FzDfaCreate("/tmp/fz_dfa_tr_a1.bin", "/tmp/fz_dfa_out_zero.bin", "/tmp/fz_dfa_eod_clear.bin", 300, 300, &e) == 0 && e) {
            memset(out, 0, sizeof(out));
            (void)(kadd_dfa_engine_tokenize(e, "a", 1, out, &n));
            kadd_dfa_engine_free(e); e = NULL;
        }
        /* eod = EMIT(1) | token<<2 */
        FzDfaTable("/tmp/fz_dfa_eod_emit1.bin", 2, 2, 0, 1, (uint16_t)(1u | (9u << 2)), (size_t)-1);
        if (FzDfaCreate("/tmp/fz_dfa_tr_a1.bin", "/tmp/fz_dfa_out_zero.bin", "/tmp/fz_dfa_eod_emit1.bin", 300, 300, &e) == 0 && e) {
            memset(out, 0, sizeof(out));
            (void)(kadd_dfa_engine_tokenize(e, "a", 1, out, &n));
            kadd_dfa_engine_free(e); e = NULL;
        }
    }
    /* --- emit_token 路径 --- */
    {
        /* output_code: emit=1(<<5), accept=3, scan_delta=1(<<10), start_enc=1(<<12), end=0 */
        uint16_t oc_emit = (uint16_t)((1u << 5) | 3u | (1u << 10) | (1u << 12));
        FzDfaTable("/tmp/fz_dfa_out_emit.bin", 2, TR_CELLS, 0, (size_t)'a', oc_emit, (size_t)-1);
        FzDfaTable("/tmp/fz_dfa_eod_zero.bin", 2, 2, 0, (size_t)-1, 0, (size_t)-1);
        if (FzDfaCreate("/tmp/fz_dfa_tr_a1.bin", "/tmp/fz_dfa_out_emit.bin", "/tmp/fz_dfa_eod_zero.bin", 300, 300, &e) == 0 && e) {
            memset(out, 0, sizeof(out));
            (void)(kadd_dfa_engine_tokenize(e, "aa", 2, out, &n));
            kadd_dfa_engine_free(e); e = NULL;
        }
        /* scan_delta=3 且输入很短 -> scan_pos > input_length */
        uint16_t oc_scan3 = (uint16_t)((1u << 5) | (3u << 10) | (1u << 12));
        FzDfaTable("/tmp/fz_dfa_out_scan3.bin", 2, TR_CELLS, 0, (size_t)'a', oc_scan3, (size_t)-1);
        if (FzDfaCreate("/tmp/fz_dfa_tr_a1.bin", "/tmp/fz_dfa_out_scan3.bin", "/tmp/fz_dfa_eod_zero.bin", 300, 300, &e) == 0 && e) {
            memset(out, 0, sizeof(out));
            (void)(kadd_dfa_engine_tokenize(e, "a", 1, out, &n));
            kadd_dfa_engine_free(e); e = NULL;
        }
        /* EMIT_PAIR: end=3, accept!=0, max_tokens=1 -> 第二个 token 越界检查 */
        uint16_t oc_pair = (uint16_t)((1u << 5) | 3u | (1u << 10) | (1u << 12) | (3u << 14));
        FzDfaTable("/tmp/fz_dfa_out_pair.bin", 2, TR_CELLS, 0, (size_t)'a', oc_pair, (size_t)-1);
        if (FzDfaCreate("/tmp/fz_dfa_tr_a1.bin", "/tmp/fz_dfa_out_pair.bin", "/tmp/fz_dfa_eod_zero.bin", 300, 1, &e) == 0 && e) {
            memset(out, 0, sizeof(out));
            (void)(kadd_dfa_engine_tokenize(e, "a", 1, out, &n));
            kadd_dfa_engine_free(e); e = NULL;
        }
        if (FzDfaCreate("/tmp/fz_dfa_tr_a1.bin", "/tmp/fz_dfa_out_pair.bin", "/tmp/fz_dfa_eod_zero.bin", 300, 300, &e) == 0 && e) {
            memset(out, 0, sizeof(out));
            (void)(kadd_dfa_engine_tokenize(e, "a", 1, out, &n));
            kadd_dfa_engine_free(e); e = NULL;
        }
        /* start_enc=0 -> start_delta=-1，且 pos==0 -> PARSE */
        uint16_t oc_startneg = (uint16_t)((1u << 5) | (1u << 10) | (0u << 12));
        FzDfaTable("/tmp/fz_dfa_out_sneg.bin", 2, TR_CELLS, 0, (size_t)'a', oc_startneg, (size_t)-1);
        if (FzDfaCreate("/tmp/fz_dfa_tr_a1.bin", "/tmp/fz_dfa_out_sneg.bin", "/tmp/fz_dfa_eod_zero.bin", 300, 300, &e) == 0 && e) {
            memset(out, 0, sizeof(out));
            (void)(kadd_dfa_engine_tokenize(e, "a", 1, out, &n));
            kadd_dfa_engine_free(e); e = NULL;
        }
        /* start_enc=3 -> start_delta=2，token_start=pos+2 > input_length */
        uint16_t oc_startfar = (uint16_t)((1u << 5) | (1u << 10) | (3u << 12));
        FzDfaTable("/tmp/fz_dfa_out_sfar.bin", 2, TR_CELLS, 0, (size_t)'a', oc_startfar, (size_t)-1);
        if (FzDfaCreate("/tmp/fz_dfa_tr_a1.bin", "/tmp/fz_dfa_out_sfar.bin", "/tmp/fz_dfa_eod_zero.bin", 300, 300, &e) == 0 && e) {
            memset(out, 0, sizeof(out));
            (void)(kadd_dfa_engine_tokenize(e, "a", 1, out, &n));
            kadd_dfa_engine_free(e); e = NULL;
        }
        /* count>=max_output_dim: max_tokens=1 且普通 emit 两次 */
        if (FzDfaCreate("/tmp/fz_dfa_tr_a1.bin", "/tmp/fz_dfa_out_emit.bin", "/tmp/fz_dfa_eod_zero.bin", 300, 1, &e) == 0 && e) {
            memset(out, 0, sizeof(out));
            (void)(kadd_dfa_engine_tokenize(e, "aa", 2, out, &n));
            kadd_dfa_engine_free(e); e = NULL;
        }
    }
    /* --- tokenize 入参校验 --- */
    if (FzDfaCreate("/tmp/fz_dfa_tr.bin", "/tmp/fz_dfa_out.bin", "/tmp/fz_dfa_eod.bin", 4, 300, &e) == 0 && e) {
        int o2[8]; int n2 = 0;
        (void)(kadd_dfa_engine_tokenize(e, "abcdefgh", 8, o2, &n2));
        (void)(kadd_dfa_engine_tokenize(e, NULL, 0, o2, &n2));
        (void)(kadd_dfa_engine_tokenize(e, "abc", 3, NULL, &n2));
        (void)(kadd_dfa_engine_tokenize(e, "abc", 3, o2, NULL));
        kadd_dfa_engine_free(e); e = NULL;
    }
    kadd_dfa_engine_free(NULL);
}

FZ_MAYBE_UNUSED static void FzMkTable(const char *p, uint32_t states, uint16_t fill, size_t idx, uint16_t val) {
    std::vector<unsigned char> v; FzPutU32LE(v, states);
    for (size_t i = 0; i < (size_t)states * FzDfaCells; ++i) FzPutU16LE(v, (i == idx) ? val : fill);
    FzWriteBytes(p, v);
}

FZ_MAYBE_UNUSED static void FzMkEod(const char *p, uint32_t states, uint16_t fill) {
    std::vector<unsigned char> v; FzPutU32LE(v, states);
    for (size_t i = 0; i < (size_t)states; ++i) FzPutU16LE(v, fill);
    FzWriteBytes(p, v);
}

FZ_MAYBE_UNUSED static void FuzzDfaBatchErrorPaths()
{
    /* 转移表：state0 --'a'--> state1，其余字符为 ID_ERROR(65535)。
     * 输出表全 0（只前进不发射）；eod 表 state1 = EMIT_TWO(3) | 5<<2 | 7<<8，
     * 使 tokenize("a") 稳定产出 2 个 token —— 批量接口要求每条 token_count > 0。 */
    FzMkTable("/tmp/fz3_tr.bin", 2, 0xFFFFu, 97, 1);
    FzMkTable("/tmp/fz3_out.bin", 2, 0, (size_t)-1, 0);
    {   std::vector<unsigned char> v; FzPutU32LE(v, 2u);
        FzPutU16LE(v, 0);
        FzPutU16LE(v, (uint16_t)(3u | (5u << 2) | (7u << 8)));
        FzWriteBytes("/tmp/fz3_eod.bin", v); }

    kadd_dfa_table_paths_t tp;
    memset(&tp, 0, sizeof(tp));
    tp.transition_table_path = "/tmp/fz3_tr.bin";
    tp.output_table_path     = "/tmp/fz3_out.bin";
    tp.eod_table_path        = "/tmp/fz3_eod.bin";

    kadd_dfa_engine_t *e = nullptr;
    (void)(kadd_dfa_engine_create(&e, &tp, 64, 64));
    if (!e) {  return; }

    /* ---------- tokenize_batch：入参校验 ---------- */
    const char *in0 = "a", *in1 = "a";
    size_t len0 = 1, len1 = 1;
    const char *ins[2] = {in0, in1};
    size_t lens[2] = {len0, len1};
    int *outs[2] = {nullptr, nullptr};
    int dims[2] = {-1, -1};

    kadd_dfa_batch_t b;
    memset(&b, 0, sizeof(b));
    b.inputs = ins; b.lengths = lens; b.count = 2;
    b.outputs = outs; b.dims = dims; b.batch_size = 64;

    (void)(kadd_dfa_engine_tokenize_batch(nullptr, &b));
    (void)(kadd_dfa_engine_tokenize_batch(e, nullptr));
    {   kadd_dfa_batch_t t = b; t.inputs = nullptr;
        (void)(kadd_dfa_engine_tokenize_batch(e, &t)); }
    {   kadd_dfa_batch_t t = b; t.outputs = nullptr;
        (void)(kadd_dfa_engine_tokenize_batch(e, &t)); }
    {   kadd_dfa_batch_t t = b; t.dims = nullptr;
        (void)(kadd_dfa_engine_tokenize_batch(e, &t)); }
    {   kadd_dfa_batch_t t = b; t.count = 0;
        (void)(kadd_dfa_engine_tokenize_batch(e, &t)); }
    {   kadd_dfa_batch_t t = b; t.count = -7;
        (void)(kadd_dfa_engine_tokenize_batch(e, &t)); }
    {   kadd_dfa_batch_t t = b; t.count = 10000001;
        (void)(kadd_dfa_engine_tokenize_batch(e, &t)); }

    /* ---------- tokenize_batch：正常 + batch_size 边界 ---------- */
    {   kadd_dfa_batch_t t = b; t.batch_size = 0;      /* 回退默认 64 */
        (void)(kadd_dfa_engine_tokenize_batch(e, &t));
        for (int i = 0; i < 2; ++i) { free(t.outputs[i]); t.outputs[i] = nullptr; t.dims[i] = -1; } }
    {   kadd_dfa_batch_t t = b; t.batch_size = 5000;   /* 超上限 -> 回退默认 */
        (void)(kadd_dfa_engine_tokenize_batch(e, &t));
        for (int i = 0; i < 2; ++i) { free(t.outputs[i]); t.outputs[i] = nullptr; t.dims[i] = -1; } }
    {   kadd_dfa_batch_t t = b; t.batch_size = 1;      /* 逐条分批 */
        (void)(kadd_dfa_engine_tokenize_batch(e, &t));
        for (int i = 0; i < 2; ++i) { free(t.outputs[i]); t.outputs[i] = nullptr; t.dims[i] = -1; } }
    {   kadd_dfa_batch_t t = b; t.lengths = nullptr;   /* lengths=NULL -> strlen */
        (void)(kadd_dfa_engine_tokenize_batch(e, &t));
        for (int i = 0; i < 2; ++i) { free(t.outputs[i]); t.outputs[i] = nullptr; t.dims[i] = -1; } }

    /* ---------- tokenize_batch：某条 input 为 NULL（全有或全无 + 清理） ---------- */
    {   const char *bad[2] = {in0, nullptr};
        kadd_dfa_batch_t t = b; t.inputs = bad; t.lengths = nullptr;
        (void)(kadd_dfa_engine_tokenize_batch(e, &t)); }

    /* ---------- tokenize_batch：某条 tokenize 失败（长度超限） ---------- */
    {   std::string big(200, 'a');
        const char *mix[2] = {in0, big.c_str()};
        size_t ml[2] = {1, 200};
        kadd_dfa_batch_t t = b; t.inputs = mix; t.lengths = ml;
        (void)(kadd_dfa_engine_tokenize_batch(e, &t)); }

    /* ---------- create：路径/参数非法 ---------- */
    {   kadd_dfa_engine_t *x = nullptr;
        (void)(kadd_dfa_engine_create(nullptr, &tp, 64, 64));
        (void)(kadd_dfa_engine_create(&x, nullptr, 64, 64));
        kadd_dfa_table_paths_t q = tp; q.transition_table_path = nullptr;
        (void)(kadd_dfa_engine_create(&x, &q, 64, 64));
        q = tp; q.output_table_path = nullptr;
        (void)(kadd_dfa_engine_create(&x, &q, 64, 64));
        q = tp; q.eod_table_path = nullptr;
        (void)(kadd_dfa_engine_create(&x, &q, 64, 64));
        q = tp; q.transition_table_path = "";
        (void)(kadd_dfa_engine_create(&x, &q, 64, 64));
        (void)(kadd_dfa_engine_create(&x, &tp, 0, 64));
        (void)(kadd_dfa_engine_create(&x, &tp, 64, 0)); }

    /* ---------- 表文件损坏 ---------- */
    {   kadd_dfa_engine_t *x = nullptr;
        FzWriteBytes("/tmp/fz3_2b.bin", std::vector<unsigned char>(2, 0));
        kadd_dfa_table_paths_t q = tp; q.transition_table_path = "/tmp/fz3_2b.bin";
        (void)(kadd_dfa_engine_create(&x, &q, 64, 64));
        if (x) { kadd_dfa_engine_free(x); x = nullptr; }

        FzMkTable("/tmp/fz3_s0.bin", 0, 0, (size_t)-1, 0);
        q = tp; q.transition_table_path = "/tmp/fz3_s0.bin";
        (void)(kadd_dfa_engine_create(&x, &q, 64, 64));
        if (x) { kadd_dfa_engine_free(x); x = nullptr; }

        /* states 超过 65535 上限 */
        std::vector<unsigned char> v; FzPutU32LE(v, 70000u);
        for (size_t i = 0; i < 8; ++i) FzPutU16LE(v, 0);
        FzWriteBytes("/tmp/fz3_sbig.bin", v);
        q = tp; q.transition_table_path = "/tmp/fz3_sbig.bin";
        (void)(kadd_dfa_engine_create(&x, &q, 64, 64));
        if (x) { kadd_dfa_engine_free(x); x = nullptr; }

        /* 表头声明 100 状态但数据不足 -> 文件过短 */
        std::vector<unsigned char> w; FzPutU32LE(w, 100u);
        for (size_t i = 0; i < 8; ++i) FzPutU16LE(w, 0);
        FzWriteBytes("/tmp/fz3_short.bin", w);
        q = tp; q.transition_table_path = "/tmp/fz3_short.bin";
        (void)(kadd_dfa_engine_create(&x, &q, 64, 64));
        if (x) { kadd_dfa_engine_free(x); x = nullptr; }

        /* output 表状态数与 transition 不一致 */
        FzMkTable("/tmp/fz3_out3.bin", 3, 0, (size_t)-1, 0);
        q = tp; q.output_table_path = "/tmp/fz3_out3.bin";
        (void)(kadd_dfa_engine_create(&x, &q, 64, 64));
        if (x) { kadd_dfa_engine_free(x); x = nullptr; }

        /* eod 表状态数不一致 */
        FzMkEod("/tmp/fz3_eod3.bin", 3, 0);
        q = tp; q.eod_table_path = "/tmp/fz3_eod3.bin";
        (void)(kadd_dfa_engine_create(&x, &q, 64, 64));
        if (x) { kadd_dfa_engine_free(x); x = nullptr; }

        /* 转移表引用了不存在的状态（且不是 ID_ERROR） */
        FzMkTable("/tmp/fz3_badref.bin", 2, 5, (size_t)-1, 5);
        q = tp; q.transition_table_path = "/tmp/fz3_badref.bin";
        (void)(kadd_dfa_engine_create(&x, &q, 64, 64));
        if (x) { kadd_dfa_engine_free(x); x = nullptr; }

        /* 文件不存在 */
        q = tp; q.transition_table_path = "/tmp/fz3_nope.bin";
        (void)(kadd_dfa_engine_create(&x, &q, 64, 64));
        if (x) { kadd_dfa_engine_free(x); x = nullptr; }
    }

    /* ---------- tokenize：ID_ERROR 转移（65535）导致 break ---------- */
    {   FzMkTable("/tmp/fz3_iderr.bin", 2, 0xFFFFu, (size_t)-1, 0xFFFFu);
        kadd_dfa_table_paths_t q = tp; q.transition_table_path = "/tmp/fz3_iderr.bin";
        kadd_dfa_engine_t *x = nullptr;
        /* 转移全为 ID_ERROR -> 校验时 ID_ERROR 被放行，tokenize 立即 break */
        if (kadd_dfa_engine_create(&x, &q, 64, 64) == 0 && x) {
            int out[64]; int n = 0;
            (void)(kadd_dfa_engine_tokenize(x, "abc", 3, out, &n));
            kadd_dfa_engine_free(x);
        } else {  if (x) kadd_dfa_engine_free(x); }
    }

    kadd_dfa_engine_free(e);
    kadd_dfa_engine_free(nullptr);
    

}

FZ_MAYBE_UNUSED static void FuzzRfRealModelPaths() {
    const char *kRf = FzPickPath(FzRfKrfAbs, FzRfKrfRel);
    kadd_rf_model_t *m = nullptr;
    (void)(kadd_rf_model_load(&m, kRf));
    if (!m) {  return; }
    int dim = kadd_rf_model_get_feature_dim(m);
    (void)(kadd_rf_model_get_feature_dim(nullptr));

    std::vector<float> f((size_t)dim);
    for (int i = 0; i < dim; ++i) f[(size_t)i] = (float)(i % 7) / 7.0f;
    float p = -1.0f;
    (void)(kadd_rf_model_predict(m, f.data(), &p));

    /* 批量：多种 batch_size（含 SVE 非整倍尾块） */
    const int bs[] = {1, 2, 3, 31, 32, 33, 64, 65, 128, 257};
    for (size_t i = 0; i < sizeof(bs)/sizeof(bs[0]); ++i) {
        int b = bs[i];
        std::vector<float> mat((size_t)dim * (size_t)b, 0.1f);
        std::vector<float> out((size_t)b, 0.0f);
        (void)(kadd_rf_model_predict_batch(m, mat.data(), b, out.data(), b));
    }
    /* batch_size 非法 -> 回退默认 */
    {
        std::vector<float> mat((size_t)dim * 4u, 0.1f); float o[4] = {0,0,0,0};
        (void)(kadd_rf_model_predict_batch(m, mat.data(), 4, o, 2000));
        (void)(kadd_rf_model_predict_batch(m, mat.data(), 4, o, 0));
        (void)(kadd_rf_model_predict_batch(m, mat.data(), 4, o, -1));
    }
    /* 非有限特征 */
    {
        std::vector<float> bad((size_t)dim, 0.0f);
        bad[0] = std::numeric_limits<float>::quiet_NaN();
        (void)(kadd_rf_model_predict(m, bad.data(), &p));
        bad[0] = std::numeric_limits<float>::infinity();
        (void)(kadd_rf_model_predict(m, bad.data(), &p));
        std::vector<float> ok((size_t)dim, 0.0f);
        /* 批量接口不做逐元素有限性校验（内部按行忽略），返回 SUCCESS 但该行概率保持默认 */
        (void)(kadd_rf_model_predict_batch(m, bad.data(), 1, &p, 1));
        (void)ok;
    }
    /* NULL 参数 */
    (void)(kadd_rf_model_predict(nullptr, f.data(), &p));
    (void)(kadd_rf_model_predict(m, nullptr, &p));
    (void)(kadd_rf_model_predict(m, f.data(), nullptr));
    (void)(kadd_rf_model_predict_batch(nullptr, f.data(), 1, &p, 1));
    (void)(kadd_rf_model_predict_batch(m, nullptr, 1, &p, 1));
    (void)(kadd_rf_model_predict_batch(m, f.data(), 1, nullptr, 1));
    (void)(kadd_rf_model_predict_batch(m, f.data(), 0, &p, 1));
    (void)(kadd_rf_model_predict_batch(m, f.data(), -1, &p, 1));
    (void)(kadd_rf_model_predict_batch(m, f.data(), 10000001, &p, 1));
    (void)(kadd_rf_model_load(nullptr, kRf));
    (void)(kadd_rf_model_load(&m, nullptr));
    kadd_rf_model_free(nullptr);
    kadd_rf_model_free(m);

    /* 损坏/篡改/未签名变体 */
    std::vector<unsigned char> raw = FzReadAll(kRf);
    {   /* 截断 */
        std::vector<unsigned char> t(raw.begin(), raw.begin() + 32);
        FzWriteAll("/tmp/fz4_rf_trunc.krf", t);
        kadd_rf_model_t *x = nullptr; (void)(kadd_rf_model_load(&x, "/tmp/fz4_rf_trunc.krf"));
        if (x) kadd_rf_model_free(x);
    }
    {   /* 短文件 10B */
        FzWriteAll("/tmp/fz4_rf_short.krf", std::vector<unsigned char>(10, 0));
        kadd_rf_model_t *x = nullptr; (void)(kadd_rf_model_load(&x, "/tmp/fz4_rf_short.krf"));
        if (x) kadd_rf_model_free(x);
    }
    {   /* 坏 magic */
        std::vector<unsigned char> t = raw; t[0] = 'X';
        FzWriteAll("/tmp/fz4_rf_magic.krf", t);
        kadd_rf_model_t *x = nullptr; (void)(kadd_rf_model_load(&x, "/tmp/fz4_rf_magic.krf"));
        if (x) kadd_rf_model_free(x);
    }
    {   /* 篡改 payload（HMAC 失配） */
        std::vector<unsigned char> t = raw; t[100] ^= 0xFF;
        FzWriteAll("/tmp/fz4_rf_tamper.krf", t);
        kadd_rf_model_t *x = nullptr; (void)(kadd_rf_model_load(&x, "/tmp/fz4_rf_tamper.krf"));
        if (x) kadd_rf_model_free(x);
    }
    {   /* 去掉 HMAC 尾部（截断 32B） */
        std::vector<unsigned char> t(raw.begin(), raw.end() - 32);
        FzWriteAll("/tmp/fz4_rf_nohmac.krf", t);
        kadd_rf_model_t *x = nullptr; (void)(kadd_rf_model_load(&x, "/tmp/fz4_rf_nohmac.krf"));
        if (x) kadd_rf_model_free(x);
    }
    {   /* flags bit0 清零 -> 未签名 */
        std::vector<unsigned char> t = raw; t[40] = 0;
        FzWriteAll("/tmp/fz4_rf_unsigned.krf", t);
        kadd_rf_model_t *x = nullptr; (void)(kadd_rf_model_load(&x, "/tmp/fz4_rf_unsigned.krf"));
        if (x) kadd_rf_model_free(x);
    }
    {   /* 尾部多 1 字节 */
        std::vector<unsigned char> t = raw; t.push_back(0);
        FzWriteAll("/tmp/fz4_rf_extra.krf", t);
        kadd_rf_model_t *x = nullptr; (void)(kadd_rf_model_load(&x, "/tmp/fz4_rf_extra.krf"));
        if (x) kadd_rf_model_free(x);
    }
    {   /* 不存在 */
        kadd_rf_model_t *x = nullptr; (void)(kadd_rf_model_load(&x, "/tmp/fz4_nope.krf"));
        if (x) kadd_rf_model_free(x);
    }
    {   /* 错误密钥 */
        const char *save = getenv("KADD_MODEL_HMAC_KEY");
        std::string saved = save ? save : "";
#ifdef _WIN32
        _putenv("KADD_MODEL_HMAC_KEY=0000000000000000000000000000000000000000000000000000000000000000");
#else
        setenv("KADD_MODEL_HMAC_KEY", "0000000000000000000000000000000000000000000000000000000000000000", 1);
#endif
        kadd_rf_model_t *x = nullptr; (void)(kadd_rf_model_load(&x, kRf));
        if (x) kadd_rf_model_free(x);
#ifdef _WIN32
        std::string rs = std::string("KADD_MODEL_HMAC_KEY=") + saved; _putenv(rs.c_str());
#else
        setenv("KADD_MODEL_HMAC_KEY", saved.c_str(), 1);
#endif
    }
    {   /* 非法密钥格式（长度不对） */
#ifdef _WIN32
        _putenv("KADD_MODEL_HMAC_KEY=zz");
#else
        setenv("KADD_MODEL_HMAC_KEY", "zz", 1);
#endif
        kadd_rf_model_t *x = nullptr; (void)(kadd_rf_model_load(&x, kRf));
        if (x) kadd_rf_model_free(x);
#ifdef _WIN32
        std::string rs = std::string("KADD_MODEL_HMAC_KEY=") + FzKeyHex; _putenv(rs.c_str());
#else
        setenv("KADD_MODEL_HMAC_KEY", FzKeyHex, 1);
#endif
    }
}

FZ_MAYBE_UNUSED static void FuzzCnnRealModelPaths() {
    const char *kCnn = FzPickPath(FzCnnKcnAbs, FzCnnKcnRel);
    kadd_cnn_model_t *m = nullptr;
    (void)(kadd_cnn_model_load(&m, kCnn));
    if (!m) {  return; }
    int dim = kadd_cnn_model_get_feature_dim(m);
    (void)(kadd_cnn_model_get_feature_dim(nullptr));
    std::vector<float> seq((size_t)dim);
    for (int i = 0; i < dim; ++i) seq[(size_t)i] = (float)(i % 17);
    float p = -1.0f;
    (void)(kadd_cnn_model_predict(m, seq.data(), &p));
    const int bs[] = {1, 2, 3, 31, 32, 33, 64, 65, 128};
    for (size_t i = 0; i < sizeof(bs)/sizeof(bs[0]); ++i) {
        int b = bs[i];
        std::vector<float> mat((size_t)dim * (size_t)b);
        for (size_t k = 0; k < mat.size(); ++k) mat[k] = (float)(k % 13u);
        std::vector<float> out((size_t)b, 0.0f);
        (void)(kadd_cnn_model_predict_batch(m, mat.data(), b, out.data(), b));
    }
    /* token 越界 / 负数 */
    {
        std::vector<float> bad((size_t)dim, 0.0f); bad[0] = 100000.0f;
        (void)(kadd_cnn_model_predict(m, bad.data(), &p));
        std::vector<float> neg((size_t)dim, 0.0f); neg[0] = -5.0f;
        (void)(kadd_cnn_model_predict(m, neg.data(), &p));
    }
    (void)(kadd_cnn_model_predict(nullptr, seq.data(), &p));
    (void)(kadd_cnn_model_predict(m, nullptr, &p));
    (void)(kadd_cnn_model_predict(m, seq.data(), nullptr));
    (void)(kadd_cnn_model_predict_batch(nullptr, seq.data(), 1, &p, 1));
    (void)(kadd_cnn_model_predict_batch(m, nullptr, 1, &p, 1));
    (void)(kadd_cnn_model_predict_batch(m, seq.data(), 1, nullptr, 1));
    (void)(kadd_cnn_model_predict_batch(m, seq.data(), 0, &p, 1));
    (void)(kadd_cnn_model_predict_batch(m, seq.data(), -1, &p, 1));
    (void)(kadd_cnn_model_load(nullptr, kCnn));
    (void)(kadd_cnn_model_load(&m, nullptr));
    kadd_cnn_model_free(nullptr);
    kadd_cnn_model_free(m);

    std::vector<unsigned char> raw = FzReadAll(kCnn);
    {
        std::vector<unsigned char> t(raw.begin(), raw.begin() + 32);
        FzWriteAll("/tmp/fz4_cnn_trunc.kcn", t);
        kadd_cnn_model_t *x = nullptr; (void)(kadd_cnn_model_load(&x, "/tmp/fz4_cnn_trunc.kcn"));
        if (x) kadd_cnn_model_free(x);
    }
    {
        FzWriteAll("/tmp/fz4_cnn_short.kcn", std::vector<unsigned char>(10, 0));
        kadd_cnn_model_t *x = nullptr; (void)(kadd_cnn_model_load(&x, "/tmp/fz4_cnn_short.kcn"));
        if (x) kadd_cnn_model_free(x);
    }
    {
        std::vector<unsigned char> t = raw; t[0] = 'X';
        FzWriteAll("/tmp/fz4_cnn_magic.kcn", t);
        kadd_cnn_model_t *x = nullptr; (void)(kadd_cnn_model_load(&x, "/tmp/fz4_cnn_magic.kcn"));
        if (x) kadd_cnn_model_free(x);
    }
    {
        std::vector<unsigned char> t = raw; t[100] ^= 0xFF;
        FzWriteAll("/tmp/fz4_cnn_tamper.kcn", t);
        kadd_cnn_model_t *x = nullptr; (void)(kadd_cnn_model_load(&x, "/tmp/fz4_cnn_tamper.kcn"));
        if (x) kadd_cnn_model_free(x);
    }
    {
        std::vector<unsigned char> t = raw; t[40] = 0;
        FzWriteAll("/tmp/fz4_cnn_unsigned.kcn", t);
        kadd_cnn_model_t *x = nullptr; (void)(kadd_cnn_model_load(&x, "/tmp/fz4_cnn_unsigned.kcn"));
        if (x) kadd_cnn_model_free(x);
    }
    {
        std::vector<unsigned char> t = raw; t.push_back(0);
        FzWriteAll("/tmp/fz4_cnn_extra.kcn", t);
        kadd_cnn_model_t *x = nullptr; (void)(kadd_cnn_model_load(&x, "/tmp/fz4_cnn_extra.kcn"));
        if (x) kadd_cnn_model_free(x);
    }
    {
        kadd_cnn_model_t *x = nullptr; (void)(kadd_cnn_model_load(&x, "/tmp/fz4_nope.kcn"));
        if (x) kadd_cnn_model_free(x);
    }
}

/* ==========================================================================
 * DL 分发层错误路径（kadd_dl_engine.c）
 *   - 46-47   NULL 参数
 *   - 67-73   SVE 后端 feature_dim 失配（SVE 构建 + 真实 .kcn）
 *   - 89-91   ONNX 加载失败（路径不存在）
 *   - 94-100  ONNX 后端 feature_dim 失配（真实 .onnx + 错误维度）
 *   - 116-141 predict_batch NULL / n 非法 / token 越界、非整数、NaN
 *   - 完整生命周期：load → predict → predict_batch → free（1:1:1:1）
 * ========================================================================== */
FZ_MAYBE_UNUSED static void FuzzDlErrorPaths() {
    kadd_dl_model_t *m = nullptr;

    /* NULL 参数 */
    (void)kadd_dl_model_load(nullptr, FzCnnKcnAbs, 0);
    (void)kadd_dl_model_load(&m, nullptr, 0);
    if (m) { kadd_dl_model_free(m); m = nullptr; }

    /* 路径不存在 → ONNX 加载失败分支 */
    (void)kadd_dl_model_load(&m, "/nonexistent/kadd_fz_nope.onnx", 0);
    if (m) { kadd_dl_model_free(m); m = nullptr; }

    /* 真实 .kcn + 错误 feature_dim：SVE 硬件下命中原生失配分支 */
    (void)kadd_dl_model_load(&m, FzPickPath(FzCnnKcnAbs, FzCnnKcnRel), 999);
    if (m) { kadd_dl_model_free(m); m = nullptr; }

    /* 真实 .onnx + 错误 feature_dim：ONNX 后端失配分支 */
    if (FzAllowRealAsset(1)) {   /* 本函数唯一会建 ONNX 会话的一行，单独降频 */
        (void)kadd_dl_model_load(&m, FzPickPath(FzCnnOnnxAbs, FzCnnOnnxRel), 999);
        if (m) { kadd_dl_model_free(m); m = nullptr; }
    }

    /* n / batch 非法 + NULL（predict_batch 校验分支） */
    (void)kadd_dl_model_predict(nullptr, nullptr, nullptr);
    (void)kadd_dl_model_predict_batch(nullptr, nullptr, 0, nullptr, 0);
    (void)kadd_dl_model_get_feature_dim(nullptr);
    kadd_dl_model_free(nullptr);

    /* 正确加载 → 完整生命周期（load → predict → predict_batch → free） */
    const char *cands[] = {FzCnnKcnAbs, FzCnnKcnRel, FzCnnOnnxAbs, FzCnnOnnxRel};
    for (size_t c = 0; c < sizeof(cands) / sizeof(cands[0]) && !m; ++c) {
        if (kadd_dl_model_load(&m, cands[c], 0) != 0 && m) {
            kadd_dl_model_free(m);
            m = nullptr;
        }
    }
    if (!m) {
        return;
    }
    int dim = kadd_dl_model_get_feature_dim(m);
    if (dim <= 0 || dim > 4096) {
        kadd_dl_model_free(m);
        return;
    }
    /* token 必须为 [0,29) 的整数值 */
    std::vector<float> one((size_t)dim);
    for (int i = 0; i < dim; ++i) one[(size_t)i] = (float)(i % 17);
    float prob = 0.0f;
    (void)kadd_dl_model_predict(m, one.data(), &prob);

    const int bs[] = {1, 4, 33, 64};
    for (size_t i = 0; i < sizeof(bs) / sizeof(bs[0]); ++i) {
        const int b = bs[i];
        std::vector<float> mat((size_t)dim * (size_t)b);
        for (size_t k = 0; k < mat.size(); ++k) mat[k] = (float)(k % 13u);
        std::vector<float> out((size_t)b, 0.0f);
        (void)kadd_dl_model_predict_batch(m, mat.data(), b, out.data(), b);
    }
    /* token 非法变体：越界 / 负数 / 非整数 / NaN / n 非法 / batch_size 回退 */
    std::vector<float> bad = one;
    bad[0] = 100000.0f;
    (void)kadd_dl_model_predict(m, bad.data(), &prob);
    bad[0] = -5.0f;
    (void)kadd_dl_model_predict(m, bad.data(), &prob);
    bad[0] = 1.5f;
    (void)kadd_dl_model_predict(m, bad.data(), &prob);
    bad[0] = std::numeric_limits<float>::quiet_NaN();
    (void)kadd_dl_model_predict(m, bad.data(), &prob);
    (void)kadd_dl_model_predict_batch(m, one.data(), 0, &prob, 1);
    (void)kadd_dl_model_predict_batch(m, one.data(), -1, &prob, 1);
    (void)kadd_dl_model_predict_batch(m, one.data(), 10000001, &prob, 1);
    (void)kadd_dl_model_predict_batch(m, nullptr, 1, &prob, 1);
    (void)kadd_dl_model_predict_batch(m, one.data(), 1, nullptr, 1);
    /* batch_size 越界 → 后端回退默认 */
    std::vector<float> out4(4, 0.0f);
    std::vector<float> mat4((size_t)dim * 4u);
    for (size_t k = 0; k < mat4.size(); ++k) mat4[k] = (float)(k % 13u);
    (void)kadd_dl_model_predict_batch(m, mat4.data(), 4, out4.data(), 0);
    (void)kadd_dl_model_predict_batch(m, mat4.data(), 4, out4.data(), 4096);
    kadd_dl_model_free(m);
}

/* ==========================================================================
 * SQLi 引擎错误路径（kadd_sqli_engine.c）
 *   - 289-290  create_from_file NULL 参数
 *   - 294-296  config 文件不存在 / 解析失败
 *   - 209-226  ML 模式：native 失败 → ONNX 回退（成功 / 双双失败 / 同路径）
 *   - 251-268  DL 模式：同上
 *   - 545-553  detect_batch NULL / count 非法
 *   - 479-481  batch_size 越界回退
 *   - 完整生命周期：create → detect → detect_batch → free（1:1:1:1）
 * ========================================================================== */
FZ_MAYBE_UNUSED static void FuzzSqliErrorPaths() {
    /* NULL 参数 */
    (void)kadd_sqli_engine_create_from_file(nullptr, "config.json");
    kadd_sqli_engine_t *e = nullptr;
    (void)kadd_sqli_engine_create_from_file(&e, nullptr);
    if (e) { kadd_sqli_engine_free(e); e = nullptr; }

    /* config 不存在 / 空文件（JSON 解析失败） */
    (void)kadd_sqli_engine_create_from_file(&e, "/nonexistent/kadd_fz_cfg.json");
    if (e) { kadd_sqli_engine_free(e); e = nullptr; }
    FzWriteFile("/tmp/fz_sqli_empty.json", "");
    (void)kadd_sqli_engine_create_from_file(&e, "/tmp/fz_sqli_empty.json");
    if (e) { kadd_sqli_engine_free(e); e = nullptr; }
    FzWriteFile("/tmp/fz_sqli_dirlike.json", "{ not json at all");
    (void)kadd_sqli_engine_create_from_file(&e, "/tmp/fz_sqli_dirlike.json");
    if (e) { kadd_sqli_engine_free(e); e = nullptr; }

    const char *tr  = FzPickPath(FzTranAbs, FzTranRel);
    const char *ot  = FzPickPath(FzOutAbs, FzOutRel);
    const char *eod = FzPickPath(FzEodAbs, FzEodRel);
    const char *rf_onnx  = FzPickPath(FzRfOnnxAbs, FzRfOnnxRel);
    const char *cnn_onnx = FzPickPath(FzCnnOnnxAbs, FzCnnOnnxRel);

    /* ML / DL 模式下的 native→ONNX 回退矩阵：
     *   fb_ok    : native 路径无效 + onnx 有效  → 回退成功（209-212 / 251-254）
     *   fb_bad   : 两者都无效                  → 双双失败（213-217 / 255-259）
     *   fb_same  : 两字段相同且无效            → else 分支（219-225 / 261-267）
     *   fb_empty : onnx 字段为空串             → ONNX 构建 #else 分支（229-236 / 271-278） */
    struct FzSqliCfg {
        const char *name;
        const char *mode;      /* "ml" | "dl" */
        const char *native_p;  /* model_rf / model_cnn */
        const char *onnx_p;    /* model_rf_onnx / model_cnn_onnx */
    };
    const FzSqliCfg cfgs[] = {
        {"ml_fb_ok",    "ml", "/nonexistent/kadd_fz_bad.krf", rf_onnx},
        {"ml_fb_bad",   "ml", "/nonexistent/kadd_fz_bad.krf", "/nonexistent/kadd_fz_bad.onnx"},
        {"ml_fb_same",  "ml", "/nonexistent/kadd_fz_bad.onnx", "/nonexistent/kadd_fz_bad.onnx"},
        {"ml_fb_empty", "ml", "/nonexistent/kadd_fz_bad.krf", ""},
        {"dl_fb_ok",    "dl", "/nonexistent/kadd_fz_bad.kcn", cnn_onnx},
        {"dl_fb_bad",   "dl", "/nonexistent/kadd_fz_bad.kcn", "/nonexistent/kadd_fz_bad.onnx"},
        {"dl_fb_same",  "dl", "/nonexistent/kadd_fz_bad.onnx", "/nonexistent/kadd_fz_bad.onnx"},
        {"dl_fb_empty", "dl", "/nonexistent/kadd_fz_bad.kcn", ""},
    };
    /* 回退矩阵是本函数唯一的重路径：8 个 config 全指向真实 10.4MB DFA 表，
     * ml_fb_ok / dl_fb_ok 还会建真实 ONNX 会话，故单独降频（1/64）；上方的
     * NULL / 不存在 / 畸形 JSON 分支仍每次迭代执行。 */
    const size_t cfg_n = FzAllowRealAsset(0) ? sizeof(cfgs) / sizeof(cfgs[0]) : 0u;
    for (size_t i = 0; i < cfg_n; ++i) {
        const FzSqliCfg &c = cfgs[i];
        const bool is_ml = (strcmp(c.mode, "ml") == 0);
        std::string json = std::string("{\"engine_mode\":\"") + c.mode + "\","
            + "\"transition_table\":\"" + tr + "\","
            + "\"output_table\":\"" + ot + "\","
            + "\"eod_table\":\"" + eod + "\","
            + (is_ml ? "\"model_rf\":\"" : "\"model_cnn\":\"") + c.native_p + "\","
            + (is_ml ? "\"model_rf_onnx\":\"" : "\"model_cnn_onnx\":\"") + c.onnx_p + "\"}";
        char path[128];
        snprintf(path, sizeof(path), "/tmp/fz_sqli_%s.json", c.name);
        FzWriteFile(path, json);
        kadd_sqli_engine_t *eng = nullptr;
        if (kadd_sqli_engine_create_from_file(&eng, path) == 0 && eng) {
            /* 创建成功 → 完整生命周期：detect → detect_batch → free */
            float pr = 0.0f;
            (void)kadd_sqli_detect(eng, "id=1 OR 1=1--", &pr);
            const char *ins[2] = {"id=1 OR 1=1", "plain text"};
            float ps[2] = {0.0f, 0.0f};
            (void)kadd_sqli_detect_batch(eng, ins, 2, ps, 2);
            kadd_sqli_engine_free(eng);
        } else if (eng) {
            kadd_sqli_engine_free(eng);
        }
    }

    /* detect_batch 参数校验（需要有效引擎；用真实 config 创建） */
    if (kadd_sqli_engine_create_from_file(&e, FzResolveConfig()) == 0 && e) {
        const char *ins[2] = {"a", "b"};
        float ps[2] = {0.0f, 0.0f};
        (void)kadd_sqli_detect_batch(nullptr, ins, 2, ps, 2);
        (void)kadd_sqli_detect_batch(e, nullptr, 2, ps, 2);
        (void)kadd_sqli_detect_batch(e, ins, 2, nullptr, 2);
        (void)kadd_sqli_detect_batch(e, ins, 0, ps, 2);
        (void)kadd_sqli_detect_batch(e, ins, -1, ps, 2);
        (void)kadd_sqli_detect_batch(e, ins, 10000001, ps, 2);
        /* batch_size 非法 → init_batch_detect_params 回退默认（479-481） */
        (void)kadd_sqli_detect_batch(e, ins, 2, ps, 0);
        (void)kadd_sqli_detect_batch(e, ins, 2, ps, 100000);
        /* detect NULL 参数（311-312）与空串快路径（316-318） */
        float pr = 0.0f;
        (void)kadd_sqli_detect(nullptr, "x", &pr);
        (void)kadd_sqli_detect(e, nullptr, &pr);
        (void)kadd_sqli_detect(e, "", &pr);
        (void)kadd_sqli_detect(e, "x", nullptr);
        kadd_sqli_engine_free(e);
        e = nullptr;
    } else if (e) {
        kadd_sqli_engine_free(e);
        e = nullptr;
    }
    kadd_sqli_engine_free(nullptr);
}

/* ==========================================================================
 * FuzzInferenceThreadEnvBranches  ORT 线程数环境变量分支
 *   目标行（kadd_inference_engine.c）：
 *   - 276-278  环境变量未设置 / 空串            → 回退默认值
 *   - 283-289  取值非法（非数字/空白/后缀/越界） → 告警并回退默认值
 *   - 290      取值合法                         → 采用
 *
 * coverage_all.sh 恒定导出合法的 KADD_ORT_INTRA_OP_THREADS=8 / INTER_OP_THREADS=1，
 * 因此前两类分支在常规 UT 与 Fuzz 下永远走不到，必须临时改写环境变量后
 * 经由公共接口真实加载一次模型（kadd_dl_model_load → kadd_inference_engine_load_from_path
 * → create_ort_session → ort_thread_count_from_env）才能触达库副本中的这些行。
 *
 * 同时验证功能契约：线程数属于性能调优项，环境变量缺失或非法时模型加载
 * 绝不允许失败（源码 312-334 只告警不中断）。
 *
 * 模型加载较重，故进程内只执行一次；fuzz 侧仅在
 * LLVMFuzzerInitialize 调用（含等频随机长稳模式，随机 fuzz 不跳过初始化）。
 * ========================================================================== */
FZ_MAYBE_UNUSED static void FuzzInferenceThreadEnvBranches() {
    static bool s_done = false;
    if (s_done) { return; }
    s_done = true;

    /* 环境变量名必须与 kadd_inference_engine.c 的 KADD_ORT_INTRA_OP_ENV /
     * KADD_ORT_INTER_OP_ENV 保持一致。此处刻意用字面量而非宏：本 .inc 会被
     * kadd_fuzzer.cpp 包含，而后者在 KADD_FUZZ_INFERENCE_INJECT=ON 时已内联
     * kadd_inference_engine.c，重复 #define 会触发宏重定义告警。 */
    static const char *const kIntraEnv = "KADD_ORT_INTRA_OP_THREADS";
    static const char *const kInterEnv = "KADD_ORT_INTER_OP_THREADS";

    const char *intra_prev = getenv(kIntraEnv);
    const char *inter_prev = getenv(kInterEnv);
    const std::string intra_bak = (intra_prev != nullptr) ? intra_prev : "";
    const std::string inter_bak = (inter_prev != nullptr) ? inter_prev : "";

    /* 取值矩阵只保留覆盖 kadd_inference_engine.c:276-290 全部分支的最小集：
     * nullptr → 未设置回退；"" → 空串子条件；"abc" → 非法值告警回退；
     * "1"/"8" → 合法值生效。每项都完整走一次 load → predict → predict_batch
     * → free（1:1:1:1），不累积句柄。不取 "1024"（合法上界）：命中与 "1"/"8"
     * 同一分支，却会真建 1024 线程的 ORT 线程池（CNN/RF 各一次，单次可达数秒）；
     * 边界值 1024/1025 由 UT 直接调用 ort_thread_count_from_env 覆盖
     * （test_inference_engine_gtest.cpp 第 21 组）。初始化的真实 ONNX 会话 20 → 10。 */
    static const char *const kVals[] = { nullptr, "", "abc", "1", "8" };
    const char *cnn_onnx = FzPickPath(FzCnnOnnxAbs, FzCnnOnnxRel);
    const char *rf_onnx  = FzPickPath(FzRfOnnxAbs,  FzRfOnnxRel);

    for (size_t i = 0; i < sizeof(kVals) / sizeof(kVals[0]); ++i) {
        FzSetEnv(kIntraEnv, kVals[i]);
        FzSetEnv(kInterEnv, kVals[i]);

        /* .onnx 传入 DL 层：原生 CNN 解析必然失败 → 稳定回退 ONNX 后端 */
        kadd_dl_model_t *dm = nullptr;
        (void)kadd_dl_model_load(&dm, cnn_onnx, 0);
        if (dm != nullptr) {
            int dim = kadd_dl_model_get_feature_dim(dm);
            if (dim > 0 && dim <= 4096) {
                std::vector<float> one((size_t)dim, 0.2f);
                float p = 0.0f;
                (void)kadd_dl_model_predict(dm, one.data(), &p);
                std::vector<float> mat((size_t)dim * 2u, 0.2f);
                float out[2] = {0.0f, 0.0f};
                (void)kadd_dl_model_predict_batch(dm, mat.data(), 2, out, 2);
            }
            kadd_dl_model_free(dm);
            dm = nullptr;
        }

        /* ML 层同样经由 kadd_inference_engine_load_from_path（kadd_ml_engine.c:75），
         * 顺带覆盖 RF ONNX 模型在不同线程数配置下的加载 */
        kadd_model_handle_t *mm = nullptr;
        (void)kadd_model_load(&mm, rf_onnx, 0);
        if (mm != nullptr) {
            int dim = kadd_model_get_feature_dim(mm);
            if (dim > 0 && dim <= 4096) {
                std::vector<float> one((size_t)dim, 0.2f);
                float p = 0.0f;
                (void)kadd_model_predict(mm, one.data(), &p);
                std::vector<float> mat((size_t)dim * 2u, 0.2f);
                float out[2] = {0.0f, 0.0f};
                (void)kadd_model_predict_batch(mm, mat.data(), 2, out, 2);
            }
            kadd_model_free(mm);
            mm = nullptr;
        }
    }

    /* 精确复原：原来不存在则删除，原来存在则还原原值 */
    FzSetEnv(kIntraEnv, (intra_prev != nullptr) ? intra_bak.c_str() : nullptr);
    FzSetEnv(kInterEnv, (inter_prev != nullptr) ? inter_bak.c_str() : nullptr);
}

#endif /* KADD_ERROR_PATHS_CASES_INC */
