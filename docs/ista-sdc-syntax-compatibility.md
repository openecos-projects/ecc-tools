# iSTA SDC Syntax-Only Compatibility

ECC PR [354](https://github.com/openecos-projects/ecc/pull/354) generates SDC
commands whose timing semantics are not yet implemented by iSTA. The following
adapters allow those scripts to be read; they do **not** implement the constraints.

| Command | Accepted arguments | Current effect |
| --- | --- | --- |
| `set_max_transition` | `transition objects`, `-clock_path`, `-data_path`, `-rise`, `-fall` | Warns; no constraint storage or transition-limit checks. |
| `set_max_capacitance` | `capacitance objects` | Warns; no constraint storage or capacitance-limit checks. |
| `set_timing_derate` | `derate`, `-early`, `-late`, `-clock`, `-data`, `-delay_corner collection` | Warns; no OCV derates or delay changes. |
| `all_delay_corners` | No arguments | Warns and returns an empty Tcl collection; no named corners are created. |

Required arguments and numeric parsing are checked. These adapters do not resolve
target objects or apply analysis semantics. Unrelated unknown commands remain
errors. Existing supported commands still apply their constraints normally.

Since these syntax-only commands do not create effective constraints,
`output_sdc` does not export them. Keep the original SDC for downstream tools;
an exported effective-constraint snapshot does not preserve these ignored rules.

STA developers must later implement constraint storage, target/corner resolution,
transition and capacitance checks, OCV clock/data propagation, and effective SDC
export. Successful parsing alone is not evidence of complete SDC support or a
successful full harden flow. PR descriptions for this change must explicitly
state this syntax-only scope.
