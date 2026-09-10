/* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved. */
/**
 * @file kadd_inference_fault_inject.h
 * @brief kadd_inference_engine.c 的 ONNX Runtime 故障注入用例（UT 与 FUZZ 共用）
 *
 * 背景（覆盖率）
 * -------------
 * kadd_inference_engine.c 里约 90 行是「ORT API 返回错误状态之后的清理分支」
 * （check_ort_status / get_model_input_dim / get_model_output_dim 的 goto cleanup、
 *   ReleaseStatus、ReleaseTypeInfo 等）。真实模型正常加载时这些分支永远走不到，
 * 只有能让 OrtApi 函数表中的某个成员返回错误状态才可达。
 *
 * 实现方式
 * -------
 * 1. 使用方必须先 `#include "kadd_inference_engine.c"`（本头要访问其内部结构与
 *    static 函数），再包含本头文件。test_inference_engine_gtest.cpp 与
 *    tests/fuzz/kadd_fuzzer.cpp 都是这么做的。
 * 2. 复制一份真实的 OrtApi 函数表，只替换「要注入失败」的那一个成员，其余成员
 *    仍指向 ONNX Runtime 的真实实现。这样 ReleaseStatus / GetErrorMessage 等
 *    清理函数作用在真实的 OrtStatus 上，不需要为它们手写桩函数，
 *    也就不存在桩函数签名与 ORT 头文件不一致的编译风险。
 * 3. 错误状态本身由真实 ORT 产生：对一个必然不存在的模型路径调用 CreateSession，
 *    ORT 一定返回非 NULL 的 OrtStatus*。
 * 4. 桩函数的签名与 test_inference_engine_gtest.cpp 中已在编译的 Fake* 系列
 *    逐字一致（含 noexcept），因此不会引入新的签名假设。
 *
 * 安全约束
 * -------
 * 注入场景绝不把伪造的 session/type_info 指针交给真实 ORT 函数：所有会解引用
 * 这些指针的成员都已被替换为桩函数，真实实现只处理真实的 OrtStatus/OrtSessionOptions。
 */
#ifndef KADD_INFERENCE_FAULT_INJECT_H
#define KADD_INFERENCE_FAULT_INJECT_H

#include <cstring>

/* 注入场景编号（0 保留为「不注入」） */
enum KaddInjScenario {
    KADD_INJ_NONE = 0,
    KADD_INJ_CHECK_STATUS,        /* check_ort_status 的错误分支 */
    KADD_INJ_INPUT_TYPE_INFO,     /* SessionGetInputTypeInfo 返回错误 */
    KADD_INJ_CAST_INPUT,          /* CastTypeInfoToTensorInfo 返回错误（输入维度） */
    KADD_INJ_DIM_COUNT_INPUT,     /* GetDimensionsCount 返回错误 */
    KADD_INJ_DIM_COUNT_TOO_BIG,   /* 维度数超过 MAX_TENSOR_DIMS */
    KADD_INJ_DIMS_INPUT,          /* GetDimensions 返回错误 */
    KADD_INJ_INPUT_DIM_OK,        /* 输入维度探测成功路径 */
    KADD_INJ_OUTPUT_TYPE_INFO,    /* SessionGetOutputTypeInfo 返回错误 */
    KADD_INJ_CAST_OUTPUT,         /* CastTypeInfoToTensorInfo 返回错误（输出维度） */
    KADD_INJ_OUTPUT_DIM_OK,       /* 输出维度探测成功路径 */
    KADD_INJ_CREATE_OPTIONS_FAIL, /* create_ort_session: CreateSessionOptions 失败（297-300） */
    KADD_INJ_EXEC_MODE_FAIL,      /* create_ort_session: SetSessionExecutionMode 失败（302-305） */
    KADD_INJ_GRAPH_OPT_FAIL,      /* create_ort_session: SetSessionGraphOptimizationLevel 失败（307-310） */
    KADD_INJ_INTRA_THREADS_FAIL,  /* create_ort_session: SetIntraOpNumThreads 告警分支（319-323） */
    KADD_INJ_INTER_THREADS_FAIL,  /* create_ort_session: SetInterOpNumThreads 告警分支（329-333） */
    KADD_INJ_MEMORY_INFO_FAIL,    /* create_ort_session: CreateMemoryInfo 失败 → cleanup_session（343-352） */
    KADD_INJ_ALLOCATOR_FAIL,      /* get_io_name: GetAllocatorWithDefaultOptions 失败（126-128） */
    KADD_INJ_OUTPUT_COUNT_FAIL,   /* get_io_name: SessionGetOutputCount 失败（135-137） */
    KADD_INJ_INPUT_NAME_FAIL,     /* get_io_name: SessionGetInputName 失败（143-145） */
    KADD_INJ_CREATE_TENSOR_FAIL,  /* predict_batch: CreateTensorWithDataAsOrtValue 失败（512-514） */
    KADD_INJ_RUN_FAIL,            /* predict_batch: Run 失败（520-526） */
    KADD_INJ_GET_DATA_FAIL,       /* predict_batch: GetTensorMutableData 失败（528-533） */
    KADD_INJ_OUTPUT_DIM_SMALL,    /* predict_batch: output_dim<2 防御（535-541） */
    KADD_INJ_IO_NAME_NULL,        /* get_io_name: status 成功但 name 为空的防御分支（151-153） */
    KADD_INJ_ALLOCATOR_FREE_FAIL, /* get_io_name: AllocatorFree 返回错误状态（160-163） */
    KADD_INJ_CREATE_ENV_FAIL,     /* init_ort_env CreateEnv 失败（110-113）+ load_engine_internal 上抛（395-398） */
    KADD_INJ_SCENARIO_COUNT
};

namespace kadd_inject {

/* 真实 ORT 对象（进程内只建一次） */
inline const OrtApi *&RealApi() { static const OrtApi *p = nullptr; return p; }
inline OrtEnv *&Env() { static OrtEnv *p = nullptr; return p; }

/* 取真实 OrtApi；失败返回 nullptr */
inline const OrtApi *AcquireRealApi()
{
    if (RealApi() != nullptr) {
        return RealApi();
    }
    const OrtApiBase *base = OrtGetApiBase();
    if (base == nullptr) {
        return nullptr;
    }
    RealApi() = base->GetApi(ORT_API_VERSION);
    return RealApi();
}

/**
 * @brief 用真实 ORT 产生一个错误状态（调用方负责确保它最终被 ReleaseStatus 释放）
 */
inline OrtStatus *MakeRealErrorStatus()
{
    const OrtApi *api = AcquireRealApi();
    if (api == nullptr) {
        return nullptr;
    }
    if (Env() == nullptr) {
        OrtEnv *env = nullptr;
        if (api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "kadd-inject", &env) != nullptr) {
            return nullptr;
        }
        Env() = env;
    }
    OrtSessionOptions *options = nullptr;
    if (api->CreateSessionOptions(&options) != nullptr || options == nullptr) {
        return nullptr;
    }
    /* [PERF] 不设线程数时 ORT 默认 intra-op = 核数（高核机可达 256），
     * CreateSession 会先建满线程池再因模型不存在而失败。本函数每个注入场景
     * 都调一次、每 256 迭代一轮 26 个场景 → 周期性数千次线程创建/销毁。
     * 注入只需“一个真实错误状态”，线程数与覆盖目标无关，置 1 后代价归零。 */
    (void)api->SetIntraOpNumThreads(options, 1);
    (void)api->SetInterOpNumThreads(options, 1);
    OrtSession *session = nullptr;
    /* 该路径必然不存在 -> ORT 返回错误状态 */
    OrtStatus *err = api->CreateSession(
        Env(), "kadd_inject_this_model_must_not_exist.onnx", options, &session);
    api->ReleaseSessionOptions(options);
    if (session != nullptr) {
        api->ReleaseSession(session);
    }
    return err;
}

/* ---- 桩函数：签名与 test_inference_engine_gtest.cpp 中的 Fake* 系列一致 ---- */

/* 待返回的错误状态，由每个场景独立设置（用完即被真实 ReleaseStatus 释放） */
inline OrtStatus *&InjectedError() { static OrtStatus *p = nullptr; return p; }

inline OrtStatus *FailSessionGetInputTypeInfo(const OrtSession *, size_t,
                                              OrtTypeInfo **) noexcept
{
    return InjectedError();
}
inline OrtStatus *FailSessionGetOutputTypeInfo(const OrtSession *, size_t,
                                               OrtTypeInfo **) noexcept
{
    return InjectedError();
}
inline OrtStatus *FailCastTypeInfoToTensorInfo(const OrtTypeInfo *,
                                               const OrtTensorTypeAndShapeInfo **) noexcept
{
    return InjectedError();
}
inline OrtStatus *FailGetDimensionsCount(const OrtTensorTypeAndShapeInfo *, size_t *) noexcept
{
    return InjectedError();
}
inline OrtStatus *FailGetDimensions(const OrtTensorTypeAndShapeInfo *, int64_t *, size_t) noexcept
{
    return InjectedError();
}

inline OrtStatus *OkSessionGetInputTypeInfo(const OrtSession *, size_t,
                                            OrtTypeInfo **type_info) noexcept
{
    /* 伪指针：只有被替换过的桩函数会看到它，真实 ORT 永远不会解引用 */
    *type_info = reinterpret_cast<OrtTypeInfo *>(0x1);
    return nullptr;
}
inline OrtStatus *OkSessionGetOutputTypeInfo(const OrtSession *, size_t,
                                             OrtTypeInfo **type_info) noexcept
{
    *type_info = reinterpret_cast<OrtTypeInfo *>(0x1);
    return nullptr;
}
inline OrtStatus *OkCastTypeInfoToTensorInfo(const OrtTypeInfo *,
                                             const OrtTensorTypeAndShapeInfo **tensor_info) noexcept
{
    *tensor_info = reinterpret_cast<const OrtTensorTypeAndShapeInfo *>(0x2);
    return nullptr;
}
inline OrtStatus *TwoGetDimensionsCount(const OrtTensorTypeAndShapeInfo *,
                                        size_t *dim_count) noexcept
{
    *dim_count = 2U;
    return nullptr;
}
inline OrtStatus *TooManyGetDimensionsCount(const OrtTensorTypeAndShapeInfo *,
                                            size_t *dim_count) noexcept
{
    *dim_count = MAX_TENSOR_DIMS + 1U;
    return nullptr;
}
inline OrtStatus *SeqGetDimensions(const OrtTensorTypeAndShapeInfo *,
                                   int64_t *dims, size_t dim_count) noexcept
{
    for (size_t i = 0; i < dim_count; ++i) {
        dims[i] = static_cast<int64_t>(i) + 2;   /* dims[1] = 3 -> feature_dim/output_dim = 3 */
    }
    return nullptr;
}
/* 伪 type_info 不能交给真实 ReleaseTypeInfo，必须置空实现 */
inline void NoopReleaseTypeInfo(OrtTypeInfo *) noexcept {}

/* ---- create_ort_session 场景桩（签名与 OrtApi v1 成员逐一一致） ---- */
inline OrtStatus *FailCreateSessionOptions(OrtSessionOptions **) noexcept
{
    return InjectedError();
}
inline OrtStatus *FailSetSessionExecutionMode(OrtSessionOptions *, ExecutionMode) noexcept
{
    return InjectedError();
}
inline OrtStatus *FailSetSessionGraphOptimizationLevel(OrtSessionOptions *,
                                                       GraphOptimizationLevel) noexcept
{
    return InjectedError();
}
inline OrtStatus *FailSetIntraOpNumThreads(OrtSessionOptions *options, int) noexcept
{
    /* [PERF] 本桩返回错误后，create_ort_session 的 intra 保持 ORT 默认值 = 核数，
     * 随后的真实 CreateSession 会先建满线程池再失败。用真实 API 修正为 1：
     * 仍返回错误状态，告警分支（319-323）覆盖目标不变，但不再扩线程。 */
    const OrtApi *real = AcquireRealApi();
    if (real != nullptr && options != nullptr) {
        (void)real->SetIntraOpNumThreads(options, 1);
    }
    return InjectedError();
}
inline OrtStatus *FailSetInterOpNumThreads(OrtSessionOptions *options, int) noexcept
{
    /* [PERF] 同上：修正为 1，避免默认 inter-op 线程数在真实 CreateSession 中扩线程 */
    const OrtApi *real = AcquireRealApi();
    if (real != nullptr && options != nullptr) {
        (void)real->SetInterOpNumThreads(options, 1);
    }
    return InjectedError();
}
inline OrtStatus *OkCreateSession(const OrtEnv *, const char *,
                                  const OrtSessionOptions *, OrtSession **out) noexcept
{
    /* 伪 session 指针：只有被替换过的 ReleaseSession 桩会看到它，真实 ORT 永不解引用 */
    *out = reinterpret_cast<OrtSession *>(0x1);
    return nullptr;
}
inline OrtStatus *FailCreateMemoryInfo(const char *, OrtAllocatorType, int,
                                       OrtMemType, OrtMemoryInfo **) noexcept
{
    return InjectedError();
}
inline void NoopReleaseSession(OrtSession *) noexcept {}

/* ---- get_io_name 场景桩 ---- */
inline OrtStatus *FailGetAllocatorWithDefaultOptions(OrtAllocator **) noexcept
{
    return InjectedError();
}
inline OrtStatus *FailSessionGetOutputCount(const OrtSession *, size_t *) noexcept
{
    return InjectedError();
}
inline OrtStatus *FailSessionGetInputName(const OrtSession *, size_t,
                                          OrtAllocator *, char **) noexcept
{
    return InjectedError();
}
/* status 成功但 name 为空：ORT 契约不会出现，专门用于验证 white_box_test_047
 * 加固后的 name==NULL 防御分支确实可达且不会解引用空指针 */
inline OrtStatus *OkSessionGetInputNameNull(const OrtSession *, size_t,
                                            OrtAllocator *, char **value) noexcept
{
    *value = nullptr;
    return nullptr;
}
/* status 成功且 name 非空：交给被测代码的 strdup / AllocatorFree 处理 */
inline OrtStatus *OkSessionGetInputNameDup(const OrtSession *, size_t,
                                           OrtAllocator *, char **value) noexcept
{
    static const char kName[] = "kadd_inject_input_name";
    *value = strdup(kName);
    return (*value != nullptr) ? nullptr : InjectedError();
}
/* AllocatorFree 返回错误状态（仍然释放内存，避免长稳 fuzz 下累积泄漏） */
inline OrtStatus *FailAllocatorFree(OrtAllocator *, void *p) noexcept
{
    free(p);
    return InjectedError();
}
/* init_ort_env: CreateEnv 失败（错误状态由 check_ort_status 消费） */
inline OrtStatus *FailCreateEnv(OrtLoggingLevel, const char *, OrtEnv **) noexcept
{
    return InjectedError();
}

/* ---- predict_batch 场景桩 ---- */
inline OrtStatus *FailCreateTensorWithData(const OrtMemoryInfo *, void *, size_t,
                                           const int64_t *, size_t,
                                           ONNXTensorElementDataType, OrtValue **) noexcept
{
    return InjectedError();
}
inline OrtStatus *OkCreateTensorWithData(const OrtMemoryInfo *, void *, size_t,
                                         const int64_t *, size_t,
                                         ONNXTensorElementDataType, OrtValue **out) noexcept
{
    *out = reinterpret_cast<OrtValue *>(0x1);   /* 伪 input value，仅桩函数可见 */
    return nullptr;
}
inline OrtStatus *FailRun(OrtSession *, const OrtRunOptions *, const char *const *,
                          const OrtValue *const *, size_t, const char *const *,
                          size_t, OrtValue **) noexcept
{
    return InjectedError();
}
inline OrtStatus *OkRun(OrtSession *, const OrtRunOptions *, const char *const *,
                        const OrtValue *const *, size_t, const char *const *,
                        size_t, OrtValue **output_values) noexcept
{
    *output_values = reinterpret_cast<OrtValue *>(0x2);   /* 伪 output value，仅桩函数可见 */
    return nullptr;
}
inline OrtStatus *FailGetTensorMutableData(OrtValue *, void **) noexcept
{
    return InjectedError();
}
inline OrtStatus *OkGetTensorMutableData(OrtValue *, void **out) noexcept
{
    static float buf[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    *out = buf;
    return nullptr;
}
inline void NoopReleaseValue(OrtValue *) noexcept {}

/* predict 场景的伪引擎准备：feature_dim=2、IO 名称为静态串，
 * session/memory_info 为伪指针——所有会解引用它们的成员都已被桩替换 */
inline void PreparePredictEngine(kadd_inference_engine_t *engine)
{
    static char kInputName[] = "input";
    static char kOutputName[] = "output";
    engine->feature_dim = 2;
    engine->output_dim = 2;
    engine->input_name = kInputName;
    engine->output_name = kOutputName;
    engine->memory_info = reinterpret_cast<OrtMemoryInfo *>(0x4);
}

inline int PredictOnce(kadd_inference_engine_t *engine)
{
    const float feats[2] = {0.25f, 0.75f};
    float prob = 0.0f;
    return kadd_inference_predict_batch(engine, feats, 1, &prob, 1);
}

/**
 * @brief 执行一个注入场景
 * @return 0 = 已执行；-1 = 环境不可用（ORT 未就绪），调用方应跳过
 */
inline int RunScenario(int scenario)
{
    if (scenario <= KADD_INJ_NONE || scenario >= KADD_INJ_SCENARIO_COUNT) {
        return -1;
    }
    const OrtApi *real = AcquireRealApi();
    if (real == nullptr) {
        return -1;
    }
    OrtStatus *err = MakeRealErrorStatus();
    if (err == nullptr) {
        return -1;
    }
    InjectedError() = err;

    OrtApi api = *real;                       /* 其余成员保持真实实现 */
    api.ReleaseTypeInfo = NoopReleaseTypeInfo;

    kadd_inference_engine_t engine;
    std::memset(&engine, 0, sizeof(engine));
    engine.api = &api;
    engine.session = reinterpret_cast<OrtSession *>(0x1);
    engine.env = Env();
    engine.feature_dim = 28;
    engine.output_dim = 2;

    bool consumed = true;                     /* err 是否会被被测代码 ReleaseStatus 掉 */
    switch (scenario) {
        case KADD_INJ_CHECK_STATUS:
            (void)check_ort_status(&api, err, "kadd-fault-inject");
            break;
        case KADD_INJ_INPUT_TYPE_INFO:
            api.SessionGetInputTypeInfo = FailSessionGetInputTypeInfo;
            (void)get_model_input_dim(&engine);
            break;
        case KADD_INJ_CAST_INPUT:
            api.SessionGetInputTypeInfo = OkSessionGetInputTypeInfo;
            api.CastTypeInfoToTensorInfo = FailCastTypeInfoToTensorInfo;
            (void)get_model_input_dim(&engine);
            break;
        case KADD_INJ_DIM_COUNT_INPUT:
            api.SessionGetInputTypeInfo = OkSessionGetInputTypeInfo;
            api.CastTypeInfoToTensorInfo = OkCastTypeInfoToTensorInfo;
            api.GetDimensionsCount = FailGetDimensionsCount;
            (void)get_model_input_dim(&engine);
            break;
        case KADD_INJ_DIM_COUNT_TOO_BIG:
            api.SessionGetInputTypeInfo = OkSessionGetInputTypeInfo;
            api.CastTypeInfoToTensorInfo = OkCastTypeInfoToTensorInfo;
            api.GetDimensionsCount = TooManyGetDimensionsCount;
            api.GetDimensions = SeqGetDimensions;   /* 不应被调用 */
            consumed = false;                        /* status 为 NULL，cleanup 不释放 */
            (void)get_model_input_dim(&engine);
            break;
        case KADD_INJ_DIMS_INPUT:
            api.SessionGetInputTypeInfo = OkSessionGetInputTypeInfo;
            api.CastTypeInfoToTensorInfo = OkCastTypeInfoToTensorInfo;
            api.GetDimensionsCount = TwoGetDimensionsCount;
            api.GetDimensions = FailGetDimensions;
            (void)get_model_input_dim(&engine);
            break;
        case KADD_INJ_INPUT_DIM_OK:
            api.SessionGetInputTypeInfo = OkSessionGetInputTypeInfo;
            api.CastTypeInfoToTensorInfo = OkCastTypeInfoToTensorInfo;
            api.GetDimensionsCount = TwoGetDimensionsCount;
            api.GetDimensions = SeqGetDimensions;
            consumed = false;
            (void)get_model_input_dim(&engine);
            break;
        case KADD_INJ_OUTPUT_TYPE_INFO:
            api.SessionGetOutputTypeInfo = FailSessionGetOutputTypeInfo;
            get_model_output_dim(&engine);
            break;
        case KADD_INJ_CAST_OUTPUT:
            api.SessionGetOutputTypeInfo = OkSessionGetOutputTypeInfo;
            api.CastTypeInfoToTensorInfo = FailCastTypeInfoToTensorInfo;
            get_model_output_dim(&engine);
            break;
        case KADD_INJ_OUTPUT_DIM_OK:
            api.SessionGetOutputTypeInfo = OkSessionGetOutputTypeInfo;
            api.CastTypeInfoToTensorInfo = OkCastTypeInfoToTensorInfo;
            api.GetDimensionsCount = TwoGetDimensionsCount;
            api.GetDimensions = SeqGetDimensions;
            consumed = false;
            get_model_output_dim(&engine);
            break;
        /* ---- create_ort_session 错误/告警分支（294-362） ---- */
        case KADD_INJ_CREATE_OPTIONS_FAIL:
            api.CreateSessionOptions = FailCreateSessionOptions;
            (void)create_ort_session(&engine, "kadd_inject_dummy.onnx");
            break;
        case KADD_INJ_EXEC_MODE_FAIL:
            api.SetSessionExecutionMode = FailSetSessionExecutionMode;
            (void)create_ort_session(&engine, "kadd_inject_dummy.onnx");
            break;
        case KADD_INJ_GRAPH_OPT_FAIL:
            api.SetSessionGraphOptimizationLevel = FailSetSessionGraphOptimizationLevel;
            (void)create_ort_session(&engine, "kadd_inject_dummy.onnx");
            break;
        case KADD_INJ_INTRA_THREADS_FAIL:
            /* err 在告警分支被 ReleaseStatus 消费；随后真实 CreateSession 对不存在
             * 的路径返回自己的错误状态，由 cleanup 释放（336-340 / 356-361） */
            api.SetIntraOpNumThreads = FailSetIntraOpNumThreads;
            (void)create_ort_session(&engine, "kadd_inject_this_model_must_not_exist.onnx");
            break;
        case KADD_INJ_INTER_THREADS_FAIL:
            api.SetInterOpNumThreads = FailSetInterOpNumThreads;
            (void)create_ort_session(&engine, "kadd_inject_this_model_must_not_exist.onnx");
            break;
        case KADD_INJ_MEMORY_INFO_FAIL:
            /* CreateSession 桩成功（伪 session）→ CreateMemoryInfo 桩失败 →
             * cleanup_session（349-352）用桩 ReleaseSession 释放伪指针，安全 */
            api.CreateSession = OkCreateSession;
            api.CreateMemoryInfo = FailCreateMemoryInfo;
            api.ReleaseSession = NoopReleaseSession;
            consumed = false;   /* cleanup_session 路径不释放 status（注入器兜底释放） */
            (void)create_ort_session(&engine, "kadd_inject_dummy.onnx");
            break;
        /* ---- get_io_name 失败分支（源码直接 return NULL 不释放 status，注入器兜底） ---- */
        case KADD_INJ_ALLOCATOR_FAIL:
            api.GetAllocatorWithDefaultOptions = FailGetAllocatorWithDefaultOptions;
            consumed = false;
            (void)get_io_name(&engine, 0);
            break;
        case KADD_INJ_OUTPUT_COUNT_FAIL:
            api.SessionGetOutputCount = FailSessionGetOutputCount;
            consumed = false;
            (void)get_io_name(&engine, 1);
            break;
        case KADD_INJ_INPUT_NAME_FAIL:
            api.SessionGetInputName = FailSessionGetInputName;
            consumed = false;
            (void)get_io_name(&engine, 0);
            break;
        /* ---- predict_batch 运行期错误分支（508-541） ---- */
        case KADD_INJ_CREATE_TENSOR_FAIL: {
            PreparePredictEngine(&engine);
            api.CreateTensorWithDataAsOrtValue = FailCreateTensorWithData;
            (void)PredictOnce(&engine);
            break;
        }
        case KADD_INJ_RUN_FAIL: {
            PreparePredictEngine(&engine);
            api.CreateTensorWithDataAsOrtValue = OkCreateTensorWithData;
            api.Run = FailRun;
            api.ReleaseValue = NoopReleaseValue;
            (void)PredictOnce(&engine);
            break;
        }
        case KADD_INJ_GET_DATA_FAIL: {
            PreparePredictEngine(&engine);
            api.CreateTensorWithDataAsOrtValue = OkCreateTensorWithData;
            api.Run = OkRun;
            api.GetTensorMutableData = FailGetTensorMutableData;
            api.ReleaseValue = NoopReleaseValue;
            (void)PredictOnce(&engine);
            break;
        }
        case KADD_INJ_OUTPUT_DIM_SMALL: {
            PreparePredictEngine(&engine);
            engine.output_dim = 1;              /* 触发 output_dim<2 防御（535-541） */
            api.CreateTensorWithDataAsOrtValue = OkCreateTensorWithData;
            api.Run = OkRun;
            api.GetTensorMutableData = OkGetTensorMutableData;
            api.ReleaseValue = NoopReleaseValue;
            consumed = false;                   /* 无错误状态参与 */
            (void)PredictOnce(&engine);
            break;
        }
        case KADD_INJ_IO_NAME_NULL:
            /* status 成功但 name==NULL -> 直接返回 NULL（151-153），不触碰 strdup */
            api.SessionGetInputName = OkSessionGetInputNameNull;
            consumed = false;                   /* 无错误状态参与 */
            (void)get_io_name(&engine, 0);
            break;
        case KADD_INJ_ALLOCATOR_FREE_FAIL: {
            /* strdup 成功后 AllocatorFree 返回错误状态 -> 161-163 收并释放 */
            api.SessionGetInputName = OkSessionGetInputNameDup;
            api.AllocatorFree = FailAllocatorFree;
            char *got = get_io_name(&engine, 0);
            free(got);                          /* 返回值归调用方所有 */
            break;
        }
        case KADD_INJ_CREATE_ENV_FAIL: {
            /* init_ort_env 只在全局 g_ort_env 为空时才会调 CreateEnv，而进程内
             * 它一旦建立就不会再为空；这里临时保存并置空两个 TU 内 static 全局量，
             * 同时把 g_ort_api 换成本场景的伪函数表（init_ort_api 会把它赋给 e->api），
             * 事后逐一精确复原，不影响其它已存活的引擎句比。
             * 一次覆盖：110-113（CreateEnv 失败）与 395-398（初始化失败上抛）。 */
            OrtEnv *saved_env = g_ort_env;
            const OrtApi *saved_api = g_ort_api;
            int saved_refs = g_ort_env_ref_count;
            g_ort_env = nullptr;
            g_ort_env_ref_count = 0;
            api.CreateEnv = FailCreateEnv;
            g_ort_api = &api;
            kadd_inference_engine_t *out = nullptr;
            (void)load_engine_internal(&out, "kadd_inject_dummy.onnx");
            g_ort_api = saved_api;              /* 必须立即复原：api 是本函数的局部量 */
            g_ort_env = saved_env;
            g_ort_env_ref_count = saved_refs;
            break;
        }
        default:
            consumed = false;
            break;
    }
    if (!consumed) {
        real->ReleaseStatus(err);
    }
    InjectedError() = nullptr;
    return 0;
}

/**
 * @brief 跑完全部注入场景
 * @return 实际执行的场景数（0 表示 ORT 环境不可用，调用方应视为跳过而非失败）
 */
inline int RunAllScenarios()
{
    int done = 0;
    for (int s = KADD_INJ_NONE + 1; s < KADD_INJ_SCENARIO_COUNT; ++s) {
        if (RunScenario(s) == 0) {
            ++done;
        }
    }
    return done;
}

}  // namespace kadd_inject

#endif  /* KADD_INFERENCE_FAULT_INJECT_H */
