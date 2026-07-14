# 用户指南

本指南提供KADD SQL注入检测引擎的配置说明和具体代码示例。

---

## 配置文件

KADD各模块通过 `config.json` 配置：

```json
{
  "kadd_dfa_engine": {
    "transition_table_path": "/opt/KADD/SQLi/assets/data/transition_table.bin",
    "output_table_path": "/opt/KADD/SQLi/assets/data/output_table.bin",
    "eod_table_path": "/opt/KADD/SQLi/assets/data/eod_table.bin",
    "max_input_length": 300,
    "max_output_dim": 300
  },
  "kadd_ml_engine": {
    "model_path": "/opt/KADD/SQLi/assets/model/model_tokens_rf.onnx",
    "feature_dim": 28,
    "window_size": 5
  },
  "kadd_sqli_engine": {
    "transition_table_path": "/opt/KADD/SQLi/assets/data/transition_table.bin",
    "output_table_path": "/opt/KADD/SQLi/assets/data/output_table.bin",
    "eod_table_path": "/opt/KADD/SQLi/assets/data/eod_table.bin",
    "ml_model_path": "/opt/KADD/SQLi/assets/model/model_tokens_rf.onnx",
    "feature_dim": 28,
    "window_size": 5,
    "max_input_length": 300,
    "max_output_dim": 300
  },
  "kadd_feature_engine": {
    "ml_model_path": "/opt/KADD/SQLi/assets/model/model_tokens_rf.onnx",
    "feature_dim": 28,
    "window_size": 5
  }
}
```

### 参数说明

| 参数 | 说明 | 目的 | 推荐值 |
|------|------|------|--------|
| `transition_table_path` | DFA状态转移表路径 | 指导SQL分词 | 默认即可 |
| `output_table_path` | DFA输出表路径 | 输出Token映射 | 默认即可 |
| `eod_table_path` | DFA结束状态表路径 | 检测输入结束 | 默认即可 |
| `model_path` | ONNX模型文件路径 | SQL注入判断 | 默认即可 |
| `max_input_length` | 最大输入长度（字节） | 限制输入大小 | 300 |
| `max_output_dim` | 最大输出Token数 | 限制输出大小 | 300 |
| `feature_dim` | 特征向量维度 | 特征提取维度 | 28 |
| `window_size` | 滑动窗口大小 | 特征计算窗口 | 5 |

**注意**：`feature_dim` 与 `window_size`  模型训练时参数保持一致，详见 /opt/KADD/SQLi/tools/model_train。

---

## 前提条件

请参考[安装指南](installation_guide.md)完成安装。

---

## SQL注入检测完整用例

### 用例1：Web应用实时检测

#### 完整代码 web_detection_app.c 

```c
// web_detection_app.c 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kadd_sqli_engine.h"
#include "kadd_errors.h"

int main() {
    // 1. 创建引擎
    kadd_sqli_engine_t *engine = NULL;
    int ret = kadd_sqli_engine_create_from_file(&engine, "/opt/KADD/SQLi/config.json");
    if (ret != KADD_SUCCESS) {
        fprintf(stderr, "引擎创建失败: %d\n", ret);
        return -1;
    }

    // 2. 模拟用户输入检测
    const char *user_inputs[] = {
        "id=1",                          // 正常输入
        "name=John",                     // 正常输入
        "id=1' OR '1'='1",              // SQL注入攻击
        "admin'--",                      // SQL注入攻击
        "SELECT * FROM users"            // SQL注入攻击
    };

    // 3. 逐个检测
    for (int i = 0; i < 5; i++) {
        float probability = 0.0f;
        ret = kadd_sqli_detect(engine, user_inputs[i], &probability);

        if (ret == KADD_SUCCESS) {
            if (probability >= 0.5f) {
                printf("🔴 [攻击] 输入: \"%s\" (概率: %.2f%%)\n",
                       user_inputs[i], probability * 100);
            } else {
                printf("✅ [安全] 输入: \"%s\" (概率: %.2f%%)\n",
                       user_inputs[i], probability * 100);
            }
        }
    }

    // 4. 清理资源
    kadd_sqli_engine_free(engine);
    return 0;
}
```

#### 编译运行

```bash
gcc -o web_detection_app web_detection_app.c \
    -I/opt/KADD/SQLi/include \
    -L/opt/KADD/SQLi/build/src \
    -lkadd_sqli_engine -lkadd_ml_engine -lkadd_feature_engine \
    -lkadd_dfa_engine -lkadd_config_loader
    
# 设置库搜索路径
export LD_LIBRARY_PATH=/opt/KADD/SQLi/build/src:/opt/KADD/SQLi/util/onnxruntime-linux-aarch64-1.16.0/lib:$LD_LIBRARY_PATH
./web_detection_app
```

**输出示例**：

```bash
✅ [安全] 输入: "id=1" (概率: 0.02%)
✅ [安全] 输入: "name=John" (概率: 0.01%)
🔴 [攻击] 输入: "id=1' OR '1'='1" (概率: 98.54%)
🔴 [攻击] 输入: "admin'--" (概率: 95.21%)
🔴 [攻击] 输入: "SELECT * FROM users" (概率: 87.33%)
```

---

### 用例2：批量日志分析

#### 完整代码 log_analysis_app.c

```c
// log_analysis_app.c
#include <stdio.h>
#include "kadd_sqli_engine.h"
#include "kadd_errors.h"

int main() {
    // 1. 创建引擎
    kadd_sqli_engine_t *engine = NULL;
    if (kadd_sqli_engine_create_from_file(&engine, "/opt/KADD/SQLi/config.json") != KADD_SUCCESS) {
        return -1;
    }

    // 2. 批量日志数据
    const char *log_entries[] = {
        "[2024-01-01 10:00:00] GET /api/user?id=123",
        "[2024-01-01 10:00:05] POST /login user=admin&pass=test",
        "[2024-01-01 10:00:10] GET /search?q=cat' OR '1'='1",
        "[2024-01-01 10:00:15] POST /update id=1; DROP TABLE users--"
    };
    int log_count = 4;
    float probabilities[log_count];

    // 3. 批量检测
    int ret = kadd_sqli_detect_batch(engine, log_entries, log_count,
                                      probabilities, 64);

    if (ret >= 0) {
        printf("批量检测结果：\n");
        for (int i = 0; i < log_count; i++) {
            if (probabilities[i] >= 0.5f) {
                printf("🔴 日志%d: 检测到攻击 (%.2f%%)\n", i+1, probabilities[i] * 100);
                printf("   内容: %s\n", log_entries[i]);
            }
        }
    }

    kadd_sqli_engine_free(engine);
    return 0;
}
```

#### 编译运行

```bash
gcc -o log_analysis_app log_analysis_app.c \
    -I/opt/KADD/SQLi/include \
    -L/opt/KADD/SQLi/build/src \
    -lkadd_sqli_engine -lkadd_ml_engine -lkadd_feature_engine \
    -lkadd_dfa_engine -lkadd_config_loader
# 运行前需设置库搜索路径（kadd 动态库 + ONNX Runtime）
export LD_LIBRARY_PATH=/opt/KADD/SQLi/build/src:/opt/KADD/SQLi/util/onnxruntime-linux-aarch64-1.16.0/lib:$LD_LIBRARY_PATH
./log_analysis_app
```

**输出示例**：

```bash
批量检测结果：
🔴 日志3: 检测到攻击 (83.04%)
内容: [2024-01-01 10:00:10] GET /search?q=cat' OR '1'='1
🔴 日志4: 检测到攻击 (79.58%)
内容: [2024-01-01 10:00:15] POST /update id=1; DROP TABLE users--
```
