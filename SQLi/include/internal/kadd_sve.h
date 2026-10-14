/* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved. */
/**
 * @file kadd_sve.h
 * @brief 集中式 ARM SVE 检测
 *
 * 所有引擎统一 include 此头文件来判断 SVE 可用性，
 * 避免每个 .c 文件重复 #if defined(__aarch64__) ... 逻辑。
 *
 * 编译期宏（KADD_HAVE_SVE）只能证明"编译器开启了 SVE 代码生成"，
 * 不能证明"运行环境（目标 CPU）支持 SVE"：
 * 交叉编译或错误部署时，直接执行 SVE 指令会 SIGILL。
 * 因此启用任何 SVE 代码路径之前必须先调用 kadd_sve_runtime_available()
 * 做运行时硬件能力检测（Linux 上通过内核辅助向量 AT_HWCAP 读取），
 * 检测失败时回退 ONNX 后端。
 */
#ifndef KADD_SVE_H
#define KADD_SVE_H

#if defined(__aarch64__) && defined(__ARM_FEATURE_SVE)
#include <arm_sve.h>
#define KADD_HAVE_SVE 1
#else
#define KADD_HAVE_SVE 0
#endif

#if KADD_HAVE_SVE && defined(__linux__)
#include <elf.h>      /* AT_HWCAP */
#include <sys/auxv.h> /* getauxval */

/* HWCAP_SVE 定义于内核 UAPI 头 <asm/hwcap.h>；旧工具链可能缺失，此处兜底 */
#ifndef HWCAP_SVE
#define HWCAP_SVE (1UL << 22)
#endif

/*
 * 运行时检测当前 CPU 是否支持 SVE。
 * 返回 1 表示支持；返回 0 表示不支持（必须回退，否则 SVE 指令会触发 SIGILL）。
 */
static inline int kadd_sve_runtime_available(void)
{
    return (getauxval(AT_HWCAP) & HWCAP_SVE) != 0;
}
#elif KADD_HAVE_SVE
/*
 * 非 Linux 平台没有统一的硬件能力检测接口，保守返回"可用"，
 * 保持与既有行为一致（仅当编译器已启用 SVE 时才会编译本函数）。
 */
static inline int kadd_sve_runtime_available(void)
{
    return 1;
}
#endif

#endif /* KADD_SVE_H */
