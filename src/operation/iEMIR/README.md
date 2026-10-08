# iEMIR static IR/EM inputs

iEMIR reads the loaded LEF/DEF database into its own power network, imports
instance power, builds and solves the network, and writes IR/EM reports. It does
not run iSTA or write its solved network back into the shared design database.

## Tcl interface

Load technology LEF, cell LEF and the design DEF before `init_emir`. The current
comparison driver also loads Liberty. The resistor network is extracted
internally from DEF power geometry, LEF via geometry, and the RedHawk technology
file:

```tcl
init_emir \
  -temp_directory_path ./emir_output \
  -ptpx_instance_power_file_path ./design.ptpx.tsv \
  -ploc_file_path ./design.ploc \
  -redhawk_tech_file_path ./process.tech \
  -temperature_c 25.0 \
  -em_violation_threshold_percent 100.0 \
  -thread_number 4
run_emir
destroy_emir
```

| Input | Contract |
| --- | --- |
| `-ptpx_instance_power_file_path` | Required. TSV with the `# iEMIR_PTPX_INSTANCE_POWER_V1` marker and the seven columns below. |
| `-ploc_file_path` | Explicit supply contacts. Without a PLOC file, the graph must have connected DEF PG IO sources. A graph without a source fails; top-layer nodes are never automatically made ideal sources. |
| `-redhawk_tech_file_path` | Required. RedHawk metal sheet resistance and temperature coefficients, via resistance-per-cut, and EM rules. |
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
instance names are mapped to DEF instances. This is the normalized PTPX export,
not a raw commercial report or an iSTA binary power file.

Golden `.ir.full` and `.em.worst` files belong to the external comparison step;
iEMIR does not read them to solve voltages or currents. Its own reports are
written under `emir_reporter/`, with solver diagnostics under `ir_analyzer/`.

## Module boundary

The public interface imports DEF SPECIALNET geometry. Graph construction splits
wires at crossings, vias, sources, and pin contacts, then calculates each wire
as `Rsheet(T) * length / width` and each via as `Rpercut / cut_count`. No external
RedHawk `.res_network` is read by the solve path.

The supported interface has no `-instance_power_file_path` alias, PG SPEF import
option or generated-source compatibility switch. It does not require a power
SPEF in the shared SPEF reader. iSTA activity configuration is outside this
module's change set.

PLOC/DEF supply-location background: `RedHawk_User_Manual_11.1.pdf`, PDF
pp. 604–605. The interface and module-boundary decisions above describe this
implementation; they are not requirements imposed by that manual.

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
nonsymmetric and are outside this SPD solver's contract. Dynamic-flow context:
`RedHawk_User_Manual_11.1.pdf`, PDF p. 46.

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
