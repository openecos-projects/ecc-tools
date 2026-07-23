#!/usr/bin/env python
"""Spec/manifest join, schema validation, and the --check gate for the
ecc_py binding census.

The curated spec (``binding_spec.json``) carries, per in-scope (binding,
parameter), the old/new C++ types and defaults, scalar/list shape,
required/optional shape, and the path / non_path / ambiguous classification
with a written rationale. The manifest (``manifest.json``) is the
deterministic generated join of lexer discovery + spec.
"""
import json
from pathlib import Path

from lexer import DiscoveredBinding, discover

MANIFEST_VERSION = 1

PATH_TYPE_REQUIRED = "std::filesystem::path"
PATH_TYPE_OPTIONAL = "std::optional<std::filesystem::path>"
PATH_TYPE_LIST = "std::vector<std::filesystem::path>"

# Machine-readable form of the reviewed classification baseline: the bindings
# and parameters that must exist in the curated spec with a `path`
# classification. Used by --check for coverage; the spec/manifest remain the
# final authority (any deviation is documented in baseline_diff.md).
BASELINE_PATH_PARAMS: dict[tuple[str, str], list[str]] = {
    ("py_config", "flow_init"): ["flow_config"],
    ("py_config", "db_init"): [
        "config_path", "tech_lef_path", "lef_paths", "def_path", "verilog_path",
        "output_path", "feature_path", "lib_paths", "sdc_path",
    ],
    ("py_eval", "cell_density"): ["save_path"],
    ("py_eval", "pin_density"): ["save_path"],
    ("py_eval", "net_density"): ["save_path"],
    ("py_eval", "rudy_congestion"): ["save_path"],
    ("py_eval", "lut_rudy_congestion"): ["save_path"],
    ("py_eval", "egr_congestion"): ["save_path"],
    ("py_eval", "eval_cell_hierarchy"): ["plot_path"],
    ("py_eval", "eval_macro_hierarchy"): ["plot_path"],
    ("py_eval", "eval_macro_connection"): ["plot_path"],
    ("py_eval", "eval_macro_pin_connection"): ["plot_path"],
    ("py_eval", "eval_macro_io_pin_connection"): ["plot_path"],
    ("py_feature", "feature_summary"): ["path"],
    ("py_feature", "feature_tool"): ["path"],
    ("py_feature", "feature_pl_eval"): ["json_path"],
    ("py_feature", "feature_cts_eval"): ["json_path"],
    ("py_feature", "feature_eval_map"): ["path"],
    ("py_feature", "feature_route"): ["path"],
    ("py_feature", "feature_route_read"): ["path"],
    ("py_feature", "feature_macro_drc"): ["path", "drc_path"],
    ("py_feature", "feature_eval_summary"): ["path"],
    ("py_feature", "feature_timing_eval_summary"): ["path"],
    ("py_feature", "feature_net_eval"): ["path"],
    ("py_feature", "feature_cong_map"): ["dir"],
    ("py_icts", "run_cts"): ["cts_config", "cts_work_dir"],
    ("py_icts", "cts_report"): ["path"],
    ("py_idb", "idb_init"): ["config_path"],
    ("py_idb", "tech_lef_init"): ["techlef_path"],
    ("py_idb", "def_init"): ["def_path"],
    ("py_idb", "verilog_init"): ["verilog_path"],
    ("py_idb", "sdc_init"): ["sdc_path"],
    ("py_idb", "spef_init"): ["spef_path"],
    ("py_idb", "lef_init"): ["lef_paths"],
    ("py_idb", "lib_init"): ["lib_paths"],
    ("py_idb", "def_save"): ["def_name"],
    ("py_idb", "tcl_save"): ["tcl_name"],
    ("py_idb", "gds_save"): ["gds_name"],
    ("py_idb", "netlist_save"): ["netlist_path"],
    ("py_idb", "json_save"): ["path"],
    ("py_idb", "save_data"): ["path"],
    ("py_idb", "load_data"): ["path"],
    ("py_idb", "write_soc_json"): ["path"],
    ("py_idb", "write_abstract_lef"): ["output_lef_path"],
    ("py_idb", "view_json_save"): ["output_dir"],
    ("py_idb", "view_json_apply_edits"): ["edits_path"],
    ("py_idb", "idb_get"): ["file_name"],
    ("py_idrc", "init_drc"): ["temp_directory_path"],
    ("py_idrc", "run_drc"): ["config", "report"],
    ("py_idrc", "save_drc"): ["path"],
    ("py_irt", "init_rt"): ["config"],
    ("py_irt", "run_ert"): ["config"],
    ("py_ista", "init_sta"): ["config"],
    ("py_ircx", "init_rcx"): ["config"],
    ("py_izh", "fix_fanout"): ["config"],
    ("py_izh", "insert_filler"): ["config"],
    ("py_report", "report_wirelength"): ["path"],
    ("py_report", "report_db"): ["path"],
    ("py_report", "report_congestion"): ["path"],
    ("py_report", "report_dangling_net"): ["path"],
    ("py_report", "report_route"): ["path"],
    ("py_report", "report_drc"): ["path"],
}


def load_json(path: Path) -> dict:
    return json.loads(path.read_text())


def load_schema(path: Path) -> dict:
    return load_json(path)


def validate_spec(spec: dict, schema: dict) -> None:
    import jsonschema

    jsonschema.validate(spec, schema)


def validate_manifest(manifest: dict, schema: dict) -> None:
    import jsonschema

    jsonschema.validate(manifest, schema)


def build_manifest(discovery: dict, spec: dict) -> dict:
    """Join the curated spec against discovery into manifest entries."""
    index: dict[tuple[str, str], DiscoveredBinding] = {}
    for binding in discovery["bindings"]:
        index.setdefault((binding.module, binding.py_name), binding)
    entries: list[dict] = []
    for spec_binding in spec["bindings"]:
        key = (spec_binding["module"], spec_binding["py_name"])
        discovered = index.get(key)
        if discovered is None:
            raise ValueError(f"spec binding not discovered: {key[0]}.{key[1]}")
        for param in spec_binding["params"]:
            entries.append(
                {
                    "module": discovered.module,
                    "file": discovered.file,
                    "line": discovered.line,
                    "py_name": discovered.py_name,
                    "cpp_target": discovered.cpp_target,
                    "param": param["param"],
                    "old_type": param["old_type"],
                    "old_default": param["old_default"],
                    "new_type": param["new_type"],
                    "new_default": param["new_default"],
                    "scalar_or_list": param["scalar_or_list"],
                    "required_or_optional": param["required_or_optional"],
                    "binding_status": discovered.status_in_source,
                    "classification": param["classification"],
                    "classification_rationale": param["classification_rationale"],
                }
            )
    entries.sort(key=lambda e: (e["module"], e["py_name"], e["param"]))
    return {"version": MANIFEST_VERSION, "entries": entries}


def _dumps(obj: dict) -> bytes:
    return (json.dumps(obj, indent=2, sort_keys=True) + "\n").encode()


def generate_manifest_bytes(repo_root: Path, census_dir: Path) -> bytes:
    discovery = discover(repo_root)
    spec = load_json(census_dir / "binding_spec.json")
    validate_spec(spec, load_schema(census_dir / "binding_spec.schema.json"))
    manifest = build_manifest(discovery, spec)
    validate_manifest(manifest, load_schema(census_dir / "manifest.schema.json"))
    return _dumps(manifest)


def _is_string_literal_default(default: str | None) -> bool:
    return default is not None and default.lstrip().startswith('"')


def check(repo_root: Path, census_dir: Path) -> list[str]:
    """Run every census gate; return a list of failure messages (empty = pass)."""
    failures: list[str] = []
    discovery = discover(repo_root)
    manifest_path = census_dir / "manifest.json"

    loaded: dict[str, dict] = {}
    for name in ("binding_spec.json", "manifest.json", "binding_spec.schema.json", "manifest.schema.json"):
        try:
            loaded[name] = load_json(census_dir / name)
        except json.JSONDecodeError as exc:
            failures.append(f"{name} is not valid JSON: {exc}")
        except OSError as exc:
            failures.append(f"{name} is missing or unreadable: {exc}")
    if failures:
        return failures
    spec = loaded["binding_spec.json"]
    spec_schema = loaded["binding_spec.schema.json"]
    manifest_schema = loaded["manifest.schema.json"]

    # (b) schema validation for spec and committed manifest
    import jsonschema

    try:
        validate_spec(spec, spec_schema)
    except jsonschema.ValidationError as exc:
        failures.append(f"binding_spec.json fails schema validation: {exc.message}")
    manifest = loaded["manifest.json"]
    try:
        validate_manifest(manifest, manifest_schema)
    except jsonschema.ValidationError as exc:
        failures.append(f"manifest.json fails schema validation: {exc.message}")

    # (a) regeneration must be byte-stable against the committed manifest
    try:
        rebuilt = build_manifest(discovery, spec)
    except (KeyError, ValueError) as exc:
        failures.append(f"manifest regeneration failed: {exc}")
        return failures
    regenerated = _dumps(rebuilt)
    if regenerated != manifest_path.read_bytes():
        failures.append(
            "manifest.json is out of date: regeneration is not byte-stable against the committed file"
        )

    bindings = discovery["bindings"]
    discovered_index: dict[tuple[str, str], DiscoveredBinding] = {}
    for binding in bindings:
        discovered_index.setdefault((binding.module, binding.py_name), binding)
    spec_index: dict[tuple[str, str], dict[str, dict]] = {}
    for spec_binding in spec["bindings"]:
        spec_index[(spec_binding["module"], spec_binding["py_name"])] = {
            p["param"]: p for p in spec_binding["params"]
        }

    # (c) coverage, forward: active bindings with string-literal py::arg defaults
    for binding in bindings:
        if binding.status_in_source != "active":
            continue
        spec_params = spec_index.get((binding.module, binding.py_name), {})
        for param in binding.params:
            if _is_string_literal_default(param.default) and param.name not in spec_params:
                failures.append(
                    f"coverage: active binding {binding.py_name} ({binding.file}:{binding.line}) has "
                    f'py::arg("{param.name}") with a string-literal default but no spec entry'
                )

    # (c) coverage, baseline-named path parameters must be spec'd as path
    for (module, py_name), params in BASELINE_PATH_PARAMS.items():
        binding = discovered_index.get((module, py_name))
        if binding is None:
            failures.append(f"baseline binding not discovered: {module}.{py_name}")
            continue
        if binding.status_in_source != "active":
            failures.append(f"baseline binding {module}.{py_name} is not active")
            continue
        spec_params = spec_index.get((module, py_name), {})
        for param in params:
            row = spec_params.get(param)
            if row is None:
                failures.append(f"coverage: baseline path parameter {module}.{py_name}.{param} has no spec entry")
            elif row["classification"] != "path":
                failures.append(
                    f"coverage: baseline path parameter {module}.{py_name}.{param} is classified "
                    f"{row['classification']} in the spec"
                )

    # (c) coverage, reverse: every spec entry names a discovered binding/param
    for spec_binding in spec["bindings"]:
        key = (spec_binding["module"], spec_binding["py_name"])
        binding = discovered_index.get(key)
        if binding is None:
            failures.append(f"spec entry names an undiscovered binding: {key[0]}.{key[1]}")
            continue
        if binding.params:
            discovered_params = {p.name for p in binding.params}
            for param in spec_binding["params"]:
                if param["param"] not in discovered_params:
                    failures.append(
                        f"spec entry {key[0]}.{key[1]}.{param['param']} does not match any discovered "
                        f"py::arg of {key[1]}"
                    )
        # py::arg-free bindings: params are curated-only (documented limitation)

    # (d) ambiguous classifications must carry a rationale
    for spec_binding in spec["bindings"]:
        for param in spec_binding["params"]:
            if param["classification"] == "ambiguous" and not param["classification_rationale"].strip():
                failures.append(
                    f"ambiguous classification without rationale: "
                    f"{spec_binding['module']}.{spec_binding['py_name']}.{param['param']}"
                )

    # (e) path-classified params of active bindings must carry the converted types
    for entry in rebuilt["entries"]:
        if entry["classification"] != "path" or entry["binding_status"] != "active":
            continue
        expected = (
            PATH_TYPE_LIST
            if entry["scalar_or_list"] == "list"
            else PATH_TYPE_OPTIONAL
            if entry["required_or_optional"] == "optional"
            else PATH_TYPE_REQUIRED
        )
        if entry["new_type"] != expected:
            failures.append(
                f"new_type: {entry['module']}.{entry['py_name']}.{entry['param']} is {entry['scalar_or_list']}/"
                f"{entry['required_or_optional']} path but has new_type {entry['new_type']!r} (expected {expected!r})"
            )
    return failures
