/**
 * @file kadd_dfa_demo.c
 * @brief DFA 分词引擎交互式演示
 *
 * 演示如何使用 kadd_dfa_engine 进行文本分词
 * 将输入字符串转换为 token 索引向量
 *
 * 使用方式:
 *   ./kadd_dfa_demo    # 交互式模式
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kadd_dfa_engine.h"
#include "tools.h"

#ifdef _WIN32
#include <windows.h>
#endif

/* ========== Token 类型名称映射 ========== */

static const char *token_type_name(int index) {
    static const char *names[] = {
        "NONE", "KEYWORD", "UNION", "GROUP", "EXPRESSION",
        "SQLTYPE", "FUNCTION", "BAREWORD", "NUMBER", "VARIABLE",
        "STRING", "OPERATOR", "LOGIC_OP", "COMMENT", "COLLATE",
        "LPAREN", "RPAREN", "LBRACE", "RBRACE", "DOT",
        "COMMA", "COLON", "SEMICOLON", "STATEMENT", "UNKNOWN",
        "EVIL", "FINGERPRINT", "BACKSLASH"
    };
    if (index >= 0 && index < 28) return names[index];
    return "UNKNOWN";
}

/* ========== 分词和打印函数 ========== */

/**
 * @brief 对输入进行分词并打印结果
 */
static void tokenize_and_print(kadd_dfa_engine_t *engine, const char *input) {
    int tokens[8192];
    int token_count = 0;

    // 开始计时
    void *timer = timer_start();

    int ret = kadd_dfa_engine_tokenize(engine, input, strlen(input), tokens, &token_count);

    // 结束计时
    double elapsed_us = timer_end(timer);

    if (ret != 0) {
        printf("  ✗ 分词失败 (错误码: %d)\n\n", ret);
        return;
    }

    printf("  ✓ 分词成功 (耗时: %.2f μs)\n", elapsed_us);
    printf("  Token 向量 [长度=%d]: [", token_count);
    for (int i = 0; i < token_count && i < 20; i++) {
        if (i > 0) printf(", ");
        printf("%d", tokens[i]);
    }
    if (token_count > 20) printf(", ...");
    printf("]\n");

    if (token_count <= 20) {
        printf("  Token 详情:\n");
        for (int i = 0; i < token_count; i++) {
            printf("    [%2d] %2d -> %s\n", i, tokens[i], token_type_name(tokens[i]));
        }
    }
    printf("\n");
}

/* ========== 主程序 ========== */

int main(int argc, char *argv[])
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║           DFA 分词引擎交互式演示 (kadd_dfa_engine)        ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");

    // ========== 创建 DFA 引擎 ==========
    printf("正在初始化 DFA 引擎...\n");

    kadd_dfa_engine_t *engine = NULL;
    if (kadd_dfa_engine_create(&engine, "tools/profile_data/transition_table.bin",
                                "tools/profile_data/states_meta.json", 8192, 8192) != 0) {
        printf("✗ 引擎创建失败，请检查文件路径\n");
        return 1;
    }

    printf("✓ 引擎初始化成功\n\n");
    printf("输入 'quit' 或 'exit' 退出，'help' 查看帮助\n\n");

    // ========== 交互式模式 ==========
    char input[2048];
    int line_num = 1;

    while (printf("[%d]> ", line_num), fgets(input, sizeof(input), stdin)) {
        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "quit") == 0 || strcmp(input, "exit") == 0 ||
            strcmp(input, "q") == 0) {
            break;
        }

        if (strcmp(input, "help") == 0 || strcmp(input, "h") == 0 ||
            strcmp(input, "?") == 0) {
            printf("\n帮助信息:\n");
            printf("  Token 类型: 0=NONE 1=KEYWORD 8=NUMBER 10=STRING 11=OPERATOR\n");
            printf("             15=LPAREN 16=RPAREN 22=SEMICOLON 24=UNKNOWN\n\n");
            line_num++;
            continue;
        }

        if (strlen(input) == 0) continue;

        printf("  输入: \"%s\"\n", input);
        tokenize_and_print(engine, input);
        line_num++;
    }

    // ========== 释放资源 ==========
    kadd_dfa_engine_free(engine);
    printf("\n✓ 引擎已释放\n");

    return 0;
}
