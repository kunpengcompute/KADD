/* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved. */
/**
 * @file kadd_sve.h
 * @brief 集中式 ARM SVE 检测
 *
 * 所有引擎统一 include 此头文件来判断 SVE 可用性，
 * 避免每个 .c 文件重复 #if defined(__aarch64__) ... 逻辑。
 */
#ifndef KADD_SVE_H
#define KADD_SVE_H

#if defined(__aarch64__) && defined(__ARM_FEATURE_SVE)
#include <arm_sve.h>
#define KADD_HAVE_SVE 1
#else
#define KADD_HAVE_SVE 0
#endif

#endif /* KADD_SVE_H */
