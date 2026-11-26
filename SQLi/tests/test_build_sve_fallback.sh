#!/usr/bin/env bash
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# All rights reserved.
#
# Licensed under the 3-Clause BSD License (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://opensource.org/license/bsd-3-clause
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_SH="${SCRIPT_DIR}/../build.sh"

extract_sve_cpu_supported() {
    sed -n '/^sve_cpu_supported()/,/^}/p' "${BUILD_SH}"
}

extract_sve_fallback_logic() {
    sed -n '/^SVE_REQUESTED=/,/^case "\$MODE"/p' "${BUILD_SH}" | sed '$d'
}

run_case() {
    local arch="$1"
    local flags="$2"
    local expected="$3"
    local actual=""

    actual="$(
        uname() { echo "${arch}"; }
        lscpu() { echo "Flags: ${flags}"; }
        eval "$(extract_sve_cpu_supported)"
        if sve_cpu_supported; then
            echo "supported"
        else
            echo "unsupported"
        fi
    )"

    if [ "${actual}" != "${expected}" ]; then
        echo "FAIL: arch=${arch}, flags=${flags}, expected=${expected}, got=${actual}" >&2
        return 1
    fi
    echo "PASS: arch=${arch}, flags=${flags} -> ${expected}"
}

main() {
    run_case "aarch64" "fp asimd sve" "supported"
    run_case "aarch64" "fp asimd" "unsupported"
    run_case "x86_64" "sse avx" "unsupported"

    local effective_sve
    effective_sve="$(
        SVE=ON
        uname() { echo "aarch64"; }
        lscpu() { echo "Flags: fp asimd"; }
        eval "$(extract_sve_cpu_supported)"
        eval "$(extract_sve_fallback_logic)" >/dev/null
        echo "${SVE}"
    )"
    if [ "${effective_sve}" != "OFF" ]; then
        echo "FAIL: SVE=ON on non-SVE CPU should fall back to OFF, got ${effective_sve}" >&2
        return 1
    fi
    echo "PASS: SVE=ON on non-SVE CPU -> OFF"
}

main "$@"
