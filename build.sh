yum search onnxruntime

# openEuler
yum install cmake gcc-c++ make

# --- cJSON ---
mkdir -p third_party/cJSON
wget https://buildtools.obs.cn-north-4.myhuaweicloud.com/cJSON-master.zip
unzip -q cJSON*.zip
cp cJSON-master/cJSON.h third_party/cJSON/
cp cJSON-master/cJSON.c third_party/cJSON/
rm -rf cJSON-master cJSON.zip

# --- ONNX Runtime ---
wget https://buildtools.obs.cn-north-4.myhuaweicloud.com/onnxruntime-linux-aarch64-1.22.0.tgz
tar xzf onnxruntime-linux-*.tgz
mkdir -p third_party/onnxruntime/lib third_party/onnxruntime/include/onnxruntime
cp -r onnxruntime-linux-*/lib/* third_party/onnxruntime/lib/
cp -r onnxruntime-linux-*/include/* third_party/onnxruntime/include/
rm -rf onnxruntime-linux-*

# --- Google Test ---
wget https://buildtools.obs.cn-north-4.myhuaweicloud.com/googletest-release-1.12.1.zip
unzip googletest*.zip
mkdir -p third_party/gtest
cp -r googletest-release-1.12.1/googletest/include third_party/gtest/
cp -r googletest-release-1.12.1/googletest/src third_party/gtest/
rm -rf googletest*

# ============================================================
# CMake 构建
# ============================================================
mkdir -p build && cd build
cmake -DUSE_SYSTEM_LIBS=OFF -DKADD_OPTIMIZE=ON -DCMAKE_BUILD_TYPE=Release ..
cmake --build .

# 运行测试（设置 LD_LIBRARY_PATH 以找到动态库）
export LD_LIBRARY_PATH=$(pwd)/src:$(pwd)/../third_party/onnxruntime/lib:$LD_LIBRARY_PATH
ctest --verbose
