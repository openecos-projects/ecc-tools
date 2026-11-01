#!/usr/bin/env python3
# SPDX-License-Identifier: MulanPSL-2.0
"""Compare the iFP work databases produced by the iDB and EccDB adaptors.

This is intentionally separate from ``paired_ifp.py``'s DEF semantic gate.
It compares the JSON snapshots captured immediately after ``initFP()`` and
after ``runFP()``, before either adaptor writes back to its physical database.
The report keeps provider order visible, but also distinguishes an order-only
difference from a difference in exact object fields or nested collections.
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import sys
from typing import Any

# ``unittest`` may import this file by absolute path rather than execute it,
# in which case its sibling directory is not automatically on sys.path.
_SCRIPT_DIRECTORY = Path(__file__).resolve().parent
if str(_SCRIPT_DIRECTORY) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIRECTORY))

from paired_ifp import _canonical, _compare_models, _preview


BACKENDS = ("idb", "eccdb")
REQUIRED_STAGES = {
    "input": "input-model.json",
    "computed": "computed-model.json",
}
OPTIONAL_STAGES = {
    "controlled_input": "controlled-input-model.json",
}
EXPECTED_SCHEMA = 2
INDEX_TARGETS = {
    "instances": "instances",
    "routing_layers": "routing_layers",
    "io_pins": "io_pins",
    "pg_nets": "pg_nets",
}
REQUIRED_TOP_LEVEL_SECTIONS = frozenset({
    "snapshot_schema", "database_info", "index_maps", "sites", "masters", "routing_layers", "tracks",
    "rows", "new_rows", "instances", "io_pins", "nets", "pg_nets", "pg_segments",
})
UNORDERED_RECORD_LIST_FIELDS = frozenset({
    "sites", "masters", "routing_layers", "tracks", "rows", "new_rows", "instances", "io_pins",
    "nets", "pg_nets", "pg_segments", "pin_shapes", "pins", "ports", "new_ports",
})
MAX_SAMPLES = 12
MAX_FIELD_SAMPLES = 20


def _list_has_collection_semantics(path: str, value: list[Any]) -> bool:
    """Only record collections, never coordinate tuples, may ignore order."""
    field = path.rsplit(".", 1)[-1]
    return field in UNORDERED_RECORD_LIST_FIELDS and all(isinstance(item, dict) for item in value)


def _unordered_key(value: Any, path: str = "") -> str:
    """Canonicalize only known record-list order, preserving tuple/vector order.

    This is only a diagnostic projection.  The original JSON remains in the
    snapshots and the raw strict comparison remains in the report.
    """
    if isinstance(value, dict):
        return "{" + ",".join(
            f"{json.dumps(key, ensure_ascii=False, separators=(',', ':'))}:"
            f"{_unordered_key(child, f'{path}.{key}' if path else key)}"
            for key, child in sorted(value.items())
        ) + "}"
    if isinstance(value, list):
        children = [_unordered_key(child, f"{path}[{index}]") for index, child in enumerate(value)]
        if _list_has_collection_semantics(path, value):
            children.sort()
        return "[" + ",".join(children) + "]"
    return _canonical(value)


def _relation(left: Any, right: Any, path: str = "") -> dict[str, bool | str]:
    """Return exact and list-order-insensitive equality without normalization."""
    strict_equal = _canonical(left) == _canonical(right)
    values_equal = _unordered_key(left, path) == _unordered_key(right, path)
    return {
        "classification": "equal" if strict_equal else ("order_only" if values_equal else "value_difference"),
        "strict_equal": strict_equal,
        "values_equal": values_equal,
    }


def _load_json(path: Path) -> tuple[dict[str, Any], Any | None]:
    if not path.is_file():
        return {"status": "missing", "path": str(path)}, None
    try:
        with path.open(encoding="utf-8") as stream:
            return {"status": "loaded", "path": str(path), "bytes": path.stat().st_size}, json.load(stream)
    except (OSError, json.JSONDecodeError) as error:
        return {"status": "invalid", "path": str(path), "error": repr(error)}, None


def _named_objects(values: Any) -> bool:
    return isinstance(values, list) and all(
        isinstance(value, dict) and isinstance(value.get("name"), str) for value in values
    )


def _group_by_name(values: list[dict[str, Any]]) -> dict[str, list[dict[str, Any]]]:
    grouped: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for value in values:
        grouped[value["name"]].append(value)
    return grouped


def _field_differences(left: Any, right: Any, path: str = "") -> list[dict[str, Any]]:
    """Summarize fields whose exact values differ, including nested lists."""
    relation = _relation(left, right, path)
    if relation["classification"] == "equal":
        return []
    if isinstance(left, dict) and isinstance(right, dict):
        result: list[dict[str, Any]] = []
        for key in sorted(left.keys() | right.keys()):
            child_path = f"{path}.{key}" if path else key
            if key not in left:
                result.append({"path": child_path, "classification": "value_difference",
                               "left": "<missing>", "right": _preview(right[key])})
            elif key not in right:
                result.append({"path": child_path, "classification": "value_difference",
                               "left": _preview(left[key]), "right": "<missing>"})
            else:
                result.extend(_field_differences(left[key], right[key], child_path))
            if len(result) >= MAX_FIELD_SAMPLES:
                return result[:MAX_FIELD_SAMPLES]
        return result
    if isinstance(left, list) and isinstance(right, list):
        return [{
            "path": path,
            "classification": relation["classification"],
            "left_count": len(left),
            "right_count": len(right),
            "full_field_multiset_equal": relation["values_equal"],
            "left": _preview(left),
            "right": _preview(right),
        }]
    return [{"path": path, "classification": relation["classification"],
             "left": _preview(left), "right": _preview(right)}]


def _named_array_report(left: Any, right: Any, section_name: str) -> dict[str, Any] | None:
    """Compare an object list by its exact ``name`` field and full field values."""
    if not _named_objects(left) or not _named_objects(right):
        return None
    left_by_name = _group_by_name(left)
    right_by_name = _group_by_name(right)
    left_names = set(left_by_name)
    right_names = set(right_by_name)
    only_left = sorted(left_names - right_names)
    only_right = sorted(right_names - left_names)
    duplicate_left = sorted(name for name, objects in left_by_name.items() if len(objects) > 1)
    duplicate_right = sorted(name for name, objects in right_by_name.items() if len(objects) > 1)

    changed_names: list[str] = []
    multiplicity_changed: list[str] = []
    field_samples: list[dict[str, Any]] = []
    field_counts: Counter[str] = Counter()
    nested_order_only: Counter[str] = Counter()
    nested_value_difference: Counter[str] = Counter()
    for name in sorted(left_names & right_names):
        # Pair duplicate copies by their unordered representation first.  That
        # keeps a nested list reorder from looking like a different object.
        object_path = f"{section_name}[name={json.dumps(name, ensure_ascii=False)}]"
        left_values = sorted(left_by_name[name], key=lambda value: (_unordered_key(value, object_path), _canonical(value)))
        right_values = sorted(right_by_name[name], key=lambda value: (_unordered_key(value, object_path), _canonical(value)))
        if len(left_values) != len(right_values):
            multiplicity_changed.append(name)
            changed_names.append(name)
        for occurrence, (left_value, right_value) in enumerate(zip(left_values, right_values)):
            changes = _field_differences(left_value, right_value)
            if not changes:
                continue
            changed_names.append(name)
            for change in changes:
                field_counts[str(change["path"])] += 1
                if change["path"] in {"pin_shapes", "pins"}:
                    if change["classification"] == "order_only":
                        nested_order_only[str(change["path"])] += 1
                    else:
                        nested_value_difference[str(change["path"])] += 1
                if len(field_samples) < MAX_FIELD_SAMPLES:
                    field_samples.append({"name": name, "occurrence": occurrence, **change})

    return {
        "identity": "exact JSON string field name; no unescaping, stripping, or case folding",
        "left_object_count": len(left),
        "right_object_count": len(right),
        "left_unique_name_count": len(left_names),
        "right_unique_name_count": len(right_names),
        "only_left_name_count": len(only_left),
        "only_right_name_count": len(only_right),
        "only_left_name_sample": only_left[:MAX_SAMPLES],
        "only_right_name_sample": only_right[:MAX_SAMPLES],
        "duplicate_left_name_sample": duplicate_left[:MAX_SAMPLES],
        "duplicate_right_name_sample": duplicate_right[:MAX_SAMPLES],
        "multiplicity_changed_name_count": len(set(multiplicity_changed)),
        "multiplicity_changed_name_sample": sorted(set(multiplicity_changed))[:MAX_SAMPLES],
        "changed_name_count": len(set(changed_names)),
        "changed_name_sample": sorted(set(changed_names))[:MAX_SAMPLES],
        "changed_field_counts": dict(sorted(field_counts.items())),
        "field_samples": field_samples,
        "nested_collection_diagnostics": {
            "order_only": dict(sorted(nested_order_only.items())),
            "value_difference": dict(sorted(nested_value_difference.items())),
        },
    }


def _generic_section_report(name: str, left: Any, right: Any) -> dict[str, Any]:
    relation = _relation(left, right, name)
    result: dict[str, Any] = {"name": name, **relation}
    named = _named_array_report(left, right, name)
    if named is not None:
        result["named_objects"] = named
    return result


def _numeric_observations(left: dict[str, Any], right: dict[str, Any]) -> dict[str, Any]:
    """Call out the known accumulated cell-area float without masking it."""
    left_info = left.get("database_info")
    right_info = right.get("database_info")
    if not isinstance(left_info, dict) or not isinstance(right_info, dict):
        return {}
    left_value = left_info.get("cell_area")
    right_value = right_info.get("cell_area")
    numeric = lambda value: isinstance(value, (int, float)) and not isinstance(value, bool)
    if not numeric(left_value) or not numeric(right_value):
        return {}
    return {
        "database_info.cell_area": {
            "idb": left_value,
            "eccdb": right_value,
            "absolute_difference": abs(float(left_value) - float(right_value)),
            "strict_equal": left_value == right_value,
            "note": "Recorded exactly; this observation does not apply an epsilon or hide a raw database difference.",
        }
    }


def _index_invariants(model: Any) -> dict[str, Any]:
    """Check that each exact-name map points to, and covers, its source list."""
    if not isinstance(model, dict):
        return {"status": "invalid_model", "consistent": False, "maps": {}}
    maps = model.get("index_maps")
    if not isinstance(maps, dict):
        return {"status": "missing_index_maps", "consistent": False, "maps": {}}

    reports: dict[str, Any] = {}
    overall = True
    for map_name, list_name in INDEX_TARGETS.items():
        full_coverage_required = map_name != "pg_nets"
        mapping = maps.get(map_name)
        objects = model.get(list_name)
        if not isinstance(mapping, dict) or not isinstance(objects, list):
            reports[map_name] = {
                "list": list_name,
                "status": "unavailable",
                "consistent": False,
                "mapping_type": type(mapping).__name__,
                "list_type": type(objects).__name__,
            }
            overall = False
            continue
        names: list[str] = []
        malformed_object_indices: list[int] = []
        for index, object_value in enumerate(objects):
            if isinstance(object_value, dict) and isinstance(object_value.get("name"), str):
                names.append(object_value["name"])
            else:
                malformed_object_indices.append(index)
        name_counts = Counter(names)
        duplicate_names = sorted(name for name, count in name_counts.items() if count > 1)
        expected_names = set(names)
        map_names = set(mapping)
        invalid_entries: list[dict[str, Any]] = []
        for name, ordinal in mapping.items():
            valid_ordinal = isinstance(ordinal, int) and not isinstance(ordinal, bool)
            target = objects[ordinal] if valid_ordinal and 0 <= ordinal < len(objects) else None
            if not valid_ordinal or not isinstance(target, dict) or target.get("name") != name:
                if len(invalid_entries) < MAX_SAMPLES:
                    invalid_entries.append({"name": name, "ordinal": ordinal,
                                            "target_name": target.get("name") if isinstance(target, dict) else None})
        missing_names = sorted(expected_names - map_names)
        map_only_names = sorted(map_names - expected_names)
        # DataManager intentionally builds pg_net_name_to_idx_map from the
        # configured PG-net subset.  It is therefore allowed to be empty or
        # partial; every entry it does contain must still point at its exact
        # same-name PG net.  The other three maps cover every *exact name*.
        # They cannot cover duplicate records separately: iFP deliberately
        # retains an old projection beside a same-name new replacement while
        # the map points to the authoritative record.
        entry_targets_valid = not invalid_entries
        unique_name_valid = not duplicate_names
        full_coverage_valid = not missing_names and not map_only_names
        consistent = entry_targets_valid
        if full_coverage_required:
            consistent = (
                consistent
                and not malformed_object_indices
                and full_coverage_valid
            )
        reports[map_name] = {
            "list": list_name,
            "status": "checked",
            "consistent": consistent,
            "full_coverage_required": full_coverage_required,
            "entry_targets_valid": entry_targets_valid,
            "unique_name_valid": unique_name_valid,
            "unique_name_note": (
                "advisory only: same-name old/new iFP work records are permitted; raw list multiplicity is "
                "compared independently"
            ),
            "full_coverage_valid": full_coverage_valid if full_coverage_required else None,
            "list_count": len(objects),
            "map_count": len(mapping),
            "unique_list_name_count": len(expected_names),
            "duplicate_name_count": len(duplicate_names),
            "covered_name_count": len(expected_names & map_names),
            "missing_name_count": len(missing_names),
            "map_only_name_count": len(map_only_names),
            "invalid_entry_count": len(invalid_entries),
            "malformed_object_index_sample": malformed_object_indices[:MAX_SAMPLES],
            "duplicate_name_sample": duplicate_names[:MAX_SAMPLES],
            "missing_name_sample": missing_names[:MAX_SAMPLES],
            "map_only_name_sample": map_only_names[:MAX_SAMPLES],
            "invalid_entry_sample": invalid_entries,
        }
        overall = overall and consistent
    return {"status": "checked", "consistent": overall, "maps": reports}


def _index_maps_section(
    left: Any, right: Any, left_invariants: dict[str, Any], right_invariants: dict[str, Any]
) -> dict[str, Any]:
    """Compare map membership while retaining raw ordinal differences separately."""
    raw = _relation(left, right, "index_maps")
    reports: dict[str, Any] = {}
    values_equal = True
    raw_ordinal_difference_count = 0
    raw_ordinal_samples: list[dict[str, Any]] = []
    if not isinstance(left, dict) or not isinstance(right, dict):
        return {"name": "index_maps", "classification": "value_difference", "strict_equal": raw["strict_equal"],
                "values_equal": False, "raw_maps": raw,
                "note": "index_maps must be JSON objects"}

    for map_name in INDEX_TARGETS:
        left_map = left.get(map_name)
        right_map = right.get(map_name)
        if not isinstance(left_map, dict) or not isinstance(right_map, dict):
            reports[map_name] = {"classification": "value_difference", "left_type": type(left_map).__name__,
                                 "right_type": type(right_map).__name__}
            values_equal = False
            continue
        left_names = set(left_map)
        right_names = set(right_map)
        same_names = left_names == right_names
        per_map_raw_equal = _canonical(left_map) == _canonical(right_map)
        left_map_consistent = left_invariants.get("maps", {}).get(map_name, {}).get("consistent") is True
        right_map_consistent = right_invariants.get("maps", {}).get(map_name, {}).get("consistent") is True
        mismatches = [name for name in sorted(left_names & right_names) if left_map[name] != right_map[name]]
        raw_ordinal_difference_count += len(mismatches)
        for name in mismatches:
            if len(raw_ordinal_samples) < MAX_SAMPLES:
                raw_ordinal_samples.append({"map": map_name, "name": name,
                                            "idb_ordinal": left_map[name], "eccdb_ordinal": right_map[name]})
        reports[map_name] = {
            "classification": "equal" if per_map_raw_equal else (
                "order_only" if same_names and left_map_consistent and right_map_consistent else "value_difference"
            ),
            "strict_equal": per_map_raw_equal,
            "values_equal": same_names,
            "idb_self_consistent": left_map_consistent,
            "eccdb_self_consistent": right_map_consistent,
            "left_only_name_sample": sorted(left_names - right_names)[:MAX_SAMPLES],
            "right_only_name_sample": sorted(right_names - left_names)[:MAX_SAMPLES],
            "raw_ordinal_difference_count": len(mismatches),
        }
        values_equal = values_equal and same_names

    # An ordinal mismatch remains visible in raw maps, but is only an ordering
    # fact when the relevant maps independently point at their own same-name
    # object.  Whole-stage invariants are reported separately: identical stale
    # maps can be raw-equal yet still fail the stage's invariant gate.
    per_map_order_safe = all(
        report.get("classification") in {"equal", "order_only"} for report in reports.values()
    )
    classification = "equal" if raw["strict_equal"] else (
        "order_only" if values_equal and per_map_order_safe else "value_difference"
    )
    return {
        "name": "index_maps",
        "classification": classification,
        "strict_equal": raw["strict_equal"],
        "values_equal": values_equal,
        "raw_ordinal_difference_count": raw_ordinal_difference_count,
        "raw_ordinal_difference_sample": raw_ordinal_samples,
        "maps": reports,
        "note": (
            "Raw name-to-ordinal maps remain compared. A different ordinal is not treated as a wrong "
            "association when each backend map is self-consistent and exact-name membership matches."
        ),
    }


def _stage_report(name: str, left_path: Path, right_path: Path) -> dict[str, Any]:
    left_info, left = _load_json(left_path)
    right_info, right = _load_json(right_path)
    result: dict[str, Any] = {
        "name": name,
        "left": left_info,
        "right": right_info,
    }
    if left is None or right is None:
        result.update({
            "status": "unavailable",
            "strict_equal": False,
            "values_equal": False,
            "coverage_complete": False,
            "index_invariants": {"idb": {"status": "unavailable", "consistent": False},
                                 "eccdb": {"status": "unavailable", "consistent": False}},
        })
        return result
    if not isinstance(left, dict) or not isinstance(right, dict):
        result.update({
            "status": "partial_schema",
            "strict_equal": _canonical(left) == _canonical(right),
            "values_equal": _unordered_key(left) == _unordered_key(right),
            "coverage_complete": False,
            "schema": {"expected": EXPECTED_SCHEMA, "idb": None, "eccdb": None},
            "index_invariants": {"idb": _index_invariants(left), "eccdb": _index_invariants(right)},
        })
        return result

    left_invariants = _index_invariants(left)
    right_invariants = _index_invariants(right)
    schema = {
        "expected": EXPECTED_SCHEMA,
        "idb": left.get("snapshot_schema"),
        "eccdb": right.get("snapshot_schema"),
        "idb_missing_top_level_sections": sorted(REQUIRED_TOP_LEVEL_SECTIONS - set(left)),
        "eccdb_missing_top_level_sections": sorted(REQUIRED_TOP_LEVEL_SECTIONS - set(right)),
    }
    coverage_complete = (
        schema["idb"] == EXPECTED_SCHEMA
        and schema["eccdb"] == EXPECTED_SCHEMA
        and not schema["idb_missing_top_level_sections"]
        and not schema["eccdb_missing_top_level_sections"]
    )
    sections: dict[str, Any] = {}
    for section_name in sorted(left.keys() | right.keys()):
        if section_name not in left or section_name not in right:
            sections[section_name] = {
                "name": section_name,
                "classification": "value_difference",
                "strict_equal": False,
                "values_equal": False,
                "left": _preview(left.get(section_name, "<missing>")),
                "right": _preview(right.get(section_name, "<missing>")),
            }
        elif section_name == "index_maps":
            sections[section_name] = _index_maps_section(
                left[section_name], right[section_name], left_invariants, right_invariants
            )
        else:
            sections[section_name] = _generic_section_report(section_name, left[section_name], right[section_name])

    order_only_sections = [name for name, section in sections.items() if section["classification"] == "order_only"]
    value_difference_sections = [name for name, section in sections.items()
                                 if section["classification"] == "value_difference"]
    result.update({
        "status": "compared" if coverage_complete else "partial_schema",
        "schema": schema,
        "coverage_complete": coverage_complete,
        "strict_equal": _canonical(left) == _canonical(right),
        "values_equal": all(section["values_equal"] for section in sections.values()),
        "order_only_sections": order_only_sections,
        "value_difference_sections": value_difference_sections,
        "index_invariants": {"idb": left_invariants, "eccdb": right_invariants},
        "numeric_observations": _numeric_observations(left, right),
        "sections": sections,
        # Keep paired_ifp's field-level raw diagnostic in the independent
        # report; it is diagnostic only and does not replace the DEF gate.
        "raw_value_diagnostics": _compare_models(left_path, right_path, 0.0),
    })
    return result


def _optional_stage(pair_root: Path, name: str, filename: str) -> dict[str, Any]:
    left_path = pair_root / "idb" / filename
    right_path = pair_root / "eccdb" / filename
    left_exists = left_path.is_file()
    right_exists = right_path.is_file()
    if not left_exists and not right_exists:
        return {"name": name, "status": "not_present", "required": False}
    if not left_exists or not right_exists:
        return {
            "name": name,
            "status": "unavailable",
            "required": False,
            "left": {"status": "loaded" if left_exists else "missing", "path": str(left_path)},
            "right": {"status": "loaded" if right_exists else "missing", "path": str(right_path)},
        }
    result = _stage_report(name, left_path, right_path)
    result["required"] = False
    return result


def build_report(pair_root: Path) -> dict[str, Any]:
    stages = {
        name: _stage_report(name, pair_root / "idb" / filename, pair_root / "eccdb" / filename)
        for name, filename in REQUIRED_STAGES.items()
    }
    stages.update({
        name: _optional_stage(pair_root, name, filename)
        for name, filename in OPTIONAL_STAGES.items()
    })
    required = [stages[name] for name in REQUIRED_STAGES]
    unavailable = any(stage["status"] == "unavailable" for stage in required)
    coverage_complete = all(stage.get("coverage_complete") is True for stage in required)
    values_equal = all(stage.get("values_equal") is True for stage in required)
    strict_equal = all(stage.get("strict_equal") is True for stage in required)
    index_consistent = all(
        stage.get("index_invariants", {}).get("idb", {}).get("consistent") is True
        and stage.get("index_invariants", {}).get("eccdb", {}).get("consistent") is True
        for stage in required
    )
    order_only_sections = {
        name: stage.get("order_only_sections", [])
        for name, stage in stages.items() if "order_only_sections" in stage
    }
    value_difference_sections = {
        name: stage.get("value_difference_sections", [])
        for name, stage in stages.items() if "value_difference_sections" in stage
    }
    full_database_pass = not unavailable and coverage_complete and values_equal and index_consistent
    return {
        "report_schema": 1,
        "pair_root": str(pair_root),
        "required_stages": list(REQUIRED_STAGES),
        "stages": stages,
        "strict_equal": strict_equal,
        "values_equal": values_equal,
        "coverage_complete": coverage_complete,
        "index_invariants": {
            name: stage.get("index_invariants")
            for name, stage in stages.items() if "index_invariants" in stage
        },
        "order_only_sections": order_only_sections,
        "value_difference_sections": value_difference_sections,
        "full_database_pass": full_database_pass,
        "interpretation": (
            "The input stage isolates adaptor projection differences. The computed stage adds iFP algorithm output. "
            "This report does not select either backend as a truth source and does not replace DEF semantic comparison."
        ),
        "exit_policy": {
            "0": "required snapshots have schema 2, exact values match modulo list order, and all index maps are self-consistent",
            "1": "comparison completed but a value difference or an index invariant violation remains",
            "2": "a required snapshot is unavailable/invalid or schema coverage is incomplete",
        },
    }


def _exit_code(report: dict[str, Any]) -> int:
    required = [report["stages"][name] for name in REQUIRED_STAGES]
    if any(stage["status"] == "unavailable" for stage in required) or not report["coverage_complete"]:
        return 2
    return 0 if report["full_database_pass"] else 1


def _parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pair-root", required=True, type=Path,
                        help="directory containing idb/ and eccdb/ snapshot directories")
    parser.add_argument("--output", type=Path,
                        help="report path; defaults to <pair-root>/fp-database-diff.json")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(argv)
    pair_root = args.pair_root.resolve()
    output = (args.output or pair_root / "fp-database-diff.json").resolve()
    report = build_report(pair_root)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({
        "output": str(output),
        "full_database_pass": report["full_database_pass"],
        "coverage_complete": report["coverage_complete"],
        "values_equal": report["values_equal"],
        "strict_equal": report["strict_equal"],
    }, ensure_ascii=False, sort_keys=True))
    return _exit_code(report)


if __name__ == "__main__":
    sys.exit(main())
