# ECCDB Benchmark

[简体中文：完整性能测试指南](../doc/Benchmark.zh-CN.md)

Three executables measure database access (`eccdb_benchmark`), iRT input
materialization (`irt_input_benchmark`), and EccDB binary persistence
(`eccdb_binary_archive_benchmark`). These are performance workloads, not a
replacement for semantic differential tests or an end-to-end routing benchmark.

The unified runner configures dependencies and runs all three workloads:

```bash
src/database/eccdb/doc/run_tests.sh benchmark --case ispd19_test8 --repeat 3
```

It runs iDB and EccDB in separate, serial processes and writes raw JSONL and
median/min/max CSV results to a unique directory. See the
[runner guide](../doc/Test_Runner.zh-CN.md) for configuration.

## Build

The benchmark needs the regular LEF/DEF importer and exporter targets, so use a
normal (non-`BUILD_IDB_CORE_ONLY`) build:

```bash
cmake -S . -B build/eccdb-benchmark \
  -DCMAKE_BUILD_TYPE=Release \
  -DECCDB_BUILD_BENCHMARKS=ON -DECCDB_BUILD_TESTS=OFF
cmake --build build/eccdb-benchmark --parallel 2 --target \
  eccdb_benchmark irt_input_benchmark eccdb_binary_archive_benchmark
```

The resulting executable is `bin/eccdb_benchmark`.

The target list above also builds
`bin/eccdb_binary_archive_benchmark`. It measures the current EnTTDB
text-import and binary-persistence path without running the query workloads.

## Run

Each invocation appends one JSON object per measurement to `--output`:

```bash
./bin/eccdb_benchmark \
  --lef /path/to/workspace/reference/ispd2019/ispd19_test1/ispd19_test1.input.lef \
  --def /path/to/workspace/reference/ispd2019/ispd19_test1/ispd19_test1.input.def \
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

The schemas differ between executables. Timed records report elapsed
nanoseconds and memory in KiB; workload-specific fields include input/output
bytes, record/edge/shape counts and throughput. `restored_counts` is not a timed
record. The general benchmark's `ok` is an execution-status flag, not a semantic
correctness check; also check process exit status and non-empty writer outputs.

RSS after iRT wrapping includes both the source database and iRT data. Peak RSS
includes earlier operations in the same process. Binary reload immediately
following export is a filesystem-cache workload. Matching operation names do
not imply matching work: compare counters before computing speedup ratios.

The runner CSV contains `case,source,operation,samples,median_seconds,
min_seconds,max_seconds,median_rss_mib`; its RSS median uses end-of-operation
RSS, not peak RSS. The runner uses input DEF files and `--writes 256`.
Use the executables directly for custom routed DEF or different write counts.

## ISPD2019 data

Download external corpora using the [source guide](../doc/Data_Sources.zh-CN.md).
The `ispd19_test1` through `ispd19_test10` input LEF/DEF files are suitable for
scaling runs. The downloaded `sample*` cases additionally
provide `solution.good.def` and `solution.bad.def`; these are useful routed
DEF inputs for route-geometry workloads. The test cases generally do not ship
solution DEF files, so pass any separately obtained routed DEF explicitly via
`--def` rather than silently substituting an input DEF.

For example, a downloaded routed sample can be measured with:

```bash
./bin/eccdb_benchmark \
  --lef /path/to/workspace/reference/ispd2019/ispd19_sample/ispd19_sample.input.lef \
  --def /path/to/workspace/reference/ispd2019/ispd19_sample/ispd19_sample.solution.good.def \
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
  --lef /path/to/workspace/reference/ispd2019/ispd19_test10/ispd19_test10.input.lef \
  --def /path/to/workspace/reference/ispd2019/ispd19_test10/ispd19_test10.input.def \
  --archive-dir src/database/eccdb/benchmarks/results/ispd19-test10-input-binary \
  --output src/database/eccdb/benchmarks/results/ispd19-test10-input-binary.jsonl
```

The archive directory is retained so its size can be inspected after the run.
Both the archive directory and JSONL output are under the benchmark `results/`
directory, which is ignored by Git. Restored entity and routing counts are
checked against the text-imported database before a successful result is
written.


Unlike the two other executables, the binary benchmark truncates its `--output`
JSONL file. Use a unique path per invocation. To load existing archives and
export DEF in a fresh process:

```bash
./bin/eccdb_binary_archive_benchmark \
  --source-archive-dir /path/to/archives \
  --def-export /path/to/results/restored.def \
  --output /path/to/results/binary-to-def.jsonl
```

Choose exactly one input mode (`--lef` plus `--def`, or
`--source-archive-dir`) and one output mode (`--archive-dir` or `--def-export`).
There is no standalone iDRC benchmark target in this group.
