# IDB / EccDB API Comparison, 2026-09-27

## Scope

This compares native database operations on the current
`experiment/eccdb-embedded-instance-pins` worktree, based on
`5a020f4318656e77cd9476b6b8344567e2655028`, with existing uncommitted changes.
EccDB instance pins are embedded values in this branch. This experiment adds
benchmark code and validation; it does not modify production storage or services.

All times below are **whole-workload milliseconds**, not per-call latency.
Each backend runs in a fresh process, alternating order, three measured runs,
median reported, CPU affinity 24. LEF/DEF/JSON parsing, library setup, query
preparation, validation, and result writing are excluded from the API timers.

Both backends use GCC 13.3.0, `-O3 -DNDEBUG`; the benchmark executables use
Release IPO/LTO. Dependencies use their existing CMake library settings.
Machine: AMD EPYC 9654, 384 logical CPUs. EnTT version: 4.0.0.
The root project's default Release flags omit `-DNDEBUG`, so this was supplied
through `CXXFLAGS` during configuration and building. Initial assertion-enabled
measurements are kept separately in `debug-asserts/` and are not used below.

This is not the earlier ODB/EccDB TritonRoute-facing Service table, a complete
DRT import, or a run of two routing backends. Wire tests read native payloads,
not projected `RouteShape` objects. No ODB performance was remeasured here.

## Results

The DB-only test replays the existing ISPD18 test6 logical fixture: 117 masters,
107,919 instances, 107,701 regular nets, 690,219 instance pins, and 474,330
instance-pin connections. Minimal masters have names and terms but no LEF
geometry. IO pins, placement, special nets, and routing are excluded from this
logical construction test.

| Operation | Work items | IDB ms | EccDB ms | EccDB / IDB |
| --- | ---: | ---: | ---: | ---: |
| Create instances, including automatic pins | 107,919 | 209.219 | 31.655 | 0.151 |
| Create regular nets | 107,701 | 50.972 | 15.250 | 0.299 |
| Connect pre-resolved pins | 474,330 | 242.001 | 36.697 | 0.152 |
| Find instance by name, successful queries | 107,919 | 23.601 | 6.905 | 0.293 |
| Find net by name, successful queries | 107,701 | 15.545 | 6.878 | 0.442 |
| Find instance pin by name, successful queries | 474,330 | 182.722 | 60.722 | 0.332 |
| Enumerate instances | 107,919 | 0.149 | 0.223 | 1.495 |
| Enumerate regular nets | 107,701 | 0.161 | 0.148 | 0.914 |
| Enumerate net-to-instance-pin handles | 474,330 | 4.887 | 4.288 | 0.877 |
| Read routed regular-wire payloads | 2,957,852 paths | 130.919 | 77.363 | 0.591 |
| Read special-wire payloads, separate subset | 8,378 paths | 0.0631 | 0.1075 | 1.703 |

The median of each run's create-instance + create-net + connect time is
502.920 ms for IDB and 83.492 ms for EccDB, about 6.02x faster for this logical
construction workload. This excludes library creation and all physical objects.
It is the median of per-run sums, not the sum of the three row medians.

After loading the complete routed test6 LEF/DEF and `malloc_trim`, median RSS
before validation/snapshot allocation is 1,620,780 KiB (1,582.8 MiB) for IDB and
441,816 KiB (431.5 MiB) for EccDB. These are whole-process loaded-design values,
not DB-only memory or bytes per object. Different retained pin geometry and
metadata are included, so this is not proof of identical object payload size.

## API Boundaries

- Instance creation receives a pre-resolved master ID/pointer on both sides;
  both create their instance pins automatically.
- Connection inputs are resolved before timing. IDB uses `connectPinToNet`,
  which also maintains unique pin lists, net-to-instance references, and a
  pin's net-name string. EccDB uses its production unchecked initial-connect
  API. Results include these native differences; they do not isolate EnTT,
  allocator, or hash-table performance alone.
- Instance/Net queries use the same shuffled names. Pin queries use the same
  instance and term name: EccDB's master-term name index and pin ordinal vs
  IDB's linear `get_pin` scan. All queries are hits; misses are not measured.
- Plain traversal consumes handles, not every associated Instance/MasterTerm
  name or pin-geometry field. Pre-resolved pins and untimed logical validation
  touch objects before subsequent phases; this is not a cold-cache benchmark.
- Native checksums prevent dead-code elimination. They are backend-specific
  and are not compared with each other as semantic results.
- Wire timers read point coordinates, via origins, and rectangle coordinates.
  EccDB enumerates nets through non-allocating EnTT views. Neither timer
  constructs normalized tuples or Service snapshots.

## Correctness

Both logical constructors validate instance/master/name lookup, term/pin
relationships, forward connectivity, reverse net-pin membership, and counts
against the same fixture outside timed phases. EccDB additionally compares the
full reverse relation as a sorted multiset after all timers; duplicate or
missing reverse entries cannot pass merely by preserving list lengths.

The routed test6 input has 107,589 wires and 2,957,852 paths: 1,122,672
two-point lines, 1,391,014 single-point via paths, and 444,166 single-point
rectangle paths. Total point count is 4,080,524. Every path is inside the
comparator's supported profile. Two-point direction is normalized because
IDB sorts regular-wire point pointers after import.

The untimed canonical tuple includes regular/special category, wire status,
net/layer name, special width/SHAPE/STYLE, points, via names/origins, and
absolute rectangle coordinates. Repeated paths are retained. The warmup
requires byte-identical sorted JSONL snapshots before formal measurements;
all measured runs also verify the shared-field counts/fingerprint.
The regular snapshots are 252,785,398 bytes each and have SHA-256
`8e9d3cef0300a8739a9a5101ff47033b39049848923b0d03c61e8da68ea52a02`.

This is **shared-field equality**, not complete DEF equivalence. IDB drops
regular width, flush extension, masks, taper/style attributes, and some via
attributes. Those are not compared. Bent/multi-via/bare-single-point paths and
standalone special RECT/POLYGON/VIA clauses are outside this comparator.

Test6 has no SPECIALNETS. Its special time is marked `no_workload`, without a
ratio. The measured special workload instead extracts the header, DEF VIAS,
and complete SPECIALNETS section from ISPD19 test6, preserving its 2 PG nets
and 8,378 paths (1,538 lines, 6,840 vias). It has no instances or logical pin
connections and is not a second full-design scaling test. Original datasets
are unchanged. The special canonical snapshots are also byte-identical.

## Files And Reproduction

Results and raw individual-run JSON/logs:
`build/eccdb-differential/idb-comparison/{db-only,regular-wire,special-wire}.json`
and the matching `.runs/` directories. Large canonical snapshots stay in these
build directories; their hashes are included in the summary JSON.

Benchmark sources: `DbOnlyIdbBenchmark.cpp`, `DbOnlyEccdbBenchmark.cpp`,
`IdbComparisonBenchmark.cpp`, `DbOnlyBenchCommon.h`, `IdbComparison.py` in
`src/database/eccdb/benchmarks`. Build targets:
`eccdb_db_only_idb_benchmark`, `eccdb_db_only_benchmark`,
`eccdb_idb_comparison_benchmark`.

See the benchmark README's IDB/EccDB section for commands. The measured build
directory was `build/adapter-differential`, configured and built with
`CXXFLAGS=-DNDEBUG`. Fixture:
`build/eccdb-differential/ispd18_test6_logical.json`. Regular routed DEF:
`/tmp/eccdb-route-parity-test6.u6o5OJ/eccdb-route-flow-adapter.routed.def`.
Input paths, input hashes, executable hashes, all raw samples, validation, and
measured order are recorded in the JSON files.

Generate the special-only input without modifying the original DEF:

```bash
awk 'NR <= 7 {print; next} /^VIAS / || /^SPECIALNETS / {section = 1} section {print} /^END VIAS$/ || /^END SPECIALNETS$/ {section = 0} END {print "END DESIGN"}' \
  /home/zhuxu1/workspace/reference/ispd2019/ispd19_test6/ispd19_test6.input.def \
  > build/eccdb-differential/idb-comparison/ispd19_test6.special-only.def
```

This exact extraction is specific to the current ISPD19 file layout, not a
general DEF parser. It keeps the three DEF-defined generated vias needed by
the special paths. The source LEF is the original ISPD19 test6 LEF. The DB-only
logical JSON came from the earlier fixture-generation step, which used a
temporary Metal9 PRL correction; that fixture carries no technology tables.
The new wire imports use the original unmodified LEFs directly.
