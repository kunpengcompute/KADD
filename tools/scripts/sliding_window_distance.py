#!/usr/bin/env python3
"""
滑窗距离计算 - 错位累加优化版本

算法说明：
- 对于序列中每个位置，取左右各 window_size 个元素
- 权重计算：距离 d 的权重为 (window_size + 1 - d)
  即距离1权重最大，距离window_size权重为1

优化方案：错位累加
1. 预计算权重向量: seq[i] * weight
2. 错位累加: 左右移位后求和

例如序列 [1, 2, 3, 4, 5]，window_size=2：
- 位置0（值1）：右边[2,3] → 2*2 + 3*1 = 7
- 位置1（值2）：左边[1] + 右边[3,4] → 1*2 + 3*2 + 4*1 = 12
- 位置2（值3）：左边[1,2] + 右边[4,5] → 1*1 + 2*2 + 4*2 + 5*1 = 18
"""
from train_tokens import feature_vector_single, Config

try:
    import numpy as np
    HAS_NUMPY = True
except ImportError:
    HAS_NUMPY = False
    print("Warning: NumPy not found, falling back to pure Python (slower)")

# ============================================================================
# 配置类
# ============================================================================


# ============================================================================
# 方法1: 错位累加 - NumPy 向量化版本 * 最优
# ============================================================================

def sliding_window_distance_shift(sequence, window_size):
    """
    错位累加实现 - 最优方案

    复杂度: O(n * window_size)，但常数极小
    思路:
        1. 预计算: vec[d] = seq * (window_size - d)
        2. 错位累加: 左移/右移后求和

    Args:
        sequence: 输入序列
        window_size: 窗口大小 (实际计算的距离范围是 1 到 window_size-1)

    Returns:
        result: 距离矩阵
    """
    if not HAS_NUMPY:
        return sliding_window_distance_python(sequence, window_size)

    seq = np.array(sequence, dtype=np.int64)
    n = len(seq)
    result = np.zeros(n, dtype=np.int64)

    # 预计算权重向量并错位累加
    # 注意：循环到 window_size（不含），权重为 window_size - d
    for d in range(1, window_size):
        weight = window_size - d  # 距离d的权重

        # 右边贡献: seq[i+d] * weight 影响 result[i]
        # 相当于 seq * weight 向左移 d 位
        weighted = seq * weight
        result += np.roll(weighted, -d)  # 左移

        # 左边贡献: seq[i-d] * weight 影响 result[i]
        # 相当于 seq * weight 向右移 d 位
        result += np.roll(weighted, d)   # 右移

    # 修正边界：roll 是循环移位，需要把越界部分置零
    # 右边贡献：左移后，最后 d 个元素是无效的
    for d in range(1, window_size):
        result[-d:] -= seq[-(d-window_size):] * (window_size - d) if d > window_size else 0

    # 左边贡献：右移后，前 d 个元素是无效的
    # 由于我们同时加了左右，需要更仔细处理边界
    # 简单方法：重新计算，忽略边界溢出

    # 重新实现，更清晰的边界处理
    result = np.zeros(n, dtype=np.int64)
    for d in range(1, window_size):
        weight = window_size - d
        weighted = seq * weight

        # 右边: seq[i+d] → result[i]，i 从 0 到 n-d-1
        # 即 result[:n-d] += weighted[d:]
        result[:n - d] += weighted[d:]

        # 左边: seq[i-d] → result[i]，i 从 d 到 n-1
        # 即 result[d:] += weighted[:n-d]
        result[d:] += weighted[:n - d]

    return result.tolist()


# ============================================================================
# 方法2: 错位累加 - 纯 Python 版本
# ============================================================================

def sliding_window_distance_python(sequence, window_size):
    """
    错位累加 - 纯 Python 实现

    复杂度: O(n * window_size)

    注意：window_size 参数实际上限是计算距离，
         实际计算距离范围是 1 到 window_size-1
    """
    n = len(sequence)
    result = [0] * n

    # 循环到 window_size（不含），权重为 window_size - d
    for d in range(1, window_size):
        weight = window_size - d

        # 预计算加权序列
        weighted = [x * weight for x in sequence]

        # 右边贡献: weighted[d:] 加到 result[:n-d]
        for i in range(n - d):
            result[i] += weighted[i + d]

        # 左边贡献: weighted[:n-d] 加到 result[d:]
        for i in range(d, n):
            result[i] += weighted[i - d]

    return result


# ============================================================================
# 方法3: Naive 实现 - 用于验证
# ============================================================================

def sliding_window_distance_naive(sequence, window_size):
    """
    Naive 实现 - 用于验证正确性
    """
    n = len(sequence)
    result = [0] * n

    for i in range(n):
        weighted_sum = 0

        # 循环到 window_size（不含），权重为 window_size - d
        for d in range(1, window_size):
            weight = window_size - d

            if i - d >= 0:
                weighted_sum += sequence[i - d] * weight
            if i + d < n:
                weighted_sum += sequence[i + d] * weight

        result[i] = weighted_sum

    return result


# ============================================================================
# 详细打印函数
# ============================================================================

def print_detail(sequence, window_size):
    """详细打印计算过程"""
    n = len(sequence)
    print(f"序列: {sequence}")
    print(f"窗口大小: {window_size} (实际计算距离 1 到 {window_size-1})")
    print(f"长度: {n}")
    print("-" * 60)

    for i in range(n):
        left_elements = []
        right_elements = []
        calculations = []

        # 循环到 window_size（不含），权重为 window_size - d
        for d in range(1, window_size):
            weight = window_size - d
            left_idx = i - d
            right_idx = i + d

            if left_idx >= 0:
                left_elements.append((sequence[left_idx], d, weight))
                calculations.append(f"{sequence[left_idx]}*{weight}")
            if right_idx < n:
                right_elements.append((sequence[right_idx], d, weight))
                calculations.append(f"{sequence[right_idx]}*{weight}")

        calc_str = " + ".join(calculations) if calculations else "0"
        result = sum(val * w for val, _, w in left_elements + right_elements)

        print(f"位置{i} (值{sequence[i]}):")
        print(f"  左边: {left_elements}")
        print(f"  右边: {right_elements}")
        print(f"  计算: {calc_str} = {result}")
        print()


def print_shift_process(sequence, window_size):
    """打印错位累加的计算过程"""
    n = len(sequence)
    print(f"序列: {sequence}")
    print(f"窗口大小: {window_size} (实际计算距离 1 到 {window_size-1})")
    print("-" * 60)

    result = [0] * n

    # 循环到 window_size（不含），权重为 window_size - d
    for d in range(1, window_size):
        weight = window_size - d
        weighted = [x * weight for x in sequence]

        print(f"\n距离 d={d}, 权重={weight}")
        print(f"  加权向量: {weighted}")

        # 右边贡献
        right_contrib = [0] * n
        for i in range(n - d):
            right_contrib[i] = weighted[i + d]
        print(f"  左移{d}位: {right_contrib}")

        # 左边贡献
        left_contrib = [0] * n
        for i in range(d, n):
            left_contrib[i] = weighted[i - d]
        print(f"  右移{d}位: {left_contrib}")

        # 累加
        for i in range(n):
            result[i] += right_contrib[i] + left_contrib[i]

    print(f"\n最终结果: {result}")
    print("-" * 60)


# ============================================================================
# 性能测试
# ============================================================================

import time


def benchmark():
    """性能基准测试"""
    print("=" * 60)
    print("性能基准测试")
    print("=" * 60)

    test_cases = [
        (28, 4, "小规模"),
        (1000, 50, "中规模"),
        (10000, 100, "大规模"),
    ]

    for n, w, desc in test_cases:
        print(f"\n{desc}: n={n}, window_size={w}")
        print("-" * 40)

        import random
        random.seed(42)
        sequence = [random.randint(1, 100) for _ in range(n)]

        # Naive
        start = time.time()
        result_naive = sliding_window_distance_naive(sequence, w)
        time_naive = time.time() - start

        # Python 错位累加
        start = time.time()
        result_py = sliding_window_distance_python(sequence, w)
        time_py = time.time() - start

        # NumPy 错位累加
        if HAS_NUMPY:
            start = time.time()
            result_np = sliding_window_distance_shift(sequence, w)
            time_np = time.time() - start

            assert result_naive == result_py == result_np, "结果不一致!"

            # 只有当基准时间足够长时才显示加速比
            if time_naive > 0.001:  # Naive 时间 > 1ms 才显示加速比
                speedup_py = f"{time_naive/time_py:.1f}x" if time_py > 1e-9 else ">1000x"
                speedup_np = f"{time_naive/time_np:.1f}x" if time_np > 1e-9 else ">1000x"
                print(f"  Naive:     {time_naive*1000:.2f} ms")
                print(f"  Python移位: {time_py*1000:.2f} ms ({speedup_py})")
                print(f"  NumPy移位:  {time_np*1000:.2f} ms ({speedup_np}) *")
            else:
                print(f"  Naive:     {time_naive*1000:.2f} ms")
                print(f"  Python移位: {time_py*1000:.2f} ms")
                print(f"  NumPy移位:  {time_np*1000:.2f} ms *")
        else:
            assert result_naive == result_py, "结果不一致!"
            if time_naive > 0.001:  # Naive 时间 > 1ms 才显示加速比
                speedup_py = f"{time_naive/time_py:.1f}x" if time_py > 1e-9 else ">1000x"
                print(f"  Naive:     {time_naive*1000:.2f} ms")
                print(f"  Python移位: {time_py*1000:.2f} ms ({speedup_py})")
            else:
                print(f"  Naive:     {time_naive*1000:.2f} ms")
                print(f"  Python移位: {time_py*1000:.2f} ms")


# ============================================================================
# 主函数
# ============================================================================

def main():
    """测试主函数"""

    print("=" * 60)
    print("滑窗距离计算 - 错位累加优化")
    print("=" * 60)
    print()

    # 测试用例 1: 详细过程
    print("测试用例 1: 详细过程展示")
    print("=" * 60)
    sequence = [1, 2, 3, 4, 5]
    window_size = 3  # 计算距离1和2，权重为2和1
    print_shift_process(sequence, window_size)

    # 测试用例 2: 标准计算
    print("\n测试用例 2: 标准计算")
    print("=" * 60)
    sequence = [1, 2, 3, 4, 5]
    window_size = 3  # 计算距离1和2，权重为2和1
    print_detail(sequence, window_size)

    # 测试用例 3
    print("\n测试用例 3: 长序列")
    print("=" * 60)
    sequence = [1, 2, 3, 4, 5, 6, 7, 8]
    window_size = 4  # 计算距离1、2、3
    result = sliding_window_distance_shift(sequence, window_size)
    print(f"序列: {sequence}")
    print(f"窗口大小: {window_size}")
    print(f"结果: {result}")

    # 测试用例 4
    print("\n测试用例 4: 长序列")
    print("=" * 60)
    sequence = [7, 7, 11, 11, 8, 8]
    window_size = 5  # 计算距离1、2、3、4
    result = sliding_window_distance_shift(sequence, window_size)
    print(f"序列: {sequence}")
    print(f"窗口大小: {window_size}")
    print(f"结果: {result}")

    # 测试用例 5: 特征向量转换
    print("\n测试用例 5: 特征向量转换")
    print("=" * 60)
    if HAS_NUMPY:
        sequence = [7, 11, 8, 12, 8, 11, 8]
        window_size = 5
        final_result = feature_vector_single(np.array(sequence), window_size)
        print(f"序列: {sequence}")
        print(f"窗口大小: {window_size}")
        print(f"特征向量: {final_result}")
    else:
        print("跳过（需要 NumPy）")


    print("\n测试用例 6: 特征向量转换")
    print("=" * 60)
    if HAS_NUMPY:
        sequence = [7, 11, 8, 12, 8, 11, 12]
        window_size = 3
        final_result = feature_vector_single(np.array(sequence), window_size)
        print(f"序列: {sequence}")
        print(f"窗口大小: {window_size}")
        print(f"特征向量: {final_result}")
    else:
        print("跳过（需要 NumPy）")

    # 性能测试
    if HAS_NUMPY:
        benchmark()
    else:
        print("\n提示: 安装 NumPy 可获得更好的性能")
        print("  pip install numpy")


if __name__ == "__main__":
    main()
