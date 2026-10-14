# Release Notes

<!-- md-trans-meta sourceCommit=fe1b285cc2871a7cc7ccc5a933b8404d18154f2f translatedAt=2026-09-22T03:49:14.754Z pushedAt=2026-09-24T01:32:19.093Z -->

## Change History

| Version | Date | Description |
| --- | --- | --- |
| 1.1.0 | 2026-09-30 | Added the PI feature (jailbreak text detection), the deep learning (DL) inference mode for SQLi, and other updates. |
| 1.0.0 | 2026-06-30 | First official release of KADD-SQLi. Provided a hybrid detection solution based on the deterministic finite automaton (DFA) and machine learning (ML) to enable high-performance detection of SQL injection attacks. |

## Version Mapping

### Product Version Information

| Item | Content |
| --- | --- |
| Product Name | Kunpeng BoostKit |
| Product Version | 26.2.RC1 |
| Software Name | KADD |
| Software Version | 1.1.0 |

### OS, Compiler, and CPU

#### PI Feature

| OS | CPU | Python Version |
| --- | --- | --- |
| openEuler 24.03 LTS SP2 | Kunpeng 920 processor | 3.9 |
| openEuler 24.03 LTS SP2 | New Kunpeng 920 processor model | 3.9 |
| openEuler 24.03 LTS SP2 | Kunpeng 950 processor | 3.10 |

#### SQLi Feature

| OS | CPU | Compiler |
| --- | --- | --- |
| openEuler 24.03 LTS SP2 | New Kunpeng 920 processor model | GCC 10.3.1 |

## v1.1.0

### Change Description

**New Features**

| Feature | Description |
| --- | --- |
| Jailbreak text detection | A hybrid detection solution based on the LERT model and rules, which supports three-level detection with dynamic blocklist/trustlist updates, regular expression rule matching, and ONNX model inference, and provides Python APIs. |

**Updated Features**

| Feature | Description |
| --- | --- |
| DL inference mode | A CNN deep learning inference mode is added. The SQLi engine supports ML/DL dual-mode switching, selected through the `engine_mode` configuration. |
| Native SVE + ONNX fallback | The ML/DL engines uniformly adopt a loading strategy that prioritizes native SVE binaries and automatically falls back to ONNX Runtime, requiring no manual switching. |
| Unified ONNX inference engine | The `kadd_inference_engine` module is added, which encapsulates the ONNX Runtime API for shared use by the ML/DL engines. |
| Configuration file optimization | `config.json` is changed to a flat structure, simplifying configuration items and supporting dual-path configuration for native models and ONNX models. |

### Performance Metrics

| Metric Item | Metric Value |
| --- | --- |
| SQL injection detection accuracy (ML/RF mode) | 99.92% (measured on the standard test set, meeting the ≥ 99.9% requirement) |
| SQL injection detection accuracy (DL/CNN mode) | 99.92% (measured on the standard test set, meeting the ≥ 99.9% requirement) |

![note icon](../public_sys-resources/icon-note.gif) **NOTE:**

- The accuracy is measured on the standard test set `SQLi/tools/model_train/train_data/verify_data.csv`: the `train_data` directory stores both training data and test data; `verify_data.csv` is the test set randomly split from the full dataset, containing 57,701 samples (36,907 normal samples and 20,794 SQL injection samples). It is strictly isolated from the training set and does not participate in model training.
- The above accuracy conclusions hold for both the SVE native backend and the ONNX backend.

## v1.0.0

### Change Description

**New Features**

| Feature | Description |
| --- | --- |
| SQL injection detection | First official release of KADD-SQLi. Provided a hybrid detection solution based on the DFA and ML to enable high-performance detection of SQL injection attacks. |

## Related Documentation

| Document | Description | Delivery Form |
| --- | --- | --- |
| 1.1.0 Release Notes | Provides version update descriptions for KADD and its PI and SQLi features. | Open-source repository |
| PI Feature Quick Start | Provides quick usage methods for the demos of the KADD PI feature. | Open-source repository |
| PI Feature Installation Guide | Provides environment deployment and dependency package installation methods for the KADD PI feature. | Open-source repository |
| PI Feature User Guide | Provides usage and practices for the KADD PI feature. | Open-source repository |
| PI Feature API Reference | Provides API descriptions for the KADD PI feature. | Open-source repository |
| PI Feature FAQs | Lists some frequently asked questions (FAQs) about the KADD PI feature. | Open-source repository |
| SQLi Feature Quick Start | Provides quick usage methods for the demos of the KADD SQLi feature. | Open-source repository |
| SQLi Feature Installation Guide | Provides dependency package installation and compilation methods for the demos of the KADD SQLi feature. | Open-source repository |
| SQLi Feature User Guide | Provides detailed usage for the demos of the KADD SQLi feature. | Open-source repository |
| SQLi Feature API Reference | Provides API descriptions for feature extraction and feature inference of the KADD SQLi feature. | Open-source repository |
| SQLi Feature FAQs | Lists some FAQs about the KADD SQLi feature. | Open-source repository |

**Obtaining Documentation**

Visit the [open-source repository](https://gitcode.com/boostkit/kadd-open/tree/master) to view or download the required documents.
