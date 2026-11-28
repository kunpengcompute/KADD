# ============================================================================
# KADD 门禁（PR 构建 / 单元测试）基础镜像
#
# 镜像需覆盖的调用链（.gitcode/workflows → 仓库内脚本）：
#   build_job.yml
#     prepare-deps : kadd-deps-sync            → 使用镜像内置 /opt/kadd-deps
#     build        : sh build.sh
#                     ├─ dnf / git-lfs / git lfs pull   （LFS 模型与训练数据）
#                     └─ SQLi/build.sh → cmake + gcc/g++ + python3
#   ut_job.yml
#     ut           : sh build.sh && SQLi/coverage_all.sh
#                     └─ clang/clang++ + llvm-profdata/llvm-cov + compiler-rt
#                        （-fsanitize=fuzzer / -fprofile-instr-generate）
#                        + timeout/unshare（并行 fuzz 的私有 /tmp 隔离）
#
# 构建（必须在 ARM64/aarch64 机器上：门禁节点为 ARM64，且固化的是 arm64 依赖包）：
#   docker build -f .gitcode/dockerfiles/Dockerfile \
#     -t swr.cn-north-4.myhuaweicloud.com/cloud_boostkit/openeuler24.03_lts_sp3:KADD_26.3.0.01 .
#   docker push swr.cn-north-4.myhuaweicloud.com/cloud_boostkit/openeuler24.03_lts_sp3:KADD_26.3.0.01
#   PR-Pipeline_KADD.yml 的 build/ut 作业 docker_img 已指向该 tag；升级镜像时同步改两处。
#
# 注意：默认基础镜像 tag 为 latest（可变引用），结果不可完全复现；如需可复现，
#       用 --build-arg BASE_IMAGE=<具体 tag> 覆盖即可，无需改文件。
# ============================================================================
ARG BASE_IMAGE=swr.cn-north-4.myhuaweicloud.com/cloud_boostkit/openeuler-24.03-lts-sp3:latest
FROM ${BASE_IMAGE}

ARG ONNXRUNTIME_VERSION=1.28.0
ARG CJSON_VERSION=1.7.19
ARG GTEST_VERSION=release-1.12.1
# 第三方依赖离线包地址（构建节点无法访问 api.nuget.org / github.com，统一放内网 OBS）
# 需要上传的文件：
#   microsoft.ml.onnxruntime.1.28.0.nupkg
#   cJSON-v1.7.19.zip
#   googletest-release-1.12.1.tar.gz
ARG DEPS_BASE_URL="https://buildtools.obs.cn-north-4.myhuaweicloud.com/kadd-deps"

ENV LANG=C.UTF-8 \
    LC_ALL=C.UTF-8 \
    KADD_DEPS_DIR=/opt/kadd-deps

LABEL org.opencontainers.image.title="KADD CI base image" \
      org.opencontainers.image.description="openEuler 24.03 LTS SP3 + KADD build/UT toolchain and offline third-party deps"

# ----------------------------------------------------------------------------
# 1) 软件源换清华 TUNA 镜像 + 预装构建/UT 工具链
#    覆盖 build.sh / SQLi/build.sh / SQLi/coverage_all.sh 实际调用的全部命令：
#      cmake gcc g++ make            —— CMake 构建（根 build.sh、SQLi/build.sh）
#      clang clang++ llvm            —— coverage_all.sh 的 fuzzer + 覆盖率构建
#      compiler-rt                   —— libFuzzer / profile 运行时（-fsanitize=fuzzer、
#                                       -fprofile-instr-generate 必需，缺失则 UT 阶段编译失败）
#      python3                       —— SQLi/build.sh 生成模型密钥、改写 config.json
#      git git-lfs ca-certificates   —— 根 build.sh 的 git lfs pull（HTTPS 证书校验）
#      wget curl unzip tar gzip xz   —— 依赖解包（镜像构建期/脚本回退下载）
#      util-linux/coreutils/...      —— timeout、unshare、mount、nproc、lscpu 等
#    说明：
#      - dnf 配置成快速失败，避免门禁里 build.sh 的 dnf makecache 在外网不可达时长时间卡住；
#      - TUNA 源不可用时自动回退官方源，避免镜像构建因镜像站抖动而失败；
#      - TUNA 的 update 源 repomd 校验常不一致，debuginfo/source 构建也用不到，
#        安装时直接禁用（仓库不存在时 dnf 会忽略该参数）。
# ----------------------------------------------------------------------------
RUN set -eux; \
    sed -i -e '/^timeout=/d' -e '/^retries=/d' -e '/^skip_if_unavailable=/d' /etc/dnf/dnf.conf; \
    printf '\ntimeout=10\nretries=1\nskip_if_unavailable=True\n' >> /etc/dnf/dnf.conf; \
    cp -a /etc/yum.repos.d /tmp/yum.repos.d.orig; \
    for f in /etc/yum.repos.d/openEuler*.repo; do \
        if [ -f "$f" ]; then \
            sed -i 's|https\?://repo\.openeuler\.org|https://mirrors.tuna.tsinghua.edu.cn/openeuler|g' "$f"; \
        fi; \
    done; \
    PKGS="cmake gcc gcc-c++ make clang llvm python3 git git-lfs ca-certificates wget curl unzip tar gzip xz util-linux coreutils findutils diffutils which procps-ng numactl"; \
    if ! dnf install -y --disablerepo=update --disablerepo=debuginfo --disablerepo=source $PKGS; then \
        echo "[WARN] TUNA 源安装失败，回退 openEuler 官方源重试"; \
        rm -f /etc/yum.repos.d/*.repo; \
        cp -a /tmp/yum.repos.d.orig/. /etc/yum.repos.d/; \
        dnf clean all; \
        dnf install -y $PKGS; \
    fi; \
    dnf install -y compiler-rt || echo "[WARN] compiler-rt 包不可用，由第 4 步烟测判定 libFuzzer/profile 运行时是否可用"; \
    dnf clean all; \
    rm -rf /var/cache/dnf /var/cache/yum /tmp/yum.repos.d.orig; \
    ln -sf /bin/bash /bin/sh; \
    git lfs install --system || git lfs install

# ----------------------------------------------------------------------------
# 2) 第三方依赖离线缓存（目录结构对齐 SQLi/util，可直接整体拷贝）
#    与 SQLi/build.sh 的存在性判断一一对应：
#      util/cJSON/cJSON.c
#      util/onnxruntime-linux-aarch64-1.28.0/{include,lib}
#      util/gtest/{include,src}（需含 src/gtest-all.cc、src/gtest_main.cc）
#    - ONNX Runtime（aarch64，ML 推理必需）：取 NuGet 包
#      microsoft.ml.onnxruntime 的 include/ 与 linux-arm64 的 .so
#    - cJSON 源码（build.sh 以 cJSON.h/cJSON.c 内嵌方式使用）
#    - Google Test include/src（单元测试构建，含 gmock）
#    三个包统一从 DEPS_BASE_URL（内网 OBS）下载：构建节点无法访问
#    api.nuget.org / github.com。任一下载/校验失败即镜像构建失败
#    （不产出“假成功”镜像）。
# ----------------------------------------------------------------------------
RUN set -eux; \
    mkdir -p "${KADD_DEPS_DIR}/cJSON" "${KADD_DEPS_DIR}/gtest"; \
    work="$(mktemp -d)"; cd "$work"; \
    ORT_NAME="onnxruntime-linux-aarch64-${ONNXRUNTIME_VERSION}"; \
    ORT_NUPKG="microsoft.ml.onnxruntime.${ONNXRUNTIME_VERSION}.nupkg"; \
    CJSON_ZIP="cJSON-v${CJSON_VERSION}.zip"; \
    GTEST_TAR="googletest-${GTEST_VERSION}.tar.gz"; \
    wget -T 60 --tries=3 -O "${ORT_NUPKG}" "${DEPS_BASE_URL}/${ORT_NUPKG}"; \
    unzip -q "${ORT_NUPKG}" -d ort_tmp; \
    mkdir -p "${KADD_DEPS_DIR}/${ORT_NAME}/include" "${KADD_DEPS_DIR}/${ORT_NAME}/lib"; \
    cp -a ort_tmp/build/native/include/. "${KADD_DEPS_DIR}/${ORT_NAME}/include/"; \
    cp -a ort_tmp/runtimes/linux-arm64/native/*.so "${KADD_DEPS_DIR}/${ORT_NAME}/lib/"; \
    cd "${KADD_DEPS_DIR}/${ORT_NAME}/lib"; \
    [ -e libonnxruntime.so.1 ] || ln -s libonnxruntime.so libonnxruntime.so.1; \
    [ -e "libonnxruntime.so.${ONNXRUNTIME_VERSION}" ] || ln -s libonnxruntime.so "libonnxruntime.so.${ONNXRUNTIME_VERSION}"; \
    cd "$work"; \
    wget -T 60 --tries=3 -O cjson.zip "${DEPS_BASE_URL}/${CJSON_ZIP}"; \
    unzip -q cjson.zip; \
    cp "cJSON-${CJSON_VERSION}/cJSON.h" "cJSON-${CJSON_VERSION}/cJSON.c" "${KADD_DEPS_DIR}/cJSON/"; \
    wget -T 60 --tries=3 -O gtest.tar.gz "${DEPS_BASE_URL}/${GTEST_TAR}"; \
    tar xzf gtest.tar.gz; \
    cp -r "googletest-${GTEST_VERSION}/googletest/include" "${KADD_DEPS_DIR}/gtest/"; \
    cp -r "googletest-${GTEST_VERSION}/googletest/src" "${KADD_DEPS_DIR}/gtest/"; \
    test -f "${KADD_DEPS_DIR}/${ORT_NAME}/lib/libonnxruntime.so"; \
    test -f "${KADD_DEPS_DIR}/${ORT_NAME}/include/onnxruntime_c_api.h"; \
    test -s "${KADD_DEPS_DIR}/cJSON/cJSON.c"; \
    test -f "${KADD_DEPS_DIR}/gtest/src/gtest-all.cc"; \
    test -f "${KADD_DEPS_DIR}/gtest/src/gtest_main.cc"; \
    if ldd "${KADD_DEPS_DIR}/${ORT_NAME}/lib/libonnxruntime.so" | grep "not found"; then \
        echo "[ERROR] ONNX Runtime 动态库存在未解析依赖，运行期会加载失败"; \
        exit 1; \
    fi; \
    cd /; rm -rf "$work"

# ----------------------------------------------------------------------------
# 3) 缓存同步脚本：kadd-deps-sync [目标目录，默认 ./SQLi/util]
#    幂等；SQLi/build.sh 检测到 util/ 下依赖已存在即跳过外网下载
# ----------------------------------------------------------------------------
RUN printf '%s\n' \
        '#!/bin/sh' \
        '# 将镜像内置的第三方依赖缓存同步到仓库 SQLi/util（幂等，可重复执行）' \
        'set -eu' \
        'dest="${1:-./SQLi/util}"' \
        '[ -d /opt/kadd-deps ] || { echo "[ERROR] 镜像内缺少 /opt/kadd-deps"; exit 1; }' \
        'mkdir -p "$dest"' \
        'cp -a /opt/kadd-deps/. "$dest/"' \
        'echo "[kadd] deps synced: /opt/kadd-deps -> $dest"' \
        > /usr/local/bin/kadd-deps-sync \
    && chmod +x /usr/local/bin/kadd-deps-sync

# ----------------------------------------------------------------------------
# 4) 门禁工具链自检（镜像阶段暴露问题，避免 CI 跑到一半才失败）
#    - 命令存在性：与三个门禁脚本实际调用一一对应
#    - fuzzer 烟测：验证 clang 能链接 libFuzzer 并生成覆盖率插桩目标
#      （缺少 compiler-rt 时这里直接失败，而不是等到 ut 作业）
# ----------------------------------------------------------------------------
RUN set -eux; \
    miss=""; \
    for c in cmake ctest gcc g++ make clang clang++ llvm-cov llvm-profdata \
             python3 git git-lfs wget curl unzip tar gzip xz \
             timeout unshare nproc lscpu sed grep; do \
        command -v "$c" >/dev/null 2>&1 || miss="$miss $c"; \
    done; \
    if [ -n "$miss" ]; then \
        echo "[ERROR] 镜像缺少门禁脚本所需命令:$miss"; \
        exit 1; \
    fi; \
    cmake --version | head -n 1; \
    clang --version | head -n 1; \
    llvm-cov --version | head -n 1; \
    python3 --version; \
    git lfs version; \
    printf '%s\n' \
        'extern "C" int LLVMFuzzerTestOneInput(const unsigned char *d, unsigned long n) {' \
        '    (void)d; (void)n; return 0;' \
        '}' > /tmp/fuzz_smoke.cpp; \
    clang++ -c -fprofile-instr-generate -fcoverage-mapping /tmp/fuzz_smoke.cpp -o /tmp/fuzz_smoke.o; \
    clang++ -fsanitize=fuzzer /tmp/fuzz_smoke.cpp -o /tmp/fuzz_smoke; \
    /tmp/fuzz_smoke -runs=1 >/dev/null 2>&1; \
    rm -f /tmp/fuzz_smoke.cpp /tmp/fuzz_smoke.o /tmp/fuzz_smoke

WORKDIR /workspace
