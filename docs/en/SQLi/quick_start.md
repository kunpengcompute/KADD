# Quick Start

This document provides a quick start guide for KADD. It provides a tool set and demo examples. By running the demo programs, you can quickly experience the SQL injection detection function. By using the tools, you can generate the deterministic finite automaton (DFA) tables and train models. For details, see [User Guide](user_guide.md).

## Prerequisites

You have completed installation by following instructions in [Installation Guide](installation_guide.md).

---

## Demo Program Usage

KADD provides five demo programs to demonstrate how to use each engine.

### Running the Demo Programs

Before running the demo programs, set the library search path.

```bash
export LD_LIBRARY_PATH=/opt/kadd-open/lib:$LD_LIBRARY_PATH
```

#### 1. SQL Injection Detection - kadd_sqli_demo

This demo provides a complete demonstration process for SQL injection detection.

```bash
cd /opt/kadd-open/demo
./kadd_sqli_demo
```

**Output example:**

```bash
=== KADD SQL injection detection engine demonstration ===

Test 1: normal input
Input: id=1
Result: secure input (Probability: 0.02%)

Test 2: SQL injection attack
Input: id=1' OR '1'='1
Result: SQL injection detected! (Probability: 98.54%)

Demonstration completed!
```

#### 2. DFA Tokenization Engine - kadd_dfa_demo

This demo tokenizes a string into a token index sequence.

```bash
cd /opt/kadd-open/demo
./kadd_dfa_demo
```

**Output example:**

```bash
Input: SELECT * FROM users WHERE id = 1

Tokenization result (8 tokens):
  [1] Token index: 7
  [2] Token index: 11
  [3] Token index: 8
  ...
```

#### 3. ML Inference Engine - kadd_ml_demo

This demo uses the ONNX model for inference.

```bash
cd /opt/kadd-open/demo
./kadd_ml_demo
```

**Output example:**

```bash
Input feature: [0.10 0.10 0.10 0.10 0.10 ...]
Injection probability: 8.45%
✅ Inference succeeded.
```

#### 4. Feature Extraction Engine - kadd_feature_demo

This demo converts a token ID sequence into a feature vector.

```bash
cd /opt/kadd-open/demo
./kadd_feature_demo
```

**Output example:**

```bash
Token sequence (length: 7):
  [1] Token ID: 7
  [2] Token ID: 11
  ...

Feature vector (28 dimensions):
[12 8 15 20 11 ...]
✅ Feature extraction succeeded.
```

#### 5. Configuration Loader - kadd_config_demo

This demo loads the configuration from a JSON file.

```bash
cd /opt/kadd-open/demo
./kadd_config_demo
```

**Output example:**

```bash
DFA engine:
  Transition table path: assets/data/transition_table.bin
  Maximum input length: 512

ML engine:
  Model path: assets/model/model_tokens_rf.onnx
  Feature dimension: 28
```

## DFA Table Compilation

Use the DFA compiler to compile the SQL pattern rules into a DFA state table. Run the following commands only when you need to modify the SQL pattern rules.

```bash
cd /opt/kadd-open/tools/dfa_compile
python dfa_compiler.py profile_sqli_fixed.txt
```

The following command output is displayed:

```bash
================================================================================
DFA compiler
================================================================================

[1/5] Loading the DFA file...
      8023 states loaded.

[2/5] Parsing transition rules...

[3/5] Parsing special instructions...
      39941 special instructions are expanded, 1641 synthetic states are added, and 2 common end states are added.

[4/5] Building the transition table...

[5/5] Saving the compilation result...
      Transition table: /opt/kadd-open/assets/data/transition_table.bin
      Output table: /opt/kadd-open/assets/data/output_table.bin
      EOD table: /opt/kadd-open/assets/data/eod_table.bin

================================================================================
Compilation completed!
  Number of states: 10100
  Transition table size: 10100 x 256
  Output directory: /opt/kadd-open/assets/data

Generated files:
  - transition_table.bin    Binary transition table
  - output_table.bin        Boundary output table
  - eod_table.bin           EOD output table
================================================================================
```

**Output files:**

- `transition_table.bin`: DFA state transition table
- `output_table.bin`: DFA output table
- `eod_table.bin`: DFA end detection table

Copy the generated table files to the `/opt/kadd-open/assets/data/` directory to replace the original files.

---

## Model Training

Use the training tool to train the random forest classifier and export the ONNX model.

### Step 1: Generating Training Vectors

`csv_to_vector_tool` converts the text content into feature vectors.

**Feature Generation Tool Compilation**

```bash
cd /opt/kadd-open/tools/model_train/train_data
export LD_LIBRARY_PATH=/opt/kadd-open/lib:$LD_LIBRARY_PATH
mkdir build && cd build
cmake ..
make
```

Run the tool to generate 28-dimensional feature vectors:

- Run the following command in the build directory:

```bash
./csv_to_vector_tool
```

- Specify parameters in sequence, including the original SQL injection text, the destination addresses of the converted vectors, and the address of the DFA feature configuration file.

```bash
./csv_to_vector_tool \
    ../total_training_dataset_v4_pinnacle.csv \
    ../training_vectors.csv \
    /opt/kadd-open/config.json
```

**Output format:**

```bash
========== Statistics ==========
Total number of rows
Success:
Failure:
Label distribution: Label 0 = , Label 1 =
Output file:
==============================
```

```csv
Query,feature_0,feature_1,...,feature_27,Label
"SELECT * FROM users",0,5,2,...,1,0
"1' OR '1'='1",1,3,8,...,0,1
```

### Step 2: Training a Model

Use the Python script to train the model.

- Use the default CSV path.

```bash
cd /opt/kadd-open/tools/model_train
python model_train.py
```

- Specify the vector CSV path.

```bash
cd /opt/kadd-open/tools/model_train
python model_train.py --csv /opt/kadd-open/tools/model_train/train_data/training_vectors.csv
```

**Training output:**

- `model_tokens_rf.onnx`: model file in ONNX format
- Evaluation metrics such as accuracy, classification report, and confusion matrix

```bash
Data source: /opt/kadd-open/tools/model_train/train_data/training_vectors.csv (vector CSV)
Vector CSV encoding: latin-1
Vector data: 44074 samples; feature dimensions: 28; label distribution: 0=23012, 1=21062
X=(44074, 28), y=(44074,), 0=23012, 1=21062
Train: 35259, Test: 8815, Verify: 100
X_train: (35259, 28), range: 0.0–9789.0, mean: 55.62
y_train: Counter({np.int32(0): 18409, np.int32(1): 16850})
============================================================
Random forest model training...
  Train Accuracy: 
  Test Accuracy: 
  Verify Accuracy: 

Classification report (test):
              precision    recall  f1-score   support

     Label 0       
     Label 1       

    accuracy       
   macro avg       
weighted avg       

Confusion matrix:
[[4596    7]
 [ 399 3813]]
```

---
