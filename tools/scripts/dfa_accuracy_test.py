#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
DFA 引擎准确率测试 - Python版本

测试 dfa_engine_copy.py 的分词准确率
基于 full_dfa_tokenize_results.json 中的数据进行测试
"""

import sys
import os
import json
import time
from pathlib import Path

# 添加当前目录到路径以导入 dfa_engine_copy
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dfa_engine import DFAEngine


class AccuracyTester:
    """准确率测试器"""

    def __init__(self, json_file_path):
        """
        初始化测试器

        Args:
            json_file_path: full_dfa_tokenize_results.json 文件路径
        """
        self.json_file_path = json_file_path
        self.test_data = []
        self.all_results = []  # 存储所有测试结果
        self.results = {
            'total_tests': 0,
            'correct_tests': 0,
            'total_tokens': 0,
            'correct_tokens': 0,
            'malicious_count': 0,
            'malicious_correct': 0,
            'mismatches': []
        }

    def load_test_data(self):
        """从 JSON 文件加载测试数据"""
        print(f"从文件加载: {self.json_file_path}")

        with open(self.json_file_path, 'r', encoding='utf-8') as f:
            raw_data = json.load(f)

        # 检查是否有顶层 data 字段
        data = raw_data.get('data', raw_data) if isinstance(raw_data, dict) else raw_data

        print(f"成功加载 {len(data)} 条测试数据")

        # 解析测试数据
        for item in data:
            # 跳过没有 tokens_correct 字段的条目
            if 'tokens_correct' not in item:
                continue

            test_item = {
                'line': item.get('line', 0),
                'uri': item.get('uri', ''),
                'uri_without_url_encode': item.get('uri_without_url_encode', item.get('uri', '')),
                'label': item.get('label', 0),
                'expected_tokens': item.get('tokens_correct', [])
            }
            self.test_data.append(test_item)

        return len(self.test_data)

    def compare_tokens(self, actual_tokens, expected_tokens):
        """
        比较两个 token 序列

        Returns:
            (is_match, correct_count)
            is_match: 序列是否完全匹配
            correct_count: 正确的 token 数量
        """
        if actual_tokens == expected_tokens:
            return True, len(expected_tokens)

        # 计算 token 准确率
        max_len = max(len(actual_tokens), len(expected_tokens))
        if max_len == 0:
            return True, 0

        correct = 0
        for i in range(min(len(actual_tokens), len(expected_tokens))):
            if actual_tokens[i] == expected_tokens[i]:
                correct += 1

        return False, correct

    def run_test(self, engine, max_tests=None, verbose=False, show_mismatches=10):
        """
        运行准确率测试

        Args:
            engine: DFA 引擎实例
            max_tests: 最大测试数量，None 表示全部
            verbose: 是否显示详细信息
            show_mismatches: 显示不匹配案例的数量
        """
        test_count = min(len(self.test_data), max_tests) if max_tests else len(self.test_data)

        print(f"\n开始测试 {test_count} 条数据...")

        start_time = time.time()

        for i, test in enumerate(self.test_data[:test_count]):
            # 使用 uri_without_url_encode 作为输入
            input_uri = test.get('uri_without_url_encode', test.get('uri', ''))
            expected_tokens = test.get('expected_tokens', [])
            label = test.get('label', 0)

            # 执行分词
            actual_tokens = engine.tokenize_to_indices(input_uri)

            # 统计
            self.results['total_tests'] += 1
            self.results['total_tokens'] += len(expected_tokens)

            if label == 1:
                self.results['malicious_count'] += 1

            # 比较
            is_match, correct_count = self.compare_tokens(actual_tokens, expected_tokens)
            self.results['correct_tokens'] += correct_count

            # 创建测试结果（保存所有结果）
            test_result = {
                'line': test['line'],
                'uri': input_uri,
                'label': label,
                'is_match': is_match,
                'token_accuracy': correct_count / len(expected_tokens) if expected_tokens else 1.0,
                'correct_tokens': correct_count,
                'expected_tokens': expected_tokens,
                'actual_tokens': actual_tokens,
                'expected_count': len(expected_tokens),
                'actual_count': len(actual_tokens)
            }
            self.all_results.append(test_result)

            if is_match:
                self.results['correct_tests'] += 1
                if label == 1:
                    self.results['malicious_correct'] += 1
            else:
                # 记录不匹配案例
                mismatch = {
                    'index': len(self.results['mismatches']),
                    'line': test['line'],
                    'uri': test['uri'],
                    'label': label,
                    'is_match': False,
                    'token_accuracy': correct_count / len(expected_tokens) if expected_tokens else 1.0,
                    'correct_tokens': correct_count,
                    'expected_count': len(expected_tokens),
                    'actual_count': len(actual_tokens),
                    'expected_tokens': expected_tokens,
                    'actual_tokens': actual_tokens
                }

                # 计算 diff
                diff_start = 0
                for j in range(min(len(expected_tokens), len(actual_tokens))):
                    if expected_tokens[j] != actual_tokens[j]:
                        diff_start = j
                        break

                mismatch['diff'] = {
                    'diff_start': diff_start,
                    'expected_context': expected_tokens[max(0, diff_start-2):diff_start+10],
                    'actual_context': actual_tokens[max(0, diff_start-2):diff_start+10]
                }

                self.results['mismatches'].append(mismatch)

                # 显示不匹配案例
                if verbose and len(self.results['mismatches']) <= show_mismatches:
                    self._print_mismatch(mismatch, i + 1)

            # 进度显示
            if (i + 1) % 10000 == 0:
                print(f"进度: {i + 1}/{test_count} ({(i + 1) * 100 // test_count}%)")

        elapsed = time.time() - start_time

        # 计算准确率
        self.results['sequence_accuracy'] = (
            self.results['correct_tests'] / self.results['total_tests'] * 100
            if self.results['total_tests'] > 0 else 0
        )
        self.results['token_accuracy'] = (
            self.results['correct_tokens'] / self.results['total_tokens'] * 100
            if self.results['total_tokens'] > 0 else 0
        )
        self.results['malicious_accuracy'] = (
            self.results['malicious_correct'] / self.results['malicious_count'] * 100
            if self.results['malicious_count'] > 0 else 0
        )
        self.results['elapsed_time'] = elapsed
        self.results['throughput'] = test_count / elapsed if elapsed > 0 else 0

        return self.results

    def _print_mismatch(self, mismatch, test_num):
        """打印不匹配案例"""
        print(f"\n--- 测试 #{test_num} (line {mismatch['line']}) ---")
        print(f"URI: {mismatch['uri']}")
        print(f"标签: {'恶意' if mismatch['label'] == 1 else '正常'}")
        print(f"期望: {mismatch['expected_tokens']} (共{len(mismatch['expected_tokens'])}个)")
        print(f"实际: {mismatch['actual_tokens']} (共{len(mismatch['actual_tokens'])}个)")
        print(f"Token准确率: {mismatch['token_accuracy'] * 100:.2f}%")

    def print_summary(self):
        """打印测试结果汇总"""
        print("\n" + "=" * 60)
        print("                      测试结果汇总                           ")
        print("=" * 60)
        print(f"  总测试数:           {self.results['total_tests']:>15} ")
        print(f"  正确数:             {self.results['correct_tests']:>15} ")
        print(f"  错误数:              {len(self.results['mismatches']):>15} ")
        print(f"  序列准确率:          {self.results['sequence_accuracy']:.2f}%                                  ")
        print(f"  Token准确率:         {self.results['token_accuracy']:.2f}%                                  ")
        print(f"  恶意样本数:          {self.results['malicious_count']:>15} ")
        print(f"  恶意样本正确:        {self.results['malicious_correct']:>15} ")
        print(f"  恶意样本准确率:     {self.results['malicious_accuracy']:.2f}%                                  ")
        print(f"  耗时:                 {self.results['elapsed_time']:.2f} 秒                                  ")
        print(f"  吞吐量:             {int(self.results['throughput'])} 条/秒                             ")
        print("=" * 60)

    def save_results(self, output_dir):
        """
        保存测试结果到 JSON 文件

        Args:
            output_dir: 输出目录路径
        """
        output_dir = Path(output_dir)
        output_dir.mkdir(parents=True, exist_ok=True)

        # 保存完整结果（包含所有测试案例，类似C版本）
        results_file = output_dir / 'dfa_python_accuracy_results.json'
        results_data = {
            'summary': {
                'total_tests': self.results['total_tests'],
                'correct_tests': self.results['correct_tests'],
                'error_tests': len(self.results['mismatches']),
                'sequence_accuracy': self.results['correct_tests'] / self.results['total_tests'] if self.results['total_tests'] > 0 else 0,
                'total_tokens': self.results['total_tokens'],
                'correct_tokens': self.results['correct_tokens'],
                'token_accuracy': self.results['correct_tokens'] / self.results['total_tokens'] if self.results['total_tokens'] > 0 else 0,
                'malicious_samples': self.results['malicious_count'],
                'malicious_correct': self.results['malicious_correct'],
                'malicious_accuracy': self.results['malicious_correct'] / self.results['malicious_count'] if self.results['malicious_count'] > 0 else 0
            },
            'results': self.all_results
        }
        with open(results_file, 'w', encoding='utf-8') as f:
            json.dump(results_data, f, indent=2, ensure_ascii=False)
        print(f"结果已保存到 {results_file}")

        # 保存不匹配案例
        mismatches_file = output_dir / 'dfa_python_mismatches.json'
        mismatches_data = {
            'total_mismatches': len(self.results['mismatches']),
            'mismatches': self.results['mismatches']
        }
        with open(mismatches_file, 'w', encoding='utf-8') as f:
            json.dump(mismatches_data, f, indent=2, ensure_ascii=False)
        print(f"不匹配token已保存到 {mismatches_file} (共{len(self.results['mismatches'])}条)")


def main():
    """主函数"""
    print("=" * 60)
    print("                 DFA 引擎准确率测试 (Python)                  ")
    print("       基于 full_dfa_tokenize_results.json 所有数据                  ")
    print("=" * 60)

    # 获取脚本所在目录
    script_dir = Path(__file__).parent.resolve()

    # JSON 文件路径
    json_file = script_dir / 'full_dfa_tokenize_results.json'

    # 检查文件是否存在
    if not json_file.exists():
        print(f"错误: 找不到文件 {json_file}")
        return 1

    # 显示文件大小
    file_size = json_file.stat().st_size
    print(f"文件大小: {file_size} bytes")

    # 创建测试器
    tester = AccuracyTester(str(json_file))

    # 加载测试数据
    count = tester.load_test_data()

    # 创建 DFA 引擎
    print("\nDFA 引擎创建中...")
    engine = DFAEngine(str(script_dir))
    print("DFA 引擎创建成功")

    # 运行测试
    results = tester.run_test(engine, verbose=True, show_mismatches=10)

    # 打印结果
    tester.print_summary()

    # 保存结果
    results_dir = script_dir / 'results'
    tester.save_results(results_dir)

    return 0


if __name__ == '__main__':
    sys.exit(main())

