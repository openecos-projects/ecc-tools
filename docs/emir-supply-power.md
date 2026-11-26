# Per-supply iPW → iEMIR power handoff

## 1. Scope

An IO cell can draw power from several voltages. A library's nominal voltage
is not the voltage of every PG pin. Preserve Liberty's `pg_pin`, `voltage_map`,
and signal-pin `related_power_pin` / `related_ground_pin` metadata through
power analysis and resolve the actual rail using DEF connectivity.

## 2. Interfaces

`init_emir(..., supply_ground_pins={})` accepts an optional Python dictionary
of power-pin names to return-ground-pin names. It fills missing return metadata;
it does not override a known Liberty association or change physical connectivity.
The default is empty: there are no built-in PDK-specific pin pairs.

`InstancePowerReader::read(path, supply_ground_pins)` reads iPW's native TSV.
The Python orchestrator must not convert or rewrite that artifact.

## 3. File and current contracts

Averaged power uses `# iEMIR_INSTANCE_POWER_V2` and these tab-separated columns:

```text
instance_name voltage_v internal_power_w switching_power_w leakage_power_w total_power_w average_current_a power_pin ground_pin
```

The displayed spaces above stand for tabs. Multiple rows for an instance are
allowed only for distinct power pins. Models without PG metadata keep a single
generic row with both final fields empty. The seven-column V1 input remains
supported. For every row, the exported nonnegative components sum to total power
and `voltage_v * average_current_a == total_power_w`. Negative internal power is
clamped at the native export boundary, independently per rail.

Each explicit rail contributes its current once to its power graph and returns
that same current on its associated ground. It must not also enter the generic
instance-current distribution. Shared graph contacts are deduplicated before
weight normalization. Source voltages are compared only on the same power net.

Geometry-free device-layer PG pins may use the same instance's physical
contacts only when DEF explicitly connects both to the same net. This does not
repair an unconnected physical metal pin. `ir_analyzer/supply_coverage.tsv`
records selected/unselected power and ground contacts; omitted source domains
are not signed off. Multi-supply peak-window export is not implemented and is
rejected rather than exporting averaged per-rail values as peak values.

## 4. Validation

| Input | Result |
| --- | --- |
| Multiple supplies with missing voltage metadata | Reject; do not use one library voltage for every rail |
| Nonzero per-supply current with no known return | Require an explicit `supply_ground_pins` entry |
| Duplicate PG rows or generic and PG rows mixed for one instance | Reject |
| Invalid numbers, inconsistent component sum or `P = V × I` | Reject |
| Different voltages on one physical power net | Reject with net / instance / pin context |
| Nonzero current lacking a DEF PG connection or selected graph contact | Reject |
| Connected supply without selected PLOC sources | Report omitted coverage; not full-chip signoff |

## 5. Examples

- Good: one IO's 1.2 V core rail and 3.3 V IO rail on separate DEF nets, with
  explicit current and matching return on each rail.
- Base: a single-supply extracted macro without PG metadata uses its nominal
  voltage and the legacy generic distribution.
- Bad: change the IO library voltage to 1.2 V or ignore voltage conflicts to
  make a mixed-voltage run finish.

## 6. Regression tests

`ipw_supply_power` checks Liberty metadata import/moves, unit conversion,
per-supply leakage aggregation, V2 conservation, legacy fallback, and peak guard.
`iemir_supply_current_test` checks distinct-rail voltages, same-net conflicts,
current conservation, V1/V2 parsing, explicit return fallback, missing PG
contacts, coverage, and same-instance/same-net device-layer pin aliases.
Both run in CI with existing Liberty-parser and EMIR IR/EM regressions.

## 7. Wrong vs correct

Wrong: assign all IO power to the library nominal voltage, then distribute it
again on every connected power net.

Correct: retain rail voltage and power at the producer, resolve actual DEF PG
connections at the consumer, and preserve current through each power/ground pair.
