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
- **模型认证**：原生`.krf`/`.kcn`强制HMAC-SHA256认证，未签名、密钥缺失或不匹配一律拒绝加载（fail-closed）并回退ONNX；`.onnx`不参与认证

---

## 前提条件

请参考[安装指南](installation_guide.md)完成安装。

加载原生模型（`.krf`/`.kcn`）前还需注入模型HMAC密钥：

```bash
export KADD_MODEL_HMAC_KEY=74fab58d87dc235ee049b6faf21b44825dba1d9dc05bbc14aeded49cb211a1db
```

也可以直接source `build.sh`生成的环境文件（同时包含库搜索路径）：

```bash
source /opt/KADD/SQLi/build/kadd_env.sh
```

![说明图标](../../public_sys-resources/icon-note.gif) **说明：**

- 上面的密钥是仓库自带的公开**开发密钥**，仅用于构建、测试与功能体验；生产部署必须用自有密钥通过`tools/rf_sign/rf_sign.py`重新签名模型，并经KMS或受限权限的env文件注入。
- 未注入密钥时，原生模型会被拒绝加载并在stderr打印ERROR，随后回退ONNX（丢失SVE加速）；直接调用`kadd_rf_model_load()`/`kadd_cnn_model_load()`的程序则加载失败。

### 性能调优：ONNX Runtime线程数

回退到ONNX Runtime时，引擎在创建session时已显式设置`intra_op=8`、`inter_op=1`，原因如下：

- 模型很小（batch≤64、feature_dim=300），ONNX Runtime默认按物理核数开线程，线程派发与同步开销远超算子计算本身。
- 实测CNN批量吞吐只有约1.6千samples/sec；固定为8/1后恢复到约1.4万-1.8万samples/sec。

如需按机型调优，可用环境变量覆盖，无需重新编译：

```bash
export KADD_ORT_INTRA_OP_THREADS=8   # 取值1-1024，非法值忽略并回退默认8
export KADD_ORT_INTER_OP_THREADS=1   # 取值1-1024，非法值忽略并回退默认1
```

![说明图标](../../public_sys-resources/icon-note.gif) **说明：**

- 线程数只影响性能，不影响检测结果；取值非法时仅打印WARN并沿用默认值，不会导致模型加载失败。
- 环境变量在创建引擎实例时读取，修改后需重新创建引擎才生效。
- `coverage_all.sh`已默认导出上述两个变量（8/1），可通过`KADD_ORT_INTRA_OP_THREADS=4 ./coverage_all.sh`临时调优。

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

2. 设置库搜索路径与模型HMAC密钥，然后运行程序。

    ```bash
    export LD_LIBRARY_PATH=/opt/KADD/SQLi/build/src:/opt/KADD/SQLi/util/onnxruntime-linux-aarch64-1.28.0/lib:$LD_LIBRARY_PATH
    # 开发密钥，生产环境请替换为自有密钥
    export KADD_MODEL_HMAC_KEY=74fab58d87dc235ee049b6faf21b44825dba1d9dc05bbc14aeded49cb211a1db
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
    if (kadd_sqli_engine_create_from_file(&engine, "/opt/KADD/SQLi/config.json")
            != KADD_SUCCESS) {
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

2. 运行前设置库搜索路径与模型HMAC密钥（包含KADD动态库和ONNX Runtime），然后运行程序。

    ```bash
    export LD_LIBRARY_PATH=/opt/KADD/SQLi/build/src:/opt/KADD/SQLi/util/onnxruntime-linux-aarch64-1.28.0/lib:$LD_LIBRARY_PATH
    # 开发密钥，生产环境请替换为自有密钥
    export KADD_MODEL_HMAC_KEY=74fab58d87dc235ee049b6faf21b44825dba1d9dc05bbc14aeded49cb211a1db
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
