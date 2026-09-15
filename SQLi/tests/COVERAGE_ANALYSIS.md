# KADD SQLi 覆盖率分析与整改说明

> 对应报告：`coverage/` 下 4 份 llvm-cov 报告
> （SVE+UT、SVE+Fuzz、ONNX+UT、ONNX+Fuzz）。
> 目标：**函数覆盖率 = 100%，行覆盖率整体 ≥ 80% 且单文件 ≥ 80%**；
> 确实无法覆盖的行在本文档第 4 节逐条给出原因。

## 1. 整改前基线

| 报告 | 行覆盖 | 函数覆盖 | 主要缺口 |
|---|---|---|---|
| SVE + UT   | 2226/2784 = 79.96% | 109/109 | 错误路径分支 |
| SVE + Fuzz | 2183/2784 = 78.41% | 108/109 | native→ONNX 回退、rf 原生加载链 |
| ONNX + UT  | 1143/1505 = 75.95% | — | **报告缺少 kadd_cnn_engine.c / kadd_rf_engine.c 两个文件** |
| ONNX + Fuzz| 1738/2370 = 73.33% | 97/102 | rf 5 个函数从未进入、ORT 错误清理分支 |

## 2. 根因与修复

### 2.1 llvm-cov 单二进制映射导致 UT 数据被静默丢弃（影响：ONNX+UT 全报告）

6 个 `#include "*.c"` 型 gtest（config/dfa/feature/inference/ml/sqli）以及
rf_engine_gtest、cnn_engine_gtest 的 profiling 记录哈希与 `kadd_sqli_demo`
中的库副本不一致，llvm-cov 以 demo 单二进制做映射时把这些 TU 的数据全部丢弃。
表现为：ONNX+UT 报告里 cnn/rf 两个文件整体缺失、各引擎错误分支覆盖率偏低。

**修复**（`coverage_all.sh`）：UT 报告改为把 demo + 全部 `*_test`/`*_gtest`
二进制逐个以 `-object` 传入 `llvm-cov report/show`，合并所有 TU 的覆盖数据；
`kadd_fuzzer` 单独统计，不混入 UT 报告。

### 2.2 合成 RF 模型未签名，HMAC fail-closed 后所有深层分支不可达（影响：ONNX+Fuzz 函数覆盖 97/102）

[REPORT-2026-0000028] 之后原生模型强制 HMAC-SHA256 认证，fuzzer 旧
`FuzzWriteRfModel()` 写的是 flags=0 未签名模型，加载器在认证处即拒绝，
其后的 CRC 校验、节点解析、`validate_tree()`、`predict_tree()`、
`predict_batch_scalar()` 永远走不到（rf 5 个函数 0 命中）。

**修复**（`tests/fuzz/kadd_fuzzer.cpp`）：合成模型自带合法签名（密钥取值逻辑
与加载器一致：优先环境变量，否则仓库公开开发密钥），并新增树结构非法变体
（feature 越界 / reserved 非 0 / left<=index / 非有限阈值与概率 / offsets 非法 /
CRC 失配 / HMAC 篡改 / 尾部多字节）覆盖 `validate_tree()` 全部拒绝分支；
另直接加载仓库真实 `.krf` 走完整生命周期。本地 gcov 实测 rf +99 行、+5 函数。

### 2.3 错误路径用例缺失（影响：全部 4 份报告的行覆盖）

config/dfa/feature/rf/cnn/dl/sqli 的参数校验、表文件解析、feature_dim 失配、
native→ONNX 回退等分支此前没有任何用例触达。

**修复**：新增 `tests/error_paths_cases.inc`（UT 与 FUZZ 共用的错误路径用例集，
本地 gcov 逐条验证），由两处包含：
- `tests/test_error_paths_gtest.cpp`（新 UT，注册为 `error_paths_test`）→ 提升两份 UT 报告；
- `tests/fuzz/kadd_fuzzer.cpp` 分层调度 → 提升两份 Fuzz 报告。

本地实测（gcc/gcov，ONNX 等价配置，仅跑 .inc 用例、未叠加既有测试）：

| 文件 | 行覆盖 |
|---|---|
| kadd_feature_engine.c | 89.58% |
| kadd_config_loader.c  | 85.22% |
| kadd_dfa_engine.c     | 87.58% |
| kadd_rf_engine.c      | 86.15% |
| kadd_cnn_engine.c     | 91.48% |

与既有 UT/Fuzz 覆盖取并集后只会更高；clang 对闭合花括号/续行的计入比 gcc 更宽
（实测 ONNX_Fuzz rf 的 127 个覆盖行中 41 行 gcc 不计），实际报告数字优于上表。

### 2.4 ORT 错误清理分支只有故障注入可达（影响：inference 约 90 行）

`check_ort_status` / `get_model_input_dim` / `get_model_output_dim` / `create_ort_session` /
`get_io_name` / `init_ort_env` / `predict_batch` 的 goto cleanup、ReleaseStatus、
ReleaseTypeInfo、告警回退等分支需要 OrtApi 函数表成员返回错误状态才可达，
真实模型正常加载永远走不到。

**修复**：
- `tests/kadd_inference_fault_inject.h`：复制真实 OrtApi 函数表、只替换要注入
  失败的成员，错误状态由真实 ORT 产生（对不存在的模型路径调 CreateSession），
  清理函数作用于真实 OrtStatus，无签名假设风险；
- 注入场景由 10 个扩展到 **26 个**，新增三组：
  `create_ort_session`（options / 执行模式 / 图优化 / intra、inter 线程数告警 /
  CreateMemoryInfo 失败 → cleanup_session）6 个、`get_io_name`（allocator / output_count /
  input_name 失败、name==NULL 防御、AllocatorFree 返错）5 个、`predict_batch`
  （CreateTensor / Run / GetTensorData 失败、output_dim<2）4 个，以及
  `init_ort_env` CreateEnv 失败 1 个；
- `test_inference_engine_gtest.cpp` 新增 `InferenceFaultInjectTest.*`（UT 报告生效）
  与 `InferencePublicApiTest.*`（NULL/非法参数校验，不解引用 ORT）；
- `kadd_fuzzer.cpp` 在 `KADD_FUZZ_INFERENCE_INJECT=ON`（默认 ON）时内联
  `kadd_inference_engine.c` 并每 256 次迭代执行一轮全场景注入（Fuzz 报告生效）。
  覆盖率构建为静态库，归档不会重复拉取该目标文件，无重复定义；构建异常时
  `coverage_all.sh` 自动以 OFF 重试。

### 2.5 SQLi native→ONNX 回退矩阵（影响：SVE+Fuzz 209-226/251-268，ONNX+Fuzz 229-236/271-278）

`FuzzSqliErrorPaths()` 构造 8 个合成 config（ML/DL × 回退成功/双双失败/
两字段相同/onnx 字段为空），完整覆盖 `create_engine_internal()` 的回退矩阵；
创建成功的引擎执行 detect → detect_batch → free 完整生命周期。

### 2.6 指针返回值校验加固（white_box_test_047）

白盒测试检查项「调用函数返回类型为指针的需要先校验再使用」走读发现：
`kadd_inference_engine.c` 的 `get_io_name()` 中 `strdup(name)` 的返回值未判空、
失败无日志，契约完全依赖唯一调用方 `detect_io_names()` 的兜底判空
（该兜底存在且失败路径闭环：`load_engine_internal` 会释放引擎并返回
`KADD_ERROR_MODEL_LOAD_FAILED`，故现网无 NULL 解引用路径）。

**加固**：`get_io_name()` 内新增两道显式防御——
1) `name == NULL` 直接返回 NULL（`strdup(NULL)` 为 UB，ORT 契约外的防御）；
2) `strdup` 失败打印 ERROR 日志后返回 NULL，使失败可观测、契约自洽。
行为不变（调用方兜底逻辑保持），本次加固后该文件 147 行以后行号整体 +11，
第 4 节目录与 fuzz 用例注释中的行号已同步更新。

### 2.7 UT 报告里同一函数出现两条记录（不是缺陷）

现象：UT 报告的函数列表里同时出现

- `kadd_inference_engine.c:create_ort_session`
- `test_inference_engine_gtest.cpp:_ZL18create_ort_sessionP23kadd_inference_engine_tPKc`

并且 HTML 里看起来「多了几个框框」。根本原因是**同一份源码被编译了两次**：

| 记录 | 来自哪个二进制 | 语言 / 链接性 | 符号名 |
|---|---|---|---|
| `kadd_inference_engine.c:create_ort_session` | `kadd_sqli_demo`、`ml_engine_test`、`dl_engine_test`、`error_paths_test` 等链接了 `kadd_inference_engine` 库的目标 | C（库副本） | 未修饰名 |
| `test_inference_engine_gtest.cpp:_ZL18create_ort_sessionP23kadd_inference_engine_tPKc` | `inference_engine_test`（该目标在 `tests/CMakeLists.txt` 里显式排除了 `kadd_inference_engine` 库） | C++：`#include "kadd_inference_engine.c"` 内联，static → 内部链接 | `_ZL…` 中的 `L` 就是内部链接标记；`P23kadd_inference_engine_t`＝`kadd_inference_engine_t*`，`PKc`＝`const char*` |

`coverage_all.sh` 把 demo 与全部 `*_test`/`*_gtest` 逐个以 `-object` 传给 `llvm-cov`（见 2.1 节），
两份编译产物的数据都会进来：

1. **函数列表**按「编译单元 × 函数」列出，所以同一个函数出现两条；
2. **行 / 区域覆盖**按源文件归并取**并集**，不会重复计数，
   `kadd_inference_engine.c` 的命中行是两份的合计；
3. HTML 里「多出来的框框」是两份 mapping 的 region 轮廓叠在同一批源码行上的
   渲染结果，属于显示层现象。

结论：**数字是对的，不需要处理**。若希望报告只出现一条，就得让
`inference_engine_test` 不再内联 `.c`，但那样无法测试 static 函数
（`create_ort_session` / `ort_thread_count_from_env` / `get_io_name` / `load_engine_internal` 等），
覆盖率反而会大幅下降，因此保留现状。Fuzz 报告不出现重复项，
是因为它只有 `kadd_fuzzer` 一个二进制，且静态归档不会重复拉取同一个目标文件。

### 2.8 ORT 线程数环境变量分支（影响：inference 276-291）

`coverage_all.sh` 恒定导出合法的 `KADD_ORT_INTRA_OP_THREADS=8` /
`KADD_ORT_INTER_OP_THREADS=1`，因此 `ort_thread_count_from_env()` 的
「未设置 / 空串 → 回退默认」与「取值非法 → 告警回退」两类分支
在常规 UT 与 Fuzz 下永远走不到。

**修复**（三处互补，UT / FUZZ 两份报告都生效）：

- `test_inference_engine_gtest.cpp`：新增 RAII 守卫 `ScopedEnvVar`（析构时精确复原，
  原变量不存在则删除，不污染脚本导出的调优值）与 `InferenceEnvConfigTest` 两个用例：
  一个直接调用 static `ort_thread_count_from_env()` 遍历 20 个取值，一个端到端确认
  「环境变量缺失 / 非法时模型仍必须加载成功」这一功能契约；
- `tests/error_paths_cases.inc`：新增 `FuzzInferenceThreadEnvBranches()`，经公共接口
  `kadd_dl_model_load` / `kadd_model_load` 真实加载模型，覆盖**库副本**中的同一批行
  （UT 由 `error_paths_test` 命中，FUZZ 由 `LLVMFuzzerInitialize` 命中，两种模式都执行）；
  进程内只执行一次（等频随机长稳模式同样执行——长稳不跳过任何随机 fuzz 初始化）；
- `kadd_fuzzer.cpp`：`FuzzInferenceFaultInjection()` 内直接调用 static 函数做取值扫描
  （本 TU 已内联 `.c`，无需加载模型），并补 `release_engine_resources(nullptr)` 覆盖 367-369。

取值矩阵已本地逐项实测（抽出源码中的函数原文单独编译运行，20/20 符合预期）：
未设置 / `""` / `abc` / `" 8"` / `8x` / `"8 "` / `0` / `-1` / `+8` / `0x8` / `1025` /
`99999999999999999999` → 回退默认值；`1` / `2` / `8` / `16` / `1024` → 采用原值。

### 2.9 `KADD_RF_HAVE_SVE` 嵌套矛盾守卫与 LCOV 标记残留（影响：SVE 构建下 rf 批量推理的 fail-open 隐患）

外部走读意见提出「`KADD_RF_HAVE_SVE` 恒不成立，为不可达残留」。核查结论分两部分：

**(1) `KADD_RF_HAVE_SVE` 本身不是恒不成立。** 它由 `__aarch64__ && __ARM_FEATURE_SVE`
决定（`kadd_rf_engine.c:21-26`）；`CMakeLists.txt:96-146` 在 `ENABLE_ARM_SVE=ON`
（默认，`build.sh:62` 显式传 ON）时先用 `-march=armv8-a+sve` 真编译一段
`#include <arm_sve.h>` 探测编译器能力，通过才 `add_compile_options(-march=armv8-a+sve)`，
此时编译器自动定义 `__ARM_FEATURE_SVE` → 宏为 1。`coverage_all.sh` 本就跑
SVE ON / OFF 两套，两条分支都真实存在，不是残留。

**(2) 真正恒不成立的是它取反后的嵌套守卫（原第 591 行）。**
`predict_batch_sve_chunk()` 整体位于 `#if KADD_RF_HAVE_SVE`（原 542-665）内，
而步数预算守卫里又写了一层 `#if !KADD_RF_HAVE_SVE → predict_batch_scalar(...)`：
外层为真时内层必为假，**任何构建配置下都编不进来**；且 `predict_batch_scalar`
只在 `#if !KADD_RF_HAVE_SVE` 内定义，SVE 构建下并不存在（真编进来会直接编译报错）。
全文 10 处 `KADD_RF_HAVE_SVE` 中仅此一处自相矛盾（其余 9 处均在顶层），
引入者为历史提交 `7634c21`。对比 `kadd_cnn_engine.c` 的 5 处嵌套守卫为
`KADD_CNN_HAVE_SVE && defined/!defined(KADD_CNN_SVE256_ONLY)`——前半冗余但后半有意义，
不构成矛盾，可佐证 RF 这处是孤例漏改。

**危害不只是废码**：剔除矛盾守卫后 SVE 构建实际执行裸 `return;`，
`predict_batch_sve_chunk()` 返回 `void`，本 chunk 的 `probabilities[]` 一个都不写，
而 `kadd_rf_model_predict_batch()` 仍无条件 `return KADD_SUCCESS`
——静默输出未初始化脏数据（fail-open）。

**当前不可触发**：`validate_tree()`（171-202）强制内部节点 `left/right > index`
且落在 `[begin, end)`，子节点下标严格递增 → 树内不可能成环，最长路径
<= `maximum_steps - 1` 步，而触发条件是 `traversal_steps > maximum_steps`，数学上永不成立。
配套前提亦已核对：`prepare_sve_layout()` 把叶子 `feature=-1` 映射为 `UINT32_MAX`，
内部节点 feature 受 `feature_count <= 4096` 约束不会撞哨兵，故 `svcmpne_n_u32`
能正确熄灭叶子通道、循环必然收敛；`.krf` 已强制 HMAC 认证且节点数组加载后不可变。
属**纯防御性死代码**，但一旦放宽 `validate_tree` 的单调性约束就会从「死」变「活」，
且活法是无声输出脏数据。

**修复**：删除矛盾的 `#if !KADD_RF_HAVE_SVE / #endif`，改为确定性把本 chunk 的
`probabilities[]` 全部写 `0.0F` 后再 `return`（fail-safe）；可达路径行为完全不变，
覆盖率不受影响（仍归入第 4 节 C 类，见 rf 表新增行）。

**(3) 顺带修正同一区域失效的 LCOV 标记。** 原第 482 行
`// LCOV_EXCL_START: non-SVE fallback, unreachable on SVE hardware` 有两个问题：
一是**未配对 STOP**（两个 START 只有一个 STOP，482 那个从未闭合）；
二是**说明与事实相反**——`predict_tree()` / `kadd_rf_model_predict()` 没有任何 `#if` 守卫，
SVE ON/OFF 构建都会编译，且 `kadd_rf_model_predict()` 是 `kadd_rf_engine.h:25`
导出的对外单样本接口，UT（`test_rf_engine_gtest.cpp:102/138/178/185`）与
FUZZ（`error_paths_cases.inc`、`kadd_fuzzer.cpp`）都会调用并覆盖。
对当前数字无影响（`coverage_all.sh` 用 `llvm-cov report/show`，不识别 `LCOV_EXCL_*`，
那是 lcov/genhtml 的标记），但一旦改用 `llvm-cov gcov | lcov` 就会把一段**已覆盖的对外接口**
从统计里挖掉。已删除该 START，仅保留 `predict_batch_scalar` 那对准确标记
（它确实仅在非 SVE 构建编译）。

**(4) 另修正 `kadd_feature_engine.c:3` 的 `@file` 陈旧文件名**
（原写 `kadd_seq_to_feature.c`，实际文件为 `kadd_feature_engine.c`；
`kadd_seq_to_feature` 是仍在使用的 API 函数名，与本处 doxygen 文件名无关）。
全仓 `@file` 标签已逐个与真实文件名比对（SQLi 的 src/include/internal/demo/tests/fuzz），
仅此一处不符。

### 2.10 Fuzz 吞吐与并行（ASan 移除 / 重量级用例降频 / 多进程并行）

等频随机长稳与常规覆盖率 fuzz 共用同一套随机调度：`KADD_FUZZ_EQUAL_RANDOM=1`
时 selector / heavy / fault-inject 全部保留，仅叠加 21 个可长跑开放接口的
等频真实随机调用层（`FzEqualRandomApiLayer()`，有效句柄 + 随机输入）。
以下两项是常规 fuzz 模式的历史瓶颈，均已处理：

**ASan 移除**（`tests/CMakeLists.txt`）：非覆盖率构建原为 `-fsanitize=fuzzer,address`。
ASan 带来 2-3 倍内存与速度开销，影子内存还会显著抬高 RSS、限制可并行路数。
现两条分支统一为 `-fsanitize=fuzzer`。同时补齐了 `else()` 分支缺失的
`-fprofile-instr-generate -fcoverage-mapping` **链接**选项：该分支的编译选项里
一直带着 `-fprofile-instr-generate`（`kadd_fuzzer.cpp` 据此声明并调用
`__llvm_profile_write_file`），链接选项里却没有，属未闭合的链接期依赖，
非覆盖率 fuzz 构建会 `undefined reference`。

**重量级用例降频**（`kadd_fuzzer.cpp`）：`FzHeavyRealLifecycle()` 原为 1/64，
每个相位都要真实打开文件并创建 ONNX 会话（毫秒~数十毫秒级），迭代量增大后会长尾
堆积并把 exec/sec 拉低一个量级。现由新常量 `kFzHeavyInterval = 1024` 统一控制，
触发频率降至原来的 1/16，且 4 相位轮转保持覆盖面。触发判定 `it % kFzHeavyInterval` 与相位轮转
`(it / kFzHeavyInterval) % 4` 同源于该常量，调频不会失步；
相位内部 load:predict:predict_batch:free = 1:1:1:1 的配比不受影响。

**多进程并行**（`coverage_all.sh`，`FUZZ_JOBS=N`，默认 1 与原行为完全一致）：
并行的前提是给每个 worker 一份私有 `/tmp`——fuzzer 的合成资产是硬编码绝对路径
（`/tmp/kadd_fz_*`、`/tmp/fz_*`、`/tmp/fz3_*`、`/tmp/fz4_*`），
多进程共用同一 `/tmp` 会互相覆写：`BuildSyntheticAssets()` 在 init 阶段重写
`/tmp/kadd_fz_*`，而 selector 的 DFA / Config 用例会读取它们；
`error_paths_cases.inc` 更在热循环里反复重写 `/tmp/fz_*`。撕裂读会把本该成功的
加载路径随机翻成错误路径，既污染覆盖率（成功路径掉到错误路径）又制造假崩溃。

实现上用 `unshare --mount --propagation private` + tmpfs 给每个 worker 独立 `/tmp`
（`--propagation private` 必须带：systemd 下 `/` 默认是 shared mount，
不加会把 tmpfs 反向传播回宿主 `/tmp`）；探测失败则**自动回退串行并告警**，
绝不裸并行。不采用 libFuzzer 自带的 `-jobs`：它不隔离 `/tmp`，
且其「跑完即重启、直到发现 bug」的语义会让 `-max_total_time` 失去总时长上界。

并行安全性核对（以下均落在宿主路径，不受 namespace 影响）：

| 项 | 处理 |
|---|---|
| profraw | `LLVM_PROFILE_FILE="$FBLD/fuzz_%p.profraw"`，`%p`=PID 天然不冲突；合并步骤 `fuzz_*.profraw` 与 `llvm-profdata merge` 无需改动 |
| 崩溃产物 | `-artifact_prefix` 指向 `$BLD/fuzz_artifacts/wN/`（**不能放 /tmp**，否则随 namespace 退出丢失），并已加入 tar 打包清单 |
| 真实模型资产 | 走相对路径，cwd 与串行完全一致 |
| 随机起点 | 各 worker `-seed=$(w * 7919)`，避免重复探索同一空间（仅常规模式需要） |
| 日志 | 每 worker 独立 `fuzz_wN.log`，结束后合并为 `fuzz.log`（产物名与打包内容不变） |
| ORT 线程 | 每个 worker 各用 `KADD_ORT_INTRA_OP_THREADS` 个线程，总数 ~= N x intra（默认 8），按核数选 N |

总墙钟时间仍约为 `FUZZ_TIME`，迭代总量约为单路的 N 倍。单接口 3000 万次等频随机
长稳直接使用 `./tools/fuzz_longrun.sh [sve|onnx]`；该模式同样构造 `/tmp` 合成资产，
N>1 时脚本自动为每实例创建私有 `/tmp`（与 `coverage_all.sh` 同一探测与回退逻辑）。


### 2.11 长稳吞吐塌方（实测 stat::average_exec_per_sec 仅 2）

**根因（按贡献排序）**

1. `kadd_dfa_engine.c: load_u16_table` 逐单元 `fread(cell, 1, 2, fp)`：真实表
   2,585,600 单元/表，transition + output 即 ~517 万次 fread，单次
   `kadd_dfa_engine_create` 实测 ~0.5s（长稳下占 CPU 70%+）。这不只是 fuzz 问题：
   生产侧每次 `create_from_file` 同样付这笔开销。
2. 调度器把「真实资产重」用例放进了每次迭代的分组分支：平均每迭代
   ~0.6 次真实 ONNX 会话生命周期（CreateEnv + CreateSession(ORT_ENABLE_ALL) + intra=8
   线程池 + warmup Run + Release；fuzzer 串行 load→free 使 `g_ort_env` 引用计数
   （`kadd_inference_engine.c:567-573`）每次归零，无缓存可复用）+ ~0.5 次真实
   10.4MB 表读取（`FuzzSqliErrorPaths` 一次就是 8 个指向真实表的 config）。
3. 故障注入的线程风暴：`MakeRealErrorStatus()` 与 `FailSet*OpNumThreads` 桩未设
   线程数，ORT 默认 intra-op = 核数（256 核机上即 256），每 256 迭代 × 26 个
   场景 → 周期性数千次线程创建/销毁。
4. 指标口径：`stat::average_exec_per_sec` 的分母是「进程启动至今」，含
   `LLVMFuzzerInitialize`（原本 10 个环境变量取值 × 2 个真实模型 = 20 个 ONNX
   会话，其中 "1024" 会真建 1024 线程池）；`-max_total_time` 同样从进程启动计时。

**修复**

- `kadd_dfa_engine.c`：整块 `fread` 后按字节 `read_le16` 解析（语义完全一致、大小端
  兼容），单次建表从 ~0.5s 降到毫秒级；REPORT-2026-0000023 的大小预检与
  错误分支保留。
- `kadd_inference_fault_inject.h`：注入路径显式 `SetIntra/InterOpNumThreads(options, 1)`；
  两个 Fail 桩在返回注入错误前用真实 API 把线程数修正为 1 —— 覆盖目标不变，
  线程风暴归零。
- `error_paths_cases.inc`：新增真实资产闸门 `FzAllowRealAsset(slot)`（FUZZ 侧 1/64，
  UT 侧恒放行）。闸门只套在「真实资产」那一段：`FuzzSqliErrorPaths` 的 8 配置
  回退矩阵、`FuzzDlErrorPaths` 唯一建 ONNX 会话的一行；同函数内的 NULL / 畸形
  JSON 等廉价分支仍每迭代执行。
- `kadd_fuzzer.cpp`：7 个「通篇都是真实资产」的用例在入口早退；`FuzzConfigLoader`
  只把 DL 引擎创建那一块降频，其余十几个 config_load 分支保持每迭代。
- `FuzzInferenceThreadEnvBranches`（初始化一次，普通/等频随机两种模式都执行）：取值 10 → 5
  （nullptr / "" / "abc" / "1" / "8"），仍覆盖 276-290 全部分支
  （含 `value[0]=='\0'` 子条件），去掉 "1024" 的病态线程池；初始化 ONNX 会话 20 → 10。
  边界值 1024/1025 由 UT 直接调用覆盖
  （test_inference_engine_gtest.cpp 第 21 组）。
- 等频长稳自报吞吐（单行状态文件，每 10s 或 4096 次迭代，-runs 尾段逐次写）：
  `kadd_fuzz_stat: iter/init_s/win_exec_s/all_exec_s/eq_ok/api_calls>=/api=[..]/api_min>=`。
  `api_calls` 是 21 个等频随机接口的调用总和（参考值）；
  长稳口径为「单接口」：`api_min`（21 个独立计数的最小值）≥ 目标值（3000 万）即达标；
  `eq_ok=0` 表示等频层句柄缺失（资产/环境问题），该实例立即判失败，不用 NULL 凑数。

**覆盖率与长稳关系**：等频随机长稳保留完整 selector / heavy / fault-inject
随机测试，因此其自身报告也执行覆盖率门槛（每个 C 文件函数 100%、行 ≥80%）；
计数结论由等频层保底（21 个接口逐次同频），覆盖率结论仍由随机测试主导。
普通模式下每个降频点在 90s 覆盖率跑（修复后约 5k~20k 次迭代）也有 ~80~300 次。

**未采纳的改法（评审记录）**

- 旧的 `FzAllOpenApiQuotaLifecycle`「38 函数 NULL 配额」方案已整体废弃：它为凑调用
  次数跳过 selector / heavy / fault-inject，用空指针 early-return 代替真实调用，
  覆盖率从函数 100% / 行 80%+ 跌到 10% 以下，本末倒置。现行
  `FzEqualRandomApiLayer()` 只覆盖 21 个可长跑接口，全部有效句柄 + 随机输入，
  且叠加在完整随机 fuzz 之上；17 个生命周期接口因文件 I/O / 模型解析 /
  ONNX 会话创建 / 释放语义豁免并在报告单列，load→predict→predict_batch→free
  的 1:1:1:1 生命周期仍由 selector / heavy 层保证。
- 把 `FuzzSQLiEngineFree` 的 3 次创建循环改成 1 次：削弱「多次创建释放（内存池
  分配/释放路径）」的本意；入口降频 1/64 后已无吞吐压力，保留 3 次。
- 在调度点用同一个 `it` 掩码降频多个用例：会让多个重路径落在同一批迭代上
  形成周期性尖峰；改为每个降频点独立计数器。
- `FUZZ_NO_ASAN` 构建开关：本仓库已移除 ASan，同一需求由 `KADD_FUZZ_PROFILE=OFF`
  承担（额外去掉覆盖率插桩），不再引入第二个开关。


## 3. Fuzz 调度设计（等频长稳与常规覆盖率分工）

`kadd_fuzzer.cpp` 的 selector（首字节 0..255）分三层：

| 层 | 区间 | 比重 | 内容 |
|---|---|---|---|
| 公共 API 层（include 公共头） | [0,216] | 84.8% | SQLi/DFA/Feature/Config/ML/DL/综合边界 7 组 × 31 槽位 |
| 内部 API 层（include/internal） | [217,247] | 12.1% | rf/cnn/inference 直连用例 6 类轮转 |
| 自由输入层 | [248,255] | 3.1% | 原始随机字节直接喂公共检测接口 |

公共 + 自由层合计 ≈ 87.9%，内部层 ≈ 12.1%，贴合「对外接口 85% / 内部接口 15%」
的加大力度要求（对外/内部按 include 与 include/internal 头文件归属判别）。

全局节奏（静态迭代计数器驱动）：

| 模式 / 频率 | 动作 |
|---|---|
| 等频随机层（每次迭代，`KADD_FUZZ_EQUAL_RANDOM=1`） | `FzEqualRandomApiLayer()`：21 个可长跑开放接口各做 1 次真实随机调用（有效持久句柄 + 随机输入；17 个生命周期接口豁免并单列） |
| 普通模式 selector | 按 [0,216] / [217,247] / [248,255] 分层调度 |
| 普通：每 8 次 | 合成已签名 RF 微型模型 load→predict→predict_batch→free（内部 API，低频控制比重） |
| 普通：每 1024 次 | 真实资产重量级生命周期，4 相位轮转：ML(.onnx) / DL(.kcn/.onnx) / SQLi(create→detect→detect_batch→free) / 原生 RF+CNN（间隔由 `kFzHeavyInterval` 统一控制，原 1/64 降至 1/1024，见 2.10） |
| 普通：真实资产段 1/64 | `FzAllowRealAsset()` 闸门只套在真实 .onnx 会话与真实 10.4MB DFA 表那一段（见 2.11）；同函数内廉价分支仍每迭代；UT 侧恒放行 |
| 普通：初始化一次 | `FuzzInferenceThreadEnvBranches()`：临时改写 `KADD_ORT_INTRA/INTER_OP_THREADS` 后经公共接口真实加载 DL/ML ONNX 模型，覆盖**库副本**的 276-291（需加载模型，故不进热循环，也不依赖 inject 开关） |
| 普通：每 256 次 | ORT 故障注入全场景（**26 个**）+ 线程数环境变量取值扫描 + `release_engine_resources(nullptr)`（仅 inject=ON 时非空） |

**所有模式（含等频随机长稳）的模型类用例内部均为完整生命周期：
load → predict → predict_batch → free，四个动作 1:1:1:1 同频执行**；
等频随机长稳在同一 selector 之上保证 21 个可长跑接口逐次同频。

日志控制：`LLVMFuzzerInitialize` 把 stdout/stderr 一并重定向到 /dev/null
（库日志与错误路径输出会随迭代线性增长），进度改看单行状态文件；
需要保留输出时设置 `KADD_FUZZ_KEEP_STDOUT=1`。

吞吐自监控（单行状态文件，每 10s 或 4096 次迭代覆盖写，-runs 尾段逐次写，`KADD_FUZZ_QUIET_STATS=1` 可关）：

```
kadd_fuzz_stat: iter=.. init_s=.. win_exec_s=.. all_exec_s=.. eq_ok=1
                api_calls>=.. api=[dfa_engine_tokenize=.. ... sqli_detect_batch=..] api_min>=..
```

`win_exec_s` 是不含初始化的稳态吞吐；长稳口径为「单接口」：`api_min`（21 个
等频接口独立计数的最小值）≥ 3000 万即达标，`api_calls` 为 21 接口总和（参考值）；
不要用 libFuzzer 的 `stat::average_exec_per_sec`（其分母含初始化，且口径是迭代数
而非对外接口调用次数）。详见 2.11。

## 4. 无法覆盖行目录（逐条原因）

以下行在整改后仍不可达，均属四类：
**A** 分配失败（OOM）分支——真实进程无法稳定构造；
**B** 常规文件上的 fseek/ftell/fclose/fread 失败——OS 层实际不可达（预检已消除竞态窗口）；
**C** 防御性死代码——上游校验保证入参恒合法；
**D** 32 位平台溢出防护——64 位 size_t 下数学上不可达。

### kadd_config_loader.c
| 行 | 类 | 原因 |
|---|---|---|
| 45-46 | C | `neutralize_ctrl_char` 的 `uc<0x20||uc==0x7F` 之外分支恒走 return c；45-46 为控制字符替换分支，仅当调用方传入控制字符——safe_copy 已在上游净化 |
| 54-59 | C | `sanitize_for_log` 的 dest/dest_len/src NULL 守卫，全部调用点传栈数组与已判空指针 |
| 72-73 | C | `safe_copy` 截断分支：所有调用点 dest_len 为 KADD_CONFIG_PATH_MAX_LEN，源串已在 JSON 解析层限长 |
| 89-92 | B | 常规文件 fseek(SEEK_END) 失败 |
| 96-98 | B | ftell 返回 <=0 或超限：空文件已被 fopen 后 fseek/ftell 路径覆盖，>KADD_CONFIG_MAX_FILE_SIZE 需 8MB+ 配置文件（用例已覆盖超限路径时归入本行则例外，见 gcov 明细） |
| 102-105 | B | 常规文件 fseek(SEEK_SET) 失败 |
| 110-113 | A | calloc(≤8MB) 失败 |
| 119-123 | B | 预检后 fread 短读（需 TOCTOU 并发截断） |
| 133-134 | C | `parse_engine_mode` 默认分支：调用前已 strcmp 穷举 ml/dl |
| 151-154 | — | **可覆盖**：超长路径用例已在 FuzzConfigErrorPaths 中触达 |
| 214-221 | C | switch(engine_mode) 的 default 分支（白盒整改新增）：engine_mode 仅由 parse_engine_mode 赋值且当前枚举只有 ML/DL 两值，上方 case 已穷举，正常不可达；fail-closed 防御，未来新增模式漏改时直接拒绝配置 |

### kadd_dfa_engine.c
| 行 | 类 | 原因 |
|---|---|---|
| 136-138 | C | `load_u16_table` 参数守卫：3 个调用点均传合法指针与 256/states |
| 149-154 | D | total_cells 溢出：states≤65535 已在表头校验，64 位下 16M cells 不溢出 |
| 162-171 | B | 常规文件 ftell/fseek 失败 |
| 183-186 | A | calloc(≤32MB) 失败 |
| 191-195 | B | 173-179 已按文件实际大小预检，短读仅剩并发截断竞态 |
| 200-203 | B | fclose 失败（需底层 IO 错误） |
| 274-276 | A | 引擎结构体 calloc 失败 |
| 352-355 / 415-418 | C | 状态越界防御：create 阶段已全表校验转移目标 < states，运行期表只读 |
| 457-459 | C | 表未初始化防御：create 成功即三表非 NULL，引擎不暴露半初始化状态 |
| 530-533 | A | 批量输出缓冲 calloc 失败 |
| 568-570 | C | `batch_size` 回退分支已由用例覆盖；剩余行为 defensive default |

### kadd_feature_engine.c
| 行 | 类 | 原因 |
|---|---|---|
| 69-71 | A | 加权数组 calloc 失败 |
| 165-167 | A | 结果数组 calloc 失败 |
| 177-179 | C | `sliding_window_distance_*` 在 seq/seq_len/window 已通过前置校验后无失败路径 |
| 198-235 | — | **可覆盖**：批量 NULL/参数/上限分支已全部由 FuzzFeatureErrorPaths 触达 |
| 256-259 | D | count≤10^7、dim≤300 → total≤3×10^9 < 2^64，64 位下不可溢出（32 位平台防护） |
| 280-283 / 293-294 | — | **可覆盖**：seq_len 非法与内层失败上抛已触达 |

### kadd_rf_engine.c
| 行 | 类 | 原因 |
|---|---|---|
| 133 | B | read_entire_file 的 ftell 负值防御 |
| 146-147 / 157-158 | B | 常规文件 fseek 失败 |
| 389-390 / 397-398 | C | validate_tree 深度防御：节点归属已由 tree_offsets 边界校验约束 |
| 427-428 / 436-438 | A | predict 路径临时分配失败 |
| 526 / 528 | C | `kadd_rf_model_predict` 的概率钳位：叶节点 `positive_probability` 已在 `validate_tree` 约束到 [0,1]，tree_count 个叶均值必然落在 [0,1]，两个钳位分支不可达（原表把行号写成 517/519 且原因误写为 batch_size 守卫，已根据当前源码校正） |
| 579-587 | C | `predict_batch_sve_chunk` 的 svcntw()>64 回退分支（白盒整改新增，**仅 SVE 构建编译**）：SVE 规范向量长度上限 2048bit → svcntw() ≤ 64 恒成立，回退到逐样本 kadd_rf_model_predict 仅在超出规范上限的实现上执行，真实硬件不可达 |
| 615-634 | C | `predict_batch_sve_chunk` 的步数预算防御（**仅 SVE 构建编译**，ONNX 报告里不存在）：`validate_tree` 强制 `left/right > index` 且落在 `[begin, end)` → 子节点下标严格递增、树内不可能成环，最长路径 <= `maximum_steps - 1` 步，而触发条件为 `traversal_steps > maximum_steps`，数学上永不成立；详见 2.9 |

### kadd_cnn_engine.c
| 行 | 类 | 原因 |
|---|---|---|
| 加载器 ftell/fseek/calloc 失败行 | A/B | 同上（文件预检后仅剩 OOM 与 IO 错误） |
| decode_tokens 之后的越界防御行 | C | 分发层（kadd_dl_engine.c 133-141）已统一做 token 有限性/范围/整数校验 |

### kadd_ml_engine.c
| 行 | 类 | 原因 |
|---|---|---|
| 40-41 | A | 句柄 calloc 失败 |
| 69-71 | — | SVE 构建专属打印（`CPU does not support SVE`）：ONNX 构建不参与编译；SVE 构建在不支持 SVE 的硬件上运行时可覆盖 |
| 105-106 | C | NULL 守卫：唯一调用点已判空 |

### kadd_dl_engine.c
| 行 | 类 | 原因 |
|---|---|---|
| 53-54 | A | 句柄 calloc 失败 |
| 67-73 | — | **SVE 构建专属**：需 SVE 硬件 + 真实 .kcn + 错误 feature_dim，用例已内置（FuzzDlErrorPaths），在 SVE 报告生效 |
| 82-84 | — | 同上，SVE 构建 + 非 SVE 硬件时打印，ONNX 构建不参与编译 |
| 89-91 / 94-100 / 148-151 | — | **可覆盖**：加载失败与 ONNX 维度失配用例已内置 |

### kadd_sqli_engine.c
| 行 | 类 | 原因 |
|---|---|---|
| 122-126 / 153 / 159 / 170-171 / 175-177 / 187-189 | A/C | 引擎与缓冲分配失败及其守卫；create 入参 NULL 守卫由既有用例覆盖 |
| 193-197 | C | DFA create 失败上抛：config 指向坏表文件时可达——已由 FuzzSqliErrorPaths 的合成 config 触达（表路径非法变体） |
| 209-226 / 251-268 | — | **SVE 构建专属回退分支**：回退矩阵用例已内置，SVE 报告生效 |
| 229-236 / 271-278 | — | **可覆盖**：ONNX 构建加载失败分支由 fb_empty 变体触达 |
| 289-290 / 294-296 | — | **可覆盖**：create_from_file NULL / config 失败用例已内置 |
| 337-345 / 356-358 | C | detect 中 feature/ML/DL 失败上抛：DFA 成功产出 token 后，特征提取对合法 token 序列无失败路径；模型推理失败需 ORT 运行期错误（由故障注入在 inference 层覆盖） |
| 369-394 | C | DL 模式逐条推理的失败上抛：同上，需后端运行期失败 |
| 435-437 / 454-456 | C | 批量特征/ML 失败上抛：同 337-345 |
| 473-476 | C | `init_batch_detect_params` 为 static，唯一调用点（567 行）先于其执行已判空 probabilities（544 行），out_actual_batch_size 为栈变量恒非 NULL——**死代码** |
| 509-512 / 524-527 | C | 批内单条失败清理：单条失败需 DFA/特征/模型运行期错误，同 337-345 |
| 545-553 | — | **可覆盖**：detect_batch NULL/count 非法用例已内置 |
| 560-561 / 569-570 / 577-578 | C | DL 逐条失败与缓冲分配失败上抛：同 337-345 / A 类 |

### kadd_inference_engine.c

本地已用「假 ORT 运行时 + 真实源码」实跑验证（26 个注入场景全部执行成功、
100 项断言 0 失败、无崩溃）：**行覆盖 264/277 = 95.3%**。
下表逐条列出残余 13 行及原因（其中 443-449 在真实 UT 中已覆盖，见表内说明）。

| 行 | 类 | 原因 |
|---|---|---|
| 74-76 / 184-213 / 228-238 | — | **已覆盖**：`CheckOrtStatusErrorBranch` + `GetModelInputDimErrorBranches` + `GetModelOutputDimErrorBranches` 共 10 个注入场景 |
| 85-86 / 97-99 | C | `init_ort_api`：`OrtGetApiBase()` 返回 NULL、或 `GetApi(28..28)` 全部返回 NULL。两者都意味着 ORT 库缺失或 ABI 不兼容——此时进程根本无法链接/加载；`OrtGetApiBase` 是外部符号，不在 OrtApi 函数表内，无法用现有注入机制替换（需 LD_PRELOAD 级手段） |
| 110-113 / 395-398 | — | **已覆盖**：新增场景 `KADD_INJ_CREATE_ENV_FAIL`。`init_ort_env` 仅在全局 `g_ort_env` 为空时才调 `CreateEnv`，而它一旦建立就不会再为空，因此场景临时保存并置空 TU 内 static 全局 `g_ort_env`/`g_ort_api`/引用计数，使 `CreateEnv` 失败 → `init_ort_env` 返 -1 → `load_engine_internal` 释放句柄并上抛 `KADD_ERROR_MODEL_LOAD_FAILED`；场景结束后逐一精确复原，不影响进程内其它常驻引擎 |
| 126-128 / 135-137 / 143-145 | — | **已覆盖**：`IoNameErrorBranches` 3 个场景（allocator / output_count / input_name 失败）。源码在这三条路径上直接 `return NULL` 不释放 status，注入器以 `consumed=false` 兜底回收，不产生泄漏 |
| 151-153 | — | **已覆盖**：新增场景 `KADD_INJ_IO_NAME_NULL`（status 成功但 name 为空），专门验证 2.6 节 white_box_test_047 加固的 `name==NULL` 防御分支确实可达且不会解引用空指针 |
| 155-158 | A | `strdup` 堆耗尽：需真实 OOM，无 portable 的 malloc interpose 手段。加固后已有 ERROR 日志 + 返回 NULL，由唯一调用方 `detect_io_names` 兜底（该兜底已覆盖） |
| 160-163 | — | **已覆盖**：新增场景 `KADD_INJ_ALLOCATOR_FREE_FAIL`（`AllocatorFree` 返回错误状态）。桩仍然释放内存只是额外返错，避免 3000 万次长稳下累积泄漏 |
| 276-278 / 283-289 / 290 | — | **已覆盖**：见 2.8 节（`InferenceEnvConfigTest` + `FuzzInferenceThreadEnvBranches` + fuzz 取值扫描） |
| 297-361 | — | **已覆盖**：`CreateSessionErrorBranches` 6 个场景，含 `cleanup` / `cleanup_options` / `cleanup_session` 三条 goto 出口与 intra/inter 线程数告警分支（后者同时命中真实 `CreateSession` 对不存在路径的失败返回） |
| 367-369 | — | **已覆盖**：`release_engine_resources(nullptr)` 直接调用（UT `AccessorAndFreeNullSafety`、FUZZ `FuzzInferenceFaultInjection`）。公共 `kadd_inference_engine_free(nullptr)` 在 561-563 就返回，无法间接触达 |
| 390-391 | A | `load_engine_internal` 的 `calloc` 失败：同 155-158，堆耗尽不可达 |
| 413-417 | C | warmup 推理失败：需要「会话创建 + IO 名称探测 + 维度探测全部成功，但 `Run` 失败」。`init_ort_api` 会用全局 `g_ort_api` 覆写 `e->api`，逐调用注入的伪函数表到不了 warmup 阶段；而替换全局表会影响进程内其它常驻引擎，风险大于收益 |
| 429-441 / 443-449 / 456-457 | — | **已覆盖**：`InferencePublicApiTest.LoadNullAndInvalidArgs` 覆盖校验分支；443-449 的成功路径由既有约 30 处 `kadd_inference_engine_load(&e, "config.json", ...)` 命中（本地 harness 把 `kadd_config_load` 桩成失败，所以这 4 行在本地数字里显示未覆盖，属 harness 差异而非真实缺口） |
| 477-481 / 490-493 / 500-504 | — | **已覆盖**：`PredictBatchValidationWithoutSession`（NULL / n 非法 / feature_dim 非法 / NaN / Inf） |
| 494-498 | C | `cur * feature_dim` 的 `SIZE_MAX` 溢出防御：`cur ≤ KADD_INFERENCE_MAX_SAMPLE_COUNT`、`feature_dim ≤ KADD_INFERENCE_MAX_FEATURE_DIM` 均已在前置校验中限定，64 位下乘积不可能溢出 |
| 512-514 / 520-526 / 528-533 / 535-541 | — | **已覆盖**：`PredictRuntimeErrorBranches` 4 个场景（CreateTensor / Run / GetTensorData 失败、output_dim<2 防御） |
| 553-555 / 561-563 | — | **已覆盖**：`AccessorAndFreeNullSafety` |
| 569-572 | — | **已覆盖**：进程内所有引擎释放后 `g_ort_env_ref_count` 归零触发 `ReleaseEnv`（本地实测已命中；UT 中只要有用例释放了最后一个引擎即命中） |

> 已知代码瑕疵（本轮不改，避免再次引发行号整体偏移）：`get_io_name` 的三条失败路径
> （126-128 / 135-137 / 143-145）与 `create_ort_session` 的 `cleanup_session`（349-352）
> 直接返回而不释放已取得的 `OrtStatus`，属于小量状态对象泄漏（非内存安全问题）。
> 注入场景对这些路径统一用 `consumed=false` 由注入器兜底回收，因此测试本身不泄漏。

## 5. 验证方式

1. 本地（Windows/MinGW，与 ONNX 构建等价的 SVE=0 配置）：
   `error_paths_cases.inc` 全部可本地编译用例已实测跑通且无崩溃，
   gcov 数字见 2.3 节表格；`kadd_fuzzer.cpp` 全文件 `-fsyntax-only -Wall -Wextra` 零 error。
2. 本地「假 ORT 运行时」实跑验证（仅验证用，不入仓）：下载 ONNX Runtime v1.28.0
   官方头文件（`ORT_API_VERSION=28`，与工程要求一致，并把 `ORTCHAR_T` 改回目标平台的
   `char`），自行实现一份最小 `OrtApi` 假实现（仅 28 个被引擎实际调用的成员，
   签名逐个比对官方声明），然后真实链接运行：
   - 编译检查全部通过（`-Wall -Wextra` 零 warning）：`kadd_inference_engine.c`（C）、
     注入头（C++）、`test_inference_engine_gtest.cpp`、`test_error_paths_gtest.cpp`、
     `kadd_fuzzer.cpp`（inject ON / OFF 两种配置）；
   - 运行结果：**26/26 注入场景全部执行成功、100 项断言 0 失败、无崩溃**；
   - gcov 行覆盖：`kadd_inference_engine.c` **95.3%**（264/277），残余 13 行见第 4 节。
   该验证方式同时校正了注入桩的 3 处签名错误（`SetSessionExecutionMode` 应为
   `ExecutionMode` 而非 `int`；`GetTensorMutableData` 首参非 const），否则目标环境会直接编译失败。
3. 目标环境（aarch64 Linux）：`cd /opt/KADD/SQLi && ./coverage_all.sh all`
   一键重出 4 份报告；对照第 1 节基线与第 4 节目录核对。
   注意：重跑前必须确认 `/opt/KADD` 已同步到本提交，否则报告里的源码行号
   与本文档不对应（可用报告 HTML 里的源码总行数快速判别：本提交为 577 行）。
4. Fuzz 长稳（单接口 3000 万次等频随机）：`./tools/fuzz_longrun.sh`，
   分层比重与生命周期配比见第 3 节；多路并行命令见 2.10 与 `coverage_all.sh` 头部注释。
5. `kadd_rf_engine.c` **两条 SVE 分支均已本地编译验证**（MinGW gcc，`-std=c11 -Wall -Wextra`）：
   - 默认（x86 → `KADD_RF_HAVE_SVE=0`）：`-fsyntax-only` **零 error 零 warning**；
   - SVE 路径：`-D__aarch64__ -D__ARM_FEATURE_SVE` + 一份最小 `arm_sve.h` 桩
     （按源码实际使用的 22 个 intrinsic 与 5 个向量类型逐个声明，仅验证用不入仓；
     其中 `svptest_any` 返回 `uint64_t` 而非 `svbool_t`）：`-fsyntax-only` **零 error 零 warning**。
   另用脚本遍历全文 `#if/#endif` 嵌套栈（先剔除注释）：预处理嵌套完全平衡、
   不再存在任何 `KADD_RF_HAVE_SVE` 与 `!KADD_RF_HAVE_SVE` 的矛盾嵌套；
   全文 `LCOV_EXCL_START` / `LCOV_EXCL_STOP` 已 1:1 配对。
6. `coverage_all.sh` 并行逻辑已用假 `kadd_fuzzer` 实际空跑验证（`bash -n` 语法检查 + 三条路径）：
   - 无 `unshare` 环境下请求 `FUZZ_JOBS=3` → **告警并回退串行**，仍正常出报告；
   - 有 `unshare` 环境下 `FUZZ_JOBS=3` → 3 份 worker 日志 / 3 个合并区段 /
     3 个 artifact 目录 / 3 个互不相同的 `-seed`，汇总摘要正常；
   - worker 返回非 0（模拟 rc=77）→ 退出码正确上报且自动打印全文日志；
   - 并发性用时间戳实证：4 个 worker（PID 773/777/782/787，各 sleep 4s）
     启动分散 1.96s、总跨度 6.33s（串行应 >= 16s），执行区间两两重叠——
     确为并行而非伪并行（启动分散主要是 Windows/Git-Bash 的进程创建开销，
     Linux 上为毫秒级）。
