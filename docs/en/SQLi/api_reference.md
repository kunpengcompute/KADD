# API Reference

## Overview

The KADD SQL injection detection engine provides the following modules:

| Module| Header File| Function|
|------|--------|------|
| SQLi engine| `kadd_sqli_engine.h` | SQL injection detection|
| Deterministic finite automaton (DFA) engine| `kadd_dfa_engine.h` | Character string tokenization|
| Feature engine| `kadd_feature_engine.h` | Sequence-to-feature vector conversion|
| Machine learning (ML) engine| `kadd_ml_engine.h` | ONNX model inference|
| Config | `kadd_config_loader.h` | Configuration loading|

## SQLi Engine APIs

### kadd_sqli_engine_create_from_file

**Function**

Creates an SQL injection detection engine from a configuration file.

**Function API**

```c
int kadd_sqli_engine_create_from_file(kadd_sqli_engine_t **engine, const char *config_file);
```

**Parameters**

* `engine`: pointer to the engine handle pointer.
* `config_file`: path to the JSON configuration file.

**Return Value**

* Status code: `KADD_SUCCESS` indicates that the operation is successful, and a negative value indicates that the operation fails.

---

### kadd_sqli_detect

**Function**

Checks whether a single input is SQL injection.

**Function API**

```c
int kadd_sqli_detect(kadd_sqli_engine_t *engine, const char *input, float *probability);
```

**Parameters**

* `engine`: engine handle.
* `input`: input string.
* `probability`: output probability. The value range is [0, 1]. A larger value indicates a higher probability of SQL injection.

**Return Value**

* Status code: `KADD_SUCCESS` indicates that the operation is successful, and a negative value indicates that the operation fails.

---

### kadd_sqli_detect_batch

**Function**

Batch checks multiple inputs.

**Function API**

```c
int kadd_sqli_detect_batch(kadd_sqli_engine_t *engine, const char **inputs, int count,
                           float *probabilities, int batch_size);
```

**Parameters**

* `engine`: engine handle.
* `inputs`: input string array.
* `count`: number of inputs.
* `probabilities`: output probability array.
* `batch_size`: ML inference batch size (32, 64, or 128 is recommended).

**Return Value**

* Status code: `KADD_SUCCESS` indicates that the operation is successful, and a negative value indicates that the operation fails.

---

### kadd_sqli_engine_free

**Function**

Releases engine resources. Passing `NULL` is safe.

**Function API**

```c
void kadd_sqli_engine_free(kadd_sqli_engine_t *engine);
```

---

## DFA Engine APIs

### kadd_dfa_engine_create

**Function**

Creates a DFA tokenization engine.

**Function API**

```c
int kadd_dfa_engine_create(kadd_dfa_engine_t **engine,
                           const kadd_dfa_table_paths_t *table_paths,
                           size_t max_input_length,
                           size_t max_output_dim);
```

**Parameters**

* `engine`: pointer to the engine handle pointer.
* `table_paths`: structure of the paths to the three DFA tables.
* `max_input_length`: maximum length of the input string.
* `max_output_dim`: maximum number of the output feature dimensions.

**Return Value**

* Status code: `KADD_SUCCESS` indicates that the operation is successful, and a negative value indicates that the operation fails.

---

### kadd_dfa_engine_tokenize

**Function**

Tokenizes the input string.

**Function API**

```c
int kadd_dfa_engine_tokenize(const kadd_dfa_engine_t *engine,
                             const char *input,
                             size_t input_length,
                             int *output,
                             int *token_count);
```

**Parameters**

* `engine`: engine handle.
* `input`: input string.
* `input_length`: length of the input string.
* `output`: output token index array.
* `token_count`: number of output tokens.

**Return Value**

* Status code: `KADD_SUCCESS` indicates that the operation is successful, and a negative value indicates that the operation fails.

---

### kadd_dfa_engine_tokenize_batch

**Function**

Batch tokenizes multiple strings.

**Function API**

```c
int kadd_dfa_engine_tokenize_batch(const kadd_dfa_engine_t *engine,
                                   kadd_dfa_batch_t *batch);
```

**Parameters**

* `engine`: engine handle.
* `batch`: batch processing context structure, including:
  * `inputs`: input string array.
  * `lengths`: length array.
  * `count`: number of inputs.
  * `outputs`: output token array.
  * `dims`: number of tokens for each string.
  * `batch_size`: batch size. (`0` or a negative value indicates that the default value `64` is used.)

**Return Value**

* Status code: `KADD_SUCCESS` indicates that the operation is successful, and a negative value indicates that the operation fails.

**Precautions**

* If any input string fails to be tokenized, an error is returned immediately, and partial success does not occur.

---

### kadd_dfa_engine_free

**Function**

Releases DFA engine resources. Passing `NULL` is safe.

**Function API**

```c
void kadd_dfa_engine_free(kadd_dfa_engine_t *engine);
```

---

## ML Engine APIs

### kadd_model_load

**Function**

Loads an ONNX model.

**Function API**

```c
int kadd_model_load(kadd_model_handle_t **handle, const char *model_path, int feature_dim);
```

**Parameters**

* `handle`: pointer to the model handle pointer.
* `model_path`: path to the ONNX model file.
* `feature_dim`: number of the feature dimensions.

**Return Value**

* Status code: `KADD_SUCCESS` indicates that the operation is successful, and a negative value indicates that the operation fails.

---

### kadd_model_predict

**Function**

Performs single-sample inference.

**Function API**

```c
int kadd_model_predict(kadd_model_handle_t *handle, const float *features, float *probability);
```

**Parameters**

* `handle`: model handle.
* `features`: feature vector.
* `probability`: output probability. The value range is [0, 1].

**Return Value**

* Status code: `KADD_SUCCESS` indicates that the operation is successful, and a negative value indicates that the operation fails.

---

### kadd_model_predict_batch

**Function**

Performs batch inference (block-based processing).

**Function API**

```c
int kadd_model_predict_batch(kadd_model_handle_t *handle, const float *features,
                             int n, float *probabilities, int batch_size);
```

**Parameters**

* `handle`: model handle.
* `features`: feature matrix (*n* × `feature_dim`).
* `n`: number of samples (It is recommended that the value be determined based on the actual memory. The recommended value is less than 10,000,000.)
* `probabilities`: output probability array.
* `batch_size`: batch size. The recommended value is `32`, `64`, or `128`. The value ranges from 1 to 1024.

**Return Value**

* Status code: `KADD_SUCCESS` indicates that the operation is successful, and a negative value indicates that the operation fails.

**Precautions**

* If `batch_size` is greater than 1024, it is automatically limited to `1024`.
* If any batch fails to be processed, `KADD_ERROR_INFERENCE_FAILED` is returned immediately, and partial success does not occur.

---

### kadd_model_free

**Function**

Releases model resources. Passing `NULL` is safe.

**Function API**

```c
void kadd_model_free(kadd_model_handle_t *handle);
```

---

## Feature Engine APIs

### kadd_seq_to_feature

**Function**

Converts a token sequence into a fixed-dimensional feature vector.

**Function API**

```c
int kadd_seq_to_feature(const int *seq, int seq_len, int window_size,
                        int *feature, int feature_dim);
```

**Parameters**

* `seq`: input sequence.
* `seq_len`: sequence length.
* `window_size`: sliding window size.
* `feature`: output feature vector.
* `feature_dim`: number of the output feature dimensions.

**Return Value**

* Status code: `KADD_SUCCESS` indicates that the operation is successful, and a negative value indicates that the operation fails.

---

### kadd_seq_to_feature_batch

**Function**

Batch converts token sequences into feature vectors.

**Function API**

```c
int kadd_seq_to_feature_batch(kadd_feature_batch_t *batch);
```

**Parameters**

* `batch`: batch processing context structure, including:
  * `seqs`: token sequence array.
  * `seq_lens`: sequence length array.
  * `count`: number of sequences.
  * `features`: output feature.
  * `feature_dim`: number of the feature dimensions.
  * `window_size`: sliding window size.
  * `batch_size`: batch size. (`0` or a negative value indicates that the default value `64` is used.)

**Return Value**

* Status code: `KADD_SUCCESS` indicates that the operation is successful, and a negative value indicates that the operation fails.

**Precautions**

* If any sequence fails to be converted, an error is returned immediately, and partial success does not occur.

---

## Configuration Loading APIs

### kadd_config_load

**Function**

Loads all engine configurations from the configuration file.

**Function API**

```c
int kadd_config_load(const char *config_path, kadd_config_t *config);
```

**Parameters**

* `config_path`: configuration file path.
* `config`: output configuration structure.

**Return Value**

* Status code: `KADD_SUCCESS` indicates that the operation is successful, and a negative value indicates that the operation fails.

---

### kadd_config_free

**Function**

Releases configuration resources. Passing `NULL` is safe.

**Function API**

```c
void kadd_config_free(kadd_config_t *config);
```

---

## ⚠️ Important Notes

**For details about the type definitions and complete error code list, see the source header files.**

| Header File| Path|
|--------|------|
| SQLi engine| `/opt/kadd-open/include/kadd_sqli_engine.h` |
| DFA engine| `/opt/kadd-open/include/kadd_dfa_engine.h` |
| Feature engine| `/opt/kadd-open/include/kadd_feature_engine.h` |
| ML engine| `/opt/kadd-open/include/kadd_ml_engine.h` |
| Config | `/opt/kadd-open/include/kadd_config_loader.h` |
| Error codes| `/opt/kadd-open/include/kadd_errors.h` |
