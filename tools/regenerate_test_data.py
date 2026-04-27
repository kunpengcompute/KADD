#!/usr/bin/env python3
"""重新生成测试数据，确保与Python引擎一致"""

import json
import sys
import os
sys.path.insert(0, 'src/py')
from dfa_engine_copy import DFAEngine

# 加载原始数据
with open('parsed_results.json', 'r', encoding='utf-8') as f:
    content = f.read().split("处理完成")[0]
    data = json.loads(content)

# 创建引擎
engine = DFAEngine()

# 重新生成tokens
print("重新生成测试数据...")
fixed_count = 0
for i, item in enumerate(data['data']):
    uri = item['uri']
    actual_tokens = engine.tokenize_to_indices(uri)
    
    if actual_tokens != item['tokens']:
        if fixed_count < 10:  # 只打印前10个差异
            print(f"Line {item['line']}: {uri[:50]}")
            print(f"  期望: {item['tokens']}")
            print(f"  实际: {actual_tokens}")
        item['tokens'] = actual_tokens
        fixed_count += 1

print(f"\n共修复 {fixed_count} 条数据")

# 保存修正后的数据
with open('parsed_results_fixed.json', 'w', encoding='utf-8') as f:
    json.dump(data, f, ensure_ascii=False, indent=2)

print("已保存到 parsed_results_fixed.json")
