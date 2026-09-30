# ECCDB Benchmark

[简体中文：完整性能测试指南](../doc/Benchmark.zh-CN.md)

The main executables measure database access (`eccdb_benchmark`), iRT input
materialization (`irt_input_benchmark`), and EccDB binary persistence
(`eccdb_binary_archive_benchmark`). These are performance workloads, not a
replacement for semantic differential tests or an end-to-end routing benchmark.

## ISPD18 test6 OpenDB comparison

`IspdImportComparison.py` compares LEF/DEF import on the single 107,919-instance
`ispd18_test6` case. It makes one temporary repair to the malformed Metal9
PRL row and feeds the same repaired LEF to both backends. OpenDB is timed
inside native `odb.read_lef` / `odb.read_def` calls through its Python binding;
EccDB is timed inside its C++ importers. Process startup and count checks are
outside both timed regions. EccDB currently parses LEF twice (tech, library),
while OpenDB parses it once, so compare DEF times separately from total times.

```bash
cmake --build build/eccdb-differential --target eccdb_ispd18_import_benchmark
python3 src/database/eccdb/benchmarks/IspdImportComparison.py \
  --eccdb build/eccdb-differential/benchmarks/eccdb_ispd18_import_benchmark \
  --differential-test build/eccdb-differential/tests/differential/eccdb_opendb_design_differential_test \
  --output build/eccdb-differential/ispd18_test6_import_comparison.json
```

The runner requires the OpenDB `odb.py` and `_odb.so` build artifacts; use
`--opendb-pythonpath` if they are not under the default OpenROAD checkout.
It runs one untimed warmup pair and five measured fresh imports per backend
in alternating order, checks comparable object counts, and records the OpenDB
semantic differential result. Raw TRACKS counts differ
because OpenDB merges X/Y patterns into one grid per layer; the runner checks
the normalized axis-pattern count instead. The input DEF has no routed wires,
so this benchmark does not verify wire import or routing performance.

## Parser-excluded OpenDB/EccDB logical-netlist comparison

`DbOnlyFixture.py` reads ISPD18 test6 once through OpenDB and writes a common,
deterministic JSON fixture. The native C++ benchmarks then construct fresh
databases from that fixture and time instance creation (including automatic
instance pins), net creation, pin connection, name lookup, and traversal.
Fixture loading, LEF/DEF parsing, library/master creation, validation, and
result writing are outside the timers. Both backends validate the complete
logical instance/master/pin/net connectivity against the fixture.

The timed EccDB connection phase uses the production `connect` API after the
fixture's IDs have been resolved and validated during setup, matching OpenDB's
cached `dbITerm*`/`dbNet*` connection path. EccDB `connect` is the former
`connectDirect` implementation: callers must provide live IDs from the same
storage and an unconnected relation for the target net kind. It does not check
duplicates, conflicting connections, or MUSTJOIN constraints. The DEF importer
handles its repeated declarations and reconnections before calling it. This
measures valid initial connections, not equivalent error or reconnect behavior.

This is a **logical-netlist replay**, not the database-only part of either
production LEF/DEF importer. It excludes BTerms, placement, special nets,
routing geometry, and the importers' own object-construction paths. The test6
fixture contains 107,919 instances, 107,701 regular nets, 690,219 instance
pins, and 474,330 connected instance pins. Fixture generation applies the
known Metal9 PRL repair to a temporary LEF copy; the source data is unchanged.

```bash
python3 src/database/eccdb/benchmarks/DbOnlyFixture.py \
  --output build/eccdb-differential/ispd18_test6_logical.json
cmake --build build/eccdb-differential --target eccdb_db_only_benchmark -j 32
python3 src/database/eccdb/benchmarks/BuildDbOnlyOdb.py \
  --openroad-root /home/zhuxu1/workspace/OpenROAD \
  --bazel bazelisk --jobs 32 \
  --output-base /tmp/eccdb-odb-bazel-base --offline \
  --output build/eccdb-differential/benchmarks/odb_db_only_benchmark
python3 src/database/eccdb/benchmarks/DbOnlyComparison.py \
  --fixture build/eccdb-differential/ispd18_test6_logical.json \
  --eccdb build/eccdb-differential/benchmarks/eccdb_db_only_benchmark \
  --odb build/eccdb-differential/benchmarks/odb_db_only_benchmark \
  --repeat 11 \
  --output build/eccdb-differential/ispd18_test6_db_only_comparison.json
```

The OpenDB build helper uses a temporary Bazel package overlay; it does not
modify the OpenROAD checkout. `DbOnlyComparison.py` runs an untimed warmup pair,
then alternates fresh ODB/EccDB processes and reports per-phase median times
and EccDB/ODB ratios. The shown `--output-base` is the cache populated on this
machine; omit `--output-base` and `--offline` for an initial dependency fetch
elsewhere. The helper always disables Bazel's remote cache. Use a consistent
CPU affinity and toolchain when making performance claims; a small build-time
difference is sensitive to host load.

## IDB/EccDB DB-only and native-wire comparison

`IdbComparison.py` has two separate modes. `db-only` replays one deterministic
logical-netlist fixture into fresh IDB and EccDB databases. It measures instance
and net creation, pre-resolved instance-pin connection, name lookup, and
traversal. Fixture JSON parsing, library/master construction, fixture
validation, and output are outside the timers. The master and connection-pin
inputs are resolved before the timed phases. Timed lookup/traversal results are
folded into backend-local checksums so the compiler cannot eliminate them.
Counts must match between backends; native checksums are deliberately not
compared because they can encode backend-specific handles.
Pin resolution and untimed fixture checks touch objects before subsequent
phases, so connect/find/traverse results are not cold-cache measurements.

`wire` imports the same LEF/DEF natively into each backend, then measures only
regular- and special-wire scans. LEF/DEF import and shared-field validation are
outside the wire-read timers. Before the three formal alternating repetitions,
the runner emits one canonical JSONL snapshot per backend and requires the two
files to be byte-identical. Raw native JSON, stdout/stderr, snapshots, input
SHA-256 values, and executable SHA-256 values are retained with `--keep-raw`.

Use a fresh top-level build directory with `-DNDEBUG`: this root project builds
its Release flags from `CXXFLAGS`, so a plain Release configuration does not by
itself guarantee that assertions are disabled in both backends.

```bash
repo=/home/zhuxu1/workspace/ecc-tools
build="$repo/build/adapter-differential-release-ndebug"
export CXXFLAGS=-DNDEBUG
cmake -S "$repo" -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DECCDB_BUILD_BENCHMARKS=ON \
  -DECCDB_BUILD_TESTS=OFF
cmake --build "$build" --parallel "$(nproc)" --target \
  eccdb_db_only_idb_benchmark \
  eccdb_db_only_benchmark \
  eccdb_idb_comparison_benchmark
bin="$repo/bin"
reference=/home/zhuxu1/workspace/reference
test6="$reference/ispd2018/ispd18_test6"
mkdir -p "$build/idb-comparison"
```

The IDB targets require the complete top-level build, where `IdbBuilder` is
available; the standalone `build/eccdb-differential` configuration does not
provide it.

### DB-only fixture replay

```bash
fixture="$build/idb-comparison/ispd18-test6-logical.json"
python3 "$repo/src/database/eccdb/benchmarks/DbOnlyFixture.py" \
  --lef "$test6/ispd18_test6.input.lef" \
  --def "$test6/ispd18_test6.input.def" \
  --output "$fixture"
taskset -c 24 python3 "$repo/src/database/eccdb/benchmarks/IdbComparison.py" \
  --mode db-only --fixture "$fixture" \
  --idb "$bin/eccdb_db_only_idb_benchmark" \
  --eccdb "$bin/eccdb_db_only_benchmark" \
  --repeat 3 --keep-raw \
  --output "$build/idb-comparison/ispd18-test6-db-only.json"
```

### Native wire scans

The routed ISPD18 test6 input has no `SPECIALNETS`; its special result is
therefore reported as `no_workload`, not as a performance ratio. For the
regular-wire case, supply the routed DEF produced by the routing flow:

```bash
routed_def=/path/to/ispd18-test6.routed.def
taskset -c 24 python3 "$repo/src/database/eccdb/benchmarks/IdbComparison.py" \
  --mode wire \
  --lef "$test6/ispd18_test6.input.lef" \
  --def "$routed_def" \
  --wire-benchmark "$bin/eccdb_idb_comparison_benchmark" \
  --repeat 3 --keep-raw \
  --output "$build/idb-comparison/ispd18-test6-regular-wire.json"
```

The shared regular-wire snapshot contains all 2,957,852 comparable paths for
the routed test6 fixture. This is not complete DEF equivalence: the canonical
subset accepts two-point Manhattan lines and single-point VIA/RECT paths, and
normalizes two-point direction because IDB sorts regular points. It also omits
regular-wire attributes that IDB drops, including extensions and masks.
Special-path width, SHAPE, and STYLE are included in the untimed comparison.
EccDB enumerates nets with the non-allocating EnTT-view APIs, rather than
constructing a temporary Net-ID vector inside the wire timer. The timer reads
point coordinates, via origins, and rectangle coordinates, not Service-level
RouteShape projection or the complete TritonRoute import.

Use the prepared real-ISPD19 test6 special-only input for special routing. It
retains the DEF header, `VIAS`, and `SPECIALNETS` needed for this isolated
workload and contains 8,378 paths:

```bash
special_def="$repo/build/eccdb-differential/idb-comparison/ispd19_test6.special-only.def"
taskset -c 24 python3 "$repo/src/database/eccdb/benchmarks/IdbComparison.py" \
  --mode wire \
  --lef "$reference/ispd2019/ispd19_test6/ispd19_test6.input.lef" \
  --def "$special_def" \
  --wire-benchmark "$bin/eccdb_idb_comparison_benchmark" \
  --repeat 3 --keep-raw \
  --output "$build/idb-comparison/ispd19-test6-special-wire.json"
```

## TritonRoute-facing ODB/EccDB API comparison

For a recorded workload originating from a real ODB-backed TritonRoute run,
see [TritonRoute trace replay](../doc/TritonRoute_Trace_Replay.md). Unlike the
independent API workloads below, that experiment captures invocation order,
query arguments, and actual wire, guide, track-assignment, and pin-access
writes at their router call sites and replays them on ODB, EccDB, and IDB.
It explicitly covers a subset of database API groups; it is not an automatic
trace of every ODB method or the router's internal `fr*` operations.

`DrtServiceComparison.py` times eight operations used by the current
TritonRoute ODB/EccDB wrappers: regular-Net enumeration, Instance/Net/Pin name
lookup, Net-Pin traversal, guide traversal and replacement, and routed-wire
reading. It uses ISPD18 test6 (107,919 instances) and the routed ODB/EccDB DEF
artifacts from `EccdbRouteFlowTest`. The ODB runner exports all pre-route
guides, including empty-Net entries, to JSONL; the EccDB runner attaches that
same guide payload before timing. A deterministic logical fixture fixes the
query names and shuffle order. Each runner validates the complete logical
fixture, guide values, and route counts outside timed sections; the EccDB
runner also checks every projected route primitive against its imported wire.

```bash
cmake -S src/database/eccdb -B build/eccdb-differential \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON
cmake --build build/eccdb-differential --target eccdb_drt_service_benchmark -j 32
python3 src/database/eccdb/benchmarks/BuildDbOnlyOdb.py \
  --benchmark drt-service \
  --openroad-root /home/zhuxu1/workspace/OpenROAD \
  --bazel bazelisk --jobs 32 \
  --output-base /tmp/eccdb-odb-bazel-base --offline \
  --output build/eccdb-differential/benchmarks/odb_drt_service_benchmark
taskset -c 24 python3 src/database/eccdb/benchmarks/DrtServiceComparison.py \
  --fixture build/eccdb-differential/ispd18_test6_logical.json \
  --lef /home/zhuxu1/workspace/reference/ispd2018/ispd18_test6/ispd18_test6.input.lef \
  --routed-def /tmp/eccdb-route-parity-test6.u6o5OJ/eccdb-route-flow-adapter.eccdb.def \
  --preroute-odb /tmp/eccdb-route-parity-test6.u6o5OJ/ispd18_test6.preroute.odb \
  --routed-odb /tmp/eccdb-route-parity-test6.u6o5OJ/eccdb-route-flow-adapter.routed.odb \
  --eccdb build/eccdb-differential/benchmarks/eccdb_drt_service_benchmark \
  --odb build/eccdb-differential/benchmarks/odb_drt_service_benchmark \
  --repeat 7 \
  --output build/eccdb-differential/ispd18_test6_drt_service_comparison_lto_7.json
```

The comparison excludes LEF/DEF parsing, ODB snapshot loading, JSON fixture
parsing, guide attachment, validation, and output writing from each timed
phase. Each measurement uses a fresh process; a warmup pair precedes alternating
ODB/EccDB runs. The comparison disables EccDB's benchmark-only pin-cache
experiments so they cannot warm the subsequent guide and route phases.
Pass `--include-experiments` to measure the separate shadow instance-pin
name lookup and shadow net-pin traversal; these are not production Service
API timings, and their cache construction is outside the timed phases.
Append `--pins-only` to rerun only the native `find_pins_ns` and
`scan_net_pins_ns` phases on both backends. Its JSON `measured_fields` lists
only those two values; it cannot be combined with `--include-experiments`.
`eccdb_over_odb` is below 1 when EccDB is faster. These are
native database API timings, **not** two interchangeable full TritonRoute
backends: the current EccDB adapter runs after the ODB parser and alongside
the ODB writer. Test6 has no pre-existing pin-access points or track assignments,
so those APIs are not benchmarked. Route replacement is also not benchmarked:
an equivalent neutral route-shape fixture is needed before comparing ODB
`dbWireEncoder` with EccDB `NetRouteService::replaceRouted`. Both local builds
use `-O3` and LTO, but ODB's Bazel toolchain uses LLVM while EccDB's CMake
build uses GCC, so treat ratios as measured implementation results rather
than a compiler-controlled microarchitecture comparison.

## Path/Record layout study

`eccdb_path_record_benchmark` and the Boost.Container backed
`eccdb_path_record_inline_benchmark` are
experimental layout comparisons. They import a routed DEF through the current
storage, then materialize the same route geometry as `Wire -> Path -> Record`.
The second target stores up to two records inline in each Path. Both verify
geometry counts and a field fingerprint before reporting capacity, allocation
buffer counts, traversal time, and Path deletion time. Neither target changes
the production routing layout. The inline target is built when Boost.Container
headers are available.

```bash
cmake --build build/eccdb-differential --target \
  eccdb_path_record_benchmark eccdb_path_record_inline_benchmark
build/eccdb-differential/benchmarks/eccdb_path_record_inline_benchmark \
  /path/to/design.lef /path/to/routed.def 100 regular-only
```

The optional stride samples one eligible Wire in every 100; `regular-only`
excludes special nets. Omit both arguments to inspect every Wire. Capacity
figures include routing vector headers and reserved elements but exclude EnTT
registry overhead. The edit comparison reports both rebuilding through the
current `appendPath` operation and an experimental in-place flat-vector erase.
The candidate erases and restores one Path in its Path vector. The
prototype reconstructs records from the current point/VIA/rectangle arrays,
so it cannot recover their original interleaving in the source DEF.

For the locally available ISPD19 test10 routed DEF, sampling every 100th
regular Wire yielded 8,951 Wires and 467,934 Paths. Every sampled Path had at
most two point/VIA/rectangle primitives. One Release run gave:

| Layout | Routing capacity | Nonempty vector buffers | Route walk |
| --- | ---: | ---: | ---: |
| Current flat Wire vectors | 38.9 MB | 33,459 | 11.9 ms |
| Path with `std::vector<Record>` | 42.1 MB | 476,885 | 27.0 ms |
| Path with two inline Records | 43.5 MB | 53,358 | 10.9 ms |

On a 1,141-Path Wire, erasing and restoring the first Path took 0.0032 ms
with direct range adjustment in the current flat vectors and 0.0119 ms with
the inline-record Path vector. These are single-run measurements, not a
production mutation API or a full-database RSS comparison. The results do not
support replacing the current routing layout solely to make Path deletion
easier; a range-adjusting edit operation can work with the current layout.

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
