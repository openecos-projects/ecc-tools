#!/usr/bin/env bash
set -euo pipefail

adapter_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
workspace="$(git -C "${adapter_dir}" rev-parse --show-toplevel)"
dependencies="${OPENROAD_DEPENDENCY_SOURCE:-${workspace}/../OpenROAD}"
native_source="${OPENROAD_NATIVE_SOURCE:-${workspace}/build/openroad-native-eccdb-src}"
native_build="${OPENROAD_NATIVE_BUILD:-${workspace}/build/openroad-native-eccdb-build}"
binary="${native_build}/src/drt/eccdbNativeRoute"
fixture_dir="${adapter_dir}/tests"
default_lef="${workspace}/../reference/ispd2018/ispd18_sample2/ispd18_sample2.input.lef"
lef="${1:-${default_lef}}"
results="$(mktemp -d /tmp/eccdb-native-fixtures.XXXXXX)"

if (($# > 1)); then
  echo "usage: $0 [sample2-lef]" >&2
  exit 2
fi
if [[ ! -f "${native_source}/src/drt/src/io/EccdbRouteAdapter.cpp" ]]; then
  echo "Native OpenROAD fork is missing: ${native_source}" >&2
  exit 1
fi
if [[ ! -x "${binary}" ]]; then
  echo "Native route binary is missing: ${binary}" >&2
  echo "Build it with ${adapter_dir}/BuildNativeRoute.sh first." >&2
  exit 1
fi
if [[ ! -f "${lef}" ]]; then
  echo "LEF is missing: ${lef}" >&2
  exit 1
fi

# Keep the runtime environment aligned with BuildNativeRoute.sh without building.
source "${dependencies}/scripts/eccdb-local-env.sh"
export OPENROAD_ECCDB_ROOT="${native_source}"
export OPENROAD_ECCDB_SOURCE_DIR="${adapter_dir}/../.."
ulimit -v 524288000

echo "Results: ${results}"

fail() {
  echo "$*" >&2
  exit 1
}

assert_same() {
  local expected="$1"
  local actual="$2"
  if ! cmp -s "${expected}" "${actual}"; then
    echo "Files differ: ${expected} ${actual}" >&2
    diff -u "${expected}" "${actual}" >&2 || true
    exit 1
  fi
}

run_import() {
  local backend="$1"
  local def="$2"
  local output="$3"
  local log="${output}.log"
  if ! "${binary}" --backend "${backend}" --lef "${lef}" --def "${def}" \
      --import-only --output "${output}" >"${log}" 2>&1; then
    echo "${backend} import failed for ${def}; log follows:" >&2
    tail -100 "${log}" >&2 || true
    return 1
  fi
  jq -e --arg backend "${backend}" \
    '.backend == $backend and .native == ($backend == "eccdb") and .import_only == true' \
    "${output}/metrics.json" >/dev/null \
    || fail "Metrics do not identify the requested ${backend} import: ${output}/metrics.json"
}

run_fixture_imports() {
  local fixture="$1"
  local name="$2"
  run_import odb "${fixture}" "${results}/${name}-odb"
  run_import eccdb "${fixture}" "${results}/${name}-eccdb"
  assert_same "${results}/${name}-odb/snapshot.txt" "${results}/${name}-eccdb/snapshot.txt"
  assert_same "${results}/${name}-odb/placement.txt" "${results}/${name}-eccdb/placement.txt"
}

orientations="${fixture_dir}/NativeOrientations.def"
geometry="${fixture_dir}/NativeRouteGeometry.def"
unsupported="${fixture_dir}/NativeRouteUnsupported.def"
special="${fixture_dir}/NativeSpecialGeometry.def"
special_polygon="${fixture_dir}/NativeSpecialPolygonUnsupported.def"
for fixture in "${orientations}" "${geometry}" "${special}" "${unsupported}" "${special_polygon}"; do
  [[ -f "${fixture}" ]] || fail "Fixture is missing: ${fixture}"
done

run_fixture_imports "${orientations}" orientations
echo "Orientation raw snapshots match."

run_fixture_imports "${geometry}" geometry
for writer in odb eccdb; do
  writer_output="${results}/geometry-${writer}"
  rg -q '^net .* route ' "${writer_output}/snapshot.txt" \
    || fail "Geometry import has no route records: ${writer}"
done
assert_same "${results}/geometry-odb/design.txt" "${results}/geometry-eccdb/design.txt"

run_fixture_imports "${special}" special
rg -q '^net "VDD" route ' "${results}/special-eccdb/snapshot.txt" \
  || fail "SPECIALNET fixture has no imported route geometry"
run_fixture_imports "${unsupported}" virtual

for fixture in orientations geometry; do
  for writer in odb eccdb; do
    writer_output="${results}/${fixture}-${writer}"
    for reader in odb eccdb; do
      restored="${results}/${fixture}-${writer}-by-${reader}"
      run_import "${reader}" "${writer_output}/output.def" "${restored}"
      assert_same "${results}/${fixture}-odb/design.txt" "${restored}/design.txt"
      assert_same "${results}/${fixture}-odb/placement.txt" "${restored}/placement.txt"
    done
  done
done
echo "Geometry, orientation and placement snapshots match on all writer/reader round-trips."

echo "Virtual route and its physical geometry import match."

for backend in odb eccdb; do
  rejected="${results}/special-polygon-${backend}"
  if "${binary}" --backend "${backend}" --lef "${lef}" --def "${special_polygon}" \
      --import-only --output "${rejected}" >"${rejected}.log" 2>&1; then
    fail "${backend} unexpectedly accepted a SPECIALNET POLYGON"
  fi
done
echo "Both backends reject SPECIALNET POLYGON."
