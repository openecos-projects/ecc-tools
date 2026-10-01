# InstanceTerm Dense Map Experiment, 2026-10-01

## Status

This experimental storage-layout change completed one candidate replay on 2026-10-01. All 1,878,138 events passed validation. Saved same-day baseline, partition, ODB and iDB measurements were reused without rerunning them. Full results and code handoff are archived under `/home/zhuxu1/workspace/daily/2026年10月1日/instance-term-densemap-experiment`.

| Group | Baseline ms | Dense map ms | ODB ms | iDB ms | ODB / Dense map |
| --- | ---: | ---: | ---: | ---: | ---: |
| find_pin | 149.614 | 127.950 | 178.844 | 175.579 | 1.398x |
| read_instance_pins | 37.006 | 37.110 | 48.342 | 88.177 | 1.303x |
| net_pins | 198.853 | 195.561 | 197.838 | 248.028 | 1.012x |
| full 34 | 3460.526 | 3379.346 | 5330.140 | unsupported | 1.577x |
| common 18 | 1518.804 | 1481.821 | 1257.577 | 1427.251 | 0.849x |

Name lookup elapsed time decreased 14.48%; full API elapsed time decreased 2.35%. Net-pin traversal changed only 1.66%, too small for a strong conclusion from these single observations. Whole-process peak RSS increased from 1658.395 to 1661.832 MiB (+3.438 MiB, 0.21%). Unmodified operation groups also varied, so the complete difference cannot be attributed solely to Term storage. Tests: 90 unique passed, 1 skipped, 3 previously disabled; the final added binary round-trip assertions also passed separately. Release replay, iRT interface and native TritonRoute adapter builds succeeded.

## Layout Under Test

The candidate keeps actual instance-term values in a per-Instance `dense_map<string, Term>`. It deliberately does **not** create a separate EnTT entity for every InstanceTerm.

- Ordered Term IDs remain for compatibility with existing relationship/vector-facing APIs.
- One runtime `ID -> Term*` vector resolves those compatibility IDs to the per-Instance values.
- The `DesignRoot` Term high-water mark remains persisted, so the compatibility ID space does not silently restart after archive/load.
- The map uses the default `dense_map` hash.
- Existing owner and MasterTerm caches remain, and the new ID table is itself a cache. This is not a cache-free experiment.

Consequently, any observed change combines payload placement, name-map access, ordered-ID compatibility, and the new ID lookup table. It cannot be attributed solely to removing a Term entity.

## Workload And Scope

The frozen source is the original TritonRoute import/writeback trace:

```text
build/term-pin-v3-20260930.trace.jsonl
SHA-256: 0cfd87eb11d2a08cfc05d4fdd901b9659885319aea5bd099f27ffdb2c59887b8
```

The saved reference replay has 1,878,138 events across 34 observed API groups. Eighteen groups are shared with iDB. The trace comes from an ODB-backed TritonRoute operation capture, but this experiment only replays that captured API sequence. Replay setup still reads the input database, while TritonRoute detailed routing, maze routing, DRC, and the router algorithm are not rerun independently for each backend.

For each trace event, the native replay timer includes the adapter operation chain and returned JSON-field projection. It excludes trace/LEF/DEF/guide parsing, token binding, canonicalization, and final validation. `/usr/bin/time -v` records whole-process peak RSS, wall, user, and system time; those values include trace reading, parsing, import, replay, and validation. Post-import and post-replay RSS are not collected because the frozen reference harness has no phase instrumentation.

## Comparison Discipline

The baseline EccDB, registry-partition EccDB, ODB, and iDB values are reused from one same-day saved reference round. The dense-map candidate is one fresh pass. These are not synchronized A/B runs, no backend means or standard deviations are computed, and the report makes no significance claim.

Speedup columns use `ODB ms / EccDB ms`, `baseline ms / dense-map ms`, and `partition ms / dense-map ms`. A value above `1x` means the dense-map candidate is faster. Positive dense-map change percentages mean the candidate took longer.

iDB is intentionally absent for the full 34-group total when it reports unsupported operations. Unsupported rows are not converted to zero. iDB endpoint-extension fields are not cross-compared because that backend does not retain those semantics.

## Reporting Command

The default `python3` environment does not contain Matplotlib. Use the already available Python environment below; no dependency installation is part of this experiment.

```bash
/home/zhuxu1/.venv/bin/python -B \
  src/database/eccdb/benchmarks/InstanceTermDenseMapReport.py \
  --references build/registry-partition-20261001/replay/summary.json \
  --candidate build/instance-term-densemap-20261001/replay/candidate.json \
  --candidate-time build/instance-term-densemap-20261001/replay/candidate.time.txt \
  --output-dir build/instance-term-densemap-20261001/report
```

The output directory contains `summary.json`, `summary.csv`, `summary.md`, `instance_term_densemap.{png,svg}`, and `instance_term_densemap_distribution.{png,svg}`. The JSON preserves input hashes, replay counts, full-34 and common-18 aggregates, whole-process resources, and candidate RSS deltas. The CSV carries every API group, call count, event frequency, backend totals, explicit iDB support state, and ratios. The first two-panel graph shows the three Term-access groups and the full/common aggregate groups; RSS is deliberately not plotted with API timer totals. The distribution graph is also two-panel: its left pie is event-call frequency, while its right pie is cumulative dense-map candidate timer share. Each uses the top five API groups plus an explicit Other bucket; the two pies must not be read as the same metric.

## Implementation And Verification

The source change currently places the concrete `DesignInstanceTerm` payload inside each instance's `dense_map`, while the ordered compatibility IDs resolve through a runtime pointer table. `PhysicalDesignService::findInstanceTermData` returns the embedded payload pointer for the hot `find_pin` path. The default `std::hash<std::string>` lookup materializes a temporary `std::string` from the incoming `string_view`; this is part of the measured design, not an omitted cost.

Net-pin traversal retains its ID vector and resolves the embedded payload through the runtime pointer table without adding per-endpoint hot-path checks. The binary design schema is version 8: it persists term records and the root high-water counter, but deliberately excludes runtime pointers, which are rebuilt after restore. Existing owner/master caches remain.

The release GCC 13 `O3`/`NDEBUG`/LTO replay build retry and the final native `eccdbNativeRoute` build both completed successfully after the local ccache retry; the iRT interface also built. CTest XML records 94 cases with zero failures, three disabled, one skipped, and 90 unique passed cases. A late binary round-trip assertion update was followed by a binary-target-only rebuild and one successful `RoundTrips...` rerun. These checks establish implementation status, not a performance result.

The single fresh dense-map replay completed on CPU 25 with all 34 operation groups and 1,878,138 events validated. No further performance run is planned. ODB and iDB remain saved reference observations; they were not rerun in this experiment.
