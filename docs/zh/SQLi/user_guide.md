# 用户指南

本指南提供KADD SQL注入检测引擎的配置说明和具体代码示例。

---

## 配置文件

KADD通过 `config.json` 配置各引擎模块：

```json
{
  "engine_mode": "ml",

  "transition_table": "/opt/KADD/SQLi/assets/data/transition_table.bin",
  "output_table": "/opt/KADD/SQLi/assets/data/output_table.bin",
  "eod_table": "/opt/KADD/SQLi/assets/data/eod_table.bin",

  "model_rf": "/opt/KADD/SQLi/assets/model/model_tokens_rf.krf",
  "model_rf_onnx": "/opt/KADD/SQLi/assets/model/model_tokens_rf.onnx",

  "model_cnn": "/opt/KADD/SQLi/assets/model/model_tokens_dl_cnn.kcn",
  "model_cnn_onnx": "/opt/KADD/SQLi/assets/model/model_tokens_dl_cnn.onnx"
}
```

### 参数说明

| 参数 | 说明 | 可选值 |
|------|------|--------|
| `engine_mode` | 推理模式 | `ml`（随机森林）/ `dl`（CNN） |
| `transition_table` | DFA状态转移表路径 | 默认即可 |
| `output_table` | DFA输出表路径 | 默认即可 |
| `eod_table` | DFA结束状态表路径 | 默认即可 |
| `model_rf` | RF原生模型路径（`.krf`） | 默认即可 |
| `model_rf_onnx` | RF ONNX模型路径（`.onnx`） | 默认即可 |
| `model_cnn` | CNN原生模型路径（`.kcn`） | 默认即可 |
| `model_cnn_onnx` | CNN ONNX模型路径（`.onnx`） | 默认即可 |

### 推理模式说明

- **ML模式**（默认）：使用随机森林分类器。SVE可用时优先加载原生`.krf`格式，失败则回退ONNX Runtime加载`.onnx`格式
- **DL模式**：使用CNN深度学习模型。SVE可用时优先加载原生`.kcn`格式，失败则回退ONNX Runtime加载`.onnx`格式

---

## 前提条件

请参考[安装指南](installation_guide.md)完成安装。

---

## SQL注入检测完整用例

### 用例1：Web应用实时检测

#### 完整代码 web_detection_app.c

示例程序按以下顺序执行：

1. 创建引擎。
2. 模拟用户输入检测。输入包含正常输入和SQL注入攻击。
3. 逐个检测并按概率输出结果。
4. 清理资源。

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kadd_sqli_engine.h"
#include "kadd_errors.h"

int main() {
    kadd_sqli_engine_t *engine = NULL;
    int ret = kadd_sqli_engine_create_from_file(&engine, "/opt/KADD/SQLi/config.json");
    if (ret != KADD_SUCCESS) {
        fprintf(stderr, "引擎创建失败: %d\n", ret);
        return -1;
    }

    const char *user_inputs[] = {
        "id=1",
        "name=John",
        "id=1' OR '1'='1",
        "admin'--",
        "SELECT * FROM users"
    };

    for (int i = 0; i < 5; i++) {
        float probability = 0.0f;
        ret = kadd_sqli_detect(engine, user_inputs[i], &probability);

        if (ret == KADD_SUCCESS) {
            if (probability >= 0.5f) {
                printf("[攻击] 输入: \"%s\" (概率: %.2f%%)\n",
                       user_inputs[i], probability * 100);
            } else {
                printf("[安全] 输入: \"%s\" (概率: %.2f%%)\n",
                       user_inputs[i], probability * 100);
            }
        }
    }

    kadd_sqli_engine_free(engine);
    return 0;
}
```

#### 编译运行

1. 编译程序。

    ```bash
    gcc -o web_detection_app web_detection_app.c \
        -I/opt/KADD/SQLi/include \
        -L/opt/KADD/SQLi/build/src \
        -lkadd_sqli_engine -lkadd_dl_engine -lkadd_ml_engine \
        -lkadd_cnn_engine -lkadd_rf_engine -lkadd_inference_engine \
        -lkadd_feature_engine -lkadd_dfa_engine -lkadd_config_loader
    ```

2. 设置库搜索路径并运行程序。

    ```bash
    export LD_LIBRARY_PATH=/opt/KADD/SQLi/build/src:/opt/KADD/SQLi/util/onnxruntime-linux-aarch64-1.18.0/lib:$LD_LIBRARY_PATH
    ./web_detection_app
    ```

##### 预期输出

```bash
[安全] 输入: "id=1" (概率: 0.02%)
[安全] 输入: "name=John" (概率: 0.01%)
[攻击] 输入: "id=1' OR '1'='1" (概率: 98.54%)
[攻击] 输入: "admin'--" (概率: 95.21%)
[攻击] 输入: "SELECT * FROM users" (概率: 87.33%)
```

---

### 用例2：批量日志分析

#### 完整代码 log_analysis_app.c

示例程序按以下顺序执行：

1. 创建引擎。
2. 准备批量日志数据。
3. 批量检测并输出攻击日志。
4. 清理资源。

```c
#include <stdio.h>
#include "kadd_sqli_engine.h"
#include "kadd_errors.h"

int main() {
    kadd_sqli_engine_t *engine = NULL;
    if (kadd_sqli_engine_create_from_file(&engine, "/opt/KADD/SQLi/config.json") != KADD_SUCCESS) {
        return -1;
    }

    const char *log_entries[] = {
        "[2024-01-01 10:00:00] GET /api/user?id=123",
        "[2024-01-01 10:00:05] POST /login user=admin&pass=test",
        "[2024-01-01 10:00:10] GET /search?q=cat' OR '1'='1",
        "[2024-01-01 10:00:15] POST /update id=1; DROP TABLE users--"
    };
    int log_count = 4;
    float probabilities[log_count];

    int ret = kadd_sqli_detect_batch(engine, log_entries, log_count,
                                      probabilities, 64);

    if (ret >= 0) {
        printf("批量检测结果：\n");
        for (int i = 0; i < log_count; i++) {
            if (probabilities[i] >= 0.5f) {
                printf("[攻击] 日志%d: 检测到攻击 (%.2f%%)\n", i+1, probabilities[i] * 100);
                printf("   内容: %s\n", log_entries[i]);
            }
        }
    }

    kadd_sqli_engine_free(engine);
    return 0;
}
```

#### 编译运行

1. 编译程序。

    ```bash
    gcc -o log_analysis_app log_analysis_app.c \
        -I/opt/KADD/SQLi/include \
        -L/opt/KADD/SQLi/build/src \
        -lkadd_sqli_engine -lkadd_dl_engine -lkadd_ml_engine \
        -lkadd_cnn_engine -lkadd_rf_engine -lkadd_inference_engine \
        -lkadd_feature_engine -lkadd_dfa_engine -lkadd_config_loader
    ```

2. 运行前设置库搜索路径，包含KADD动态库和ONNX Runtime，然后运行程序。

    ```bash
    export LD_LIBRARY_PATH=/opt/KADD/SQLi/build/src:/opt/KADD/SQLi/util/onnxruntime-linux-aarch64-1.18.0/lib:$LD_LIBRARY_PATH
    ./log_analysis_app
    ```

##### 预期输出

```bash
批量检测结果：
[攻击] 日志3: 检测到攻击 (83.04%)
内容: [2024-01-01 10:00:10] GET /search?q=cat' OR '1'='1
[攻击] 日志4: 检测到攻击 (79.58%)
内容: [2024-01-01 10:00:15] POST /update id=1; DROP TABLE users--
```
