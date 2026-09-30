#!/usr/bin/env bash
set -euo pipefail

adapter_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
workspace="$(git -C "${adapter_dir}" rev-parse --show-toplevel)"
dependencies="${OPENROAD_DEPENDENCY_SOURCE:-${workspace}/../OpenROAD}"
native_source="${OPENROAD_NATIVE_SOURCE:-${workspace}/build/openroad-native-eccdb-src}"
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
cmake --build "${native_build}" --target eccdbNativeRoute eccdbDrtTraceOdb \
  --parallel "${OPENROAD_ECCDB_JOBS:-64}"
