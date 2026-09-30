# TritonRoute database trace replay

## What Is Recorded

This experiment records semantic database API groups at their actual call sites
inside the local OpenROAD/TritonRoute fork. It is not an initial-netlist fixture,
a randomly generated workload, or an automatic interception of every ODB getter.

The recorder is enabled after LEF/DEF/guide loading, immediately before the real
router/import entry point, and closed when that entry point completes. LEF/DEF
input parsers, exporters, and post-run snapshots cannot append events; the DRT
Parser, GuideProcessor, and Writer are instrumented. Schema v3 declares every
supported operation in its header. An operation absent from a particular trace
was not exercised by that router run.

| Group | Recorded operations | Actual caller |
| --- | --- | --- |
| Tech and library | `read_tech_header`, `read_tech_layers`, `read_routing_layer_rules`, `read_cut_layer_rules`, `read_tech_vias`, `read_design_vias`, `read_via_generate_rules`, `read_ndrs`, `read_library_masters`, `read_library_terms`, `read_library_mpin_geometry`, `read_library_obstructions` | Parser technology/library import |
| Floorplan and design | `read_die`, `scan_tracks`, `read_blockages`, `read_bterms`, `read_bterm_pin_access`, `read_master_pin_access`, `read_preferred_pin_access`, `read_special_wire`, `read_gcell_grid`, `read_guides` | Parser and GuideProcessor |
| Logical netlist | `scan_instances`, `read_instance`, `read_instance_pins`, `scan_nets`, `read_net_attributes`, `net_pins`, `find_instance`, `find_pin` | Parser and Writer |
| Ordinary routing | `read_wire`, `define_design_via`, `clear_wire`, `replace_wire`, `append_bterm_stack_wire` | Parser and Writer |
| Routing context writes | `append_track_assignment`, `replace_guides`, `replace_master_pin_access`, `replace_instance_pin_access`, `append_bterm_pin_access` | GuideProcessor and Writer |

Operation counts and ordering are taken from the recorded execution. A query
that does not occur has no synthetic event. This workload does not manufacture
create-instance, create-net, or logical pin-connect requests.
Enumeration is represented as one API group; the following per-object groups
retain the recorded object access order. The scan's result comparison is a
multiset, not a requirement to share the same container iteration order.

The recorder writes schema v3. The replayers also accept existing v1/v2
traces. A VIA definition retains its name, default flag, and every fixed
rectangle, including duplicate rectangles. ODB and iDB retain the default
flag; EccDB currently stores the geometry but not that selection metadata.
The fixed-VIA replay does not compare iDB cut row/column metadata. Definition
events may originate from either routed-wire output or pin-access output.

The trace is bounded to the listed projections, not every getter or full ODB
object identity/callback semantics. Advanced LEF58 constraints, polygon
geometry, antenna/mask metadata, DRC markers, GUI, distributed calls, and
unlisted Writer mutations remain outside it. `scan_nets` intentionally filters
to ordinary nets; special wires are separate name-bound reads. A missing group
must not be presented as zero cost for a fully instrumented router.

## Reference And Correctness Rules

The JSONL header binds ODB-ID-derived opaque trace tokens to instance/net names.
Tokens are not pointers, EnTT packed positions, or object IDs assumed identical between
databases. Each replay backend imports the same input first and binds the
tokens to its own IDs/pointers outside timers. It does not cache returned fields.

Each read result is computed again through the backend API and compared against
the recorded expected result. Net-pin endpoints are compared as a sorted
multiset including IO pins and duplicate multiplicity. Every wire mutation is
read back immediately; all recorded wires (including unmodified ones) and net-pin connectivity are
also checked after the complete replay. A missing footer, incorrect event
count, unknown token/operation, or unsupported primitive is an error. A backend
can explicitly declare an operation unsupported; it is counted separately and
excluded from cross-backend comparable timing.

Primitive geometry is normalized as two-point Manhattan segments, named vias,
and absolute patch rectangles. VIA layer means its canonical bottom routing
layer; it is not the source path's carrier layer. Direction reversal exchanges
segment endpoint extensions. Router-created fixed block VIA definitions are
supported in v2/v3; VIARULE-generated definitions, masks, non-R0 block VIAs, NDR,
non-routed status, virtual/junction path constructs, and unsupported wire
attributes are rejected by the supported profile rather than discarded.

ODB and EccDB additionally compare segment endpoint extensions. IDB does not
retain regular-wire FLUSH extensions, so its validation is limited to shared
primitive geometry/connectivity. The trace keeps the extensions and reports
their population; no side table fakes native IDB support. This is not complete
DEF equality, nor byte-identical wire/path grouping across databases.

## Timing

Each measured event invokes the replay adapter's backend APIs. EccDB uses
`PhysicalDesignService` and `NetRouteService`; instance reads follow the actual
native adapter's access through `PhysicalDesignService::design()` and its
netlist storage, because that facade has no instance-record enumeration API.
IDB and ODB use their existing native object APIs. Wire replacement goes through
the native writer/encoder or the corresponding service, not direct container
modification invented by the benchmark.

Timers include API calls and the same returned-field projection. Trace parsing,
input parsing, token binding, canonical sorting, result comparison, and final
state checks and per-event backend capability checks are outside timers.
One clock pair is used per recorded event,
and elapsed times are accumulated by API group. Small read timings include
this measurement overhead and the common result-container
construction. This is not raw getter latency or whole-router runtime.
No backend runs an extra full-net preflight; only recorded reads and the final
state represented by those reads/writes are validated.
The current replayer streams JSONL events and keeps only the final per-net
validation state in memory. Earlier materializing replayers used 2,340,380 KiB
peak RSS for the 572 MB test6 v1 trace; that historical result is not a
measurement of the streaming implementation or a million-instance estimate.

By default, three fresh processes per backend replay exactly the same trace,
with CPU affinity 24. Order rotates ODB/EccDB/IDB, EccDB/IDB/ODB,
IDB/ODB/EccDB. `--repeat 1` runs one fresh process per backend when a single
large-run measurement is sufficient. Mutation results cannot carry over.

## Reproduction

Build the recorder and ODB replayer:

```bash
bash src/database/eccdb/adapters/tritonroute/BuildNativeRoute.sh
cmake --build build/adapter-differential \
  --target eccdb_drt_trace_replay eccdb_drt_trace_test --parallel 64
```

Record the real ODB-backed route on the approximately 100K-instance test6:

```bash
build/openroad-native-eccdb-build/src/drt/eccdbNativeRoute \
  --backend odb \
  --lef /home/zhuxu1/workspace/reference/ispd2018/ispd18_test6/ispd18_test6.input.lef \
  --def /home/zhuxu1/workspace/reference/ispd2018/ispd18_test6/ispd18_test6.input.def \
  --guides /home/zhuxu1/workspace/reference/ispd2018/ispd18_test6/ispd18_test6.input.guide \
  --threads 64 --trace /tmp/ispd18-test6.trace.jsonl \
  --output /tmp/ispd18-test6.trace-route
```

To exercise routing-context writes as well as ordinary wire writes, add
`--save-guide-updates --write-net-tracks` to the native route command. Those
options expose the same GuideProcessor/Writer paths used by TritonRoute; they
are not synthetic replay events. `--write-net-tracks` is ODB-only in this
headless driver. `--max-routing-level N` is likewise ODB-only and follows the
block's signal-layer limit. A separate `--import-only --max-routing-level 2`
Nangate45 trace exercises top-level BTerm VIA-stack insertion before detailed
routing. That trace validates the API behavior, not routed-wire throughput.

Replay and validate three times per backend:

```bash
python3 src/database/eccdb/benchmarks/DrtTraceComparison.py \
  --trace /tmp/ispd18-test6.trace.jsonl \
  --lef /home/zhuxu1/workspace/reference/ispd2018/ispd18_test6/ispd18_test6.input.lef \
  --def /home/zhuxu1/workspace/reference/ispd2018/ispd18_test6/ispd18_test6.input.def \
  --odb build/openroad-native-eccdb-build/src/drt/eccdbDrtTraceOdb \
  --eccdb-idb bin/eccdb_drt_trace_replay \
  --repeat 3 --cpu 24 --output build/eccdb-differential/trace-replay/test6.json
```

For v3 traces with `read_guides` or guide replacements, pass the source
`--guides` file to the comparison command as well.

The result JSON contains exact per-event-category totals, individual runs,
operation counts, input/executable SHA-256 hashes, and explicit validation scope.

To exercise a router-created design VIA without relying on a particular route
to create one, replay the checked-in sample2 fixture. It defines a four-RECT
VIA, including two identical cut rectangles, then writes a wire using it:

```bash
python3 src/database/eccdb/benchmarks/DrtTraceComparison.py \
  --trace src/database/eccdb/benchmarks/DrtTraceRuntimeViaSample2.jsonl \
  --lef /home/zhuxu1/workspace/reference/ispd2018/ispd18_sample2/ispd18_sample2.input.lef \
  --def /home/zhuxu1/workspace/reference/ispd2018/ispd18_sample2/ispd18_sample2.input.def \
  --odb build/openroad-native-eccdb-build/src/drt/eccdbDrtTraceOdb \
  --eccdb-idb bin/eccdb_drt_trace_replay \
  --repeat 3 --cpu 24 \
  --output build/adapter-differential/trace-replay/sample2-runtime-via-fixture.json
```

All nine fixture replays passed: one new VIA, four rectangles, one referenced
wire, and two events per run. These two events are a correctness fixture, not
a useful performance measurement. A real sample2 ODB route emitted a v2 trace
with 251 events; all three backends replayed it three times successfully, but
that route did not create a new block VIA. The previous 572 MB test6 v1 trace
also passed again on all three backends, with 1,229,052 validated events per
run. Neither real route yet exercises the new VIA recorder hook in situ.

The native EccDB writer also creates a fixed `DesignVia` when a routed
`frViaDef` is absent from EccDB and marked `isAddedByRouter()`. It copies all
lower-routing, cut, and upper-routing rectangles through `DesignViaService`
before storing the wire reference; missing non-router VIAs still fail. The
native `eccdbAdapterTest` suite passed 6/6, including focused generated-VIA
and missing-VIA cases. A separate sample2 EccDB route completed with zero DRC,
and its normalized routing snapshot matched the ODB route byte-for-byte.
This real sample did not exercise creation of a new block VIA. The durable
OpenROAD patch passed `git apply --check` against a clean source archive.

## Independent Term/Pin Test6 V3 Replay, 2026-09-30

The current independent InstanceTerm/BlockPin model was replayed against the
full v3 workload. The original temporary v3 JSONL had been deleted, so it was
regenerated with the same ODB recorder and route configuration. Its SHA-256
matches the 2026-09-28 original exactly:
`0cfd87eb11d2a08cfc05d4fdd901b9659885319aea5bd099f27ffdb2c59887b8`.
The new trace is retained at `build/term-pin-v3-20260930.trace.jsonl`.

Each backend replayed once, concurrently on CPUs 24/25/26. ODB and EccDB
validated all 1,878,138 events across 34 observed groups (40 declared).
Their full totals were 5367.977 ms and 4417.523 ms. iDB validated 1,336,980
events and marked 541,158 unsupported; its 1478.514 ms partial total is not
comparable to those full totals. Across the 18 shared groups, totals were
1269.027 / 1937.960 / 1478.514 ms for ODB / EccDB / iDB.

These are cumulative adapter execution and returned-value projection times;
parser, binding and validation are excluded. The regenerated ODB route itself
finished in 152.628 s with zero DRC. This is not three fresh router runs.

The earlier 2026-09-30 eight-group result used the older v1 trace and is a
limited check, not the full v3 comparison. The complete new table, counts,
unobserved groups and correctness boundaries are archived here:

- [Full 34-group report](results/Term_Pin_Entity_V3_Trace_Replay_20260930.md)
- [Replay JSON](results/Term_Pin_Entity_V3_Trace_Replay_20260930.json)
- [ODB route metrics](results/Term_Pin_Entity_V3_RouteMetrics_20260930.json)

## ISPD18 Test6 V3 Single-Run, 2026-09-28

One real ODB-backed TritonRoute run on the original test6 inputs recorded
1,878,138 API-group events (34 observed groups out of 40 declared). The design
has 107,919 instances and 107,701 ordinary nets. The router used 64 threads,
finished in 153.60 s with zero DRC, and peaked at 7,876,344 KiB RSS; the
process had a 500 GiB virtual-memory limit. Both guide-update and net-track
writer paths were enabled. The v3 JSONL trace was 1.1 GB and was used only as
temporary replay input. Each backend was measured once on CPU 24, as requested.

The following values are **single-run API-group totals in milliseconds**, not
per-call latencies or whole-router runtime. `-` means the iDB backend explicitly
reported that projection unsupported; zero means less than 0.0005 ms after
rounding. LEF/DEF/guide parsing, trace parsing, token binding, normalization,
and validation are outside the event timers.

| API group | Calls | ODB (ms) | EccDB (ms) | iDB (ms) |
| --- | ---: | ---: | ---: | ---: |
| find_pin | 690,219 | 173.500 | 155.553 | 180.644 |
| find_instance | 107,919 | 108.699 | 47.345 | 81.882 |
| read_instance | 107,919 | 26.342 | 30.748 | 41.658 |
| read_instance_pins | 107,919 | 47.167 | 52.543 | 86.784 |
| replace_instance_pin_access | 107,919 | 2671.323 | 790.312 | - |
| net_pins | 107,701 | 191.117 | 193.117 | 238.644 |
| read_guides | 107,701 | 326.344 | 383.393 | - |
| read_net_attributes | 107,701 | 88.978 | 63.254 | - |
| read_wire | 107,701 | 15.721 | 24.438 | 17.997 |
| replace_guides | 107,701 | 560.060 | 642.655 | - |
| append_track_assignment | 107,589 | 396.315 | 406.831 | - |
| replace_wire | 107,589 | 659.529 | 1350.952 | 767.681 |
| append_bterm_pin_access | 1,211 | 2.065 | 1.785 | - |
| read_bterm_pin_access | 1,211 | 0.325 | 0.184 | - |
| replace_master_pin_access | 117 | 74.195 | 116.321 | - |
| scan_nets | 3 | 14.818 | 7.331 | 21.637 |
| read_blockages | 1 | 0.003 | 0.001 | 0.001 |
| read_bterms | 1 | 4.117 | 1.236 | 1.125 |
| read_cut_layer_rules | 1 | 0.007 | 0.012 | - |
| read_design_vias | 1 | 0.001 | 0 | 0 |
| read_die | 1 | 0.001 | 0.003 | 0 |
| read_gcell_grid | 1 | 0.001 | 0.002 | 0.001 |
| read_library_masters | 1 | 0.079 | 0.071 | 0.072 |
| read_library_mpin_geometry | 1 | 1.799 | 1.773 | - |
| read_library_obstructions | 1 | 0.042 | 0.039 | - |
| read_library_terms | 1 | 0.544 | 0.499 | - |
| read_ndrs | 1 | 0.003 | 0.004 | - |
| read_routing_layer_rules | 1 | 0.031 | 0.032 | - |
| read_tech_header | 1 | 0.003 | 0.006 | - |
| read_tech_layers | 1 | 0.019 | 0.037 | 0.019 |
| read_tech_vias | 1 | 0.033 | 0.031 | 0.026 |
| read_via_generate_rules | 1 | 0.001 | 0.005 | - |
| scan_instances | 1 | 4.522 | 6.341 | 9.233 |
| scan_tracks | 1 | 0.005 | 0.004 | 0.004 |

ODB and EccDB validated all 1,878,138 events. Their full measured API totals
were 5.368 s and 4.277 s, respectively. iDB validated 1,336,980 events and
explicitly marked the remaining 541,158 unsupported; its 1.447 s partial
total is **not** comparable to the two full totals. On the 18 operation groups
all three executed, totals were ODB 1.246 s, EccDB 1.870 s, and iDB 1.447 s.
EccDB's overall advantage comes mainly from instance pin-access replacement
(0.790 s versus ODB 2.671 s); its routed-wire replacement is slower (1.351 s
versus ODB 0.660 s). This one-run result is directional, not a stable speedup
estimate.

All three backends checked 107,589 final written wires, 2,957,852 shared
routing primitives, and 107,701 net-pin sets. ODB/EccDB also checked 1,099
segment endpoint-extension fields; iDB's wire check is restricted to shared
geometry. This does not prove byte-identical DEF output or equal router runtime.
The raw trace was temporary and is not archived; the compact result retains
input/executable hashes and operation-level counts.

- [Single-run result JSON](results/DrtTrace_Test6_V3_OneRun_20260928.json)
- [Route metrics JSON](results/DrtTrace_Test6_V3_RouteMetrics_20260928.json)

## Historical Test6 V1 Results, 2026-09-28

The real ODB-backed route used the original, unmodified ISPD18 test6 LEF, DEF,
and guide: 107,919 instances and 107,701 ordinary nets. Routing completed with
zero DRC using 64 threads. Compilation used 128 jobs; processes were limited to
500 GiB virtual address space. Replay is serial to preserve the recorded order.
No parser time or validation time is included in the table.

| API Group | Recorded Calls | ODB (ms) | EccDB (ms) | IDB (ms) |
| --- | ---: | ---: | ---: | ---: |
| Enumerate instances | 1 | 9.86 | 5.75 | 8.81 |
| Read instance fields | 107,919 | 16.11 | 15.92 | 28.49 |
| Enumerate ordinary nets | 3 | 17.43 | 10.25 | 26.79 |
| Read Net-Pin endpoints | 107,701 | 234.12 | 198.18 | 186.77 |
| Read initial wire (all empty) | 107,701 | 7.19 | 14.07 | 11.90 |
| Find instance by name | 107,919 | 41.35 | 20.60 | 48.18 |
| Find instance pin by term name | 690,219 | 74.11 | 71.51 | 104.66 |
| Replace routed wire | 107,589 | 437.94 | 1189.90 | 946.35 |
| Total | 1,229,052 | 838.35 | 1527.97 | 1367.94 |

Values are the medians of three fresh-process repetitions. The total is the
median of each run's total, not the sum of the independent row medians. These
are API-group totals, not single-call latencies. EccDB's current service-based
wire write is 2.72x ODB and dominates its total. This measurement does not
separate storage costs from service conversion or name resolution costs.

All 107,701 initial wire reads returned empty geometry. This therefore does
not measure reading already-routed wires and does not replace that earlier
microbenchmark. No clear-wire, instance/net creation, or logical connect events
occurred; they were not artificially added. Wire replacement is a semantic
primitive sequence, not a byte-identical replay of every encoder opcode.

### Validation

All nine runs validated every recorded event, immediately reread every written
wire, and finally checked all 107,701 recorded wires and all 107,701 Net-Pin
endpoint sets. The 107,589 written nets contained 2,957,852 primitives:
1,122,672 segments, 1,391,014 vias, and 444,166 patches. Duplicate multiplicity
is preserved. ODB/EccDB also matched all 1,099 explicitly recorded segment
extension fields. IDB matched shared geometry/connectivity only; it does not
retain these extensions. No claim of full DEF metadata or path/topology equality
is made.

The small sample2 integration test also passed all three backends after a
zero-DRC route: 251 events, 16 nets, 110 primitives, five extension fields.
`eccdb_drt_trace_test` passes and covers incomplete/count-mismatched traces,
integer bounds, direction/extension normalization, duplicate geometry, wrong
read results, damaged writes, collateral wire damage, and changed connectivity.
The preserved OpenROAD patch passes `git apply --check` against a clean
upstream `48e8467bceaf` archive. No existing Storage/Service implementation was
changed for this experiment.

### Artifacts

- [Archived three-run result JSON](results/DrtTrace_Test6_20260928.json)
- Full trace, router outputs, metrics, and replay logs:
  `build/drt-trace-test6-20260928.W8Z1f3/`
- Trace SHA-256:
  `d078acee269d82b5b8b373a1cd6f490b367538283cd338f8d9364aa7963ea4a2`
- The capture run's 150.82-second router time includes recording overhead and
  is not used in the backend performance comparison.

Native ODB replay is GCC 13/C++20 Release with LTO disabled; the top-level
EccDB/IDB replayer is Release with executable LTO enabled. Both use their current
library builds. These are implementation-level adapter timings, not a
compiler-normalized claim about the underlying data structures.

## ISPD19 Test10 Import-Side Results, 2026-09-28

The original test10 input contains 899,404 instances and 895,253 ordinary
nets. ODB's actual TritonRoute `Parser` produced a complete 394 MB v2 trace
with 2,689,912 calls. The three backends replayed that trace in fresh
processes, three times each, with CPU affinity 24 and a 500 GiB address-space
limit. All nine runs passed every event and the final 895,253 Net-Pin and wire
checks. The input has no ordinary routed wire geometry, so this is an
**import-side API comparison**, not a route-write benchmark.

| API group | Calls | ODB (ms) | EccDB (ms) | iDB (ms) |
| --- | ---: | ---: | ---: | ---: |
| Net-Pin endpoints | 895,253 | 2031.91 | 1817.19 | 1683.65 |
| Instance fields | 899,404 | 120.81 | 132.23 | 268.21 |
| Initial wire reads | 895,253 | 70.39 | 117.92 | 101.21 |
| Instance enumeration | 1 | 60.05 | 60.51 | 86.70 |
| Ordinary-net enumeration | 1 | 54.01 | 21.16 | 79.01 |
| Total | 2,689,912 | 2341.58 | 2151.96 | 2217.78 |

Values are three-run medians in milliseconds. Each total is the median of a
run's total, not the sum of independently rounded row medians. LEF/DEF parsing,
trace parsing, token binding, and validation are excluded from these timers.
The trace SHA-256 is
`9123c89ca3c698ac7a090a9254920685b9dc10c58a13772434d0c55dd8027d50`;
the full [three-run result JSON](results/DrtTrace_Test10_Import_20260928.json)
records executable and input hashes. The large raw JSONL trace was removed
after the comparison; only the compact result JSON remains. A full test10
route was started but cancelled before routing completed, so there is no
million-scale wire-write result. Future runs of this size should use streaming
aggregation or in-process instrumentation rather than retaining JSONL events.
