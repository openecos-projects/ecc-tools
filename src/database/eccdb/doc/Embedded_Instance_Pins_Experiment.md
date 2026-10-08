# Embedded Instance Pins Experiment

Historical experiment: this document preserves the embedded-Pin variants and
cache measurements from before 2026-09-30. Its branch, schema numbers and
replay results are historical. For the 2026-10-08 independent InstanceTerm
components, ownership index and Design schema 13, see
[the current native model](Native_API.md#instanceterm-身份与所有权).

Baseline commit: `5a020f4318656e77cd9476b6b8344567e2655028`.
Experiment branch: `experiment/eccdb-embedded-instance-pins`.

## Layout

`DesignInstancePins` remains a component on the Instance entity. Its `records`
vector now owns the Pin payload; `values` retains the ordered public Pin IDs.
No entity or EnTT component is allocated per Instance Pin. The original
embedded-Pin experiment used a runtime `vector<DesignInstancePin*>` to resolve
opaque IDs without duplicating Pin payload. The follow-up experiment below
replaces it with an ID/ordinal locator. Net reverse-connection vectors, IO
Pins, placement, routing, and the Tech/Library layout are unchanged.

Pin-access preferences remain sparse, but their component is attached to the
owning Instance. Binary Design schema is now 4; it persists IDs, embedded
records, and the ID high-water mark, not pointers. Schema 3 files are rejected.
IDs are monotonic and not reused; unlimited delete/recreate churn was not
measured. Master replacement remains unsupported, as in the baseline.

## Measurement

ISPD18 test6: 107,919 Instances, 107,701 regular Nets, 690,219 Instance Pins,
474,330 regular Pin connections. Each result is the median of three fresh
processes on CPU 24, after one warmup pair. EccDB uses GCC 13, Release/O3/LTO;
the existing ODB executable uses its LLVM Bazel build. Times below are whole
operation batches, not one API call. The ODB column is from the experiment's
paired measurement, not an EccDB-before/after compiler-controlled baseline.

### Parser-Excluded Logical Database Operations

| Operation | Baseline EccDB ms | Embedded EccDB ms | ODB ms |
| --- | ---: | ---: | ---: |
| Create Instances, including Pins | 54.89 | 31.23 | 88.20 |
| Create Nets | 18.80 | 16.34 | 52.11 |
| Connect Pins | 37.94 | 32.31 | 42.59 |
| Build total (the three phases above) | 111.53 | 80.62 | 183.16 |
| Find Instance by name | 6.68 | 6.77 | 43.20 |
| Find Net by name | 6.69 | 6.94 | 46.81 |
| Find Instance Pin by name | 70.61 | 67.67 | 103.55 |
| Traverse Instances | 0.35 | 0.21 | 0.72 |
| Traverse Nets | 0.18 | 0.21 | 0.87 |
| Traverse Net Pin IDs only | 4.47 | 4.48 | 39.08 |

Fixture parsing, library setup, validation, and output writing are excluded.
Each backend validates the full fixture's object names, masters, ordered
terms, connections, and reverse-connection counts outside the timers.

### Routed TritonRoute-Facing Service Operations

These use the existing routed test6 artifacts, not a new routing run.
Both backends contain 2,957,852 route shapes and 1,391,014 route vias. The
EccDB runner additionally validates every projected route primitive against
its imported wire. Parsing/import, fixture loading, guide attachment, and
validation are outside the timers.

| Operation | Baseline EccDB ms | Embedded EccDB ms | ODB ms |
| --- | ---: | ---: | ---: |
| Enumerate regular Nets | 0.80 | 0.80 | 1.36 |
| Find Instance by name | 11.78 | 11.44 | 47.13 |
| Find Net by name | 10.01 | 9.94 | 45.49 |
| Find Instance Pin by name | 97.15 | 91.72 | 104.36 |
| Traverse Net Pins and related values | 76.74 | 71.91 | 79.07 |
| Traverse guides | 11.03 | 11.09 | 36.75 |
| Replace guides | 50.94 | 50.20 | 82.24 |
| Read routed wires | 350.24 | 352.42 | 94.68 |

### Complete LEF/DEF Workflow

Unlike the tables above, import times here include the parsers. Both EccDB
versions read the original test6 LEF/DEF, without a temporary PRL repair.
Three fresh processes per version; each imports, reads, and exports the DB.

| Operation | Baseline ms | Embedded ms |
| --- | ---: | ---: |
| LEF import | 11.92 | 11.38 |
| DEF import | 729.19 | 606.38 |
| Net-to-Pin count traversal | 3.52 | 3.29 |
| Full Pin-to-Net traversal | 1.60 | 2.51 |
| Placed geometry | 48.57 | 39.37 |
| Regular route geometry | 2.76 | 2.85 |
| Special route geometry | 0.00034 | 0.00098 |
| Tech/floorplan access | 0.011 | 0.013 |
| LEF Tech export | 0.263 | 0.196 |
| LEF Library export | 1.62 | 1.67 |
| DEF export | 219.93 | 171.05 |

### Memory

| Metric | Baseline MiB | Embedded MiB | Reduction |
| --- | ---: | ---: | ---: |
| Logical benchmark process peak RSS | 162.90 | 141.90 | 12.9% |
| Complete DEF import settled RSS | 138.67 | 115.64 | 16.6% |

Peak RSS is `/usr/bin/time` max RSS and includes fixture/library/setup and
validation temporaries. Settled RSS is sampled from `smaps_rollup` after
`malloc_trim`, while the complete imported DB remains alive. It is not a
Pin-only allocation total. The original embedded ID-to-pointer index is included.

## Verification

- Standalone EccDB targets and `irt_interface` compiled with 32 jobs.
- 72 selected unit/DEF/binary/Service tests passed. Three pre-existing tests
  remain disabled; the opt-in ISPD19 large archive test was skipped.
- New tests verify one Instance entity allocation despite automatic Pins,
  embedded record identity, deletion invalidation, unrelated-owner pointer
  stability, and deleted Pin ID non-reuse across binary restore.
- The original 107,919-Instance test6 DB was saved and restored. Its text-import
  DEF export and binary-restored DEF export match byte for byte (16,536,597
  bytes; SHA-256 `eddd52fc325d942b56f9ababc7d26704765d64e1fcb4827c90ad4657a96100a0`).
- Native ODB/EccDB benchmark fixture and routed-data validations passed.
  The separate OpenDB Python normalized DEF differential test was skipped
  because the local `odb.py`/`_odb.so` runtime was unavailable. No complete
  dual-backend routing run was performed for this experiment.

## Interpretation

Instance creation is 43.1% faster and logical build time is 27.7% lower.
Removing Pin entities also lowers process RSS. Name lookup is still dominated
by the unchanged master-term name index, so the gain is modest. Wire reading
does not improve because its layout and projection path were not changed.

Full Pin-to-Net scanning is 56.8% slower: it now walks many instance-local
allocations instead of one paged EnTT Pin payload. The layout is therefore a
creation/memory improvement, not a universal traversal improvement.

## Pointer-Cache Removal Follow-up (2026-09-28)

This follow-up removes all three embedded-Pin pointer cache fields
(`instance_ptr`, `master_term_ptr`, `master_term_name_index`) and replaces the
runtime `PinId -> Pin*` locator with `PinId -> {InstanceId, ordinal}`. It does
not remove the stable Pin IDs, the Library master-term name index itself, or
temporary pointer maps owned by downstream adapters. The no-cache version
resolves Instance and MasterTerm through their registries when a Service
consumer requests those related values.

The same test6 fixture has 107,919 Instances, 107,701 Nets, 690,219 embedded
Pins, and 474,330 connections. Times are medians of three CPU-25 runs, in ms
for the full batch. The parser-excluded logical comparison uses fresh processes;
the actual TritonRoute trace replays 1,229,052 captured API events. ODB and IDB
are unchanged reference binaries. Both EccDB variants use GCC 13 Release/O3.

| Logical DB operation | ODB | IDB | EccDB cached | EccDB no pointers |
| --- | ---: | ---: | ---: | ---: |
| Create Instances and Pins | 83.55 | 216.99 | 41.96 | 39.79 |
| Connect Pins | 42.35 | 242.68 | 43.26 | 83.41 |
| Find Instance Pin by name | 93.78 | 178.94 | 76.94 | 110.80 |
| Traverse Net Pin IDs only | 38.67 | 4.94 | 5.76 | 5.74 |

| Captured TritonRoute API batch | ODB | IDB | EccDB cached | EccDB no pointers |
| --- | ---: | ---: | ---: | ---: |
| Find Instance Pin | 74.36 | 103.85 | 72.39 | 96.06 |
| Read Net Pins and related values | 225.59 | 187.60 | 196.11 | 288.87 |
| Replace ordinary wires | 432.77 | 947.02 | 1169.40 | 1169.35 |
| Total captured API time | 821.50 | 1363.23 | 1504.00 | 1620.48 |

The no-pointer layout saves about 5.0 MiB of peak RSS in the logical benchmark
(157,624 to 152,524 KiB), but makes Connect 1.93x, name lookup 1.44x, and
the trace's Net-Pin read 1.47x slower. The trace's wire write time is unchanged.
Full original LEF/DEF import has the same object counts, and three fresh
processes show 109,720-111,704 KiB peak RSS versus 116,736 KiB with the
pointer caches. Import timing is a single indicative run, not a repeated
comparison.

The EccDB logical checksum is identical before and after. Of 53 selected
unit/binary/Service tests, 51 passed, one was disabled, and one opt-in
large-data test was skipped. Both trace variants validated every event, all 107,701 Net-Pin
sets, and 2,957,852 final wire primitives. These results isolate the embedded
Pin caches; they do not measure end-to-end routing runtime.

Raw follow-up JSON is under `build/eccdb-differential/` with the
`embedded_pointer_cache_{before,after}_*20260928.json` names. The original
cached executables are preserved as
`benchmarks/eccdb_{db_only,drt_trace}_pointer_cache_before_20260928`.

### Cache Restored

The current working tree restores all four runtime pointer caches after the
no-pointer comparison. Three CPU-25 runs give 47.50 ms for logical Pin connect
and 77.53 ms for logical Pin name lookup, versus 83.41 ms and 110.80 ms
without pointers. On the same captured TritonRoute trace, Pin lookup is
70.88 ms, Net-Pin reads are 198.15 ms, and total API time is 1518.14 ms;
the no-pointer values were 96.06 ms, 288.87 ms, and 1620.48 ms. The restored
logical benchmark peak RSS is 157,628 KiB versus 152,524 KiB without pointers.
All selected unit/binary/Service tests and all trace validations pass.

These pointers are runtime-only and rebuilt after binary load. Instance and
name-index addresses remain stable under supported operations. The
`LibraryMasterTerm*` cache retains the existing requirement that Library terms
are not removed or reordered while a Design referencing them is alive; term
deletion uses swap-and-pop and could otherwise invalidate a cached address.
Restored measurements are in `embedded_pointer_cache_restored_final_odb_20260928.json`,
`embedded_pointer_cache_restored_service_20260928.json`, and
`embedded_pointer_cache_restored_trace_20260928.json`.

## Raw Artifacts

Under `build/eccdb-differential/`:

- `embedded_pin_{baseline,experiment}_odb_3.json`
- `embedded_pin_{baseline,experiment}_drt_3.json`
- `embedded_pin_import_{baseline,experiment}.jsonl`
- `embedded_pin_archive/` and `embedded_pin_archive_roundtrip.jsonl`
- `embedded_pin_{text,restored}.def`

The old executables remain as `benchmarks/eccdb_db_only_baseline`,
`benchmarks/eccdb_drt_service_baseline`, and `benchmarks/eccdb_benchmark_baseline`.
These build artifacts are ignored by Git. `DbOnlyComparison.py --peak-rss`
records fresh-process memory alongside its normal phase timings.
