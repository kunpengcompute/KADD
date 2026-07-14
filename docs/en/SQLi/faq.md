# FAQs

This document describes the frequently asked questions (FAQs) and solutions related to the KADD engine.

---

## Installation

### Q1: What Should I Do If a Message Is Displayed Indicating That the Library File Cannot Be Found During Runtime?

**Question**: `error while loading shared libraries: libkadd_sqli.so: cannot open shared object file`

**Solution**:

- Temporary solution

```bash
export LD_LIBRARY_PATH=/path/to/kadd-open/lib:$LD_LIBRARY_PATH
```

- Permanent solution (adding the environment variable to `~/.bashrc`) - not recommended

```bash
echo 'export LD_LIBRARY_PATH=/path/to/kadd-open/lib:$LD_LIBRARY_PATH' >> ~/.bashrc
source ~/.bashrc
```

---

## Usage

### Q2: How Do I Set the Detection Threshold?

**Question**: How do I adjust the sensitivity of SQL injection detection?

**Solution**:

```c
float probability = 0.0f;
kadd_sqli_detect(engine, input, &probability);

// Select a threshold based on the scenario.
if (probability >= 0.5f) {      // Standard threshold
    // Injection is detected.
} else if (probability >= 0.3f) {  // Strict mode
    // Injection may occur.
} else {
    // Secure input
}

```

### Q3: Can an Engine Instance Be Used Across Threads?

**Question**: Can multiple threads share the same engine instance?

**Solution**:

No. Engine instances **are not thread-safe**. Each thread should create an independent engine instance.

```c
// Each thread creates an independent instance.
kadd_sqli_engine_t *engine = NULL;
kadd_sqli_engine_create_from_file(&engine, "config.json");

// Use the engine...

// Clear the engine when the thread ends.
kadd_sqli_engine_free(engine);
```
