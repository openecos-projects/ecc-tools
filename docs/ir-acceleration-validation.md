# IR acceleration validation — 2026-10-06

Implementation base: `a14ba6fb54de0de26ff441ee661ea2082cae614e` on `emir_main`.
GCC 10 Release, Eigen3, pinned Boost 1.91.0; AMD EPYC 9654 shared host.
Benchmarks set one Eigen thread; real-design runs use `-thread_number 1`.
Numbers are individual local measurements, not stable cross-machine performance guarantees.

## Numerical and integration checks

- Built `ecc_bin`, `iemir_ir_solver_test`, `iemir_ir_em_test`, and `iemir_ir_solver_benchmark`.
- Both relevant CTests passed. Coverage includes all backends, a known tiny-current ladder, changed RHS/matrix values/pattern/coordinates/dimensions, zero RHS, warm starts, forced fallback, grounded components, coincident geometry, invalid matrices and an effective RC operator.
- Independent review tested 270 irregular systems and numeric updates against SparseLU. Its floating-network and loose-tolerance findings were fixed and independently rechecked.
- Tcl `-ir_solver_tolerance 0.001` now fails at configuration with an explanatory error.
- All three real designs ran in default, SparseLU and ICCG modes. All reported nodes and edges were matched against the saved pre-change executable, including fixed physical parameters.

| Design | Nodes, VDD + VSS | Edges | Max voltage difference | Max branch-current difference |
| --- | ---: | ---: | ---: | ---: |
| gcd | 2,139 | 2,299 | 0 V | 1e-11 A |
| picorv32a | 72,896 | 81,896 | 1e-09 V | 1e-10 A |
| salsa20 | 124,834 | 140,376 | 1e-09 V | 1e-10 A |

Voltage reports round to 1 nV and current reports to seven significant digits; this comparison does not establish bitwise equality. The maximum EM-ratio difference was approximately `1e-6` percentage points. Default-backend load-normalized residuals were at most `2.30e-11`. ICCG used direct fallback on both salsa20 nets and produced matching reports.

## Solver-only mesh benchmark

Each run changes the load vector across ten solves. The baseline performs a fresh SparseLU factorization for each RHS. Prepared SparseLU separates the benefit of reuse from ordering/backend changes. Timings include preparation plus solves, and exclude graph construction and matrix stamping.

| Nodes | RHS count | Rebuilt SparseLU | Prepared SparseLU | Hierarchical LDLT | Hierarchical speedup vs rebuild |
| --- | ---: | ---: | ---: | ---: | ---: |
| 10,000 | 10 | 0.2566 s | 0.0376 s | 0.0242 s | 10.60× |
| 40,000 | 10 | 1.3891 s | 0.1918 s | 0.1285 s | 10.81× |

For 40,000 nodes and one RHS, hierarchical LDLT took 0.1036 s versus 0.1393 s for rebuilt SparseLU (1.34×). Mesh voltages differed by at most `3.97e-14 V`. Explicit ICCG was slower than hierarchical LDLT on these measurements, which is why `auto` does not choose ICCG by node count.

## Real-design cold and repeated solves

Cold setup is not universally faster. The same current-offset implementation with forced SparseLU provides a comparable timed control; the old executable has no subsecond solver timer. Reported times sum VDD and VSS preparation/solve only.

| Design | Cold hierarchical | Cold SparseLU control | Control / hierarchical |
| --- | ---: | ---: | ---: |
| gcd | 0.001476 s | 0.001905 s | 1.29× |
| picorv32a | 0.105050 s | 0.088318 s | 0.84× |
| salsa20 | 0.158012 s | 0.174969 s | 1.11× |

A separate repeated-run experiment used the same network and loads in two consecutive `run_emir` calls. Every net retained one numeric preparation while its solve count advanced to two. This verifies cache lifetime; changing RHS without refactorization is independently covered by the mesh and integration tests.

| Design | First preparation + solve | Repeated prepare-check + solve | First / repeated |
| --- | ---: | ---: | ---: |
| gcd | 0.001554 s | 0.000175 s | 8.87× |
| picorv32a | 0.084161 s | 0.004614 s | 18.24× |
| salsa20 | 0.157634 s | 0.007937 s | 19.86× |

These ratios do not describe whole-flow speedups: `run_emir` still rebuilds graphs, stamps matrices and generates reports. Future time-step callers can prepare once and call the numerical solver directly. The cold picorv32a result and differences between the two experiments also illustrate shared-host timing variability.

## Reproduction and artifacts

```sh
cmake --build build --target ecc_bin iemir_ir_solver_test iemir_ir_em_test iemir_ir_solver_benchmark
ctest --test-dir build -R "^iemir_ir_(solver|em)_test$" --output-on-failure
bin/iemir_ir_solver_benchmark 100 10
bin/iemir_ir_solver_benchmark 200 10
bin/iemir_ir_solver_benchmark 200 1
```

Workspace artifacts: `../diagnostics/ir_acceleration_20261006/` relative to this repository root.
This contains build/CTest logs, benchmark CSVs, original/new report trees, `comparison.json`, `repeat_comparison.json`, executable hashes, the comparison script, and local Tcl wrappers for backend/repeated-run/invalid-tolerance checks.
The driver `/nfs/share/home/lark/iemir/run.sh` was used with local `iemir` action, `LARK_PUSH=0`, and no commercial-tool invocation or publication.

## Dynamic-analysis boundary

The completed work supplies an owned SPD operator, reusable preparation, optional initial guesses and true-residual validation. The test for `G+C/dt` checks an interface needed by a future backward-Euler RC solver. Waveforms, capacitor/inductor extraction, time-step control, transient reports and general RLC/MNA solving remain separate work. See the module README for the thesis page references and API contract.
