# PatternGrid capacitance extraction

## Input and module ownership

Each process corner's `captab_file` is a P grid. `DataManager` loads it once
and shares the immutable grid across that corner's temperatures. Extraction
uses the corresponding corner's grid without mode environment variables.
A/B tables and the multi-neighbor prebuilt calculation have been removed.
Invalid, empty and mixed-format tables fail during initialization.

- `source/cap_table/grid/PatternGrid.hpp` owns parsing, validation and width/gap
  interpolation, exported through the `ircx_cap_table` CMake target.
- `GridNeighborhood.hpp` selects the nearest physical neighbor on each side
  within 8 um and assigns duplicate metal to one owner. `GridSides.hpp` computes
  the nominal drawn gaps for lookup.
- `CapExtractor` selects the corner grid, queries coefficients and assembles
  ground and reciprocal coupling capacitance.

A section starts with `P master below above width`. All three layer fields use
LEF/DEF design names (the mapping file's left column), with `SUBSTRATE` for an
absent lower plane and `-` for an absent upper plane. ITF names remain in the
mapping file's right column and are resolved separately for process evaluation.
Widths and gaps use micrometers; coefficients use fF/um. The rows contain
`left_gap right_gap ground left right`. Gap `-1` means no neighbor on that side,
whose coupling must be zero. Each section must contain a complete Cartesian
grid with absent and present samples on both gap axes. Queries interpolate the
two gaps and width; out-of-range positive coordinates clamp to the sampled
range. Missing layer contexts fail explicitly.

Rows may append `ground_low ground_high`; both must be nonnegative and sum to
`ground`. These are directional portions of the aggregate ground coefficient.
Without them, each portion defaults to half of `ground`. V2 files are rejected.

For example:

```text
P M1 SUBSTRATE - 0.1
-1 -1 0.08 0 0
-1 0.1 0.05 0 0.03
0.1 -1 0.05 0.03 0
0.1 0.1 0.02 0.03 0.03
```

`IRCX_PATTERN_GRID=/absolute/path/to/grid.captab` can override the configured
grid for an extraction with exactly one corner. `IRCX_MULTI_MODE=grid` and
`IRCX_MULTI_RANK=1` are unnecessary but accepted for existing grid invocations;
other values, `IRCX_PATTERN_TABLE` and `IRCX_XLAYER_TABLE` fail instead of
selecting another method.
Set `IRCX_MULTI_OUTPUT` to an existing directory to write `grid_stats.txt`,
including queried, duplicate, overlapping and clamped lengths.

All coefficients must come from process data and predetermined synthetic
geometry samples. The same grid is reusable across designs. No target layout
or reference extraction is used to construct or tune the table, and no field
solver runs during extraction.

Build `ircx_interface_test`, `ircx_pattern_test`,
`ircx_grid_layer_names_test`, and `ircx_resistance_geometry_test`. Run:

```sh
ctest --test-dir build -R '^ircx_' --output-on-failure
```
