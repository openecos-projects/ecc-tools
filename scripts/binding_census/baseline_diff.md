# Manifest vs classification baseline

Comparison of the generated `manifest.json` against the reviewed
classification baseline. The manifest is the final authority; every deviation
has a written rationale below.

## Totals

| Measure | Baseline | Manifest | Delta |
|---|---|---|---|
| path scalars | 69 | 69 | 0 |
| path lists | 4 | 4 | 0 |
| required path scalars | 41 | 40 | -1 |
| optional path scalars | 28 | 29 | +1 |

The single scalar that moves between the required and optional columns is
`sdc_init.sdc_path`; see the deviation rationale. The expected post-review
targets (40 required + 29 optional = 28 draft-optional + the `sdc_init`
reclassification) match the manifest exactly.

## Per-module rows (path parameters)

| Module | Path scalars (req/opt) | Path lists | Matches baseline |
|---|---|---|---|
| py_config | 8 (1/7) | 2 | yes |
| py_eval | 11 (5/6) | 0 | yes |
| py_feature | 13 (13/0) | 0 | yes |
| py_icts | 3 (3/0) | 0 | yes |
| py_idb | 18 (16/2) | 2 | yes, with the `sdc_init` deviation below |
| py_idrc | 4 (0/4) | 0 | yes |
| py_irt | 2 (0/2) | 0 | yes |
| py_ista | 1 (0/1) | 0 | yes |
| py_ircx | 1 (1/0) | 0 | yes |
| py_izh | 2 (0/2) | 0 | yes |
| py_report | 6 (1/5) | 0 | yes |
| **Total** | **69 (40/29)** | **4** | |

Modules with no path parameters (`py_ifp`, `py_ipdn`, `py_instance`,
`py_imp`, `py_flow`) contribute only `non_path` adjudication rows
(`py_ifp` 22, `py_ipdn` 31, `py_instance` 4; `py_imp` and `py_flow` have no
string-carrying parameters in their active bindings and therefore no rows).

## Deviations and rationales

1. **`py_idb.sdc_init.sdc_path`: required in the baseline, `optional` in the
   spec.** Its C++ body (`initSdc` in `py_db.cpp`) stores the value as-is and
   the timing flow treats empty as unset, and the production harden flow
   passes `None` natively (`runner.py` passes `workspace.pdk.sdc`, typed
   `Path | None`). Reclassification to
   `std::optional<std::filesystem::path>` with `py::none()` default was
   confirmed during design review. This is the only count deviation: optional
   28 -> 29, required 41 -> 40.

2. **`py_idb.tech_lef_init` parameter is named `techlef_path`, not
   `tech_lef_path`.** The binding is `py::arg`-free, so the parameter name is
   curated from the `initTechLef(const std::string& techlef_path)`
   declaration (curated-only limitation). Same parameter, same
   classification; no count impact.

3. **The manifest carries adjudication rows the baseline does not itemize.**
   The baseline lists path parameters and a handful of named non-paths
   (`step`, `net`, `json_format`, `pdk`). The census scopes in *every*
   string-carrying parameter of every active binding, so the manifest
   additionally holds `non_path` rows for name candidates — including the
   container-typed ones `netlist_save.exclude_cell_names`
   (`std::set<std::string>`), `write_soc_json.harden_cores`, and
   `report_place_distribution.prefixes` (`std::vector<std::string>`), plus
   the `py_ifp`/`py_ipdn`/`py_instance` name parameters. All keep
   `std::string` (`new_type == old_type`) with a written rationale; they do
   not affect the path totals above.

4. **Disabled bindings have no spec rows.** `runMP`, `runRef`, the commented
   `SAPlaceSeqPairInt64`/`write_placement_back` duplicates, and the whole
   `py_vec` family are discovered (with file:line) but excluded from the
   spec, because only active bindings are conversion targets. Their status is
   reported by the dead-binding audit (`dead_bindings.md`); `binding_status`
   in the manifest is therefore `active` for every row by construction, and
   `absent` appears only inside that audit.

## Confirmed ambiguous-name rulings

Per the ambiguous-name rule (stay `std::string` unless call-path evidence
proves filesystem semantics), with the evidence recorded in each row's
`classification_rationale`:

- `idb_get.file_name` = `path` (optional): `idbGet` forwards it to
  `rptInst->reportInstance/reportNet` (`py_db_op.h`), which write the report
  to that file.
- `place_instance.source` = `non_path`: `DataManager::placeInst` forwards it
  to `instance->set_type` (`idm_design_inst.cpp`) — a provenance tag, no
  filesystem semantics.
- The `config` parameters of `init_rt`, `run_ert`, `init_sta`, `init_rcx`,
  `fix_fanout`, `insert_filler`, `init_drc`/`run_drc` = `path`: the wrapper
  passes config file paths via `path_text(...)` at the call sites in
  `chipcompiler/tools/ecc/module.py` (cited per row).
- `init_rcx.pdk` = `non_path`: a PDK identifier (already
  `std::optional<std::string>`), not a path.
- `feature_tool.step`, `feature_cong_map.step`, `report_route.net`,
  `view_json_save.json_format` = `non_path`: selector/name strings forwarded
  unchanged to the feature/report APIs.

No parameter remains `ambiguous` in the committed spec.
