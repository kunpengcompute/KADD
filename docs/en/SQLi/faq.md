# FAQs

<!-- md-trans-meta sourceCommit=f4dd8fc56e783ad29ae1a42afe93c56f6c379e83 translatedAt=2026-09-22T04:30:15.244Z pushedAt=2026-09-23T03:27:50.817Z -->

This document describes the frequently asked questions (FAQs) and solutions related to the KADD engine.



## Installation

### Library Files Are Not Found at Runtime

**Question**

`error while loading shared libraries: libkadd_sqli.so: cannot open shared object file`

**Answer**

Use a temporary solution:

```bash
export LD_LIBRARY_PATH=/opt/KADD/SQLi/build/src:$LD_LIBRARY_PATH
```

Use a permanent solution by adding the environment variable to `~/.bashrc`. This approach is not recommended.

```bash
echo 'export LD_LIBRARY_PATH=/opt/KADD/SQLi/build/src:$LD_LIBRARY_PATH' >> ~/.bashrc
source ~/.bashrc
```



## Usage

### How to Set the Detection Threshold

**Question**

How do I adjust the sensitivity of SQL injection detection?

**Answer**

The threshold meanings are as follows:

1. When the probability is greater than or equal to `0.5f`, the input is determined as a SQL injection according to the standard threshold.
2. When the probability is greater than or equal to `0.3f` and less than `0.5f`, the input is determined as a possible SQL injection in strict mode.
3. In other cases, the input is determined as a safe input.

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



### Cross-Thread Engine Instance Usage

**Question**

Can multiple threads share the same engine instance?

**Answer**

No. Engine instances **are not thread-safe**. Each thread should create an independent engine instance.

Each thread uses the engine in the following order:

1. Create an independent engine instance.
2. Use the engine to perform detection.
3. Clean up resources when the thread ends.

```c
kadd_sqli_engine_t *engine = NULL;
kadd_sqli_engine_create_from_file(&engine, "config.json");

const char *input = "id=1";
float probability = 0.0f;
kadd_sqli_detect(engine, input, &probability);

kadd_sqli_engine_free(engine);
```



### Differences and Selection Between ML Mode and DL Mode

**Question**

What is the difference between setting `engine_mode` to `ml` and `dl` in `config.json`?

**Answer**

| Comparison Item | ML Mode | DL Mode |
|--------|--------|--------|
| Model type | Random Forest | Convolutional Neural Network (TextCNN) |
| Input features | 28-dimensional handcrafted features (sliding window statistics) | 300-dimensional token ID sequence |
| Applicable scenarios | Low latency and resource-constrained environments | Pursuing higher detection accuracy |

**Configuration switching**: Modify the `engine_mode` field in `config.json`:
Set this field to `ml` or `dl`.

```json
{
  "engine_mode": "ml"
}
```

No recompilation is required; it takes effect the next time the engine is created.

**Model loading strategy**: Both modes use the same strategy. When SVE is available, the native binary format (`.krf`/`.kcn`) is loaded first; if that fails, it automatically falls back to ONNX Runtime to load the `.onnx` format.



### SVE Acceleration Working Principle

**Question**

Despite compilation with `-DENABLE_ARM_SVE=ON`, the runtime reports that SVE is unavailable.

**Answer**

Both the ML and DL engines of KADD adopt an **automatic fallback** strategy:

1. Enable the SVE compilation path during compilation via `-DENABLE_ARM_SVE=ON` (enabled by default).
2. Detect the SVE availability at runtime (determined by the macros in `kadd_sve.h`).
3. When SVE is available, load the native binary model (`.krf`/`.kcn`) for acceleration with SVE instructions.
4. When SVE is unavailable, automatically fall back to ONNX Runtime to load the `.onnx` model.
