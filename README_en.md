# KADD

<!-- md-trans-meta sourceCommit=bd143b7dd3a8d6940dbfa762540fd554e881848e translatedAt=2026-09-22T04:46:45.625Z pushedAt=2026-09-24T02:41:21.159Z -->

[![License](https://img.shields.io/badge/license-BSD%203--Clause-blue)](LICENSE)

## Latest Updates

- [2026-09-30] : Released KADD 1.1.0: Added the PI feature; updated the SQLi feature by adding the DL (CNN) inference mode and supporting ML/DL dual-mode switching.
- [2026-06-30] : Released KADD SQL injection detection library 1.0.0. It is a hybrid detection solution based on the deterministic finite automaton (DFA) and machine learning (ML). It provides high-performance detection for SQL injection attacks and supports text feature extraction and batch inference optimization.

## Project Introduction

KADD is a Kunpeng intelligent data analytics (IDA) kit. It provides multiple security detection capabilities, and currently supports the following features:

- **PI feature**: Adopts a hybrid detection architecture that combines rules and deep learning to achieve high-accuracy, low-latency detection of jailbreak text, and provides Python APIs.
- **SQL injection detection**: Adopts a hybrid detection architecture that combines the DFA with ML/DL dual engines. The DFA engine performs precise SQL statement tokenization, and the ML engine or DL (CNN) engine performs intelligent feature inference, achieving high-accuracy, low-latency detection of SQL injection attacks.

The solution is optimized for Kunpeng processor features. While ensuring detection accuracy, it provides concise C APIs for easy integration into existing security protection systems.

## Feature Description

| Feature | Description |
|----------|----------|
| [PI feature](#pi-feature) | Adopts a hybrid detection architecture that combines rules and deep learning (LERT model). Through three-level filtering of trustlists/blocklists, regular expression rules, and ONNX model inference, it achieves high-performance jailbreak text detection. It provides Python APIs. |
| [SQL injection detection](#sql-injection-detection-feature) | Provides precise SQL statement tokenization based on the DFA, 28-dimensional feature vector extraction, and ML/DL dual-mode inference, achieving high-performance SQL injection attack detection. |

## Directory Structure

The project directory structure is as follows:

```text
|—— build.sh                  # Project build script
|—— config.json               # Project configuration file
|—— LICENSE                   # Project main license
|—— README.md                 # Description document
|—— docs/                     # Documentation directory
│   ├── LICENSE               # Documentation license
│   ├── en/                   # English documentation directory
│   │   ├── PI/               # PI feature documentation
│   │   │   ├── api_reference.md
│   │   │   ├── faq.md
│   │   │   ├── installation_guide.md
│   │   │   ├── quick_start.md
│   │   │   ├── release_notes.md
│   │   │   └── user_guide.md
│   │   └── SQLi/             # SQL injection detection feature documentation
│   │       ├── api_reference.md
│   │       ├── faq.md
│   │       ├── installation_guide.md
│   │       ├── quick_start.md
│   │       ├── release_notes.md
│   │       └── user_guide.md
│   ├── figures/              # Image resources
│   │   └── SQLi/
│   ├── public_sys-resources/ # Documentation icon resources
│   └── zh/                   # Chinese documentation directory
│       ├── menu_kadd.md      # Common menu
│       ├── release_notes.md  # Common release notes
│       ├── PI/               # PI feature documentation
│       │   ├── api_reference.md
│       │   ├── faq.md
│       │   ├── installation_guide.md
│       │   ├── quick_start.md
│       │   └── user_guide.md
│       └── SQLi/             # SQL injection detection feature documentation
│           ├── api_reference.md
│           ├── faq.md
│           ├── installation_guide.md
│           ├── quick_start.md
│           └── user_guide.md
|—— PI/                       # PI feature code
│   └── THIRD_PARTY_LICENSES  # PI feature third-party licenses
|—— SQLi/                     # SQL injection detection feature code
│   └── THIRD_PARTY_LICENSES  # SQL injection detection feature third-party licenses
```

## PI Feature

### Feature Introduction

KADD Prompt Injection (PI) adopts a hybrid architecture that combines rules and deep learning for jailbreak text detection. The detection process consists of three-level filtering: trustlist/blocklist filtering (based on MinHash LSH approximate matching), regular expression rule filtering (common jailbreak pattern matching), and model classification filtering (ONNX inference based on the LERT model), achieving high-accuracy, low-latency detection of jailbreak text. The solution is optimized for Kunpeng processor features. While ensuring detection accuracy, it provides concise Python APIs for easy integration into existing security protection systems.

### Release Notes

The KADD release notes include software version mapping, software package downloads, and feature update descriptions for each version. For details, see [Release Notes](docs/en/release_notes.md).

### Quick Start

The quick start for the PI feature includes quick installation, data preparation, and tool usage. For details, see [Quick Start](docs/en/PI/quick_start.md).

### Documents

This feature provides the following learning documents for reference.

| Document | Introduction |
| -- | -- |
| [Release Notes](docs/en/release_notes.md) | Provides version update descriptions for KADD and its PI and SQLi features. |
| [Installation Guide](docs/en/PI/installation_guide.md) | Provides the environment deployment, dependency installation, and acceleration library compilation methods for the KADD PI feature. |
| [Quick Start](docs/en/PI/quick_start.md) | Provides the quick usage method for the KADD PI feature demos. |
| [API Reference](docs/en/PI/api_reference.md) | Provides the complete API descriptions for the initialization, detection, and trustlist/blocklist update APIs of the KADD PI feature. |
| [User Guide](docs/en/PI/user_guide.md) | Provides the configuration description, detection principles, and code examples for the KADD PI feature. |
| [FAQs](docs/en/PI/faq.md) | Lists some frequently asked questions (FAQs) about the KADD PI feature. |

## SQL Injection Detection Feature

### Feature Introduction

The KADD SQL injection detection solution adopts a hybrid detection architecture that combines the DFA with ML/DL dual engines. The DFA engine is used for precise SQL statement tokenization, and the ML engine or the DL (CNN) engine is used for intelligent feature inference. The solution achieves high-accuracy, low-latency detection of SQL injection attacks. It is optimized for Kunpeng processor features. While ensuring detection accuracy, it provides concise C APIs for easy integration into existing security protection systems.

### Release Notes

The KADD release notes include software version mapping, software package downloads, and feature update descriptions for each version. For details, see [Release Notes](docs/en/release_notes.md).

### Quick Start

The quick start for the SQL injection detection feature includes quick installation, data preparation, and tool usage. For details, see [Quick Start](docs/en/SQLi/quick_start.md).

### Documents

This feature provides the following learning documents for reference.

| Document | Introduction |
| -- | -- |
| [Release Notes](docs/en/release_notes.md) | Provides version update descriptions for KADD and its PI and SQLi features. |
| [Installation Guide](docs/en/SQLi/installation_guide.md) | Provides the environment deployment, dependency installation, and compilation methods for the KADD SQL injection detection feature. |
| [Quick Start](docs/en/SQLi/quick_start.md) | Provides quick usage methods for the demos of the KADD SQL injection detection feature. |
| [API Reference](docs/en/SQLi/api_reference.md) | Provides complete API descriptions for the DFA engine, feature engine, ML engine, DL engine, and SQLi engine of the KADD SQL injection detection feature. |
| [User Guide](docs/en/SQLi/user_guide.md) | Provides usage and practices for the KADD SQL injection detection feature. |
| [FAQs](docs/en/SQLi/faq.md) | Lists some FAQs about the KADD SQL injection detection feature. |

## Contribution Statement

If you have any questions or want to provide feedback on feature requirements and bug reports, you can submit issues. For details, see the [contribution guideline](https://gitcode.com/boostkit/community/blob/master/docs/contributor/contributing.md).

## Disclaimer

To KADD users

- This project provides only reference examples and tools. You are responsible for any risks and should carefully review the following information:
    - Data processing and deletion: Users are responsible for managing and deleting any data generated while using this tool. Users are advised to delete such data promptly after use to prevent information leakage.
    - Data confidentiality and transmission: Users understand and agree not to share or transmit any data generated by this tool without authorization. Neither the tool nor its developers are responsible for any information leaks, data breaches, or other negative consequences.
    - User input security: Users are responsible for the security of any commands they enter and for any risks or losses resulting from improper input. The tool and its developers are not liable for issues caused by incorrect command usage.
- Disclaimer scope: This disclaimer applies to all individuals and entities using this tool. It is not recommended that this tool be used for any commercial purposes. By using the tool, you acknowledge and accept this statement and assume all risks and responsibilities arising from its use. If you do not agree, please stop using this tool immediately.
- Before using this tool, please carefully read and understand the preceding disclaimer. If you have any questions, contact the developer.

## License

This project is licensed under **BSD 3-Clause** License. For details, see [LICENSE](LICENSE).

The documents of this project are licensed under **CC-BY 4.0**. For details, see [LICENSE](docs/LICENSE).



## Third-Party License

The third-party libraries used by each feature module and their licenses are described in the `THIRD_PARTY_LICENSES` file in the corresponding module directory:

- **PI feature**: See [PI/THIRD_PARTY_LICENSES](PI/THIRD_PARTY_LICENSES).
- **SQL injection detection feature**: See [SQLi/THIRD_PARTY_LICENSES](SQLi/THIRD_PARTY_LICENSES).
