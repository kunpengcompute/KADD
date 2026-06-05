#!/usr/bin/env python3
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
        'TYPE_BACKSLASH': '\\',
        'TYPE_UNKNOWN': '?',
        'TYPE_EVIL': 'X',
        'TYPE_STATEMENT': 'S',
        'TYPE_GROUP': 'B',
        'TYPE_COLLATE': 'C',
    }

    @staticmethod
    def char_type_to_index(char_type):
        """
        将字符类型映射到索引值
        对应 libinjection sqli_token_type 到索引的转换
        """
        type_to_index = {
            0: 0,      # TYPE_NONE
            'k': 1,     # TYPE_KEYWORD
            'U': 2,     # TYPE_UNION
            'B': 3,     # TYPE_GROUP
            'E': 4,     # TYPE_EXPRESSION
            't': 5,     # TYPE_SQLTYPE
            'f': 6,     # TYPE_FUNCTION
            'n': 7,     # TYPE_BAREWORD
            '1': 8,     # TYPE_NUMBER
            'v': 9,     # TYPE_VARIABLE
            's': 10,    # TYPE_STRING
            'o': 11,    # TYPE_OPERATOR
            '&': 12,    # TYPE_LOGIC_OPERATOR
            'c': 13,    # TYPE_COMMENT
            'A': 14,    # TYPE_COLLATE
            '(': 15,    # TYPE_LEFTPARENS
            ')': 16,    # TYPE_RIGHTPARENS
            '{': 17,    # TYPE_LEFTBRACE
            '}': 18,    # TYPE_RIGHTBRACE
            '.': 19,    # TYPE_DOT
            ',': 20,    # TYPE_COMMA
            ':': 21,    # TYPE_COLON
            ';': 22,    # TYPE_SEMICOLON
            'T': 23,    # TYPE_STATEMENT
            '?': 24,    # TYPE_UNKNOWN
            'X': 25,    # TYPE_EVIL
            'F': 26,    # TYPE_FINGERPRINT
            '\\': 27,   # TYPE_BACKSLASH
        }
        return type_to_index.get(char_type, 24)  # 默认返回 TYPE_UNKNOWN (24)

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

        print(f"[DFA] 加载了 {self.num_states} 个状态", file=sys.stderr)

        # 查找常用状态 ID
        try:
            self.ID_WORD = self.state_names.index('WORD')
        except ValueError:
            self.ID_WORD = None

    def tokenize(self, input_str):
        """对输入字符串进行分词，返回 Token 对象列表"""
        tokens = []
        pos = 0
        n = len(input_str)
        # print("test_string", input_str)
        # 调试开关 - 设置为 True 启用调试输出
        DEBUG = os.environ.get('DEBUG_TOKENIZE', '').lower() in ('1', 'true', 'yes')

        if DEBUG:
            print(f"\n=== DEBUG TOKENIZE START ===")
            print(f"Input: \"{input_str}\"")
            print(f"Length: {n}")

        while pos < n:
            # 跳过空白
            char = input_str[pos]
            code = ord(char)

            # 只跳过 ASCII 空白字符
            if code <= 127 and (char.isspace() or code in [0, 9, 10, 13, 32, 160]):
                pos += 1
                continue

            # 从 ENTRY 开始解析
            token, new_pos = self._parse_from_entry(input_str, pos)
            # print("token.type", token.type, "token.pos", token.pos, 'new_pos:', new_pos)
            if token:
                tokens.append(token)
                if DEBUG:
                    print(f"Token #{len(tokens)-1}: type='{token.type}' value=\"{token.value}\" pos={pos}->{new_pos}")
                pos = new_pos
            else:
                pos += 1

        if DEBUG:
            print(f"=== DEBUG TOKENIZE END ===")
            print(f"Total tokens: {len(tokens)}\n")

        return tokens

    def tokenize_to_indices(self, input_str):
        """
        对输入字符串进行分词，返回类型索引列表
        """
        tokens = self.tokenize(input_str)
        return [self.char_type_to_index(token.type) for token in tokens]

    def _parse_from_entry(self, input_str, start_pos):
        """从 ENTRY 状态解析"""
        state_id = self.ID_ENTRY
        pos = start_pos
        token_value = ''
        token_type = None
        n = len(input_str)
        qstring_start_pos = None

        # 调试开关
        DEBUG = os.environ.get('DEBUG_PARSE_TOKEN', '').lower() in ('1', 'true', 'yes')

        if DEBUG:
            print(f"\n--- _parse_from_entry START (pos={start_pos}) ---")

        while pos < n:
            char = input_str[pos]
            code = ord(char)

            # 处理非 ASCII 字符
            if code > 255:
                current_state_name = self.state_names[state_id]
                # 注释状态：允许包含任何非 ASCII 字符
                if current_state_name in ['COMMENT_EOL', 'COMMENT_BLOCK', 'SLASH_ASTERISK', 'HASH_COMMENT', 'COMMENT_COMPLETE']:
                    token_value += char
                    pos += 1
                    continue
                # BWORD 和 WORD 状态：允许包含非 ASCII 字符作为标识符
                elif current_state_name in ['BWORD', 'WORD']:
                    token_value += char
                    pos += 1
                    continue
                # 其他状态：作为 BAREWORD 处理
                else:
                    token_value += char
                    pos += 1
                    if token_type is None:
                        token_type = 'n'
                    # 检查下一个字符是否是分隔符，如果是则结束 token
                    if pos < n:
                        next_code = ord(input_str[pos])
                        if next_code <= 255:
                            next_char = input_str[pos]
                            # 空白字符或常见分隔符
                            if next_char.isspace() or next_char in '(),;:+-*/^~%&|<>[]=!?':
                                # 结束当前 token
                                return Token(token_type, token_value, start_pos), pos
                    continue

            # 获取转移
            next_state = self.transition_table[state_id][code]

            if next_state == self.ID_ERROR:
                break

            # 检查是否是特殊指令（优先检查，避免被其他逻辑覆盖）
            if isinstance(next_state, str):
                current_state_name = self.state_names[state_id]

                # 首先检查 dup/copy 指令（优先于标识符字符检查）
                if next_state == 'copy ENTRY':
                    break
                elif next_state.startswith('dup ') and 'prepend' in next_state:
                    # 对于 WORD_* 状态，需要先检查当前字符是否是标识符字符
                    # 如果是标识符字符，应该继续扫描而不是立即应用dup指令
                    if current_state_name.startswith('WORD_') or current_state_name in ['WORD', 'BWORD']:
                        is_identifier_char = (
                            (65 <= code <= 90) or  # A-Z
                            (97 <= code <= 122) or  # a-z
                            (48 <= code <= 57) or  # 0-9
                            (code == 95) or  # _
                            (code == 36) or  # $
                            (code == 96)  # ` (backtick)
                        )
                        if is_identifier_char:
                            # 跳过dup指令，继续扫描作为标识符
                            # 重置token_type，因为我们应该作为BAREWORD处理
                            token_type = None
                            state_id = self.ID_WORD if hasattr(self, 'ID_WORD') else state_id
                            token_value += char
                            pos += 1
                            continue

                    # 不是标识符字符，应用dup指令
                    # Handle both 'dup ENTRY prepend' and 'dup WORD_* prepend'
                    parts = next_state.split()
                    if len(parts) >= 4:
                        raw_type = parts[3]
                        token_type = self.TOKEN_TYPE_MAP.get(raw_type, raw_type)
                    # 对于 BWORD 状态，需要包含 ']' 到 token_value 中
                    if self.state_names[state_id] == 'BWORD' and code == 93:
                        token_value += char
                        pos += 1
                    # 对于 QSTRING_R* 状态，需要包含结束的 ''' 到 token_value 中
                    elif self.state_names[state_id].startswith('QSTRING_R') and code == 39:
                        token_value += char
                        pos += 1
                    else:
                        pos -= 1
                    break
                elif next_state == 'copy ENTRY':
                    break
                else:
                    # 对于 WORD_* 状态，检查当前字符是否是标识符字符
                    # 如果是，libinjection 会继续扫描而不是立即应用关键字类型
                    if current_state_name.startswith('WORD_') or current_state_name in ['WORD', 'BWORD']:
                        # 注意：这里不包含点号（46），因为点号应该结束当前 token
                        # 除非是在 WORD 状态中（已经在其他地方处理了）
                        is_identifier_char = (
                            (65 <= code <= 90) or  # A-Z
                            (97 <= code <= 122) or  # a-z
                            (48 <= code <= 57) or  # 0-9
                            (code == 95) or  # _
                            (code == 36) or  # $
                            (code == 96)  # ` (backtick)
                        )
                        if is_identifier_char:
                            # 跳过字符串指令，继续扫描
                            # 模拟转移到 WORD 状态的行为
                            state_id = self.ID_WORD if hasattr(self, 'ID_WORD') else state_id
                            token_value += char
                            pos += 1
                            continue
                    break

            # 检查是否是 delimiter 转移（多级转移处理）
            if isinstance(next_state, int) and next_state < self.num_states:
                next_state_name = self.state_names[next_state]
                if next_state_name == 'WORD' and token_value:
                    is_word_char = (
                        (65 <= code <= 90) or  # A-Z
                        (97 <= code <= 122) or  # a-z
                        (48 <= code <= 57) or  # 0-9
                        (code == 95) or  # _
                        (code == 46) or  # . (dot for qualified names like u.name)
                        (code == 96)  # ` (backtick - libinjection treats backtick as part of bareword)
                    )
                    if not is_word_char:
                        return Token('n', token_value, start_pos), pos

                current_state_name = self.state_names[state_id]
                if next_state == self.ID_ENTRY and current_state_name.endswith('COMMENT') and token_value:
                    return Token('c', token_value, start_pos), pos

            # 特殊处理：从 DASH 转移到 DASH_DASH 时，发出第一个 '-' 的 OPERATOR token
            # 但如果下一个字符是空白字符，则不发出（因为 '-- ' 是注释）
            current_state_name = self.state_names[state_id]
            if (current_state_name == 'DASH' and
                isinstance(next_state, int) and
                self.state_names[next_state] == 'DASH_DASH'):
                # Check if next character is whitespace
                if pos + 1 < n:
                    next_char = input_str[pos + 1]
                    next_code = ord(next_char)
                    # Whitespace characters: 0-32, 127, 160
                    if (0 <= next_code <= 32) or next_code in [127, 160]:
                        # '--' followed by whitespace is a comment, don't emit operator yet
                        pass
                    else:
                        # '--' followed by non-whitespace: emit first dash as operator
                        return Token('o', '-', start_pos), pos
                else:
                    # '--' at end of input: emit operator
                    return Token('o', '-', start_pos), pos

            # 普通状态转移
            state_id = next_state
            token_value += char
            pos += 1

            # 追踪 q-string 起始位置
            if ('QSTRING' in self.state_names[state_id] or 'NQSTRING' in self.state_names[state_id]) and qstring_start_pos is None:
                qstring_start_pos = pos - 1

            # 检查当前状态是否有 action
            state_name = self.state_names[state_id]
            if state_name in self.state_meta:
                meta = self.state_meta[state_name]

                action = meta.get('action', 0)
                if action == 3:  # ACTION_DUP_PREPEND
                    if pos < n:
                        next_code = ord(input_str[pos])
                        if next_code > 255:
                            # Non-ASCII character: for WORD/BWORD states, continue scanning
                            if state_name in ['WORD', 'BWORD']:
                                continue
                        elif next_code <= 255:
                            next_target = self.transition_table[state_id][next_code]
                            if next_target != self.ID_ERROR and not isinstance(next_target, str):
                                continue

                    token_type = meta.get('action_arg')
                    # Only decrement pos if we consumed a character (not for non-ASCII in WORD/BWORD)
                    if pos < n and ord(input_str[pos]) <= 255:
                        pos -= 1
                    break
                elif meta.get('token_type'):
                    token_type = meta['token_type']

                    action = meta.get('action', 0)
                    if action == 0:  # ACTION_NONE
                        if pos < n:
                            next_code = ord(input_str[pos])
                            # For comment states, non-ASCII characters should continue scanning
                            if next_code > 255 and state_name in ['SLASH_ASTERISK', 'HASH_COMMENT', 'COMMENT_EOL', 'DASH_COMMENT', 'COMMENT_COMPLETE']:
                                continue
                            if next_code <= 255:
                                # For WORD_* states (keywords/functions), check if next char is identifier char
                                # If yes, continue scanning (libinjection only matches keyword if entire word matches)
                                if state_name.startswith('WORD_') or state_name in ['WORD', 'BWORD']:
                                    is_identifier_char = (
                                        (65 <= next_code <= 90) or  # A-Z
                                        (97 <= next_code <= 122) or  # a-z
                                        (48 <= next_code <= 57) or  # 0-9
                                        (next_code == 95) or  # _
                                        (next_code == 36) or  # $
                                        (next_code == 46) or  # . (dot for qualified names)
                                        (next_code == 96)  # ` (backtick)
                                    )
                                    if is_identifier_char:
                                        continue
                                # Check next target
                                next_target = self.transition_table[state_id][next_code]
                                if next_target != self.ID_ERROR and not isinstance(next_target, str):
                                    continue

                        pos -= 1
                        break

        # Check if current state has ACTION_DUP_PREPEND (for cases like TICK_BWORD_TICK at end of input)
        if token_value and not token_type and state_id < self.num_states:
            state_name = self.state_names[state_id]
            if state_name in self.state_meta:
                meta = self.state_meta[state_name]
                action = meta.get('action', 0)
                if action == 3:  # ACTION_DUP_PREPEND
                    token_type = meta.get('action_arg')

        # Fix: If we're in a WORD_* state at end of input and the current state doesn't have token_type,
        # we should return BAREWORD instead of using the previously set token_type from a parent state.
        # This handles cases like "DeCl" which goes through WORD_DEC (has token_type) to WORD_DECL (no token_type).
        if token_value and token_type and state_id < self.num_states:
            state_name = self.state_names[state_id]
            if state_name.startswith('WORD_') and state_name in self.state_meta:
                meta = self.state_meta[state_name]
                # If current state doesn't have token_type, use BAREWORD
                if not meta.get('token_type'):
                    token_type = 'n'

        if token_value and token_type:
            final_value = token_value
            final_pos = start_pos
            if qstring_start_pos is not None and token_type == 's':
                if len(token_value) >= 5 and token_value[0] == 'q' and token_value[1] == "'":
                    open_delimiter = token_value[2] if len(token_value) > 2 else None
                    # The second-to-last char should be the closing delimiter
                    close_delimiter = token_value[-2] if len(token_value) > 4 else None
                    delimiter_pairs = {'(': ')', '[': ']', '{': '}', '<': '>'}
                    if open_delimiter in delimiter_pairs and close_delimiter == delimiter_pairs[open_delimiter]:
                        final_value = token_value[3:-2]  # Remove q'( and ')'
                        final_pos = start_pos + 3
                        new_pos = pos  # pos already includes the closing quote
                    else:
                        new_pos = final_pos + len(final_value)
                else:
                    new_pos = final_pos + len(final_value)
            else:
                new_pos = final_pos + len(final_value)
            return Token(token_type, final_value, final_pos), new_pos

        # Handle case where pos < start_pos (happens with "dup ENTRY prepend")
        if pos < start_pos and token_type:
            if DEBUG:
                print(f"_parse_from_entry END (dup ENTRY): type='{token_type}' value=\"{input_str[start_pos]}\" pos={start_pos}->{start_pos + 1}")
            return Token(token_type, input_str[start_pos], start_pos), start_pos + 1

        if pos > start_pos:
            current_state_name = self.state_names[state_id]
            if current_state_name.startswith('STRING_') and token_value:
                if DEBUG:
                    print(f"_parse_from_entry END (STRING): type='s' value=\"{token_value}\" pos={start_pos}->{pos}")
                return Token('s', token_value, start_pos), pos
            if token_type:
                if DEBUG:
                    print(f"_parse_from_entry END: type='{token_type}' value=\"{token_value}\" pos={start_pos}->{pos}")
                return Token(token_type, token_value, start_pos), pos
            if DEBUG:
                print(f"_parse_from_entry END (BAREWORD): type='n' value=\"{token_value}\" pos={start_pos}->{pos}")
            return Token('n', token_value, start_pos), pos
        elif pos == start_pos:
            if token_type:
                if DEBUG:
                    print(f"_parse_from_entry SINGLE CHAR: type='{token_type}' value=\"{input_str[start_pos]}\" pos={start_pos}->{start_pos + 1}")
                return Token(token_type, input_str[start_pos], start_pos), start_pos + 1
            if DEBUG:
                print(f"_parse_from_entry SINGLE CHAR (BAREWORD): type='n' value=\"{input_str[start_pos]}\" pos={start_pos}->{start_pos + 1}")
            return Token('n', input_str[start_pos], start_pos), start_pos + 1

        if DEBUG:
            print(f"_parse_from_entry: No token generated, returning None")
        return None, start_pos


def main():
    """测试 DFA 引擎"""
    print("=" * 80)
    print("DFA 执行引擎测试")
    print("=" * 80)

    engine = DFAEngine()

    # 测试用例
    test_sections = {
        "10.1 parse_white - 空白字符处理": [
            "a   b", "SELECT\nFROM",
        ],
        "10.2 parse_operator1 - 单字符运算符": [
            "a % b", "x + y", "a ^ b", "~x",
        ],
        "10.3 parse_operator2 - 双字符运算符": [
            "a != b", "x && y", "i || j", "a << b", "a >> b",
            "a <= b", "a >= b", "a <> b", "a == b", "a <=> b", "!!", "::",
        ],
        "10.4 parse_char - 单字符标点": [
            "(a + b)", "func(a, b)", "BEGIN; END;", "{a: b}",
        ],
        "10.5 parse_other - 未知字符": [
            "?param", "a]",
        ],
        "10.6 parse_eol_comment - 行尾注释": [
            "-- comment\nSELECT", "# comment\nSELECT",
        ],
        "10.7 parse_dash - 减号和注释": [
            "a - b", "-- comment\na",
        ],
        "10.8 parse_slash - 斜杠和块注释": [
            "a / b", "/* comment */", "/*/",
        ],
        "10.9 parse_backslash - 反斜杠": [
            "\\N", "\\",
        ],
        "10.10 parse_hash - 井号": [
            "# MySQL comment",
        ],
        "10.11 parse_money - 货币符号": [
            "$", "$100", "$1,000.00", "$$string$$",
        ],
        "10.12 parse_number - 数字": [
            "0", "123", "3.14", "0xFF", "0b10101", "1.5e10", "123f",
        ],
        "10.13 parse_string - 字符串": [
            "'hello'", '"world"', "''", "'it''s'",
        ],
        "10.14 parse_word - 关键字和标识符": [
            "SELECT", "FROM", "WHERE", "identifier", "_underscore",
        ],
        "10.15 parse_tick - 反引号": [
            "`table`", "`SELECT`", "`COUNT`(*)",
        ],
        "10.16 parse_var - 变量": [
            "@variable", "@@global", "@`var`",
        ],
        "10.17 parse_bstring - 二进制字符串": [
            "B'10101'", "b'10101'",
        ],
        "10.18 parse_xstring - 十六进制字符串": [
            "X'FFA2'", "x'FFA2'",
        ],
        "10.19 parse_estring - 转义字符串": [
            "E'escaped\\nstring'", "e'escaped\\nstring'",
        ],
        "10.20 parse_ustring - Unicode字符串": [
            "U&'unicode string'", "u&'unicode string'",
        ],
        "10.21 parse_qstring - 引用字符串": [
            "q'(string)'", "q'[string]'", "q'{string}'", "q'<string>'",
        ],
        "10.22 parse_nqstring - 国家字符集字符串": [
            "N'string'", "n'string'",
        ],
        "10.23 parse_bword - 方括号标识符": [
            "[identifier]", "[identifier with spaces]",
        ],
        "综合测试 - SQL注入模式": [
            "passwd=1024 or 1=1",
            "SELECT * FROM users WHERE id=1",
            "'admin' OR '1'='1'",
            "'; DROP TABLE users; --",
            "1' UNION SELECT 1,2,3--",
        ],

        "高级测试 - complex SQL patterns (来自comparison_results.txt)": [
            # SQL 注入模式
            "1' OR '1'='1'--",
            "admin' UNION SELECT NULL,password,NULL FROM users WHERE username='admin'--",
            "1'; DROP TABLE users; --",
            "1' AND 1=1--",
            "' OR 1=1#",
            "1' OR 1=1--",
            "admin'--",
            "' UNION SELECT 1,version(),3--",
            "1' AND (SELECT COUNT(*) FROM users) > 0--",
            "'; EXEC xp_cmdshell('dir');--",

            # 实际 SQL 语句
            "SELECT * FROM users WHERE id=1 AND status='active'",
            "SELECT u.name, COUNT(o.id) as order_count FROM users u JOIN orders o ON u.id=o.user_id GROUP BY u.name",
            "INSERT INTO users (name,email,created_at) VALUES ('test@example.com','password123',NOW())",
            "UPDATE products SET price=price*1.1, updated_at=NOW() WHERE id=100",
            "DELETE FROM logs WHERE created_at < DATE_SUB(NOW(), INTERVAL 30 DAY)",
            "SELECT N'unicode string', E'escaped\nstring' FROM table1",

            # 类型转换和特殊字符串
            "SELECT U&'\\0041\\0042' as unicode, B'1010' as binary FROM table1",
            "SELECT n'national string', e'\\x41\\x42', x'FF' FROM table1",
            "SELECT q'[bracket string]', q'{brace string}', q'(paren string)' FROM table1",

            # 运算符组合
            "x && y || z ! w & a | b ^ c << e >> f",
            "a == b != c <> d <= e >= f <=> g",
            "a::text || b::int || c::date",
            "col1 % 2 = 0 AND col2 BETWEEN 1 AND 100",
            "value1 IN (1,2,3) AND value2 NOT IN (4,5,6)",

            # 复杂查询
            "SELECT * FROM (SELECT a,b FROM t1 WHERE x>0) AS subq",
            "CASE WHEN type=1 THEN 'one' WHEN type=2 THEN 'two' ELSE 'other' END",
            "COALESCE(NULLIF(a,b),c,d,e) as result",
            "SELECT CAST('123' AS UNSIGNED), CONVERT(varchar, 123)",
            "SELECT JSON_EXTRACT(data, '$.user.name') FROM users",

            # 注释测试
            "SELECT * FROM users -- 这是单行注释\nWHERE id=1",
            "/* 这是多行注释 */\nSELECT * FROM table",
            "# MySQL 注释风格\nSELECT 1",

            # 变量和函数
            "@@global.sql_mode, @session_var, @user_var",
            "SELECT CONCAT_WS(':', name, email), UPPER(name) FROM users",
            "SELECT IFNULL(value, 'default'), IF(cond, a, b) FROM table",
            "SELECT NOW(), CURRENT_DATE(), CURRENT_TIME FROM dual",
            "DATE_ADD('2024-01-01', INTERVAL 1 YEAR), TIMESTAMPDIFF(SECOND, start_time, end_time)",
        ],
        "Web/URI 测试": [
            "api.php?action=get);SELECT PG_SLEEP(5)--&table=user_profiles&id=1",
            "api.php?action=get));SELECT PG_SLEEP(5)--&table=user_profiles&id=1",
            "api.php?action=get;SELECT PG_SLEEP(5)--&table=user_profiles&id=1",
            "api.php?action=get);WAITFOR DELAY '0:0:5'--&table=user_profiles&id=1",
            "api.php?action=get));WAITFOR DELAY '0:0:5'--&table=user_profiles&id=1",
            "api.php?action=get;WAITFOR DELAY '0:0:5'--&table=user_profiles&id=1",
            "api.php?action=get);DECLARE @x CHAR(9);SET @x=0x303a303a35;WAITFOR DELAY @x--&table=user_profiles&id=1",
            "api.php?action=get));DECLARE @x CHAR(9);SET @x=0x303a303a35;WAITFOR DELAY @x--&table=user_profiles&id=1",
            "api.php?action=get;DECLARE @x CHAR(9);SET @x=0x303a303a35;WAITFOR DELAY @x--&table=user_profiles&id=1",
            "api.php?action=get);SELECT DBMS_PIPE(CHR(121)||CHR(83)||CHR(78)||CHR(70),5) FROM DUAL--&table=user_profiles&id=1",
            "index.php?table=user_profiles&id=1\")));SELECT SLEEP(5) AND (((\"eQVc\"=\"eQVc",
            "search.php?q=test\")));(SELECT * FROM (SELECT(SLEEP(5)))mwZE) AND (((\"taNc\" LIKE \"taNc",
            "api.php?action=get);SELECT DBMS_PIPE.RECEIVE_MESSAGE(CHR(82)||CHR(102)||CHR(121)||CHR(71),5) FROM DUAL--&table=user_profiles&id=1",
            "index.php?table=products&id=1';SELECT SLEEP(5) AND 'notB'='notB",
            "index.php?table=articles&id=1';SELECT SLEEP(5) AND 'NOtr'='NOtr",
            "index.php?table=products&id=51&ref=search",
            "jenkins/job/Ksim_CI/wfapi/runs?since=#289&fullStages=true&_=1767144313183",
            "api.php?action=get&table=user_profiles\") AS AyWa WHERE 8450=8450;SELECT SLEEP(5)-- mFBr&id=1",
            "index.php?table=user_profiles(',)....\",&id=1",
            "index.php?table=user_profiles'iWoioD<'\">HOvEOc&id=1",
            "index.php?table=user_profiles\")) AS OPBO WHERE 9345=9345;SELECT SLEEP(5)#&id=1",
            "api.php?action=get&table=user_profiles`=`user_profiles`;SELECT SLEEP(5)#&id=1",
            "index.php?table=user_profiles` WHERE 8358=8358;SELECT SLEEP(5)#&id=1",
            "api.php?action=get&table=user_profiles` WHERE 3424=3424;SELECT SLEEP(5)-- LTup&id=1",
            "api.php?action=get&table=user_profiles')) AS mpsf WHERE 1708=1708;SELECT SLEEP(5)#&id=1",
            "api.php?action=get');SELECT DBMS_PIPE.RECEIVE_MESSAGE(CHR(75)||CHR(74)||CHR(66)||CHR(76),5) FROM DUAL--&table=user_profiles&id=1",
            "api.php?action=get);SELECT DBMS_PIPE.RECEIVE_MESSAGE(CHR(121)||CHR(83)||CHR(78)||CHR(70),5) FROM DUAL--&table=user_profiles&id=1",
            "api.php?action=get&table=user_profiles`=`user_profiles`;SELECT SLEEP(5)#&id=1",
            "api.php?action=get&table=user_profiles WHERE 1224=1224 UNION ALL SELECT NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,CONCAT(0x716a767871,(CASE WHEN ((SELECT super_priv FROM mysql.user WHERE user=0x77656275736572 LIMIT 0,1)=0x59) THEN 1 ELSE 0 END),0x71786a7871),NULL,NULL,NULL,NULL-- -&id=1"
        ],
        "手工 测试": [
            "search.php?q=test\")));(SELECT * FROM (SELECT(SLEEP(5)))mwZE) AND (((\"taNc\" LIKE \"taNc",
            "api.php?action=get&table=user_profiles);SELECT BENCHMARK(5000000,MD5(0x676d514e))-- FdDd&id=1",
            "index.php?table=user_profiles]-(SELECT 0 WHERE 4468=4468;SELECT SLEEP(5))|[user_profiles&id=1",
            "index.php?table=products&id=1\")));SELECT BENCHMARK(5000000,MD5(0x7965694c)) AND (((\"powc\"=\"powc",
            "index.php?table=user_profiles\");SELECT BENCHMARK(5000000,MD5(0x6164476d)) AND (\"fOrC\"=\"fOrC&id=1",
            "search.php?q=test\")));(SELECT * FROM (SELECT(SLEEP(5)))mwZE) AND (((\"taNc\" LIKE \"taNc",

        ],
    }

    total_tests = 0
    passed_tests = 0
    failed_tests = []
    
    # 只测试第一个章节（用于调试）
    section_list = list(test_sections.items())
    for section_name, test_inputs in section_list[-1:]:
        print(f"\n{'=' * 80}")
        print(f"测试章节: {section_name}")
        print(f"{'=' * 80}")

        for test in test_inputs:
            total_tests += 1
            # test_display = test.replace('\n', '\\n')
            print(f"\n输入: '{test}'")
            print("-" * 60)

            try:
                tokens = engine.tokenize(test)
                token_indices = engine.tokenize_to_indices(test)
                # print(f"分词结果 ({len(tokens)} 个 tokens):")
                # for i, tok in enumerate(tokens):
                #     print(f"  [{i}] {tok}")

                print(f"Token 类型索引: {token_indices}")

                token_seq = ' '.join([f"{t.value}/{t.type}" for t in tokens])
                # print(f"Token 序列: {token_seq}")

                if tokens:
                    passed_tests += 1
                else:
                    failed_tests.append((section_name, test, "未生成任何 token"))

            except Exception as e:
                print(f"错误: {e}")
                failed_tests.append((section_name, test, str(e)))

    print("\n" + "=" * 80)
    print("测试摘要")
    print("=" * 80)
    print(f"总测试数: {total_tests}")
    print(f"通过: {passed_tests}")
    print(f"失败: {len(failed_tests)}")

    if failed_tests:
        print("\n失败的测试:")
        for section, test, error in failed_tests:
            print(f"  [{section}] '{test}': {error}")

    print("=" * 80)

    return len(failed_tests) == 0


if __name__ == "__main__":
    success = main()
    # sys.exit(0 if success else 1)
