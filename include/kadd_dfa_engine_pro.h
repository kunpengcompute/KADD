#ifndef KADD_DFA_ENGINE_PRO_H
#define KADD_DFA_ENGINE_PRO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief DFA 引擎结构
 */
typedef struct kadd_dfa_engine_pro_t kadd_dfa_engine_pro_t;

/**
 * @brief 创建 DFA 引擎
 *
 * @param engine 输出参数，返回创建的引擎指针
 * @param table_path 转移表文件路径 (transition_table.bin)
 * @param state_path 状态元数据文件路径 (states_meta.json)
 * @param max_input_length 最大输入字符长度
 * @param max_output_dim 最大输出特征维度
 * @return 0 成功，非0 失败
 */
int kadd_dfa_engine_pro_create(kadd_dfa_engine_pro_t **engine,
                               const char *table_path,
                               const char *state_path,
                               size_t max_input_length,
                               size_t max_output_dim);

/**
 * @brief 释放 DFA 引擎
 *
 * @param e DFA 引擎指针
 */
void kadd_dfa_engine_pro_free(kadd_dfa_engine_pro_t *e);

/**
 * @brief 分词输入字符串
 *
 * @param e DFA 引擎
 * @param input 输入字符串
 * @param input_length 输入字符串长度
 * @param output 输出数组（存储token索引）
 * @param dim 输出参数，返回实际的token数量
 * @return 0 成功，非0 失败
 */
int kadd_dfa_engine_pro_tokenize(const kadd_dfa_engine_pro_t *e,
                                const char *input,
                                size_t input_length,
                                int *output,
                                int *dim);

/**
 * @brief 获取token类型名称
 *
 * @param type token类型字符
 * @return token类型名称字符串
 */
const char* kadd_dfa_engine_pro_get_token_name(char type);

/**
 * @brief 将token类型字符转换为索引（与libinjection兼容）
 *
 * @param type token类型字符
 * @return token类型索引
 */
int kadd_dfa_engine_pro_char_type_to_index(char type);

/**
 * @brief Token类型索引定义（与libinjection兼容）
 */
enum kadd_dfa_token_type {
    KADD_DFA_TYPE_NONE = 0,        /* 0 */
    KADD_DFA_TYPE_KEYWORD = 1,     /* k */
    KADD_DFA_TYPE_UNION = 2,       /* U */
    KADD_DFA_TYPE_GROUP = 3,       /* B */
    KADD_DFA_TYPE_EXPRESSION = 4,   /* E */
    KADD_DFA_TYPE_SQLTYPE = 5,     /* t */
    KADD_DFA_TYPE_FUNCTION = 6,    /* f */
    KADD_DFA_TYPE_BAREWORD = 7,    /* n */
    KADD_DFA_TYPE_NUMBER = 8,      /* 1 */
    KADD_DFA_TYPE_VARIABLE = 9,    /* v */
    KADD_DFA_TYPE_STRING = 10,     /* s */
    KADD_DFA_TYPE_OPERATOR = 11,   /* o */
    KADD_DFA_TYPE_LOGIC_OPERATOR = 12, /* & */
    KADD_DFA_TYPE_COMMENT = 13,    /* c */
    KADD_DFA_TYPE_COLLATE = 14,    /* A */
    KADD_DFA_TYPE_LEFTPARENS = 15, /* ( */
    KADD_DFA_TYPE_RIGHTPARENS = 16,/* ) */
    KADD_DFA_TYPE_LEFTBRACE = 17,  /* { */
    KADD_DFA_TYPE_RIGHTBRACE = 18,  /* } */
    KADD_DFA_TYPE_DOT = 19,        /* . */
    KADD_DFA_TYPE_COMMA = 20,      /* , */
    KADD_DFA_TYPE_COLON = 21,      /* : */
    KADD_DFA_TYPE_SEMICOLON = 22,  /* ; */
    KADD_DFA_TYPE_STATEMENT = 23,  /* T */
    KADD_DFA_TYPE_UNKNOWN = 24,    /* ? */
    KADD_DFA_TYPE_EVIL = 25,       /* X */
    KADD_DFA_TYPE_FINGERPRINT = 26,/* F */
    KADD_DFA_TYPE_BACKSLASH = 27   /* \ */
};

#ifdef __cplusplus
}
#endif

#endif /* KADD_DFA_ENGINE_PRO_H */
