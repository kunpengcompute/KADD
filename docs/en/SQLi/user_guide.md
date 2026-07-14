# User Guide

This document describes how to configure the KADD SQL injection detection engine and provides code examples.

---

## Configuration File

KADD modules are configured using `config.json`.

```json
{
  "kadd_dfa_engine": {
    "transition_table_path": "/opt/kadd-open/assets/data/transition_table.bin",
    "output_table_path": "/opt/kadd-open/assets/data/output_table.bin",
    "eod_table_path": "/opt/kadd-open/assets/data/eod_table.bin",
    "max_input_length": 300,
    "max_output_dim": 300
  },
  "kadd_ml_engine": {
    "model_path": "/opt/kadd-open/assets/model/model_tokens_rf.onnx",
    "feature_dim": 28,
    "window_size": 5
  },
  "kadd_sqli_engine": {
    "transition_table_path": "/opt/kadd-open/assets/data/transition_table.bin",
    "output_table_path": "/opt/kadd-open/assets/data/output_table.bin",
    "eod_table_path": "/opt/kadd-open/assets/data/eod_table.bin",
    "ml_model_path": "/opt/kadd-open/assets/model/model_tokens_rf.onnx",
    "feature_dim": 28,
    "window_size": 5,
    "max_input_length": 300,
    "max_output_dim": 300
  },
  "kadd_feature_engine": {
    "ml_model_path": "/opt/kadd-open/assets/model/model_tokens_rf.onnx",
    "feature_dim": 28,
    "window_size": 5
  }
}
```

### Parameter Description

| Parameter| Description| Purpose| Recommended Value|
|------|------|------|--------|
| `transition_table_path` | Path to the DFA state transition table| Guides SQL tokenization.| Retain the default value.|
| `output_table_path` | Path to the DFA output table| Outputs token mapping.| Retain the default value.|
| `eod_table_path` | Path to the DFA end state table| Detects the end of input.| Retain the default value.|
| `model_path` | ONNX model file path| Determines SQL injection.| Retain the default value.|
| `max_input_length` | Maximum input length (bytes)| Limits the input size.| <code>300</code> |
| `max_output_dim` | Maximum number of output tokens| Limits the output size.| <code>300</code> |
| `feature_dim` | Number of feature vector dimensions| Specifies the number of extracted feature dimensions.| <code>28</code>|
| `window_size` | Sliding window size| Specifies the feature calculation window.| <code>5</code> |

**NOTE**: `feature_dim` and `window_size` used in model usage must be consistent with those used in model training. For details, see `/opt/kadd-open/tools/model_train`.

---

## Prerequisites

You have completed installation by following instructions in [Installation Guide](installation_guide.md).

---

## SQL Injection Detection Cases

### Case 1: Real-Time Detection for Web Applications

#### Complete code: `web_detection_app.c`

```c
// web_detection_app.c 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kadd_sqli_engine.h"
#include "kadd_errors.h"

int main() {
    // 1. Create an engine.
    kadd_sqli_engine_t *engine = NULL;
    int ret = kadd_sqli_engine_create_from_file(&engine, "/opt/kadd-open/config.json");
    if (ret != KADD_SUCCESS) {
        fprintf(stderr, "Failed to create the engine: %d\n", ret);
        return -1;
    }

    // 2. Simulate user input detection.
    const char *user_inputs[] = {
        "id=1",                          // Normal input
        "name=John",                     // Normal input
        "id=1' OR '1'='1",              // SQL injection attack
        "admin'--",                      // SQL injection attack
        "SELECT * FROM users"            // SQL injection attack
    };

    // 3. Check one by one.
    for (int i = 0; i < 5; i++) {
        float probability = 0.0f;
        ret = kadd_sqli_detect(engine, user_inputs[i], &probability);

        if (ret == KADD_SUCCESS) {
            if (probability >= 0.5f) {
                printf("🔴 [Attack] Input: \"%s\" (Probability: %.2f%%)\n",
                       user_inputs[i], probability * 100);
            } else {
                printf("✅ [Secure] Input: \"%s\" (Probability: %.2f%%)\n",
                       user_inputs[i], probability * 100);
            }
        }
    }

    // 4. Clear resources.
    kadd_sqli_engine_free(engine);
    return 0;
}
```

#### Compilation and Running

```bash
gcc -o web_detection_app web_detection_app.c \
    -I/opt/kadd-open/include \
    -L/opt/kadd-open/lib \
    -lkadd_sqli_engine -lkadd_ml_engine -lkadd_feature_engine \
    -lkadd_dfa_engine -lkadd_config_loader
    
./web_detection_app
```

**Output example:**

```bash
✅ [Secure] Input: "id=1" (Probability: 0.02%)
✅ [Secure] Input: "name=John" (Probability: 0.01%)
🔴 [Attack] Input: "id=1' OR '1'='1" (Probability: 98.54%)
🔴 [Attack] Input: "admin'--" (Probability: 95.21%)
🔴 [Attack] Input: "SELECT * FROM users" (Probability: 87.33%)
```

---

### Case 2: Batch Log Analysis

#### Complete code: `log_analysis_app.c`

```c
// log_analysis_app.c
#include <stdio.h>
#include "kadd_sqli_engine.h"
#include "kadd_errors.h"

int main() {
    // 1. Create an engine.
    kadd_sqli_engine_t *engine = NULL;
    if (kadd_sqli_engine_create_from_file(&engine, "/opt/kadd-open/config.json") != KADD_SUCCESS) {
        return -1;
    }

    // 2. Batch log data
    const char *log_entries[] = {
        "[2024-01-01 10:00:00] GET /api/user?id=123",
        "[2024-01-01 10:00:05] POST /login user=admin&pass=test",
        "[2024-01-01 10:00:10] GET /search?q=cat' OR '1'='1",
        "[2024-01-01 10:00:15] POST /update id=1; DROP TABLE users--"
    };
    int log_count = 4;
    float probabilities[log_count];

    // 3. Batch detection
    int ret = kadd_sqli_detect_batch(engine, log_entries, log_count,
                                      probabilities, 64);

    if (ret >= 0) {
        printf("Batch detection result:\n");
        for (int i = 0; i < log_count; i++) {
            if (probabilities[i] >= 0.5f) {
                printf("🔴 Log %d: Attack detected (%.2f%%)\n", i+1, probabilities[i] * 100);
                printf("Content: %s\n", log_entries[i]);
            }
        }
    }

    kadd_sqli_engine_free(engine);
    return 0;
}
```

#### Compilation and Running

```bash
gcc -o log_analysis_app log_analysis_app.c \
    -I/opt/kadd-open/include \
    -L/opt/kadd-open/lib \
    -lkadd_sqli_engine -lkadd_ml_engine -lkadd_feature_engine \
    -lkadd_dfa_engine -lkadd_config_loader
./log_analysis_app
```

**Output example:**

```bash
Batch detection result:
🔴 Log 3: Attack detected (83.04%)
Content: [2024-01-01 10:00:10] GET /search?q=cat' OR '1'='1
🔴 Log 4: Attack detected (79.58%)
Content: [2024-01-01 10:00:15] POST /update id=1; DROP TABLE users--
```
