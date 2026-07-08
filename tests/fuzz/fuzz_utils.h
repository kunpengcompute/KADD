/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *
 * @file fuzz_utils.h
 * @brief FUZZ测试工具函数声明
 *
 * 提供FUZZ测试所需的辅助函数和数据生成器
 */

#ifndef KADD_FUZZ_UTILS_H
#define KADD_FUZZ_UTILS_H

#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief 生成SQL注入测试用例
 *
 * @return 包含各种SQL注入攻击模式的字符串向量
 */
std::vector<std::string> GenerateSQLiSamples();

/**
 * @brief 生成正常输入测试用例
 *
 * @return 包含正常用户输入的字符串向量
 */
std::vector<std::string> GenerateNormalSamples();

/**
 * @brief 生成边界情况测试用例
 *
 * @return 包含边界情况和特殊输入的字符串向量
 */
std::vector<std::string> GenerateEdgeCaseSamples();

/**
 * @brief 生成畸形输入测试用例
 *
 * @return 包含格式错误和异常输入的字符串向量
 */
std::vector<std::string> GenerateMalformedSamples();

/**
 * @brief 生成随机Unicode字符串
 *
 * @param length 期望的字符串长度
 * @return 包含随机Unicode字符的字符串
 */
std::string GenerateRandomUnicode(const uint8_t* data, size_t size, size_t length);

/**
 * @brief 生成随机SQL片段
 *
 * @param data 输入数据（用于派生随机选择）
 * @param size 数据大小
 * @param complexity 复杂度等级(1-5)，越高越复杂
 * @return 随机生成的SQL语句片段
 */
std::string GenerateRandomSQLFragment(const uint8_t* data, size_t size, int complexity);

/**
 * @brief 验证输入有效性
 *
 * @param data 输入数据指针
 * @param size 数据大小
 * @return true 如果输入有效，false 否则
 */
bool IsValidInput(const uint8_t* data, size_t size);

/**
 * @brief 从数据构造有效的token序列
 *
 * @param data 输入数据
 * @param size 数据大小
 * @param max_token 最大token值
 * @return 生成的token序列
 */
std::vector<int> GenerateValidTokens(const uint8_t* data, size_t size, int max_token);

/**
 * @brief 生成随机特征向量
 *
 * @param feature_dim 特征维度
 * @param min_value 最小值
 * @param max_value 最大值
 * @return 随机特征向量
 */
std::vector<int> GenerateRandomFeatures(int feature_dim, int min_value, int max_value);

/**
 * @brief 生成特殊字符序列
 *
 * @param include_null 是否包含空字符
 * @param length 序列长度
 * @return 特殊字符序列
 */
std::string GenerateSpecialChars(bool include_null, size_t length);

/**
 * @brief 从二进制数据创建安全字符串
 *
 * @param data 二进制数据
 * @param size 数据大小
 * @return 安全的字符串（移除了不安全字符）
 */
std::string CreateSafeString(const uint8_t* data, size_t size);

/**
 * @brief 生成连续字符串用于压力测试
 *
 * @param character 重复字符
 * @param length 字符串长度
 * @return 连续字符组成的字符串
 */
std::string GenerateRepeatedString(char character, size_t length);

#endif // KADD_FUZZ_UTILS_H