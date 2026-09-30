#!/usr/bin/env bash
set -euo pipefail

adapter_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
workspace="$(git -C "${adapter_dir}" rev-parse --show-toplevel)"
dataset="${1:-${workspace}/../reference/ispd2018/ispd18_test6}"
case_name="$(basename "${dataset}")"
results="${2:-$(mktemp -d /tmp/eccdb-native-route.XXXXXX)}"
threads="${3:-64}"
route_options=("${@:4}")
native_build="${OPENROAD_NATIVE_BUILD:-${workspace}/build/openroad-native-eccdb-build}"
source "${OPENROAD_DEPENDENCY_SOURCE:-${workspace}/../OpenROAD}/scripts/eccdb-local-env.sh"
ulimit -v 524288000
mkdir -p "${results}"
echo "Results: ${results}"

for backend in odb eccdb; do
  "${native_build}/src/drt/eccdbNativeRoute" --backend "${backend}" \
    --lef "${dataset}/${case_name}.input.lef" --def "${dataset}/${case_name}.input.def" \
    --import-only --output "${results}/${backend}-import" \
    > "${results}/${backend}-import.log" 2>&1
  jq -e --arg backend "${backend}" \
    '.backend == $backend and .native == ($backend == "eccdb") and .import_only == true' \
    "${results}/${backend}-import/metrics.json" > /dev/null
done
cmp "${results}/odb-import/snapshot.txt" "${results}/eccdb-import/snapshot.txt"
cmp "${results}/odb-import/placement.txt" "${results}/eccdb-import/placement.txt"
for backend in odb eccdb; do
  jq -S '.routing_configuration' "${results}/${backend}-import/metrics.json" > "${results}/${backend}-import/configuration.json"
done
cmp "${results}/odb-import/configuration.json" "${results}/eccdb-import/configuration.json"
echo "Import snapshots match."

for backend in odb eccdb; do
  "${native_build}/src/drt/eccdbNativeRoute" --backend "${backend}" \
    --lef "${dataset}/${case_name}.input.lef" --def "${dataset}/${case_name}.input.def" \
    --guides "${dataset}/${case_name}.input.guide" --threads "${threads}" \
    "${route_options[@]}" --output "${results}/${backend}-route" > "${results}/${backend}-route.log" 2>&1
  jq -e --arg backend "${backend}" \
    '.backend == $backend and .native == ($backend == "eccdb") and .import_only == false and .drc == 0' \
    "${results}/${backend}-route/metrics.json" > /dev/null
done
cmp "${results}/odb-route/snapshot.txt" "${results}/eccdb-route/snapshot.txt"
cmp "${results}/odb-route/placement.txt" "${results}/eccdb-route/placement.txt"
for backend in odb eccdb; do
  jq -S '.routing_configuration' "${results}/${backend}-route/metrics.json" > "${results}/${backend}-route/configuration.json"
done
cmp "${results}/odb-route/configuration.json" "${results}/eccdb-route/configuration.json"
cmp "${results}/odb-route/routing.drc.rpt" "${results}/eccdb-route/routing.drc.rpt"
echo "Routed snapshots match."

for backend in odb eccdb; do
  awk '/^net / && / route /' "${results}/${backend}-route/snapshot.txt" \
    > "${results}/${backend}-route-primitives.txt"
  test -s "${results}/${backend}-route-primitives.txt"
  for reader in odb eccdb; do
    restored="${results}/${backend}-restored-by-${reader}"
    "${native_build}/src/drt/eccdbNativeRoute" --backend "${reader}" \
      --lef "${dataset}/${case_name}.input.lef" --def "${results}/${backend}-route/output.def" \
      --import-only --output "${restored}" > "${restored}.log" 2>&1
    jq -e --arg backend "${reader}" \
      '.backend == $backend and .native == ($backend == "eccdb") and .import_only == true' \
      "${restored}/metrics.json" > /dev/null
    awk '/^net / && / route /' "${restored}/snapshot.txt" > "${restored}-primitives.txt"
    cmp "${results}/${backend}-route-primitives.txt" "${restored}-primitives.txt"
    cmp "${results}/${backend}-route/design.txt" "${restored}/design.txt"
    cmp "${results}/${backend}-route/placement.txt" "${restored}/placement.txt"
  done
done
echo "Both exported DEF files preserve geometry and connectivity on OpenDB and EccDB reimport."
