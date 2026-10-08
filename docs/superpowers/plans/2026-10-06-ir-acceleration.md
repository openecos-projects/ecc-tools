# IR Acceleration Implementation Plan

> **For agentic workers:** Execute inline with executing-plans and test-driven-development. Use verification-before-completion and one independent requesting-code-review pass.

**Goal:** Accelerate iEMIR's existing static IR solve using the indexed thesis, with reusable matrix preparation for later time-dependent solves.

**Architecture:** Add an owned, reusable sparse SPD solver. A recursive geometric separator ordering eliminates local interiors before interface nodes; sparse LDLT performs the corresponding Schur elimination without materializing dense port matrices. Add incomplete-Cholesky preconditioned CG for large networks, true-residual validation, warm starts and a direct fallback. Integrate by solving voltage offsets from the common source potential and caching per-net solver state.

**Tech Stack:** C++20, installed Eigen3, existing CMake/CTest, local thesis knowledge base.

**Spec:** User request: implement the thesis's acceleration methods in iEMIR IR calculation in preparation for future dynamic voltage drop.

## Global Constraints

- Preserve source/load mapping, source-node loads, edge resistances and IR/EM report semantics.
- Retain a SparseLU reference mode; add explicit auto/hierarchical/iccg modes.
- All solver state owns its matrix; matrix-value changes invalidate numeric factors, pattern/coordinate/options changes invalidate ordering when applicable.
- Repeated right-hand sides reuse preparation; same-pattern value changes may reuse symbolic analysis. Future callers can prepare G+C/dt, but this change does not claim to implement RLC transient analysis.
- Reject nonfinite, nonsquare, asymmetric or unusable systems and verify the true residual before publishing voltages.
- No commercial-tool execution or external publishing. Benchmark locally against SparseLU and existing results.

## References

- `嵌套式层次化P_G网IR-drop分析方法的研究_解敏.caj`, physical pp. 39–41 (port elimination / hierarchy), p. 51 (ICCG). Original page images inspected.
- `RedHawk_User_Manual_11.1.pdf`, PDF p. 46: dynamic-flow input boundary; no claim that accelerating a resistor solve supplies missing dynamic RLC/current models.
- Eigen official sparse-solver, SimplicialLDLT and IncompleteCholesky documentation; installed Eigen headers determine API behavior.

## Review Focus

- Tiny loads must not disappear into the nominal supply-voltage residual scale.
- Repeated loads and matrix updates must never use stale factors or stale node permutations.
- Iterative nonconvergence must be visible and fall back to a residual-checked direct solution.
- Degenerate geometry and long cross-partition wires must not create invalid separators or dense explicit Schur storage.
- Warm starts, zero RHS, changed dimensions, multiple grounded components and invalid matrices need explicit tests.

## Tasks

- [x] Add standalone numerical tests comparing all backends with SparseLU, including a known resistor ladder, mesh, tiny loads, repeated RHS, value/pattern changes, zero RHS, invalid inputs and forced iterative fallback. Run RED before implementing.
- [x] Implement `IRLinearSolver.hpp/.cpp`, geometric separator ordering and lifecycle/statistics. Benchmark preparation and repeated solves on reproducible meshes.
- [x] Integrate per-net solver reuse, offset-voltage RHS, configuration (`-ir_solver`, tolerance/iteration controls) and appended CSV diagnostics. Extend the existing IR/EM integration test for repeated loads and matrix changes.
- [x] Build with the existing GCC 10 Release toolchain; run numerical tests, IR/EM tests, and relevant local real-design runs. Compare voltages/currents and record measured speedups with scope and hardware limits.
- [x] Update module README with citation, configuration, lifecycle and dynamic-readiness limits. Run independent review, resolve material findings, and finish verification.

## Execution Ledger

- Baseline: clean `emir_main` working tree at `a14ba6fb54de0de26ff441ee661ea2082cae614e`; existing IR/EM test passed.
- Ruling: work in the user's existing dedicated checkout and retain its build tree/toolchain. No plan approval pause: user authorized implementation.
- Ruling: use ICCG for the symmetric resistor operator rather than copying the thesis's BCG benchmark conclusion; backend choice will be checked on local matrices.

- RED: standalone test initially could not include the not-yet-created solver; integration test failed on the absent `Config::ir_solver`. Both passed after implementation.
- Build: GCC 10 Release, Eigen3 and existing local Boost 1.91.0. Initial CMake regeneration required pointing `Boost_DIR` at the installed pinned version; no dependency requirement was changed.
- Backend ruling: measured meshes and real designs do not favor ICCG by node count. `auto` uses hierarchical LDLT; explicit ICCG remains available and residual-checked.
- Review: independent requesting-code-review found floating-network acceptance in ICCG and a tolerance/guard conflict. Added RED regression, grounded-conductance certification or positive-pivot LDLT validation, and an integration tolerance ceiling of 1e-8. Reviewer independently retested both and 270 irregular systems; no remaining correctness blockers.
- Review scope rulings: full-design performance and thesis fidelity were checked by the primary implementation/validation work. Full transient RLC is explicitly deferred. Nonsingular indefinite SparseLU input remains outside this API's documented SPD contract.
- Validation: complete `ecc_bin` plus numerical/integration/benchmark targets built; 2 relevant CTests passed. Three real designs passed all-node/all-edge report comparison for default, SparseLU and ICCG modes. Repeated `run_emir` verified factor reuse; invalid Tcl tolerance was rejected early.
- Measurement scope: solver-only speedups distinguish cold preparation and repeated RHS. Cold real-design speedup is not universal; see `docs/ir-acceleration-validation.md` and workspace `diagnostics/ir_acceleration_20261006/` for the evidence and precision limits.
