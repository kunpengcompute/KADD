# 常见问题解答

本文档收集了KADD PI特性使用过程中的常见问题和解决方案。

---

## 安装部署相关

### Q1：缺少动态链接库怎么办？

**问题**：在鲲鹏920处理器下，环境部署好后新开终端并手动激活虚拟环境，运行程序出现缺少动态链接库的错误。

**解决方案**：

将加速库添加进环境变量：

```bash
export LD_LIBRARY_PATH=项目路径/acceleration_library:$LD_LIBRARY_PATH
```

或使用部署脚本时以 `source` 方式执行（会自动配置环境变量）：

```bash
source ./deploy/deploy.sh
```

---

### Q2：缺少 _ctypes 模块怎么办？

**问题**：运行程序时提示 `ModuleNotFoundError: No module named '_ctypes'`。

**解决方案**：

这是因为Python安装不完整。需要检查系统中是否存在 `libffi-devel`，并在Python源码编译时添加 `--with-system-ffi` 选项：

```bash
# 安装 libffi-devel（openEuler）
sudo yum install -y libffi-devel

# 重新编译 Python 时添加 --with-system-ffi
./configure --with-system-ffi
make && make install
```

---

### Q3：缺少系统库（GLIBC）怎么办？

**问题**：运行程序时提示 `version GLIBC_XXX not found` 等系统库版本不匹配的错误。

**解决方案**：

系统的 `ldd` 版本低于加速库编译时的 `ldd` 版本。由于加速库的限制，无法启用鲲鹏加速。可手动下载标准版 `onnxruntime` 并运行未加速的版本：

```bash
pip install onnxruntime==1.19.2
```

---

### Q4：Python 版本不匹配怎么办？

**问题**：部署脚本提示 Python 版本不匹配。

**解决方案**：

不同鲲鹏芯片型号需要不同的Python版本：

| 鲲鹏芯片型号      | 所需Python版本 |
| ----------- | ---------- |
| 鲲鹏920处理器    | 3.9        |
| 鲲鹏920新型号处理器 | 3.9        |
| 鲲鹏950处理器    | 3.10       |

使用 `-p` 参数指定正确版本的 Python：

```bash
source ./deploy/deploy.sh -p /opt/python3.9/bin/python3.9
```

---

## 使用相关

### Q5：如何判断检测结果？

**问题**：`kp_detect` 返回的结果如何解读？

**解决方案**：

检测结果为字典格式，`result` 字段的值含义如下：

| result值 | 含义              | 常量                          |
| ------- | --------------- | --------------------------- |
| 1       | 越狱文本（JAILBREAK） | `KunpengConstant.JAILBREAK` |
| 0       | 正常文本（BENIGN）    | `KunpengConstant.BENIGN`    |

```python
res = kp_detect(kp, prompt)
if res["result"] == 1:
    print("检测到越狱文本")
else:
    print("正常文本")
```

---

### Q6：黑白名单和输入文本有哪些限制？

**问题**：黑白名单和输入文本是否有长度或数量限制？

**解决方案**：

| 限制项         | 限制值     | 超限行为                         |
| ----------- | ------- | ---------------------------- |
| 黑/白名单条目数量   | 各 500 条 | 新增记录被跳过，输出警告日志               |
| 黑/白名单单条记录长度 | 8500 字符 | 抛出 `KpUpdateException` 异常    |
| 待检测文本长度     | 8500 字符 | 抛出 `KpDetectionException` 异常 |

---

### Q7：黑白名单的匹配方式是什么？

**问题**：黑白名单是精确匹配还是模糊匹配？

**解决方案**：

黑白名单使用 **MinHash LSH** 近似匹配算法，相似度阈值为 0.8。文本首先经过 jieba 中文分词，然后计算 MinHash 签名进行近似匹配。这意味着高度相似的文本也会被匹配到。

---

### Q8：检测实例可以跨线程使用吗？

**问题**：多个线程能否共享同一个检测实例？

**解决方案**：

建议每个线程创建独立的检测实例，避免并发问题：

```python
# 每个线程创建独立实例
logger = logging.getLogger()
kp = kp_detector_initialization(logger)
```

---

## 性能相关

### Q9：如何获得最佳检测性能？

**问题**：如何降低检测延迟？

**解决方案**：

使用 `numactl` 绑定NUMA节点：

```bash
numactl --cpunodebind=0 --membind=0 python 脚本名称.py
```

此外，检测实例在初始化时会进行模型预热（warmup），首次检测会比后续检测稍慢。

---

## 模型训练相关

### Q10：如何自定义训练模型？

**问题**：如何用自己的数据集训练新的检测模型？

**解决方案**：

1. 准备训练数据，格式为JSON，存放在 `train/dataset/train_data.json`
2. 下载 Chinese-lert-small 预训练模型，放置在 `train/pretrained_model/chinese-lert-small/`
3. 修改 `train/config/config.json` 中的训练参数
4. 运行训练脚本：

```bash
python train/train.py
```

训练完成后会自动导出ONNX模型并进行INT8量化。将生成的 `train/onnx_model/clf_model.onnx` 复制到 `algorithm/knowledge_base/model_file/` 替换原有模型。

**训练参数说明**：

| 参数          | 默认值  | 说明       |
| ----------- | ---- | -------- |
| epochs      | 15   | 训练轮数     |
| lr          | 2e-5 | 学习率      |
| batch_size  | 8    | 批次大小     |
| max_length  | 512  | 最大序列长度   |
| warmup_rate | 0.03 | 预热比例     |
| dropout     | 0    | Dropout率 |
