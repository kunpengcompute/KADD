# 安装指南

本指南说明 KADD PI（越狱文本检测）特性的环境部署和依赖安装步骤。

分为两个章节，章节一介绍如何编译onnxruntime库，章节二介绍如何部署PI特性。

# 1、编译加速库

## 编译环境要求

本小节说明部署PI特性加速库的环境依赖和编译步骤，加速库分为两个版本，分别为供鲲鹏920处理器、鲲鹏920新型号处理器使用的加速库以及供鲲鹏950处理器使用的加速库，下文简写为加速库20和加速库50。

### 编译环境

|                     | 加速库20已验证版本                                                                  | 加速库50已验证版本                                      |
| ------------------- | --------------------------------------------------------------------------- | ----------------------------------------------- |
| 处理器                 | 鲲鹏920处理器                                                                    | 鲲鹏920新型号处理器                                     |
| 操作系统                | openEuler 24.03 LTS                                                         | openEuler 24.03 LTS                             |
| cmake               | 3.27.9                                                                      | 3.28.1                                          |
| make                | 4.3                                                                         | 4.4.1                                           |
| gcc                 | 10.3.1                                                                      | 12.3.1                                          |
| python              | 3.9.9                                                                       | 3.10.20                                         |
| python三方库           | numpy==2.0.2,packaging==26.3,SCons==4.11.1,wheel==0.48.0,setuptools==59.4.0 | numpy==2.2.6,packaging==26.2,setuptools==79.0.1 |
| onnxruntime         | 1.19.2                                                                      | 1.22.2                                          |
| Arm Compute Library | 23.08                                                                       | \                                               |
| KONNX               | 1.1.0                                                                       | \                                               |

### 编译加速库

请注意：默认当前路径为**/path**

#### 加速库20

**安装环境**

1. 确认所使用的处理器
   
   ```bash
   lscpu #可查看BIOS Model name字段
   ```

2. 安装处理器对应编译环境中所需的cmake、make、gcc、python以及python三方库

**安装Arm Compute Library**

下载arm compute library的源码,下载好后解压并进入该文件夹/ComputeLibrary-23.08

```bash
wget -q https://github.com/ARM-software/ComputeLibrary/archive/refs/tags/v23.08.zip -O ComputeLibrary-23.08.zip
unzip ComputeLibrary-23.08.zip && cd ComputeLibrary-23.08
```

使用如下命令进行编译

```bash
scons Werror=1 -j8 debug=0 neon=1 opencl=0 os=linux arch=armv8a build=native
```

**安装KONNX**
按照鲲鹏社区指引，先安装[SRA_Inference](https://www.hikunpeng.com/document/detail/zh/boostsra/srainference/SRA_Inference/kunpengsra_inference_16_0006.html)

```bash
wget -q https://kunpeng-repo.obs.cn-north-4.myhuaweicloud.com/Kunpeng%20BoostKit/Kunpeng%20BoostKit%2026.1.RC1/BoostKit-boostsra-inference_1.2.0_RC1.zip -O sra_inference.zip
unzip sra_inference.zip
rpm -ivh boostsra-inference-1.2.0-RC1.aarch64.rpm
source /etc/profile
```

然后安装打入KONNX补丁的onnxruntime源码，具体见[参考文档](https://www.hikunpeng.com/document/detail/zh/boostsra/srainference/SRA_Inference/kunpengsra_inference_16_0031.html)

```bash
git clone -b v1.19.2 https://github.com/microsoft/onnxruntime.git
git clone https://gitcode.com/openeuler/sra_onnxruntime_adapter.git
cd onnxruntime
git apply ../sra_onnxruntime_adapter/konnx.patch
```

**编译OnnxRuntime**
进入onnxruntime目录下，然后开始源码编译onnxruntime，使用命令如下，其中acl的两个参数需要指定编译后的Arm Compute Library路径

```bash
# 在onnxruntime目录下
./build.sh --config Release --build_wheel --parallel --compile_no_warning_as_error \
  --skip_submodule_sync --allow_running_as_root --skip-keras-test --skip_onnx_tests \
  --skip_tests --build_dir build --use_acl "ACL_2308" \
  --acl_home=/path/ComputeLibrary-23.08 \
  --acl_libs=/path/ComputeLibrary-23.08/build
```

编译成功后在/onnxruntime/build/Release/dist下可以得到**onnxruntime_acl-xx.whl**

若遇到Eigen库下载失败，可参考[解决方法](https://www.hikunpeng.com/document/detail/zh/SRA/ecosystemEnable/ONNXRuntime/kunpengonnxruntime_02_0019.html)

#### 加速库50

**安装环境**

1. 确认所使用的处理器
   
   ```bash
   lscpu #可查看BIOS Model name字段
   ```

2. 安装处理器对应编译环境中所需的cmake、make、gcc、python以及python三方库

**编译OnnxRuntime**

下载onnxruntime的源码并解压进入文件夹

```bash
wget -q https://codeload.github.com/microsoft/onnxruntime/zip/refs/tags/v1.22.2 -O onnxruntime-1.22.2.zip
unzip onnxruntime-1.22.2.zip && cd onnxruntime-1.22.2
```

进入onnxruntime-1.22.2目录下，然后开始源码编译onnxruntime，在/onnxruntime-1.22.2目录下使用以下命令

```bash
# 在/onnxruntime-1.22.2目录下
./build.sh --config Release --build_wheel --parallel --compile_no_warning_as_error --skip_submodule_sync --allow_running_as_root --skip-keras-test --skip_onnx_tests --skip_tests --build_dir build --cmake_extra_defines onnxruntime_USE_KLEIDIAI=ON
```

编译好后在/onnxruntime/build/Release/dist下可以得到**onnxruntime-xx.whl**

# 2、部署PI特性

## 运行环境要求

本小节说明运行PI特性的环境部署和依赖安装步骤

### 已验证环境

| 鲲鹏芯片型号      | Python版本 | ONNX Runtime           |
| ----------- | -------- | ---------------------- |
| 鲲鹏920处理器    | 3.9      | onnxruntime_acl-1.19.2 |
| 鲲鹏920新型号处理器 | 3.9      | onnxruntime_acl-1.19.2 |
| 鲲鹏950处理器    | 3.10     | onnxruntime-1.22.2     |
| 其他          | 不限       | onnxruntime==1.19.2    |

操作系统均为openEuler 24.03 LTS

### 三方库依赖

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

**安装环境**

1. 确认所使用的处理器
   
   ```bash
   lscpu #可查看BIOS Model name字段
   ```

2. 若为鲲鹏920处理器，鲲鹏920新型号处理器，鲲鹏950处理器，则安装python以及python三方库，python版本需与步骤一中编译.whl时所使用的python版本一致

   ```bash
   # /path为PI目录的父文件夹
   pip install -r /path/PI/deploy/requirements.txt
   ```

3. 若非上述系列处理器，则可直接使用pip install onnxruntime==1.19.2安装非加速版的三方库

**安装onnxruntime库**

使用**鲲鹏920处理器**以及**鲲鹏920新型号处理器**时，需要使用pip下载**加速库20**

```bash
pip install onnxruntime_acl-xx.whl
```

需要配置的环境变量如下

- 安装[SRA_Inference](https://www.hikunpeng.com/document/detail/zh/boostsra/srainference/SRA_Inference/kunpengsra_inference_16_0006.html)，若与编译加速库是同一台机器，则无需再安装

- 添加ACL库，其中/path/ComputeLibrary-23.08/build目录下应有libarm_compute.so、libarm_compute_core.so以及libarm_compute_graph.so
  
  ```bash
  # 将/path修改为实际路径
  export LD_LIBRARY_PATH=/path/ComputeLibrary-23.08/build:$LD_LIBRARY_PATH
  ```

- 添加onnxruntime库本身链接
  
  ```bash
  export LD_LIBRARY_PATH=$(python -c 'import sysconfig,os;print(os.path.join(sysconfig.get_paths()["purelib"],"onnxruntime","capi"))'):$LD_LIBRARY_PATH
  ```

使用**鲲鹏950处理器**时，需要使用pip下载**加速库50**

```bash
pip install onnxruntime-xx.whl
```

需要配置的环境变量如下

- 添加onnxruntime库本身链接
  
  ```bash
  export LD_LIBRARY_PATH=$(python -c 'import sysconfig,os;print(os.path.join(sysconfig.get_paths()["purelib"],"onnxruntime","capi"))'):$LD_LIBRARY_PATH
  ```

**功能验证**
当部署好环境后，可以通过单元测试验证功能是否正常，在/PI路径下运行命令

```bash
python function_test.py
```

预期输出

```bash
.................
----------------------------------------------------------------------
Ran 17 tests in 1.703s

OK
```

---

## 性能优化命令

部署完成后，使用以下命令运行脚本以达到最大性能：

```bash
# 运行命令示例
numactl --cpunodebind=0 --membind=0 python 脚本名称.py
```

`numactl` 将进程绑定到NUMA节点0的CPU和内存上，减少跨节点访问延迟。
