#!/usr/bin/env bash
set -euo pipefail

adapter_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
workspace="$(git -C "${adapter_dir}" rev-parse --show-toplevel)"
dataset="${1:-${workspace}/../reference/ispd2018/ispd18_test6}"
case_name="$(basename "${dataset}")"
results="${2:-$(mktemp -d /tmp/eccdb-native-import.XXXXXX)}"
native_build="${OPENROAD_NATIVE_BUILD:-${workspace}/build/openroad-native-eccdb-build}"
source "${OPENROAD_DEPENDENCY_SOURCE:-${workspace}/../OpenROAD}/scripts/eccdb-local-env.sh"
ulimit -v 524288000
mkdir -p "${results}"
echo "Results: ${results}"

# Each backend runs in a fresh process. drt_ms starts after LEF/DEF parsing.
for repetition in 1 2 3; do
  for backend in odb eccdb; do
    "${native_build}/src/drt/eccdbNativeRoute" --backend "${backend}" \
      --lef "${dataset}/${case_name}.input.lef" --def "${dataset}/${case_name}.input.def" \
      --import-only --output "${results}/${repetition}-${backend}" \
      > "${results}/${repetition}-${backend}.log" 2>&1
    jq -e --arg backend "${backend}" \
      '.backend == $backend and .native == ($backend == "eccdb") and .import_only == true' \
      "${results}/${repetition}-${backend}/metrics.json" > /dev/null
    jq -S '.routing_configuration' "${results}/${repetition}-${backend}/metrics.json" \
      > "${results}/${repetition}-${backend}/configuration.json"
  done
  cmp "${results}/${repetition}-odb/snapshot.txt" "${results}/${repetition}-eccdb/snapshot.txt"
  cmp "${results}/${repetition}-odb/placement.txt" "${results}/${repetition}-eccdb/placement.txt"
  cmp "${results}/${repetition}-odb/configuration.json" "${results}/${repetition}-eccdb/configuration.json"
done

jq -s '
  def median: sort | .[length / 2 | floor];
  group_by(.backend) | map({
    backend: .[0].backend,
    repetitions: length,
    drt_ms: (map(.drt_ms) | median),
    rss_loaded_kib: (map(.rss_loaded_kib) | median),
    rss_drt_kib: (map(.rss_drt_kib) | median),
    peak_before_snapshots_kib: (map(.peak_before_snapshots_kib) | median)
  })
' "${results}"/{1,2,3}-{odb,eccdb}/metrics.json | tee "${results}/summary.json"
