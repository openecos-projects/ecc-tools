# Native EccDB / TritonRoute integration

Verified on 2026-09-28 using ISPD 2018 and ISPD 2019 files under
`/home/zhuxu1/workspace/reference`.

The 2026-10-08 Service API migration uses a shared `DatabaseContext` and the
business entry points described in [Service layer](Service_Layer_20261007.md).
The historical routing results below are not new measurements of that revision.

## Data path

```text
LEF / DEF / guides
       |
EccDB TechStore + LibraryStore + DesignStore
       |
DatabaseContext -> PhysicalDesignService / RoutingService
       |
native converters -> TritonRoute frTech / frMaster / frBlock
       |
Pin Access -> Guide Processing -> Route Preparation -> Track Assignment -> DR
       |
EccDB route and pin-access writeback -> EccDB DEF exporter
```

The EccDB branch does not create an OpenDB database, tech, library, instance,
terminal, wire, or rule object. It reads its own LEF/DEF files, not ODB-derived
JSON. The ODB baseline is a different, fresh process using ODB's own readers.
Both branches use the same TritonRoute routing engine and parameters.

ODB value types/enums, geometry utilities, headers and linked libraries remain.
This is independence from ODB **database objects**, not removal of every ODB
source dependency. `dbDatabase*`, layer/instance back-pointers, selected APs,
and all master/IO candidate AP back-pointers are checked for null in native mode.
The supported rule-conversion and routing paths were also inspected for ODB
object access; this check is not a heap-wide allocation audit.

## Code locations

| File | Responsibility |
| --- | --- |
| `adapters/tritonroute/NativeEccdbTechnology.cpp` | Value-backed layer rules and via definitions |
| `adapters/tritonroute/NativeEccdbDesign.cpp` | Masters, ports/OBS, instances, IO, nets, connectivity, tracks/blockages |
| `adapters/tritonroute/NativeRouteMain.cpp` | Independent database loading, headless CLI, timing and native checks |
| `adapters/tritonroute/NativeRouteSnapshot.cpp` | Canonical semantic comparisons, outside timing |
| `adapters/tritonroute/NativeDatabaseSnapshot.cpp` | Native DB placement-state comparison, not reduced to the DRT model |
| Fork `src/drt/src/io/EccdbRouteAdapter.cpp` | Existing service-based route read/write |
| Fork `src/drt/src/io/EccdbContextAdapter.cpp` | Guides, explicit GCell grids and pin access |
| Fork `frLayer`, `frInst`, `TritonRoute`, `GuideProcessor`, PA/DR | Remove required ODB object access on the native path |

The current local fork is `../OpenROAD-eccdb`, branch `eccdb-native-ispd`, next
to ecc-tools. The original `/home/zhuxu1/workspace/OpenROAD` remains the
dependency checkout. Existing embedded-instance-pin experiment
changes in ecc-tools were retained. `build/openroad-native-eccdb-src` is the
historical fork location for results collected before this relocation; those
results remain unchanged.

`adapters/tritonroute/OpenRoadNativeEccdb.diff` preserves all DRT changes,
including the preceding hybrid adapter, against upstream commit `48e8467bceaf`.
Apply it only to a clean checkout of that base, not to the already patched fork.
`OpenRoadLocalBuild.diff` separately preserves the existing yaml-cpp target
compatibility adjustment needed by the local dependencies.

The required local environment script is preserved as
`adapters/tritonroute/OpenRoadLocalEnv.sh`. It is an exact copy of
`../OpenROAD/scripts/eccdb-local-env.sh`; install it at that location in a new
dependency checkout before using the build scripts. Do not source the archived
copy in place: it derives the OpenROAD root from its own `scripts/` location.
The `.local` dependency packages and `build-standalone/_deps/fmt-src` are still
external build prerequisites, not files supplied by the ecc-tools commit.

## Build and test

The normal configured build uses GCC 13, C++20, Release `-O3 -DNDEBUG`, Ninja,
ccache, GUI/Python/tests disabled, and LTO disabled. Dependencies and the scoped runtime
environment come from `../OpenROAD/.local` and `../OpenROAD/scripts/eccdb-local-env.sh`.
No global shell profile, system proxy or system package was changed.

Run from the ecc-tools root:

```bash
bash src/database/eccdb/adapters/tritonroute/BuildNativeRoute.sh
bash src/database/eccdb/adapters/tritonroute/TestNativeFixtures.sh
bash src/database/eccdb/adapters/tritonroute/TestNativeAdvancedImport.sh
bash src/database/eccdb/adapters/tritonroute/TestNativeRoute.sh \
  ../reference/ispd2018/ispd18_test6 /tmp/native-test6 64
bash src/database/eccdb/adapters/tritonroute/BenchmarkNativeImport.sh \
  ../reference/ispd2018/ispd18_test6 /tmp/native-import
```

`BuildNativeRoute.sh` and `TestNativeFixtures.sh` now default
`OPENROAD_NATIVE_SOURCE` to `../OpenROAD-eccdb`; the variable still overrides
that location. `OPENROAD_DEPENDENCY_SOURCE` continues to default to
`../OpenROAD`, and the build output remains
`build/openroad-native-eccdb-build`. A CMake cache created from the historical
`build/openroad-native-eccdb-src` source cannot be reused for the new source.
After removing that old cache, reconfigure the retained build directory without
compiling native targets:

```bash
bash src/database/eccdb/adapters/tritonroute/BuildNativeRoute.sh --configure-only
```

Run `BuildNativeRoute.sh` without options when the native targets should be
rebuilt.

The local build can enable the adapter unit target separately with
`cmake -S ../OpenROAD-eccdb -B build/openroad-native-eccdb-build
-DENABLE_TESTS=ON`, followed by `cmake --build
build/openroad-native-eccdb-build --target eccdbAdapterTest --parallel 64`.
The historical integration run passed 9/9 adapter tests and 63/63 EccDB
`eccdb_design_test` cases. The 2026-10-07 relocation verified configuration
only and removed the old native executables; it did not repeat those tests.

Build and full routing default to 64 threads. Each process has a 500 GiB virtual
address-space limit; this is not an aggregate cgroup RSS cap. The import-only
projection is serial on both sides. `BenchmarkNativeImport.sh` runs exactly
three fresh-process repetitions per backend, sequentially.
The full-route test requires zero DRC from both backends, not merely equal reports.

## Correctness results

| Test | Result |
| --- | --- |
| Original `ispd18_sample2`: 23 instances / 16 nets | Full dual-backend routing, 0 DRC, identical semantic snapshots and four DEF readbacks |
| Eight instance orientations, PLACED/FIXED/COVER status | Identical transforms and status; all four writer/reader readbacks match |
| Explicit extensions, static via, independent RECT patch, ROUTED/FIXED fixture | Identical import geometry; both writers' DEF read by both readers without geometry, connectivity or fixed-status loss |
| Legal DEF VIRTUAL fixture | Identical import; VIRTUAL resets the physical origin without inventing a copper segment |
| SPECIALNET fixture | Identical special-pin connectivity, routed strap, VIA array and standalone RECT |
| SPECIALNET POLYGON fixture | Both ODB and EccDB reject it; OpenDB's DEF reader does not support this form |
| ISPD19 test5, 28,920 instances / 29,416 nets | ODB/EccDB import snapshots match, including two NDRs and two SPECIALNETs |
| Generated DEF VIA fixture, using ISPD19 test5 LEF | ODB/EccDB imported VIA definition and routed geometry match |
| ISPD19 test10, 899,404 instances / 895,253 nets | ODB/EccDB import snapshots match, including LEF58 CORNERSPACING, VIARULE GENERATE and two SPECIALNETs |
| Original `ispd18_test6`: 107,919 instances / 107,701 nets | Full dual-backend routing rerun, 0 DRC, identical snapshots, routing configuration and DRC reports |
| Routed test6 DEF: ODB writer x ODB/EccDB reader, EccDB writer x ODB/EccDB reader | All four readbacks preserve geometry, connectivity and original placement status |

Test6 has **2,957,852 routed geometric primitives** after routing. Snapshots
compare layer rules, generated-via rules, NDRs, via/master/port/OBS geometry, instance geometry transforms,
pin-net associations, endpoint order, selected APs, routed segment/via/patch
geometry, widths/extensions, fixed status, and DRC markers. Sorting preserves
duplicate records. The readback snapshot intentionally excludes volatile object
IDs, endpoint order, APs and runtime-derived tech state.

The test5 source DEF places GROUPS and REGIONS before COMPONENTS. OpenDB
resolves group members immediately and rejects that order. The advanced-import
script makes a temporary copy with those two complete sections after
COMPONENTS; it supplies that same copy to both backends. The original file is
not changed. Test10 and all ISPD18 fixtures use their original files.
Special-net pins may be connected implicitly by supply use, so the snapshot
compares their endpoint multiset without a container ordinal. Ordinary-net
endpoint order remains part of the runtime snapshot.

`placement.txt` is generated separately from each original database, not from
DRT, which has no instance placement-status field. It compares normalized
instance status and per-IO status multisets. ODB NONE/UNPLACED map to UNPLACED;
its DEF FIXED representation FIRM maps to FIXED. IO port order is ignored here,
while port geometry is checked in the DRT snapshot. This is not an exhaustive
audit of all optional DEF metadata or of status-to-geometry pairing on
multi-port IO pins with different statuses.

This proves equality of the compared geometry/connectivity semantics, not
byte-identical DEF or identical wire opcode/path grouping. EccDB writes route
primitives as separate canonical paths. DRC markers remain in `frDesign` and
the report; this task does not add persistent EccDB DRC-marker storage.
Pin-net membership comparisons are not an independent physical connectivity
extraction; geometric connectivity is handled by the shared TritonRoute engine.

The original Metal9 PRL width rows, including their non-monotonic order, were
used unchanged. No repaired LEF copy was used. Standard ISPD input behavior was
aligned explicitly: reverse file-order guides (ODB head insertion), GRT-derived
GCell patterns, and signal routing from Metal1 through Metal9. For test6 the grid
is X `(100, 571, 3000)` and Y `(0, 354, 3000)`.

## Performance: test6 only

The three-run raw-projection measurements below are from 2026-09-27, before
the broader rule/special-net support was added; they have not been rerun for
the current code. Fresh processes used the same executable and Release build.
Raw import timing excludes
LEF/DEF parsing, guide parsing, DEF output, null-pointer checks and semantic
snapshot generation. It measures **already-built DB -> raw TritonRoute model**,
including Tech/Library/Design conversion, without postProcess or PA. It is not
standalone CRUD latency or parser-free database construction time.

| Measurement | ODB | EccDB |
| --- | ---: | ---: |
| Raw projection run 1 | 888.806 ms | 431.109 ms |
| Raw projection run 2 | 892.708 ms | 434.319 ms |
| Raw projection run 3 | 889.811 ms | 441.973 ms |
| Raw projection median | **889.811 ms** | **434.319 ms** |
| Loaded DB RSS median, without guides | 127.7 MiB | 123.0 MiB |
| DB + raw TritonRoute model RSS median | 301.2 MiB | 304.2 MiB |
| Full routing once, 64 threads (2026-09-28) | 133.177 s | 133.863 s |
| RSS after full routing (2026-09-28) | 7.610 GiB | 7.464 GiB |
| Routed DEF write once (2026-09-28) | 1.546 s | 0.590 s |

The earlier raw EccDB projection took 48.8% of ODB time (about 2.05x speedup),
but that result is not a current-code performance claim. Full routing is
dominated by the shared routing engine; the latest single-run EccDB/ODB
times differ by about 0.5%, insufficient to claim a repeatable speed difference.
Full timing includes DRT import, postProcess, PA,
guide processing, preparation, TA, detailed routing and database writeback;
it excludes initial parsers and final DEF text writing.

`rss_loaded_kib` / `rss_drt_kib` are sampled current process RSS, not DB-only
allocation totals. `peak_before_snapshots_kib` is the process high-water mark
after native checks and DEF output, before expensive comparison snapshots.
The initial measurement files used the older name `peak_before_validation_kib`
for that same boundary; it was renamed to avoid implying that export/verification
were excluded. Reported RSS here does not use that peak field.

Artifacts:
`/tmp/eccdb-native-route-final`,
`/tmp/eccdb-native-advanced.WwPv5D`,
`/tmp/eccdb-native-fixtures.2AKahK`,
`/tmp/eccdb-native-ispd.E0C1UF/test6-final`,
`/tmp/eccdb-native-ispd.E0C1UF/sample-verified`,
`/tmp/eccdb-native-ispd.E0C1UF/projection-final`,
`/tmp/eccdb-native-ispd.E0C1UF/placement-test6`,
`/tmp/eccdb-native-fixtures.kq5sZX`.

## Current boundary

Supported and tested: headless ISPD18 routing; ISPD19 test5/test10 import;
ordinary and special nets; referenced LEF/DEF NDRs; VIARULE GENERATE and
generated DEF vias; LEF58 CORNERSPACING; special straps, VIA arrays and RECTs;
ordinary-net VIRTUAL paths; service-based geometry/context writeback.
Technology/geometry metadata that DRT's ODB importer ignores remains in the
EccDB source database rather than being discarded from storage.

The native adapter still deliberately rejects inputs that cannot be projected
faithfully into the current DRT model, including oriented route vias,
SPECIALNET POLYGON, special-path VIRTUAL/TAPERRULE/STYLE, and certain DEF NET
options. `USEMINSPACING OBS OFF` and LEF58 backside layers are not yet
projected with full ODB parity. EccDB preserves DEF TAPER into DRT's tapered
route flag, whereas the existing ODB parser loses it; this is a known
runtime-state difference on designs that actually contain TAPER.

GUI, ODB snapshots/debug-worker replay, distributed routing, incremental ECO,
targeted ODB-instance pin access and PDN repair are not supported in native mode.
The full ISPD 2019 routing flow has not been validated; test5/test10 above are
import-only correctness checks.
