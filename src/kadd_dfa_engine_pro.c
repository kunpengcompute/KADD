/**
 * @file kadd_dfa_engine_pro.c
 * @brief DFA 执行引擎 - 基于转移表的高效分词器（Pro版本）
 *
 * 基于 Python dfa_engine.py 实现，与 libinjection token 完全兼容
 */

#include "kadd_dfa_engine_pro.h"
#include "cJSON.h"
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
 * @brief Token结构（内部使用）
 */
typedef struct {
    char type;
    size_t pos;
    int is_inline;
    union {
        char value_inline[KADD_DFA_INLINE_STRING_SIZE];
        char *value_ptr;
    } value;
} kadd_token_t;

/**
 * @brief DFA 引擎结构（内部定义）
 */
struct kadd_dfa_engine_pro_t {
    char table_path[256];                /**< 转移表文件路径 */
    char state_path[256];                /**< 状态元数据文件路径 */
    uint32_t num_states;                 /**< 状态数量 */
    char **state_names;                  /**< 状态名称数组 */
    kadd_trans_entry_t **transitions;    /**< 状态转移表 */
    kadd_state_meta_t *meta;             /**< 状态元数据 */
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

/* ========== 前向声明 ========== */
static int parse_token(const kadd_dfa_engine_pro_t *e, const char *s, size_t len,
                       size_t start_pos, kadd_token_t *out, size_t *new_pos, int *state_ptr);
static char map_token_type(const char *raw_type);

/* ========== 辅助函数 ========== */

/**
 * @brief 将字符类型映射到索引值
 * 对应 libinjection sqli_token_type 到索引的转换
 */
int kadd_dfa_engine_pro_char_type_to_index(char type) {
    switch (type) {
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

/**
 * @brief 获取token类型名称
 */
const char* kadd_dfa_engine_pro_get_token_name(char type) {
    switch (type) {
        case 'o': return "OPERATOR";
        case 'k': return "KEYWORD";
        case 'n': return "BAREWORD";
        case '1': return "NUMBER";
        case 'v': return "VARIABLE";
        case 's': return "STRING";
        case '&': return "LOGIC_OP";
        case 'U': return "UNION";
        case 'E': return "EXPRESSION";
        case 't': return "SQLTYPE";
        case 'f': return "FUNCTION";
        case '(': return "L_PAREN";
        case ')': return "R_PAREN";
        case '.': return "DOT";
        case 'c': return "COMMENT";
        case ',': return "COMMA";
        case ':': return "COLON";
        case ';': return "SEMICOLON";
        case '{': return "L_BRACE";
        case '}': return "R_BRACE";
        case '\\': return "BACKSLASH";
        case '?': return "UNKNOWN";
        default: return "OTHER";
    }
}

/**
 * @brief 将 TYPE_* 字符串映射为单字符token类型
 */
static char map_token_type(const char *raw_type) {
    if (!raw_type) return 0;
    if (strlen(raw_type) == 1) {
        return raw_type[0];
    }
    /* Map TYPE_* strings to single characters */
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

/**
 * @brief 检查字符是否为空白字符
 * 对应Python: if code <= 127 and (char.isspace() or code in [0, 9, 10, 13, 32, 160])
 * 注意：Python中code <= 127限制了160不被匹配，所以160实际上不会被包含
 *
 * isspace() 匹配: 9(\t), 10(\n), 11(\v), 12(\f), 13(\r), 32( )
 * 额外列表: 0(\0)
 * 最终匹配: 0, 9, 10, 11, 12, 13, 32
 */
static inline int is_whitespace_char(unsigned char c) {
    /* 首先必须是ASCII字符 (c <= 127) */
    if (c > 127) return 0;

    /* 匹配: 0, 9, 10, 11, 12, 13, 32 */
    return c == 0 || (c >= 9 && c <= 13) || c == 32;
}

/**
 * @brief 检查字符是否为标识符字符（A-Z, a-z, 0-9, _, $, `）
 */
static inline int is_identifier_char(unsigned char c) {
    return (c >= 65 && c <= 90) ||   /* A-Z */
           (c >= 97 && c <= 122) ||  /* a-z */
           (c >= 48 && c <= 57) ||   /* 0-9 */
           (c == 95) ||               /* _ */
           (c == 36) ||               /* $ */
           (c == 96);                 /* ` (backtick) */
}

/**
 * @brief 检查字符是否为word字符（包含点号）
 */
static inline int is_word_char(unsigned char c) {
    return is_identifier_char(c) || (c == 46);  /* 包含点号 */
}

/**
 * @brief 检查字符是否为分隔符
 */
static inline int is_delimiter_char(unsigned char c) {
    return c == '(' || c == ')' || c == '[' || c == ']' ||
           c == '{' || c == '}' || c == ',' || c == ';' ||
           c == ':' || c == '+' || c == '*' || c == '^' ||
           c == '~' || c == '%' || c == '&' || c == '|' ||
           c == '<' || c == '>' || c == '=' || c == '!' || c == '?';
}

/**
 * @brief 获取token值
 */
static inline const char *token_get_value(const kadd_token_t *tok) {
    if (!tok) return NULL;
    return tok->is_inline ? tok->value.value_inline : tok->value.value_ptr;
}

/**
 * @brief 解析特殊指令类型
 */
static inline char parse_special_instruction_type(const kadd_trans_entry_t *ent) {
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

/**
 * @brief 检查是否可以继续扫描
 * 对应Python版本的can_continue_scanning逻辑
 *
 * Python版本的逻辑（在ACTION_NONE中）：
 * 1. 对于WORD_*状态，如果下一个字符是标识符字符，继续扫描
 * 2. 检查next_target，如果不是ERROR且不是指令（字符串），继续扫描
 *
 * 注意：对于有token_type的关键字状态（WORD_*），如果下一个字符不是标识符字符，
 * 应该停止扫描并发出关键字token，而不是继续扫描。
 */
static inline int can_continue_scanning(const kadd_dfa_engine_pro_t *e, int state,
                                       unsigned char next_code, int pos_in_range) {
    if (!pos_in_range) return 0;

    if (state < 0 || state >= (int)e->num_states) return 0;

    const char *state_name = e->state_names[state];

    /* 对于WORD_*状态，检查下一个字符是否是标识符字符 */
    if (strncmp(state_name, "WORD_", 5) == 0 || strcmp(state_name, "WORD") == 0 ||
        strcmp(state_name, "BWORD") == 0) {
        if (is_identifier_char(next_code)) {
            return 1;
        }
        /* 对于WORD_*状态，如果下一个字符不是标识符字符，停止扫描 */
        /* 这样关键字状态（如WORD_SELECT）在遇到非标识符字符时会正确结束 */
        return 0;
    }

    /* 对于注释状态，继续扫描 */
    if (strstr(state_name, "COMMENT") != NULL || strcmp(state_name, "SLASH_ASTERISK") == 0 ||
        strcmp(state_name, "HASH_COMMENT") == 0 || strcmp(state_name, "DASH_COMMENT") == 0 ||
        strcmp(state_name, "COMMENT_COMPLETE") == 0 ||
        strcmp(state_name, "DASH_DASH_NOTWHITE") == 0) {
        return 1;
    }

    /* 检查转移表 - 对应Python版本的 next_target 检查 */
    const kadd_trans_entry_t *ent = &e->transitions[state][next_code];

    /* Python: if next_target != self.ID_ERROR and not isinstance(next_target, str): continue */
    /* 即：如果转移有效且不是指令，则继续扫描 */
    if (!ent->is_insn && ent->next_state != KADD_DFA_ID_ERROR) {
        /* 这是一个有效的状态转移，应该继续扫描 */
        return 1;
    }

    /* 如果是指令或ERROR，不能继续扫描 */
    return 0;
}

/* ========== JSON 解析辅助函数（使用 cJSON）========== */

/* ========== 核心 API 函数 ========== */

/**
 * @brief 创建 DFA 引擎
 */
int kadd_dfa_engine_pro_create(kadd_dfa_engine_pro_t **engine,
                               const char *table_path,
                               const char *state_path,
                               size_t max_input_length,
                               size_t max_output_dim) {
    if (!engine || !table_path || !state_path) {
        return -1;
    }
    if (max_input_length == 0 || max_output_dim == 0) {
        return -1;
    }

    kadd_dfa_engine_pro_t *e = calloc(1, sizeof(kadd_dfa_engine_pro_t));
    if (!e) return -1;

    snprintf(e->table_path, sizeof(e->table_path), "%s", table_path);
    snprintf(e->state_path, sizeof(e->state_path), "%s", state_path);
    e->max_input_length = max_input_length;
    e->max_output_dim = max_output_dim;

    /* 加载转移表 */
    FILE *f = fopen(e->table_path, "rb");
    if (!f) {
        free(e);
        return -1;
    }

    uint32_t num_states;
    if (fread(&num_states, sizeof(uint32_t), 1, f) != 1) {
        fclose(f);
        free(e);
        return -1;
    }

    e->num_states = num_states;
    e->state_names = calloc(num_states, sizeof(char *));
    e->transitions = calloc(num_states, sizeof(kadd_trans_entry_t *));
    e->meta = calloc(num_states, sizeof(kadd_state_meta_t));

    if (!e->state_names || !e->transitions || !e->meta) {
        if (e->state_names) free(e->state_names);
        if (e->transitions) free(e->transitions);
        if (e->meta) free(e->meta);
        fclose(f);
        free(e);
        return -1;
    }

    /* 读取状态名 */
    for (uint32_t i = 0; i < num_states; i++) {
        uint8_t len;
        if (fread(&len, sizeof(uint8_t), 1, f) != 1) {
            fclose(f);
            goto error;
        }
        e->state_names[i] = malloc(len + 1);
        if (fread(e->state_names[i], 1, len, f) != len) {
            fclose(f);
            goto error;
        }
        e->state_names[i][len] = '\0';

        if (strcmp(e->state_names[i], "WORD") == 0) {
            e->id_word = (int)i;
        }
    }

    /* 读取转移表 */
    for (uint32_t i = 0; i < num_states; i++) {
        e->transitions[i] = calloc(256, sizeof(kadd_trans_entry_t));
        if (!e->transitions[i]) {
            fclose(f);
            goto error;
        }
        for (int j = 0; j < 256; j++) {
            uint16_t val;
            if (fread(&val, sizeof(uint16_t), 1, f) != 1) {
                fclose(f);
                goto error;
            }
            if (val == KADD_DFA_INS_MAGIC) {
                uint8_t insn_len;
                if (fread(&insn_len, sizeof(uint8_t), 1, f) != 1) {
                    fclose(f);
                    goto error;
                }
                e->transitions[i][j].is_insn = 1;
                e->transitions[i][j].insn = malloc(insn_len + 1);
                if (fread(e->transitions[i][j].insn, 1, insn_len, f) != insn_len) {
                    fclose(f);
                    goto error;
                }
                e->transitions[i][j].insn[insn_len] = '\0';
            } else {
                e->transitions[i][j].is_insn = 0;
                e->transitions[i][j].next_state = val;
            }
        }
    }
    fclose(f);

    /* 加载状态元数据 - 使用 cJSON */
    FILE *jf = fopen(e->state_path, "rb");
    if (jf) {
        fseek(jf, 0, SEEK_END);
        long jsize = ftell(jf);
        fseek(jf, 0, SEEK_SET);
        char *json_content = malloc(jsize + 1);
        if (json_content) {
            fread(json_content, 1, jsize, jf);
            json_content[jsize] = '\0';
            fclose(jf);

            /* 使用 cJSON 解析 */
            cJSON *root = cJSON_Parse(json_content);
            if (root) {
                cJSON *states = cJSON_GetObjectItem(root, "states");
                if (states && cJSON_IsObject(states)) {
                    for (uint32_t i = 0; i < num_states; i++) {
                        const char *name = e->state_names[i];
                        cJSON *state = cJSON_GetObjectItem(states, name);
                        if (state) {
                            /* 读取 token_type */
                            cJSON *token_type_item = cJSON_GetObjectItem(state, "token_type");
                            if (token_type_item && cJSON_IsString(token_type_item)) {
                                size_t len = strlen(token_type_item->valuestring);
                                e->meta[i].token_type = malloc(len + 1);
                                if (e->meta[i].token_type) {
                                    strcpy(e->meta[i].token_type, token_type_item->valuestring);
                                }
                            }

                            /* 读取 action */
                            cJSON *action_item = cJSON_GetObjectItem(state, "action");
                            if (action_item && cJSON_IsNumber(action_item)) {
                                e->meta[i].action = (int)action_item->valuedouble;
                            }

                            /* 读取 action_arg */
                            cJSON *action_arg_item = cJSON_GetObjectItem(state, "action_arg");
                            if (action_arg_item && cJSON_IsString(action_arg_item)) {
                                size_t len = strlen(action_arg_item->valuestring);
                                e->meta[i].action_arg = malloc(len + 1);
                                if (e->meta[i].action_arg) {
                                    strcpy(e->meta[i].action_arg, action_arg_item->valuestring);
                                }
                            }
                        }
                    }
                }
                cJSON_Delete(root);
            }
            free(json_content);
        } else {
            fclose(jf);
        }
    }

    *engine = e;
    return 0;

error:
    if (e->transitions) {
        for (uint32_t i = 0; i < num_states; i++) {
            if (e->transitions[i]) {
                for (int j = 0; j < 256; j++) {
                    if (e->transitions[i][j].is_insn) {
                        free(e->transitions[i][j].insn);
                    }
                }
                free(e->transitions[i]);
            }
        }
        free(e->transitions);
    }
    if (e->state_names) {
        for (uint32_t i = 0; i < num_states; i++) {
            if (e->state_names[i]) free(e->state_names[i]);
        }
        free(e->state_names);
    }
    if (e->meta) free(e->meta);
    free(e);
    return -1;
}

/**
 * @brief 释放 DFA 引擎
 */
void kadd_dfa_engine_pro_free(kadd_dfa_engine_pro_t *e) {
    if (!e) return;

    if (e->transitions) {
        for (uint32_t i = 0; i < e->num_states; i++) {
            if (e->transitions[i]) {
                for (int j = 0; j < 256; j++) {
                    if (e->transitions[i][j].is_insn) {
                        free(e->transitions[i][j].insn);
                    }
                }
                free(e->transitions[i]);
            }
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

    free(e);
}

/**
 * @brief 处理 q-string 格式
 */
static int handle_qstring_format(const char *buf, size_t buf_len,
                                 kadd_token_t *out, size_t start_pos, size_t pos) {
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
            return 1;
        }
    }
    return 0;
}

/**
 * @brief 解析单个token
 * 基于 Python dfa_engine.py 的 _parse_from_entry 方法
 */
static int parse_token(const kadd_dfa_engine_pro_t *e, const char *s, size_t len,
                       size_t start_pos, kadd_token_t *out, size_t *new_pos, int *state_ptr) {
    int state = state_ptr ? *state_ptr : KADD_DFA_ID_ENTRY;
    char buf[MAX_BUF_SIZE] = {0};
    size_t buf_len = 0;
    char type = 0;
    const char *state_name = "ENTRY";
    size_t pos = start_pos;
    int qstring_start_pos = -1;

    #define STATE_IS_VALID(s) ((s) >= 0 && (s) < (int)e->num_states)
    #define ADD_CHAR_TO_BUF(c) do { if (buf_len < MAX_BUF_SIZE - 1) buf[buf_len++] = (c); } while(0)
    #define TOKEN_DONE() do { goto token_done; } while(0)

    while (pos < len) {
        unsigned char ch = s[pos];
        int code = (int)ch;

        /* 跳过空白字符 */
        if (is_whitespace_char(ch)) {
            if (buf_len > 0 && STATE_IS_VALID(state)) {
                state_name = e->state_names[state];
                /* 对于注释状态，空白不结束token */
                if (strstr(state_name, "COMMENT") == NULL &&
                    strcmp(state_name, "SLASH_ASTERISK") != 0 &&
                    strcmp(state_name, "HASH_COMMENT") != 0 &&
                    strcmp(state_name, "DASH_COMMENT") != 0 &&
                    strcmp(state_name, "COMMENT_COMPLETE") != 0) {
                    if (e->meta[state].token_type) {
                        type = e->meta[state].token_type[0];
                    } else if (!type) {
                        type = 'n';
                    }
                    TOKEN_DONE();
                }
            }
            pos++;
            continue;
        }

        /* 处理非ASCII字符 */
        if (code > 255) {
            state_name = STATE_IS_VALID(state) ? e->state_names[state] : "UNKNOWN";
            /* 注释和WORD状态接受非ASCII字符 */
            if ((strstr(state_name, "COMMENT") != NULL ||
                 strcmp(state_name, "SLASH_ASTERISK") == 0 ||
                 strcmp(state_name, "HASH_COMMENT") == 0 ||
                 strcmp(state_name, "DASH_COMMENT") == 0 ||
                 strcmp(state_name, "COMMENT_COMPLETE") == 0 ||
                 strcmp(state_name, "BWORD") == 0 ||
                 strcmp(state_name, "WORD") == 0 ||
                 strncmp(state_name, "WORD_", 5) == 0)) {
                ADD_CHAR_TO_BUF(ch);
                pos++;
                continue;
            }
            /* 其他状态：作为BAREWORD处理 */
            if (!type) type = 'n';
            ADD_CHAR_TO_BUF(ch);
            pos++;
            /* 检查下一个字符是否是分隔符 */
            if (pos < len && is_delimiter_char(s[pos])) {
                TOKEN_DONE();
            }
            continue;
        }

        /* 获取状态转移 */
        kadd_trans_entry_t *ent = &e->transitions[state][ch];

        if (ent->next_state == KADD_DFA_ID_ERROR) {
            if (buf_len > 0) {
                if (!type) type = 'n';
                TOKEN_DONE();
            } else {
                pos++;
                continue;
            }
        }

        /* 处理特殊指令 */
        if (ent->is_insn) {
            state_name = STATE_IS_VALID(state) ? e->state_names[state] : "UNKNOWN";

            /* 对于WORD_*状态，检查是否是标识符字符 */
            if ((strncmp(state_name, "WORD_", 5) == 0 || strcmp(state_name, "WORD") == 0 ||
                 strcmp(state_name, "BWORD") == 0) && is_identifier_char(code)) {
                state = e->id_word;
                ADD_CHAR_TO_BUF(s[pos]);
                pos++;
                continue;
            }

            /* 对于多字符操作符状态，检查是否可以继续扫描 */
            if (pos + 1 < len) {
                unsigned char next_code = s[pos + 1];
                const kadd_trans_entry_t *next_ent = &e->transitions[state][next_code];
                if (!next_ent->is_insn && next_ent->next_state != KADD_DFA_ID_ERROR &&
                    next_ent->next_state < e->num_states) {
                    const char *next_state_name = e->state_names[next_ent->next_state];
                    /* 只有特定的单字符操作符状态才能继续扫描形成多字符操作符 */
                    int can_continue = 0;
                    if (strcmp(state_name, "OPERATOR2_PIPE") == 0 && next_code == '|') can_continue = 1;
                    else if (strcmp(state_name, "OPERATOR2_AMP") == 0 && next_code == '&') can_continue = 1;
                    else if (strcmp(state_name, "OPERATOR2_EQ") == 0 && next_code == '=') can_continue = 1;
                    else if (strcmp(state_name, "OPERATOR2_NOT") == 0 && (next_code == '=' || next_code == '!')) can_continue = 1;
                    else if (strcmp(state_name, "OPERATOR2_LT") == 0 && (next_code == '=' || next_code == '<')) can_continue = 1;
                    else if (strcmp(state_name, "OPERATOR2_GT") == 0 && (next_code == '=' || next_code == '>')) can_continue = 1;
                    else if (strcmp(state_name, "OPERATOR2_COLON") == 0 && next_code == ':') can_continue = 1;
                    else if (strcmp(state_name, "PLUS") == 0 && next_code == '+') can_continue = 1;
                    else if (strcmp(state_name, "DASH") == 0 && next_code == '-') can_continue = 1;
                    else if (strcmp(state_name, "STAR") == 0 && next_code == '*') can_continue = 1;

                    if (can_continue) {
                        state = next_ent->next_state;
                        ADD_CHAR_TO_BUF(s[pos]);
                        pos++;
                        continue;
                    }
                }
            }

            /* 处理 dup/copy 指令 */
            char insn_type = parse_special_instruction_type(ent);
            int is_copy_entry = (ent->insn[0] == 'c' && strcmp(ent->insn, "copy ENTRY") == 0);

            if (insn_type != 0 || is_copy_entry) {
                if (insn_type != 0 && type == 0) {
                    type = insn_type;
                }

                /* 对于BWORD状态，需要包含']' */
                if (strcmp(state_name, "BWORD") == 0 && code == ']') {
                    ADD_CHAR_TO_BUF(ch);
                    pos++;
                }
                /* 对于QSTRING_R*状态，需要包含结束的''' */
                else if (strncmp(state_name, "QSTRING_R", 10) == 0 && code == '\'') {
                    ADD_CHAR_TO_BUF(ch);
                    pos++;
                }
                /* 对于TICK_BWORD_TICK状态，需要包含结束的` */
                else if (strcmp(state_name, "TICK_BWORD_TICK") == 0 && code == '`') {
                    ADD_CHAR_TO_BUF(ch);
                    pos++;
                }
                else {
                    /* 其他情况：不消费当前字符 */
                }
                TOKEN_DONE();
            }
            break;
        }

        /* 处理 DASH -> DASH_DASH 特殊情况 */
        if (STATE_IS_VALID(state) && STATE_IS_VALID(ent->next_state)) {
            const char *current_name = e->state_names[state];
            const char *next_name = e->state_names[ent->next_state];
            if (strcmp(current_name, "DASH") == 0 && strcmp(next_name, "DASH_DASH") == 0) {
                /* 检查下一个字符是否是空白 */
                if (pos + 1 >= len) {
                    /* '--' 在输入末尾：发出 OPERATOR token */
                    if (!type) type = 'o';
                    TOKEN_DONE();
                } else {
                    unsigned char next_char = s[pos + 1];
                    if (is_whitespace_char(next_char)) {
                        /* '--' 后跟空白，是注释，不发出operator */
                    } else {
                        /* '--' 后跟非空白字符：发出第一个 '-' 的 OPERATOR token */
                        if (!type) type = 'o';
                        TOKEN_DONE();
                    }
                }
            }
        }

        /* 处理特殊前缀字符串（B', X', E', N', U&'）的引号字符 */
        if (STATE_IS_VALID(state) && buf_len > 0) {
            const char *current_name = e->state_names[state];
            if ((ch == '\'' || ch == '"') &&
                (strncmp(current_name, "WORD_", 5) == 0 || strcmp(current_name, "WORD") == 0)) {
                /* 检查是否是特殊前缀字符串的转换 */
                if (buf_len == 1) {
                    char prefix = buf[0];
                    if ((prefix == 'B' || prefix == 'b' || prefix == 'X' || prefix == 'x' ||
                         prefix == 'E' || prefix == 'e' || prefix == 'N' || prefix == 'n') &&
                         ch == '\'') {
                        /* 特殊前缀字符串：不发出token，让后续逻辑处理 */
                    } else if ((prefix == 'U' || prefix == 'u') && ch == '&') {
                        /* U& 前缀：继续扫描 */
                    } else {
                        /* 普通情况：发出当前WORD token，引号将作为新token的开始 */
                        if (!type) type = 'n';
                        TOKEN_DONE();
                    }
                } else if (buf_len == 2 && buf[0] == 'U' && buf[1] == '&' && ch == '\'') {
                    /* U&' 前缀字符串：继续扫描 */
                } else if (buf_len >= 2) {
                    /* 检查是否已经是前缀+引号的组合 */
                    char prefix = buf[0];
                    char prev = buf[buf_len - 1];
                    if ((prefix == 'B' || prefix == 'b' || prefix == 'X' || prefix == 'x' ||
                         prefix == 'E' || prefix == 'e' || prefix == 'N' || prefix == 'n') &&
                         prev == ch && (ch == '\'' || ch == '"')) {
                        /* 已经是前缀+引号，继续扫描 */
                    } else {
                        /* 检查是否需要在引号处结束当前token */
                        if (!is_identifier_char(ch)) {
                            if (!type) type = 'n';
                            TOKEN_DONE();
                        }
                    }
                }
            }
        }

        /* 普通状态转移 */
        state = ent->next_state;
        state_name = STATE_IS_VALID(state) ? e->state_names[state] : "UNKNOWN";

        /* 转移到WORD且缓冲区非空时的特殊处理 */
        if (state == e->id_word && buf_len > 0) {
            if (!is_word_char(code)) {
                type = 'n';
                TOKEN_DONE();
            }
        }

        /* 从注释状态转移到ENTRY时，发出注释token */
        if (state == KADD_DFA_ID_ENTRY && buf_len > 0) {
            const char *prev_name = e->state_names[ent->next_state];
            if (strstr(prev_name, "COMMENT") != NULL ||
                strcmp(prev_name, "SLASH_ASTERISK") == 0 ||
                strcmp(prev_name, "HASH_COMMENT") == 0 ||
                strcmp(prev_name, "DASH_COMMENT") == 0) {
                type = 'c';
                TOKEN_DONE();
            }
        }

        ADD_CHAR_TO_BUF(s[pos]);
        pos++;

        /* 追踪q-string起始位置 */
        if ((strstr(state_name, "QSTRING") != NULL || strstr(state_name, "NQSTRING") != NULL) &&
            qstring_start_pos < 0) {
            qstring_start_pos = (int)(pos - 1);
        }

        /* 检查状态的action */
        if (STATE_IS_VALID(state)) {
            kadd_state_meta_t *meta = &e->meta[state];

            if (meta->action == 3) { /* ACTION_DUP_PREPEND */
                /* 检查是否可以继续扫描 */
                if (pos < len && can_continue_scanning(e, state, s[pos], 1)) {
                    continue;
                }

                /* 对于WORD_*状态，额外检查下一个字符是否是标识符字符 */
                if (pos < len && (strncmp(state_name, "WORD_", 5) == 0 ||
                                  strcmp(state_name, "WORD") == 0 ||
                                  strcmp(state_name, "BWORD") == 0)) {
                    if (is_identifier_char(s[pos]) || s[pos] == '.') {
                        continue;
                    }
                }

                if (meta->action_arg && meta->action_arg[0]) {
                    type = meta->action_arg[0];
                }
                /* 对于ACTION_DUP_PREPEND，不回退pos
                 * 因为操作符字符已经被包含在缓冲区中了
                 * pos 指向下一个要处理的字符 */
                TOKEN_DONE();
            } else if (meta->action == 2) { /* ACTION_COPY */
                /* 对于ACTION_COPY，检查是否有token_type */
                if (meta->token_type && meta->token_type[0]) {
                    type = meta->token_type[0];
                    /* 对于某些状态（如DASH_DASH），需要特殊处理 */
                    if (strcmp(state_name, "DASH_DASH") == 0) {
                        if (pos < len && s[pos] == '&') {
                            /* --& 模式：发出两个 OPERATOR token */
                            type = 'o';
                            pos--;  /* 回退以发出第二个'-' */
                        }
                    }
                    TOKEN_DONE();
                }
            } else if (meta->token_type) {
                type = meta->token_type[0];
                if (meta->action == 0) { /* ACTION_NONE */
                    /* 对于多字符操作符的最终状态，立即发出token */
                    if (strstr(state_name, "OPERATOR2_PIPE_PIPE") != NULL ||
                        strstr(state_name, "OPERATOR2_AMP_AMP") != NULL ||
                        strstr(state_name, "OPERATOR2_NOT_EQ") != NULL ||
                        strstr(state_name, "OPERATOR2_NOT_NOT") != NULL ||
                        strstr(state_name, "OPERATOR2_LT_LT") != NULL ||
                        strstr(state_name, "OPERATOR2_LT_GT") != NULL ||
                        strstr(state_name, "OPERATOR2_GT_GT") != NULL ||
                        strstr(state_name, "OPERATOR2_LT_EQ") != NULL ||
                        strstr(state_name, "OPERATOR2_GT_EQ") != NULL ||
                        strstr(state_name, "OPERATOR2_COLON_COLON") != NULL ||
                        strstr(state_name, "OPERATOR2_LT_EQ_GT") != NULL) {
                        /* 立即发出token，不回退pos */
                        TOKEN_DONE();
                    }

                    /* 检查是否可以继续扫描 */
                    if (pos < len && can_continue_scanning(e, state, s[pos], 1)) {
                        continue;
                    }
                    /* 对于WORD_*状态，额外检查下一个字符是否是标识符字符 */
                    if (pos < len && (strncmp(state_name, "WORD_", 5) == 0 ||
                                      strcmp(state_name, "WORD") == 0 ||
                                      strcmp(state_name, "BWORD") == 0)) {
                        if (is_identifier_char(s[pos]) || s[pos] == '.') {
                            continue;
                        }
                    }
                    /* 回退pos */
                    if (pos > start_pos) {
                        pos--;
                    }
                    TOKEN_DONE();
                }
            }
        }
    }

token_done:
    /* 检查当前状态是否有ACTION_DUP_PREPEND */
    if (buf_len > 0 && !type && STATE_IS_VALID(state)) {
        state_name = e->state_names[state];
        kadd_state_meta_t *meta = &e->meta[state];
        if (meta->action == 3 && meta->action_arg) {
            type = meta->action_arg[0];
        }
    }

    /* 对于WORD_*状态，如果没有token_type，使用BAREWORD */
    if (buf_len > 0 && type && STATE_IS_VALID(state)) {
        state_name = e->state_names[state];
        if (strncmp(state_name, "WORD_", 5) == 0) {
            kadd_state_meta_t *meta = &e->meta[state];
            if (!meta->token_type) {
                type = 'n';
            }
        }
    }

    if (buf_len > 0 && type) {
        /* 处理q-string格式 */
        if (qstring_start_pos >= 0 && type == 's') {
            int qstring_handled = handle_qstring_format(buf, buf_len, out, start_pos, pos);
            if (qstring_handled == 1) {
                *new_pos = pos;
                return 1;
            } else if (qstring_handled == -1) {
                return 0;
            }
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
        /* 使用 start_pos + buf_len 而不是 pos，因为 pos 可能已经被回退修改了 */
        *new_pos = start_pos + buf_len;
        return 1;
    }

    if (pos > start_pos) {
        state_name = STATE_IS_VALID(state) ? e->state_names[state] : "UNKNOWN";
        if (strstr(state_name, "STRING_") != NULL && buf_len > 0) {
            type = 's';
        }
        if (!type) type = 'n';
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
        return 1;
    }

    if (pos == start_pos) {
        out->is_inline = 1;
        out->value.value_inline[0] = s[pos];
        out->value.value_inline[1] = '\0';
        out->pos = pos;
        out->type = type ? type : 'n';
        *new_pos = pos + 1;
        return 1;
    }

    #undef STATE_IS_VALID
    #undef ADD_CHAR_TO_BUF
    #undef TOKEN_DONE

    return 0;
}

/**
 * @brief 分词输入字符串
 */
int kadd_dfa_engine_pro_tokenize(const kadd_dfa_engine_pro_t *e,
                                const char *input,
                                size_t input_length,
                                int *output,
                                int *dim) {
    if (!e || !input || !output || !dim) return -1;

    if (input_length > e->max_input_length) {
        return -1;
    }

    size_t pos = 0;
    *dim = 0;
    int dfa_state = KADD_DFA_ID_ENTRY;  /* 每次都从 ENTRY 开始 */

    while (pos < input_length && (size_t)*dim < e->max_output_dim) {
        unsigned char ch = input[pos];
        if (is_whitespace_char(ch)) {
            pos++;
            continue;
        }

        kadd_token_t tok;
        size_t new_pos = 0;
        int temp_state = KADD_DFA_ID_ENTRY;  /* 每次都从 ENTRY 开始解析 */
        int parse_result = parse_token(e, input, input_length, pos, &tok, &new_pos, &temp_state);
        if (parse_result) {
            char *temp_heap_ptr = NULL;
            if (!tok.is_inline && tok.value.value_ptr) {
                temp_heap_ptr = tok.value.value_ptr;
            }

            int token_index = kadd_dfa_engine_pro_char_type_to_index(tok.type);
            output[(*dim)++] = token_index;

            if (temp_heap_ptr) free(temp_heap_ptr);
            pos = new_pos;

            /* 防止无限循环：确保pos前进 */
            if (pos <= 0) pos = 1;
        } else {
            pos++;
        }
    }

    return 0;
}
