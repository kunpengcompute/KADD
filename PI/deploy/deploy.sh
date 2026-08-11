#!/usr/bin/env bash

is_sourced() {
    [[ "${BASH_SOURCE[0]}" != "$0" ]]
}

IS_SOURCED=0
if is_sourced; then
    IS_SOURCED=1
fi

set -o pipefail

# =========================
# 颜色输出
# =========================
USE_COLOR="false"
if [[ -z "${NO_COLOR:-}" && -t 1 ]]; then
    USE_COLOR="true"
fi

if [[ "$USE_COLOR" == "true" ]]; then
    C_RESET=$'\033[0m'
    C_BOLD=$'\033[1m'
    C_RED=$'\033[31m'
    C_GREEN=$'\033[32m'
    C_YELLOW=$'\033[33m'
    C_BLUE=$'\033[34m'
    C_CYAN=$'\033[36m'
else
    C_RESET=""
    C_BOLD=""
    C_RED=""
    C_GREEN=""
    C_YELLOW=""
    C_BLUE=""
    C_CYAN=""
fi

log() {
    printf '%b
' "${C_BLUE}[INFO]${C_RESET} $*"
}

ok() {
    printf '%b
' "${C_GREEN}[OK]${C_RESET} $*"
}

warn() {
    printf '%b
' "${C_YELLOW}[WARN]${C_RESET} $*" >&2
}

fail() {
    printf '%b
' "${C_RED}[ERROR]${C_RESET} $*" >&2
}

error_exit() {
    fail "$*"
    return 1
}

usage() {
    cat <<EOF
Usage:
  source ${BASH_SOURCE[0]} [options]

Options:
  -p, --python PATH_OR_CMD      指定 Python 可执行文件路径或命令
  -d, --venv-dir DIR            指定虚拟环境创建目录，默认项目根目录
  -e, --venv PATH               指定已有虚拟环境路径
  -r, --requirements FILE       指定 requirements.txt 路径，默认脚本同级目录下的 requirements.txt
  --recreate-venv               如果目标虚拟环境已存在，则删除后重新创建
  --pip-index-url URL           指定 pip 镜像源，默认阿里云 PyPI 镜像
  --pip-trusted-host HOST       指定 pip trusted-host，默认自动从 index-url 推导
  --proxy URL                   设置统一代理，同时作用于 http_proxy 和 https_proxy
  -s, --skip-function-test      部署完成后不执行功能测试
  -h, --help                    查看帮助

Examples:
  source ${BASH_SOURCE[0]}
  source ${BASH_SOURCE[0]} --python /opt/python3.9/bin/python3.9
  source ${BASH_SOURCE[0]} --python python3.9 --venv-dir /data/envs
  source ${BASH_SOURCE[0]} --venv /data/envs/kunpengDetection
  source ${BASH_SOURCE[0]} --pip-index-url https://pypi.tuna.tsinghua.edu.cn/simple
  source ${BASH_SOURCE[0]} --pip-index-url https://pypi.tuna.tsinghua.edu.cn/simple --pip-trusted-host pypi.tuna.tsinghua.edu.cn
  source ${BASH_SOURCE[0]} --proxy http://127.0.0.1:7890
  source ${BASH_SOURCE[0]} --skip-function-test
EOF
}



# =========================
# 全局变量
# =========================
SCRIPT_DIR=""
PROJECT_ROOT=""
ACCELERATION_LIBRARY_DIR=""

VENV_NAME="kunpengDetection"
PYTHON_INPUT=""
VENV_BASE_DIR=""
VENV_PATH_INPUT=""
REQUIREMENTS_FILE=""
RECREATE_VENV="false"

PIP_INDEX_URL="https://mirrors.aliyun.com/pypi/simple"
PIP_TRUSTED_HOST=""

PROXY_URL=""

SHOW_HELP="false"

DEPLOY_VENV_ACTIVATED="false"
DEPLOY_VENV_PATH=""

FUNCTION_TEST_FILE=""
SKIP_FUNCTION_TEST="false"

KUNPENG920_ONNXRUNTIME_WHL="onnxruntime_acl-1.19.2-cp39-cp39-linux_aarch64.whl"
KUNPENG950_ONNXRUNTIME_WHL="onnxruntime-1.22.2-cp310-cp310-linux_aarch64.whl"

need_arg() {
    local opt="$1"
    local val="${2:-}"

    if [[ -z "$val" || "$val" == -* ]]; then
        error_exit "参数 ${opt} 需要指定值"
        return 1
    fi
}

init_paths() {
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)" || return 1
    PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)" || return 1
    ACCELERATION_LIBRARY_DIR="${PROJECT_ROOT}/acceleration_library"

    if [[ -z "$VENV_BASE_DIR" ]]; then
        VENV_BASE_DIR="$PROJECT_ROOT"
    fi

    if [[ -z "$REQUIREMENTS_FILE" ]]; then
        REQUIREMENTS_FILE="${SCRIPT_DIR}/requirements.txt"
    fi

    if [[ -z "$FUNCTION_TEST_FILE" ]]; then
        FUNCTION_TEST_FILE="${PROJECT_ROOT}/function_test.py"
    fi
}

derive_trusted_host_from_url() {
    local url="$1"
    local host=""

    host="${url#*://}"
    host="${host%%/*}"
    host="${host%%:*}"

    printf '%s' "$host"
}

prepare_pip_source() {
    if [[ -z "$PIP_TRUSTED_HOST" ]]; then
        PIP_TRUSTED_HOST="$(derive_trusted_host_from_url "$PIP_INDEX_URL")"
    fi

    if [[ -z "$PIP_TRUSTED_HOST" ]]; then
        error_exit "无法从 pip 源地址推导 trusted-host，请手动指定 --pip-trusted-host"
        return 1
    fi
}

setup_proxy() {
    if [[ -z "$PROXY_URL" ]]; then
        return 0
    fi

    export http_proxy="$PROXY_URL"
    export https_proxy="$PROXY_URL"
    export HTTP_PROXY="$PROXY_URL"
    export HTTPS_PROXY="$PROXY_URL"

    log "已设置代理:"
    echo "  http_proxy=${PROXY_URL}"
    echo "  https_proxy=${PROXY_URL}"
}

parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -p|--python)
                need_arg "$1" "${2:-}" || return 1
                PYTHON_INPUT="$2"
                shift 2
                ;;
            -d|--venv-dir)
                need_arg "$1" "${2:-}" || return 1
                VENV_BASE_DIR="$2"
                shift 2
                ;;
            -e|--venv)
                need_arg "$1" "${2:-}" || return 1
                VENV_PATH_INPUT="$2"
                shift 2
                ;;
            -r|--requirements)
                need_arg "$1" "${2:-}" || return 1
                REQUIREMENTS_FILE="$2"
                shift 2
                ;;
            --recreate-venv)
                RECREATE_VENV="true"
                shift
                ;;
            --pip-index-url)
                need_arg "$1" "${2:-}" || return 1
                PIP_INDEX_URL="$2"
                shift 2
                ;;
            --pip-trusted-host)
                need_arg "$1" "${2:-}" || return 1
                PIP_TRUSTED_HOST="$2"
                shift 2
                ;;
            --proxy)
                need_arg "$1" "${2:-}" || return 1
                PROXY_URL="$2"
                shift 2
                ;;
            -s|--skip-function-test)
                SKIP_FUNCTION_TEST="true"
                shift
                ;;
            -h|--help)
                usage
                SHOW_HELP="true"
                return 0
                ;;
            *)
                error_exit "未知参数: $1"
                return 1
                ;;
        esac
    done
}

detect_cpu_chip() {
    local cpu_info=""
    cpu_info="$(LC_ALL=C lscpu 2>/dev/null || true)"
    cpu_info="$(printf '%s
' "$cpu_info" | tr '[:upper:]' '[:lower:]' | tr -d ' _-')"

    if [[ "$cpu_info" =~ kunpeng950 ]]; then
        echo "kunpeng950"
    elif [[ "$cpu_info" =~ kunpeng920 ]]; then
        echo "kunpeng920"
    else
        echo "other"
    fi
}

get_required_python_version() {
    case "$1" in
        kunpeng920) echo "3.9" ;;
        kunpeng950) echo "3.10" ;;
        *) echo "" ;;
    esac
}

resolve_python_path() {
    local input="$1"

    [[ -n "$input" ]] || return 1

    if [[ -x "$input" && ! -d "$input" ]]; then
        echo "$input"
        return 0
    fi

    if [[ -d "$input" ]]; then
        if [[ -x "$input/bin/python" ]]; then
            echo "$input/bin/python"
            return 0
        elif [[ -x "$input/bin/python3" ]]; then
            echo "$input/bin/python3"
            return 0
        elif [[ -x "$input/python" ]]; then
            echo "$input/python"
            return 0
        elif [[ -x "$input/python3" ]]; then
            echo "$input/python3"
            return 0
        fi
    fi

    if command -v "$input" >/dev/null 2>&1; then
        command -v "$input"
        return 0
    fi

    return 1
}

resolve_venv_python() {
    local venv_path="$1"

    [[ -n "$venv_path" && -d "$venv_path" ]] || return 1

    if [[ -x "$venv_path/bin/python" ]]; then
        echo "$venv_path/bin/python"
        return 0
    elif [[ -x "$venv_path/bin/python3" ]]; then
        echo "$venv_path/bin/python3"
        return 0
    fi

    return 1
}

get_python_version() {
    local python_bin="$1"
    "$python_bin" -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")'
}

check_python_venv_available() {
    local python_bin="$1"
    "$python_bin" -c 'import venv' >/dev/null 2>&1
}

find_python_from_path() {
    local required_version="$1"
    local candidates=()

    if [[ "$required_version" == "3.9" ]]; then
        candidates=("python3.9" "python3" "python")
    elif [[ "$required_version" == "3.10" ]]; then
        candidates=("python3.10" "python3" "python")
    else
        candidates=("python3" "python")
    fi

    local candidate candidate_path candidate_version
    for candidate in "${candidates[@]}"; do
        if command -v "$candidate" >/dev/null 2>&1; then
            candidate_path="$(command -v "$candidate")"
            candidate_version="$(get_python_version "$candidate_path" 2>/dev/null || true)"

            [[ -n "$candidate_version" ]] || continue

            if [[ -n "$required_version" ]]; then
                if [[ "$candidate_version" == "$required_version" ]]; then
                    echo "$candidate_path"
                    return 0
                fi
            else
                echo "$candidate_path"
                return 0
            fi
        fi
    done

    return 1
}

select_python_for_new_venv() {
    local required_version="$1"
    local python_bin=""

    if [[ -n "$PYTHON_INPUT" ]]; then
        python_bin="$(resolve_python_path "$PYTHON_INPUT")" || {
            error_exit "指定的 Python 不存在或不可执行: ${PYTHON_INPUT}"
            return 1
        }
        echo "$python_bin"
        return 0
    fi

    if python_bin="$(find_python_from_path "$required_version")"; then
        echo "$python_bin"
        return 0
    fi

    warn "未在系统 PATH 中检测到合适的 Python。"

    if [[ -n "$required_version" ]]; then
        warn "当前 CPU 需要 Python ${required_version}"
    else
        warn "当前 CPU 对 Python 版本无限制，但仍需要可用 Python 创建虚拟环境"
    fi

    local user_python_input=""
    while true; do
        read -r -p "请输入 Python 可执行文件路径或命令: " user_python_input
        python_bin="$(resolve_python_path "$user_python_input")" && {
            echo "$python_bin"
            return 0
        }
        warn "指定的 Python 不存在或不可执行: ${user_python_input}"
    done
}

activate_venv() {
    local venv_path="$1"

    [[ -f "${venv_path}/bin/activate" ]] || {
        error_exit "虚拟环境激活脚本不存在: ${venv_path}/bin/activate"
        return 1
    }

    # shellcheck disable=SC1091
    source "${venv_path}/bin/activate" || {
        error_exit "激活虚拟环境失败: ${venv_path}"
        return 1
    }

    DEPLOY_VENV_ACTIVATED="true"
    DEPLOY_VENV_PATH="$venv_path"
}

deactivate_on_failure() {
    local status="$1"

    if [[ "$status" -eq 0 ]]; then
        return 0
    fi

    if [[ "$DEPLOY_VENV_ACTIVATED" != "true" ]]; then
        return 0
    fi

    if [[ -n "${VIRTUAL_ENV:-}" && "$VIRTUAL_ENV" == "$DEPLOY_VENV_PATH" ]]; then
        if type deactivate >/dev/null 2>&1; then
            warn "部署失败，正在退出虚拟环境: ${DEPLOY_VENV_PATH}"
            deactivate
        else
            warn "部署失败，但未找到 deactivate 函数，无法自动退出虚拟环境"
        fi
    fi
}

pip_install_with_source() {
    python -m pip install "$@" \
        -i "$PIP_INDEX_URL" \
        --trusted-host "$PIP_TRUSTED_HOST"
}

install_requirements() {
    local requirements_file="$1"

    [[ -f "$requirements_file" ]] || {
        error_exit "未找到依赖文件: ${requirements_file}"
        return 1
    }

    if [[ ! -s "$requirements_file" ]]; then
        log "requirements 文件为空，跳过依赖安装"
        return 0
    fi

    log "安装依赖: ${requirements_file}"
    log "使用 pip 镜像源: ${PIP_INDEX_URL}"

    pip_install_with_source -r "$requirements_file" || {
        error_exit "安装 requirements 失败"
        return 1
    }
}

install_onnxruntime() {
    local cpu_chip="$1"
    local whl_name=""
    local whl_path=""

    case "$cpu_chip" in
        kunpeng920)
            whl_name="$KUNPENG920_ONNXRUNTIME_WHL"
            [[ -n "$whl_name" ]] || {
                error_exit "Kunpeng920 的 onnxruntime whl 包名称未配置"
                return 1
            }

            whl_path="${ACCELERATION_LIBRARY_DIR}/${whl_name}"
            [[ -f "$whl_path" ]] || {
                error_exit "未找到 Kunpeng920 onnxruntime whl 包: ${whl_path}"
                return 1
            }

            log "检测到 Kunpeng920，安装本地 onnxruntime ACL whl 包"
            log "whl 路径: ${whl_path}"
            log "使用 pip 镜像源: ${PIP_INDEX_URL}"

            pip_install_with_source "$whl_path" || {
                error_exit "安装 Kunpeng920 onnxruntime whl 失败"
                return 1
            }
            ;;
        kunpeng950)
            whl_name="$KUNPENG950_ONNXRUNTIME_WHL"
            [[ -n "$whl_name" ]] || {
                error_exit "Kunpeng950 的 onnxruntime whl 包名称暂未配置，请先补充 KUNPENG950_ONNXRUNTIME_WHL"
                return 1
            }

            whl_path="${ACCELERATION_LIBRARY_DIR}/${whl_name}"
            [[ -f "$whl_path" ]] || {
                error_exit "未找到 Kunpeng950 onnxruntime whl 包: ${whl_path}"
                return 1
            }

            log "检测到 Kunpeng950，安装本地 onnxruntime whl 包"
            log "whl 路径: ${whl_path}"
            log "使用 pip 镜像源: ${PIP_INDEX_URL}"

            pip_install_with_source "$whl_path" || {
                error_exit "安装 Kunpeng950 onnxruntime whl 失败"
                return 1
            }
            ;;
        *)
            log "非 Kunpeng920/Kunpeng950 CPU，安装 onnxruntime==1.19.2"
            log "使用 pip 镜像源: ${PIP_INDEX_URL}"

            pip_install_with_source "onnxruntime==1.19.2" || {
                error_exit "安装 onnxruntime==1.19.2 失败"
                return 1
            }
            ;;
    esac
}

export_acceleration_library()
{
    local lib_dir="$1"


    if [[ ! -d "$lib_dir" ]]; then
        error_exit "不存在的库目录: ${lib_dir}"
        return 1
    fi


    if [[ ":${LD_LIBRARY_PATH:-}:" != *":${lib_dir}:"* ]]; then
        
        export LD_LIBRARY_PATH="${lib_dir}:${LD_LIBRARY_PATH:-}"

        log "已添加 LD_LIBRARY_PATH:"
        echo "  ${lib_dir}"

    else

        log "LD_LIBRARY_PATH 已存在，无需重复添加"

    fi
}

run_function_test() {
    local test_file="$1"

    if [[ "$SKIP_FUNCTION_TEST" == "true" ]]; then
        log "已跳过功能测试"
        return 0
    fi

    [[ -f "$test_file" ]] || {
        error_exit "未找到功能测试脚本: ${test_file}"
        return 1
    }

    log "执行功能测试: ${test_file}"

    python -m unittest "$test_file" || {
        error_exit "功能测试失败: ${test_file}"
        return 1
    }

    ok "功能测试通过"
    return 0
}

prompt_numactl_command() {
    local prefix="numactl --cpunodebind=0 --membind=0 "
    local cmd=""
    local cmd_status=0

    printf '%b
' "${C_CYAN}用法：${C_RESET}${prefix}python 测试脚本.py"

    if ! command -v numactl >/dev/null 2>&1; then
        warn "未检测到 numactl 命令，请确认系统已安装 numactl"
    fi

    if [[ ! -t 0 || ! -t 1 ]]; then
        warn "当前不是交互式终端，无法预填命令行，已跳过"
        return 0
    fi

    printf '%b
' "${C_CYAN}请在下面补全并执行测试命令，按 Enter 执行；如不执行，可直接 Ctrl+C 或清空后回车。${C_RESET}"

    if read -r -e -i "$prefix" -p "$ " cmd; then
        if [[ -z "${cmd//[[:space:]]/}" || "$cmd" == "$prefix" ]]; then
            warn "未输入完整测试命令，跳过执行"
            return 0
        fi

        log "执行命令: ${cmd}"

        eval "$cmd"
        cmd_status=$?

        if [[ "$cmd_status" -ne 0 ]]; then
            warn "测试命令执行结束，退出码: ${cmd_status}"
        fi
    else
        echo
        warn "未读取到命令，跳过执行"
    fi

    return 0
}

main() {
    init_paths || return 1
    parse_args "$@" || return 1

    if [[ "$SHOW_HELP" == "true" ]]; then
        return 0
    fi
    
    setup_proxy || return 1
    prepare_pip_source || return 1

    if [[ -n "$VENV_PATH_INPUT" && -n "$PYTHON_INPUT" ]]; then
        error_exit "--venv 与 --python 不能同时使用"
        return 1
    fi

    if [[ -n "$VENV_PATH_INPUT" && "$RECREATE_VENV" == "true" ]]; then
        error_exit "--venv 与 --recreate-venv 不能同时使用"
        return 1
    fi

    local cpu_chip required_python_version python_bin venv_path
    cpu_chip="$(detect_cpu_chip)"
    required_python_version="$(get_required_python_version "$cpu_chip")"

    log "脚本目录: ${SCRIPT_DIR}"
    log "项目目录: ${PROJECT_ROOT}"
    log "加速库目录: ${ACCELERATION_LIBRARY_DIR}"
    log "检测到 CPU 类型: ${cpu_chip}"

    if [[ -n "$required_python_version" ]]; then
        log "当前 CPU 要求 Python 版本: ${required_python_version}"
    else
        log "当前 CPU 对 Python 版本无限制"
    fi

    if [[ -n "$VENV_PATH_INPUT" ]]; then
        venv_path="$VENV_PATH_INPUT"
        log "使用已有虚拟环境: ${venv_path}"

        python_bin="$(resolve_venv_python "$venv_path")" || {
            error_exit "指定的虚拟环境无效或未找到 Python: ${venv_path}"
            return 1
        }

        local venv_python_version
        venv_python_version="$(get_python_version "$python_bin" 2>/dev/null || true)"
        [[ -n "$venv_python_version" ]] || {
            error_exit "无法获取指定虚拟环境的 Python 版本: ${python_bin}"
            return 1
        }

        if [[ -n "$required_python_version" && "$venv_python_version" != "$required_python_version" ]]; then
            error_exit "虚拟环境 Python 版本不匹配，当前版本为 ${venv_python_version}，所需版本为 ${required_python_version}"
            return 1
        fi
    else
        python_bin="$(select_python_for_new_venv "$required_python_version")" || return 1

        local python_version
        python_version="$(get_python_version "$python_bin" 2>/dev/null || true)"
        [[ -n "$python_version" ]] || {
            error_exit "无法获取 Python 版本: ${python_bin}"
            return 1
        }

        if [[ -n "$required_python_version" && "$python_version" != "$required_python_version" ]]; then
            error_exit "Python 版本不匹配，当前版本为 ${python_version}，所需版本为 ${required_python_version}"
            return 1
        fi

        if ! check_python_venv_available "$python_bin"; then
            error_exit "当前 Python 不支持 venv 模块，无法创建虚拟环境: ${python_bin}"
            return 1
        fi

        mkdir -p "$VENV_BASE_DIR" || {
            error_exit "无法创建虚拟环境目录: ${VENV_BASE_DIR}"
            return 1
        }

        venv_path="${VENV_BASE_DIR}/${VENV_NAME}"

        if [[ -d "$venv_path" ]]; then
            if [[ "$RECREATE_VENV" == "true" ]]; then
                log "删除已有虚拟环境: ${venv_path}"
                rm -rf "$venv_path" || {
                    error_exit "删除旧虚拟环境失败: ${venv_path}"
                    return 1
                }
            else
                log "虚拟环境已存在: ${venv_path}"

                local existing_venv_python existing_venv_version
                existing_venv_python="$(resolve_venv_python "$venv_path")" || {
                    error_exit "目标路径已存在，但不是有效虚拟环境: ${venv_path}"
                    return 1
                }

                existing_venv_version="$(get_python_version "$existing_venv_python" 2>/dev/null || true)"
                [[ -n "$existing_venv_version" ]] || {
                    error_exit "无法获取虚拟环境 Python 版本: ${existing_venv_python}"
                    return 1
                }

                if [[ -n "$required_python_version" && "$existing_venv_version" != "$required_python_version" ]]; then
                    error_exit "虚拟环境 Python 版本不匹配，当前版本为 ${existing_venv_version}，所需版本为 ${required_python_version}"
                    return 1
                fi

                python_bin="$existing_venv_python"
            fi
        fi

        if [[ ! -d "$venv_path" ]]; then
            log "创建虚拟环境: ${venv_path}"
            "$python_bin" -m venv "$venv_path" || {
                error_exit "创建虚拟环境失败: ${venv_path}"
                return 1
            }
        fi
    fi

    activate_venv "$venv_path" || return 1
    install_requirements "$REQUIREMENTS_FILE" || return 1
    install_onnxruntime "$cpu_chip" || return 1
    export_acceleration_library "$ACCELERATION_LIBRARY_DIR" || return 1

    ok "部署完成"
    ok "虚拟环境: ${venv_path}"
    ok "Python: $(command -v python)"

    run_function_test "$FUNCTION_TEST_FILE" || return 1

    prompt_numactl_command

    return 0
}

main "$@"
main_status=$?

deactivate_on_failure "$main_status"

if [[ "$IS_SOURCED" -eq 1 ]]; then
    return "$main_status"
else
    exit "$main_status"
fi
