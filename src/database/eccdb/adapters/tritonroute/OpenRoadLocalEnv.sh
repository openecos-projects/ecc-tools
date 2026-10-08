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

# Source this file before configuring the EccDB-enabled CMake build:
#   source scripts/eccdb-local-env.sh
#   openroad_eccdb_configure build-eccdb
#   openroad_eccdb_build build-eccdb
#
# It only changes the current shell. Dependencies deliberately live in
# versioned .local directories so an OpenROAD checkout's existing tools and
# system packages remain untouched.

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  echo "Source this script instead: source ${BASH_SOURCE[0]}" >&2
  exit 1
fi

_openroad_eccdb_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
_openroad_eccdb_local="${OPENROAD_ECCDB_LOCAL_PREFIX:-${_openroad_eccdb_root}/.local}"

export OPENROAD_ECCDB_ROOT="${_openroad_eccdb_root}"
export OPENROAD_ECCDB_LOCAL_PREFIX="${_openroad_eccdb_local}"
export OPENROAD_ECCDB_SWIG_ROOT="${_openroad_eccdb_local}/swig-4.3.1"
export OPENROAD_ECCDB_FMT_ROOT="${_openroad_eccdb_local}/fmt-12.1"
export OPENROAD_ECCDB_SPDLOG_ROOT="${_openroad_eccdb_local}/spdlog-1.17"
export OPENROAD_ECCDB_LEMON_ROOT="${_openroad_eccdb_local}/lemon-1.3.1"
export OPENROAD_ECCDB_GTEST_ROOT="${_openroad_eccdb_local}/gtest-1.14.0"
export OPENROAD_ECCDB_ORTOOLS_ROOT="${_openroad_eccdb_local}/or-tools"
export OPENROAD_ECCDB_YAML_CPP_ROOT="${_openroad_eccdb_local}/yaml-cpp"
export OPENROAD_ECCDB_CUDD_ROOT="${_openroad_eccdb_local}/cudd"
export OPENROAD_ECCDB_FMT_SOURCE_DIR="${_openroad_eccdb_root}/build-standalone/_deps/fmt-src"
export OPENROAD_ECCDB_SOURCE_DIR="${OPENROAD_ECCDB_SOURCE_DIR:-$(cd "${_openroad_eccdb_root}/../ecc-tools/src/database/eccdb" && pwd -P)}"
# The OR-Tools bundle contains Boost headers/configuration but not all compiled
# Boost components. OpenROAD needs system Boost.Iostreams, so pin Boost's
# top-level config before exposing the OR-Tools prefix to CMake.
export OPENROAD_ECCDB_SYSTEM_BOOST_DIR="${OPENROAD_ECCDB_SYSTEM_BOOST_DIR:-/usr/lib/x86_64-linux-gnu/cmake/Boost-1.83.0}"

_openroad_eccdb_missing=()
for _openroad_eccdb_path in \
  "${OPENROAD_ECCDB_SWIG_ROOT}/bin/swig" \
  "${OPENROAD_ECCDB_FMT_ROOT}/lib/cmake/fmt/fmt-config.cmake" \
  "${OPENROAD_ECCDB_SPDLOG_ROOT}/lib/cmake/spdlog/spdlogConfig.cmake" \
  "${OPENROAD_ECCDB_LEMON_ROOT}/share/lemon/cmake/LEMONConfig.cmake" \
  "${OPENROAD_ECCDB_GTEST_ROOT}/lib/cmake/GTest/GMockTargets.cmake" \
  "${OPENROAD_ECCDB_ORTOOLS_ROOT}/lib/cmake/ortools/ortoolsConfig.cmake" \
  "${OPENROAD_ECCDB_YAML_CPP_ROOT}/lib/cmake/yaml-cpp/yaml-cpp-config.cmake" \
  "${OPENROAD_ECCDB_CUDD_ROOT}/include/cudd.h" \
  "${OPENROAD_ECCDB_SYSTEM_BOOST_DIR}/BoostConfig.cmake" \
  "${OPENROAD_ECCDB_SOURCE_DIR}/CMakeLists.txt" \
  "${OPENROAD_ECCDB_FMT_SOURCE_DIR}/CMakeLists.txt"; do
  [[ -e "${_openroad_eccdb_path}" ]] || _openroad_eccdb_missing+=("${_openroad_eccdb_path}")
done

if (( ${#_openroad_eccdb_missing[@]} )); then
  printf 'OpenROAD EccDB local environment is incomplete:\n' >&2
  printf '  %s\n' "${_openroad_eccdb_missing[@]}" >&2
  unset _openroad_eccdb_path _openroad_eccdb_missing
  return 1
fi
unset _openroad_eccdb_path _openroad_eccdb_missing

export PATH="${OPENROAD_ECCDB_SWIG_ROOT}/bin:${PATH}"

# CMake reads environment CMAKE_PREFIX_PATH with the host path separator.
# Keep OR-Tools out of these global search paths: its partial Boost 1.87
# package can otherwise eclipse the system Boost library components that
# OpenROAD itself needs. Its required package directories are passed directly
# to CMake below.
export CMAKE_PREFIX_PATH="${OPENROAD_ECCDB_SPDLOG_ROOT}:${OPENROAD_ECCDB_FMT_ROOT}:${OPENROAD_ECCDB_GTEST_ROOT}:${OPENROAD_ECCDB_YAML_CPP_ROOT}${CMAKE_PREFIX_PATH:+:${CMAKE_PREFIX_PATH}}"
export CMAKE_LIBRARY_PATH="${OPENROAD_ECCDB_SPDLOG_ROOT}/lib:${OPENROAD_ECCDB_FMT_ROOT}/lib:${OPENROAD_ECCDB_GTEST_ROOT}/lib:${OPENROAD_ECCDB_YAML_CPP_ROOT}/lib${CMAKE_LIBRARY_PATH:+:${CMAKE_LIBRARY_PATH}}"
export CMAKE_INCLUDE_PATH="${OPENROAD_ECCDB_SPDLOG_ROOT}/include:${OPENROAD_ECCDB_FMT_ROOT}/include:${OPENROAD_ECCDB_GTEST_ROOT}/include:${OPENROAD_ECCDB_YAML_CPP_ROOT}/include:${OPENROAD_ECCDB_CUDD_ROOT}/include${CMAKE_INCLUDE_PATH:+:${CMAKE_INCLUDE_PATH}}"
export LD_LIBRARY_PATH="${OPENROAD_ECCDB_SPDLOG_ROOT}/lib:${OPENROAD_ECCDB_FMT_ROOT}/lib:${OPENROAD_ECCDB_ORTOOLS_ROOT}/lib:${OPENROAD_ECCDB_YAML_CPP_ROOT}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

# These are consumed by CMake's FindSWIG, FindCUDD, and config-mode package
# lookup. The configure helper below passes their corresponding CMake cache
# entries explicitly as well, so a fresh build directory is deterministic.
export SWIG_ROOT="${OPENROAD_ECCDB_SWIG_ROOT}"
export CUDD_DIR="${OPENROAD_ECCDB_CUDD_ROOT}"
export LEMON_ROOT="${OPENROAD_ECCDB_LEMON_ROOT}"
export GTest_ROOT="${OPENROAD_ECCDB_GTEST_ROOT}"
export ortools_ROOT="${OPENROAD_ECCDB_ORTOOLS_ROOT}"
# Shell identifiers cannot contain '-'.  The configure helper supplies the
# exact CMake cache spelling (yaml-cpp_ROOT) below.
export YAML_CPP_ROOT="${OPENROAD_ECCDB_YAML_CPP_ROOT}"
export fmt_DIR="${OPENROAD_ECCDB_FMT_ROOT}/lib/cmake/fmt"
export spdlog_DIR="${OPENROAD_ECCDB_SPDLOG_ROOT}/lib/cmake/spdlog"

openroad_eccdb_configure() {
  local build_dir="${1:-${OPENROAD_ECCDB_ROOT}/build-eccdb}"
  shift || true

  cmake -S "${OPENROAD_ECCDB_ROOT}" -B "${build_dir}" -G Ninja \
    -DENABLE_TESTS=OFF \
    -DBUILD_GUI=OFF \
    -DBUILD_PYTHON=OFF \
    -DLINK_TIME_OPTIMIZATION=OFF \
    -DDRT_USE_ECCDB=ON \
    -DDRT_ECCDB_SOURCE_DIR="${OPENROAD_ECCDB_SOURCE_DIR}" \
    -DSWIG_EXECUTABLE="${OPENROAD_ECCDB_SWIG_ROOT}/bin/swig" \
    -DSWIG_DIR="${OPENROAD_ECCDB_SWIG_ROOT}/share/swig/4.3.1" \
    -DCUDD_DIR="${OPENROAD_ECCDB_CUDD_ROOT}" \
    -Dfmt_DIR="${OPENROAD_ECCDB_FMT_ROOT}/lib/cmake/fmt" \
    -Dspdlog_DIR="${OPENROAD_ECCDB_SPDLOG_ROOT}/lib/cmake/spdlog" \
    -DLEMON_ROOT="${OPENROAD_ECCDB_LEMON_ROOT}" \
    -DLEMON_DIR="${OPENROAD_ECCDB_LEMON_ROOT}/share/lemon/cmake" \
    -DLEMON_INCLUDE_DIR="${OPENROAD_ECCDB_LEMON_ROOT}/include" \
    -DLEMON_LIBRARY="${OPENROAD_ECCDB_LEMON_ROOT}/lib/libemon.a" \
    -DGTest_ROOT="${OPENROAD_ECCDB_GTEST_ROOT}" \
    -DGTest_DIR="${OPENROAD_ECCDB_GTEST_ROOT}/lib/cmake/GTest" \
    -DBoost_DIR="${OPENROAD_ECCDB_SYSTEM_BOOST_DIR}" \
    -Dortools_ROOT="${OPENROAD_ECCDB_ORTOOLS_ROOT}" \
    -Dortools_DIR="${OPENROAD_ECCDB_ORTOOLS_ROOT}/lib/cmake/ortools" \
    -DZLIB_ROOT="${OPENROAD_ECCDB_ORTOOLS_ROOT}" \
    -Dabsl_DIR="${OPENROAD_ECCDB_ORTOOLS_ROOT}/lib/cmake/absl" \
    -DProtobuf_DIR="${OPENROAD_ECCDB_ORTOOLS_ROOT}/lib/cmake/protobuf" \
    -Dutf8_range_DIR="${OPENROAD_ECCDB_ORTOOLS_ROOT}/lib/cmake/utf8_range" \
    -Dre2_DIR="${OPENROAD_ECCDB_ORTOOLS_ROOT}/lib/cmake/re2" \
    -DCoinUtils_DIR="${OPENROAD_ECCDB_ORTOOLS_ROOT}/lib/cmake/CoinUtils" \
    -DOsi_DIR="${OPENROAD_ECCDB_ORTOOLS_ROOT}/lib/cmake/Osi" \
    -DClp_DIR="${OPENROAD_ECCDB_ORTOOLS_ROOT}/lib/cmake/Clp" \
    -DCgl_DIR="${OPENROAD_ECCDB_ORTOOLS_ROOT}/lib/cmake/Cgl" \
    -DCbc_DIR="${OPENROAD_ECCDB_ORTOOLS_ROOT}/lib/cmake/Cbc" \
    -Dhighs_DIR="${OPENROAD_ECCDB_ORTOOLS_ROOT}/lib/cmake/highs" \
    -DEigen3_DIR="${OPENROAD_ECCDB_ORTOOLS_ROOT}/share/eigen3/cmake" \
    -DSCIP_DIR="${OPENROAD_ECCDB_ORTOOLS_ROOT}/lib/cmake/scip" \
    -Dyaml-cpp_ROOT="${OPENROAD_ECCDB_YAML_CPP_ROOT}" \
    -DFETCHCONTENT_SOURCE_DIR_FMT="${OPENROAD_ECCDB_FMT_SOURCE_DIR}" \
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
    "$@"
}

openroad_eccdb_build() {
  local build_dir="${1:-${OPENROAD_ECCDB_ROOT}/build-eccdb}"
  shift || true
  cmake --build "${build_dir}" --target drt_lib --parallel "${OPENROAD_ECCDB_JOBS:-$(nproc)}" "$@"
}

unset _openroad_eccdb_root _openroad_eccdb_local
