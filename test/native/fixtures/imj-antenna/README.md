# iMJ Antenna End-to-End Regression Controls

The M1 regression controls exercise the two production import paths, antenna
metadata transport, pin/wire graph construction, and antenna ratio evaluation.
They are deliberately not kernel-only tests.  The separate unsupported POLY
fixture records a parser boundary rather than a checker result.

The M1 manifests load `tech.lef` first and `gate.lef` second.  The technology
uses 1000 DBU per micron and defines an M1 `ANTENNAAREARATIO` of 2.  `GATE/PINA`
is an INPUT pin with `ANTENNAGATEAREA 1` and a 0.100 by 0.100 micron M1 port.
Each M1 DEF connects two placed `PINA` instances on one signal net, so the total
gate area is 2 square microns and every routed conductor overlaps a gate pin.

`pass.def` uses a 0.100 micron wire from x=0.400 to 2.600 microns.  Its routed
metal is approximately 0.23 square microns with the observed route end caps,
yielding a PAR near 0.115 against the two square micron gate area:
below the threshold of 2.

`violation.def` uses the same wire width and pin metadata, but its wire runs
from x=0.400 to 50.600 microns.  Its approximately 5.03 square microns of
metal yields a PAR near 2.515, above the threshold of 2.  Thus a complete run
should report one M1 PAR violation rather than merely completing the stage.

`clock-violation.def` is the same long-wire calculation, but `N_CLK` uses
`CLOCK` rather than `SIGNAL`.  The native EccDB provider treats clock nets as
ordinary signal nets for antenna checking, so it must report the same M1 PAR
violation as the long SIGNAL wire.  Upstream `AntennaChecker` intentionally
retains its `is_signal()`-only traversal, so the legacy iDB provider does not
check this net.  This is an intentional backend-specific regression control,
not a strict paired-equality case.

`patch-violation.def` keeps the short, otherwise passing signal wire and adds
`NEW M1 ( 400 500 ) RECT ( 0 0 2500 2000 )`.  DEF attaches that patch to the
preceding point, making an absolute 2.500 by 2.000 micron M1 rectangle that
overlaps the routed wire and both gate-connected conductor paths.  The patch
adds roughly 5 square microns of metal, for a PAR near 2.56, so a fixed provider
must report one M1 PAR violation with complete coverage.  The native EccDB
provider consumes the routed `RECT`; upstream `AntennaChecker` intentionally
retains its `is_wire()`-only path and does not consume it.  This control catches
a provider that imports wire segments but drops routed `RECT` patches, and is
also an intentional backend-specific regression control rather than a strict
paired-equality case.

Run the paired harness from the repository root with a fresh output path:

```bash
PROBE=build/upstream-integrate-20261013/bin/imj_pair_probe
RUN_ROOT=artifacts/imj-differential-antenna-$(date +%Y%m%d-%H%M%S)

python3 test/native/run_imj_pair_differential.py \
  --probe "$PROBE" \
  --manifest test/native/fixtures/imj-antenna/pass.manifest.json \
  --options test/native/fixtures/imj-antenna/options.json \
  --stage antenna \
  --out "$RUN_ROOT/control-antenna-pass"

python3 test/native/run_imj_pair_differential.py \
  --probe "$PROBE" \
  --manifest test/native/fixtures/imj-antenna/violation.manifest.json \
  --options test/native/fixtures/imj-antenna/options.json \
  --stage antenna \
  --out "$RUN_ROOT/control-antenna-violation"

python3 test/native/run_imj_pair_differential.py \
  --probe "$PROBE" \
  --manifest test/native/fixtures/imj-antenna/clock-violation.manifest.json \
  --options test/native/fixtures/imj-antenna/options.json \
  --stage antenna \
  --out "$RUN_ROOT/control-antenna-clock-violation"

python3 test/native/run_imj_pair_differential.py \
  --probe "$PROBE" \
  --manifest test/native/fixtures/imj-antenna/patch-violation.manifest.json \
  --options test/native/fixtures/imj-antenna/options.json \
  --stage antenna \
  --out "$RUN_ROOT/control-antenna-patch-violation"

```

## Current Regression Contract

Process success alone is insufficient. `pass.def` and `violation.def` are the
strict paired controls: both providers must complete coverage and have the same
complete numerical violation vector. The vectors retain raw names, so
escaped-name behavior is not hidden by normalization.

| Fixture | Legacy iDB expectation | Native EccDB expectation | Pair interpretation |
| --- | --- | --- | --- |
| `pass.def` | `[]` | `[]` | Strict equality required. |
| `violation.def` | `N_ANT:M1 PAR ratio=2.515 threshold=2 bbox=(0.35,0.45,50.65,0.55)` | Same vector | Strict equality required. |
| `clock-violation.def` | `[]`; coverage is incomplete because upstream ignores `CLOCK` | `N_CLK:M1 PAR ratio=2.515 threshold=2 bbox=(0.35,0.45,50.65,0.55)` | Intentional mismatch; retain it. |
| `patch-violation.def` | `[]`; coverage is incomplete because upstream ignores routed `RECT` | `N_PATCH:M1 PAR ratio=2.55875 threshold=2 bbox=(0.35,0.45,2.9,2.5)` | Intentional mismatch; retain it. |

The generic paired harness remains strict. After the upstream legacy behavior
is restored, its `result_equal` / `strict_raw_equal` result for the CLOCK and
routed-RECT controls must remain false; do not weaken the common comparison
acceptance rule to turn these known semantic differences into a pass. Record
their backend-specific expectations separately when running the fixtures.

## Historical Temporary Adaptation Result

The following result is retained only as historical evidence. During an earlier
adapter experiment, the legacy checker was temporarily extended to include
`CLOCK` nets and routed `RECT` segments. That non-upstream behavior produced
`coverage_complete=true`, `result_equal=true`, and `strict_raw_equal=true` for
all four controls. It is not the current regression contract and must not be
used to justify deleting the native EccDB CLOCK/RECT handling.

| Fixture | Historical complete violation vector on each provider |
| --- | --- |
| `pass.def` | `[]` |
| `violation.def` | `N_ANT:M1 PAR ratio=2.515 threshold=2 bbox=(0.35,0.45,50.65,0.55)` |
| `clock-violation.def` | `N_CLK:M1 PAR ratio=2.515 threshold=2 bbox=(0.35,0.45,50.65,0.55)` |
| `patch-violation.def` | `N_PATCH:M1 PAR ratio=2.55875 threshold=2 bbox=(0.35,0.45,2.9,2.5)` |

## Unsupported POLY LEF Negative Control

`unsupported-poly-violation.manifest.json` deliberately isolates a POLY layer
declared as `TYPE MASTERSLICE`, with `ANTENNAMODEL OXIDE1` and
`ANTENNAAREARATIO 2`.  `POLYGATE/PINA` supplies a real POLY rectangle from
`(0,0)` to `(3,1)` microns and `ANTENNAGATEAREA 1`; its one-terminal DEF net
has no illegal masterslice route.  The encoded geometry is 3 square microns
divided by gate area 1, or PAR 3 above threshold 2; this calculation documents
the input's intended physical data, not an antenna-check acceptance result.

This is intentionally a negative parser contract, not a fifth passing antenna
control.  SI2 rejects the LEF before antenna checking because it permits these
antenna statements only on `ROUTING` or `CUT` layers.  The expected diagnostics
are `LEFPARS-1557` for `ANTENNAMODEL` and `LEFPARS-1533` for
`ANTENNAAREARATIO`; therefore no coverage, result vector, ratio, or bbox is
valid for this manifest.  The recorded fresh run in
`control-antenna-poly-final` observed EccDB return code 1 with both diagnostics
and iDB return code -11 after printing the same two diagnostics.  That artifact
is retained as evidence of the unsupported file-level input boundary.
