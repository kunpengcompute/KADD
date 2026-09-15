# KADD

[![License](https://img.shields.io/badge/license-BSD%203--Clause-blue)](LICENSE)

## 最新消息

- [2026-09-30] : 发布KADD 1.1.0版本。新增PI特性；更新SQLi特性，新增DL（CNN）推理模式并支持ML/DL双模式切换。
- [2026-06-30] : 发布KADD SQL注入检测库 1.0.0版本。基于DFA状态机+机器学习的混合检测方案，提供高性能SQL注入攻击检测能力，支持文本特征提取和批量推理优化。

## 项目介绍

KADD是鲲鹏智能数据分析套件，提供多种安全检测能力。当前支持以下特性：

- **PI特性**：采用规则和深度学习相结合的混合检测架构，实现对越狱文本的高精度、低延迟检测，提供python语言的API接口
- **SQL注入检测**：采用DFA状态机与ML/DL双引擎相结合的混合检测架构，通过DFA引擎进行精确的SQL语句分词，结合ML引擎或DL（CNN）引擎进行智能特征推理，实现对SQL注入攻击的高精度、低延迟检测

方案针对鲲鹏处理器特性进行优化，在保证检测准确率的同时，提供简洁的C语言API接口，易于集成到现有安全防护系统中。

## 特性介绍

| 特性名称 | 特性介绍 |
|----------|----------|
| [PI特性](#pi特性) | 采用规则与深度学习（LERT模型）相结合的混合检测架构，通过黑白名单、正则规则和ONNX模型推理三级过滤，实现高性能越狱文本检测能力，提供Python语言API接口。 |
| [SQL注入检测](#sql注入检测特性) | 提供基于DFA状态机的SQL语句精确分词、28维特征向量提取、ML/DL双模式推理，实现高性能SQL注入攻击检测能力。 |

## 目录结构

项目目录结构如下：

```text
|—— build.sh                  # 项目构建脚本
|—— config.json               # 项目配置文件
|—— LICENSE                   # 项目主许可证
|—— README.md                 # 说明文档
|—— docs/                     # 文档资料目录
│   ├── LICENSE               # 文档许可证
│   ├── en/                   # 英文文档目录
│   │   ├── PI/               # PI特性文档
│   │   │   ├── api_reference.md
│   │   │   ├── faq.md
│   │   │   ├── installation_guide.md
│   │   │   ├── quick_start.md
│   │   │   ├── release_notes.md
│   │   │   └── user_guide.md
│   │   └── SQLi/             # SQL注入检测特性文档
│   │       ├── api_reference.md
│   │       ├── faq.md
│   │       ├── installation_guide.md
│   │       ├── quick_start.md
│   │       ├── release_notes.md
│   │       └── user_guide.md
│   ├── figures/              # 图片资源
│   │   └── SQLi/
│   ├── public_sys-resources/ # 文档提示图标资源
│   └── zh/                   # 中文文档目录
│       ├── menu_kadd.md      # 公共菜单
│       ├── release_notes.md  # 公共版本说明书
│       ├── PI/               # PI特性文档
│       │   ├── api_reference.md
│       │   ├── faq.md
│       │   ├── installation_guide.md
│       │   ├── quick_start.md
│       │   └── user_guide.md
│       └── SQLi/             # SQL注入检测特性文档
│           ├── api_reference.md
│           ├── faq.md
│           ├── installation_guide.md
│           ├── quick_start.md
│           └── user_guide.md
|—— PI/                       # PI特性代码
│   └── THIRD_PARTY_LICENSES  # PI特性三方许可证
|—— SQLi/                     # SQL注入检测特性代码
│   └── THIRD_PARTY_LICENSES  # SQL注入检测特性三方许可证
```

## PI特性

### 特性简介

KADD PI（Prompt Injection，越狱文本检测）采用规则和深度学习相结合的混合检测架构。检测流程为三级过滤：黑白名单过滤（基于MinHash LSH近似匹配）、正则规则过滤（匹配常见越狱模式）、模型分类过滤（基于LERT模型的ONNX推理），实现对越狱文本的高精度、低延迟检测。方案针对鲲鹏处理器特性进行优化，在保证检测准确率的同时，提供简洁的Python语言API接口，易于集成到现有安全防护系统中。

### 版本说明

KADD的版本说明包含软件版本配套关系和软件包下载以及每个版本的特性变更说明，具体请参见[版本说明](docs/zh/release_notes.md)。

### 快速入门

PI特性的快速入门，包括快速安装、数据准备和工具使用等，具体请参见[快速入门](docs/zh/PI/quick_start.md)。

### 学习文档

本特性提供以下学习文档可供参考。

| 文档 | 介绍|
| -- | -- |
| [版本说明书](docs/zh/release_notes.md) | 提供KADD及PI、SQLi特性的版本更新说明。 |
| [安装指南](docs/zh/PI/installation_guide.md) | 提供KADD PI特性的环境部署、依赖包安装及加速库编译方法。 |
| [快速入门](docs/zh/PI/quick_start.md) | 提供KADD PI特性的demo示例快速使用方法。 |
| [API参考](docs/zh/PI/api_reference.md) | 提供KADD PI特性的初始化、检测和黑白名单更新接口的完整API说明。 |
| [用户指南](docs/zh/PI/user_guide.md) | 提供KADD PI特性的配置说明、检测原理和代码示例。 |
| [FAQ](docs/zh/PI/faq.md) | 列举KADD PI特性的一些常见问题。 |

## SQL注入检测特性

### 特性简介

KADD SQL注入检测方案采用DFA状态机与ML/DL双引擎相结合的混合检测架构。通过DFA引擎进行精确的SQL语句分词，结合ML引擎或DL（CNN）引擎进行智能特征推理，实现对SQL注入攻击的高精度、低延迟检测。方案针对鲲鹏处理器特性进行优化，在保证检测准确率的同时，提供简洁的C语言API接口，易于集成到现有安全防护系统中。

### 版本说明

KADD的版本说明包含软件版本配套关系和软件包下载以及每个版本的特性变更说明，具体请参见[版本说明](docs/zh/release_notes.md)。

### 快速入门

SQL注入检测特性的快速入门，包括快速安装、数据准备和工具使用等，具体请参见[快速入门](docs/zh/SQLi/quick_start.md)。

### 学习文档

本特性提供以下学习文档可供参考。

| 文档 | 介绍|
| -- | -- |
| [版本说明书](docs/zh/release_notes.md) | 提供KADD及PI、SQLi特性的版本更新说明。 |
| [安装指南](docs/zh/SQLi/installation_guide.md) | 提供KADD SQL注入检测特性的环境部署、依赖包安装及编译方法。 |
| [快速入门](docs/zh/SQLi/quick_start.md) | 提供KADD SQL注入检测特性的demo示例快速使用方法。 |
| [API参考](docs/zh/SQLi/api_reference.md) | 提供KADD SQL注入检测特性的DFA引擎、特征引擎、ML引擎、DL引擎和SQLi引擎的完整API接口说明。 |
| [用户指南](docs/zh/SQLi/user_guide.md) | 提供KADD SQL注入检测特性的用法和实践。 |
| [FAQ](docs/zh/SQLi/faq.md) | 列举KADD SQL注入检测特性的一些常见问题。 |

## 贡献声明

如果使用过程中有任何问题，或者需要反馈特性需求和bug报告，可以提交issues联系我们，具体贡献方法可参考[这里](https://gitcode.com/boostkit/community/blob/master/docs/contributor/contributing.md)。

## 免责声明

致KADD使用者

- 本项目仅提供参考示例及工具，使用者需自行承担使用风险，并理解以下内容：
    - 数据处理及删除：用户在使用本工具过程中产生的数据属于用户责任范畴。建议用户在使用完毕后及时删除相关数据，以防信息泄露。
    - 数据保密与传播：使用者了解并同意不得将通过本工具产生的数据随意外发或传播。对于由此产生的信息泄露、数据泄露或其他不良后果，本工具及其开发者概不负责。
    - 用户输入安全性：用户需自行保证输入的命令行的安全性，并承担因输入不当而导致的任何安全风险或损失。对于输入命令行不当所导致的问题，本工具及其开发者概不负责。
- 免责声明范围：本免责声明适用于所有使用本工具的个人或实体。不建议将此工具用于任何商业用途。使用本工具即表示您同意并接受本声明的内容，并愿意承担因使用该功能而产生的风险和责任，如有异议请停止使用本工具。
- 在使用本工具之前，请谨慎阅读并理解以上免责声明的内容。对于使用本工具所产生的任何问题或疑问，请及时联系开发者。

## 许可证书

本项目采用**BSD 3-Clause**许可证。详见 [LICENSE](LICENSE)。

本项目的文档适用**CC-BY 4.0**许可证，具体请参见 [LICENSE](docs/LICENSE) 文件。

---

## 第三方许可证

各特性模块使用的第三方库及其许可证详见对应模块目录下的 `THIRD_PARTY_LICENSES` 文件：

- **PI特性**：详见 [PI/THIRD_PARTY_LICENSES](PI/THIRD_PARTY_LICENSES)
- **SQL注入检测特性**：详见 [SQLi/THIRD_PARTY_LICENSES](SQLi/THIRD_PARTY_LICENSES)
