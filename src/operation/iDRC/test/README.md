# iDRC minimal case tests

These tests run the production `RuleValidator::verify()` on small hand-written
cases. They need no LEF, DEF or idb. Each case is a text file of about ten
lines: the technology the rule validator reads, the shapes it checks, and the
exact set of violations it must report.

The tests enter through `DataManager::inputDatabase()`. That method runs the same
steps as `DataManager::input()`, except that it takes a prepared `Database`
instead of wrapping one from idb, so the layer checks and derived data
(`buildDatabase()`) are shared with production. The rule code is unchanged.

The tests do not cover:

- `DRCInterface::getViolationList()`, including the `ids::Shape` conversion and
  the macro-obstruction check;
- the idb wrapping in `DataManager::wrap*()`;
- the report writers.

## Running

```
cmake -S . -B build -G Ninja -DBUILD_TESTING=ON
ninja -C build idrc_case_runner
ctest --test-dir build -R '^idrc_case_' --output-on-failure
./bin/idrc_case_runner src/operation/iDRC/test/cases/<case>.drc
```

`idrc_case_runner --print <case>.drc` prints the observed violations in
`expect` syntax. Use it for diagnosis only: expectations must come from the rule
(see "Writing cases"), otherwise a case turns back into a golden file.

## Case format (`*.drc`)

The format has one statement per line. `#` starts a comment, and tokens are
separated by whitespace. Coordinates and sizes are integer DBU. Syntax errors are
reported as `<file>:<line>: <message>`.

| Statement | Meaning |
|---|---|
| `case <name>` | Label (optional). |
| `dbu <int>` | Micron DBU (default 1000). |
| `die <llx> <lly> <urx> <ury>` | Required. |
| `routing <name> dir=<H\|V> pitch=<int> [spacing=<int>] [min_width=<int>] [min_area=<int>]` | Routing layer. `dir` and `pitch` are required by `DataManager::checkLayerList()`. |
| `cut <name>` | Cut layer (no cut rules yet). At least one is required. |
| `rule <metal_short\|out_of_die>...` | Enables parameterless rules. |
| `result <net> <layer> <llx> <lly> <urx> <ury>` | Shape under check (`drc_result_shape_list`, `kRegularWire`). |
| `env <net\|obs> <layer> <llx> <lly> <urx> <ury>` | Fixed shape (`drc_env_shape_list`): a named net is a pin (`kInstancePin`); `obs` is an obstruction (net -1, `kInstanceObs`). |
| `check <type>...` | Restricts the run to these violation types. The default is every enabled rule. |
| `expect <type> <layer> <llx> <lly> <urx> <ury> nets=<net>[,<net>] required=<int>` | One expected violation. |
| `expect none` | The case must produce no violation. A case without any `expect` is rejected. |
| `vary pitch=<int>` | Extra run with every routing pitch set to this value (see "Oracles"). |

Other details:

- Layers are declared bottom to top. Each routing or cut layer's index is its
  position among the layers of the same kind. Production assumes the same mapping
  when it passes idb layer ids as indices.
- A rule runs only if the case declares it:
  - `spacing=` enables `parallel_run_length_spacing`, as a default `SPACING`
    entry;
  - `min_width=` enables `minimum_width`;
  - `min_area=` enables `minimum_area`;
  - `rule` enables `metal_short` and `out_of_die`.

  Unlike `DataManager::wrapRoutingDesignRule()`, nothing is enabled implicitly,
  so a case never fails on a rule it does not mention.
- Violations are compared as canonical lines, in the same syntax as `expect`
  without the keyword. Net -1 prints as `obs`.

## Oracles

Each case is run several times, each run with a fresh `DataManager` and
`RuleValidator`:

1. **Expectation:** the sorted multiset of reported violations must equal the
   expected one. Missing and unexpected violations are both reported.
2. **Thread count:** a run with `-thread_number 4` must equal the
   single-threaded run.
3. **Shape order:** a run with both shape lists reversed must equal the
   baseline.
4. **Cluster partition** (only with `vary pitch=`): clusters are `100 * pitch`
   wide with a `5 * pitch` margin (`RuleValidator::setRVComParam()`). Pitch is not
   read by any rule covered here, so changing it must not change the result. Pick
   a value whose margin still covers the largest enabled spacing.

## Writing cases

- Derive every expectation from the rule and write the arithmetic in a comment.
- Give every positive case a legal twin at the boundary, for example a gap of
  exactly the required spacing.
- Test one rule per case, and use `check` to keep other enabled rules out.

## Known failing cases

These four cases fail on `d4b6f0a`. They show two defects in how
`RuleValidator` partitions the layout into clusters.

| Case | Failure |
|---|---|
| `metal_short_on_cluster_boundary` | The zero-area short between two abutting nets is dropped, because `processRVCluster()` keeps a violation only if it open-overlaps a cluster. A zero-width rectangle on a boundary open-overlaps neither neighbour. `metal_short_on_cluster_boundary_control` differs only by not moving the cluster origin, and passes. |
| `metal_short_abutting` | Same defect, triggered through `vary pitch=10`. |
| `minimum_area_split_polygon` | A cluster receives only some rectangles of a legal net polygon, measures the fragment and reports `minimum_area`. `minimum_area_split_polygon_control` lets the cluster see the whole polygon, and passes. |
| `minimum_area_cluster_split` | Same defect, triggered through `vary pitch=20`. |
