# EccDB Differential Testing Guide

English | [简体中文](Differential_Testing.zh-CN.md)

This guide describes the EccDB differential test suites, what each suite compares, how to configure them, which LEF/DEF corpora are required, and the criteria for claiming a complete pass.

## 1. What differential testing means

Differential testing does not normally compare raw exported LEF/DEF text. Object order, whitespace, default values, and VIA representations may differ without changing database semantics.

The current tests use four kinds of oracle:

| Oracle | Comparison | Purpose |
| --- | --- | --- |
| OpenROAD SI2 golden | Byte-exact reader output | Validate the complete LEF/DEF 5.8 grammar reader |
| Legacy iDB | Convert both import paths to EccDB objects and compare fields | Validate direct LEF import against iDB adapter materialization |
| OpenDB | Normalize DEF with OpenDB, re-import it into EccDB, and compare semantic snapshots | Validate DEF behavior against an independent database |
| Legacy iRT/iDRC wrapper | Feed the same LEF/DEF through iDB and EccDB adapters | Validate tool-visible data, writeback, and geometry results |

Comparison helpers sort collections by stable keys to remove legal iteration-order differences. Sorting belongs in snapshots and differ code, not in production iRT/iDRC wrappers where it could hide a real input difference.

## 2. Required test layers

| Layer | Target/source | Input | Main assertion |
| --- | --- | --- | --- |
| Unit and IO baseline | `tests/unit`, `tests/io` | Synthetic LEF/DEF and PDK LEF | Storage invariants, LEF/DEF round trips, binary fixed points |
| Complete SI2 syntax | `eccdb_opendb_design_differential_test` | OpenROAD `complete.5.8.lef/def` and `.au` | SI2 reader output is byte-identical to OpenROAD golden output |
| Legacy iDB conversion | `eccdb_lef_idb_conversion_test` | Synthetic data and Sky130 | iDB-parser-to-EccDB adapter materialization |
| Direct LEF object differential | `eccdb_direct_lef_differential_test` | Synthetic qualifiers and Sky130 | Field equality for layers, vias, macros, pins, ports, and related objects |
| Full LEF semantic corpus | `eccdb_lef_full_corpus_semantic_test` | Sky130, IHP130, OpenROAD gscl45 | Shared direct/iDB fields, rectangles, and canonical export fixed points |
| DEF semantic fixed point | `eccdb_def_full_corpus_semantic_test` | OpenROAD ODB/DRT/GPL DEF | Equal snapshots and complete net-routing semantics after import/export/import |
| OpenDB DEF differential | `eccdb_opendb_design_differential_test` | Synthetic, OpenROAD routed DEF, and ISPD | Modeled semantics survive OpenDB normalization of source and EccDB-exported DEF |
| iRT adapter differential | `irt_adapter_differential_test` | ISPD2018/2019 and external LEF/DEF | Wrapper input, routing snapshots, DEF writeback, and DRC geometry |
| iDRC adapter differential | `idrc_adapter_differential_test` | ISPD2018/2019 and synthetic geometry | Wrapper state and canonical wire/via shape and violation sets |

Memory benchmarks and memory comparisons are resource tests, not correctness differentials. Run them after correctness is established.

The current DEF structural snapshot compares component counts plus global, row, track-grid, gcell-grid, instance, instance-pin, IO-pin, design-VIA, NDR, regular/special-net, region, group, blockage, and fill data. Routing comparison also checks wire/path primitives, point/via/rectangle extras, and expanded geometry per net. LEF differentials locate technology layers, VIA/VIA rules, NDRs, sites, macros, terms, ports, and obstructions by name and compare their modeled fields and geometry.

## 3. Build dependencies

The standalone EccDB differential build requires:

- a C++20 compiler;
- CMake 3.20 or newer;
- Ninja;
- Boost 1.75 or newer, including Boost.PFR;
- GoogleTest;
- checked-in `src/third_party/lefdef` and `src/third_party/entt` sources.

OpenDB is not linked into EccDB. It is a runtime oracle. The recommended configuration uses the Python module built by OpenROAD:

```text
OpenROAD/bazel-bin/src/odb/
├── odb.py
└── _odb.so
```

A standalone `odbtcl` executable is also supported.

## 4. Required data and layouts

### 4.1 Ecc-tools PDK LEF corpus

`ECCDB_TEST_DATA_ROOT` must point to a data/source root containing:

```text
ECCDB_TEST_DATA_ROOT/
└── scripts/foundry/
    ├── sky130/lef/
    │   ├── sky130_fd_sc_hd.tlef
    │   ├── sky130_fd_sc_hd_merged.lef
    │   ├── sky130_fd_sc_hs.tlef
    │   └── ... IO and SRAM LEF files
    └── ihp130/ihp-sg13g2/libs.ref/
        ├── sg13g2_stdcell/lef/
        ├── sg13g2_io/lef/
        └── sg13g2_sram/lef/
```

These files drive the Sky130/IHP130 importer, exporter, legacy iDB, and binary fixed-point tests. Once those suites are selected, missing PDK files are normally failures rather than acceptable skips.

### 4.2 OpenROAD source corpus

`OPENROAD_SOURCE_DIR` points to the OpenROAD source root. Tests consume:

```text
src/odb/src/lef/TEST/complete.5.8.lef[.au]
src/odb/src/def/TEST/complete.5.8.def[.au]
src/odb/test/data/gscl45nm.lef
src/odb/test/data/{design,design58,ndr,parser_test,virtual_route}.def
src/odb/test/data/gcd/*.def
src/odb/test/data/sky130hd_multi_patterned.def
src/odb/test/data/sky130hd/sky130hd_multi_patterned.tlef
src/gpl/test/nangate45.lef
src/gpl/test/large01.def
src/drt/test/aes_nangate45_preroute.def
test/Nangate45/{Nangate45_tech,Nangate45_stdcell}.lef
```

The `complete.5.8` files are grammar corpora with intentional dangling names, invalid table dimensions, and other boundary cases. They are compared at the SI2 reader-output level and must not be treated as one consistent technology/library/design database.

### 4.3 ISPD2018/2019 wrapper corpora

iRT and iDRC wrapper tests use flat extracted roots:

```text
ECCDB_ISPD18_ROOT/
├── ispd18_sample/ispd18_sample.input.{lef,def}
├── ispd18_sample2/...
├── ispd18_sample3/...
└── ispd18_test1 ... ispd18_test10/

ECCDB_ISPD19_ROOT/
├── ispd19_sample ... ispd19_sample4/
└── ispd19_test1 ... ispd19_test10/
```

Each case needs matching `.input.lef` and `.input.def` files.

### 4.4 Large ISPD2019 routed solution

The largest OpenDB and Design binary stress cases use a different official-package layout:

```text
ECCDB_ISPD19_ROOT/
├── input/ispd19_test10/ispd19_test10.input.lef
└── solutions/extracted/test10/def/12.t10.def
```

This is not the flat wrapper layout. Create a combined root containing both layouts, or change `ECCDB_ISPD19_ROOT` between suites. An `ispd19_test10.input.def` file is not a substitute for `12.t10.def`.

## 5. Configuration variables

| Variable | Stage | Meaning |
| --- | --- | --- |
| `ECCDB_ECC_TOOLS_ROOT` | CMake | Ecc-tools root providing third-party and legacy iDB sources |
| `ECCDB_TEST_DATA_ROOT` | CMake | Root providing `scripts/foundry`; it may differ from the source root |
| `OPENROAD_SOURCE_DIR` | CMake or runtime | OpenROAD corpus root; runtime value overrides the compiled default |
| `OPENDB_PYTHON_MODULE_DIR` | CMake | Directory containing `odb.py` and `_odb.so` |
| `OPENDB_PYTHON` | Runtime | Python interpreter override |
| `OPENDB_PYTHONPATH` | Runtime | OpenDB Python module directory override |
| `OPENDB_TCL` | Runtime | Optional `odbtcl` executable |
| `ECCDB_ISPD18_ROOT` | Runtime | Extracted ISPD2018 root |
| `ECCDB_ISPD19_ROOT` | Runtime | Extracted or combined ISPD2019 root |
| `ECCDB_RUN_LARGE_DEF_TESTS=1` | Runtime | Enable `large01`, AES preroute, and the largest OpenDB DEF |
| `ECCDB_RUN_LARGE_DESIGN_BINARY_TESTS=1` | Runtime | Enable the ISPD2019 test10 Design binary fixed point |
| `ECCDB_NET_COMPARE_THREADS` | Runtime | Large-net semantic comparison threads; use `128` |
| `ECCDB_RT_THREAD_NUMBER` | Runtime | iRT routing threads; current runs require `128` |
| `ECCDB_KEEP_TEMP=1` | Runtime | Preserve iRT temporary DEF, snapshots, and logs |
| `ECCDB_IRT_WRAP_LEF/DEF` | Runtime | Custom files for `Wrap.ExternalLefDef` |

## 6. Standalone EccDB differential build

Example for the current workspace:

```bash
export ECCDB_SRC=/home/zhuxu1/workspace/ecc-tools-eccdbv1
export ECCDB_DATA=/home/zhuxu1/workspace/ecc-tools
export OPENROAD_ROOT=/home/zhuxu1/workspace/OpenROAD
export ECCDB_DIFF_BUILD="$ECCDB_SRC/build/eccdb-differential"

cmake -S "$ECCDB_SRC/src/database/eccdb" -B "$ECCDB_DIFF_BUILD" -G Ninja \
  -DECCDB_BUILD_TESTS=ON \
  -DECCDB_STANDALONE_LEGACY_IDB=ON \
  -DECCDB_TEST_DATA_ROOT="$ECCDB_DATA" \
  -DOPENROAD_SOURCE_DIR="$OPENROAD_ROOT" \
  -DOPENDB_PYTHON_MODULE_DIR="$OPENROAD_ROOT/bazel-bin/src/odb"

cmake --build "$ECCDB_DIFF_BUILD" \
  --target eccdb_differential_tests --parallel 128
```

`ECCDB_STANDALONE_LEGACY_IDB=ON` implies `ECCDB_STANDALONE_LEF_DEF`. It builds only the minimal legacy iDB LEF path, not the full DEF, Verilog, GDS, iRT, or iDRC trees.

Explicitly build the IO and binary baseline targets before following the recommended execution order:

```bash
cmake --build "$ECCDB_DIFF_BUILD" --parallel 128 --target \
  eccdb_lef_tech_importer_test \
  eccdb_lef_library_importer_test \
  eccdb_lef_tech_exporter_test \
  eccdb_lef_library_exporter_test \
  eccdb_def_design_roundtrip_test \
  eccdb_binary_database_archive_test \
  eccdb_design_binary_database_archive_test
```

Set runtime data paths before running:

```bash
export OPENROAD_SOURCE_DIR="$OPENROAD_ROOT"
export OPENDB_PYTHON="$(command -v python3)"
export OPENDB_PYTHONPATH="$OPENROAD_ROOT/bazel-bin/src/odb"
export ECCDB_ISPD18_ROOT="$ECCDB_DATA/reference/ispd2018"
export ECCDB_ISPD19_ROOT="$ECCDB_DATA/reference/ispd2019"
export ECCDB_NET_COMPARE_THREADS=128
```

List and run the default differential tests:

```bash
ctest --test-dir "$ECCDB_DIFF_BUILD" -N -L eccdb_differential
ctest --test-dir "$ECCDB_DIFF_BUILD" \
  --output-on-failure --parallel 128 --label-regex eccdb_differential
```

Large DEF cases are opt-in:

```bash
export ECCDB_RUN_LARGE_DEF_TESTS=1
ctest --test-dir "$ECCDB_DIFF_BUILD" \
  --output-on-failure --parallel 128 --label-regex eccdb_differential
```

## 7. Running groups and individual cases

Use the test executables for precise filters:

```bash
"$ECCDB_DIFF_BUILD/tests/differential/eccdb_direct_lef_differential_test" \
  --gtest_filter='DirectLefDifferentialTest.*'

"$ECCDB_DIFF_BUILD/tests/differential/eccdb_lef_full_corpus_semantic_test" \
  --gtest_filter='LefFullCorpusSemanticDifferentialTest.*'

"$ECCDB_DIFF_BUILD/tests/differential/eccdb_def_full_corpus_semantic_test" \
  --gtest_filter='OpenRoadOdbData/*'

"$ECCDB_DIFF_BUILD/tests/differential/eccdb_opendb_design_differential_test" \
  --gtest_filter='OpenRoadOdbData/*'
```

The fast OpenROAD routed-DEF group contains:

- `virtual_route`
- `ndr`
- `sky130hd_multi_patterned`
- `gcd_floorplan`
- `gcd_placed`
- `gcd_pdn`
- `gcd_nangate45_route`
- `gcd_route_via_only_layer`
- `gcd_route_with_power_pins`

`large01` and `aes_nangate45_preroute` require `ECCDB_RUN_LARGE_DEF_TESTS`.

## 8. iRT/iDRC wrapper differential

Wrapper tests require the full ecc-tools build and are not produced by the standalone EccDB build:

```bash
export ECCDB_SRC=/home/zhuxu1/workspace/ecc-tools-eccdbv1
export ECC_TOOLS_BUILD="$ECCDB_SRC/build/ecc-tools"

cmake -S "$ECCDB_SRC" -B "$ECC_TOOLS_BUILD" -G Ninja \
  -DECCDB_BUILD_TESTS=ON
cmake --build "$ECC_TOOLS_BUILD" \
  --target irt_adapter_differential_test idrc_adapter_differential_test \
  --parallel 128
```

Keep the project's normal toolchain, package-manager, or dependency options when configuring the full root project; the command above only shows EccDB-related options.

The binaries normally appear under repository `bin/`:

```bash
export ECCDB_ISPD18_ROOT=/home/zhuxu1/workspace/ecc-tools/reference/ispd2018
export ECCDB_ISPD19_ROOT=/home/zhuxu1/workspace/ecc-tools/reference/ispd2019
export ECCDB_RT_THREAD_NUMBER=128

"$ECCDB_SRC/bin/irt_adapter_differential_test" --gtest_list_tests
"$ECCDB_SRC/bin/idrc_adapter_differential_test" --gtest_list_tests

"$ECCDB_SRC/bin/irt_adapter_differential_test" \
  --gtest_filter='Wrap.*'
"$ECCDB_SRC/bin/idrc_adapter_differential_test" \
  --gtest_filter='Wrap.*:Shapes.*:SelfCheck.*'
```

Run actual iRT routing/writeback only for the registered sub-100k group by default:

```bash
ECCDB_KEEP_TEMP=1 ECCDB_RT_THREAD_NUMBER=128 \
  "$ECCDB_SRC/bin/irt_adapter_differential_test" \
  --gtest_filter='IspdUnder100k/*'
```

`IspdAround100k/*` currently contains only ISPD18 test6 at about 108k instances. Treat it as an additional stress case, not part of the sub-100k default.

Both adapter targets are marked `RUN_SERIAL TRUE` in CTest because the tools have global interfaces and shared state. Separate iRT and iDRC tmux sessions are possible, but do not run multiple cases from one wrapper binary concurrently. `ECCDB_RT_THREAD_NUMBER=128` controls internal routing parallelism for one case; it does not mean running 128 test cases at once.

### 8.1 Known ISPD iRT failures

Controlled 128-thread diagnostics completed on 2026-08-27 confirmed **four ISPD cases that fail inside the iRT routing stage itself**:

| Case | Instances | Legacy iDB | EccDB/EnTT | Failure point |
| --- | ---: | --- | --- | --- |
| `Ispd18Test1` | 8,879 | FAIL | FAIL | Second `PinAccessor::updateAccessPoint` pass |
| `Ispd18Test2` | 35,913 | FAIL | FAIL | Second `PinAccessor::updateAccessPoint` pass |
| `Ispd18Test3` | 35,977 | FAIL | FAIL | Second `PinAccessor::updateAccessPoint` pass |
| `Ispd18Test4` | 72,094 | FAIL | FAIL | Second `PinAccessor::updateAccessPoint` pass |

All four fail with the original iDB wrapper even when no EccDB database is created. The iDB and EccDB paths have matching box/violation progress before failure. The current evidence therefore classifies them as common iRT routing/concurrency failures rather than EccDB parser, storage, or adapter mismatches. They fail before routed DEF and snapshot generation, so they must not be described as differing routed results.

These cases are not marked GTest `DISABLED_` or expected-failure tests. A complete `IspdUnder100k/*` run still reports them as failures. This keeps the issue visible; they must not be counted as passes or skips. After an iRT fix, rerun both database paths and remove this known-failure record.

`Ispd19Sample4` is a separate category: both routing workers finish and their routed snapshots match, but the final post-writeback DRC geometry sets have differed. It is a post-routing differential failure, not an iRT routing failure. `Ispd19Sample3` has passed routing, EccDB writeback, DEF round trip, and object comparison, and is the current routed smoke case:

```bash
ECCDB_RT_THREAD_NUMBER=128 \
  "$ECCDB_SRC/bin/irt_adapter_differential_test" \
  --gtest_filter='*Ispd19Sample3'
```

## 9. Binary fixed-point tests

Binary tests are not an independent oracle, but they protect pool records, ID generations, and indexes across persistence:

```bash
cmake --build "$ECCDB_DIFF_BUILD" --parallel 128 --target \
  eccdb_binary_database_archive_test \
  eccdb_design_binary_database_archive_test
ctest --test-dir "$ECCDB_DIFF_BUILD" --output-on-failure \
  -R 'BinaryDatabaseArchiveTest|DesignBinaryDatabaseArchiveTest'
```

The large ISPD2019 solution requires the layout in section 4.4:

```bash
export ECCDB_RUN_LARGE_DESIGN_BINARY_TESTS=1
export ECCDB_ISPD19_ROOT=/path/to/combined-ispd2019-root
ctest --test-dir "$ECCDB_DIFF_BUILD" --output-on-failure \
  -R 'LargeIspd19RoutingPoolReachesByteExactFixedPoint'
```

This imports about 895k wires and tens of millions of path and point records. Its memory and runtime requirements are substantially higher than normal differential tests.

## 10. Recommended execution order

1. Run unit tests, LEF/DEF IO round trips, and small binary tests.
2. Run SI2 complete syntax, legacy iDB conversion, and direct LEF object differential tests.
3. Run the full Sky130/IHP130 LEF corpus.
4. Run small and routed OpenROAD DEF semantic/OpenDB differentials.
5. Run iRT/iDRC `Wrap.*` to validate adapter input.
6. Run the passing iRT `Ispd19Sample3` routed smoke case first, then run `IspdUnder100k/*` as a full diagnostic; the latter currently includes the known failures in section 8.1.
7. Opt into large DEF, the approximately 108k-instance route case, and ISPD2019 test10 binary stress last.

## 11. Pass criteria

A result may be reported as a default differential pass only when:

- every executed test passes without crashes, timeouts, or assertion failures;
- every skip is explained by an intentionally disabled large case or a documented optional corpus;
- OpenDB tests do not skip for a missing oracle when OpenDB was intended to be configured;
- an “all ISPD2018/2019” claim includes every required case and solution; skips do not count as passes;
- iRT routed differential passes wrapper snapshots, DEF writeback semantics, and final DRC geometry sets; passing only the first two is incomplete;
- the iRT failures in section 8.1 are reported separately and are not counted as passes merely because legacy iDB and EccDB both fail;
- the report records the EccDB commit, OpenROAD commit, environment variables, and pass/skip/fail counts.

Differentials cover only fields present in their snapshots and assertions. Whenever a modeled EccDB field is added, the corresponding snapshot/differ must also be extended.

## 12. Troubleshooting

- `configure the OpenDB Python module or set OPENDB_TCL`: verify `odb.py`, `_odb.so`, the Python ABI, and `OPENDB_PYTHONPATH`.
- OpenDB child exit code `127`: the configured interpreter or `odbtcl` could not be executed; this is not a semantic mismatch.
- `set OPENROAD_SOURCE_DIR`: neither the runtime variable nor compiled default identifies a valid checkout.
- ISPD root exists but a file assertion fails: check the two layouts in sections 4.3 and 4.4. This is a failure, not an automatic skip.
- iRT snapshots match but DRC shapes differ: preserve temporary output and compare VIA definitions, path expansion, orientation/offset, and special-net geometry per net in the two routed DEF files.
- A large case is killed for memory: run one case at a time and avoid concurrent wrapper processes. Internal iRT thread count and test-process concurrency are separate settings.

## 13. Adding a new corpus case

Record the following with every new case:

- source, version, and license;
- relative LEF, DEF, and additional library LEF paths;
- routed/unrouted status, scale, and minimum wire/path/via counts;
- selected oracle and compared semantic fields;
- any allowed and justified normalization;
- whether missing data should skip or fail;
- a small stable regression reproducer.

Add a reduced synthetic grammar test first, then add the real file to the corpus. The small test pinpoints a field; the corpus exposes composition, scale, and cross-object reference failures.
