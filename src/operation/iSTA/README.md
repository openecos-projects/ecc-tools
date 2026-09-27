# iSTA

`iSTA` follows the operation-tool layout used by hand-written `iRT`.

Current skeleton:

```text
src/operation/iSTA/
  interface/
  source/
    data_manager/
    module/
    toolkit/
      logger/
      monitor/
      utility/
  test/
```

Keep command and external API boundaries in `interface/`. Keep shared iSTA
state in `source/data_manager/`. Add concrete algorithm stages under
`source/module/<stage>/` only when the stage design is known.

## Minimum slew during RC propagation

`init_sta -min_slew_degradation 1` (the default) propagates the calculated
receiver waveform slew. The same integer option is accepted by the C++ config
map and the Python dictionary/JSON bindings. Values other than `0` or `1` are
rejected. As with other Python configuration fields, an empty dictionary/JSON
string leaves the option unspecified. Configure it before starting a run;
changing the internal config after calculation requires reinitializing the
delay calculator and its caches.

`-min_slew_degradation 0` enables an experimental early-mode approximation for
cell-driven RC trees: when the moment-reduced pi resistance exceeds `0.45` times
the Arnoldi driver resistance, a receiver with matching slew thresholds and
slew derate uses the driver slew. Receivers with different slew conventions
retain their calculated waveform, including the existing library conversion.
The compatibility check applies to each load and transition; equivalent
fractional and percentage thresholds are accepted across library names.
The resistance decision uses the analysis corner and output transition.
RC wire delay and its threshold correction still use the calculated waveform.
Maximum analysis, input-port waveforms, resistance loops, and the DMP fallback
retain their previous behavior.

This approximation was evaluated against controlled PrimeTime minimum-slew
runs. Restricting it to compatible slew conventions avoids applying the
approximation to unvalidated library conversions, which previously worsened
some SRAM receiver delays. This is a conservative scope limit, not a
requirement of RC physics. The fitted Arnoldi resistance is a heuristic,
not an exact physical resistance derived from the delay/load slope.
Neither the ratio nor this scope limit should be treated as PrimeTime's
complete algorithm. Some paths improve and others regress, so the option
remains experimental. It does not resolve all driver-model or SDF coverage
differences.
