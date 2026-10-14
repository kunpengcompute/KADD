# 安装指南

本指南介绍 KADD PI（Prompt Injection，越狱文本检测）特性的环境部署和依赖安装步骤，分为两个章节：

- **第 1 章 编译加速库**：说明如何编译针对鲲鹏处理器优化的 onnxruntime 库，**仅鲲鹏处理器用户需要**。
- **第 2 章 部署 PI 特性**：说明如何安装依赖、配置环境变量并验证功能，**所有用户都需要**。

## 开始之前：选择你的安装路径

**第 1 步：确认处理器型号**

```bash
lscpu   # 查看输出中的 BIOS Model name 字段，确认处理器型号
```

**第 2 步：根据下表选择安装路径**

| 处理器型号                | 使用的 onnxruntime         | 安装路径                   |
| -------------------- | ----------------------- | ---------------------- |
| 鲲鹏920处理器、鲲鹏920新型号处理器 | 加速库20（onnxruntime_acl）  | 第 1 章（1.2 节），再第 2 章    |
| 鲲鹏950处理器             | 加速库50（onnxruntime）      | 第 1 章（1.3 节），再第 2 章    |
| 其他处理器（含 x86 架构）      | 标准版 onnxruntime==1.19.2 | **跳过第 1 章**，直接从第 2 章开始 |

### 术语说明

| 术语                    | 含义                                                                                  |
| --------------------- | ----------------------------------------------------------------------------------- |
| 加速库                   | 针对鲲鹏处理器优化的 onnxruntime 库，通过源码编译得到 whl 安装包（Python 的二进制安装包）                           |
| 加速库20                 | 供鲲鹏920处理器、鲲鹏920新型号处理器使用的加速库，基于 onnxruntime 1.19.2 编译，依赖 Arm Compute Library 和 KONNX |
| 加速库50                 | 供鲲鹏950处理器使用的加速库，基于 onnxruntime 1.22.2 编译                                            |
| Arm Compute Library   | Arm 官方开源计算库，编译加速库20的前置依赖                                                            |
| KONNX / SRA_Inference | 鲲鹏社区提供的推理加速组件，编译加速库20的前置依赖                                                          |

### 路径约定

本文所有命令中的 **`/path` 均为占位符**，执行命令时请替换为实际路径。本文假设：

- 编译源码（ComputeLibrary、onnxruntime 等）均下载并存放在 `/path` 目录下，例如 Arm Compute Library 位于 `/path/ComputeLibrary-23.08`，onnxruntime 源码位于 `/path/onnxruntime`。
- 代码仓也位于 `/path` 目录下，此时 `PI` 目录为 `/path/PI`（即 `function_test.py` 所在目录）。

例如：若实际工作目录为 `/home/user/pi_build`，则将命令中的 `/path` 全部替换为 `/home/user/pi_build`。若你的目录结构与本文假设不同，请对应替换相关路径。

为避免遗漏，本文所有包含 `/path` 的命令块**首行均带有注释提醒**，直接复制执行即可看到。若执行时出现 `No such file or directory`（找不到 `/path` 相关路径）的错误，通常是未替换占位符导致，请替换后重试。

## 1. 编译加速库

使用**非鲲鹏处理器**（如 x86）的用户无需编译加速库，请**直接阅读第 2 章**。

本节说明 PI 特性加速库的环境依赖和编译步骤。加速库分为两个版本：

- **加速库20**：供鲲鹏920处理器、鲲鹏920新型号处理器使用，编译步骤见 1.2 节
- **加速库50**：供鲲鹏950处理器使用，编译步骤见 1.3 节

### 1.1 编译环境推荐要求

|                     | 加速库20已验证版本                                                                  | 加速库50已验证版本                                      |
| ------------------- | --------------------------------------------------------------------------- | ----------------------------------------------- |
| 处理器                 | 鲲鹏920处理器                                                                    | 鲲鹏950处理器                                        |
| 操作系统                | openEuler 24.03 LTS                                                         | openEuler 24.03 LTS                             |
| cmake               | 3.27.9                                                                      | 3.28.1                                          |
| make                | 4.3                                                                         | 4.4.1                                           |
| gcc                 | 10.3.1                                                                      | 12.3.1                                          |
| python              | 3.9.9                                                                       | 3.10.20                                         |
| python三方库           | numpy==2.0.2,packaging==26.3,SCons==4.11.1,wheel==0.48.0,setuptools==59.4.0 | numpy==2.2.6,packaging==26.2,setuptools==79.0.1 |
| onnxruntime         | 1.19.2                                                                      | 1.22.2                                          |
| Arm Compute Library | 23.08                                                                       | -                                               |
| KONNX               | 1.1.0                                                                       | -                                               |

- 表中"处理器"为各加速库**编译时**所推荐使用的机器型号；加速库的适用机型以第 2 章"已验证环境"表格为准（加速库20用于鲲鹏920/鲲鹏920新型号处理器，加速库50用于鲲鹏950处理器）。
- 表中 python 三方库为**编译依赖**（编译 onnxruntime 时使用），与第 2 章"三方库依赖"（PI 特性运行依赖）相互独立，请勿混淆。

### 1.2 编译加速库20（鲲鹏920处理器 / 鲲鹏920新型号处理器）

整个流程共 4 步，请按顺序执行。

#### 步骤 1：安装编译工具链

安装 1.1 节表格中"加速库20已验证版本"列要求的 cmake、make、gcc、python。安装方式不限（系统包管理器或源码编译均可），以 openEuler 为例：

```bash
dnf install -y gcc gcc-c++ make cmake git wget unzip python3 python3-pip
```

若安装的版本与 1.1 节表格要求不一致，可在安装时指定版本：

```bash
# 查看仓库中可安装的版本
dnf list --showduplicates cmake
# 安装指定版本（版本号以上述命令的查询结果为准）
dnf install -y cmake-3.27.9
```

若仓库中没有所需版本，请到官方发布页下载对应版本的源码，自行编译安装。

安装完成后，使用以下命令核对版本是否与 1.1 节表格一致：

```bash
cmake --version    # 应为 3.27.9
make --version     # 应为 4.3
gcc --version      # 应为 10.3.1
python --version   # 应为 3.9.9
```

安装编译所需的 python 三方库（版本与 1.1 节表格一致）：

```bash
pip install numpy==2.0.2 packaging==26.3 SCons==4.11.1 wheel==0.48.0 setuptools==59.4.0
```

#### 步骤 2：编译 Arm Compute Library

在工作目录下下载源码，解压并进入文件夹：

```bash
# 注意：/path 为占位符，请先替换为实际工作目录
cd /path
wget -q https://github.com/ARM-software/ComputeLibrary/archive/refs/tags/v23.08.zip -O ComputeLibrary-23.08.zip
unzip ComputeLibrary-23.08.zip && cd ComputeLibrary-23.08
```

使用如下命令进行编译：

```bash
scons Werror=1 -j8 debug=0 neon=1 opencl=0 os=linux arch=armv8a build=native
```

**检查点**：编译完成后，`/path/ComputeLibrary-23.08/build` 目录下应存在以下三个文件（第 2 章部署时需要配置到环境变量中）：

- `libarm_compute.so`
- `libarm_compute_core.so`
- `libarm_compute_graph.so`

#### 步骤 3：安装 KONNX

按照鲲鹏社区指引，先安装[SRA_Inference](https://www.hikunpeng.com/document/detail/zh/boostsra/srainference/SRA_Inference/kunpengsra_inference_16_0006.html)：

```bash
# 注意：/path 为占位符，请先替换为实际工作目录
cd /path
wget -q https://kunpeng-repo.obs.cn-north-4.myhuaweicloud.com/Kunpeng%20BoostKit/Kunpeng%20BoostKit%2026.1.RC1/BoostKit-boostsra-inference_1.2.0_RC1.zip -O sra_inference.zip
unzip sra_inference.zip
rpm -ivh boostsra-inference-1.2.0-RC1.aarch64.rpm
source /etc/profile
```

然后安装打入KONNX补丁的onnxruntime源码，具体见[参考文档](https://www.hikunpeng.com/document/detail/zh/boostsra/srainference/SRA_Inference/kunpengsra_inference_16_0031.html)：

```bash
# 注意：/path 为占位符，请先替换为实际工作目录
cd /path
git clone -b v1.19.2 https://github.com/microsoft/onnxruntime.git
git clone https://gitcode.com/openeuler/sra_onnxruntime_adapter.git
cd onnxruntime
git apply ../sra_onnxruntime_adapter/konnx.patch
```

#### 步骤 4：编译 OnnxRuntime

进入 `onnxruntime` 目录下，然后开始源码编译onnxruntime，使用命令如下，其中 `--acl_home` 与 `--acl_libs` 两个参数需要指定步骤 2 编译得到的 Arm Compute Library 路径：

```bash
# 在 /path/onnxruntime 目录下执行
# 注意：请将下方 --acl_home 与 --acl_libs 中的 /path 替换为实际工作目录
./build.sh --config Release --build_wheel --parallel --compile_no_warning_as_error \
  --skip_submodule_sync --allow_running_as_root --skip-keras-test --skip_onnx_tests \
  --skip_tests --build_dir build --use_acl "ACL_2308" \
  --acl_home=/path/ComputeLibrary-23.08 \
  --acl_libs=/path/ComputeLibrary-23.08/build
```

**检查点**：编译成功后，在 `/path/onnxruntime/build/Release/dist` 目录下可得到 **`onnxruntime_acl-xx.whl`**（xx 为版本号，实际文件名以该目录中的生成结果为准），此文件将在第 2 章部署时安装。

若遇到Eigen库下载失败，需去浏览器下载[Eigen](https://gitlab.com/libeigen/eigen/-/archive/e7248b26a1ed53fa030c5c459f7ea095dfd276ac/eigen-e7248b26a1ed53fa030c5c459f7ea095dfd276ac.zip)，手动放置到`/path/onnxruntime/build/Release/_deps/eigen-subbuild/eigen-populate-prefix/src/`目录下，并执行如下命令，注意：`/path` 为占位符，请先替换为实际工作目录

```bash
# 在 /path/onnxruntime 目录下执行
grep -r "be8be39fdbc6e60e94fa7870b280707069b5b81a" . | cut -d: -f1 | sort -u | xargs sed -i.bak 's/be8be39fdbc6e60e94fa7870b280707069b5b81a/32b145f525a8308d7ab1c09388b2e288312d8eba/g'
```

### 1.3 编译加速库50（鲲鹏950处理器）

整个流程共 2 步，请按顺序执行。

#### 步骤 1：安装编译工具链

安装 1.1 节表格中"加速库50已验证版本"列要求的 cmake、make、gcc、python。安装方式不限（系统包管理器或源码编译均可），以 openEuler 为例：

```bash
dnf install -y gcc gcc-c++ make cmake git wget unzip python3 python3-pip
```

若安装的版本与 1.1 节表格要求不一致，可在安装时指定版本：

```bash
# 查看仓库中可安装的版本
dnf list --showduplicates cmake
# 安装指定版本（版本号以上述命令的查询结果为准）
dnf install -y cmake-3.28.1
```

若仓库中没有所需版本（如 python 3.10.20 通常无法直接安装），请到官方发布页下载对应版本的源码，自行编译安装。

安装完成后，使用以下命令核对版本是否与 1.1 节表格一致：

```bash
cmake --version    # 应为 3.28.1
make --version     # 应为 4.4.1
gcc --version      # 应为 12.3.1
python --version   # 应为 3.10.20
```

安装编译所需的 python 三方库（版本与 1.1 节表格一致）：

```bash
pip install numpy==2.2.6 packaging==26.2 setuptools==79.0.1
```

#### 步骤 2：编译 OnnxRuntime

在工作目录下下载 onnxruntime 的源码并解压进入文件夹：

```bash
# 注意：/path 为占位符，请先替换为实际工作目录
cd /path
wget -q https://codeload.github.com/microsoft/onnxruntime/zip/refs/tags/v1.22.2 -O onnxruntime-1.22.2.zip
unzip onnxruntime-1.22.2.zip && cd onnxruntime-1.22.2
```

进入 `onnxruntime-1.22.2` 目录下，然后开始源码编译onnxruntime，在 `/path/onnxruntime-1.22.2` 目录下使用以下命令：

```bash
# 在 /path/onnxruntime-1.22.2 目录下
./build.sh --config Release --build_wheel --parallel --compile_no_warning_as_error --skip_submodule_sync --allow_running_as_root --skip-keras-test --skip_onnx_tests --skip_tests --build_dir build --cmake_extra_defines onnxruntime_USE_KLEIDIAI=ON
```

**检查点**：编译完成后，在 `/path/onnxruntime-1.22.2/build/Release/dist` 目录下可得到 **`onnxruntime-xx.whl`**（xx 为版本号，实际文件名以该目录中的生成结果为准），此文件将在第 2 章部署时安装。

## 2. 部署PI特性

### 2.1 运行环境要求

#### 已验证环境

| 鲲鹏芯片型号      | Python版本 | ONNX Runtime           |
| ----------- | -------- | ---------------------- |
| 鲲鹏920处理器    | 3.9      | onnxruntime_acl-1.19.2 |
| 鲲鹏920新型号处理器 | 3.9      | onnxruntime_acl-1.19.2 |
| 鲲鹏950处理器    | 3.10     | onnxruntime-1.22.2     |
| 其他          | 不限       | onnxruntime==1.19.2    |

操作系统均为openEuler 24.03 LTS。

#### 三方库依赖

PI 特性运行依赖以下 python 三方库（对应代码仓中的 `PI/deploy/requirements.txt`，可在 2.2 节步骤 3 中一键安装）：

| 项目           | 版本     | 说明               |
| ------------ | ------ | ---------------- |
| datasketch   | 2.0.0  | MinHash LSH 近似匹配 |
| jieba        | 0.42.1 | 中文分词             |
| numpy        | 2.0.1  | 数值计算             |
| onnx         | 1.19.1 | ONNX模型格式支持       |
| onnxscript   | 0.7.1  | ONNX脚本支持         |
| scikit-learn | 1.6.1  | 机器学习工具           |
| torch        | 2.8.0  | 深度学习框架（训练用）      |
| tqdm         | 4.69.0 | 进度条              |
| transformers | 4.57.6 | 预训练模型工具          |

### 2.2 环境部署

#### 步骤 1：确认处理器型号

```bash
lscpu   # 查看 BIOS Model name 字段，确认处理器型号
```

#### 步骤 2：安装 onnxruntime 库（三选一）

**情况 A：鲲鹏920处理器 / 鲲鹏920新型号处理器 → 安装加速库20**

whl 文件为 1.2 节步骤 4 的编译产物，位于编译机的 `/path/onnxruntime/build/Release/dist` 目录下。若部署机与编译机不是同一台机器，请先将 whl 文件拷贝到部署机，再执行安装：

```bash
pip install onnxruntime_acl-xx.whl   # xx 为版本号，请替换为 dist 目录中的实际文件名
```

**情况 B：鲲鹏950处理器 → 安装加速库50**

whl 文件为 1.3 节步骤 2 的编译产物，位于编译机的 `/path/onnxruntime-1.22.2/build/Release/dist` 目录下。若部署机与编译机不是同一台机器，请先将 whl 文件拷贝到部署机，再执行安装：

```bash
pip install onnxruntime-xx.whl       # xx 为版本号，请替换为 dist 目录中的实际文件名
```

**情况 C：其他处理器 → 直接安装标准版**

```bash
pip install onnxruntime==1.19.2
```

#### 步骤 3：安装 PI 特性的 python 依赖（所有处理器均需执行）

鲲鹏处理器用户请注意：部署环境的 python 版本必须与第 1 章编译 whl 时所使用的 python 版本一致（鲲鹏920/鲲鹏920新型号：3.9，鲲鹏950：3.10），否则会导致 whl 安装失败或运行报错。

```bash
# 注意：/path 为占位符，即 PI 目录的父目录，请替换为实际路径
pip install -r /path/PI/deploy/requirements.txt
```

#### 步骤 4：配置环境变量（仅鲲鹏处理器需要）

**使用鲲鹏920处理器 / 鲲鹏920新型号处理器（加速库20）时，需配置以下 2 项：**

1. 安装[SRA_Inference](https://www.hikunpeng.com/document/detail/zh/boostsra/srainference/SRA_Inference/kunpengsra_inference_16_0006.html)。若部署机与编译加速库的机器为同一台，则无需再安装。

2. 添加ACL库路径。部署机上该目录下应存在 `libarm_compute.so`、`libarm_compute_core.so` 以及 `libarm_compute_graph.so`（若部署机与编译机不是同一台机器，请将编译机上 `/path/ComputeLibrary-23.08/build` 目录下的文件拷贝到部署机，并按实际存放路径修改下方命令）：
   
   ```bash
   # 注意：/path 为占位符，请替换为实际工作目录
   export LD_LIBRARY_PATH=/path/ComputeLibrary-23.08/build:$LD_LIBRARY_PATH
   ```

**使用鲲鹏950处理器（加速库50）时，无需配置环境变量。**

通过 `export` 配置的环境变量**仅在当前终端有效**，新开终端后会失效，需重新执行。若希望永久生效，可将上述 export 命令追加到 `~/.bashrc` 文件末尾，并执行 `source ~/.bashrc` 使其生效。

### 2.3 功能验证

环境部署完成后，可通过单元测试验证功能是否正常。在 `PI` 目录（即代码仓中 `function_test.py` 所在的目录）下运行命令：

```bash
# 注意：/path 为占位符，即 PI 目录的父目录，请替换为实际路径
cd /path/PI
python function_test.py
```

预期输出如下（表示 17 个测试全部通过）：

```bash
.................
----------------------------------------------------------------------
Ran 17 tests in 1.703s

OK
```

若输出与上述不一致，请检查前置步骤是否全部完成，或参考[常见问题解答](faq.md)。

## 3. 性能优化命令

部署完成后，可使用以下命令运行脚本以达到最大性能：

```bash
# 运行命令示例
numactl --cpunodebind=0 --membind=0 python 脚本名称.py
```

`numactl` 将进程绑定到NUMA节点0的CPU和内存上，减少跨节点访问延迟。

若提示 `numactl` 命令不存在，可先安装：

```bash
dnf install -y numactl
```

## 修订记录

| 文档版本 | 发布日期       | 修改说明    |
| ---- | ---------- | ------- |
| 01   | 2026-09-30 | 第一次正式发布 |
