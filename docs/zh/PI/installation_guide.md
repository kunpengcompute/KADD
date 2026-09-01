# 安装指南

本指南说明 KADD PI（越狱文本检测）特性的环境部署和依赖安装步骤。

## 关于 KADD

KADD 是一个安全检测工具集，通过根目录的 `build.sh` 一键构建所有特性。

| 特性   | 说明       | 状态  |
| ---- | -------- | --- |
| SQLi | SQL 注入检测 | 已实现 |
| PI   | 越狱文本检测   | 已实现 |

## 环境要求

### 已验证环境

处理器为鲲鹏920处理器，操作系统为openEuler 24.03 LTS。

### 鲲鹏亲和环境要求

| 鲲鹏芯片型号      | Python版本 | ONNX Runtime           |
| ----------- | -------- | ---------------------- |
| 鲲鹏920处理器    | 3.9      | onnxruntime_acl-1.19.2 |
| 鲲鹏920新型号处理器 | 3.9      | onnxruntime_acl-1.19.2 |
| 鲲鹏950处理器    | 3.10     | onnxruntime-1.22.2     |
| 其他          | 不限       | onnxruntime==1.19.2    |

### 软件依赖

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

---

## 环境部署

### 一键式部署（推荐）

PI特性支持环境一键式部署，在目标机器上运行仓库内的部署脚本（PI/depoly/deploy.sh）：

```bash
cd /opt/KADD/PI
source ./deploy/deploy.sh
```

部署脚本会自动完成以下操作：

1. 检测鲲鹏CPU型号，确定所需Python版本
2. 创建Python虚拟环境
3. 安装Python依赖包
4. 安装对应CPU的ONNX Runtime加速包
5. 配置加速库动态链接路径
6. 执行功能测试验证部署

出现部署完成文本后可直接输入 `python test_kp_detect.py` 进行单条文本测试。

### 部署选项

| 指令                                                                                   | 说明                   |
| ------------------------------------------------------------------------------------ | -------------------- |
| source ./deploy/deploy.sh -h                                                         | 查看帮助                 |
| source ./deploy/deploy.sh -p /opt/python3.9/bin/python3.9                            | 指定 Python 可执行文件路径或命令 |
| source ./deploy/deploy.sh -e /opt/python_venv                                        | 指定已有虚拟环境路径           |
| source ./deploy/deploy.sh -d /data/envs                                              | 指定虚拟环境创建目录           |
| source ./deploy/deploy.sh --recreate-ven                                             | 如果目标虚拟环境已存在，则删除后重新创建 |
| source ./deploy/deploy.sh --pip-index-url `https://pypi.tuna.tsinghua.edu.cn/simple` | 指定 pip 镜像源（默认阿里云）    |
| source ./deploy/deploy.sh --proxy `http://127.0.0.1:7890`                            | 设置统一代理用于下载依赖         |
| source ./deploy/deploy.sh -s                                                         | 部署完成后不执行功能测试         |

### 部署选项速查表

| 选项                         | 说明                     | 使用场景           |
| -------------------------- | ---------------------- | -------------- |
| `-p, --python`             | 指定 Python 可执行文件路径或命令   | 系统有多个Python版本时 |
| `-d, --venv-dir`           | 指定虚拟环境创建目录             | 自定义虚拟环境位置      |
| `-e, --venv`               | 指定已有虚拟环境路径             | 复用已有虚拟环境       |
| `-r, --requirements`       | 指定 requirements.txt 路径 | 自定义依赖文件        |
| `--recreate-venv`          | 删除已有虚拟环境后重新创建          | 需要全新环境时        |
| `--pip-index-url`          | 指定 pip 镜像源             | 加速依赖下载         |
| `--pip-trusted-host`       | 指定 pip trusted-host    | 镜像源需要信任主机时     |
| `--proxy`                  | 设置统一代理                 | 通过代理下载依赖       |
| `-s, --skip-function-test` | 跳过功能测试                 | 批量部署时          |
| `-h, --help`               | 查看帮助                   | 查看可用选项         |

---

## 性能优化命令

部署完成后，使用以下命令运行脚本以达到最大性能：

```bash
# 运行命令示例
numactl --cpunodebind=0 --membind=0 python 脚本名称.py
```

`numactl` 将进程绑定到NUMA节点0的CPU和内存上，减少跨节点访问延迟。
