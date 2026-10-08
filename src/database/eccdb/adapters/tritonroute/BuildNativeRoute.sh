#!/usr/bin/env bash
# ***************************************************************************************
# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2023-2025 Peng Cheng Laboratory
# Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
# Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
#
# iEDA is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
# http://license.coscl.org.cn/MulanPSL2
#
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
#
# See the Mulan PSL v2 for more details.
# ***************************************************************************************

set -euo pipefail

usage() {
  printf '%s\n' \
    "Usage: ${0##*/} [--configure-only]" \
    '' \
    'Configure and build the native EccDB TritonRoute targets.' \
    '' \
    'Options:' \
    '  --configure-only  Configure the CMake build tree without compiling targets.' \
    '  --help            Show this help.'
}

configure_only=0
while (($#)); do
  case "$1" in
    --configure-only)
      configure_only=1
      ;;
    --help)
      usage
      exit 0
      ;;
    *)
      printf 'Unknown option: %s\n' "$1" >&2
      usage >&2
      exit 2
      ;;
  esac
  shift
done

adapter_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
workspace="$(git -C "${adapter_dir}" rev-parse --show-toplevel)"
dependencies="${OPENROAD_DEPENDENCY_SOURCE:-${workspace}/../OpenROAD}"
native_source="${OPENROAD_NATIVE_SOURCE:-${workspace}/../OpenROAD-eccdb}"
native_build="${OPENROAD_NATIVE_BUILD:-${workspace}/build/openroad-native-eccdb-build}"

if [[ ! -f "${native_source}/src/drt/src/io/EccdbRouteAdapter.cpp" ]]; then
  echo "Native OpenROAD fork is missing: ${native_source}" >&2
  exit 1
fi
source "${dependencies}/scripts/eccdb-local-env.sh"
export OPENROAD_ECCDB_ROOT="${native_source}"
export OPENROAD_ECCDB_SOURCE_DIR="${adapter_dir}/../.."
export CCACHE_DIR="${workspace}/build/ccache-native"
export CCACHE_MAXSIZE=20G
ulimit -v 524288000

openroad_eccdb_configure "${native_build}" \
  -DCMAKE_C_COMPILER=gcc-13 -DCMAKE_CXX_COMPILER=g++-13 \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache

if ((configure_only)); then
  echo 'Configuration complete; skipping target build (--configure-only).'
  exit 0
fi

cmake --build "${native_build}" --target eccdbNativeRoute eccdbDrtTraceOdb \
  --parallel "${OPENROAD_ECCDB_JOBS:-64}"
