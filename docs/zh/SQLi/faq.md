# 常见问题解答

本文档收集了KADD引擎使用过程中的常见问题和解决方案。

---

## 安装相关

### Q1：运行时提示找不到库文件怎么办？

**问题**：`error while loading shared libraries: libkadd_sqli.so: cannot open shared object file`

**解决方案**：

- 临时解决方案

```bash
export LD_LIBRARY_PATH=/opt/KADD/SQLi/build/src:/opt/KADD/SQLi/util/onnxruntime-linux-aarch64-1.16.0/lib:$LD_LIBRARY_PATH
```

- 永久解决方案（添加到 ~/.bashrc）- 不推荐

```bash
echo 'export LD_LIBRARY_PATH=/opt/KADD/SQLi/build/src:/opt/KADD/SQLi/util/onnxruntime-linux-aarch64-1.16.0/lib:$LD_LIBRARY_PATH' >> ~/.bashrc
source ~/.bashrc
```

---

## 使用相关

### Q2：如何设置检测阈值？

**问题**：如何调整SQL注入检测的敏感度？

**解决方案**：

```c
float probability = 0.0f;
kadd_sqli_detect(engine, input, &probability);

// 根据场景选择阈值
if (probability >= 0.5f) {      // 标准阈值
    // 检测到注入
} else if (probability >= 0.3f) {  // 严格模式
    // 可能是注入
} else {
    // 安全输入
}

```

### Q3：引擎实例可以跨线程使用吗？

**问题**：多个线程能否共享同一个引擎实例？

**解决方案**：

不可以。引擎实例**不是线程安全的**。每个线程应创建独立的引擎实例：

```c
// 每个线程创建独立实例
kadd_sqli_engine_t *engine = NULL;
kadd_sqli_engine_create_from_file(&engine, "config.json");

// 使用引擎...

// 线程结束时清理
kadd_sqli_engine_free(engine);
```

---

## 工具相关

### Q4：tools 目录是什么？可以用于生产吗？

**问题**：`tools/` 目录下的工具可以用于生产环境吗？

**解决方案**：

**⚠️ 重要声明**

`tools/` 目录包含**模型和数据生成工具**，用于：

- 训练和生成检测模型（`tools/model_train/`）
- 生成 DFA 状态转移表（`tools/dfa_compile/`）

**这些工具不保证**：
- 代码质量符合生产标准
- 无安全漏洞
- 与主项目代码规范一致

它们是**开发过程中的临时脚本**，可能未经过充分测试和审查。如需用于生产环境，请自行评估安全性并进行必要的加固。

**示例：重新训练模型**

```bash
# 1. 准备训练数据
cp your_custom_dataset.csv assets/data/

# 2. 运行训练脚本（⚠️ 谨慎执行）
python tools/model_train/train_tokens.py \
    --input assets/data/your_custom_dataset.csv \
    --output assets/model/model_tokens_rf.onnx

# 3. 验证新模型
./build/kadd_sqli_demo
```

**推荐做法**：
- 仅在开发/测试环境使用这些工具
- 生产环境使用预编译的模型和数据文件
- 如需自定义模型，请在隔离环境中进行训练和验证
