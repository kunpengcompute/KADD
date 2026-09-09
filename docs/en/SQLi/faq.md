# FAQs

This document describes the frequently asked questions (FAQs) and solutions related to the KADD engine.

---

## Installation

### Library File Not Found at Runtime

**Question**

`error while loading shared libraries: libkadd_sqli.so: cannot open shared object file`

**Answer**

Temporary solution:

```bash
export LD_LIBRARY_PATH=/opt/KADD/SQLi/build/src:$LD_LIBRARY_PATH
```

Permanent solution: add the environment variable to `~/.bashrc`. This method is not recommended:

```bash
echo 'export LD_LIBRARY_PATH=/opt/KADD/SQLi/build/src:$LD_LIBRARY_PATH' >> ~/.bashrc
source ~/.bashrc
```

---

## Usage

### How to Set the Detection Threshold

**Question**

How do I adjust the sensitivity of SQL injection detection?

**Answer**

The thresholds mean the following:

1. If the probability is greater than or equal to `0.5f`, the input is determined as SQL injection based on the standard threshold.
2. If the probability is greater than or equal to `0.3f` and less than `0.5f`, the input is determined as a possible SQL injection in strict mode.
3. Other inputs are determined as secure.

```c
float probability = 0.0f;
kadd_sqli_detect(engine, input, &probability);

int alert_level = 0;

if (probability >= 0.5f) {
    alert_level = 2;
} else if (probability >= 0.3f) {
    alert_level = 1;
} else {
    alert_level = 0;
}
```

---

### Using Engine Instances Across Threads

**Question**

Can multiple threads share the same engine instance?

**Answer**

No. Engine instances **are not thread-safe**. Each thread should create an independent engine instance.

Each thread uses the engine in the following sequence:

1. Create an independent engine instance.
2. Use the engine to perform detection.
3. Release resources when the thread ends.

```c
kadd_sqli_engine_t *engine = NULL;
kadd_sqli_engine_create_from_file(&engine, "config.json");

const char *input = "id=1";
float probability = 0.0f;
kadd_sqli_detect(engine, input, &probability);

kadd_sqli_engine_free(engine);
```

---

### Differences Between ML and DL Modes and How to Choose

**Question**

What are the differences between setting `engine_mode` to `ml` and `dl` in `config.json`?

**Answer**

| Item                 | ML Mode                                          | DL Mode                              |
| -------------------- | ------------------------------------------------ | ------------------------------------ |
| Model type           | Random forest                                    | Convolutional neural network (TextCNN) |
| Input features       | 28-dimensional handcrafted features (sliding window statistics) | 300-dimensional token ID sequence |
| Applicable scenario  | Low-latency and resource-constrained environments | Scenarios requiring higher detection accuracy |

**Configuration switching**: Modify the `engine_mode` field in `config.json`, that is, set the field to `ml` or `dl`.

```json
{
  "engine_mode": "ml"
}
```

No recompilation is required. The setting takes effect the next time an engine is created.

**Model loading strategy**: Both modes use the same strategy. When SVE is available, the native binary formats (`.krf` / `.kcn`) are loaded first. If loading fails, the system automatically falls back to ONNX Runtime to load the `.onnx` format.

---

### How SVE Acceleration Works

**Question**

`-DENABLE_ARM_SVE=ON` is enabled during compilation, but a message indicating that SVE is unavailable is displayed at runtime.

**Answer**

Both the ML and DL engines of KADD use the **automatic fallback** strategy:

1. The SVE compilation path is enabled through `-DENABLE_ARM_SVE=ON` (enabled by default) during compilation.
2. SVE availability is checked at runtime (through the macros in `kadd_sve.h`).
3. When SVE is available, native binary models (`.krf` / `.kcn`) are loaded and accelerated by SVE instructions.
4. When SVE is unavailable, the system automatically falls back to ONNX Runtime to load `.onnx` models.
