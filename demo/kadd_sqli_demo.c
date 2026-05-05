/**
 * @file kadd_sqli_demo.c
 * @brief SQL 注入检测引擎交互式演示
 *
 * 演示如何使用 kadd_sqli_engine 进行 SQL 注入检测
 * 整合 DFA 分词 + 特征提取 + ML 推理
 *
 * 使用方式:
 *   ./kadd_sqli_demo    # 交互式模式
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kadd_sqli_engine.h"

#ifdef _WIN32
#include <windows.h>
#endif

/* ========== 检测和打印函数 ========== */

/**
 * @brief 检测输入并打印结果
 */
static void detect_and_print(kadd_sqli_engine_t *engine, const char *input) {
    float probability = 0.0f;

    int ret = kadd_sqli_detect(engine, input, &probability);
 
    if (ret == KADD_SQLI_OK) {
        int is_sqli = (probability >= 0.5f) ? 1 : 0;
        const char *icon = is_sqli ? "⚠"  : "✓";
        const char *status = is_sqli ? "[SQL 注入]" : "[安全]";

        printf("  %s %-40s | 概率: %6.2f%%\n", icon, input, probability * 100);
    } else {
        printf("  ✗ %-40s | 检测失败 (错误码: %d)\n", input, ret);
    }
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
    printf("║        SQL 注入检测引擎交互式演示 (kadd_sqli_engine)       ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");

    // ========== 创建引擎 ==========
    printf("正在初始化引擎...\n");

    kadd_sqli_engine_t *engine = NULL;
    if (kadd_sqli_engine_create_from_file(&engine, "config.json") != KADD_SQLI_OK) {
        printf("✗ 引擎创建失败，请检查 config.json\n");
        return 1;
    }

    printf("✓ 引擎初始化成功\n\n");
    printf("输入 'quit' 或 'exit' 退出，'help' 查看帮助\n\n");

    // ========== 交互式检测 ==========
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
            printf("  SQL 注入示例:\n");
            printf("    id=1' OR '1'='1\n");
            printf("    admin'--\n");
            printf("    1' UNION SELECT NULL--\n\n");
            printf("  正常输入示例:\n");
            printf("    user@example.com\n");
            printf("    SELECT * FROM users\n");
            printf("    price=99.99\n\n");
            line_num++;
            continue;
        }

        if (strlen(input) == 0) continue;

        detect_and_print(engine, input);
        line_num++;
    }

    // ========== 释放资源 ==========
    kadd_sqli_engine_free(engine);
    printf("\n✓ 引擎已释放\n");

    return 0;
}
