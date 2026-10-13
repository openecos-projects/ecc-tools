# Native EccDB toolchain

`ecc_native_flow` and `ecc_py.NativeToolchain` keep one EccDB owner and Context
alive across stages. The algorithms retain their private work models. Native
stages do not recreate the physical design in iDB. Existing iDB entry points
remain available separately for reference tests.

## Build and run

```sh
cmake -S . -B /path/to/native-build -G Ninja \
  -DCMAKE_CXX_COMPILER=g++-13 -DCMAKE_C_COMPILER=gcc-13 \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON \
  -DECCDB_BUILD_TESTS=ON -DECCDB_BUILD_BENCHMARKS=OFF -DCMD_BUILD=ON
cmake --build /path/to/native-build --target ecc_native_flow ecc_py -j16

export ECC_TOOLS_TEST_REPO_ROOT=/path/to/ecc-tools
export ECC_TOOLS_TEST_PDK_ROOT=/path/to/icsprout55-pdk
export ECC_TOOLS_TEST_ARTIFACT_ROOT=/path/to/artifacts
python3 test/native/toolchain_scenarios.py \
  --binary /path/to/native-build/bin/ecc_native_flow --stages cts routing drc
```

The public `openecos-projects/icsprout55-pdk` repository supplies the LEF and
Liberty files used by the GCD fixture. Extract its H7CL/H7CR Liberty archives
before running; fixture preparation rejects missing inputs.

To register the same real chain with CTest, configure with
`-DECC_TOOLS_NATIVE_INTEGRATION_TESTS=ON` and
`-DECC_TOOLS_NATIVE_PDK_ROOT=/path/to/icsprout55-pdk`. Then run
`ctest --test-dir /path/to/native-build -R '^native_gcd_cts_routing_drc$' --output-on-failure`.

## Manifest

```json
{
  "backend": "eccdb",
  "lef_files": ["tech.lef", "cells.lef"],
  "def_file": "placed.def",
  "liberty_files": ["typical.lib"],
  "sdc_file": "design.sdc",
  "output_directory": "native-output",
  "binary_checkpoints": true,
  "stages": [
    {"name": "cts", "config": "cts.json"},
    {"name": "routing", "config": "route.json", "options": {"-thread_number": 2}},
    {"name": "drc", "config": "drc.json"},
    {"name": "rcx", "config": "extraction.json"},
    {"name": "sta", "options": {"-output_timing_reports": 1}}
  ]
}
```

Relative input paths resolve against the manifest directory. Tool config
contents retain each tool's existing format. Optional `reference_verilog` and
`top` supply the LVS reference. The reference-side Verilog parser is independent
of the EccDB physical design; it is not a native RTL import frontend.

RCX needs actual compatible layer mapping, ITF and capacitance tables. The old
ICS55 SDK config is not interchangeable with this native extractor config.
Successful native RCX reports its exact SPEF output paths and loads the first
corner into the flow for subsequent STA. A later design mutation invalidates
that SPEF; STA rejects it until extraction is repeated. A stage may explicitly
load an external `spef_file` for an independent timing check.

## Debug artifacts

Each numbered stage directory contains `result.json`, `output.def`, and optional
`technology.edb`, `library.edb`, `design.edb`. The overall `result.json` records
the native backend, before/after object counts, revisions and phase timings.
Stage reports also record resident and peak process memory in KiB. These include
the parser, loaded timing libraries, and the tool's transient work model.
On failure it records the failing stage and error. Failed algorithm runs do not
use their normal writeback teardown; the failed flow must be restarted.
Writeback across multiple business services is not a global transaction. If
writeback itself fails, reload the last successful checkpoint rather than
continuing with a potentially partial design.

The Python API exposes the same runner plus `summary()`,
`inspect_instance(name)` and `inspect_net(name)` for inspecting native IDs,
names and connectivity without learning the private algorithm databases.

Per-stage `status: "passed"` means the stage executed and completed its
teardown. For DRC, use `drc_clean` or `drc_status` (`"clean"` or
`"violations"`) to distinguish a clean signoff from a completed check that
reported rule violations.

To resume debugging from an exported checkpoint, replace `lef_files`/`def_file`
in the manifest with `"binary_directory": "native-output/2-routing"`, or call
`ecc_py.NativeToolchain(binary_directory)`. Timing libraries, SDC and SPEF remain
external inputs and must be provided again. Verilog reference parsing currently
requires the original LEF/DEF constructor.

Useful native debugger boundaries are `ecc::NativeToolchain::run`,
`eccdb::adapter::icts::CtsEccdbAdapter::writeClocks`,
`irt::RTInterface::outputNetListToEnTT`, and the corresponding business Service.
Build with debug information, start with a small fixture, and inspect a named
Net/Instance before and after the relevant service write. Algorithm-local IDs
and EccDB entity IDs are different domains.

This is an integration entry point, not a claim that every design has passed
every algorithm or that the current project has a native RTL-to-GDS flow.
