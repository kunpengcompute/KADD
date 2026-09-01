#!/bin/bash
# ============================================================================
# KADD 一键构建脚本
# ============================================================================


KADD_ROOT="$(cd "$(dirname "$0")" && pwd)"

echo "==================== KADD 一键构建 ===================="
echo "项目根目录: ${KADD_ROOT}"
echo ""

# ============================================================================
# 准备环境：配置 dnf 国内镜像源、安装 git-lfs 并拉取仓库内 LFS 文件
# ============================================================================
if command -v dnf >/dev/null 2>&1; then
    echo "[准备] 检测到 dnf，配置国内镜像源 (阿里云)..."

    # 将默认的 mirrorlist 替换为阿里云镜像 baseurl
    sed -i 's|^mirrorlist=|#mirrorlist=|g' /etc/yum.repos.d/CentOS-*.repo 2>/dev/null
    sed -i 's|^#baseurl=http://mirror.centos.org/centos|baseurl=http://mirrors.aliyun.com/centos|g' /etc/yum.repos.d/CentOS-*.repo 2>/dev/null
    # 清理缓存并刷新
    dnf clean all 2>/dev/null
    dnf makecache 2>/dev/null

    echo "[准备] 安装 git-lfs..."
    if ! command -v git-lfs >/dev/null 2>&1; then
        dnf install -y git-lfs || {
            echo "❌ git-lfs 安装失败，请手动安装后重试"
            exit 1
        }
    fi
    echo "✅ git-lfs 版本: $(git-lfs version)"

    echo "[准备] 初始化 git-lfs 并拉取仓库内 LFS 文件..."
    cd "${KADD_ROOT}"

    git lfs install
    git lfs pull || {
        echo "❌ git-lfs 拉取 LFS 文件失败"
        exit 1
    }

    echo "[验证] 检查 LFS 文件是否正确拉取..."
    # 通过文件大小判断 LFS 文件是否仍为指针文件（指针文件通常 < 1KB）
    LFS_FILES=$(git lfs ls-files --name-only 2>/dev/null)
    if [ -z "$LFS_FILES" ]; then
        echo "⚠️ 仓库中未检测到 LFS 文件"
    else
        INVALID_COUNT=0
        while IFS= read -r file; do
            if [ -f "$file" ]; then
                size=$(wc -c < "$file")
                if [ "$size" -lt 1000 ]; then
                    echo "❌ 文件过小，可能仍为 LFS 指针: $file (${size} bytes)"
                    INVALID_COUNT=$((INVALID_COUNT + 1))
                fi
            fi
        done <<< "$LFS_FILES"
        if [ "$INVALID_COUNT" -gt 0 ]; then
            echo "❌ 共 $INVALID_COUNT 个 LFS 文件未正确拉取"
            exit 1
        fi
        echo "✅ 所有 LFS 文件已正确拉取"
    fi
else
    echo "⚠️ 未检测到 dnf，跳过 git-lfs 安装与 LFS 拉取（请确认仓库文件已完整获取）"
fi

echo ""

# 构建 SQLi
if [ -d "${KADD_ROOT}/SQLi" ]; then
    echo "[1/2] 构建 SQLi 特性..."
    cd "${KADD_ROOT}/SQLi"
    chmod +x build.sh && ./build.sh
    echo "✅ SQLi 构建完成"
fi

# 部署 PI
if [ -d "${KADD_ROOT}/PI/deploy" ]; then
    echo "[2/2] 部署 PI 特性..."
    cd "${KADD_ROOT}/PI/deploy"
    chmod +x deploy.sh && source ./deploy.sh -s < /dev/null
    echo "✅ PI 部署完成"
    echo "执行功能测试: ${KADD_ROOT}/PI/function_test.py"
    python "${KADD_ROOT}/PI/function_test.py" || {
        echo "❌ PI 功能测试失败"
        exit 1
    }
    echo "✅ PI 功能测试完成"
    cd "${KADD_ROOT}"
fi

echo ""
echo "==================== 构建完成 ===================="
echo ""
echo "SQLi输出："
echo "  库文件: ${KADD_ROOT}/SQLi/build/src/"
echo "  Demo:   ${KADD_ROOT}/SQLi/build/demo/"
echo "PI为python项目，无文件输出"
echo ""