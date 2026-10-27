# Physical pattern extraction

The existing extraction remains the default. These optional modes adapt the frozen
2026-10-09 paper experiments to the current native topology API.

| Environment variable | Meaning |
| --- | --- |
| `IRCX_MULTI_MODE` | `off` (default), `prebuilt`, `collect`, `apply`, or `grid` |
| `IRCX_MULTI_RANK` | 1–3 neighbors on each side; `grid` requires 1 |
| `IRCX_PATTERN_MODEL` | Frozen fitted model for `prebuilt`; prepared without target DEFs |
| `IRCX_MULTI_TABLE` | Exact geometry coefficient TSV for `apply` |
| `IRCX_PATTERN_GRID` | Process-specific independent asymmetric grid for `grid` or `prebuilt` fallback |
| `IRCX_MULTI_OUTPUT` | Existing directory for requests and coverage statistics |
| `IRCX_PATTERN_CROSS=physical` | Optional corrected cross-layer selection in the legacy path; leave unset for baseline comparisons |

For the reusable three-neighbor library:

```sh
unset IRCX_MULTI_TABLE
export IRCX_MULTI_MODE=prebuilt
export IRCX_MULTI_RANK=3
export IRCX_PATTERN_MODEL=/absolute/path/to/frozen/model.txt
export IRCX_PATTERN_GRID=/absolute/path/to/independent/fallback.captab
export IRCX_MULTI_OUTPUT=/existing/run/geometry
```

`prebuilt` evaluates the frozen coefficient model for each local geometry. It does
not read a geometry-request list or an exact table, and never launches a field
solver. The library is prepared once from process-only sampling and field labels;
it can be reused for different target DEFs in that same process/corner. The C++
query evaluator includes normalization, etch transformation, absence masks, and
reflection averaging. It needs no Python or PyTorch at extraction time. Results
are returned by value so concurrent or subsequent queries cannot overwrite a live
coefficient vector. The caller must validate the model, fallback, ITF, layer map,
and process corner against the frozen preparation manifest before extraction.

Touching/overlapping, post-etch invalid, or out-of-domain sections in `prebuilt`
use the independent grid with canonical nearest sides and nominal dimensions,
including explicit overlap fallbacks. `prebuilt_stats.txt` reports supported,
geometry-fallback and domain-fallback queries and lengths. Explicit overlap pieces
bypass model inference, so overall fallback accounting must also use
`field_fallback_dbu` in `multi_stats.txt`; do not sum only the model counters.
`grid_dbu` and its width/gap-clamping counters audit the grid path. The grid omits
etch correction and retains only one neighbor per side; fallback is a different,
less expressive approximation. Unknown layers and malformed model files fail.

For the existing single-neighbor grid model:

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

Normal pattern queries select the nearest covered upper/lower layers. In the
historical `apply` and `grid` modes, ambiguous
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

Build `ecc_bin`, `ircx_pattern_test`, and `ircx_prebuilt_test`; run
`ctest -R 'ircx_(pattern|prebuilt_query|apply_query)_test' --output-on-failure`
from the build directory. The additional prebuilt fixture uses synthetic process
values, queries unseen widths without an exact table, checks independent result
lifetimes and explicit fallbacks, and retains strict missing-key errors in apply mode. The test covers spatial selection against an independent enumerator,
absent/asymmetric grid queries, canonical ownership, fallback sides, special nets,
reciprocal support integration, and malformed coefficient inputs.

The 2026-10-27 paper uses the same evaluator in a frozen archived extractor, whose
wire-end completion and cross projection differ from this native implementation.
Native unit/build validation is not a claim that this port reproduces all paper
accuracy numbers. Geometry and interval assembly were not changed by this port.
