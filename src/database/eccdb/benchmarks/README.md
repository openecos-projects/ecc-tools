# ECCDB Benchmark

This benchmark measures the runtime and memory cost of the data that a future
iRT-facing EnTTDB API is expected to expose. It is intentionally independent
of the iRT implementation and does not measure iDB-to-EnTT conversion,
correctness, checksums, or semantic diffs.

## Build

The benchmark needs the regular LEF/DEF importer and exporter targets, so use a
normal (non-`BUILD_IDB_CORE_ONLY`) build:

```bash
cmake -S . -B build/eccdb-benchmark \
  -DECCDB_BUILD_BENCHMARKS=ON \
  -DECCDB_BUILD_TESTS=OFF \
  -DBUILD_GUI=OFF -DBUILD_PYTHON=OFF
cmake --build build/eccdb-benchmark --target eccdb_benchmark -j2
```

The resulting executable is `bin/eccdb_benchmark`.

The same configuration also builds
`bin/eccdb_binary_archive_benchmark`. It measures the current EnTTDB
text-import and binary-persistence path without running the query workloads.

## Run

Each invocation appends one JSON object per measurement to `--output`:

```bash
./bin/eccdb_benchmark \
  --lef reference/ispd2019/ispd19_test1/ispd19_test1.input.lef \
  --def reference/ispd2019/ispd19_test1/ispd19_test1.input.def \
  --source entt --writes 256 \
  --output results/ispd19-test1-entt.jsonl
```

Use `--source idb` for the legacy implementation, or run separate processes
for `entt` and `idb` when comparing memory. `--source both` is useful for a
single smoke run but has process-lifetime memory effects and is not a fair RSS
comparison. `--writes N` measures appending `N` two-point regular-net routing
paths; use `--writes 0` to omit that mutation when measuring only loaded-data
access and DEF writing.

The benchmark emits these operations for EnTT:

- `lef_read`: LEF technology and library import.
- `def_read`: DEF design import.
- `net_pin_forward`: regular net to instance/IO pin traversal.
- `pin_net_reverse`: instance/IO pin to net traversal.
- `placed_geometry`: placed instance master OBS and pin/port geometry traversal.
- `regular_route_geometry`: routed regular-net wire/path point, via, rectangle traversal.
- `special_route_geometry`: special-net wire/path point, via, rectangle traversal.
- `tech_floorplan_access`: layers, vias, rows, track grids, and GCell grids.
- `lef_tech_write`, `lef_library_write`, `def_write`: native EnTT exporters.
- `routing_batch_append`: direct routing result append workload.

The relationship workloads intentionally use the batch-oriented read API:

- Instance-to-Pin and Net-to-Pin obtain read-only spans, so plain `auto` does
  not accidentally copy a referenced vector and Net traversal does not copy
  one or two vectors for every Net.
- Pin-to-Net iterates the packed Instance-Pin and IO-Pin component storages
  directly, rather than performing a validated point lookup for every Pin.

These APIs do not add a derived index or change the underlying connectivity
components. A returned Net-to-Pin span is invalidated when that Net's
connectivity is mutated.

Legacy iDB emits the same access operations and `def_write`. Its
`saveLef` operation is deliberately excluded: iDB uses that API to export a
design as macro LEF, not to write the input technology/library LEF, so it is
not comparable with `lef_tech_write` or `lef_library_write`.

## iRT input materialization

`irt_input_benchmark` measures the main database-facing iRT workload without
running routing algorithms: materializing the loaded source database into
iRT's `Database`. Run EnTTDB and iDB in separate processes so their retained
RSS values do not contaminate each other:

```bash
./bin/irt_input_benchmark --lef design.lef --def design.def \
  --source entt --output results/irt-input-entt.jsonl
./bin/irt_input_benchmark --lef design.lef --def design.def \
  --source idb --output results/irt-input-idb.jsonl
```

The `irt_database_wrap` record reports elapsed time, source-database RSS before
the wrap, incremental and total RSS after the wrap, and the materialized net,
pin, pin-shape, obstacle, layer, and via-master counts. LEF/DEF parsing is
deliberately outside the timed interval. `materialized_shapes` is the sum of
pin routing/cut shapes and routing/cut obstacles; `throughput_shapes_per_s`
normalizes the wrap time by that workload.

## Metrics

Every JSONL record contains elapsed nanoseconds, input/output bytes, record,
edge, and shape counts, derived throughput, RSS delta/after, peak RSS, and
allocator bytes. `ok` reports whether a writer produced a non-empty output
file; it is an execution-status flag, not a correctness benchmark. In
particular, a failed writer must not be compared as if its partial output were
valid.

## ISPD2019 data

`reference/ispd2019/ispd19_test1` through `ispd19_test10` provide input LEF/DEF
files suitable for scaling runs. The checked-in `sample*` cases additionally
provide `solution.good.def` and `solution.bad.def`; these are useful routed
DEF inputs for route-geometry workloads. The test cases generally do not ship
solution DEF files, so pass any separately obtained routed DEF explicitly via
`--def` rather than silently substituting an input DEF.

For example, the checked-in routed sample can be measured with:

```bash
./bin/eccdb_benchmark \
  --lef reference/ispd2019/ispd19_sample/ispd19_sample.input.lef \
  --def reference/ispd2019/ispd19_sample/ispd19_sample.solution.good.def \
  --source entt --writes 0 --output results/ispd19-sample-routed.jsonl
```

## Binary archive benchmark

The binary benchmark imports the LEF and DEF, writes separate Tech, Library,
and Design archives, releases the text-imported databases, and restores all
three archives. The reported binary times include the archive header and
payload serialization performed by the production archive APIs.

```bash
cmake --build build/eccdb-benchmark \
  --target eccdb_binary_archive_benchmark -j2

./bin/eccdb_binary_archive_benchmark \
  --lef reference/ispd2019/ispd19_test10/ispd19_test10.input.lef \
  --def reference/ispd2019/ispd19_test10/ispd19_test10.input.def \
  --archive-dir src/database/eccdb/benchmarks/results/ispd19-test10-input-binary \
  --output src/database/eccdb/benchmarks/results/ispd19-test10-input-binary.jsonl
```

The archive directory is retained so its size can be inspected after the run.
Both the archive directory and JSONL output are under the benchmark `results/`
directory, which is ignored by Git. Restored entity and routing counts are
checked against the text-imported database before a successful result is
written.
