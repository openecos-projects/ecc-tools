# Inferred clock-gating checks in placement timing

`ClockGatingChecker` recognizes combinational clock gates from Boolean functions
and reachable clock cones. These gates may already exist in a core without IO
cells; adding IO cells is not a prerequisite. Explicit Liberty checks take
precedence, and constant enables or disabled clock fanout do not create checks.

The native `PathChecker` owns opening/closing edge selection, clock polarity,
uncertainty, reconvergence and path exceptions. External consumers must not
reconstruct the window from a clock period or invent a Liberty constraint table.

- Setup uses the sparse affine required-time contract. An inferred gate has
  `endpoint_arc = -1`, `check_coefficient = 0` and the complete native required
  time as its intercept. The source state and capture domain remain explicit.
  A single-clock design with inferred gates also selects sparse timing.
- Hold exports each native capture context with `check_type = 8`
  (`kClockGatingHold`). Pin, clock, source/local edge and path-tag identities
  remain intact. `required_ps` includes the constant gate margin and exceptions;
  `normal_required_ps` retains the unadjusted capture rule. There is no Liberty
  check row for an inferred gate. Structural export must not subtract a live
  LUT margin from this constant rule.
- Runtime placement propagates the changing data arrival through normal cell
  and wire models, then compares it with the native window. Hold consumers
  query live Liberty LUTs only for hold/removal classes 2/4, not gate class 8.
  Unknown check classes must still fail validation.

The existing sparse/hold payload shapes and numeric enum values are unchanged.
Consumers predating gate class 8 reject it; update DREAMPlace together with
ECC-Tools. This adds no user switch and does not suppress gate violations.
Structural placement timing still requires ideal clocks. This is not a claim
that an arbitrary gated design will close timing or that propagated clock
arrival can be recomputed inside the placement fast loop.

`test_ista_clock_gating` checks both logic polarities, inverted clocks/outputs,
full versus structural export, single versus multiple clocks, rise/fall
required-time parity and path exceptions. DREAMPlace's hold-schema and early
propagation tests cover the no-LUT context and live arrival gradients.
