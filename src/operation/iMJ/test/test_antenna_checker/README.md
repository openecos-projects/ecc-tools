# Antenna Checking Regression

`check_antenna` reads the current IDB design and LEF antenna rules. It reports to
`<iMJ temporary directory>/antenna_checker/antenna_check.rpt`; it does not route,
repair, insert diodes, or require iRT initialization.

The checker retains the existing layer-event connectivity and PAR/CAR/PSR/CSR
calculations. Reported gate/diffusion areas describe the connected component;
metal/side/cut areas describe its geometry at the reported layer. The gate pin is
one representative gate, not a list of all affected gates. A diffusion-only
output is not reported as a gate.

```sh
cmake --build build --target ecc_bin test_imj_antenna_checker test_imj_antenna_reporting
OMP_THREAD_LIMIT=4 ctest --test-dir build -R '^test_imj_antenna_' --output-on-failure
```

The synthetic fixture needs no external PDK. It covers metal/cut area ratios,
cumulative ratios, sidewall areas, overlapping via enclosures, layer-dependent
gate connectivity, disconnected gates, diffusion PWL thresholds, threshold
equality, PG exclusion and missing gate annotations. The C++ and Tcl integration
tests compare DEF output before and after checking and exercise the existing
`init_mj`, `check_antenna`, `destroy_mj` lifecycle without routing.
