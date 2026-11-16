# Process-based pattern capacitance tables

The reusable library is prepared from the process stack, dielectric and etch
parameters, nominal widths, and predetermined synthetic sections. Preparation
has no target-layout input. Extraction reads the routed layout and queries the
same library for every design in a process corner.

```sh
export IRCX_MULTI_MODE=prebuilt
export IRCX_MULTI_RANK=3
export IRCX_PATTERN_TABLE=/absolute/path/to/patterns.captab
export IRCX_PATTERN_GRID=/absolute/path/to/lef_grid192.captab
export IRCX_MULTI_OUTPUT=/existing/output/directory
```

`PatternCapacitanceTable.hpp` stores field-solver coefficients. A query retains up
to three neighbors on each side, including their individual widths and gaps,
and its upper/lower shielding context. Within the same context and neighbor
counts, it selects 64 nearby samples and computes a local affine interpolation
of log capacitances. The fixed ridge is 1e-4; results are bounded by the local
coefficient range. An existing sample is returned directly. Reflection uses a
canonical orientation. No coefficient weights are trained or stored.

The process etch transformation is applied consistently to sample and query
coordinates. Overlap, invalid post-etch geometry, missing context, and geometry
outside the declared range use the independent nearest-neighbor grid. This
fallback uses nominal dimensions and has only one neighbor per side; its limits
must not be represented as full three-neighbor characterization.
`prebuilt_stats.txt` and `multi_stats.txt` report supported and fallback lengths.
The table and grid must match the ITF, layer mapping and process corner.

`IRCX_MULTI_MODE=grid` with rank 1 selects the independent grid alone. Unset or
`off` retains the native extraction path. Target-conditioned table modes are
rejected. No field solver runs during extraction and no geometry-request file
is produced.

The resistance path applies the process horizontal scale to lengths, raw widths
and finite gaps before querying physical etch values. The shared capacitance
profile and thickness remain unchanged. Invalid scales fail before parallel
processing. Wire endpoints retain the routing topology's original support.

Build `ecc_bin`, `ircx_pattern_test`, `ircx_capacitance_table_test`, and
`ircx_resistance_geometry_test`. Run:

```sh
ctest --test-dir build -R 'ircx_(pattern|capacitance_table|resistance_geometry)' --output-on-failure
```

The native port and the paper extraction source use the same capacitance table
and interpolation implementation. Their geometry/interval assembly differs, so
the paper's measured results belong to the source and binary identified in
`/nfs/share/home/xuyipei/paper/overleaf-online/.build/table_library_20261028/`.
