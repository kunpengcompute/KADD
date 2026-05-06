#ifndef DEMO_TOOLS_H
#define DEMO_TOOLS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file tools.h
 * @brief Demo 程序工具函数
 */

/**
 * @brief 打印整数数组
 * @param arr 数组指针
 * @param n 数组长度
 */
void print_array(const int *arr, int n);

/**
 * @brief 高精度计时器（开始）
 * @return 计时器句柄
 */
void* timer_start(void);

/**
 * @brief 高精度计时器（结束）
 * @param timer 计时器句柄
 * @return 经过的微秒数（μs）
 */
double timer_end(void *timer);

#ifdef __cplusplus
}
#endif

#endif // DEMO_TOOLS_H
