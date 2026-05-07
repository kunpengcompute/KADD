#!/usr/bin/env python3
"""
DFA 编译器 - 将 profile_sqli_fixed.txt 编译为转移表

转移表结构:
- 每一行: 一个状态
- 每一列: ASCII 码 (0-255)
- 单元格: 目标状态 ID

输出格式:
1. 转移表 (transition_table.bin) - 二进制格式
2. 边界输出表 (output_table.bin) - 二进制格式
3. EOF 输出表 (eod_table.bin) - 二进制格式
"""

import sys
import os
import re
import struct
import json
from pathlib import Path
from collections import defaultdict

if sys.platform == 'win32':
    import io
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')


class DFAState:
    """DFA 状态"""

    # 动作类型
    ACTION_NONE = 0
    ACTION_EMIT_TOKEN = 1      # EOF/EOD 时发出 token
    ACTION_CLEAR_TOKEN = 2     # EOF/EOD 时清空当前 token
    OUTPUT_ACCEPT_MASK = 0x1F
    OUTPUT_EMIT_SHIFT = 5
    OUTPUT_TOKEN_MASK = 0x1F
    OUTPUT_SCAN_DELTA_SHIFT = 10
    OUTPUT_SCAN_DELTA_MASK = 0x03
    OUTPUT_START_DELTA_SHIFT = 12
    OUTPUT_START_DELTA_MASK = 0x03
    OUTPUT_END_DELTA_SHIFT = 14
    OUTPUT_END_DELTA_MASK = 0x03
    EOD_ACTION_MASK = 0x03
    EOD_TOKEN_SHIFT = 2
    EOD_TOKEN_MASK = 0x3F

    def __init__(self, name, state_id):
        self.name = name
        self.id = state_id
        self.transitions = {}  # char_code -> target_state_id
        self.default = None    # 默认转移或动作
        self.eod = None        # EOF/EOD 转移或动作
        self.token_type = None  # Token 类型 (如果状态有类型)
        self.synthetic_base_name = None
        self.eod_target = None
        self.eod_action = self.ACTION_NONE
        self.eod_action_arg = None

    def __repr__(self):
        return f"DFAState({self.id}, {self.name})"


class DFACompiler:
    """DFA 编译器"""

    STATE_PATTERN = re.compile(
        r'^state\s+([^:#\n]+?)(?:\s*:\s*([^\n#]+?))?\s*(?:#.*)?$',
        re.MULTILINE
    )
    SPACE_CONTINUATION_CODES = {9, 10, 11, 12, 13, 32, 160}

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
        'TYPE_COLLATE': 'A',
    }
    TOKEN_CHAR_ORDER = [
        'k', 'U', 'B', 'E', 't', 'f', 'n', '1', 'v', 's', 'o', '&', 'c', 'A',
        '(', ')', '{', '}', '.', ',', ':', ';', 'T', '?', 'X', 'F', '\\',
    ]
    TOKEN_CHAR_TO_ID = {char: idx + 1 for idx, char in enumerate(TOKEN_CHAR_ORDER)}

    def __init__(self, dfa_file, output_dir=None):
        self.dfa_file = dfa_file
        self.output_dir = output_dir
        self.states = {}  # state_name -> DFAState
        self.state_names = []  # state_id -> state_name
        self.entry_state = None
        self.errors = []
        self.expanded_content = ""
        self.explicit_state_names = set()
        self.terminal_states = {}
        self.synthetic_states = {}
        self.edge_accept_statuses = {}
        self.expansion_base = {}

    def compile(self):
        """编译 DFA 文件为转移表"""
        print("=" * 80)
        print("DFA 编译器")
        print("=" * 80)
        print()

        # 1. 加载并解析 DFA 文件
        print("[1/5] 加载 DFA 文件...")
        if not self._load_dfa():
            return False

        print(f"      加载了 {len(self.states)} 个状态")

        # 2. 解析转移规则
        print("\n[2/5] 解析转移规则...")
        if not self._parse_transitions():
            return False

        self._normalize_space_compounds()
        self._normalize_word_continuations()
        self._normalize_accepting_word_boundaries()
        self._normalize_word_boundary_dup_targets()
        self._normalize_nonaccepting_word_fallbacks()
        self._normalize_qstring_other_delimiters()
        self._snapshot_expansion_base()

        # 3. 物化 copy/dup 指令
        print("\n[3/5] 解析特殊指令...")
        if not self._expand_special_actions():
            return False

        self._synthesize_implicit_eod_rules()
        self._collapse_eod_chains()

        # 4. 构建转移表
        print("\n[4/5] 构建转移表...")
        transition_table = self._build_transition_table()
        if transition_table is None:
            return False
        output_table = self._build_output_table(transition_table)
        if output_table is None:
            return False
        eod_table = self._build_eod_table()
        if eod_table is None:
            return False

        # 5. 保存编译结果
        print("\n[5/5] 保存编译结果...")
        output_dir = self.output_dir or os.getcwd()
        self._save_compiled_dfa(transition_table, output_table, eod_table, output_dir)

        print("\n" + "=" * 80)
        print("编译完成！")
        print(f"  状态数量: {len(self.states)}")
        print(f"  转移表大小: {len(transition_table)} x 256")
        print(f"  输出目录: {output_dir}")
        print("\n生成的文件:")
        print("  - transition_table.bin    二进制转移表")
        print("  - output_table.bin        边界输出表")
        print("  - eod_table.bin           EOF 输出表")
        print("=" * 80)

        return True

    def _load_dfa(self):
        """加载 DFA 文件"""
        try:
            with open(self.dfa_file, 'r', encoding='utf-8') as f:
                raw_content = f.read()
        except Exception as e:
            print(f"      [ERROR] 无法加载文件: {e}")
            return False

        self.explicit_state_names = {
            match.group(1).strip()
            for match in self.STATE_PATTERN.finditer(raw_content)
        }
        self.expanded_content = raw_content
        content = raw_content

        state_id = 0
        for match in self.STATE_PATTERN.finditer(content):
            state_name = match.group(1).strip()
            token_type = match.group(2).strip() if match.group(2) else None

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
                if self._is_token_type_spec(token_type):
                    state.token_type = self._normalize_token_type(token_type)
                else:
                    print(f"      [ERROR] 非法状态头定义: state {state_name}: {token_type}")
                    return False

        return True

    def _normalize_token_type(self, token_type):
        """规范化 token 类型"""
        if token_type in self.TOKEN_TYPE_MAP:
            return self.TOKEN_TYPE_MAP[token_type]
        # 单字符类型直接返回
        if len(token_type) == 1:
            return token_type
        return 'n'  # 默认 BAREWORD

    def _token_char_id(self, token_char):
        """把 token char 编码成紧凑整数 ID。"""
        if token_char is None:
            return 0
        token_char = self._normalize_token_type(token_char)
        token_id = self.TOKEN_CHAR_TO_ID.get(token_char)
        if token_id is None:
            self.errors.append(f"未知 token char: {token_char!r}")
            return 0
        return token_id

    def _pack_accept_info(self, token_char):
        """把接受态 token id 编码到 output table 低位。"""
        return self._token_char_id(token_char)

    def _pack_output_code(self, accept_info, edge_output_spec):
        """把边上的 accepted status 编码成一个 16-bit word。"""
        emit_id = 0
        scan_delta = 0
        start_delta = 0
        end_delta = 0

        if edge_output_spec:
            emit_id = self._token_char_id(edge_output_spec['token_type'])
            scan_delta = edge_output_spec['scan_delta']
            start_delta = edge_output_spec['start_delta']
            end_delta = edge_output_spec['end_delta']

        start_delta_encoded = start_delta + 1
        end_delta_encoded = end_delta + 1
        if scan_delta < 0 or scan_delta > DFAState.OUTPUT_SCAN_DELTA_MASK:
            self.errors.append(f"scan_delta={scan_delta} 超出编码范围")
            scan_delta = 0
        if start_delta_encoded < 0 or start_delta_encoded > DFAState.OUTPUT_START_DELTA_MASK:
            self.errors.append(f"start_delta={start_delta} 超出编码范围")
            start_delta_encoded = 1
        if end_delta_encoded < 0 or end_delta_encoded > DFAState.OUTPUT_END_DELTA_MASK:
            self.errors.append(f"end_delta={end_delta} 超出编码范围")
            end_delta_encoded = 1
        return (
            (accept_info & DFAState.OUTPUT_ACCEPT_MASK)
            | ((emit_id & DFAState.OUTPUT_TOKEN_MASK) << DFAState.OUTPUT_EMIT_SHIFT)
            | ((scan_delta & DFAState.OUTPUT_SCAN_DELTA_MASK) << DFAState.OUTPUT_SCAN_DELTA_SHIFT)
            | ((start_delta_encoded & DFAState.OUTPUT_START_DELTA_MASK) << DFAState.OUTPUT_START_DELTA_SHIFT)
            | ((end_delta_encoded & DFAState.OUTPUT_END_DELTA_MASK) << DFAState.OUTPUT_END_DELTA_SHIFT)
        )

    def _pack_eod_code(self, action, token_char):
        """把 EOF 动作编码成一个 16-bit word。"""
        token_id = self._token_char_id(token_char)
        return (
            (action & DFAState.EOD_ACTION_MASK)
            | ((token_id & DFAState.EOD_TOKEN_MASK) << DFAState.EOD_TOKEN_SHIFT)
        )

    def _is_token_type_spec(self, token_type):
        """判断是否是合法的 token 类型定义"""
        return token_type in self.TOKEN_TYPE_MAP or len(token_type) == 1

    def _parse_action_string(self, action_str):
        """解析特殊动作字符串"""
        action_str = action_str.strip()
        if not action_str:
            return None

        copy_match = re.match(r'^copy\s+(.+?)$', action_str)
        if copy_match:
            return {
                'kind': 'copy',
                'target_state': copy_match.group(1).strip(),
                'token_type': None,
                'emit_len': None,
                'rewind': 0,
            }

        dup_match = re.match(r'^dup\s+(.+?)\s+prepend\s+(\S+)$', action_str)
        if dup_match:
            return {
                'kind': 'dup',
                'target_state': dup_match.group(1).strip(),
                'token_type': dup_match.group(2),
                'emit_len': None,
                'rewind': 0,
            }

        prepend_match = re.match(r'^prepend\s+(\S+)$', action_str)
        if prepend_match:
            return {
                'kind': 'prepend',
                'target_state': None,
                'token_type': prepend_match.group(1),
                'emit_len': None,
                'rewind': 0,
            }

        emit_prefix_match = re.match(
            r'^emit_prefix\s+(\d+)\s+rewind\s+(\d+)\s+prepend\s+(\S+)$',
            action_str,
        )
        if emit_prefix_match:
            return {
                'kind': 'emit_prefix',
                'target_state': None,
                'token_type': emit_prefix_match.group(3),
                'emit_len': int(emit_prefix_match.group(1)),
                'rewind': int(emit_prefix_match.group(2)),
            }
        return None

    def _is_valid_action_string(self, action_str):
        """验证特殊动作字符串是否合法"""
        parsed = self._parse_action_string(action_str)
        if parsed is None:
            return False

        target_state = parsed['target_state']
        token_type = parsed['token_type']
        if target_state and target_state not in self.states:
            return False
        if token_type and not self._is_token_type_spec(token_type):
            return False
        return True

    def _clean_rule_value(self, value):
        """去掉行内注释并清理空白"""
        if '#' in value:
            value = value.split('#')[0]
        return value.strip()

    def _is_safe_override(self, state_name, existing, new_target):
        """判断重复定义是否属于允许的特化覆盖"""
        if existing == new_target:
            return True
        if existing == state_name or new_target == state_name:
            return True
        if self._parse_action_string(existing) is not None:
            return True
        if self._parse_action_string(new_target) is not None:
            return True
        return False

    def _should_skip_transition_target(self, target):
        """跳过不支持的空格复合态，允许它们退化为多 token"""
        state_name_pattern = re.compile(r'^[A-Za-z0-9_.]+$')

        parsed = self._parse_action_string(target)
        if parsed:
            target_state = parsed['target_state']
            return bool(
                target_state
                and target_state not in self.explicit_state_names
                and (
                    ' ' in target_state
                    or state_name_pattern.match(target_state) is None
                )
            )
        return bool(
            target not in self.explicit_state_names
            and (
                ' ' in target
                or state_name_pattern.match(target) is None
            )
        )

    def _is_word_like_state(self, state_name):
        """空格复合词仅在 WORD/TICK_WORD 系列中做拆分规范化"""
        return state_name.startswith('WORD_') or state_name.startswith('TICK_WORD_')

    def _word_fallback_state(self, state_name):
        """关键字 trie 失配后，回到通用单词扫描状态继续按 bareword 读取"""
        if state_name.startswith('TICK_WORD_'):
            return 'TICK_WORD'
        return 'WORD'

    def _is_word_continuation_code(self, code):
        """parse_word 可以继续吞入同一个 bareword 的字符"""
        return (
            48 <= code <= 57 or   # 0-9
            65 <= code <= 90 or   # A-Z
            97 <= code <= 122 or  # a-z
            128 <= code <= 159 or
            161 <= code <= 255 or
            code in {36, 95, 127}  # $, _, DEL
        )

    def _normalize_accepting_word_boundaries(self):
        """对齐 libinjection parse_word 的 NUL、点号和反引号边界语义。"""
        for state in self.states.values():
            if not state.name.startswith('WORD_') or state.token_type is None:
                continue

            # strlencspn(... "\000") 会把 NUL 当作 word 分隔符。
            state.transitions[0] = f"dup ENTRY prepend {state.token_type}"

            # parse_word 会在 '.' 或 '`' 前检查前缀；前缀若是非 bareword token，
            # 先输出这个前缀，再从分隔符重新 tokenize。
            if state.token_type == 'n':
                continue

            fallback_state = self._word_fallback_state(state.name)
            for code in (46, 96):
                target = state.transitions.get(code, state.default)
                if target is None or target == fallback_state:
                    state.transitions[code] = f"dup ENTRY prepend {state.token_type}"

    def _find_delimiter_dup_action(self, state):
        """为接受态找到一个现有的分隔符 dup 动作，用来替代空格自循环"""
        for code in sorted(state.transitions):
            if code in self.SPACE_CONTINUATION_CODES:
                continue
            target = state.transitions[code]
            if not isinstance(target, str):
                continue
            parsed = self._parse_action_string(target)
            if parsed is None or parsed['kind'] != 'dup':
                continue
            if state.token_type is None:
                return target
            if self._normalize_token_type(parsed['token_type']) == state.token_type:
                return target
        return None

    def _normalize_space_compounds(self):
        """统一把带空格的复合名字拆成多个 token"""
        for state in self.states.values():
            if not self._is_word_like_state(state.name):
                continue

            replacement = None
            if state.token_type:
                replacement = self._find_delimiter_dup_action(state)

            for code in self.SPACE_CONTINUATION_CODES:
                if code not in state.transitions:
                    continue
                if replacement is not None:
                    state.transitions[code] = replacement
                else:
                    del state.transitions[code]

    def _normalize_word_continuations(self):
        """关键字前缀后若仍遇到标识符字符，应继续扫描整个 bareword"""
        for state in self.states.values():
            if not self._is_word_like_state(state.name):
                continue

            fallback_state = self._word_fallback_state(state.name)
            if fallback_state not in self.states:
                continue

            for code, target in list(state.transitions.items()):
                if not self._is_word_continuation_code(code):
                    continue
                if not isinstance(target, str):
                    continue

                parsed = self._parse_action_string(target)
                if parsed is None:
                    continue

                action = parsed['kind']
                if action in ('copy', 'dup', 'prepend'):
                    state.transitions[code] = fallback_state

    def _normalize_nonaccepting_word_fallbacks(self):
        """非接受态 trie 状态在分隔符上应结束 bareword，而不是落回 WORD 由 runtime 特判"""
        continuation_codes = set(range(48, 58))
        continuation_codes.update(range(65, 91))
        continuation_codes.update(range(97, 123))
        continuation_codes.update({36, 46, 95, 96})
        continuation_codes.add(127)
        continuation_codes.update(range(128, 160))
        continuation_codes.update(range(161, 256))

        for state in self.states.values():
            if not state.name.startswith('WORD_'):
                continue
            if state.token_type is not None:
                continue
            if state.default != 'WORD':
                continue

            for code in continuation_codes:
                if code in state.transitions:
                    continue
                state.transitions[code] = 'WORD'

            state.default = 'dup ENTRY prepend TYPE_BAREWORD'

    def _normalize_word_boundary_dup_targets(self):
        """关键字 trie 在标点边界上不应继续留在 WORD_* 子树，统一回到 ENTRY 重扫。"""
        for state in self.states.values():
            if not self._is_word_like_state(state.name):
                continue

            for code, target in list(state.transitions.items()):
                if self._is_word_continuation_code(code) or code in self.SPACE_CONTINUATION_CODES:
                    continue
                if not isinstance(target, str):
                    continue

                parsed = self._parse_action_string(target)
                if parsed is None or parsed['kind'] != 'dup':
                    continue

                target_state = parsed['target_state']
                if not (target_state.startswith('WORD_') or target_state.startswith('TICK_WORD_')):
                    continue

                if state.token_type is not None:
                    replacement_type = state.token_type
                else:
                    replacement_type = self._normalize_token_type(parsed['token_type'])
                state.transitions[code] = f"dup ENTRY prepend {replacement_type}"

    def _should_consume_dup_char(self, state_name, code):
        """哪些 dup 转移需要把当前字符纳入当前 token"""
        state = self.states.get(state_name)
        base_name = state.synthetic_base_name if state and state.synthetic_base_name else state_name
        if base_name == 'BWORD' and code == 93:
            return True
        if base_name.startswith('QSTRING_R') and code == 39:
            return True
        if base_name.startswith('QSTRING_OTHER_DELIM_') and base_name.endswith('_END') and code == 39:
            return True
        return False

    def _normalize_qstring_other_delimiters(self):
        """把 q'!text!' 这类动态分隔符展开成普通 DFA 状态。"""
        delimiter_states = {}

        def ensure_delimiter_state(code):
            existing = delimiter_states.get(code)
            if existing:
                return existing

            suffix = f"{code:02X}"
            scan_name = self._allocate_state_name(f"QSTRING_OTHER_DELIM_{suffix}")
            end_name = self._allocate_state_name(f"QSTRING_OTHER_DELIM_{suffix}_END")

            scan_state = DFAState(scan_name, len(self.state_names))
            scan_state.transitions[code] = end_name
            scan_state.default = scan_name
            self.states[scan_name] = scan_state
            self.state_names.append(scan_name)

            end_state = DFAState(end_name, len(self.state_names))
            end_state.transitions[39] = 'dup ENTRY prepend TYPE_STRING'
            end_state.default = scan_name
            self.states[end_name] = end_state
            self.state_names.append(end_name)

            delimiter_states[code] = scan_name
            return scan_name

        for state_name in ('QSTRING', 'QSTRING_QUOTE', 'NQSTRING_Q_QUOTE'):
            state = self.states.get(state_name)
            if state is None:
                continue
            for code, target in list(state.transitions.items()):
                if target == 'QSTRING_OTHER':
                    state.transitions[code] = ensure_delimiter_state(code)

    def _snapshot_expansion_base(self):
        """保存规范化后的原始 DFA 视图，作为 dup/copy 展开的基准图。"""
        self.expansion_base = {}
        for state_name, state in self.states.items():
            self.expansion_base[state_name] = {
                'token_type': state.token_type,
                'transitions': dict(state.transitions),
                'default': state.default,
                'eod': state.eod,
            }

    def _ensure_terminal_state(self, token_type):
        """为需要消费当前字符后立即收口的 dup 转移创建普通接受态。"""
        normalized_arg = self._normalize_token_type(token_type)
        key = normalized_arg
        if key in self.terminal_states:
            return self.terminal_states[key]

        state_name = f"TERMINAL__{ord(normalized_arg):02x}"
        if state_name in self.states:
            index = 2
            while f"{state_name}__{index}" in self.states:
                index += 1
            state_name = f"{state_name}__{index}"

        state = DFAState(state_name, len(self.state_names))
        state.token_type = normalized_arg
        self.states[state_name] = state
        self.state_names.append(state_name)
        self.terminal_states[key] = state_name
        return state_name

    def _token_char_name(self, token_type):
        """把单字符 token code 映射为可读的 synthetic state 名称片段。"""
        token_type = self._normalize_token_type(token_type)
        mapping = {
            'k': 'KEYWORD',
            'U': 'UNION',
            'B': 'GROUP',
            'E': 'EXPRESSION',
            't': 'SQLTYPE',
            'f': 'FUNCTION',
            'n': 'BAREWORD',
            '1': 'NUMBER',
            'v': 'VARIABLE',
            's': 'STRING',
            'o': 'OPERATOR',
            '&': 'LOGIC_OPERATOR',
            'c': 'COMMENT',
            'A': 'COLLATE',
            '(': 'LEFTPARENS',
            ')': 'RIGHTPARENS',
            '{': 'LEFTBRACE',
            '}': 'RIGHTBRACE',
            '.': 'DOT',
            ',': 'COMMA',
            ':': 'COLON',
            ';': 'SEMICOLON',
            'T': 'STATEMENT',
            '?': 'UNKNOWN',
            'X': 'EVIL',
            '\\': 'BACKSLASH',
        }
        return mapping.get(token_type, f"TYPE_{ord(token_type):02X}")

    def _expansion_state_view(self, state_name):
        """读取规范化后的基准状态定义；synthetic state 则回退到当前图。"""
        if state_name in self.expansion_base:
            return self.expansion_base[state_name]
        state = self.states[state_name]
        return {
            'token_type': state.token_type,
            'transitions': state.transitions,
            'default': state.default,
            'eod': state.eod,
        }

    def _transition_from_base(self, state_name, code):
        """在规范化基准图上执行一步转移。"""
        view = self._expansion_state_view(state_name)
        if code in view['transitions']:
            return view['transitions'][code]
        return view['default']

    def _synthetic_state_name(self, kind, accepted_token_type, base_state_name, emit_len=None, rewind=0):
        accepted_name = self._token_char_name(accepted_token_type)
        if kind == 'dup':
            return f"DUP_{accepted_name}_{base_state_name}"
        if kind == 'copy':
            return f"COPY_{accepted_name}_{base_state_name}"
        if kind == 'prefix':
            return f"PREFIX_{accepted_name}_{base_state_name}__{emit_len}__{rewind}"
        raise ValueError(f"未知 synthetic state 类型: {kind}")

    def _allocate_state_name(self, requested_name):
        state_name = requested_name
        if state_name in self.states:
            index = 2
            while f"{state_name}__{index}" in self.states:
                index += 1
            state_name = f"{state_name}__{index}"
        return state_name

    def _populate_cloned_state(self, state, base_view, base_state_name, self_target_name):
        state.token_type = base_view['token_type']
        state.synthetic_base_name = base_state_name

        for code, target in base_view['transitions'].items():
            state.transitions[code] = self_target_name if target == base_state_name else target

        base_default = base_view['default']
        state.default = self_target_name if base_default == base_state_name else base_default

        base_eod = base_view['eod']
        state.eod = self_target_name if base_eod == base_state_name else base_eod

    def _ensure_synthetic_state(self, kind, accepted_token_type, base_state_name, emit_len=None, rewind=0):
        """按 base state 克隆一个可复用的 synthetic state。"""
        normalized_accept = self._normalize_token_type(accepted_token_type)
        key = (kind, normalized_accept, base_state_name, emit_len, rewind)
        existing = self.synthetic_states.get(key)
        if existing:
            return existing, False, []

        base_view = self._expansion_state_view(base_state_name)
        state_name = self._allocate_state_name(
            self._synthetic_state_name(kind, normalized_accept, base_state_name, emit_len, rewind)
        )

        state = DFAState(state_name, len(self.state_names))
        self._populate_cloned_state(
            state,
            base_view,
            base_state_name,
            state_name,
        )

        self.states[state_name] = state
        self.state_names.append(state_name)

        self.synthetic_states[key] = state_name
        return state_name, True, []

    def _edge_accept_status(self, token_type, base_state_name=None, emit_len=0, rewind=0):
        """描述 output table 上的一组 accepted status。"""
        normalized_type = self._normalize_token_type(token_type)
        if emit_len or rewind:
            if emit_len != 1 or rewind != 1:
                self.errors.append(
                    f"不支持的 emit_prefix accepted status: emit_len={emit_len}, rewind={rewind}"
                )
                return None
            return {
                'token_type': normalized_type,
                'scan_delta': 0,
                'start_delta': -1,
                'end_delta': -1,
            }

        if base_state_name == 'ENTRY':
            return {
                'token_type': normalized_type,
                'scan_delta': 1,
                'start_delta': 1,
                'end_delta': 0,
            }

        return {
            'token_type': normalized_type,
            'scan_delta': 1,
            'start_delta': 0,
            'end_delta': 0,
        }

    def _set_edge_accept_status(self, state_name, code, accept_status, line_desc):
        """把边界 accepted status 下沉到 output table 对应边。"""
        if accept_status is None:
            return
        key = (state_name, code)
        existing = self.edge_accept_statuses.get(key)
        if existing is not None and existing != accept_status:
            self.errors.append(
                f"{line_desc}: output table 边重复定义，已有 {existing!r}，新的 {accept_status!r}"
            )
            return
        self.edge_accept_statuses[key] = accept_status

    def _resolve_group_base_state(self, target_state_name, code, line_desc, seen=None):
        """递归消解“重新处理当前字符”链，直到落到一个真正会消费当前字符的状态。"""
        if seen is None:
            seen = set()

        key = (target_state_name, code)
        if key in seen:
            self.errors.append(f"{line_desc}: 递归展开 {target_state_name} 对字符码 {code} 时出现循环")
            return None
        seen.add(key)

        next_target = self._transition_from_base(target_state_name, code)
        if next_target is None:
            self.errors.append(f"{line_desc}: 状态 {target_state_name} 对字符码 {code} 没有一步后继")
            return None
        if next_target in self.states:
            return next_target

        parsed = self._parse_action_string(next_target) if isinstance(next_target, str) else None
        if parsed is None:
            self.errors.append(
                f"{line_desc}: 状态 {target_state_name} 对字符码 {code} 的一步后继 '{next_target}' 未定义"
            )
            return None

        if parsed['kind'] in ('dup', 'copy'):
            return self._resolve_group_base_state(parsed['target_state'], code, line_desc, seen)

        self.errors.append(
            f"{line_desc}: 状态 {target_state_name} 对字符码 {code} 的一步后继 '{next_target}' "
            "不能被规约成普通 consuming state"
        )
        return None

    def _resolve_dup_or_copy_groups(self, target_state_name, codes, line_desc):
        """把一个 target state 的 256 列视图按一步后继分组。"""
        groups = defaultdict(list)
        for code in codes:
            base_state_name = self._resolve_group_base_state(target_state_name, code, line_desc)
            if base_state_name is None:
                continue
            groups[base_state_name].append(code)
        return groups

    def _expand_dup_codes(self, state_name, codes, parsed, line_desc, queue, queued):
        """把 dup TARGET prepend TYPE_X 展开成显式 DUP_* fan-out。"""
        groups = self._resolve_dup_or_copy_groups(parsed['target_state'], codes, line_desc)
        replacements = {}
        for base_state_name, grouped_codes in groups.items():
            synthetic_name, created, followups = self._ensure_synthetic_state(
                'dup',
                parsed['token_type'],
                base_state_name,
            )
            if created and synthetic_name not in queued:
                queue.append(synthetic_name)
                queued.add(synthetic_name)
            for followup_name in followups:
                if followup_name not in queued:
                    queue.append(followup_name)
                    queued.add(followup_name)
            accept_status = self._edge_accept_status(parsed['token_type'], base_state_name)
            for code in grouped_codes:
                replacements[code] = synthetic_name
                self._set_edge_accept_status(state_name, code, accept_status, line_desc)
        return replacements

    def _expand_copy_codes(self, state_name, codes, parsed, line_desc, queue, queued):
        """把 copy TARGET 展开成显式 COPY_* fan-out。"""
        source_state = self.states[state_name]
        if source_state.token_type is None:
            self.errors.append(f"{line_desc}: copy {parsed['target_state']} 只能从接受态展开")
            return {}

        groups = self._resolve_dup_or_copy_groups(parsed['target_state'], codes, line_desc)
        replacements = {}
        for base_state_name, grouped_codes in groups.items():
            synthetic_name, created, followups = self._ensure_synthetic_state(
                'copy',
                source_state.token_type,
                base_state_name,
            )
            if created and synthetic_name not in queued:
                queue.append(synthetic_name)
                queued.add(synthetic_name)
            for followup_name in followups:
                if followup_name not in queued:
                    queue.append(followup_name)
                    queued.add(followup_name)
            accept_status = self._edge_accept_status(source_state.token_type, base_state_name)
            for code in grouped_codes:
                replacements[code] = synthetic_name
                self._set_edge_accept_status(state_name, code, accept_status, line_desc)
        return replacements

    def _expand_prefix_codes(self, state_name, codes, parsed, line_desc, queue, queued):
        """把 emit_prefix N rewind M prepend TYPE_X 展开成通用 PREFIX_* 状态。"""
        if parsed['emit_len'] != 1 or parsed['rewind'] != 1:
            self.errors.append(
                f"{line_desc}: 目前只支持 emit_prefix 1 rewind 1，实际为 '{parsed['emit_len']}/{parsed['rewind']}'"
            )
            return {}

        prefix_base_by_state = {
            'DASH_DASH': 'DASH',
            'NUM_SUFFIX_D_UPPER': 'WORD_D',
            'NUM_SUFFIX_D_LOWER': 'WORD_D',
            'NUM_SUFFIX_F_UPPER': 'WORD_F',
            'NUM_SUFFIX_F_LOWER': 'WORD_F',
        }
        base_state_name = prefix_base_by_state.get(state_name)
        if base_state_name is None:
            self.errors.append(f"{line_desc}: 不支持 {state_name} 的 emit_prefix 展开")
            return {}

        synthetic_name, created, followups = self._ensure_synthetic_state(
            'prefix',
            parsed['token_type'],
            base_state_name,
            emit_len=parsed['emit_len'],
            rewind=parsed['rewind'],
        )
        if created and synthetic_name not in queued:
            queue.append(synthetic_name)
            queued.add(synthetic_name)
        for followup_name in followups:
            if followup_name not in queued:
                queue.append(followup_name)
                queued.add(followup_name)
        accept_status = self._edge_accept_status(
            parsed['token_type'],
            emit_len=parsed['emit_len'],
            rewind=parsed['rewind'],
        )
        for code in codes:
            self._set_edge_accept_status(state_name, code, accept_status, line_desc)
        return {code: synthetic_name for code in codes}

    def _materialize_accepting_boundaries(self, state_name, queue, queued):
        """把接受态的缺失边界列显式展开成 copy ENTRY，消除 runtime lookahead。"""
        state = self.states[state_name]
        if state.token_type is None or state.default is not None:
            return 0

        missing_codes = [code for code in range(256) if code not in state.transitions]
        if not missing_codes:
            return 0

        parsed = {'kind': 'copy', 'target_state': 'ENTRY', 'token_type': None}
        replacements = self._expand_copy_codes(
            state_name,
            missing_codes,
            parsed,
            f"{state_name}.accepting_boundary",
            queue,
            queued,
        )
        for code, target_name in replacements.items():
            state.transitions[code] = target_name
        return len(replacements)

    def _expand_state_special_actions(self, state_name, queue, queued):
        """展开单个状态上的特殊动作与接受态边界。"""
        resolved_count = 0
        state = self.states[state_name]

        for code, target in list(state.transitions.items()):
            parsed = self._parse_action_string(target) if isinstance(target, str) else None
            if parsed is None:
                continue

            line_desc = f"{state_name}.{code}"
            if parsed['kind'] == 'dup':
                if self._should_consume_dup_char(state_name, code):
                    state.transitions[code] = self._ensure_terminal_state(parsed['token_type'])
                else:
                    replacements = self._expand_dup_codes(state_name, [code], parsed, line_desc, queue, queued)
                    if code in replacements:
                        state.transitions[code] = replacements[code]
                resolved_count += 1
            elif parsed['kind'] == 'copy':
                replacements = self._expand_copy_codes(state_name, [code], parsed, line_desc, queue, queued)
                if code in replacements:
                    state.transitions[code] = replacements[code]
                resolved_count += 1
            elif parsed['kind'] == 'emit_prefix':
                replacements = self._expand_prefix_codes(state_name, [code], parsed, line_desc, queue, queued)
                if code in replacements:
                    state.transitions[code] = replacements[code]
                resolved_count += 1
            else:
                self.errors.append(f"{line_desc}: 不支持的显式动作 '{target}'")

        if state.default:
            parsed = self._parse_action_string(state.default)
            if parsed is not None:
                codes = [code for code in range(256) if code not in state.transitions]
                line_desc = f"{state_name}.default"
                replacements = {}

                if parsed['kind'] == 'dup':
                    replacements = self._expand_dup_codes(state_name, codes, parsed, line_desc, queue, queued)
                    if state.eod_action == DFAState.ACTION_NONE and state.eod_target is None:
                        state.eod_action = DFAState.ACTION_EMIT_TOKEN
                        state.eod_action_arg = self._normalize_token_type(parsed['token_type'])
                elif parsed['kind'] == 'copy':
                    replacements = self._expand_copy_codes(state_name, codes, parsed, line_desc, queue, queued)
                elif parsed['kind'] == 'emit_prefix':
                    replacements = self._expand_prefix_codes(state_name, codes, parsed, line_desc, queue, queued)
                elif parsed['kind'] == 'prepend':
                    if state.eod_action == DFAState.ACTION_NONE and state.eod_target is None:
                        state.eod_action = DFAState.ACTION_EMIT_TOKEN
                        state.eod_action_arg = self._normalize_token_type(parsed['token_type'])
                else:
                    self.errors.append(f"{line_desc}: 不支持的 default 动作 '{state.default}'")

                for code, target_name in replacements.items():
                    state.transitions[code] = target_name
                state.default = None
                resolved_count += 1

        if state.eod:
            parsed = self._parse_action_string(state.eod)
            if parsed is None:
                if state.eod in self.states:
                    state.eod_target = state.eod
                else:
                    self.errors.append(f"{state_name}: 未知 eod 目标 '{state.eod}'")
            else:
                if parsed['kind'] in ('dup', 'prepend'):
                    state.eod_action = DFAState.ACTION_EMIT_TOKEN
                    state.eod_action_arg = self._normalize_token_type(parsed['token_type'])
                elif parsed['kind'] == 'copy':
                    state.eod_action = DFAState.ACTION_CLEAR_TOKEN
                else:
                    self.errors.append(f"{state_name}: 不支持的 eod 动作 '{state.eod}'")
            state.eod = None
            resolved_count += 1

        resolved_count += self._materialize_accepting_boundaries(state_name, queue, queued)
        return resolved_count

    def _expand_accepting_boundaries(self):
        """对新增状态继续做闭包展开，直到接受态边界与内部特殊动作都被清空。"""
        resolved_count = 0
        queue = list(self.state_names)
        queued = set(queue)
        idx = 0

        while idx < len(queue):
            state_name = queue[idx]
            idx += 1
            queued.discard(state_name)
            resolved_count += self._expand_state_special_actions(state_name, queue, queued)

        return resolved_count

    def _add_transition(self, state, code, target, line_desc):
        """添加状态转移并检测重复定义"""
        existing = state.transitions.get(code)
        if existing is not None and existing != target:
            if not self._is_safe_override(state.name, existing, target):
                self.errors.append(
                    f"{state.name}: 字符码 {code} 重复定义，已有 '{existing}'，新的 '{target}' ({line_desc})"
                )
        state.transitions[code] = target

    def _parse_transitions(self):
        """解析转移规则"""
        content = self.expanded_content

        matches = list(self.STATE_PATTERN.finditer(content))

        for match_idx, match in enumerate(matches):
            state_name = match.group(1).strip()
            if state_name not in self.states:
                continue

            state = self.states[state_name]

            # 获取状态内容
            start_pos = match.end()
            if match_idx + 1 < len(matches):
                end_pos = matches[match_idx + 1].start()
            else:
                end_pos = len(content)

            state_content = content[start_pos:end_pos]

            # 解析转移规则（支持多行字符列表）
            pending_codes = []  # 累积待处理的字符代码
            lines = state_content.split('\n')
            i = 0
            while i < len(lines):
                line = lines[i].strip()
                if not line or line.startswith('#'):
                    i += 1
                    continue

                # 检查是否是字符代码行（以数字开头，以逗号或冒号结尾）
                if re.match(r'^\d+(?:-\d+)?(?:,\s*\d+(?:-\d+)?)*[,:]\s*$', line):
                    # 累积字符代码
                    codes_str = line.rstrip(',:\t ')
                    pending_codes.extend(self._parse_char_codes(codes_str))
                    # 如果行以冒号结尾，说明下一行是 target
                    if ':' in line:
                        # 下一行是 target（可能有缩进）
                        i += 1
                        while i < len(lines):
                            target_line = lines[i].strip()
                            if target_line and not target_line.startswith('#'):
                                target = target_line
                                # 去除注释
                                if '#' in target:
                                    target = target.split('#')[0].strip()
                                # 解析累积的字符代码
                                for code in pending_codes:
                                    if 0 <= code <= 255:
                                        if not self._should_skip_transition_target(target):
                                            self._add_transition(state, code, target, f"state {state_name}")
                                pending_codes = []
                                break
                            i += 1
                    # 如果行以逗号结尾，继续累积
                    i += 1
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
                            if not self._should_skip_transition_target(target):
                                self._add_transition(state, code, target, f"state {state_name}")
                    i += 1
                elif line.startswith('default:'):
                    default_val = line.split(':', 1)[1].strip()
                    # 去除注释（# 之后的内容）
                    if '#' in default_val:
                        default_val = default_val.split('#')[0].strip()
                    if self._should_skip_transition_target(default_val):
                        i += 1
                        continue
                    if state.default is not None and state.default != default_val:
                        self.errors.append(
                            f"{state_name}: default 重复定义，已有 '{state.default}'，新的 '{default_val}'"
                        )
                    else:
                        state.default = default_val
                    i += 1
                elif line.startswith('eod:'):
                    eod_val = line.split(':', 1)[1].strip()
                    if '#' in eod_val:
                        eod_val = eod_val.split('#')[0].strip()
                    if self._should_skip_transition_target(eod_val):
                        i += 1
                        continue
                    if state.eod is not None and state.eod != eod_val:
                        self.errors.append(
                            f"{state_name}: eod 重复定义，已有 '{state.eod}'，新的 '{eod_val}'"
                        )
                    else:
                        state.eod = eod_val
                    i += 1
                else:
                    i += 1

        if self.errors:
            print("      [ERROR] DFA 定义解析失败:")
            for error in self.errors[:20]:
                print(f"        - {error}")
            if len(self.errors) > 20:
                print(f"        ... 还有 {len(self.errors) - 20} 个错误")
            return False

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

    def _expand_special_actions(self):
        """把 copy/dup/emit_prefix 闭包展开为普通状态图。"""
        resolved_count = 0
        queue = list(self.state_names)
        queued = set(queue)
        idx = 0

        while idx < len(queue):
            state_name = queue[idx]
            idx += 1
            queued.discard(state_name)
            resolved_count += self._expand_state_special_actions(state_name, queue, queued)

        resolved_count += self._expand_accepting_boundaries()

        for state_name, state in self.states.items():
            if state.default and self._parse_action_string(state.default) is not None:
                self.errors.append(f"{state_name}: default 仍残留特殊动作 '{state.default}'")
            if state.eod and self._parse_action_string(state.eod) is not None:
                self.errors.append(f"{state_name}: eod 仍残留特殊动作 '{state.eod}'")
            for code, target in state.transitions.items():
                if isinstance(target, str) and self._parse_action_string(target) is not None:
                    self.errors.append(f"{state_name}.{code}: 仍残留特殊动作 '{target}'")

        if self.errors:
            print("      [ERROR] 特殊指令解析失败:")
            for error in self.errors[:20]:
                print(f"        - {error}")
            if len(self.errors) > 20:
                print(f"        ... 还有 {len(self.errors) - 20} 个错误")
            return False

        print(
            f"      展开了 {resolved_count} 处特殊指令，新增 "
            f"{len(self.synthetic_states)} 个 synthetic state，{len(self.terminal_states)} 个普通终止态"
        )
        return True

    def _synthesize_implicit_eod_rules(self):
        """为少量运行时兜底语义补齐编译期 EOD 元数据"""
        for state in self.states.values():
            if state.eod_target or state.eod_action != DFAState.ACTION_NONE:
                continue

            base_name = state.synthetic_base_name or state.name
            base_view = self._expansion_state_view(base_name)

            if base_name.startswith('STRING_'):
                state.eod_action = DFAState.ACTION_EMIT_TOKEN
                state.eod_action_arg = 's'
                continue

            if base_name.startswith('QSTRING_'):
                state.eod_action = DFAState.ACTION_EMIT_TOKEN
                state.eod_action_arg = 's'
                continue

            if base_name == 'BWORD' or (base_name.startswith('WORD_') and base_view['token_type'] is None):
                state.eod_action = DFAState.ACTION_EMIT_TOKEN
                state.eod_action_arg = 'n'
                continue

            if base_name in {'TICK', 'TICK_BWORD', 'TICK_BWORD_BS'}:
                state.eod_action = DFAState.ACTION_EMIT_TOKEN
                state.eod_action_arg = 'n'
                continue

            if base_name.startswith('TICK_WORD_'):
                state.eod_action = DFAState.ACTION_EMIT_TOKEN
                state.eod_action_arg = base_view['token_type'] or 'n'
                continue

    def _resolve_eod_result(self, state_name):
        """把 eod_target 链预折叠成最终 EOF 动作。"""
        resolved_action = DFAState.ACTION_NONE
        resolved_arg = None
        current_name = state_name
        seen_targets = set()

        while True:
            state = self.states[current_name]
            if state.eod_target:
                target_name = state.eod_target
                if target_name in seen_targets:
                    return resolved_action, resolved_arg
                seen_targets.add(target_name)

                if target_name not in self.states:
                    self.errors.append(f"{state_name}: 未知 eod 目标 '{target_name}'")
                    return resolved_action, resolved_arg

                target_state = self.states[target_name]
                if target_state.token_type:
                    resolved_action = DFAState.ACTION_EMIT_TOKEN
                    resolved_arg = target_state.token_type
                current_name = target_name
                continue

            if state.eod_action == DFAState.ACTION_EMIT_TOKEN:
                return DFAState.ACTION_EMIT_TOKEN, state.eod_action_arg
            if state.eod_action == DFAState.ACTION_CLEAR_TOKEN:
                return DFAState.ACTION_CLEAR_TOKEN, None
            return resolved_action, resolved_arg

    def _collapse_eod_chains(self):
        """把所有 eod_target 链预折叠成最终 eod_action/eod_action_arg。"""
        for state_name, state in self.states.items():
            resolved_action, resolved_arg = self._resolve_eod_result(state_name)
            state.eod_target = None
            state.eod_action = resolved_action
            state.eod_action_arg = resolved_arg

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
                        print(f"      [ERROR] 未知转移目标: {state_name}.{code} -> {target_name}")
                        error_count += 1

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
                else:
                    print(f"      [ERROR] 未知 default 目标: {state_name} -> {state.default}")
                    error_count += 1

        if error_count > 0:
            print(f"      [ERROR] 发现 {error_count} 个错误，无法继续")
            return None

        return transition_table

    def _build_output_table(self, transition_table):
        """构建与转移表同维度的边界输出表。"""
        num_states = len(self.states)
        output_table = [[0] * 256 for _ in range(num_states)]

        for state_name, state in self.states.items():
            state_id = state.id
            if state_id >= num_states:
                self.errors.append(f"{state_name}: state_id={state_id} 超出 output table 范围")
                continue

            for code, target_id in enumerate(transition_table[state_id]):
                if target_id == self.ID_ERROR:
                    continue
                target_name = self.state_names[target_id]
                target_state = self.states[target_name]
                accept_info = self._pack_accept_info(target_state.token_type)
                edge_accept_status = self.edge_accept_statuses.get((state_name, code))
                output_table[state_id][code] = self._pack_output_code(
                    accept_info,
                    edge_accept_status,
                )

        if self.errors:
            return None
        return output_table

    def _build_eod_table(self):
        """构建按状态索引的 EOF 输出表。"""
        num_states = len(self.states)
        eod_table = [0] * num_states

        for state_name, state in self.states.items():
            state_id = state.id
            if state_id >= num_states:
                self.errors.append(f"{state_name}: state_id={state_id} 超出 eod table 范围")
                continue
            eod_table[state_id] = self._pack_eod_code(state.eod_action, state.eod_action_arg)

        if self.errors:
            return None
        return eod_table

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

    def _save_compiled_dfa(self, transition_table, output_table, eod_table, output_dir):
        """保存编译结果"""
        import os

        # 1. 保存转移表（二进制格式）
        table_file = os.path.join(output_dir, 'transition_table.bin')
        with open(table_file, 'wb') as f:
            # 写入状态数量
            f.write(struct.pack('<I', len(self.states)))

            # 写入转移表
            for state_row in transition_table:
                for cell in state_row:
                    if not isinstance(cell, int):
                        raise ValueError(f"转移表中仍存在非整数单元: {cell!r}")
                    f.write(struct.pack('<H', cell))

        print(f"      转移表: {table_file}")

        # 2. 保存输出表（二进制格式）
        output_file = os.path.join(output_dir, 'output_table.bin')
        with open(output_file, 'wb') as f:
            f.write(struct.pack('<I', len(self.states)))
            for state_row in output_table:
                for cell in state_row:
                    f.write(struct.pack('<H', cell))
        print(f"      输出表: {output_file}")

        # 3. 保存 EOF 表（二进制格式）
        eod_file = os.path.join(output_dir, 'eod_table.bin')
        with open(eod_file, 'wb') as f:
            f.write(struct.pack('<I', len(self.states)))
            for cell in eod_table:
                f.write(struct.pack('<H', cell))
        print(f"      EOF表 : {eod_file}")

def _resolve_kadd_paths():
    """解析 KADD 工程内 profile 与 config.json 表输出路径。"""
    script_dir = Path(__file__).resolve().parent
    project_root = script_dir.parents[1]
    dfa_file = script_dir / "profile_sqli_fixed.txt"
    config_file = project_root / "config.json"

    with open(config_file, 'r', encoding='utf-8') as f:
        config = json.load(f)

    dfa_config = config.get('kadd_dfa_engine', {})
    required_keys = ('transition_table_path', 'output_table_path', 'eod_table_path')
    missing = [key for key in required_keys if not dfa_config.get(key)]
    if missing:
        raise ValueError(f"{config_file} 缺少 kadd_dfa_engine 配置项: {', '.join(missing)}")

    table_dirs = set()
    for key in required_keys:
        path = Path(dfa_config[key])
        if not path.is_absolute():
            path = project_root / path
        table_dirs.add(path.parent)

    if len(table_dirs) != 1:
        raise ValueError("三张 DFA 表必须配置在同一个目录下，才能复用 _save_compiled_dfa")

    output_dir = next(iter(table_dirs))
    output_dir.mkdir(parents=True, exist_ok=True)
    return dfa_file, str(output_dir)

def main():
    try:
        dfa_file, output_dir = _resolve_kadd_paths()
    except Exception as e:
        print(f"[ERROR] 解析 KADD DFA 编译路径失败: {e}")
        return False

    compiler = DFACompiler(str(dfa_file), output_dir=output_dir)
    success = compiler.compile()

    return success


if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
