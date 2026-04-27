#include "kadd_dfa_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#define KADD_DFA_ID_ERROR 65535
#define KADD_DFA_ID_ENTRY 0
#define KADD_DFA_INS_MAGIC 0xFFFE
#define MAX_BUF_SIZE 8192
#define KADD_DFA_INLINE_STRING_SIZE 32

/* ========== 内部结构体定义 ========== */

/**
 * @brief DFA 引擎错误代码（内部使用）
 */
typedef enum {
    KADD_DFA_OK = 0,
    KADD_DFA_ERROR_MEMORY = -1,
    KADD_DFA_ERROR_INVALID_INPUT = -2,
    KADD_DFA_ERROR_FILE_NOT_FOUND = -3,
    KADD_DFA_ERROR_PARSE = -4,
    KADD_DFA_ERROR_BUFFER_OVERFLOW = -5,
    KADD_DFA_ERROR_INVALID_LENGTH = -6
} kadd_dfa_error_t;

/**
 * @brief 状态转移条目（内部使用）
 */
typedef struct {
    int is_insn;                         /**< 是否为指令 */
    union {
        uint16_t next_state;              /**< 下一状态ID */
        char *insn;                        /**< 指令字符串 */
    };
} kadd_trans_entry_t;

/**
 * @brief 状态元数据（内部使用）
 */
typedef struct {
    char *token_type;                    /**< Token类型 */
    int action;                          /**< 动作类型 */
    char *action_arg;                    /**< 动作参数 */
} kadd_state_meta_t;

/**
 * @brief DFA 引擎结构（内部定义）
 */
struct kadd_dfa_engine_t {
    char table_path[256];                /**< 转移表文件路径 */
    char state_path[256];                /**< 状态元数据文件路径 */
    uint32_t num_states;                 /**< 状态数量 */
    char **state_names;                  /**< 状态名称数组 */
    kadd_trans_entry_t **transitions;    /**< 状态转移表 */
    kadd_state_meta_t *meta;             /**< 状态元数据 */
    uint8_t *state_types;                /**< 状态类型数组 */
    uint8_t *state_hashes;               /**< 状态哈希数组 */
    int id_word;                        /**< WORD状态ID */
    size_t max_input_length;             /**< 最大输入字符长度 */
    size_t max_output_dim;               /**< 最大输出特征维度 */
};

#if defined(__GNUC__) || defined(__clang__)
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define likely(x)   (x)
#define unlikely(x) (x)
#endif

// Token结构（内部使用）
typedef struct {
    char type;
    size_t pos;
    int is_inline;
    union {
        char value_inline[KADD_DFA_INLINE_STRING_SIZE];
        char *value_ptr;
    } value;
} kadd_token_t;

// 前向声明
static int parse_token(const kadd_dfa_engine_t *e, const char *s, size_t len,
                       size_t start_pos, kadd_token_t *out, size_t *new_pos, int *state_ptr);
static int kadd_dfa_engine_load_tables(kadd_dfa_engine_t *e);
typedef enum {
    STATE_TYPE_UNKNOWN = 0,
    STATE_TYPE_ENTRY,
    STATE_TYPE_WORD,
    STATE_TYPE_BWORD,
    STATE_TYPE_WORD_KEYWORD,  /* WORD_* 关键字状态 */
    STATE_TYPE_COMMENT,
    STATE_TYPE_STRING,
    STATE_TYPE_QSTRING,
    STATE_TYPE_NQSTRING,
    STATE_TYPE_NUMBER,
    STATE_TYPE_OPERATOR,
    STATE_TYPE_ERROR
} state_type_t;
typedef struct {
    const char *name;
    state_type_t type;  /* 预计算：状态类型 */
    uint8_t hash;       /* 状态名哈希，用于快速比较 */
} state_name_cache_t;
const char *kadd_dfa_error_string(kadd_dfa_error_t error) {
    switch (error) {
        case KADD_DFA_OK: return "Success";
        case KADD_DFA_ERROR_MEMORY: return "Memory allocation failed";
        case KADD_DFA_ERROR_INVALID_INPUT: return "Invalid input";
        case KADD_DFA_ERROR_FILE_NOT_FOUND: return "File not found";
        case KADD_DFA_ERROR_PARSE: return "Parse error";
        case KADD_DFA_ERROR_BUFFER_OVERFLOW: return "Buffer overflow";
        default: return "Unknown error";
    }
}
static char *find_json_str_fast(char *json, const char *key, size_t json_len) {
    size_t key_len = strlen(key);
    size_t search_len = key_len + 4;  /* '"' + key + '":' */
    /* 使用 C99 变长数组替代 alloca */
    char search[search_len + 1];
    snprintf(search, search_len + 1, "\"%s\":", key);
    char *p = strstr(json, search);
    if (!p) return NULL;
    p += search_len;  /* 跳过 '"key":' */
    while (p < json + json_len && (*p == ' ' || *p == '\t' || *p == '\n')) {
        p++;
    }
    return p;
}
static char *extract_json_str_fast(char **value_ptr, char *json_end) {
    char *p = *value_ptr;
    char *result = NULL;
    if (p >= json_end) {
        return NULL;
    }
    if (*p == '"') {
        p++;
        char *end = strchr(p, '"');
        if (end && end < json_end) {
            size_t len = end - p;
            result = malloc(len + 1);
            if (result) {
                memcpy(result, p, len);
                result[len] = '\0';
                *value_ptr = end + 1;
            }
            return result;
        }
    } else if (*p == 'n' && p + 4 <= json_end && strncmp(p, "null", 4) == 0) {
        *value_ptr = p + 4;
        return NULL;
    }
    return NULL;
}
static int extract_json_int_fast(char **value_ptr, char *json_end) {
    char *p = *value_ptr;
    while (p < json_end && (*p == ' ' || *p == '\t' || *p == '\n')) {
        p++;
    }
    if (p < json_end && (isdigit((unsigned char)*p) || *p == '-')) {
        char *end;
        long val = strtol(p, &end, 10);
        *value_ptr = end;
        return (int)val;
    }
    return 0;
}

static char map_token_type(const char *raw_type) {
    if (!raw_type) return 0;
    if (strlen(raw_type) == 1) {
        return raw_type[0];
    }
    /* Map TYPE_* strings to single characters
     * 对应 Python 版本中的 TOKEN_TYPE_MAP */
    if (strcmp(raw_type, "TYPE_KEYWORD") == 0) return 'k';
    if (strcmp(raw_type, "TYPE_UNION") == 0) return 'U';
    if (strcmp(raw_type, "TYPE_GROUP") == 0) return 'B';
    if (strcmp(raw_type, "TYPE_EXPRESSION") == 0) return 'E';
    if (strcmp(raw_type, "TYPE_SQLTYPE") == 0) return 't';
    if (strcmp(raw_type, "TYPE_FUNCTION") == 0) return 'f';
    if (strcmp(raw_type, "TYPE_BAREWORD") == 0) return 'n';
    if (strcmp(raw_type, "TYPE_NUMBER") == 0) return '1';
    if (strcmp(raw_type, "TYPE_VARIABLE") == 0) return 'v';
    if (strcmp(raw_type, "TYPE_STRING") == 0) return 's';
    if (strcmp(raw_type, "TYPE_OPERATOR") == 0) return 'o';
    if (strcmp(raw_type, "TYPE_LOGIC_OPERATOR") == 0) return '&';
    if (strcmp(raw_type, "TYPE_COMMENT") == 0) return 'c';
    if (strcmp(raw_type, "TYPE_LEFTPARENS") == 0) return '(';
    if (strcmp(raw_type, "TYPE_RIGHTPARENS") == 0) return ')';
    if (strcmp(raw_type, "TYPE_LEFTBRACE") == 0) return '{';
    if (strcmp(raw_type, "TYPE_RIGHTBRACE") == 0) return '}';
    if (strcmp(raw_type, "TYPE_DOT") == 0) return '.';
    if (strcmp(raw_type, "TYPE_COMMA") == 0) return ',';
    if (strcmp(raw_type, "TYPE_COLON") == 0) return ':';
    if (strcmp(raw_type, "TYPE_SEMICOLON") == 0) return ';';
    if (strcmp(raw_type, "TYPE_STATEMENT") == 0) return 'T';
    if (strcmp(raw_type, "TYPE_UNKNOWN") == 0) return '?';
    if (strcmp(raw_type, "TYPE_EVIL") == 0) return 'X';
    if (strcmp(raw_type, "TYPE_FINGERPRINT") == 0) return 'F';
    if (strcmp(raw_type, "TYPE_BACKSLASH") == 0) return '\\';
    if (strcmp(raw_type, "TYPE_COLLATE") == 0) return 'A';
    return 0;
}

static int char_type_to_index(int c) {
    /* 将字符类型映射到索引值
     * 对应 libinjection sqli_token_type 到索引的转换
     * 与 Python 版本 dfa_engine_copy.py 中的 char_type_to_index 保持一致
     */
    switch (c) {
        case 0:     return 0;   /* TYPE_NONE */
        case 'k':   return 1;   /* TYPE_KEYWORD */
        case 'U':   return 2;   /* TYPE_UNION */
        case 'B':   return 3;   /* TYPE_GROUP */
        case 'E':   return 4;   /* TYPE_EXPRESSION */
        case 't':   return 5;   /* TYPE_SQLTYPE */
        case 'f':   return 6;   /* TYPE_FUNCTION */
        case 'n':   return 7;   /* TYPE_BAREWORD */
        case '1':   return 8;   /* TYPE_NUMBER */
        case 'v':   return 9;   /* TYPE_VARIABLE */
        case 's':   return 10;  /* TYPE_STRING */
        case 'o':   return 11;  /* TYPE_OPERATOR */
        case '&':   return 12;  /* TYPE_LOGIC_OPERATOR */
        case 'c':   return 13;  /* TYPE_COMMENT */
        case 'A':   return 14;  /* TYPE_COLLATE */
        case '(':   return 15;  /* TYPE_LEFTPARENS */
        case ')':   return 16;  /* TYPE_RIGHTPARENS */
        case '{':   return 17;  /* TYPE_LEFTBRACE */
        case '}':   return 18;  /* TYPE_RIGHTBRACE */
        case '.':   return 19;  /* TYPE_DOT */
        case ',':   return 20;  /* TYPE_COMMA */
        case ':':   return 21;  /* TYPE_COLON */
        case ';':   return 22;  /* TYPE_SEMICOLON */
        case 'T':   return 23;  /* TYPE_STATEMENT */
        case '?':   return 24;  /* TYPE_UNKNOWN */
        case 'X':   return 25;  /* TYPE_EVIL */
        case 'F':   return 26;  /* TYPE_FINGERPRINT */
        case '\\':  return 27;  /* TYPE_BACKSLASH */
        default:  return 24;  /* 默认返回 TYPE_UNKNOWN (24) */
    }
}
static char *str_dup(const char *str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char *copy = malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, str, len + 1);
    return copy;
}
static char *url_decode(const char *str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char *decoded = malloc(len + 1);
    if (!decoded) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '%' && i + 2 < len &&
            isxdigit((unsigned char)str[i + 1]) && isxdigit((unsigned char)str[i + 2])) {
            char hex[3] = {str[i + 1], str[i + 2], 0};
            decoded[j++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else {
            decoded[j++] = str[i];
        }
    }
    decoded[j] = '\0';
    return decoded;
}
static const uint8_t char_class_map[256] = {
    [' '] = 1,  ['\t'] = 1, ['\n'] = 1, ['\r'] = 1, ['\f'] = 1, ['\v'] = 1,
    ['0'] = 2, ['1'] = 2, ['2'] = 2, ['3'] = 2, ['4'] = 2,
    ['5'] = 2, ['6'] = 2, ['7'] = 2, ['8'] = 2, ['9'] = 2,
    ['A'] = 2, ['B'] = 2, ['C'] = 2, ['D'] = 2, ['E'] = 2,
    ['F'] = 2, ['G'] = 2, ['H'] = 2, ['I'] = 2, ['J'] = 2,
    ['K'] = 2, ['L'] = 2, ['M'] = 2, ['N'] = 2, ['O'] = 2,
    ['P'] = 2, ['Q'] = 2, ['R'] = 2, ['S'] = 2, ['T'] = 2,
    ['U'] = 2, ['V'] = 2, ['W'] = 2, ['X'] = 2, ['Y'] = 2,
    ['Z'] = 2,
    ['a'] = 2, ['b'] = 2, ['c'] = 2, ['d'] = 2, ['e'] = 2,
    ['f'] = 2, ['g'] = 2, ['h'] = 2, ['i'] = 2, ['j'] = 2,
    ['k'] = 2, ['l'] = 2, ['m'] = 2, ['n'] = 2, ['o'] = 2,
    ['p'] = 2, ['q'] = 2, ['r'] = 2, ['s'] = 2, ['t'] = 2,
    ['u'] = 2, ['v'] = 2, ['w'] = 2, ['x'] = 2, ['y'] = 2,
    ['z'] = 2,
    ['_'] = 2, ['$'] = 2, ['`'] = 2, ['@'] = 2, ['#'] = 2,
    ['.'] = 2,
    ['\''] = 4, ['"'] = 4, ['\\'] = 4,  /* 引号和转义符 */
    ['-'] = 5, ['/'] = 5,  /* 用于 -- 和 // 注释 */
    ['('] = 3, [')'] = 3, ['['] = 3, [']'] = 3,
    ['{'] = 3, ['}'] = 3, [','] = 3, [';'] = 3,
    [':'] = 3, ['+'] = 3, ['*'] = 3, ['^'] = 3,
    ['~'] = 3, ['%'] = 3, ['&'] = 3, ['|'] = 3,
    ['<'] = 3, ['>'] = 3, ['='] = 3, ['!'] = 3, ['?'] = 3,
};
static inline int is_whitespace(int c) {
    return (c >= 0 && c < 256) ? (char_class_map[c] == 1) : 0;
}
static inline int is_identifier_char(int c) {
    if (c >= 0 && c < 256) {
        uint8_t cls = char_class_map[c];
        return cls == 2 && c != '.';  /* 排除点号 */
    }
    return 0;
}
static inline int is_word_char(int c) {
    if (c >= 0 && c < 256) {
        uint8_t cls = char_class_map[c];
        return cls == 2;  /* 包含点号 */
    }
    return 0;
}
static inline int is_delimiter_char(int c) {
    if (c >= 0 && c < 256) {
        uint8_t cls = char_class_map[c];
        return cls == 3 || cls == 1;  /* 分隔符或空白 */
    }
    return 0;
}
static inline int is_string_char(int c) {
    if (c >= 0 && c < 256) {
        return char_class_map[c] == 4;
    }
    return 0;
}
static inline int is_comment_char(int c) {
    if (c >= 0 && c < 256) {
        return char_class_map[c] == 5;
    }
    return 0;
}
static inline const char *token_get_value(const kadd_token_t *tok) {
    if (!tok) return NULL;
    return tok->is_inline ? tok->value.value_inline : tok->value.value_ptr;
}
static inline uint8_t state_name_hash(const char *name) {
    uint8_t hash = 0;
    while (*name) {
        hash = (hash << 1) ^ (*name & 0x1F);
        name++;
    }
    return hash;
}
int kadd_dfa_engine_create(kadd_dfa_engine_t **engine,
                           const char *table_path,
                           const char *state_path,
                           size_t max_input_length,
                           size_t max_output_dim) {
    if (!engine || !table_path || !state_path) {
        return KADD_DFA_ERROR_INVALID_INPUT;
    }
    if (max_input_length == 0 || max_output_dim == 0) {
        return KADD_DFA_ERROR_INVALID_INPUT;
    }

    kadd_dfa_engine_t *e = calloc(1, sizeof(kadd_dfa_engine_t));
    if (!e) return KADD_DFA_ERROR_MEMORY;

    snprintf(e->table_path, sizeof(e->table_path), "%s", table_path);
    snprintf(e->state_path, sizeof(e->state_path), "%s", state_path);
    e->max_input_length = max_input_length;
    e->max_output_dim = max_output_dim;

    if (kadd_dfa_engine_load_tables(e) != 0) {
        kadd_dfa_engine_free(e);
        return KADD_DFA_ERROR_FILE_NOT_FOUND;
    }
    *engine = e;
    return KADD_DFA_OK;
}
void kadd_dfa_engine_free(kadd_dfa_engine_t *e) {
    if (!e) return;
    if (e->transitions) {
        for (uint32_t i = 0; i < e->num_states; i++) {
            for (int j = 0; j < 256; j++) {
                if (e->transitions[i][j].is_insn) {
                    free(e->transitions[i][j].insn);
                }
            }
            free(e->transitions[i]);
        }
        free(e->transitions);
    }
    if (e->state_names) {
        for (uint32_t i = 0; i < e->num_states; i++) {
            free(e->state_names[i]);
        }
        free(e->state_names);
    }
    if (e->meta) {
        for (uint32_t i = 0; i < e->num_states; i++) {
            free(e->meta[i].token_type);
            free(e->meta[i].action_arg);
        }
        free(e->meta);
    }
    free(e->state_types);
    free(e->state_hashes);
    free(e);
}
static inline state_type_t get_state_type(const kadd_dfa_engine_t *e, int state) {
    if (state >= 0 && state < (int)e->num_states) {
        return (state_type_t)e->state_types[state];
    }
    return STATE_TYPE_UNKNOWN;
}
static inline int is_comment_state(const kadd_dfa_engine_t *e, int state) {
    state_type_t type = get_state_type(e, state);
    return type == STATE_TYPE_COMMENT;
}
static inline int is_word_state(const kadd_dfa_engine_t *e, int state) {
    state_type_t type = get_state_type(e, state);
    return type == STATE_TYPE_WORD || type == STATE_TYPE_BWORD || type == STATE_TYPE_WORD_KEYWORD;
}
static inline int is_string_state(const kadd_dfa_engine_t *e, int state) {
    state_type_t type = get_state_type(e, state);
    return type == STATE_TYPE_STRING || type == STATE_TYPE_QSTRING || type == STATE_TYPE_NQSTRING;
}
static inline int is_keyword_state(const kadd_dfa_engine_t *e, int state) {
    state_type_t type = get_state_type(e, state);
    return type == STATE_TYPE_WORD_KEYWORD;
}
int kadd_dfa_engine_load_tables(kadd_dfa_engine_t *e) {
    FILE *f = fopen(e->table_path, "rb");
    if (!f) {
        return -1;
    }

    uint32_t num_states;
    if (fread(&num_states, sizeof(uint32_t), 1, f) != 1) {
        fclose(f);
        return -1;
    }

    e->num_states = num_states;
    e->state_types = calloc(num_states, sizeof(uint8_t));
    e->state_hashes = calloc(num_states, sizeof(uint8_t));
    e->state_names = calloc(num_states, sizeof(char *));

    if (!e->state_types || !e->state_hashes || !e->state_names) {
        fclose(f);
        free(e->state_types);
        free(e->state_hashes);
        free(e->state_names);
        return -1;
    }

    for (uint32_t i = 0; i < num_states; i++) {
        uint8_t len;
        if (fread(&len, sizeof(uint8_t), 1, f) != 1) {
            fclose(f);
            return -1;
        }
        e->state_names[i] = malloc(len + 1);
        if (fread(e->state_names[i], 1, len, f) != len) {
            fclose(f);
            return -1;
        }
        e->state_names[i][len] = '\0';

        const char *name = e->state_names[i];
        e->state_hashes[i] = state_name_hash(name);

        state_type_t type = STATE_TYPE_UNKNOWN;
        if (strcmp(name, "ENTRY") == 0) {
            type = STATE_TYPE_ENTRY;
        } else if (strcmp(name, "WORD") == 0) {
            type = STATE_TYPE_WORD;
        } else if (strcmp(name, "BWORD") == 0) {
            type = STATE_TYPE_BWORD;
        } else if (strncmp(name, "WORD_", 5) == 0) {
            type = STATE_TYPE_WORD_KEYWORD;
        } else if (strstr(name, "COMMENT") != NULL ||
                   strcmp(name, "SLASH_ASTERISK") == 0 ||
                   strcmp(name, "HASH_COMMENT") == 0 ||
                   strcmp(name, "DASH_COMMENT") == 0 ||
                   strcmp(name, "COMMENT_COMPLETE") == 0) {
            type = STATE_TYPE_COMMENT;
        } else if (strncmp(name, "STRING_", 7) == 0) {
            type = STATE_TYPE_STRING;
        } else if (strstr(name, "QSTRING") != NULL) {
            type = STATE_TYPE_QSTRING;
        } else if (strstr(name, "NQSTRING") != NULL) {
            type = STATE_TYPE_NQSTRING;
        } else if (strstr(name, "BSTRING") != NULL || strstr(name, "XSTRING") != NULL ||
                   strstr(name, "ESTRING") != NULL || strstr(name, "NSTRING") != NULL ||
                   strstr(name, "USTRING") != NULL) {
            /* 特殊字符串前缀状态（B', X', E', N', U& 等）应该被识别为字符串状态 */
            type = STATE_TYPE_STRING;
        } else if (strcmp(name, "NUMBER") == 0 || strncmp(name, "NUM", 3) == 0) {
            type = STATE_TYPE_NUMBER;
        } else if (strstr(name, "OPERATOR") != NULL) {
            type = STATE_TYPE_OPERATOR;
        }
        e->state_types[i] = (uint8_t)type;

        if (strcmp(name, "WORD") == 0) {
            e->id_word = (int)i;
        }
    }

    e->transitions = calloc(num_states, sizeof(kadd_trans_entry_t *));
    if (!e->transitions) {
        fclose(f);
        return -1;
    }

    for (uint32_t i = 0; i < num_states; i++) {
        e->transitions[i] = calloc(256, sizeof(kadd_trans_entry_t));
        if (!e->transitions[i]) {
            fclose(f);
            return -1;
        }
        for (int j = 0; j < 256; j++) {
            uint16_t val;
            if (fread(&val, sizeof(uint16_t), 1, f) != 1) {
                fclose(f);
                return -1;
            }
            if (val == KADD_DFA_INS_MAGIC) {
                uint8_t len;
                if (fread(&len, sizeof(uint8_t), 1, f) != 1) {
                    fclose(f);
                    return -1;
                }
                e->transitions[i][j].is_insn = 1;
                e->transitions[i][j].insn = malloc(len + 1);
                if (fread(e->transitions[i][j].insn, 1, len, f) != len) {
                    fclose(f);
                    return -1;
                }
                e->transitions[i][j].insn[len] = '\0';
            } else {
                e->transitions[i][j].is_insn = 0;
                e->transitions[i][j].next_state = val;
            }
        }
    }
    fclose(f);

    FILE *jf = fopen(e->state_path, "rb");
    if (jf) {
        fseek(jf, 0, SEEK_END);
        long jsize = ftell(jf);
        fseek(jf, 0, SEEK_SET);
        char *json = malloc(jsize + 1);
        fread(json, 1, jsize, jf);
        json[jsize] = '\0';
        fclose(jf);

        e->meta = calloc(num_states, sizeof(kadd_state_meta_t));
        if (e->meta) {
            char *states_start = strstr(json, "\"states\"");
            if (states_start) {
                states_start = strchr(states_start, '{');
                if (states_start) {
                    char *json_end __attribute__((unused)) = json + jsize;
                    for (uint32_t i = 0; i < num_states; i++) {
                        const char *name = e->state_names[i];
                        char search[280];
                        snprintf(search, sizeof(search), "\"%s\": {", name);
                        char *state_p = strstr(states_start, search);
                        if (state_p) {
                            char *obj_start = strchr(state_p, '{');
                            if (obj_start) {
                                int brace_count = 1;
                                char *obj_end = obj_start + 1;
                                while (*obj_end && brace_count > 0) {
                                    if (*obj_end == '{') brace_count++;
                                    else if (*obj_end == '}') brace_count--;
                                    obj_end++;
                                }
                                char *p = find_json_str_fast(obj_start, "token_type", obj_end - obj_start);
                                if (p && p < obj_end) {
                                    e->meta[i].token_type = extract_json_str_fast(&p, obj_end);
                                }
                                p = find_json_str_fast(obj_start, "action", obj_end - obj_start);
                                if (p && p < obj_end) {
                                    e->meta[i].action = extract_json_int_fast(&p, obj_end);
                                }
                                p = find_json_str_fast(obj_start, "action_arg", obj_end - obj_start);
                                if (p && p < obj_end) {
                                    char *arg = extract_json_str_fast(&p, obj_end);
                                    if (arg) {
                                        e->meta[i].action_arg = malloc(strlen(arg) + 1);
                                        strcpy(e->meta[i].action_arg, arg);
                                        free(arg);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        free(json);
    } else {
        e->meta = calloc(num_states, sizeof(kadd_state_meta_t));
    }

    return 0;
}
static int handle_qstring_format(const char *buf, size_t buf_len,
                                  kadd_token_t *out, size_t start_pos, size_t pos __attribute__((unused))) {
    if (buf_len >= 5 && buf[0] == 'q' && buf[1] == '\'') {
        char open_delim = buf[2];
        char close_delim = buf[buf_len - 2];
        if ((open_delim == '(' && close_delim == ')') ||
            (open_delim == '[' && close_delim == ']') ||
            (open_delim == '{' && close_delim == '}') ||
            (open_delim == '<' && close_delim == '>')) {
            size_t content_len = buf_len - 5;
            out->type = 's';
            out->pos = start_pos + 3;
            if (content_len < KADD_DFA_INLINE_STRING_SIZE - 1) {
                out->is_inline = 1;
                memcpy(out->value.value_inline, buf + 3, content_len);
                out->value.value_inline[content_len] = '\0';
            } else {
                out->is_inline = 0;
                out->value.value_ptr = malloc(content_len + 1);
                if (!out->value.value_ptr) return -1;
                memcpy(out->value.value_ptr, buf + 3, content_len);
                out->value.value_ptr[content_len] = '\0';
            }
            return 1;  /* handled */
        }
    }
    /* 删除单字符处理逻辑，让parse_token函数自己处理
     * 原来的逻辑会将所有非特殊字符设置为'n'，导致操作符被错误识别
     * 现在parse_token已经正确处理了所有情况，包括设置type='o' */
    return 0;  /* not handled */
}
static inline int can_continue_scanning(const kadd_dfa_engine_t *e, int state, unsigned char next_code, int pos_in_range) {
    if (!pos_in_range) return 0;
    /* 对于WORD_*状态（关键词/函数），首先检查下一个字符是否是标识符字符
     * 如果是标识符字符，则继续扫描而不检查转移表
     * 这对应Python版本的行为：libinjection只在整个单词匹配时才识别为关键词
     * 例如："iFca"应该作为BAREWORD而不是FUNCTION+后续字符 */
    const char *name = NULL;
    if (state >= 0 && state < (int)e->num_states) {
        name = e->state_names[state];
        if (strncmp(name, "WORD_", 5) == 0 || strcmp(name, "WORD") == 0 || strcmp(name, "BWORD") == 0) {
            /* 检查是否是标识符字符：A-Z, a-z, 0-9, _, $, ., ` */
            int is_identifier_char = (65 <= next_code && next_code <= 90) ||  /* A-Z */
                                     (97 <= next_code && next_code <= 122) || /* a-z */
                                     (48 <= next_code && next_code <= 57) ||  /* 0-9 */
                                     (next_code == 95) ||  /* _ */
                                     (next_code == 36) ||  /* $ */
                                     (next_code == 46) ||  /* . */
                                     (next_code == 96);    /* ` */
            if (is_identifier_char) {
                return 1;  /* 继续扫描 */
            }
        }
    }
    /* 首先检查转移表（必须在状态名称检查之前） */
    const kadd_trans_entry_t *ent = &e->transitions[state][next_code];
    if (ent->is_insn || ent->next_state == KADD_DFA_ID_ERROR) {
        return 0;
    }
    /* 特殊处理：如果目标状态是特殊前缀字符串状态（BSTRING, XSTRING等），
     * 则继续扫描以包含前缀字符到字符串token中
     * 这对于处理 B'10101', X'FFA2' 等特殊字符串前缀很重要 */
    if (!ent->is_insn && ent->next_state < e->num_states && name != NULL) {
        if (strcmp(name, "WORD") == 0 || strcmp(name, "BWORD") == 0 ||
            strncmp(name, "WORD_", 5) == 0) {
            const char *next_state_name = e->state_names[ent->next_state];
            if (strcmp(next_state_name, "BSTRING") == 0 ||
                strcmp(next_state_name, "XSTRING") == 0 ||
                strcmp(next_state_name, "ESTRING") == 0 ||
                strcmp(next_state_name, "NSTRING") == 0 ||
                strcmp(next_state_name, "USTRING") == 0) {
                return 1;  /* 继续扫描，不发出token */
            }
        }
    }
    /* 对于操作符相关的状态，不继续扫描（每个操作符独立生成token）
     * 通过状态名称判断：包含DASH、SLASH、ANGLE、EQUAL、PLUS、STAR等操作符关键词的状态
     */
    if (state >= 0 && state < (int)e->num_states && name != NULL) {
        /* 修复：对于多字符操作符，需要允许继续扫描
         * 例如：|| (PIPE->PIPE_PIPE), && (AMPERSAND->AMPERSAND_AMPERSAND)
         * == (EQUAL->EQUAL_EQUAL), != (BANG->BANG_EQUAL) 等
         * 使用strncmp检查精确的状态名匹配，避免误匹配（如EQUAL不匹配ANGLE_EQUAL） */
        if ((strcmp(name, "OPERATOR2_PIPE") == 0 && next_code == 124) ||  /* | -> || */
            (strcmp(name, "OPERATOR2_AMP") == 0 && next_code == 38) ||    /* & -> && */
            (strcmp(name, "OPERATOR2_EQ") == 0 && next_code == '=') ||    /* = -> == */
            (strcmp(name, "OPERATOR2_NOT") == 0 && next_code == '=') ||   /* ! -> != */
            (strcmp(name, "OPERATOR2_NOT") == 0 && next_code == '!') ||   /* ! -> !! */
            (strcmp(name, "OPERATOR2_LT") == 0 && (next_code == '=' || next_code == '<')) ||  /* < -> <=, << */
            (strcmp(name, "OPERATOR2_GT") == 0 && (next_code == '=' || next_code == '>')) ||   /* > -> >=, >> */
            (strcmp(name, "OPERATOR2_COLON") == 0 && next_code == ':') ||  /* : -> :: */
            (strcmp(name, "PLUS") == 0 && next_code == '+') ||            /* + -> ++ */
            (strcmp(name, "DASH") == 0 && next_code == '-') ||            /* - -> -- */
            (strcmp(name, "STAR") == 0 && next_code == '*')) {            /* * -> ** */
            return 1;  /* Allow multi-character operators to form */
        }

        /* 特别处理：OPERATOR2_AMP 和 OPERATOR2_NOT 状态需要额外检查
         * 即使转移表中没有明确的转移，也要允许继续扫描
         * 这是为了处理 && 和 != 的情况 */
        if ((strcmp(name, "OPERATOR2_AMP") == 0 && next_code == 38) ||  /* && */
            (strcmp(name, "OPERATOR2_NOT") == 0 && next_code == 61)) {   /* != */
            return 1;
        }
        /* 修复：对于DASH状态，如果下一个字符是-，则继续扫描（形成--注释）
         * 对于SLASH状态，如果下一个字符是/或*，则继续扫描（形成//或/*注释） */
        if (strcmp(name, "DASH") == 0 && next_code == '-') {
            return 1;
        }
        if (strcmp(name, "SLASH") == 0 && (next_code == '/' || next_code == '*')) {
            return 1;
        }
        /* 修复：对于COMMENT状态（如DASH_COMMENT、SLASH_ASTERISK等），应该继续扫描
         * 注释会一直持续到结束，不应该在遇到非空白字符时停止
         * 但前提是转移不是错误（已在上面检查） */
        state_type_t type = get_state_type(e, state);
        if (type == STATE_TYPE_COMMENT) {
            return 1;
        }
        /* For other operator states, don't continue scanning */
        if (strstr(name, "DASH") != NULL || strstr(name, "SLASH") != NULL ||
            strstr(name, "ANGLE") != NULL || strstr(name, "PLUS") != NULL || strstr(name, "STAR") != NULL ||
            (strstr(name, "OPERATOR") != NULL && strcmp(name, "OPERATOR2_PIPE") != 0 &&
             strcmp(name, "OPERATOR2_AMP") != 0 && strcmp(name, "OPERATOR2_NOT") != 0 &&
             strcmp(name, "OPERATOR2_EQ") != 0 && strcmp(name, "OPERATOR2_LT") != 0 &&
             strcmp(name, "OPERATOR2_GT") != 0 && strcmp(name, "OPERATOR2_COLON") != 0)) {
            return 0;
        }
        /* 对于字符串状态（如STRING_DQ_QUOTE），不继续扫描
         * 字符串应该在被转义字符或非转义引号处结束
         * 修复：如果下一个状态是STRING_*_QUOTE状态，也不应继续扫描
         * 因为这意味着我们遇到了结束引号，应该发出当前token
         * 修复：对于任何STRING状态，如果遇到引号字符，不继续扫描
         * 特殊情况：对于STRING_SQ_QUOTE状态，如果下一个字符是'，则继续扫描（支持''转义语法）
         * 特殊情况：对于STRING_DQ_QUOTE状态，如果下一个字符是"，则继续扫描（支持""转义语法） */
        if (strstr(name, "STRING") != NULL) {
            /* 对于STRING_*_QUOTE状态，检查是否可以继续扫描以支持转义语法 */
            if (strstr(name, "QUOTE") != NULL) {
                /* 检查下一个字符是否是相同的引号字符 */
                if ((strstr(name, "STRING_SQ_QUOTE") != NULL && next_code == '\'') ||
                    (strstr(name, "STRING_DQ_QUOTE") != NULL && next_code == '"')) {
                    /* 下一个字符是相同的引号，检查转移是否有效 */
                    const kadd_trans_entry_t *next_ent = &e->transitions[state][next_code];
                    if (!next_ent->is_insn && next_ent->next_state != KADD_DFA_ID_ERROR &&
                        next_ent->next_state < e->num_states) {
                        return 1;  /* 可以继续扫描，支持''或""转义语法 */
                    }
                }
                return 0;  /* 其他情况不继续扫描 */
            }
            if (next_code == '\'' || next_code == '"') {
                return 0;  // 遇到引号，发出当前token
            }
        }
        /* 检查下一个状态是否为STRING_*_QUOTE状态 */
        if (ent->next_state >= 0 && ent->next_state < (int)e->num_states) {
            const char *next_state_name = e->state_names[ent->next_state];
            if (strstr(next_state_name, "STRING") != NULL && strstr(next_state_name, "QUOTE") != NULL) {
                return 0;
            }
        }
    }
    if (next_code > 127) {
        state_type_t type = get_state_type(e, state);
        return type == STATE_TYPE_WORD || type == STATE_TYPE_BWORD || type == STATE_TYPE_COMMENT;
    }
    return 1;
}
static inline char parse_special_instruction_type(const kadd_dfa_engine_t *e __attribute__((unused)), const kadd_trans_entry_t *ent) {
    if (!ent->is_insn || !ent->insn) return 0;
    const char *insn = ent->insn;
    if (insn[0] == 'c' && strcmp(insn, "copy ENTRY") == 0) {
        return 0;
    }
    if (insn[0] == 'd' && strncmp(insn, "dup ", 4) == 0) {
        const char *prepend_str = strstr(insn, " prepend ");
        if (prepend_str) {
            const char *type_start = prepend_str + 9;  /* 跳过 " prepend " */
            char raw_type[64];
            const char *space = strchr(type_start, ' ');
            size_t insn_len;
            if (space) {
                insn_len = space - type_start;
            } else {
                /* 没有尾随空格，计算到字符串末尾的长度 */
                insn_len = strlen(type_start);
            }
            if (insn_len < sizeof(raw_type)) {
                memcpy(raw_type, type_start, insn_len);
                raw_type[insn_len] = '\0';
                return map_token_type(raw_type);
            }
        }
    }
    return 0;
}
static int parse_token(const kadd_dfa_engine_t *e, const char *s, size_t len,
                       size_t start_pos, kadd_token_t *out, size_t *new_pos, int *state_ptr) {
    int state = state_ptr ? *state_ptr : KADD_DFA_ID_ENTRY;
    char buf[MAX_BUF_SIZE] = {0};
    size_t buf_len = 0;
    char type = 0;
    const char *state_name = "ENTRY";
    size_t pos = start_pos;
    int qstring_start_pos = -1;
    int should_finish = 0;  // 替代goto语句

    #define STATE_IS_VALID(s) ((s) >= 0 && (s) < (int)e->num_states)
    #define ADD_CHAR_TO_BUF(c) do { if (buf_len < MAX_BUF_SIZE - 1) buf[buf_len++] = (c); } while(0)
    #define TOKEN_DONE() do { should_finish = 1; break; } while(0)
    while (pos < len && !should_finish) {
        unsigned char ch = s[pos];
        int code = (int)ch;

        // Special handling: Quote characters should end the current WORD/BAREWORD token
        // and start a new STRING token
        // Check this BEFORE the character is added to the buffer
        // Special case: For B', X', E', N', U&' prefix strings, don't emit the WORD token
        if ((ch == '\'' || ch == '"') && buf_len > 0 && STATE_IS_VALID(state)) {
            state_type_t current_type = get_state_type(e, state);
            if (current_type == STATE_TYPE_WORD || current_type == STATE_TYPE_BWORD ||
                current_type == STATE_TYPE_WORD_KEYWORD) {
                /* 检查是否是特殊前缀字符串的转换 */
                const kadd_trans_entry_t *trans = &e->transitions[state][code];
                if (!trans->is_insn && trans->next_state < e->num_states) {
                    const char *next_state_name = e->state_names[trans->next_state];
                    if (strcmp(next_state_name, "BSTRING") == 0 ||
                        strcmp(next_state_name, "XSTRING") == 0 ||
                        strcmp(next_state_name, "ESTRING") == 0 ||
                        strcmp(next_state_name, "NSTRING") == 0 ||
                        strcmp(next_state_name, "USTRING") == 0) {
                        /* 不发出token，继续扫描以包含前缀字符 */
                        /* 不做任何处理，让后续逻辑正常处理引号字符 */
                    } else {
                        // Emit the current WORD/BAREWORD token WITHOUT including the quote
                        // The quote will be processed as the start of a new STRING token
                        if (!type) type = 'n';  // Default to BAREWORD for WORD states
                        // Don't add the quote to the buffer, just finish the token
                        // Don't increment pos, so the quote will be processed again
                        TOKEN_DONE();
                    }
                } else {
                    // Emit the current WORD/BAREWORD token WITHOUT including the quote
                    // The quote will be processed as the start of a new STRING token
                    if (!type) type = 'n';  // Default to BAREWORD for WORD states
                    // Don't add the quote to the buffer, just finish the token
                    // Don't increment pos, so the quote will be processed again
                    TOKEN_DONE();
                }
            }
        }

        if (unlikely(is_whitespace(ch))) {
            if (buf_len > 0) {
                if (likely(STATE_IS_VALID(state))) {
                    state_type_t current_type = get_state_type(e, state);
                    kadd_state_meta_t *meta = &e->meta[state];
                    /* 对于COMMENT状态，不要在空白字符处立即发出token
                     * 注释应该继续消费字符直到转移到ENTRY状态
                     * 这对应Python版本的行为：注释在next_state == ENTRY时发出 */
                    if (!meta->token_type && meta->action == 0 &&
                        current_type != STATE_TYPE_STRING &&
                        current_type != STATE_TYPE_QSTRING &&
                        current_type != STATE_TYPE_NQSTRING &&
                        current_type != STATE_TYPE_COMMENT) {
                        kadd_trans_entry_t *temp_ent = &e->transitions[state][ch];
                        if (!temp_ent->is_insn && temp_ent->next_state != KADD_DFA_ID_ERROR &&
                            temp_ent->next_state != KADD_DFA_ID_ENTRY &&
                            temp_ent->next_state < e->num_states) {
                        } else {
                            if (meta->action == 3 && meta->action_arg) {
                                type = meta->action_arg[0];
                            } else if (meta->token_type) {
                                type = meta->token_type[0];
                            }
                            TOKEN_DONE();
                        }
                    } else if (meta->action == 3 && meta->action_arg) {
                        type = meta->action_arg[0];
                        TOKEN_DONE();
                    } else if (meta->token_type) {
                        /* 对于COMMENT状态，不要在空白字符处发出token
                         * 注释应该继续消费字符直到转移到ENTRY状态 */
                        if (current_type != STATE_TYPE_COMMENT) {
                            type = meta->token_type[0];
                            TOKEN_DONE();
                        }
                        /* 对于COMMENT状态，不发出token，继续处理 */
                    } else {
                        /* 对于STRING/QSTRING/NQSTRING状态，遇到空白时不立即发出token
                         * 这对应Python版本的行为：字符串状态会继续消费所有字符
                         * 直到遇到明确的结束条件或到达输入末尾
                         * 对于ACTION_COPY状态（如DASH_DASH），也不要发出token，让DFA转移处理 */
                        if (current_type != STATE_TYPE_STRING && current_type != STATE_TYPE_QSTRING &&
                            current_type != STATE_TYPE_NQSTRING && meta->action != 2) {
                            TOKEN_DONE();
                        }
                    }
                } else {
                    TOKEN_DONE();
                }
            } else {
                pos++;
                continue;
            }
        }
        if (STATE_IS_VALID(state)) {
            state_name = e->state_names[state];
        }
        if (unlikely(ch > 127)) {
            if (unlikely(is_comment_state(e, state) || is_word_state(e, state))) {
                ADD_CHAR_TO_BUF(s[pos]);
                pos++;
                continue;
            }
            if (!type) type = 'n';
            if (pos + 1 < len && is_delimiter_char(s[pos + 1])) {
                TOKEN_DONE();
            }
            ADD_CHAR_TO_BUF(s[pos]);
            pos++;
            continue;
        }
        kadd_trans_entry_t *ent = &e->transitions[state][ch];

        /* 修复：科学计数法的E/e检查（在获取转移之后、使用转移之前）
         * 当当前状态是NUMBER且当前字符是E或e时，需要检查这是否真的是科学计数法
         * 如果E后面没有跟数字（或+/-），则应该停止扫描当前数字token
         * 这处理URL百分号编码的情况，如%3E应该解析为3和E，而不是3E */
        if (likely(!ent->is_insn) && state >= 0 && state < (int)e->num_states) {
            state_type_t current_type = get_state_type(e, state);
            if (current_type == STATE_TYPE_NUMBER && buf_len > 0) {
                if (ch == 'E' || ch == 'e') {
                    int should_stop = 0;
                    if (pos + 1 >= len) {
                        should_stop = 1;
                    } else {
                        unsigned char next_char = s[pos + 1];
                        if (next_char != '+' && next_char != '-' &&
                            !(next_char >= '0' && next_char <= '9')) {
                            should_stop = 1;
                        }
                    }
                    if (should_stop) {
                        type = '1';  /* NUMBER */
                        TOKEN_DONE();
                    }
                }
            }
        }
        if (unlikely(!ent->is_insn && ent->next_state == KADD_DFA_ID_ERROR)) {
            /* 修复：当遇到错误转移时，如果有缓冲区内容，先发出当前token
             * 然后继续处理当前字符（而不是break）
             * 这对应Python版本中处理无法转移字符的逻辑 */
            if (buf_len > 0) {
                if (!type) {
                    if (likely(state >= 0 && state < (int)e->num_states)) {
                        if (e->meta[state].token_type) {
                            type = e->meta[state].token_type[0];
                        } else {
                            type = 'n';
                        }
                    } else {
                        type = 'n';
                    }
                }
                TOKEN_DONE();
            } else {
                pos++;
                continue;
            }
        }


        /* 特殊处理：从 DASH 转移到 DASH_DASH 时，发出第一个 '-' 的 OPERATOR token
         * 但如果下一个字符是空白字符，则不发出（因为 '-- ' 是注释）
         * 对应Python版本中的逻辑（line 341-360 in dfa_engine_copy.py）*/
        if (likely(!ent->is_insn) && state >= 0 && state < (int)e->num_states &&
            e->state_hashes[state] == 10 && /* DASH */
            ent->next_state >= 0 && ent->next_state < (int)e->num_states &&
            e->state_hashes[ent->next_state] == 186 /* DASH_DASH */) {
            /* 检查下一个字符是否是空白字符 */
            if (pos + 1 >= len) {
                /* '--' 在输入末尾：发出 OPERATOR token */
                if (!type) type = 'o';
                TOKEN_DONE();
            } else {
                unsigned char next_char = s[pos + 1];
                unsigned char next_code = (unsigned char)next_char;
                /* 空白字符：0-32, 127, 160 */
                if ((next_code >= 0 && next_code <= 32) || next_code == 127 || next_code == 160) {
                    /* '--' 后跟空白字符是注释，不立即发出 operator，继续处理 */
                    /* pass */
                } else {
                    /* '--' 后跟非空白字符：发出第一个 '-' 的 OPERATOR token */
                    if (!type) type = 'o';
                    TOKEN_DONE();
                }
            }
        }

        if (unlikely(ent->is_insn)) {
            /* 对于 WORD_* 状态，检查当前字符是否是标识符字符
             * 如果是，libinjection 会继续扫描而不是立即应用关键字类型
             * 对应Python版本中的逻辑（line 256-275 in dfa_engine_copy.py）*/
            state_type_t current_type = get_state_type(e, state);

            /* 修复：对于操作符状态的dup指令，检查是否可以继续扫描形成多字符操作符
             * 例如：||、&&、==、!=等
             * 对应Python版本line 256-275的逻辑 */
            if (pos + 1 < len) {
                /* 检查当前状态是否是操作符状态 */
                if (state >= 0 && state < (int)e->num_states) {
                    const char *state_name = e->state_names[state];
                    /* 只对特定的操作符状态检查是否可以继续扫描
                     * 这些状态是形成多字符操作符的中间状态 */
                    int is_multi_char_op_state = (
                        strcmp(state_name, "OPERATOR2_AMP") == 0 ||    /* & -> && */
                        strcmp(state_name, "OPERATOR2_PIPE") == 0 ||    /* | -> || */
                        strcmp(state_name, "OPERATOR2_EQ") == 0 ||      /* = -> == */
                        strcmp(state_name, "OPERATOR2_NOT") == 0 ||     /* ! -> !=, !! */
                        strcmp(state_name, "OPERATOR2_LT") == 0 ||      /* < -> <=, << */
                        strcmp(state_name, "OPERATOR2_GT") == 0 ||      /* > -> >=, >> */
                        strcmp(state_name, "OPERATOR2_COLON") == 0 ||   /* : -> :: */
                        strcmp(state_name, "PLUS") == 0 ||              /* + -> ++ */
                        strcmp(state_name, "DASH") == 0                 /* - -> --, -> */
                    );

                    if (is_multi_char_op_state) {
                        /* 操作符状态，检查下一个字符是否允许继续扫描 */
                        unsigned char next_code = s[pos + 1];
                        const kadd_trans_entry_t *next_ent = &e->transitions[state][next_code];
                        if (!next_ent->is_insn && next_ent->next_state != KADD_DFA_ID_ERROR &&
                            next_ent->next_state < e->num_states) {
                            /* 可以继续扫描，跳过指令处理，继续扫描 */
                            state = next_ent->next_state;
                            ADD_CHAR_TO_BUF(s[pos]);
                            pos++;
                            continue;
                        }
                    }
                }
            }

            if (current_type == STATE_TYPE_WORD || current_type == STATE_TYPE_BWORD ||
                current_type == STATE_TYPE_WORD_KEYWORD) {
                /* 检查当前字符是否是标识符字符（不包含点号）
                 * 注意：这里不包含点号，因为点号应该结束当前token */
                if (is_identifier_char(code)) {
                    /* 跳过字符串指令，继续扫描
                     * 模拟转移到 WORD 状态的行为 */
                    state = e->id_word;
                    ADD_CHAR_TO_BUF(s[pos]);
                    pos++;
                    continue;
                }
                /* 特殊处理：如果当前字符是引号且下一个状态是特殊前缀字符串状态
                 * 则跳过指令继续扫描以包含前缀字符到字符串token中 */
                if ((code == '\'' || code == '"') && pos + 1 < len) {
                    /* 检查下一个字符是否会导致转换到特殊前缀字符串状态 */
                    /* 注意：这里需要模拟DFA的行为，看看如果继续扫描会进入什么状态 */
                    /* 简化处理：如果当前buffer只包含一个字符（B, X, E, N等），
                     * 且当前字符是引号，则可能是特殊前缀字符串 */
                    if (buf_len == 1) {
                        char prefix = buf[0];
                        if ((prefix == 'B' || prefix == 'b' || prefix == 'X' || prefix == 'x' ||
                             prefix == 'E' || prefix == 'e' || prefix == 'N' || prefix == 'n') &&
                            code == '\'') {
                            /* 可能是特殊前缀字符串，跳过指令继续扫描 */
                            state = e->id_word;
                            ADD_CHAR_TO_BUF(s[pos]);
                            pos++;
                            continue;
                        }
                        if ((prefix == 'U' || prefix == 'u') && pos + 1 < len && s[pos + 1] == '&' && code == '&') {
                            /* 可能是 U& 前缀字符串 */
                            state = e->id_word;
                            ADD_CHAR_TO_BUF(s[pos]);
                            pos++;
                            continue;
                        }
                    }
                }
            }
            char insn_type = parse_special_instruction_type(e, ent);
            int is_copy_entry = (ent->insn[0] == 'c' && strcmp(ent->insn, "copy ENTRY") == 0);
            if (insn_type != 0 || is_copy_entry) {
                /* 对于 dup ENTRY prepend TYPE_XXX 指令，设置type
                 * 注意：如果当前状态已经有type（如WORD_KEYWORD），则保持不变
                 * 否则使用指令中的类型 */
                if (insn_type != 0 && type == 0) {
                    type = insn_type;
                }
                /* 对于特定状态，需要包含当前字符
                 * 关键规则：当状态对当前字符有dup指令时，需要决定是否消费当前字符
                 * - copy ENTRY 指令：不消费当前字符，让 ENTRY 重新处理
                 * - dup ENTRY prepend 指令：
                 *   - 操作符状态（STATE_TYPE_OPERATOR）：总是消费
                 *   - WORD*状态（STATE_TYPE_WORD/STATE_TYPE_BWORD/STATE_TYPE_WORD_KEYWORD）：
                 *     如果当前字符是操作符（非标识符字符），则不消费
                 *     如果当前字符是标识符字符，则跳过指令继续扫描（已在前面处理）
                 *   - 字符串结束状态（如STRING_DQ_QUOTE, STRING_SQ_QUOTE）：不消费当前字符
                 */
                int consume_current_char = 0;
                if (is_copy_entry) {
                    consume_current_char = 0;
                } else if (current_type == STATE_TYPE_OPERATOR ||
                    current_type == STATE_TYPE_WORD_KEYWORD) {
                    /* 修复：对于操作符状态，需要检查是否可以继续扫描以形成多字符操作符
                     * 例如：||、&&、==、!=、<=、>=等
                     * 对应Python版本中的can_continue_scanning逻辑 */
                    if (pos < len && !can_continue_scanning(e, state, s[pos], 1)) {
                        consume_current_char = 1;
                    } else {
                        /* 可以继续扫描，不立即发出token */
                        consume_current_char = 0;
                    }
                } else if (current_type == STATE_TYPE_WORD ||
                           current_type == STATE_TYPE_BWORD) {
                    /* WORD和WORD_B状态：只有当前字符不是标识符字符时才消费
                     * 这是因为：
                     * - 如果是标识符字符（如字母），应该继续扫描组成word
                     * - 如果是操作符（如=、>等），应该消费并结束token
                     *
                     * 特殊处理：如果下一个状态是特殊前缀字符串状态（BSTRING, XSTRING等），
                     * 则不消费当前字符，而是继续扫描以包含前缀字符到字符串token中 */
                    if (!is_identifier_char(code) && code != '.') {
                        /* 检查是否是特殊前缀字符串的转换 */
                        const kadd_trans_entry_t *trans = &e->transitions[state][code];
                        if (!trans->is_insn && trans->next_state < e->num_states) {
                            const char *next_state_name = e->state_names[trans->next_state];
                            if (strcmp(next_state_name, "BSTRING") == 0 ||
                                strcmp(next_state_name, "XSTRING") == 0 ||
                                strcmp(next_state_name, "ESTRING") == 0 ||
                                strcmp(next_state_name, "NSTRING") == 0 ||
                                strcmp(next_state_name, "USTRING") == 0) {
                                /* 不发出token，继续扫描以包含前缀字符 */
                                consume_current_char = 0;
                            } else {
                                consume_current_char = 1;
                            }
                        } else {
                            consume_current_char = 1;
                        }
                    }
                } else if (current_type == STATE_TYPE_QSTRING) {
                    /* 字符串状态（STRING_DQ_QUOTE, STRING_SQ_QUOTE等）：
                     * 不消费当前字符（字符串已在之前的状态中包含了开始引号）
                     * 当前字符是字符串结束后的字符，需要被重新处理 */
                    consume_current_char = 0;
                }
                if (current_type == STATE_TYPE_BWORD && code == ']') {
                    /* 对于BWORD状态，需要包含]到token中
                     * 添加到缓冲区并发出token，不执行后续的consume_current_char逻辑 */
                    ADD_CHAR_TO_BUF(ch);
                    pos++;
                    type = 'n';  /* BAREWORD */
                    /* 直接break，不执行后续逻辑 */
                    TOKEN_DONE();
                }
                if (current_type == STATE_TYPE_QSTRING && code == '\'') {
                    const char *state_name = e->state_names[state];
                    if (strncmp(state_name, "QSTRING_R", 10) == 0) {
                        consume_current_char = 1;
                    }
                }
                if (consume_current_char) {
                    /* 当前字符已经在主循环中被添加到缓冲区了（第1421行）
                     * 这里只需要增加pos即可，不需要再次添加字符 */
                    pos++;
                } else {
                    /* 不消费当前字符时的处理
                     * Python版本：pos -= 1，回退到添加字符之前
                     * 但C版本的字符已经添加了，所以不需要再添加
                     * 直接设置标志并break即可 */
                }
                TOKEN_DONE();
            }
            break;
        }
        if (likely(!ent->is_insn && ent->next_state < e->num_states)) {
            /* Python兼容性修复：当转移到WORD状态时，检查当前字符是否是word字符
             * 如果不是word字符，立即发出BAREWORD token（对应Python版本line 301-311） */
            if (unlikely(ent->next_state == e->id_word && buf_len > 0)) {
                int is_word_char_code = (
                    (code >= 65 && code <= 90) ||   /* A-Z */
                    (code >= 97 && code <= 122) ||  /* a-z */
                    (code >= 48 && code <= 57) ||   /* 0-9 */
                    (code == 95) ||                  /* _ */
                    (code == 46) ||                  /* . */
                    (code == 96)                     /* ` */
                );
                if (!is_word_char_code) {
                    type = 'n';
                    TOKEN_DONE();
                }
            }

            /* 修复：对于BWORD状态，遇到]时应包含在当前token中
             * 对应Python版本line 286-293的处理 */
            state_type_t current_type = get_state_type(e, state);
            if (current_type == STATE_TYPE_BWORD && ch == ']' && buf_len > 0) {
                /* 添加]到缓冲区并发出token，不转移到下一状态 */
                ADD_CHAR_TO_BUF(ch);
                pos++;
                type = 'n';  /* BAREWORD */
                TOKEN_DONE();
            }
            if (unlikely(ent->next_state == KADD_DFA_ID_ENTRY && is_comment_state(e, state) && buf_len > 0)) {
                type = 'c';
                TOKEN_DONE();
            }
        }
        /* 修复v6：对于DASH_DASH状态，转移到DASH_DASH_NOTWHITE时不应提前发出token
         * DASH_DASH_NOTWHITE是COMMENT状态的一部分，应该继续扫描
         * 删除之前的逻辑，该逻辑会错误地将--作为OPERATOR发出 */
        /* 修复：对于BWORD状态，不应在非标识符字符处发出token
         * BWORD状态接受所有字符除了']'，应该继续扫描而不是提前发出token
         * 根据profile_sqli_fixed.txt，BWORD状态的default转移是BWORD（自身） */
        if (likely(STATE_IS_VALID(state)) && buf_len > 0 && !ent->is_insn) {
            state_type_t prev_type = get_state_type(e, state);
            if (prev_type == STATE_TYPE_WORD || prev_type == STATE_TYPE_WORD_KEYWORD) {
                if (!is_identifier_char(code) && code != '.' && !is_whitespace(ch)) {
                    kadd_state_meta_t *meta = &e->meta[state];
                    if (!meta->token_type) {
                        type = 'n';
                        TOKEN_DONE();
                    }
                }
            }
        }
        /* 修复：在状态转移之前，检查目标状态是否是操作符状态
         * 如果目标状态是OPERATOR*状态，并且当前缓冲区不为空，先发出当前token
         * 这样可以确保操作符被单独识别为一个token */
        if (likely(!ent->is_insn && ent->next_state < e->num_states && buf_len > 0)) {
            const char *next_state_name = e->state_names[ent->next_state];
            if (strstr(next_state_name, "OPERATOR") != NULL) {
                if (state >= 0 && state < (int)e->num_states) {
                    kadd_state_meta_t *current_meta = &e->meta[state];
                    if (!current_meta->token_type && current_meta->action != 3) {
                        type = 'n';
                        TOKEN_DONE();
                    }
                }
            }
        }
        int old_state_for_debug = state;  // Remember old state before transition
        state = ent->next_state;

        /* 特殊处理：如果刚转换到特殊前缀字符串BODY状态（BSTRING_BODY, XSTRING_BODY等），
         * 需要立即发出token，包括前缀字符 */
        if (state >= 0 && state < (int)e->num_states) {
            const char *new_state_name = e->state_names[state];
            /* 检查是否是特殊前缀字符串的BODY状态 */
            if (strcmp(new_state_name, "BSTRING_BODY") == 0 ||
                strcmp(new_state_name, "XSTRING_BODY") == 0 ||
                strcmp(new_state_name, "ESTRING_BODY") == 0 ||
                strcmp(new_state_name, "NSTRING_BODY") == 0 ||
                strcmp(new_state_name, "USTRING_BODY") == 0) {
                /* 对于B'和X'前缀，整个token应该是NUMBER类型（二进制/十六进制字面量）
                 * 对于E', N', U&'前缀，整个token应该是STRING类型 */
                if (strcmp(new_state_name, "BSTRING_BODY") == 0 ||
                    strcmp(new_state_name, "XSTRING_BODY") == 0) {
                    type = '8';  /* NUMBER */
                } else {
                    type = 'a';  /* STRING */
                }
                TOKEN_DONE();
                /* TOKEN_DONE()会重置缓冲区，所以不需要手动清空 */
            }
        }

        int old_state = state;  // Keep track of old state for debug
    consume_char: {
        ADD_CHAR_TO_BUF(s[pos]);
        pos++;
        if ((strstr(state_name, "QSTRING") != NULL || strstr(state_name, "NQSTRING") != NULL) &&
            qstring_start_pos < 0) {
            qstring_start_pos = (int)(pos - 1);
        }
    }
        if (likely(STATE_IS_VALID(state))) {
            state_name = e->state_names[state];
            kadd_state_meta_t *meta = &e->meta[state];
            state_type_t current_type = get_state_type(e, state);
            if (meta->action == 3) { /* ACTION_DUP_PREPEND */
                int can_continue = can_continue_scanning(e, state, pos < len ? s[pos] : 0, pos < len);
                if (can_continue) {
                    continue;
                }
                /* 对于WORD_*状态，额外检查下一个字符是否是标识符字符
                 * 这对应Python版本中的特殊处理：当遇到dup指令时，
                 * 如果是WORD状态且下一个字符是标识符字符，应该继续扫描
                 * 而不是立即执行dup指令
                 *
                 * 特殊处理：如果下一个状态是特殊前缀字符串状态（BSTRING, XSTRING等），
                 * 则继续扫描以包含前缀字符到字符串token中 */
                if (pos < len && (current_type == STATE_TYPE_WORD || current_type == STATE_TYPE_BWORD ||
                    current_type == STATE_TYPE_WORD_KEYWORD)) {
                    unsigned char next_code = s[pos];
                    if (is_identifier_char(next_code) || next_code == '.') {
                        continue;
                    }
                    /* 检查是否是特殊前缀字符串的转换 */
                    const kadd_trans_entry_t *trans = &e->transitions[state][next_code];
                    if (!trans->is_insn && trans->next_state < e->num_states) {
                        const char *next_state_name = e->state_names[trans->next_state];
                        if (strcmp(next_state_name, "BSTRING") == 0 ||
                            strcmp(next_state_name, "XSTRING") == 0 ||
                            strcmp(next_state_name, "ESTRING") == 0 ||
                            strcmp(next_state_name, "NSTRING") == 0 ||
                            strcmp(next_state_name, "USTRING") == 0) {
                            /* 不发出token，继续扫描以包含前缀字符 */
                            continue;
                        }
                    }
                }

                /* 对于多字符操作符状态，检查下一个字符是否可以形成更长的操作符
                 * 如果可以，则继续扫描而不是立即发出token */
                if (pos < len && current_type == STATE_TYPE_OPERATOR) {
                    unsigned char next_code = s[pos];
                    /* 检查特定的操作符组合 */
                    int can_form_multi_char = 0;
                    if (strcmp(state_name, "OPERATOR2_AMP") == 0 && next_code == '&') can_form_multi_char = 1;
                    else if (strcmp(state_name, "OPERATOR2_PIPE") == 0 && next_code == '|') can_form_multi_char = 1;
                    else if (strcmp(state_name, "OPERATOR2_EQ") == 0 && next_code == '=') can_form_multi_char = 1;
                    else if (strcmp(state_name, "OPERATOR2_NOT") == 0 && (next_code == '=' || next_code == '!')) can_form_multi_char = 1;
                    else if (strcmp(state_name, "OPERATOR2_LT") == 0 && (next_code == '=' || next_code == '<')) can_form_multi_char = 1;
                    else if (strcmp(state_name, "OPERATOR2_GT") == 0 && (next_code == '=' || next_code == '>')) can_form_multi_char = 1;
                    else if (strcmp(state_name, "OPERATOR2_COLON") == 0 && next_code == ':') can_form_multi_char = 1;
                    else if (strcmp(state_name, "PLUS") == 0 && next_code == '+') can_form_multi_char = 1;
                    else if (strcmp(state_name, "DASH") == 0 && next_code == '-') can_form_multi_char = 1;
                    else if (strcmp(state_name, "STAR") == 0 && next_code == '*') can_form_multi_char = 1;

                    if (can_form_multi_char) {
                        /* 继续扫描，不立即发出token */
                        continue;
                    }
                }
                if (meta->action_arg && meta->action_arg[0]) {
                    type = meta->action_arg[0];
                }
                /* 对于操作符状态和 WORD_KEYWORD 状态，回退pos让当前字符被重新处理
                 * 这对应Python版本中的行为：当遇到dup指令时，
                 * 不消费当前字符，让ENTRY重新处理
                 * 但对于TICK_BWORD_TICK状态（反引号字符串），需要消费结束的反引号 */
                const char *current_state_name = e->state_names[state];
                int is_tick_bword_tick = (strstr(current_state_name, "TICK_BWORD_TICK") != NULL);
                /* 对于OPERATOR2_PIPE_PIPE等完整的多字符操作符状态，不回退pos
                 * 这样可以正确消耗所有字符并发出单个token */
                int is_multi_char_operator = (strstr(current_state_name, "OPERATOR2_PIPE_PIPE") != NULL ||
                                                strstr(current_state_name, "OPERATOR2_AMP_AMP") != NULL ||
                                                strstr(current_state_name, "OPERATOR2_NOT_EQ") != NULL ||
                                                strstr(current_state_name, "OPERATOR2_NOT_NOT") != NULL ||
                                                strstr(current_state_name, "OPERATOR2_LT_LT") != NULL ||
                                                strstr(current_state_name, "OPERATOR2_LT_GT") != NULL ||
                                                strstr(current_state_name, "OPERATOR2_GT_GT") != NULL ||
                                                strstr(current_state_name, "OPERATOR2_LT_EQ") != NULL ||
                                                strstr(current_state_name, "OPERATOR2_GT_EQ") != NULL ||
                                                strstr(current_state_name, "OPERATOR2_COLON_COLON") != NULL);
                if (!is_tick_bword_tick && !is_multi_char_operator &&
                    (current_type == STATE_TYPE_OPERATOR || current_type == STATE_TYPE_WORD_KEYWORD)) {
                    /* For single-character operators, decrement pos and emit token
                     * For multi-character operators, let the loop exit naturally
                     * and the token will be emitted after the while loop */
                    if (pos > start_pos + 1) {
                        pos--;
                        TOKEN_DONE();
                    }
                }
                /* For multi-character operators, the type is already set above
                 * and the token will be emitted after the while loop exits naturally */
                break;
            }
            if (meta->action == 2) { /* ACTION_COPY - copy to action_arg state */
                /* For states with action=2, check the next character to determine the behavior.
                 * For DASH_DASH state:
                 * - If next char is '&': emit two OPERATOR tokens for the dashes
                 * - Otherwise: rely on the transition table (which will handle cases like '-- ' -> DASH_COMMENT)
                 * For other states with action=2 (like CHAR_LEFT_PAREN, BSTRING_BODY, XSTRING_BODY, etc.):
                 * - Emit token immediately if token_type is set
                 * - Otherwise, rely on the transition table
                 */
                if (buf_len > 0 && meta->action_arg && meta->action_arg[0]) {
                    const char *current_state_name = e->state_names[state];
                    /* Special handling for DASH_DASH state when next char is '&' */
                    if (strcmp(current_state_name, "DASH_DASH") == 0 && pos < len && s[pos] == '&') {
                        /* For "--&" pattern, emit two OPERATOR tokens for the dashes */
                        type = 'o';
                        /* Decrement pos to emit token for the last '-' only */
                        pos--;
                        TOKEN_DONE();
                    } else if (meta->token_type && meta->token_type[0]) {
                        /* For states like CHAR_LEFT_PAREN, BSTRING_BODY, XSTRING_BODY, etc. with token_type,
                         * emit the token immediately */
                        if (buf_len > 0) {
                            break;
                        }
                    }
                    /* For other cases, rely on the transition table to determine the next state */
                }
            }
            if (meta->token_type) {
                type = meta->token_type[0];
                if (meta->action == 0) { /* ACTION_NONE */
                    /* 修复：对于FUNCTION/KEYWORD类型，需要检查下一个字符是否可以继续扫描
                     * 如果下一个字符是标识符字符且有有效的转移，则继续扫描而不是立即发出token
                     * 这对应Python版本lines 420-422的逻辑：检查next_target是否有效
                     * 例如："iFc"应该作为BAREWORD而不是FUNCTION+后续字符
                     * "iF "应该作为FUNCTION因为后面是空格 */
                    if (can_continue_scanning(e, state, pos < len ? s[pos] : 0, pos < len)) {
                        /* 可以继续扫描，但不发出token */
                        continue;
                    }
                    /* 无法继续扫描，发出token */
                    break;
                } else if (meta->action == 2) { /* ACTION_COPY - emit token immediately */
                    /* For states like CHAR_LEFT_PAREN, CHAR_RIGHT_PAREN, etc. with action 2,
                     * emit the token immediately with the specified token_type */
                    if (buf_len > 0) {
                        break;
                    }
                }
            }
        }
        /* 修复：对于BWORD状态，不应在非标识符字符处发出token
         * BWORD状态接受所有字符除了']'，应该继续扫描而不是提前发出token
         * 特殊处理：对于特殊前缀字符串（B', X', E', N', U&'），不应在这里发出token */
        if (likely(STATE_IS_VALID(state)) && pos < len && buf_len > 0) {
            /* 检查是否是特殊前缀字符串状态（BSTRING, XSTRING, ESTRING, NSTRING, USTRING）
             * 如果是，继续扫描而不发出token */
            const char *state_name_check = e->state_names[state];
            if (strcmp(state_name_check, "BSTRING") == 0 ||
                strcmp(state_name_check, "XSTRING") == 0 ||
                strcmp(state_name_check, "ESTRING") == 0 ||
                strcmp(state_name_check, "NSTRING") == 0 ||
                strcmp(state_name_check, "USTRING") == 0 ||
                strcmp(state_name_check, "USTRING_AMP") == 0) {
                /* 特殊前缀字符串状态：继续扫描而不发出token */
                continue;
            }

            state_type_t current_type = get_state_type(e, state);
            if (current_type == STATE_TYPE_WORD || current_type == STATE_TYPE_WORD_KEYWORD ||
                current_type == STATE_TYPE_BWORD) {
                unsigned char next_code = s[pos];
                if (!is_identifier_char(next_code) && next_code != '.' && !is_whitespace(next_code)) {
                    /* 检查是否是特殊前缀字符串的情况 */
                    int is_special_prefix_string = 0;
                    /* 检查两种情况：
                     * 1. buf_len == 1 且下一个字符是引号（尚未添加到缓冲区）
                     * 2. buf_len == 2 且缓冲区包含前缀字符和引号（引号已添加到缓冲区） */
                    if ((next_code == '\'' || next_code == '"') && buf_len == 1) {
                        char prefix = buf[0];
                        if ((prefix == 'B' || prefix == 'b' || prefix == 'X' || prefix == 'x' ||
                             prefix == 'E' || prefix == 'e' || prefix == 'N' || prefix == 'n') &&
                            next_code == '\'') {
                            is_special_prefix_string = 1;
                        }
                        if ((prefix == 'U' || prefix == 'u') && pos + 1 < len && s[pos + 1] == '&' && next_code == '&') {
                            is_special_prefix_string = 1;
                        }
                    } else if (buf_len == 2) {
                        char prefix = buf[0];
                        char quote = buf[1];
                        if ((prefix == 'B' || prefix == 'b' || prefix == 'X' || prefix == 'x' ||
                             prefix == 'E' || prefix == 'e' || prefix == 'N' || prefix == 'n') &&
                            (quote == '\'' || quote == '"')) {
                            is_special_prefix_string = 1;
                        }
                        if ((prefix == 'U' || prefix == 'u') && quote == '&') {
                            is_special_prefix_string = 1;
                        }
                    }
                    if (!is_special_prefix_string) {
                        kadd_state_meta_t *meta = &e->meta[state];
                        if (!meta->token_type) {
                            type = 'n';
                            TOKEN_DONE();
                        }
                    } else {
                        /* 特殊前缀字符串：跳过token emission，继续扫描
                         * 需要继续循环而不发出token，让后续逻辑处理引号字符 */
                        continue;
                    }
                }
            }
        }
    }
    if (buf_len > 0 && likely(STATE_IS_VALID(state))) {
        state_name = e->state_names[state];
        kadd_state_meta_t *meta = &e->meta[state];
        if (meta->action == 3 && meta->action_arg) {
            type = meta->action_arg[0];
            /* 对于WORD_KEYWORD状态，需要回退pos，避免重复处理最后一个字符
             * 但对于TICK_BWORD_TICK状态（反引号字符串），需要消费结束的反引号，不回退
             * 对于多字符操作符（||, &&, ==, 等），也不回退pos，因为所有字符已经被正确消费 */
            state_type_t current_type = get_state_type(e, state);
            int is_tick_bword_tick = (strstr(state_name, "TICK_BWORD_TICK") != NULL);
            int is_multi_char_operator = (strstr(state_name, "OPERATOR2_PIPE_PIPE") != NULL ||
                                            strstr(state_name, "OPERATOR2_AMP_AMP") != NULL ||
                                            strstr(state_name, "OPERATOR2_NOT_EQ") != NULL ||
                                            strstr(state_name, "OPERATOR2_NOT_NOT") != NULL ||
                                            strstr(state_name, "OPERATOR2_LT_LT") != NULL ||
                                            strstr(state_name, "OPERATOR2_LT_GT") != NULL ||
                                            strstr(state_name, "OPERATOR2_GT_GT") != NULL ||
                                            strstr(state_name, "OPERATOR2_LT_EQ") != NULL ||
                                            strstr(state_name, "OPERATOR2_GT_EQ") != NULL ||
                                            strstr(state_name, "OPERATOR2_COLON_COLON") != NULL);
            if (!is_tick_bword_tick && !is_multi_char_operator &&
                (current_type == STATE_TYPE_OPERATOR || current_type == STATE_TYPE_WORD_KEYWORD) && pos > start_pos + 1) {
                pos--;
            }
        }
    }
    if (buf_len > 0 && type && likely(STATE_IS_VALID(state))) {
        state_name = e->state_names[state];
        if (strncmp(state_name, "WORD_", 5) == 0) {
            kadd_state_meta_t *meta = &e->meta[state];
            if (!meta->token_type) {
                type = 'n';
            }
        }
    }
#undef STATE_IS_VALID
#undef ADD_CHAR_TO_BUF
#undef TOKEN_DONE

    // Token完成后的处理逻辑
    if (buf_len > 0) {
        if (pos == start_pos) {
            return 0;  /* 没有消费任何字符 */
        }
        if (!type) {
            if (likely(state >= 0 && state < (int)e->num_states)) {
                const char *name = e->state_names[state];
                if (strncmp(name, "STRING_", 7) == 0) {
                    type = 's';
                    /* 修复：对于STRING_*_QUOTE状态，即使buffer只包含引号，也应产生STRING token
                     * 这对应Python版本的行为：空字符串应该产生STRING token */
                    if (strstr(name, "QUOTE") != NULL) {
                        type = 's';  /* 确保QUOTE状态产生STRING token */
                    }
                } else if (e->meta[state].token_type) {
                    type = e->meta[state].token_type[0];
                } else {
                    type = 'n';
                    /* 特殊处理：检查是否是特殊前缀字符串状态（BSTRING, XSTRING, ESTRING, NSTRING, USTRING）
                     * 如果buffer包含前缀字符和引号，则设置正确的token类型 */
                    if (buf_len >= 2) {
                        char prefix = buf[0];
                        char quote = buf[1];
                        if ((prefix == 'B' || prefix == 'b' || prefix == 'X' || prefix == 'x') &&
                            (quote == '\'' || quote == '"')) {
                            type = '8';  /* NUMBER for B' and X' strings */
                        } else if ((prefix == 'E' || prefix == 'e' || prefix == 'N' || prefix == 'n') &&
                                   (quote == '\'' || quote == '"')) {
                            type = 'a';  /* STRING for E' and N' strings */
                        } else if ((prefix == 'U' || prefix == 'u') && buf_len >= 3 &&
                                   quote == '&' && buf[2] == '\'' || buf[2] == '"') {
                            type = 'a';  /* STRING for U&' strings */
                        }
                    }
                }
            } else {
                type = 'n';
            }
        }
        if (buf_len == 1 && buf[0] == '.') {
            type = '.';
        }
        /* 修复：不要覆盖DFA设置的STRING类型
         * 当buffer只包含引号字符时，如果type已经是's'（STRING），保持不变
         * 这对应Python版本的行为：空字符串应该产生STRING token
         * 对于STRING_*_QUOTE状态，即使buffer包含引号，也应该产生STRING token */
        if (buf_len == 1 && type != 's') {
            char c = buf[0];
            if (c == '\'') {
                type = 's';  /* 修复：单引号也应该产生STRING token */
            } else if (c == '"') {
                type = 's';  /* 修复：双引号应该产生STRING token，不是单独的 '"' 类型 */
            } else if (c == '<' || c == '>') {
                type = 'o';
            } else if (c == '(') {
                type = '(';  /* 修复：左括号应该产生LEFTPARENS token */
            } else if (c == ')') {
                type = ')';  /* 修复：右括号应该产生RIGHTPARENS token */
            } else if (c == ',') {
                type = ',';  /* 修复：逗号应该产生COMMA token */
            }
        }
        /* 对于buffer长度>1的情况，检查是否以引号结尾（STRING_QUOTE状态）*/
        if (buf_len > 0 && type != 's') {
            char last_char = buf[buf_len - 1];
            if (last_char == '\'' || last_char == '"') {
                /* 检查当前state是否为STRING_*_QUOTE状态 */
                if (likely(state >= 0 && state < (int)e->num_states)) {
                    const char *name = e->state_names[state];
                    if (strstr(name, "STRING_") != NULL && strstr(name, "QUOTE") != NULL) {
                        type = 's';  /* 修复：STRING_*_QUOTE状态应该产生STRING token */
                    }
                }
            }
        }
        if (buf_len == 0) {
            return 0;  /* 空token，不应该生成 */
        }
        if (buf_len == 2 && buf[0] == '`' && buf[1] == '`') {
            return 0;
        }
        int qstring_handled = handle_qstring_format(buf, buf_len, out, start_pos, pos);
        if (qstring_handled == 1) {
            *new_pos = pos;
            return 1;
        } else if (qstring_handled == -1) {
            return 0;  /* 内存分配失败 */
        }
        out->type = type;
        if (buf_len < KADD_DFA_INLINE_STRING_SIZE - 1) {
            out->is_inline = 1;
            memcpy(out->value.value_inline, buf, buf_len);
            out->value.value_inline[buf_len] = '\0';
        } else {
            out->is_inline = 0;
            out->value.value_ptr = malloc(buf_len + 1);
            if (!out->value.value_ptr) return 0;
            memcpy(out->value.value_ptr, buf, buf_len);
            out->value.value_ptr[buf_len] = '\0';
        }
        out->pos = start_pos;
        *new_pos = pos;
        if (buf_len == 0) {
            return 0;  /* 空token，不应该生成 */
        }
        return 1;
    }
    if (pos < len && pos == start_pos) {
        unsigned char ch = s[pos];
        out->is_inline = 1;
        out->value.value_inline[0] = ch;
        out->value.value_inline[1] = '\0';
        out->pos = pos;
        *new_pos = pos + 1;
        if (ch == '.') {
            out->type = '.';
        } else {
            out->type = type ? type : 'n';
        }
        /* Save final state for next token */
        if (state_ptr) {
            *state_ptr = state;
        }
        return 1;
    }
    return 0;
}
int kadd_dfa_engine_tokenize(const kadd_dfa_engine_t *e,
                             const char *input,
                             size_t input_length,
                             int *output,
                             int *dim) {
    if (!e || !input || !output || !dim) return -1;

    // 检查输入长度是否超过最大限制
    if (input_length > e->max_input_length) {
        return KADD_DFA_ERROR_INVALID_LENGTH;
    }

    size_t pos = 0;
    *dim = 0;

    /* Preserve DFA state between tokens for multi-character operators */
    int dfa_state = KADD_DFA_ID_ENTRY;

    while (pos < input_length && (size_t)*dim < e->max_output_dim) {
        unsigned char ch = input[pos];
        if (likely(ch <= 127)) {
            if (unlikely(is_whitespace(ch))) {
                pos++;
                continue;
            }
        }

        // 注释：不再对未闭合的引号进行特殊处理
        // Python实现会将单个引号作为STRING token，然后继续解析后续内容
        // 因此我们让parse_token正常处理，即使引号未闭合也会继续解析
        kadd_token_t tok;
        size_t new_pos = 0;
        int parse_result = parse_token(e, input, input_length, pos, &tok, &new_pos, &dfa_state);
        if (likely(parse_result)) {
            char *temp_heap_ptr = NULL;
            if (!tok.is_inline && tok.value.value_ptr) {
                temp_heap_ptr = tok.value.value_ptr;
            }

            // Convert token type to index
            int token_index = char_type_to_index(tok.type);
            output[(*dim)++] = token_index;

            if (temp_heap_ptr) free(temp_heap_ptr);
            pos = new_pos;
        } else {
            pos++;
        }
    }
    return 0;
}
