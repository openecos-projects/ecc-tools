#!/usr/bin/env bash
set -euo pipefail

# Build ecc-tools wheel: CMake build → collect .so → uv build → auditwheel repair → smoke test
# Intended to run inside manylinux_2_34_x86_64 container or equivalent.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$REPO_ROOT"

# ── 0. Validate environment ──────────────────────────────────────────────

if [[ "${OSTYPE:-}" != linux* ]]; then
    echo "ERROR: wheel build is only supported on Linux." >&2
    exit 1
fi

PYTHON3="${PYTHON3:-$(command -v python3)}"
if [[ ! -x "$PYTHON3" ]]; then
    echo "ERROR: python3 not found." >&2
    exit 1
fi

for cmd in cmake ninja auditwheel uv sha256sum; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "ERROR: required command not found: $cmd" >&2
        exit 1
    fi
done

# ── 1. CMake configure ───────────────────────────────────────────────────

echo "[build-wheel] CMake configure"
cmake_launcher_args=()
if command -v sccache >/dev/null 2>&1; then
    cmake_launcher_args+=(
        -DCMAKE_CXX_COMPILER_LAUNCHER=sccache
        -DCMAKE_C_COMPILER_LAUNCHER=sccache
    )
    echo "[build-wheel] sccache enabled"
fi

cmake -B build -G Ninja \
    -DBUILD_ECOS=ON \
    -DBUILD_PYTHON=ON \
    -DBUILD_STATIC_LIB=OFF \
    -DCOMPATIBILITY_MODE=ON \
    -DCMAKE_BUILD_TYPE=Release \
    -DPython3_EXECUTABLE="$PYTHON3" \
    -DPython3_ROOT_DIR="$("$PYTHON3" -c "import sys; print(sys.prefix)")" \
    -DPython3_FIND_STRATEGY=LOCATION \
    "${cmake_launcher_args[@]}"

# ── 2. CMake build ───────────────────────────────────────────────────────

echo "[build-wheel] CMake build ($(nproc) jobs)"
cmake --build build --target ecc_py -j"$(nproc)"

# ── 3. Collect artifacts ─────────────────────────────────────────────────

echo "[build-wheel] Collecting .so artifacts"
ECC_PY_ABI="$("$PYTHON3" -c "import sysconfig; print(sysconfig.get_config_var('EXT_SUFFIX'))")"

# Find the ecc_py shared object (CMake outputs to bin/ or build/ depending on config)
ecc_py_so="$(find build bin -name "ecc_py${ECC_PY_ABI}" -print -quit 2>/dev/null)"
if [[ -z "$ecc_py_so" ]]; then
    echo "ERROR: ecc_py${ECC_PY_ABI} not found in build/ or bin/" >&2
    exit 1
fi
echo "[build-wheel] Found: $ecc_py_so"

# Copy into the Python package directory
cp -f "$ecc_py_so" "ecc_tools_bin/"
echo "[build-wheel] Copied ecc_py to ecc_tools_bin/"

# ── 4. Build raw wheel ──────────────────────────────────────────────────

echo "[build-wheel] Building raw wheel"
raw_out="dist/wheel/raw"
mkdir -p "$raw_out"
uv build --wheel --out-dir "$raw_out"

raw_whl="$(find "$raw_out" -name 'ecc_tools-*.whl' -print -quit)"
if [[ -z "$raw_whl" ]]; then
    echo "ERROR: raw wheel not found in $raw_out" >&2
    exit 1
fi
echo "[build-wheel] Raw wheel: $raw_whl"

# ── 5. auditwheel repair ────────────────────────────────────────────────

echo "[build-wheel] auditwheel show/repair"
repair_out="dist/wheel/repaired"
report_out="dist/wheel/reports"
mkdir -p "$repair_out" "$report_out"

show_report="$report_out/show.txt"
{
    echo "=== $(basename "$raw_whl") ==="
    auditwheel show "$raw_whl"
    echo
} > "$show_report"

auditwheel repair "$raw_whl" -w "$repair_out"

shopt -s nullglob
repaired_wheels=("$repair_out"/ecc_tools-*.whl)
if [[ ${#repaired_wheels[@]} -eq 0 ]]; then
    echo "ERROR: no repaired ecc_tools wheel found in $repair_out" >&2
    exit 1
fi
shopt -u nullglob

# ── 6. Smoke test ────────────────────────────────────────────────────────

echo "[build-wheel] Running smoke test"
smoke_dir="$(mktemp -d)"
cleanup() { rm -rf "$smoke_dir"; }
trap cleanup EXIT

"$PYTHON3" -m pip install --target "$smoke_dir/site" "${repaired_wheels[@]}"
PYTHONPATH="$smoke_dir/site" "$PYTHON3" -c "
from ecc_tools_bin import ecc_py

required = ['flow_init', 'flow_exit', 'db_init', 'def_init', 'lef_init',
            'def_save', 'run_placer', 'run_cts', 'run_rt', 'run_drc',
            'run_filler', 'init_floorplan', 'report_db', 'feature_summary']
missing = [f for f in required if not callable(getattr(ecc_py, f, None))]
assert not missing, f'missing or non-callable bindings: {missing}'

print(f'ecc_py smoke test passed: {len(required)} bindings verified')
"

# ── 7. Checksums ─────────────────────────────────────────────────────────

(
    cd "$repair_out"
    sha256sum ./*.whl > "$REPO_ROOT/dist/wheel/SHA256SUMS"
)

echo "[build-wheel] done"
echo "[build-wheel] raw wheels:      $raw_out"
echo "[build-wheel] repaired wheels: $repair_out"
echo "[build-wheel] report:          $show_report"
echo "[build-wheel] checksums:       dist/wheel/SHA256SUMS"
