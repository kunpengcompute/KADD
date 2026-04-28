#ifndef ONNXRUNTIME_MINIMAL_H
#define ONNXRUNTIME_MINIMAL_H

#ifdef __GNUC__
// MinGW/GCC 特定配置
#defineORT_EXPORT __declspec(dllimport)
#define ORT_API_CALL
#else
// MSVC 配置
#include "onnxruntime_c_api.h"
#endif

#ifdef __GNUC__

// 基础类型定义（简化版）
typedef void* OrtEnv;
typedef void* OrtSession;
typedef void* OrtSessionOptions;
typedef void* OrtMemoryInfo;
typedef void* OrtValue;
typedef void* OrtStatus;
typedef void* OrtAllocator;

// 错误码
#define ORT_OK 0
#define ORT_FAIL 1

// 日志级别
#define ORT_LOGGING_LEVEL_WARNING 2

// 内存类型
#define OrtMemTypeDefault 0
#define OrtMemTypeCPUInput 0
#define OrtMemTypeCPUOutput 0

// Allocator 类型
#define OrtArenaAllocator 0

// 数据类型
#define ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT 1

// 枚举类型
enum {
    OrtAllocatorTypeDefault = 0
};

// 函数声明（仅声明常用函数）
ORT_EXPORT OrtStatus* OrtCreateEnv(OrtLoggingLevel log_level, const char* name, OrtEnv** out);
ORT_EXPORT OrtStatus* OrtCreateSession(OrtEnv* env, const char* model_path, OrtSessionOptions* options, OrtSession** out);
ORT_EXPORT OrtStatus* OrtCreateSessionOptions(OrtSessionOptions** out);
ORT_EXPORT void OrtReleaseSessionOptions(OrtSessionOptions* options);
ORT_EXPORT OrtStatus* OrtCreateMemoryInfo(const char* name, int type, int mem_type_1, int mem_type_2, OrtMemoryInfo** out);
ORT_EXPORT void OrtReleaseMemoryInfo(OrtMemoryInfo* mem);
ORT_EXPORT OrtStatus* OrtReleaseSession(OrtSession* session);
ORT_EXPORT void OrtReleaseValue(OrtValue* value);
void OrtGetAllocatorWithDefaultOptions(OrtAllocator** out);
void* OrtGetTensorMutableData(OrtValue* value);

#endif // __GNUC__

#endif // ONNXRUNTIME_MINIMAL_H
