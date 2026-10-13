# Physical pattern extraction

The existing extraction remains the default. These optional modes adapt the frozen
2026-10-09 paper experiments to the current native topology API.

| Environment variable | Meaning |
| --- | --- |
| `IRCX_MULTI_MODE` | `off` (default), `collect`, `apply`, or `grid` |
| `IRCX_MULTI_RANK` | 1–3 neighbors on each side; `grid` requires 1 |
| `IRCX_MULTI_TABLE` | Exact geometry coefficient TSV for `apply` |
| `IRCX_PATTERN_GRID` | Process-specific asymmetric Cartesian grid for `grid` |
| `IRCX_MULTI_OUTPUT` | Existing directory for requests and coverage statistics |
| `IRCX_PATTERN_CROSS=physical` | Optional corrected cross-layer selection in the legacy path; leave unset for baseline comparisons |

For the selected grid model:

```sh
export IRCX_MULTI_MODE=grid
export IRCX_MULTI_RANK=1
export IRCX_PATTERN_GRID=/absolute/path/to/technology.grid
export IRCX_MULTI_OUTPUT=/existing/run/geometry
```

The grid contains nominal geometry coefficients in fF/um. Explicit `-1` spacing
represents an absent neighbor; left and right spacing are independent. Queries
interpolate the sampled width and spacing axes and clamp outside them. The benchmark
grids use one nominal master width per layer and nominal LEF neighbor widths;
`grid_width_clamped_dbu` audits master-width clamping;
`grid_gap_clamped_dbu` audits spacing clamping. No empirical
capacitance scale is applied. Technology matching and table hashes are checked by
`experiments/pattern_port_20261011/native_pattern.sh` in the workspace root; direct
binary callers must independently ensure the grid matches their ITF and layer map.

Same-layer parallel shapes are indexed by physical boxes within 8 um. Longitudinal
supports are split where neighbor ranks change. Coincident same-owner geometry is
canonicalized. Ground coefficients are integrated once; special-net coupling is
folded into ground. Regular-net coupling is integrated over the union of directed
supports, averaging only where both directions observe the same edge pair.

Normal pattern queries select the nearest covered upper/lower layers. Ambiguous
overlap uses an explicit fallback with canonical nearest sides, nominal spacing and
the native 1D table. This fallback intentionally retains legacy cross-layer selection
and its native same-net ground convention; it is not continuously equivalent to the
grid model. `field_fallback_dbu` records its extent. Missing exact keys fail rather
than silently substituting coefficients. Pattern application currently supports one
process corner per extraction.

`collect` records exact rank geometry while retaining the original capacitance
output. `apply` uses separately characterized exact rank coefficients. Grid rank 1
and exact rank 3 are separate experiments. Landing completion and perpendicular
cross projection from the manuscript are not included in this port.

Build `ecc_bin` and `ircx_pattern_test`; run `ctest -R ircx_pattern_test` from the build
directory. The test covers spatial selection against an independent enumerator,
absent/asymmetric grid queries, canonical ownership, fallback sides, special nets,
reciprocal support integration, and malformed coefficient inputs.
