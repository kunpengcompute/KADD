#!/usr/bin/env python3
"""
DFA 编译器 - 将 profile_sqli_full.txt 编译为转移表

转移表结构:
- 每一行: 一个状态
- 每一列: ASCII 码 (0-255)
- 单元格: 目标状态 ID 或特殊动作

输出格式:
1. 转移表 (transition_table.bin) - 二进制格式
2. 状态元数据 (states_meta.json) - JSON 格式
3. Python 执行引擎 (dfa_engine.py) - 基于转移表的分词器
4. C 语言转移表 (dfa_table.c / dfa_table.h) - C 语言数组格式
"""

import sys
import os
import re
import json
import struct
from collections import defaultdict

if sys.platform == 'win32':
    import io
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')


class DFAState:
    """DFA 状态"""

    # 动作类型
    ACTION_NONE = 0
    ACTION_EMIT_TOKEN = 1      # 发出 token
    ACTION_COPY = 2            # 复制状态转移
    ACTION_DUP_PREPEND = 3     # 复制并前置 token

    def __init__(self, name, state_id):
        self.name = name
        self.id = state_id
        self.transitions = {}  # char_code -> target_state_id
        self.default = None    # 默认转移或动作
        self.token_type = None  # Token 类型 (如果状态有类型)
        self.action = self.ACTION_NONE
        self.action_arg = None

    def __repr__(self):
        return f"DFAState({self.id}, {self.name})"


class DFACompiler:
    """DFA 编译器"""

    # 特殊状态 ID
    ID_ENTRY = 0
    ID_ERROR = 65535  # 错误状态

    # Token 类型映射
    TOKEN_TYPE_MAP = {
        'TYPE_KEYWORD': 'k',
        'TYPE_UNION': 'U',
        'TYPE_EXPRESSION': 'E',
        'TYPE_SQLTYPE': 't',
        'TYPE_FUNCTION': 'f',
        'TYPE_BAREWORD': 'n',
        'TYPE_NUMBER': '1',
        'TYPE_VARIABLE': 'v',
        'TYPE_STRING': 's',
        'TYPE_OPERATOR': 'o',
        'TYPE_LOGIC_OPERATOR': '&',
        'TYPE_COMMENT': 'c',
        'TYPE_LEFTPARENS': '(',
        'TYPE_RIGHTPARENS': ')',
        'TYPE_DOT': '.',
        'TYPE_COMMA': ',',
        'TYPE_COLON': ':',
        'TYPE_SEMICOLON': ';',
        'TYPE_LEFTBRACE': '{',
        'TYPE_RIGHTBRACE': '}',
        'TYPE_BACKSLASH': '\\',
        'TYPE_UNKNOWN': '?',
        'TYPE_EVIL': 'X',
        'TYPE_STATEMENT': 'T',
        'TYPE_GROUP': 'B',
        'TYPE_COLLATE': 'C',
    }

    def __init__(self, dfa_file):
        self.dfa_file = dfa_file
        self.states = {}  # state_name -> DFAState
        self.state_names = []  # state_id -> state_name
        self.entry_state = None

    def compile(self):
        """编译 DFA 文件为转移表"""
        print("=" * 80, flush=True)
        print("DFA 编译器", flush=True)
        print("=" * 80, flush=True)
        print(flush=True)

        # 1. 加载并解析 DFA 文件
        print("[1/5] 加载 DFA 文件...", flush=True)
        if not self._load_dfa():
            return False

        print(f"      加载了 {len(self.states)} 个状态")

        # 2. 解析转移规则
        print("\n[2/5] 解析转移规则...", flush=True)
        if not self._parse_transitions():
            return False
        print("      转移规则解析完成", flush=True)

        # 3. 解析 copy/dup 指令
        print("\n[3/5] 解析特殊指令...", flush=True)
        self._resolve_special_instructions()
        print("      特殊指令解析完成", flush=True)

        # 4. 构建转移表
        print("\n[4/5] 构建转移表...")
        transition_table = self._build_transition_table()

        # 5. 保存编译结果
        print("\n[5/5] 保存编译结果...")
        output_dir = os.path.dirname(os.path.abspath(__file__))
        self._save_compiled_dfa(transition_table, output_dir)

        print("\n" + "=" * 80)
        print("编译完成！")
        print(f"  状态数量: {len(self.states)}")
        print(f"  转移表大小: {len(transition_table)} x 256")
        print(f"  输出目录: {output_dir}")
        print("\n生成的文件:")
        print("  - transition_table.bin    二进制转移表")
        print("  - states_meta.json        状态元数据")
        print("  - dfa_engine.py           Python 执行引擎")
        print("  - dfa_table.h / .c       C 语言转移表")
        print("  - dfa_test.c             C 语言测试程序")
        print("  - Makefile.dfa           C 语言编译配置")
        print("\n使用方法:")
        print("  Python: python dfa_engine.py")
        print("  C 语言: make -f Makefile.dfa")
        print("=" * 80)

        return True

    def _load_dfa(self):
        """加载 DFA 文件"""
        print("      读取文件内容...", flush=True)
        try:
            with open(self.dfa_file, 'r', encoding='utf-8') as f:
                content = f.read()
        except Exception as e:
            print(f"      [ERROR] 无法加载文件: {e}")
            return False

        print(f"      文件大小: {len(content)} 字节", flush=True)

        # 解析所有状态
        print("      编译正则表达式...", flush=True)
        state_pattern = re.compile(
            r'^state\s+([\w.]+)(?::\s*([a-zA-Z&UE_]+))?',
            re.MULTILINE
        )

        print("      查找状态定义...", flush=True)
        state_id = 0
        count = 0
        for match in state_pattern.finditer(content):
            count += 1
            if count % 1000 == 0:
                print(f"      已解析 {count} 个状态...", flush=True)

            state_name = match.group(1)
            token_type = match.group(2)

            # ENTRY 状态必须是第一个
            if state_name == 'ENTRY':
                if self.entry_state is not None:
                    print(f"      [WARNING] 重复的 ENTRY 状态")
                self.entry_state = state_name
                state = DFAState(state_name, self.ID_ENTRY)
                if len(self.states) == 0:
                    self.states[state_name] = state
                    self.state_names.append(state_name)
                    state_id = 1
                else:
                    # ENTRY 已存在，替换
                    old_state = self.states[state_name]
                    state = DFAState(state_name, old_state.id)
                    self.states[state_name] = state
            else:
                state = DFAState(state_name, state_id)
                self.states[state_name] = state
                self.state_names.append(state_name)
                state_id += 1

            # 设置 token 类型
            if token_type:
                state.token_type = self._normalize_token_type(token_type)

        print(f"      总共加载了 {len(self.states)} 个状态", flush=True)
        return True

    def _normalize_token_type(self, token_type):
        """规范化 token 类型"""
        if token_type in self.TOKEN_TYPE_MAP:
            return self.TOKEN_TYPE_MAP[token_type]
        # 单字符类型直接返回
        if len(token_type) == 1:
            return token_type
        return 'n'  # 默认 BAREWORD

    def _parse_transitions(self):
        """解析转移规则"""
        print("      读取 DFA 文件...")
        try:
            with open(self.dfa_file, 'r', encoding='utf-8') as f:
                content = f.read()
        except Exception as e:
            print(f"      [ERROR] 无法加载文件: {e}")
            return False

        print(f"      文件大小: {len(content)} 字节")
        print("      编译正则表达式...")
        state_pattern = re.compile(
            r'^state\s+([\w.]+)(?::\s*([a-zA-Z&UE_]+))?',
            re.MULTILINE
        )

        print("      查找状态定义...")
        matches = list(state_pattern.finditer(content))
        total = len(matches)
        print(f"      找到 {total} 个状态定义")

        for i, match in enumerate(matches):
            if i % 500 == 0 or i == total - 1:
                print(f"      处理状态 {i}/{total}...")
            state_name = match.group(1)
            if state_name not in self.states:
                continue

            state = self.states[state_name]

            # 获取状态内容
            start_pos = match.end()
            if i + 1 < len(matches):
                end_pos = matches[i + 1].start()
            else:
                end_pos = len(content)

            state_content = content[start_pos:end_pos]

            # 解析转移规则（支持多行字符列表）
            pending_codes = []  # 累积待处理的字符代码
            lines = state_content.split('\n')
            line_idx = 0
            while line_idx < len(lines):
                line = lines[line_idx].strip()
                if not line or line.startswith('#'):
                    line_idx += 1
                    continue

                # 检查是否是字符代码行（以数字开头，以逗号或冒号结尾）
                if re.match(r'^\d+(?:-\d+)?(?:,\s*\d+(?:-\d+)?)*[,:]\s*$', line):
                    # 累积字符代码
                    codes_str = line.rstrip(',:\t ')
                    pending_codes.extend(self._parse_char_codes(codes_str))
                    # 如果行以冒号结尾，说明下一行是 target
                    if ':' in line:
                        # 下一行是 target（可能有缩进）
                        line_idx += 1
                        while line_idx < len(lines):
                            target_line = lines[line_idx].strip()
                            if target_line and not target_line.startswith('#'):
                                target = target_line
                                # 去除注释
                                if '#' in target:
                                    target = target.split('#')[0].strip()
                                # 解析累积的字符代码
                                for code in pending_codes:
                                    if 0 <= code <= 255:
                                        state.transitions[code] = target
                                pending_codes = []
                                break
                            line_idx += 1
                    # 如果行以逗号结尾，继续累积
                    line_idx += 1
                # 匹配单行转移规则
                elif re.match(r'(\d+(?:-\d+)?(?:,\s*\d+(?:-\d+)?)*)\s*:\s*(.+)', line):
                    m = re.match(r'(\d+(?:-\d+)?(?:,\s*\d+(?:-\d+)?)*)\s*:\s*(.+)', line)
                    codes_str = m.group(1)
                    target = m.group(2).strip()

                    # 去除注释（# 之后的内容）
                    if '#' in target:
                        target = target.split('#')[0].strip()

                    # 解析字符代码
                    codes = self._parse_char_codes(codes_str)
                    for code in codes:
                        if 0 <= code <= 255:
                            state.transitions[code] = target
                    line_idx += 1
                elif line.startswith('default:'):
                    default_val = line.split(':', 1)[1].strip()
                    # 去除注释（# 之后的内容）
                    if '#' in default_val:
                        default_val = default_val.split('#')[0].strip()
                    state.default = default_val
                    line_idx += 1
                else:
                    line_idx += 1

        return True

    def _parse_char_codes(self, codes_str):
        """解析字符代码字符串"""
        codes = []
        for part in codes_str.split(','):
            part = part.strip()
            if '-' in part:
                start, end = part.split('-')
                codes.extend(range(int(start), int(end) + 1))
            else:
                codes.append(int(part))
        return codes

    def _resolve_special_instructions(self):
        """解析 copy/dup 等特殊指令"""
        resolved_count = 0
        total_states = len(self.states)

        print(f"      开始处理 {total_states} 个状态...", flush=True)

        for idx, (state_name, state) in enumerate(self.states.items()):
            if idx % 1000 == 0:
                print(f"      处理进度 {idx}/{total_states}...", flush=True)

            # 处理 default 转移中的特殊指令
            if state.default:
                if state.default == 'copy ENTRY':
                    state.action = DFAState.ACTION_COPY
                    state.action_arg = 'ENTRY'
                    resolved_count += 1
                elif state.default.startswith('dup '):
                    # dup ENTRY prepend TYPE
                    parts = state.default.split()
                    if len(parts) >= 3 and parts[1] == 'ENTRY' and parts[2] == 'prepend':
                        state.action = DFAState.ACTION_DUP_PREPEND
                        state.action_arg = self._normalize_token_type(parts[3])
                        resolved_count += 1
                elif state.default.startswith('prepend'):
                    # prepend TYPE
                    parts = state.default.split()
                    if len(parts) >= 2:
                        state.action = DFAState.ACTION_EMIT_TOKEN
                        state.action_arg = self._normalize_token_type(parts[1])
                        resolved_count += 1

            # 处理显式转移中的特殊指令
            for code, target in list(state.transitions.items()):
                if target == 'copy ENTRY':
                    # copy 指令需要解析时处理
                    pass

        print(f"      解析了 {resolved_count} 个特殊指令", flush=True)

    def _build_transition_table(self):
        """构建转移表"""
        # 转移表: states[state_id][char_code] = target_state_id
        num_states = len(self.states)
        transition_table = [[self.ID_ERROR] * 256 for _ in range(num_states)]

        # 检查状态 ID 是否连续
        max_id = max(state.id for state in self.states.values())
        if max_id >= num_states:
            print(f"      [WARNING] 状态 ID 不连续，重新分配...")
            self._renumber_states()
            num_states = len(self.states)
            transition_table = [[self.ID_ERROR] * 256 for _ in range(num_states)]

        # 填充转移表
        error_count = 0
        for state_name, state in self.states.items():
            state_id = state.id

            if state_id >= num_states:
                print(f"      [ERROR] 状态 {state_name} ID {state_id} 超出范围")
                error_count += 1
                continue

            # 填充显式转移
            for code, target_name in state.transitions.items():
                if 0 <= code <= 255:
                    if target_name in self.states:
                        target_id = self.states[target_name].id
                        if target_id < num_states:
                            transition_table[state_id][code] = target_id
                        else:
                            print(f"      [WARNING] {state_name}.{code} -> {target_name} (ID {target_id} 超出范围)")
                    else:
                        # 保留原始字符串（可能是特殊指令）
                        transition_table[state_id][code] = target_name

            # 处理 default 转移
            if state.default:
                if state.default in self.states:
                    # default 是另一个状态名
                    default_id = self.states[state.default].id
                    if default_id < num_states:
                        # 正常的 default 转移
                        for code in range(256):
                            if transition_table[state_id][code] == self.ID_ERROR:
                                transition_table[state_id][code] = default_id
                elif state.action == DFAState.ACTION_DUP_PREPEND:
                    # default 是特殊指令 (dup ENTRY prepend TYPE)
                    # 将特殊指令字符串填充到未设置的转移中
                    for code in range(256):
                        if transition_table[state_id][code] == self.ID_ERROR:
                            transition_table[state_id][code] = state.default
                else:
                    # default 是特殊指令字符串（直接定义）
                    # 将特殊指令字符串填充到未设置的转移中
                    for code in range(256):
                        if transition_table[state_id][code] == self.ID_ERROR:
                            transition_table[state_id][code] = state.default

        if error_count > 0:
            print(f"      [ERROR] 发现 {error_count} 个错误，无法继续")
            return None

        return transition_table

    def _renumber_states(self):
        """重新分配状态 ID"""
        new_id = 0
        name_to_new_id = {}

        # ENTRY 必须是 0
        if 'ENTRY' in self.states:
            self.states['ENTRY'].id = 0
            name_to_new_id['ENTRY'] = 0
            new_id = 1

        # 按原始名称排序重新分配
        for name in sorted(self.states.keys()):
            if name == 'ENTRY':
                continue
            self.states[name].id = new_id
            name_to_new_id[name] = new_id
            new_id += 1

        # 更新 state_names 列表
        self.state_names = [None] * len(self.states)
        for name, state in self.states.items():
            self.state_names[state.id] = name

    def _save_compiled_dfa(self, transition_table, output_dir):
        """保存编译结果"""
        import os

        # 1. 保存转移表（二进制格式）
        table_file = os.path.join(output_dir, 'transition_table.bin')
        with open(table_file, 'wb') as f:
            # 写入状态数量
            f.write(struct.pack('<I', len(self.states)))

            # 写入状态名长度和名称
            for state_name in self.state_names:
                name_bytes = state_name.encode('utf-8')
                f.write(struct.pack('<B', len(name_bytes)))
                f.write(name_bytes)

            # 写入转移表
            for state_row in transition_table:
                for cell in state_row:
                    if isinstance(cell, int):
                        f.write(struct.pack('<H', cell))
                    else:
                        # 字符串（特殊指令），写入 0xFFFE + 长度 + 字符串
                        f.write(struct.pack('<H', 0xFFFE))  # 使用 0xFFFE 而不是 0xFFFF
                        str_bytes = str(cell).encode('utf-8')
                        str_len = len(str_bytes)
                        if str_len > 255:
                            str_len = 255  # 限制最大长度
                        f.write(struct.pack('<B', str_len))
                        f.write(str_bytes[:str_len])

        print(f"      转移表: {table_file}")

        # 2. 保存状态元数据（JSON 格式）
        meta_file = os.path.join(output_dir, 'states_meta.json')
        meta_data = {
            'num_states': len(self.states),
            'entry_state': 'ENTRY',
            'state_names': self.state_names,
            'states': {}
        }

        for state_name, state in self.states.items():
            meta_data['states'][state_name] = {
                'id': state.id,
                'token_type': state.token_type,
                'action': state.action,
                'action_arg': state.action_arg,
            }

        with open(meta_file, 'w', encoding='utf-8') as f:
            json.dump(meta_data, f, indent=2)

        print(f"      元数据: {meta_file}")

        # 3. 生成执行引擎
        # self._generate_engine(output_dir)

        # 4. 生成 C 语言表格
        self._generate_c_table(transition_table, output_dir)

    def _generate_engine(self, output_dir):
        """生成基于转移表的执行引擎"""
        import os

        engine_file = os.path.join(output_dir, 'dfa_engine.py')

        engine_code = '''#!/usr/bin/env python3
"""
DFA 执行引擎 - 基于转移表的高效分词器

由 dfa_compiler.py 自动生成
"""

import sys
import struct
import os

if sys.platform == 'win32':
    import io
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')


class Token:
    """Token"""
    def __init__(self, type, value, pos):
        self.type = type
        self.value = value
        self.pos = pos

    def __repr__(self):
        type_names = {
            'o': 'OPERATOR', 'k': 'KEYWORD', 'n': 'BAREWORD', '1': 'NUMBER',
            'v': 'VARIABLE', 's': 'STRING', '&': 'LOGIC_OP', 'U': 'UNION',
            'E': 'EXPRESSION', 't': 'SQLTYPE', 'f': 'FUNCTION',
            '(': 'L_PAREN', ')': 'R_PAREN', '.': 'DOT',
        }
        tname = type_names.get(self.type, self.type)
        return f"Token({tname}, '{self.value}', pos={self.pos})"


class DFAEngine:
    """DFA 执行引擎"""

    ID_ERROR = 65535
    ID_ENTRY = 0

    # Token 类型映射
    TOKEN_TYPE_MAP = {
        'TYPE_KEYWORD': 'k',
        'TYPE_UNION': 'U',
        'TYPE_EXPRESSION': 'E',
        'TYPE_SQLTYPE': 't',
        'TYPE_FUNCTION': 'f',
        'TYPE_BAREWORD': 'n',
        'TYPE_NUMBER': '1',
        'TYPE_VARIABLE': 'v',
        'TYPE_STRING': 's',
        'TYPE_OPERATOR': 'o',
        'TYPE_LOGIC_OPERATOR': '&',
        'TYPE_COMMENT': 'c',
        'TYPE_LEFTPARENS': '(',
        'TYPE_RIGHTPARENS': ')',
        'TYPE_DOT': '.',
        'TYPE_COMMA': ',',
        'TYPE_COLON': ':',
        'TYPE_SEMICOLON': ';',
        'TYPE_LEFTBRACE': '{',
        'TYPE_RIGHTBRACE': '}',
        'TYPE_BACKSLASH': '\\\\',
        'TYPE_UNKNOWN': '?',
        'TYPE_EVIL': 'X',
        'TYPE_STATEMENT': 'T',
        'TYPE_GROUP': 'B',
        'TYPE_COLLATE': 'C',
    }

    def __init__(self, table_dir=None):
        """初始化 DFA 引擎"""
        if table_dir is None:
            table_dir = os.path.dirname(os.path.abspath(__file__))

        self.table_dir = table_dir
        self.transition_table = None
        self.state_names = []
        self.state_meta = {}
        self.num_states = 0

        self._load_tables()

    def _load_tables(self):
        """加载转移表"""
        table_file = os.path.join(self.table_dir, 'transition_table.bin')
        meta_file = os.path.join(self.table_dir, 'states_meta.json')

        # 加载元数据
        import json
        with open(meta_file, 'r', encoding='utf-8') as f:
            meta = json.load(f)

        self.num_states = meta['num_states']
        self.state_names = meta['state_names']
        self.state_meta = meta['states']

        # 加载转移表
        with open(table_file, 'rb') as f:
            # 读取状态数量
            num_states = struct.unpack('<I', f.read(4))[0]

            # 读取状态名
            self.state_names = []
            for _ in range(num_states):
                name_len = struct.unpack('<B', f.read(1))[0]
                name = f.read(name_len).decode('utf-8')
                self.state_names.append(name)

            # 读取转移表
            self.transition_table = []
            for _ in range(num_states):
                row = []
                for _ in range(256):
                    value = struct.unpack('<H', f.read(2))[0]
                    if value == 0xFFFE:  # 特殊指令
                        str_len = struct.unpack('<B', f.read(1))[0]
                        str_val = f.read(str_len).decode('utf-8')
                        row.append(str_val)
                    else:
                        row.append(value)
                self.transition_table.append(row)

        print(f"[DFA] 加载了 {self.num_states} 个状态")

    def tokenize(self, input_str):
        """对输入字符串进行分词"""
        tokens = []
        pos = 0
        n = len(input_str)

        while pos < n:
            # 跳过空白
            char = input_str[pos]
            code = ord(char)

            if char.isspace() or code in [0, 9, 10, 13, 32, 160]:
                pos += 1
                continue

            # 从 ENTRY 开始解析
            token, new_pos = self._parse_from_entry(input_str, pos)
            if token:
                tokens.append(token)
                pos = new_pos
            else:
                pos += 1

        return tokens

    def _parse_from_entry(self, input_str, start_pos):
        """从 ENTRY 状态解析"""
        state_id = self.ID_ENTRY
        pos = start_pos
        token_value = ''
        token_type = None
        n = len(input_str)

        while pos < n:
            char = input_str[pos]
            code = ord(char)

            # 获取转移
            next_state = self.transition_table[state_id][code]

            if next_state == self.ID_ERROR:
                # 错误，停止
                break

            # 检查是否是特殊指令
            if isinstance(next_state, str):
                # 特殊指令
                if next_state == 'copy ENTRY':
                    # 保持当前字符，返回 token
                    break
                elif next_state.startswith('dup ENTRY prepend'):
                    # 提取 token 类型并返回
                    parts = next_state.split()
                    if len(parts) >= 4:
                        raw_type = parts[3]
                        token_type = self.TOKEN_TYPE_MAP.get(raw_type, raw_type)
                    # 回退一个字符
                    pos -= 1
                    break
                else:
                    break

            # 检查新状态对当前字符是否有特殊指令
            if isinstance(next_state, int) and next_state < self.num_states:
                current_target = self.transition_table[next_state][code]
                if isinstance(current_target, str) and current_target.startswith('dup ENTRY prepend'):
                    # 新状态对当前字符有特殊指令，不添加当前字符，直接返回
                    raw_type = current_target.split()[-1]
                    token_type = self.TOKEN_TYPE_MAP.get(raw_type, raw_type)
                    # 当前字符不加入token，让ENTRY重新处理
                    break

            # 普通状态转移
            state_id = next_state
            token_value += char
            pos += 1

            # 检查当前状态是否有 action
            state_name = self.state_names[state_id]
            if state_name in self.state_meta:
                meta = self.state_meta[state_name]

                # 检查动作类型
                action = meta.get('action', 0)
                if action == 3:  # ACTION_DUP_PREPEND
                    # 检查下一个字符是否会导致转移
                    if pos < n:
                        next_code = ord(input_str[pos])
                        next_target = self.transition_table[state_id][next_code]
                        if next_target != self.ID_ERROR and not isinstance(next_target, str):
                            # 可以继续，不返回
                            continue

                    # 获取 token 类型
                    token_type = meta.get('action_arg')
                    # 回退一个字符
                    pos -= 1
                    break
                elif meta.get('token_type'):
                    token_type = meta['token_type']

                    # 检查动作类型
                    action = meta.get('action', 0)
                    if action == 0:  # ACTION_NONE
                        # 检查是否有 copy 指令的 default
                        # 需要检查下一个字符是否会导致转移
                        if pos < n:
                            next_code = ord(input_str[pos])
                            next_target = self.transition_table[state_id][next_code]
                            if next_target != self.ID_ERROR and not isinstance(next_target, str):
                                # 可以继续，不返回
                                continue

                        # 回退一个字符
                        pos -= 1
                        break

        if token_value and token_type:
            # new_pos should be start_pos + len(token_value)
            # pos might have been incremented without consuming, so use actual length
            new_pos = start_pos + len(token_value)
            return Token(token_type, token_value, start_pos), new_pos

        # 如果没有生成 token
        if pos > start_pos:
            return Token('n', token_value, start_pos), pos
        elif pos == start_pos:
            return Token('n', input_str[start_pos], start_pos), start_pos + 1

        return None, start_pos


def main():
    """测试 DFA 引擎"""
    print("=" * 60)
    print("DFA 执行引擎测试")
    print("=" * 60)

    # 创建引擎
    engine = DFAEngine()

    # 测试字符串 - 涵盖主要解析逻辑
    test_inputs = [
        # 基础SQL注入模式
        "passwd=1024 or 1=1",
        "SELECT * FROM users WHERE id=1",
        "'admin' OR '1'='1'",

        # 单字符和双字符运算符
        "a != b",
        "x && y",
        "i || j",
        "a == b",

        # 括号和标点
        "(a + b)",
        "func(a, b)",
        "BEGIN; END;",

        # 数字
        "123",
        "3.14",
        "0xFF",

        # 变量
        "@variable",
        "@@global",

        # 关键字
        "SELECT",
        "FROM",
        "WHERE",

        # 字符串
        "'hello'",
        '"world"',
    ]

    for test in test_inputs:
        print(f"\\n输入: '{test}'")
        print("-" * 40)

        tokens = engine.tokenize(test)

        print(f"分词结果 ({len(tokens)} 个 tokens):")
        for i, tok in enumerate(tokens):
            print(f"  [{i}] {tok}")

        # 打印 token 序列
        token_seq = ' '.join([f"{t.value}/{t.type}" for t in tokens])
        print(f"\\nToken 序列: {token_seq}")

    return True


if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
'''

        with open(engine_file, 'w', encoding='utf-8') as f:
            f.write(engine_code)

        print(f"      执行引擎: {engine_file}")

    def _generate_c_table(self, transition_table, output_dir):
        """生成 C 语言转移表 - 与 .bin 格式完全一致"""
        import os

        # 生成原始二进制数据（与 .bin 格式一致）
        binary_data = bytearray()

        # 写入状态数量
        binary_data.extend(struct.pack('<I', len(self.states)))

        # 写入状态名称
        for state_name in self.state_names:
            name_bytes = state_name.encode('utf-8')
            binary_data.append(len(name_bytes))
            binary_data.extend(name_bytes)

        # 写入转移表
        for state_row in transition_table:
            for cell in state_row:
                if isinstance(cell, int):
                    binary_data.extend(struct.pack('<H', cell))
                else:
                    # 特殊指令：0xFFFE + 长度 + 字符串
                    binary_data.extend(struct.pack('<H', 0xFFFE))
                    str_bytes = str(cell).encode('utf-8')
                    str_len = len(str_bytes)
                    if str_len > 255:
                        str_len = 255
                    binary_data.append(str_len)
                    binary_data.extend(str_bytes[:str_len])

        # 计算转移表在二进制数据中的偏移
        table_offset = 4  # 状态数量
        for state_name in self.state_names:
            table_offset += 1 + len(state_name.encode('utf-8'))

        # 生成头文件
        header_file = os.path.join(output_dir, 'dfa_table.h')
        header_code = f'''/**
 * DFA 转移表头文件
 * 由 dfa_compiler.py 自动生成
 * 与 transition_table.bin 格式完全一致
 */

#ifndef DFA_TABLE_H
#define DFA_TABLE_H

#include <stdint.h>
#include <stddef.h>

/* 状态数量 */
#define DFA_NUM_STATES {len(self.states)}

/* 转移表数据偏移量（字节） */
#define DFA_TABLE_OFFSET {table_offset}

/* 特殊状态 ID */
#define DFA_STATE_ENTRY 0
#define DFA_STATE_ERROR 65535

/* 特殊指令标记 */
#define DFA_SPECIAL_INSTRUCTION_MARKER 0xFFFE

/* Token 类型定义 */
typedef enum {{
    TOKEN_KEYWORD = 'k',
    TOKEN_UNION = 'U',
    TOKEN_EXPRESSION = 'E',
    TOKEN_SQLTYPE = 't',
    TOKEN_FUNCTION = 'f',
    TOKEN_BAREWORD = 'n',
    TOKEN_NUMBER = '1',
    TOKEN_VARIABLE = 'v',
    TOKEN_STRING = 's',
    TOKEN_OPERATOR = 'o',
    TOKEN_LOGIC_OPERATOR = '&',
    TOKEN_COMMENT = 'c',
    TOKEN_LEFTPARENS = '(',
    TOKEN_RIGHTPARENS = ')',
    TOKEN_DOT = '.',
    TOKEN_COMMA = ',',
    TOKEN_COLON = ':',
    TOKEN_SEMICOLON = ';',
    TOKEN_LEFTBRACE = '{{',
    TOKEN_RIGHTBRACE = '}}',
    TOKEN_BACKSLASH = '\\\\',
    TOKEN_UNKNOWN = '?',
    TOKEN_EVIL = 'X',
    TOKEN_STATEMENT = 'S',
    TOKEN_GROUP = 'G',
    TOKEN_COLLATE = 'C',
}} token_type_t;

/* 状态动作类型 */
#define ACTION_NONE 0
#define ACTION_EMIT_TOKEN 1
#define ACTION_COPY 2
#define ACTION_DUP_PREPEND 3

/* 状态元数据 */
typedef struct {{
    const char *name;
    char token_type;
    int action;
    const char *action_arg;
}} dfa_state_meta_t;

/* 转移表访问结果 */
typedef struct {{
    uint16_t value;           /* 转移值 */
    const char *instruction;  /* 特殊指令字符串（如果有） */
}} dfa_transition_result_t;

/* 原始二进制数据（与 .bin 文件格式完全一致） */
extern const uint8_t dfa_binary_data[];

/* 二进制数据大小 */
extern const size_t dfa_binary_size;

/* 状态名称数组 */
extern const char *dfa_state_names[DFA_NUM_STATES];

/* 状态元数据数组 */
extern const dfa_state_meta_t dfa_state_meta[DFA_NUM_STATES];

/* 辅助函数声明 */
const char *dfa_get_state_name(uint16_t state_id);
const char *dfa_get_token_type_name(char token_type);

/* 转移表访问函数（解析特殊指令） */
uint16_t dfa_get_transition(const uint8_t *data, size_t data_size,
                             uint16_t state_id, uint8_t char_code,
                             char *out_instruction, size_t instruction_buf_size);

/* 解析特殊指令，返回动作类型 */
int dfa_parse_special_instruction(const char *instr, char *out_token_type);

#endif /* DFA_TABLE_H */
'''

        with open(header_file, 'w', encoding='utf-8') as f:
            f.write(header_code)

        print(f"      C 头文件: {header_file}")

        # 生成源文件
        source_file = os.path.join(output_dir, 'dfa_table.c')

        # 将二进制数据转换为 C 数组格式
        source_code = f'''/**
 * DFA 转移表实现
 * 由 dfa_compiler.py 自动生成
 * 与 transition_table.bin 格式完全一致
 */

#include "dfa_table.h"
#include <string.h>

/* 原始二进制数据（与 .bin 文件完全一致） */
const uint8_t dfa_binary_data[] = {{
'''

        # 添加二进制数据（每行 16 字节）
        for i in range(0, len(binary_data), 16):
            chunk = binary_data[i:i+16]
            hex_bytes = ' '.join(f'0x{b:02X}' for b in chunk)
            source_code += f'    {hex_bytes},'
            if i + 16 < len(binary_data):
                source_code += '\n'

        source_code += f'''}};

const size_t dfa_binary_size = sizeof(dfa_binary_data);

/* 状态名称数组 */
const char *dfa_state_names[DFA_NUM_STATES] = {{
'''
        # 添加状态名称
        for i, state_name in enumerate(self.state_names):
            if i < len(self.state_names) - 1:
                source_code += f'    "{state_name}",\n'
            else:
                source_code += f'    "{state_name}"\n'

        source_code += '''};

/* 状态元数据数组 */
const dfa_state_meta_t dfa_state_meta[DFA_NUM_STATES] = {
'''
        # 添加状态元数据
        for state_name in self.state_names:
            state = self.states[state_name]
            token_type = state.token_type if state.token_type else '0'
            action = state.action
            action_arg = state.action_arg if state.action_arg else 'NULL'

            source_code += f'    {{"{state_name}", \'{token_type}\', {action}, "{action_arg}"}},\n'

        source_code += '''};

/* 辅助函数实现 */
const char *dfa_get_state_name(uint16_t state_id) {
    if (state_id < DFA_NUM_STATES) {
        return dfa_state_names[state_id];
    }
    return "UNKNOWN";
}

const char *dfa_get_token_type_name(char token_type) {
    switch (token_type) {
        case 'k': return "KEYWORD";
        case 'U': return "UNION";
        case 'E': return "EXPRESSION";
        case 't': return "SQLTYPE";
        case 'f': return "FUNCTION";
        case 'n': return "BAREWORD";
        case '1': return "NUMBER";
        case 'v': return "VARIABLE";
        case 's': return "STRING";
        case 'o': return "OPERATOR";
        case '&': return "LOGIC_OP";
        case 'c': return "COMMENT";
        case '(': return "LEFT_PAREN";
        case ')': return "RIGHT_PAREN";
        case '.': return "DOT";
        case ',': return "COMMA";
        case ':': return "COLON";
        case ';': return "SEMICOLON";
        case '{': return "LEFT_BRACE";
        case '}': return "RIGHT_BRACE";
        case '\\\\': return "BACKSLASH";
        case '?': return "UNKNOWN";
        case 'X': return "EVIL";
        case 'T': return "STATEMENT";
        case 'G': return "GROUP";
        case 'C': return "COLLATE";
        default: return "UNKNOWN";
    }
}

/* 转移表访问函数（解析特殊指令） */
/* 返回: 转移值，如果是特殊指令则将指令字符串写入 out_instruction */
uint16_t dfa_get_transition(const uint8_t *data, size_t data_size,
                             uint16_t state_id, uint8_t char_code,
                             char *out_instruction, size_t instruction_buf_size) {{
    /* 计算转移表位置 */
    size_t pos = DFA_TABLE_OFFSET + (size_t)state_id * 256 * 2 + (size_t)char_code * 2;

    if (pos + 2 > data_size) {{
        return DFA_STATE_ERROR;
    }}

    /* 读取转移值 */
    uint16_t value = data[pos] | (data[pos + 1] << 8);
    pos += 2;

    /* 检查是否是特殊指令 */
    if (value == DFA_SPECIAL_INSTRUCTION_MARKER) {{
        /* 读取指令长度 */
        if (pos >= data_size) {{
            return DFA_STATE_ERROR;
        }}
        uint8_t str_len = data[pos++];

        /* 读取指令字符串 */
        if (pos + str_len > data_size) {{
            return DFA_STATE_ERROR;
        }}

        if (out_instruction != NULL && instruction_buf_size > 0) {{
            size_t copy_len = str_len < instruction_buf_size - 1 ? str_len : instruction_buf_size - 1;
            memcpy(out_instruction, &data[pos], copy_len);
            out_instruction[copy_len] = '\\0';
        }}
    }}

    return value;
}}

/* 解析特殊指令，返回动作类型 */
int dfa_parse_special_instruction(const char *instr, char *out_token_type) {{
    if (out_token_type) {{
        *out_token_type = 0;  // 默认无 token 类型
    }}

    if (strstr(instr, "copy ENTRY") != NULL) {{
        return ACTION_COPY;
    }} else if (strstr(instr, "dup ENTRY prepend") != NULL) {{
        // 提取 token 类型
        const char *type_start = strstr(instr, "TYPE_");
        if (type_start != NULL) {{
            if (strncmp(type_start, "TYPE_OPERATOR", 13) == 0) {{
                if (out_token_type) *out_token_type = 'o';
            }} else if (strncmp(type_start, "TYPE_KEYWORD", 12) == 0) {{
                if (out_token_type) *out_token_type = 'k';
            }} else if (strncmp(type_start, "TYPE_UNION", 10) == 0) {{
                if (out_token_type) *out_token_type = 'U';
            }} else if (strncmp(type_start, "TYPE_EXPRESSION", 15) == 0) {{
                if (out_token_type) *out_token_type = 'E';
            }} else if (strncmp(type_start, "TYPE_SQLTYPE", 12) == 0) {{
                if (out_token_type) *out_token_type = 't';
            }} else if (strncmp(type_start, "TYPE_FUNCTION", 13) == 0) {{
                if (out_token_type) *out_token_type = 'f';
            }} else if (strncmp(type_start, "TYPE_BAREWORD", 13) == 0) {{
                if (out_token_type) *out_token_type = 'n';
            }} else if (strncmp(type_start, "TYPE_NUMBER", 11) == 0) {{
                if (out_token_type) *out_token_type = '1';
            }} else if (strncmp(type_start, "TYPE_VARIABLE", 13) == 0) {{
                if (out_token_type) *out_token_type = 'v';
            }} else if (strncmp(type_start, "TYPE_STRING", 11) == 0) {{
                if (out_token_type) *out_token_type = 's';
            }} else if (strncmp(type_start, "TYPE_LOGIC_OPERATOR", 19) == 0) {{
                if (out_token_type) *out_token_type = '&';
            }} else if (strncmp(type_start, "TYPE_COMMENT", 12) == 0) {{
                if (out_token_type) *out_token_type = 'c';
            }} else if (strncmp(type_start, "TYPE_LEFTPARENS", 14) == 0) {{
                if (out_token_type) *out_token_type = '(';
            }} else if (strncmp(type_start, "TYPE_RIGHTPARENS", 15) == 0) {{
                if (out_token_type) *out_token_type = ')';
            }} else if (strncmp(type_start, "TYPE_DOT", 8) == 0) {{
                if (out_token_type) *out_token_type = '.';
            }} else if (strncmp(type_start, "TYPE_COMMA", 10) == 0) {{
                if (out_token_type) *out_token_type = ',';
            }} else if (strncmp(type_start, "TYPE_COLON", 10) == 0) {{
                if (out_token_type) *out_token_type = ':';
            }} else if (strncmp(type_start, "TYPE_SEMICOLON", 14) == 0) {{
                if (out_token_type) *out_token_type = ';';
            }} else if (strncmp(type_start, "TYPE_LEFTBRACE", 14) == 0) {{
                if (out_token_type) *out_token_type = '{{';
            }} else if (strncmp(type_start, "TYPE_RIGHTBRACE", 15) == 0) {{
                if (out_token_type) *out_token_type = '}}';
            }}
        }}
        return ACTION_DUP_PREPEND;
    }}
    return -1;  // 未知指令
}}
'''

        with open(source_file, 'w', encoding='utf-8') as f:
            f.write(source_code)

        print(f"      C 源文件: {source_file}")

        # 额外生成一个测试/示例程序
        test_file = os.path.join(output_dir, 'dfa_test.c')
        test_code = '''/**
 * DFA 测试程序
 * 演示如何使用 dfa_table.h 进行分词
 * 与 .bin 文件格式完全一致
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "dfa_table.h"

/* Token 结构 */
typedef struct {{
    char type;
    char *value;
    size_t length;
    size_t pos;
}} token_t;

/* DFA 分词器 - 使用 dfa_get_transition 函数 */
token_t *dfa_tokenize(const char *input, size_t *token_count) {{
    size_t capacity = 100;
    token_t *tokens = malloc(capacity * sizeof(token_t));
    size_t count = 0;
    size_t pos = 0;
    size_t len = strlen(input);
    char instruction_buffer[256];  /* 特殊指令缓冲区 */

    while (pos < len) {{
        /* 跳过空白字符 */
        unsigned char c = input[pos];
        if (c == ' ' || c == '\\t' || c == '\\n' || c == '\\r') {{
            pos++;
            continue;
        }}

        /* 从 ENTRY 状态开始解析 */
        uint16_t state_id = DFA_STATE_ENTRY;
        size_t start_pos = pos;
        char token_type = 0;
        int has_token = 0;

        while (pos < len) {{
            unsigned char ch = input[pos];

            /* 使用 dfa_get_transition 获取转移值 */
            uint16_t next_value = dfa_get_transition(
                dfa_binary_data, dfa_binary_size,
                state_id, ch,
                instruction_buffer, sizeof(instruction_buffer)
            );

            if (next_value == DFA_STATE_ERROR) {{
                break;
            }}

            /* 检查是否是特殊指令 */
            if (next_value == DFA_SPECIAL_INSTRUCTION_MARKER) {{
                /* 解析特殊指令 */
                char special_token_type = 0;
                int action = dfa_parse_special_instruction(instruction_buffer, &special_token_type);

                if (action == ACTION_DUP_PREPEND) {{
                    /* 发出 token 并回退 */
                    token_type = special_token_type;
                    has_token = 1;
                    /* 不消费当前字符，回退 */
                    break;
                }} else if (action == ACTION_COPY) {{
                    /* 复制 ENTRY 的转移规则 */
                    uint16_t copy_next = dfa_get_transition(
                        dfa_binary_data, dfa_binary_size,
                        DFA_STATE_ENTRY, ch,
                        NULL, 0
                    );
                    if (copy_next != DFA_STATE_ERROR && copy_next != DFA_SPECIAL_INSTRUCTION_MARKER) {{
                        state_id = copy_next;
                        pos++;
                        continue;
                    }} else {{
                        break;
                    }}
                }} else {{
                    /* 未知指令，停止 */
                    break;
                }}
            }}

            /* 正常状态转移 */
            state_id = next_value;
            pos++;

            /* 检查当前状态是否有 token 类型 */
            if (state_id < DFA_NUM_STATES) {{
                dfa_state_meta_t meta = dfa_state_meta[state_id];
                if (meta.token_type != 0 && meta.action == ACTION_NONE) {{
                    /* 检查下一个字符是否能继续 */
                    if (pos < len) {{
                        uint16_t test_next = dfa_get_transition(
                            dfa_binary_data, dfa_binary_size,
                            state_id, (unsigned char)input[pos],
                            NULL, 0
                        );
                        if (test_next != DFA_STATE_ERROR && test_next != DFA_SPECIAL_INSTRUCTION_MARKER) {{
                            continue;
                        }}
                    }}
                    token_type = meta.token_type;
                    has_token = 1;
                    pos--;  /* 回退一个字符 */
                    break;
                }}
            }}
        }}

        /* 创建 token */
        if (pos > start_pos && has_token) {{
            if (count >= capacity) {{
                capacity *= 2;
                tokens = realloc(tokens, capacity * sizeof(token_t));
            }}

            tokens[count].type = token_type;
            tokens[count].value = strndup(input + start_pos, pos - start_pos);
            tokens[count].length = pos - start_pos;
            tokens[count].pos = start_pos;
            count++;
            pos++;  /* 移动到下一个位置 */
        }} else if (pos > start_pos) {{
            /* 没有匹配的 token，跳过这个字符 */
            pos = start_pos + 1;
        }} else {{
            pos++;
        }}
    }}

    *token_count = count;
    return tokens;
}}

/* 释放 token 数组 */
void dfa_free_tokens(token_t *tokens, size_t count) {{
    for (size_t i = 0; i < count; i++) {{
        free(tokens[i].value);
    }}
    free(tokens);
}}

/* 测试主函数 */
int main(int argc, char *argv[]) {{
    printf("DFA 测试程序\\n");
    printf("状态数量: %d\\n", DFA_NUM_STATES);
    printf("二进制数据大小: %zu bytes\\n\\n", dfa_binary_size);

    /* 如果提供了命令行参数，则解析输入 */
    if (argc > 1) {{
        const char *input = argv[1];
        printf("输入: %s\\n\\n", input);

        size_t token_count = 0;
        token_t *tokens = dfa_tokenize(input, &token_count);

        printf("分词结果 (%zu 个 tokens):\\n", token_count);
        for (size_t i = 0; i < token_count; i++) {{
            const char *type_name = dfa_get_token_type_name(tokens[i].type);
            printf("[%zu] Type='%c' (%s) Value=\\"%s\\"\\n",
                   i, tokens[i].type, type_name, tokens[i].value);
        }}

        dfa_free_tokens(tokens, token_count);
    }} else {{
        /* 默认测试用例 */
        const char *test_inputs[] = {{
            "SELECT * FROM users WHERE id=1",
            "passwd=1024 OR 1=1",
            "'admin' OR '1'='1'",
            "a != b && c == d",
            "x || y",
            "i <= j",
            NULL
        }};

        for (int i = 0; test_inputs[i] != NULL; i++) {{
            printf("\\n测试 %d: %s\\n", i + 1, test_inputs[i]);
            printf("----------------------------------------\\n");

            size_t token_count = 0;
            token_t *tokens = dfa_tokenize(test_inputs[i], &token_count);

            for (size_t j = 0; j < token_count; j++) {{
                const char *type_name = dfa_get_token_type_name(tokens[j].type);
                printf("  [%zu] %c(%s)/%s", j, tokens[j].type, type_name, tokens[j].value);
                if (j < token_count - 1) printf(" ");
            }}
            printf("\\n");

            dfa_free_tokens(tokens, token_count);
        }}
    }}

    return 0;
}}
'''

        with open(test_file, 'w', encoding='utf-8') as f:
            f.write(test_code)

        print(f"      C 测试文件: {test_file}")

        # 生成 Makefile
        makefile = os.path.join(output_dir, 'Makefile.dfa')
        makefile_content = '''# DFA 测试程序 Makefile
# 由 dfa_compiler.py 自动生成

CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99
TARGET = dfa_test
SOURCES = dfa_test.c dfa_table.c
HEADERS = dfa_table.h

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
\t$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES)

clean:
\trm -f $(TARGET)

run: $(TARGET)
\t./$(TARGET) "SELECT * FROM users WHERE id=1"

.PHONY: all clean run
'''

        with open(makefile, 'w', encoding='utf-8') as f:
            f.write(makefile_content)

        print(f"      Makefile: {makefile}")


def main():
    dfa_file = os.path.join(os.path.dirname(os.path.abspath(__file__)), "profile_sqli_fixed.txt")

    compiler = DFACompiler(dfa_file)
    success = compiler.compile()

    return success


if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
