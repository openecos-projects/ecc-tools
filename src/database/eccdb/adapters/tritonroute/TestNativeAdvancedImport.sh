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

adapter_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
workspace="$(git -C "${adapter_dir}" rev-parse --show-toplevel)"
reference="${1:-${workspace}/../reference/ispd2019}"
results="${2:-$(mktemp -d /tmp/eccdb-native-advanced.XXXXXX)}"
native_build="${OPENROAD_NATIVE_BUILD:-${workspace}/build/openroad-native-eccdb-build}"
binary="${native_build}/src/drt/eccdbNativeRoute"
source "${OPENROAD_DEPENDENCY_SOURCE:-${workspace}/../OpenROAD}/scripts/eccdb-local-env.sh"
ulimit -v 524288000
mkdir -p "${results}"
echo "Results: ${results}"

run_case() {
  local case_name="$1"
  local lef="$2"
  local def="$3"
  local backend
  for backend in odb eccdb; do
    "${binary}" --backend "${backend}" --lef "${lef}" --def "${def}" \
      --import-only --output "${results}/${case_name}-${backend}" \
      > "${results}/${case_name}-${backend}.log" 2>&1
    jq -e --arg backend "${backend}" \
      '.backend == $backend and .native == ($backend == "eccdb") and .import_only == true' \
      "${results}/${case_name}-${backend}/metrics.json" > /dev/null
  done
  cmp "${results}/${case_name}-odb/snapshot.txt" "${results}/${case_name}-eccdb/snapshot.txt"
  cmp "${results}/${case_name}-odb/placement.txt" "${results}/${case_name}-eccdb/placement.txt"
  jq -se '.[0] as $odb | .[1] as $eccdb |
    ["instances", "nets", "special_nets", "vias", "via_rules", "ndrs"] |
    all(.[]; $odb[.] == $eccdb[.])' \
    "${results}/${case_name}-odb/metrics.json" "${results}/${case_name}-eccdb/metrics.json" > /dev/null
  echo "${case_name}: ODB/EccDB import snapshots match"
}

test5="${reference}/ispd19_test5"
test10="${reference}/ispd19_test10"
# This DEF places GROUPS and REGIONS before COMPONENTS. OpenDB resolves group
# members immediately, so move only those complete sections behind COMPONENTS
# for a common importable fixture; leave the source dataset untouched.
test5_ordered="${results}/ispd19_test5.ordered.def"
awk '
  /^GROUPS [0-9]+ ;/ { section = "groups" }
  /^REGIONS [0-9]+ ;/ { section = "regions" }
  section == "groups" {
    groups[++group_count] = $0
    if (/^END GROUPS/) section = ""
    next
  }
  section == "regions" {
    regions[++region_count] = $0
    if (/^END REGIONS/) section = ""
    next
  }
  {
    print
    if (/^END COMPONENTS/) {
      for (row = 1; row <= region_count; ++row) print regions[row]
      for (row = 1; row <= group_count; ++row) print groups[row]
    }
  }
' "${test5}/ispd19_test5.input.def" > "${test5_ordered}"
run_case test5 "${test5}/ispd19_test5.input.lef" "${test5_ordered}"
rg -q '^ndr ' "${results}/test5-eccdb/snapshot.txt"
rg -q '^net "vss" route ' "${results}/test5-eccdb/snapshot.txt"

run_case generated_via "${test5}/ispd19_test5.input.lef" "${adapter_dir}/tests/NativeGeneratedVia.def"
rg -q '^via "VGEN_DEF" default ' "${results}/generated_via-eccdb/snapshot.txt"
rg -q '^net "via_net" route ' "${results}/generated_via-eccdb/snapshot.txt"

run_case test10 "${test10}/ispd19_test10.input.lef" "${test10}/ispd19_test10.input.def"
rg -q '^layer "Metal2" corner ' "${results}/test10-eccdb/snapshot.txt"
rg -q '^net "VDD" route ' "${results}/test10-eccdb/snapshot.txt"
