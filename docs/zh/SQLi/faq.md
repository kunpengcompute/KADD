# 常见问题解答

本文档收集了KADD引擎使用过程中的常见问题和解决方案。

## 安装相关

### 运行时提示找不到库文件

**问题**

`error while loading shared libraries: libkadd_sqli.so: cannot open shared object file`

**回答**

使用临时解决方案：

```bash
export LD_LIBRARY_PATH=/opt/KADD/SQLi/build/src:$LD_LIBRARY_PATH
```

使用永久解决方案，将环境变量添加到 `~/.bashrc`。此方式不推荐：

```bash
echo 'export LD_LIBRARY_PATH=/opt/KADD/SQLi/build/src:$LD_LIBRARY_PATH' >> ~/.bashrc
source ~/.bashrc
```

## 使用相关

### 如何设置检测阈值

**问题**

如何调整SQL注入检测的敏感度？

**回答**

阈值含义如下：

1. 概率大于等于 `0.5f` 时，按标准阈值判定为SQL注入。
2. 概率大于等于 `0.3f` 且小于 `0.5f` 时，在严格模式下判定为可能是SQL注入。
3. 其他输入判定为安全输入。

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

### 引擎实例可以跨线程使用吗

**问题**

多个线程能否共享同一个引擎实例？

**回答**

不可以。引擎实例**不是线程安全的**。每个线程应创建独立的引擎实例：

每个线程按以下顺序使用引擎：

1. 创建独立的引擎实例。
2. 使用引擎执行检测。
3. 线程结束时清理资源。

```c
kadd_sqli_engine_t *engine = NULL;
kadd_sqli_engine_create_from_file(&engine, "config.json");

const char *input = "id=1";
float probability = 0.0f;
kadd_sqli_detect(engine, input, &probability);

kadd_sqli_engine_free(engine);
```

### ML模式和DL模式的区别与选择

**问题**

`config.json` 中 `engine_mode` 设为 `ml` 和 `dl` 有什么区别？

**回答**

| 对比项 | ML模式 | DL模式 |
|--------|--------|--------|
| 模型类型 | 随机森林（Random Forest） | 卷积神经网络（TextCNN） |
| 输入特征 | 28维手工特征（滑窗统计） | 300维Token ID序列 |
| 适用场景 | 低延迟、资源受限环境 | 追求更高检测准确率 |

**配置切换**：修改 `config.json` 中的 `engine_mode` 字段即可：
将该字段设置为 `ml` 或 `dl`。

```json
{
  "engine_mode": "ml"
}
```

无需重新编译，下次创建引擎时生效。

**模型加载策略**：两种模式均采用相同策略。SVE可用时优先加载原生二进制格式（`.krf` / `.kcn`），失败则自动回退ONNX Runtime加载`.onnx`格式。

### SVE加速如何工作

**问题**

编译时启用了 `-DENABLE_ARM_SVE=ON`，但运行时提示SVE不可用。

**回答**

KADD的ML和DL引擎均采用**自动回退**策略：

1. 编译时通过 `-DENABLE_ARM_SVE=ON`（默认开启）启用SVE编译路径
2. 运行时检测SVE可用性（通过 `kadd_sve.h` 中的宏判断）
3. SVE可用时，加载原生二进制模型（`.krf` / `.kcn`），利用SVE指令加速
4. SVE不可用时，自动回退ONNX Runtime加载`.onnx`模型
