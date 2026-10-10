# iEMIR static IR/EM inputs

iEMIR reads the loaded LEF/DEF database into its own power network, imports
instance power, builds and solves the network, and writes IR/EM reports. It does
not run iSTA or write its solved network back into the shared design database.

## Tcl interface

Load technology LEF, cell LEF and the design DEF before `init_emir`. The current
comparison driver also loads Liberty. The resistor network is extracted
internally from DEF power geometry, LEF via geometry, and the process technology
file:

```tcl
init_emir \
  -temp_directory_path ./emir_output \
  -instance_power_file_path ./instance_power.tsv \
  -pad_files {./design.ploc} \
  -technology_file_path ./process.tech \
  -temperature_c 25.0 \
  -em_violation_threshold_percent 100.0 \
  -thread_number 4
run_emir
destroy_emir
```

| Input | Contract |
| --- | --- |
| `-instance_power_file_path` | Required. TSV with the `# iEMIR_INSTANCE_POWER_V1` marker and the seven columns below. |
| `-pad_files` | List of pre-generated absolute PLOC files. Mutually exclusive with `-ploc_file_path` and enabled `-add_ploc_from_top_def`. |
| `-ploc_file_path` | Legacy single PLOC input. Mutually exclusive with `-pad_files` and enabled `-add_ploc_from_top_def`. |
| `-add_ploc_from_top_def` | Optional `0` or `1`, default `0`. Set to `1` to use top-level DEF P/G IO pins as sources. Mutually exclusive with either supply-file option. Connected physical pin geometry is required. |
| `-technology_file_path` | Required. Metal sheet resistance and temperature coefficients, via resistance-per-cut, and EM rules. |
| `-temperature_c` | Extraction temperature in Celsius; defaults to 25. Metal resistance uses `R(T)=R(Tnom)*(1+Coeff_RT1*dT+Coeff_RT2*dT^2)`. |
| `-em_limit_file_path` | Optional rule overrides. EM assessment needs applicable rules; missing rules are not evidence of passing EM. |
| `-em_violation_threshold_percent` | Report threshold; defaults to 100.0. |
| `-temp_directory_path` | Per-run output directory, recreated during initialization. Use a separate directory for each run. |
| `-thread_number` | OpenMP thread count. |

The power TSV header is:

```text
instance_name	voltage_v	internal_power_w	switching_power_w	leakage_power_w	total_power_w	average_current_a
```

Fields use volts, watts and amperes. The reader checks component sums and
`voltage_v * average_current_a == total_power_w` within its numerical tolerance;
instance names are mapped to DEF instances. The file is a producer-independent
instance-power table; iPW writes this format as `instance_power.tsv`.

Reference `.ir.full` and `.em.worst` files belong to the external comparison step;
iEMIR does not read them to solve voltages or currents. Its own reports are
written under `emir_reporter/`, with solver diagnostics under `ir_analyzer/`.

## Supply file inputs

Engineers provide technology/cell LEF, routed DEF, process resistance data,
instance power, and absolute supply locations in pre-generated PLOC files:

```tcl
tech_lef_init -path ./tech.lef
lef_init -path ./pads.lef
def_init -path ./design.def
init_emir -temp_directory_path ./emir_output \
  -instance_power_file_path ./instance_power.tsv \
  -technology_file_path ./process.tech \
  -pad_files {./power.ploc ./ground.ploc}
run_emir
destroy_emir
```

`-pad_files` is a Tcl list of absolute-coordinate PLOC files (use `[list $path1
$path2]` for paths stored in variables). It is mutually exclusive with the
single-file `-ploc_file_path` option. Keep inputs outside
`-temp_directory_path`, which is recreated during initialization.
PLOC files may contain blank lines, `#` comments and standalone `*PLOC` markers.
Source names must be unique across the entire input list.

The `.pad`/`.pcell` definition parser, automatic coordinate conversion and
`generate_ploc` command have been removed. Convert local pad/master definitions
in an external tool before analysis. iEMIR rejects `.pad`, `.pcell`, `*PAD` and
`*PCELL` inputs; it no longer writes `resolved_sources.ploc` or a generation
provenance CSV.

For block analysis using top-level DEF P/G IO pins, explicitly select them
instead of passing supply files:

```tcl
init_emir -temp_directory_path ./emir_output \
  -instance_power_file_path ./instance_power.tsv \
  -technology_file_path ./process.tech \
  -add_ploc_from_top_def 1
run_emir
destroy_emir
```

Python uses `add_ploc_from_top_def=True`. The default is off in both interfaces.
Existing scripts that relied on automatic DEF IO sources must add this option.
Without supply files or explicit DEF enablement, initialization fails before
clearing its output directory. File-selected runs do not acquire additional
ideal sources from DEF IO pins. Enabling DEF sources still requires connected
P/G IO pin shapes; it never makes arbitrary grid nodes ideal sources.

Python accepts the same absolute PLOC list after loading the physical database:

```python
ecc_py.init_emir(temp_directory_path="./emir_output",
                 instance_power_file_path="./instance_power.tsv",
                 technology_file_path="./process.tech",
                 pad_files=["./power.ploc", "./ground.ploc"])
ecc_py.run_emir()
ecc_py.destroy_emir()
```

The supported electrical model is an ideal DC source. Per-source voltage,
R/L/C fields and package-subcircuit sections are rejected.

## Supply contact input and validation

PLOC coordinates are absolute design coordinates in microns. The recommended
five-column input names the PG net explicitly:

```text
# name x_um y_um routing_layer net
bump17 10.0 9.8 MET1 VDD_A
bump18 10.0 11.2 MET1 VSS
```

An explicitly typed record can instead use `POWER` or `GROUND` in column five
and `net=NAME` in column six, for example `bump17 10.0 9.8 MET1 POWER net=VDD_A`.
Explicit net fields are independent of the source name. The net must exist in
the loaded PG design, and any stated type must agree with its design type.

Existing five-column `POWER`/`GROUND` records remain supported. Names of the
form `<net>_POWER_<id>` or `<net>_GROUND_<id>` retain their legacy net binding
when using `-ploc_file_path`. With `-pad_files`, source names are opaque and
never used to infer a voltage domain.
Otherwise, a type-only record is accepted only if exactly one design PG net
has that type. Multiple candidates require an explicit net, even if one net
appears geometrically closer. Every configured source is validated before
selecting nets to build, so unknown names cannot be silently skipped.

Source names must be unique. Coordinates must be finite and fit signed 32-bit
design units after rounding. Unsupported extra fields, including RLC values,
are rejected: this input currently models ideal supply contacts.

The existing graph contact rules are retained: a source must contact its net
on the specified layer, and a point contact must not turn nearby segment ends
into additional ideal sources. Connectivity is checked before solving.
Unselected PG nets retain the existing explicit-source selection behavior.

`graph_builder/source_connections.csv` records each accepted configured-source
contact: source name, net, layer, requested DBU coordinates, graph node ID,
attached DBU coordinates, displacement in DBU, contact kind (`wire`, `via_bottom`
or `via_top`) and the zero-based geometry index within that net's wire/via list.
Multiple rows may share a graph node when wires meet there; these rows do not
create extra electrical sources. The file is replaced on every build. A failed
build can leave partial diagnostic rows; only a successful build confirms all
configured contacts connected. Explicitly enabled DEF PG IO sources are outside
this PLOC audit; `ir_analyzer/solver_diagnostics.csv` reports their source count.

These checks implement the input contract and circuit boundary requirements
using the project's existing geometry operations. Contact validation tests use
synthetic circuits with hand-checked coordinates and resistances. No external
private implementation, binary layout or decompiled code is used.

## Module boundary

The public interface imports DEF SPECIALNET geometry. Graph construction splits
wires at crossings, vias, sources, and pin contacts, then calculates each wire
as `Rsheet(T) * length / width` and each via as `Rpercut / cut_count`. The solve
path uses the internally extracted resistance network.

The supported interface uses `-instance_power_file_path` and `-technology_file_path`;
there is no PG SPEF import option or generated-source compatibility switch.
It does not require a power SPEF in the shared SPEF reader. iSTA activity configuration is outside this
module's change set.

## Reusable IR solver

The default `-ir_solver auto` uses recursive geometric separator ordering and
sparse LDLT. Subnet interiors precede interface nodes, so elimination performs
the corresponding Schur-complement reduction without building dense inverse or
port matrices. Small subnets and unsuitable geometry use AMD ordering. This is
an adaptation of 解敏, *嵌套式层次化 P/G 网 IR-drop 分析方法的研究*, physical
pp. 39–41 (printed pp. 33–35), not a reproduction of every thesis algorithm.
The source and original page images are indexed in `../skill-books/` relative to
the repository root.

| Option | Default | Behavior |
| --- | --- | --- |
| `-ir_solver` | `auto` | `auto` currently selects `hierarchical`; `hierarchical` forces separator/LDLT, `iccg` selects incomplete-Cholesky CG, `sparse_lu` selects the reference solver. |
| `-ir_solver_tolerance` | `1e-10` | Relative true-residual tolerance; positive and at most `1e-8`, leaving margin for the existing IR/current-balance validation. |
| `-ir_solver_max_iterations` | `2000` | Positive ICCG iteration limit. Failure or inaccurate convergence falls back to hierarchical LDLT. |

ICCG follows the preconditioning approach discussed on thesis physical p. 51
(printed p. 45). It is explicit because local mesh measurements favored LDLT;
node count alone did not reliably predict a faster iterative solve. ICCG uses
the previous solution as an initial guess. Its operator receives a sufficient
grounded-conductance check; general SPD operators or uncertain rounding require
an LDLT positive-pivot check first, which can add preparation time and memory.
Fallback remains active for subsequent
right-hand sides until the operator or options change.

All backends solve `G * delta_v = I`, then recover
`V = V_source + delta_v`. This assumes the graph's existing common ideal-source
voltage contract and preserves source-node local loads. Diagnostics now measure
residual against load current, avoiding nominal supply terms that hide small
load errors. Original CSV columns remain in order; appended columns report the
backend, prepare/solve times, iterations, reuse, fallback, partition depth/count
and cumulative symbolic/numeric preparation and solve counts. Times exclude
network construction and matrix stamping.

`IRLinearSolver` owns its sparse matrix and separates `prepare(A, points, options)`
from `solve(rhs, optional_guess)`. The operator must be a fully stored, symmetric
positive-definite matrix. Nonfinite/asymmetric input and detected factorization
or residual failures are rejected. Acceptance uses the true residual:
`||A*x-b||_2 <= absolute_tolerance + relative_tolerance * ||b||_2`, with default
absolute tolerance `1e-14 A` for the static conductance equation.

An unchanged matrix reuses factors; changed values with the same pattern reuse
symbolic analysis. Changed pattern, geometry or options rebuild preparation.
Each net retains its solver across `run_emir` calls; `init_emir` and `destroy_emir`
clear the cache, and removed nets are pruned. Static analysis still stamps and
checks the matrix on each call. Future time-step callers should prepare once and
call `solve` directly for each RHS. One solver instance must not be called
concurrently; independent instances have separate state.

For later backward-Euler RC analysis, a caller can prepare `A = G + C/dt` and
supply `I(t) + (C/dt)*V_previous`, with consistent boundary/offset terms. Fixed
`dt` and components permit factor reuse; changing `dt` requires preparation
again. A numerical test exercises this effective-operator interface. This change
does not yet add capacitance/inductance extraction, current waveforms, time-step
control, or a dynamic report. General RLC/MNA operators can be indefinite or
nonsymmetric and are outside this SPD solver's contract.

Build and verify using the normal project configuration:

```sh
cmake --build build --target iemir_ir_solver_test iemir_ir_em_test iemir_ir_solver_benchmark
ctest --test-dir build -R '^iemir_ir_(solver|em)_test$' --output-on-failure
bin/iemir_ir_solver_benchmark 100 10
bin/iemir_ir_solver_benchmark 200 10
```

The benchmark compares a fresh SparseLU factorization for every RHS with each
prepared backend, including a reusable SparseLU control. It checks voltages and
residuals, and reports setup separately from solve time. These are solver-only
measurements; they do not predict full-flow or dynamic-analysis speedup.
Recorded local results and limitations: [validation report](../../../docs/ir-acceleration-validation.md).
