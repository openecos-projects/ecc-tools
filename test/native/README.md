# EccDB Operation differential probes

These tests call each Operation interface directly. EccDB workers own an
`eccdb::Database`. iFP, iRT, iDRC, iSTA, iPW, iEMIR, iLVS and iMJ bind its
`DatabaseContext` through their free functions in `eccdb::adapters`; iCTS
receives its concrete adaptor through the generic `setDesignSource()` interface. See the [adaptor interfaces](../../src/database/eccdb/adapters/README.md).
The extra platform orchestration class, executable and Python binding were removed.
Operation adapters and the independent EccDB viewer bindings remain available.

## Build

```sh
cmake -S . -B /path/to/native-build \
  -DCMAKE_CXX_COMPILER=g++-13 -DCMAKE_C_COMPILER=gcc-13 \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DECCDB_BUILD_TESTS=ON -DECCDB_BUILD_BENCHMARKS=OFF -DCMD_BUILD=ON
```

The retired orchestration scripts and their instructions are archived outside
the repository at
`/home/zhuxu1/workspace/daily/2026年10月26日/merge-source-backup-20261026/original-native-removal-source.tar.gz`.
Their historical Tiny/RV64 results do not constitute a new run. Standalone
iFP, iPW and iMJ probes and comparators below remain supported.

## Paired iPW differential

For a paired iDB/EccDB correctness run, build the explicit probe target and
give the runner the same frozen inputs for both fresh processes:

```bash
cmake --build /path/to/native-build --target ipw_pair_probe -j8
python3 test/native/run_ipw_pair_differential.py \
  --binary /path/to/native-build/bin/ipw_pair_probe \
  --output-root /path/to/fresh-power-pair \
  --case tiny /path/to/manifest.json /path/to/frozen.def \
  --threads 2 --seed 20261022
```

The manifest contains `lef_files`, `liberty_files`, `sdc_file`, `def_file`
and optional `spef_file`/`vcd_file`. File overrides are also accepted as
`--case-spef tiny=/path/to/design.spef` and
`--case-vcd tiny=/path/to/activity.vcd`. Add `--compare-only` to inspect an
existing pair. It compares the wrapped instance/pin/net/library/constraint
model, computed instance/net/group/total power, and the native TSV report.
Library table payloads are fingerprinted, including axes and values. The
comparator tests can run without building or importing the native module:

```bash
python3 -m unittest discover -s test/native -p test_run_ipw_pair_differential.py
```

The strict gate preserves raw identifier spelling and endpoint sequence.
A separate name-only diagnosis does not erase strict differences. In the
2026-10-22 ICS55 Tiny/RV64 run, instance and total power matched exactly,
while the strict gate retained the already known DEF Net escape-spelling
differences. GCD and Sky130 Tiny with real SPEF matched strictly. These
are differential correctness checks, not accuracy calibration against
silicon or a performance benchmark.

## Paired iFP differential

Build `ifp_pair_probe` and `eccdb_def_semantic_probe` with the iFP adapter and
differential test options enabled. Each backend runs in a separate fresh process
because both the iFP work model and the legacy data manager are singletons:

```sh
cmake --build /path/to/native-build --target ifp_pair_probe eccdb_def_semantic_probe -j16
python3 test/native/prepare_ifp_cases.py --workspace /path/to/workspace \
  --output-root /path/to/new-ifp-experiment --threads 2
python3 test/native/paired_ifp.py --binary /path/to/native-build/bin/ifp_pair_probe \
  --manifest /path/to/new-ifp-experiment/gcd-ics55/manifest.json \
  --output-root /path/to/new-ifp-experiment/gcd-ics55/run \
  --semantic-probe /path/to/native-build/bin/eccdb_def_semantic_probe
python3 test/native/compare_ifp_odb.py --openroad /path/to/openroad \
  --manifest /path/to/new-ifp-experiment/gcd-ics55/manifest.json \
  --pair-root /path/to/new-ifp-experiment/gcd-ics55/run
```

For a separate experiment isolating IO iteration order, use a fresh output
directory and add `--io-order-control stable-exact-name` to `paired_ifp.py`.
The probe preserves the original `input-model.json`, then sorts only the
temporary iFP IO list after `initFP()` and before `runFP()`. It rebuilds the
IO name-to-index map and saves `controlled-input-model.json` plus
`io-order-control.json`. Exact names are compared without unescaping, case
folding, or merging; other input fields are not changed. Production adapters
and storage retain their existing traversal order. A successful order control
only establishes a common IO permutation, not equality of the complete input
models. Keep this controlled result separate from the default end-to-end gate.

To compare the two adaptors' actual iFP working databases, run the independent
database comparison after the pair has finished:

```bash
python3 test/native/compare_ifp_databases.py \
  --pair-root /path/to/new-ifp-experiment/gcd-ics55/run
python3 -m unittest discover -s test/native -p test_compare_ifp_databases.py
```

This reads the raw `input-model.json` captured after `initFP()` and the
`computed-model.json` captured after `runFP()` but before `destroyFP()` writes
back. When present, `controlled-input-model.json` is reported as a separate
boundary. Snapshot schema 2 includes all current iFP Database fields,
including four name-to-index maps, database update flags, and the stored
Site/CellMaster names as well as their map keys. Older snapshots are marked
incomplete rather than treated as a full Database pass.

`fp-database-diff.json` separates exact sequence equality, collection-order
differences, exact-name field differences, and each backend's index-map
consistency. Coordinate tuples retain their order; duplicate records and
escaped names remain distinct. This checks the adapter projection itself,
independently of the existing DEF semantic gate. An order-only result is still
visible because iFP can observe vector order when assigning IO slots or naming
new instances. A working Database does not contain full technology via rules
or original DEF PORT groups; matching this boundary alone cannot certify
those external inputs or the final physical writeback.

The preparation script requires the existing real ICS55 GCD, Nangate45 GCD,
TinyRocket, and RV64 Rocket input DEFs at its documented workspace paths. It
never synthesizes dummy instances. Nangate45 iFP settings are versioned under
`test/fixtures/gcd/config/floorplan_nangate45.json`; generated configurations,
input hashes, worker logs, and binary checkpoints stay in the experiment tree.

`execution_passed` means both algorithm runs and writebacks completed. It does
not mean `differential_passed` or DEF semantic comparison passed. The work
model JSON is a diagnostic projection, not a DEF text comparison or a physical
equivalence oracle. `eccdb_def_semantic_probe` imports both DEFs through the
same parser and reuses the canonical design/net comparators under
`src/database/eccdb/tests/differential`. Its iFP gate normalizes rectangular die
encodings and generated via aliases by their expanded geometry; the strict
record-level result remains available as `represented_semantics_equal`.
The independent OpenDB snapshot checks
absolute IO rectangles, placement/halo, PG segments, expanded cut/metal via
geometry, and tracks as multisets. Generated via names and SWire grouping are
absent from the physical snapshot; net identity and duplicate geometry remain.
PORT ordinal is diagnostic only; its semantic comparison retains complete PORT
groups and detects duplicate/split/merged ports.

Run the small canonicalization/mutation tests with
`eccdb_ifp_semantic_differential_test` (build that CMake target first). To compare saved real outputs without
running iFP again, set `ECCDB_IFP_MANIFEST` to the case manifest and
`ECCDB_IFP_PAIR_ROOT` to the directory containing both backend outputs, then
run that test binary. Without these environment variables the saved-pair test
skips; once configured, missing inputs fail. Neither case is a claimed pass.
The launcher also supports `--compare-only` with a semantic probe, and OpenDB
comparison supports `--compare-only` after snapshots have been captured.
Compare-only validates the original manifest, source DEF/LEF/config hashes and
saved worker-artifact hashes before reusing the experiment. Older experiments
may use the original preparation catalog as the source-hash record. Missing
provenance is rejected. The semantic probe runs again in a fresh report directory;
it cannot consume a stale successful report. Without a valid formal comparison
the runner returns exit 2; a canonical mismatch returns exit 1 even if both
workers completed. JSON projection equality is reported as
`work_model_diagnostics_equal` and does not decide correctness.

Keep input-projection differences separate from algorithm and writeback
differences. Do not remove escaping or clear real PORT/geometry differences to
force parity. Snapshot dumping adds substantial time and temporary RSS on
larger cases, so these correctness runs are not a standalone speed benchmark.

## Paired iMJ filler / metal / antenna

Build `imj_pair_probe` explicitly (excluded from production builds), then run:

```bash
python3 test/native/run_imj_pair_differential.py \
  --probe build/upstream-integrate-20261013/bin/imj_pair_probe \
  --manifest /path/to/manifest.json --stage filler \
  --options /path/to/flat-options.json --out /path/to/new-pair --case tiny
```

Use `--def-file` to select a routed checkpoint for metal/antenna. Both providers
import identical LEFs/DEF in separate processes. Filler comparison retains raw
names and complete physical keys; metal compares rectangle/layer/mask/OPC
multisets. Before/after snapshots check unintended mutations independently of
cross-backend equality. Antenna requires a checked structured result and complete
coverage; zero violations alone do not pass. Process timings and peak RSS are
diagnostic, not a speed benchmark. Exit codes are 0 for acceptance, 1 for a
covered mismatch or missing expected insertion, and 2 for failed execution or
incomplete coverage. Original
input/binary provenance is retained when re-comparing saved outputs.

Filler and metal cases are positive insertion controls: each provider must
actually add at least one expected component/fill. Equal empty output is still
reported as comparable, but does not pass functional acceptance. A legitimate
production no-op therefore needs a separate contract; it cannot validate these
insertion controls. Antenna may legitimately pass with zero violations when
coverage is complete.

The runner refuses non-empty output roots for a fresh run. Use a new `--out`
path, or `--compare-only` to re-read an existing pair with matching inputs;
failed invocation records cannot reuse an older successful result. Each native
worker uses its own `native-work` directory. Antenna reports retain the full raw
violation vector and expose coverage and numerical equality separately from
backend-specific diagnostics; incomplete coverage never passes acceptance.

Focused native checks are available without enabling every operation test.
The adapter regression target requires `ECCDB_BUILD_TESTS=ON` and GTest:

```bash
cmake --build build/upstream-integrate-20261013 \
  --target imj_metal_kernel_test imj_antenna_kernel_test imj_native_adapter_test -j 8
build/upstream-integrate-20261013/bin/imj_metal_kernel_test
build/upstream-integrate-20261013/bin/imj_antenna_kernel_test
build/upstream-integrate-20261013/bin/imj_native_adapter_test
python3 -m unittest test/native/test_run_imj_pair_differential.py
```

Four valid file-level antenna controls (pass, long-wire, CLOCK and RECT-patch)
live in [`fixtures/imj-antenna`](fixtures/imj-antenna/README.md). They exercise the
two production importers as well as ratio computation. The separate
`unsupported-poly` fixture is a negative parser test: SI2 rejects antenna-rule
statements on a MASTERSLICE layer. Typed Service/binary masterslice compatibility
is checked by the C++ adapter regression instead. Missing PDK annotations,
unsupported geometry or unselected oxide models remain explicit incomplete
coverage; a zero violation count never replaces that coverage gate.
