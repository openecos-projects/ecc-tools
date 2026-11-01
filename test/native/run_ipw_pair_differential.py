#!/usr/bin/env python3
# SPDX-License-Identifier: MulanPSL-2.0
"""Reproducible iPW iDB/EccDB differential runner and comparator.

The iPW, iDB, and EccDB singleton lifecycles make in-process A/B comparison
invalid.  This script therefore starts ``ipw_pair_probe`` once per provider
and compares the persisted model boundaries afterward.  It intentionally
does not normalize escaped identifiers, reorder connectivity vectors, or
discard duplicate values.  A report can call out an order-only difference,
but that is still a failed power-model comparison because multi-driver net
selection is order-sensitive in iPW.

The worker snapshots are intentionally split into two stages:

* ``input-model.json`` is the post-wrap iPW model before analysis.
* ``calculated-power.json`` is the post-analysis instance/net/summary model.

The optional ``--compare-only`` path validates each worker's effective source
paths against the requested case and records post-hoc SHA-256 provenance.
An externally launched worker does not record its hashes at start time, so the
report explicitly labels that limitation instead of claiming stale-artifact
proof it cannot provide.
"""

from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass, field
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
from typing import Any, Iterable, Sequence


BACKENDS = ("idb", "eccdb")
SNAPSHOT_SCHEMA = 1
MAX_SAMPLES = 24
POWER_ABS_TOLERANCE_W = 1.0e-15
POWER_REL_TOLERANCE = 1.0e-10


def _canonical(value: Any) -> str:
    """Lossless JSON identity used only for diagnostics, never normalization."""
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=True)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _file_metadata(path: Path) -> dict[str, Any]:
    result: dict[str, Any] = {"path": str(path), "exists": path.is_file()}
    if path.is_file():
        result["bytes"] = path.stat().st_size
        result["sha256"] = _sha256(path)
    return result


def _write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=True) + "\n", encoding="utf-8")


def _load_json(path: Path) -> tuple[dict[str, Any], Any | None]:
    if not path.is_file():
        return {"status": "missing", "path": str(path)}, None
    try:
        return {
            "status": "loaded",
            "path": str(path),
            "bytes": path.stat().st_size,
            "sha256": _sha256(path),
        }, json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        return {"status": "invalid", "path": str(path), "error": repr(error)}, None


def _resolve(base: Path, value: str | None) -> Path | None:
    if not value:
        return None
    path = Path(value)
    return (path if path.is_absolute() else base / path).resolve()


@dataclass(frozen=True)
class Case:
    name: str
    manifest: Path
    def_file: Path
    spef_file: Path | None = None
    vcd_file: Path | None = None


@dataclass
class Diff:
    missing_left: int = 0
    missing_right: int = 0
    type_mismatches: int = 0
    value_mismatches: int = 0
    numeric_outside_tolerance: int = 0
    nonfinite: int = 0
    order_only: int = 0
    exact_name_differences: int = 0
    samples: list[dict[str, Any]] = field(default_factory=list)

    def add(self, kind: str, path: str, left: Any = None, right: Any = None, **extra: Any) -> None:
        if kind == "missing_left":
            self.missing_left += 1
        elif kind == "missing_right":
            self.missing_right += 1
        elif kind == "type":
            self.type_mismatches += 1
        elif kind == "numeric_tolerance":
            self.numeric_outside_tolerance += 1
        elif kind == "nonfinite":
            self.nonfinite += 1
        elif kind == "order_only":
            self.order_only += 1
        elif kind == "exact_name":
            self.exact_name_differences += 1
        else:
            self.value_mismatches += 1
        if len(self.samples) < MAX_SAMPLES:
            sample: dict[str, Any] = {"kind": kind, "path": path}
            if left is not None:
                sample["idb"] = left
            if right is not None:
                sample["eccdb"] = right
            sample.update(extra)
            self.samples.append(sample)

    @property
    def passed(self) -> bool:
        return not any((self.missing_left, self.missing_right, self.type_mismatches,
                        self.value_mismatches, self.numeric_outside_tolerance,
                        self.nonfinite, self.order_only, self.exact_name_differences))

    def report(self) -> dict[str, Any]:
        return {
            "passed": self.passed,
            "missing_idb": self.missing_left,
            "missing_eccdb": self.missing_right,
            "type_mismatches": self.type_mismatches,
            "value_mismatches": self.value_mismatches,
            "numeric_outside_tolerance": self.numeric_outside_tolerance,
            "nonfinite_values": self.nonfinite,
            "order_only_differences": self.order_only,
            "exact_identifier_differences": self.exact_name_differences,
            "samples": self.samples,
        }


def _join(path: str, token: str) -> str:
    return f"{path}.{token}" if path else token


def _is_number(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def _numbers_equal(left: float, right: float, abs_tolerance: float, rel_tolerance: float) -> bool:
    return math.isfinite(left) and math.isfinite(right) and math.isclose(
        left, right, abs_tol=abs_tolerance, rel_tol=rel_tolerance
    )


def _array_order_only(left: list[Any], right: list[Any]) -> bool:
    if len(left) != len(right):
        return False
    return Counter(_canonical(value) for value in left) == Counter(_canonical(value) for value in right)


def _is_identifier_map_path(path: str) -> bool:
    """Whether the keys at ``path`` are design/library identifiers.

    This only makes raw reports easier to read.  It never matches keys with
    different spelling, so escaped identifiers remain a strict difference.
    """
    return path.rsplit(".", 1)[-1] in {
        "instances", "pins", "nets", "vcd_activity", "parasitics",
        "cells", "timing_constraints", "instance_power", "net_power",
        "power_activity",
    }


def _compare_values(
    left: Any,
    right: Any,
    path: str,
    diff: Diff,
    *,
    numeric_tolerance: tuple[float, float] | None,
    identifier_map: bool = False,
) -> None:
    """Compare exactly except explicit numeric result tolerance.

    ``identifier_map`` only changes reporting: map-key differences are raw
    identifier differences.  It deliberately does not attempt to match
    escaped and unescaped spellings.
    """
    if _is_number(left) and _is_number(right):
        left_float, right_float = float(left), float(right)
        if not math.isfinite(left_float) or not math.isfinite(right_float):
            diff.add("nonfinite", path, left, right)
            return
        if numeric_tolerance is None:
            if left != right:
                diff.add("value", path, left, right)
            return
        abs_tolerance, rel_tolerance = numeric_tolerance
        if not _numbers_equal(left_float, right_float, abs_tolerance, rel_tolerance):
            diff.add(
                "numeric_tolerance", path, left, right,
                absolute_difference=abs(left_float - right_float),
                abs_tolerance=abs_tolerance,
                relative_tolerance=rel_tolerance,
            )
        return

    if type(left) is not type(right):
        diff.add("type", path, type(left).__name__, type(right).__name__)
        return
    if isinstance(left, dict):
        identifier_map = identifier_map or _is_identifier_map_path(path)
        left_keys, right_keys = set(left), set(right)
        for key in sorted(left_keys - right_keys):
            kind = "exact_name" if identifier_map else "missing_eccdb"
            diff.add(kind, _join(path, str(key)), left[key], None, exact_identifier=str(key) if identifier_map else None)
        for key in sorted(right_keys - left_keys):
            kind = "exact_name" if identifier_map else "missing_idb"
            diff.add(kind, _join(path, str(key)), None, right[key], exact_identifier=str(key) if identifier_map else None)
        for key in sorted(left_keys & right_keys):
            _compare_values(left[key], right[key], _join(path, str(key)), diff,
                            numeric_tolerance=numeric_tolerance)
        return
    if isinstance(left, list):
        if len(left) != len(right):
            diff.add("value", path, {"length": len(left)}, {"length": len(right)})
        if left != right and _array_order_only(left, right):
            # Connectivity-vector order is semantically observable in iPW.
            diff.add("order_only", path, {"length": len(left)}, {"length": len(right)})
            return
        for index, (left_item, right_item) in enumerate(zip(left, right)):
            _compare_values(left_item, right_item, f"{path}[{index}]", diff,
                            numeric_tolerance=numeric_tolerance)
        return
    if left != right:
        diff.add("value", path, left, right)


def _find_nonfinite(value: Any, path: str = "") -> Iterable[tuple[str, float]]:
    if _is_number(value):
        numeric = float(value)
        if not math.isfinite(numeric):
            yield path, numeric
    elif isinstance(value, dict):
        for key, child in value.items():
            yield from _find_nonfinite(child, _join(path, str(key)))
    elif isinstance(value, list):
        for index, child in enumerate(value):
            yield from _find_nonfinite(child, f"{path}[{index}]")


def _power_value_invariant(value: Any, path: str, abs_tolerance: float, rel_tolerance: float) -> list[dict[str, Any]]:
    if not isinstance(value, dict) or set(value) != {"internal_w", "switching_w", "leakage_w", "total_w"}:
        return [{"path": path, "reason": "invalid_power_value", "value": value}]
    try:
        internal = float(value["internal_w"])
        switching = float(value["switching_w"])
        leakage = float(value["leakage_w"])
        total = float(value["total_w"])
    except (TypeError, ValueError):
        return [{"path": path, "reason": "non_numeric_power_value", "value": value}]
    if not all(math.isfinite(item) for item in (internal, switching, leakage, total)):
        return [{"path": path, "reason": "nonfinite_power_value", "value": value}]
    expected = internal + switching + leakage
    if not _numbers_equal(expected, total, abs_tolerance, rel_tolerance):
        return [{"path": path, "reason": "component_total_mismatch", "expected": expected, "actual": total}]
    return []


def _library_coverage(model: dict[str, Any]) -> dict[str, Any]:
    """Classify missing/zero-power Liberty coverage without hiding it.

    Tap, filler, and decap instances can legitimately have no signal pins or
    no dynamic/leakage model in the selected Liberty input.  They remain in
    the report as physical-only exclusions.  A missing library cell on an
    instance with signal pins is a wrapper failure and invalidates the input
    snapshot.
    """
    instances = model.get("instances")
    timing_library = model.get("timing_library")
    cells = timing_library.get("cells") if isinstance(timing_library, dict) else None
    if not isinstance(instances, dict) or not isinstance(cells, dict):
        return {
            "valid": False,
            "reason": "instances_or_timing_library_cells_not_objects",
            "missing_library_signal_bearing_instance_count": 0,
            "missing_library_physical_only_instance_count": 0,
            "zero_power_model_signal_bearing_instance_count": 0,
            "zero_power_model_physical_only_instance_count": 0,
        }

    missing_signal: list[tuple[str, str]] = []
    missing_physical: list[tuple[str, str]] = []
    zero_power_signal: list[tuple[str, str]] = []
    zero_power_physical: list[tuple[str, str]] = []
    malformed_instances: list[str] = []

    for instance_name, instance in instances.items():
        if not isinstance(instance, dict):
            malformed_instances.append(str(instance_name))
            continue
        cell_name = instance.get("cell")
        pin_names = instance.get("pins")
        if not isinstance(cell_name, str) or not isinstance(pin_names, list):
            malformed_instances.append(str(instance_name))
            continue
        signal_bearing = bool(pin_names)
        if cell_name not in cells:
            (missing_signal if signal_bearing else missing_physical).append((str(instance_name), cell_name))
            continue
        cell = cells[cell_name]
        if not isinstance(cell, dict):
            malformed_instances.append(str(instance_name))
            continue
        power_arcs = cell.get("power_arcs")
        leakage_entries = cell.get("leakage_entries")
        if not isinstance(power_arcs, int) or not isinstance(leakage_entries, int):
            malformed_instances.append(str(instance_name))
            continue
        if power_arcs == 0 and leakage_entries == 0:
            (zero_power_signal if signal_bearing else zero_power_physical).append((str(instance_name), cell_name))

    def summarize(entries: list[tuple[str, str]]) -> dict[str, Any]:
        by_cell = Counter(cell for _, cell in entries)
        return {
            "count": len(entries),
            "by_cell": dict(sorted(by_cell.items())),
            "instance_sample": [name for name, _ in entries[:MAX_SAMPLES]],
        }

    return {
        "valid": not malformed_instances and not missing_signal,
        "library_cell_count": len(cells),
        "missing_library_signal_bearing": summarize(missing_signal),
        "missing_library_physical_only": summarize(missing_physical),
        "zero_power_model_signal_bearing": summarize(zero_power_signal),
        "zero_power_model_physical_only": summarize(zero_power_physical),
        "malformed_instance_count": len(malformed_instances),
        "malformed_instance_sample": malformed_instances[:MAX_SAMPLES],
        "missing_library_signal_bearing_instance_count": len(missing_signal),
        "missing_library_physical_only_instance_count": len(missing_physical),
        "zero_power_model_signal_bearing_instance_count": len(zero_power_signal),
        "zero_power_model_physical_only_instance_count": len(zero_power_physical),
    }


def validate_input_model(model: Any) -> dict[str, Any]:
    """Reject partial or empty wrapper snapshots before a comparison can pass."""
    required = {
        "snapshot_schema", "design_name", "instances", "pins", "nets", "vcd_activity",
        "timing_library", "timing_constraints", "parasitics", "counts",
    }
    errors: list[dict[str, Any]] = []
    if not isinstance(model, dict):
        return {"valid": False, "errors": [{"reason": "not_object"}]}
    missing = sorted(required - set(model))
    if missing:
        errors.append({"reason": "missing_sections", "sections": missing})
    if model.get("snapshot_schema") != SNAPSHOT_SCHEMA:
        errors.append({"reason": "unsupported_schema", "actual": model.get("snapshot_schema"), "expected": SNAPSHOT_SCHEMA})
    for section in ("instances", "pins", "nets", "vcd_activity", "parasitics"):
        if section in model and not isinstance(model[section], dict):
            errors.append({"reason": "invalid_section_type", "section": section, "actual": type(model[section]).__name__})
    counts = model.get("counts")
    if not isinstance(counts, dict):
        errors.append({"reason": "invalid_counts"})
    else:
        for count_name, section in (("instances", "instances"), ("pins", "pins"), ("nets", "nets"),
                                    ("vcd_activity", "vcd_activity"), ("parasitic_nets", "parasitics")):
            actual = model.get(section)
            if isinstance(actual, dict) and counts.get(count_name) != len(actual):
                errors.append({"reason": "count_mismatch", "count": count_name,
                               "reported": counts.get(count_name), "actual": len(actual)})
    library = model.get("timing_library")
    if not isinstance(library, dict):
        errors.append({"reason": "missing_timing_library"})
    else:
        if not isinstance(library.get("cell_count"), int) or library["cell_count"] <= 0:
            errors.append({"reason": "missing_library_cells", "cell_count": library.get("cell_count")})
        if not isinstance(library.get("table_count"), int):
            errors.append({"reason": "missing_library_table_count"})
        if library.get("all_table_payloads_hashed") is not True:
            errors.append({"reason": "table_payload_not_hashed"})
        if not isinstance(library.get("semantic_fingerprint_fnv1a64"), str):
            errors.append({"reason": "missing_library_semantic_fingerprint"})
    for path, value in _find_nonfinite(model):
        if len(errors) < MAX_SAMPLES:
            errors.append({"reason": "nonfinite", "path": path, "value": repr(value)})
    coverage = _library_coverage(model)
    if not coverage["valid"]:
        if coverage.get("missing_library_signal_bearing_instance_count", 0):
            errors.append({
                "reason": "missing_library_for_signal_bearing_instances",
                "count": coverage["missing_library_signal_bearing_instance_count"],
                "sample": coverage["missing_library_signal_bearing"]["instance_sample"],
            })
        if coverage.get("malformed_instance_count", 0):
            errors.append({
                "reason": "malformed_instance_coverage_fields",
                "count": coverage["malformed_instance_count"],
                "sample": coverage["malformed_instance_sample"],
            })
        if coverage.get("reason"):
            errors.append({"reason": coverage["reason"]})
    return {"valid": not errors, "errors": errors[:MAX_SAMPLES], "library_coverage": coverage}


def validate_power_model(model: Any, abs_tolerance: float = POWER_ABS_TOLERANCE_W,
                         rel_tolerance: float = POWER_REL_TOLERANCE) -> dict[str, Any]:
    """Validate finite power data and accounting without clamping negative values."""
    errors: list[dict[str, Any]] = []
    negative_internal_count = 0
    negative_internal_min: float | None = None
    if not isinstance(model, dict):
        return {"valid": False, "errors": [{"reason": "not_object"}]}
    required = {"instance_power", "net_power", "power_activity", "summary_total", "summary_groups", "counts"}
    missing = sorted(required - set(model))
    if missing:
        errors.append({"reason": "missing_sections", "sections": missing})
    instance_power = model.get("instance_power")
    net_power = model.get("net_power")
    if not isinstance(instance_power, dict) or not isinstance(net_power, dict):
        errors.append({"reason": "invalid_power_maps"})
        instance_power = {} if not isinstance(instance_power, dict) else instance_power
        net_power = {} if not isinstance(net_power, dict) else net_power
    counts = model.get("counts")
    if not isinstance(counts, dict):
        errors.append({"reason": "invalid_counts"})
    else:
        for count_name, values in (("instance_power", instance_power), ("net_power", net_power),
                                   ("power_activity", model.get("power_activity"))):
            if isinstance(values, dict) and counts.get(count_name) != len(values):
                errors.append({"reason": "count_mismatch", "count": count_name,
                               "reported": counts.get(count_name), "actual": len(values)})
    if not instance_power:
        errors.append({"reason": "empty_instance_power"})
    if not net_power:
        errors.append({"reason": "empty_net_power"})

    component_sums = {"internal_w": 0.0, "switching_w": 0.0, "leakage_w": 0.0, "total_w": 0.0}
    for name, row in instance_power.items():
        if not isinstance(row, dict):
            errors.append({"reason": "invalid_instance_row", "instance": name})
            continue
        power = row.get("power")
        for error in _power_value_invariant(power, f"instance_power.{name}.power", abs_tolerance, rel_tolerance):
            errors.append(error)
        if isinstance(power, dict):
            for field in component_sums:
                if _is_number(power.get(field)):
                    component_sums[field] += float(power[field])
            internal = power.get("internal_w")
            if _is_number(internal) and float(internal) < 0.0:
                negative_internal_count += 1
                negative_internal_min = float(internal) if negative_internal_min is None else min(negative_internal_min, float(internal))
    for path, value in _find_nonfinite(model):
        if len(errors) < MAX_SAMPLES:
            errors.append({"reason": "nonfinite", "path": path, "value": repr(value)})
    for error in _power_value_invariant(model.get("summary_total"), "summary_total", abs_tolerance, rel_tolerance):
        errors.append(error)
    summary = model.get("summary_total")
    if isinstance(summary, dict):
        for field, expected in component_sums.items():
            actual = summary.get(field)
            if _is_number(actual) and not _numbers_equal(expected, float(actual), abs_tolerance, rel_tolerance):
                errors.append({"reason": "instance_sum_summary_mismatch", "field": field,
                               "instance_sum": expected, "summary": actual})
    groups = model.get("summary_groups")
    if not isinstance(groups, dict):
        errors.append({"reason": "invalid_summary_groups"})
    else:
        group_sums = {"internal_w": 0.0, "switching_w": 0.0, "leakage_w": 0.0, "total_w": 0.0}
        for group, power in groups.items():
            for error in _power_value_invariant(power, f"summary_groups.{group}", abs_tolerance, rel_tolerance):
                errors.append(error)
            if isinstance(power, dict):
                for field in group_sums:
                    if _is_number(power.get(field)):
                        group_sums[field] += float(power[field])
        if isinstance(summary, dict):
            for field, expected in group_sums.items():
                actual = summary.get(field)
                if _is_number(actual) and not _numbers_equal(expected, float(actual), abs_tolerance, rel_tolerance):
                    errors.append({"reason": "group_sum_summary_mismatch", "field": field,
                                   "group_sum": expected, "summary": actual})
    return {
        "valid": not errors,
        "errors": errors[:MAX_SAMPLES],
        "negative_internal_power_count": negative_internal_count,
        "negative_internal_power_min_w": negative_internal_min,
        "instance_component_sums": component_sums,
    }


def _without_sections(model: Any, sections: set[str]) -> Any:
    if not isinstance(model, dict):
        return model
    return {key: value for key, value in model.items() if key not in sections}


def _decode_source_def_name(token: str) -> str:
    """Decode escape *pairs* from an identifier token read from this DEF.

    This is deliberately not a generic snapshot-name normalizer.  The caller
    invokes it only for raw NETS tokens collected from the frozen source DEF.
    A trailing slash is retained rather than silently erased so malformed
    source text cannot create a false semantic match.
    """
    decoded: list[str] = []
    index = 0
    while index < len(token):
        character = token[index]
        if character == "\\" and index + 1 < len(token):
            decoded.append(token[index + 1])
            index += 2
        else:
            decoded.append(character)
            index += 1
    return "".join(decoded)


def _source_def_net_catalog(path: Path) -> dict[str, Any]:
    """Read raw NETS-name tokens and conservatively expose escape aliases."""
    raw_names: set[str] = set()
    in_nets = False
    try:
        with path.open(encoding="utf-8", errors="strict") as stream:
            for raw_line in stream:
                line = raw_line.strip()
                upper = line.upper()
                if not in_nets:
                    if upper.startswith("NETS "):
                        in_nets = True
                    continue
                if upper == "END NETS":
                    break
                if line.startswith("-"):
                    fields = line[1:].lstrip().split(maxsplit=1)
                    if fields:
                        raw_names.add(fields[0])
    except (OSError, UnicodeDecodeError) as error:
        return {"valid": False, "reason": repr(error), "path": str(path)}

    semantic_to_raw: dict[str, set[str]] = {}
    form_to_semantics: dict[str, set[str]] = {}
    for raw_name in raw_names:
        semantic = _decode_source_def_name(raw_name)
        semantic_to_raw.setdefault(semantic, set()).add(raw_name)
        # The only accepted forms are exact raw source token and its pairwise
        # decoded form.  Snapshot strings not present in either form receive
        # no alias at all.
        form_to_semantics.setdefault(raw_name, set()).add(semantic)
        form_to_semantics.setdefault(semantic, set()).add(semantic)
    collisions = {semantic: sorted(raw) for semantic, raw in semantic_to_raw.items() if len(raw) != 1}
    return {
        "valid": True,
        "path": str(path),
        "raw_net_count": len(raw_names),
        "escaped_source_token_count": sum("\\" in name for name in raw_names),
        "form_to_semantics": form_to_semantics,
        "semantic_to_raw": semantic_to_raw,
        "collisions": collisions,
    }


def _net_alias_report(idb_nets: Any, eccdb_nets: Any, source_def: Path | None,
                      *, numeric_tolerance: tuple[float, float] | None) -> dict[str, Any]:
    """Check whether unmatched net names are source-licensed aliases only.

    An alias is accepted only when it is derivable from one exact token in the
    frozen source DEF, maps one unmatched iDB name to one unmatched EccDB name,
    and the input-model connectivity records are byte-for-byte equivalent (or
    within the explicit numeric result tolerance for a power record).  This
    does not transform either snapshot and collisions remain a failure.
    """
    if not isinstance(idb_nets, dict) or not isinstance(eccdb_nets, dict):
        return {"valid": False, "reason": "net_sections_not_objects", "semantic_pass": False}
    if source_def is None:
        return {"valid": False, "reason": "missing_source_def", "semantic_pass": False}
    catalog = _source_def_net_catalog(source_def)
    if not catalog.get("valid"):
        return {"valid": False, "reason": "source_def_parse_failed", "catalog": catalog, "semantic_pass": False}
    idb_only = set(idb_nets) - set(eccdb_nets)
    eccdb_only = set(eccdb_nets) - set(idb_nets)
    form_to_semantics: dict[str, set[str]] = catalog["form_to_semantics"]
    collisions: dict[str, list[str]] = catalog["collisions"]
    idb_by_semantic: dict[str, list[str]] = {}
    eccdb_by_semantic: dict[str, list[str]] = {}
    unlicensed_idb: list[str] = []
    unlicensed_eccdb: list[str] = []
    ambiguous_snapshot_forms: dict[str, list[str]] = {}

    def collect(names: set[str], destination: dict[str, list[str]], unlicensed: list[str]) -> None:
        for name in names:
            semantics = form_to_semantics.get(name, set())
            if len(semantics) != 1:
                if semantics:
                    ambiguous_snapshot_forms[name] = sorted(semantics)
                else:
                    unlicensed.append(name)
                continue
            semantic = next(iter(semantics))
            if semantic in collisions:
                ambiguous_snapshot_forms[name] = collisions[semantic]
                continue
            destination.setdefault(semantic, []).append(name)

    collect(idb_only, idb_by_semantic, unlicensed_idb)
    collect(eccdb_only, eccdb_by_semantic, unlicensed_eccdb)
    paired: list[dict[str, Any]] = []
    payload_mismatches: list[dict[str, Any]] = []
    multiplicity_mismatches: list[dict[str, Any]] = []
    for semantic in sorted(set(idb_by_semantic) | set(eccdb_by_semantic)):
        left = sorted(idb_by_semantic.get(semantic, []))
        right = sorted(eccdb_by_semantic.get(semantic, []))
        if len(left) != 1 or len(right) != 1:
            multiplicity_mismatches.append({"semantic_name": semantic, "idb_names": left, "eccdb_names": right})
            continue
        payload_diff = Diff()
        _compare_values(idb_nets[left[0]], eccdb_nets[right[0]], f"net[{semantic!r}]", payload_diff,
                        numeric_tolerance=numeric_tolerance)
        raw_source = next(iter(catalog["semantic_to_raw"][semantic]))
        pair = {"source_token": raw_source, "semantic_name": semantic,
                "idb_name": left[0], "eccdb_name": right[0], "payload_equal": payload_diff.passed}
        paired.append(pair)
        if not payload_diff.passed:
            payload_mismatches.append({**pair, "payload_difference": payload_diff.report()})
    paired_idb = {pair["idb_name"] for pair in paired if pair["payload_equal"]}
    paired_eccdb = {pair["eccdb_name"] for pair in paired if pair["payload_equal"]}
    unresolved_idb = sorted(idb_only - paired_idb)
    unresolved_eccdb = sorted(eccdb_only - paired_eccdb)
    semantic_pass = not (unlicensed_idb or unlicensed_eccdb or ambiguous_snapshot_forms or multiplicity_mismatches
                         or payload_mismatches or unresolved_idb or unresolved_eccdb)
    return {
        "valid": True,
        "semantic_pass": semantic_pass,
        "source_catalog": {
            "path": catalog["path"], "raw_net_count": catalog["raw_net_count"],
            "escaped_source_token_count": catalog["escaped_source_token_count"],
            "source_decode_collision_count": len(collisions),
            "source_decode_collision_sample": dict(list(sorted(collisions.items()))[:MAX_SAMPLES]),
        },
        "raw_idb_only_count": len(idb_only),
        "raw_eccdb_only_count": len(eccdb_only),
        "paired_alias_count": len(paired_idb),
        "paired_alias_sample": paired[:MAX_SAMPLES],
        "unlicensed_idb_count": len(unlicensed_idb),
        "unlicensed_eccdb_count": len(unlicensed_eccdb),
        "unlicensed_idb_sample": sorted(unlicensed_idb)[:MAX_SAMPLES],
        "unlicensed_eccdb_sample": sorted(unlicensed_eccdb)[:MAX_SAMPLES],
        "ambiguous_snapshot_form_count": len(ambiguous_snapshot_forms),
        "ambiguous_snapshot_form_sample": dict(list(sorted(ambiguous_snapshot_forms.items()))[:MAX_SAMPLES]),
        "multiplicity_mismatch_count": len(multiplicity_mismatches),
        "multiplicity_mismatch_sample": multiplicity_mismatches[:MAX_SAMPLES],
        "payload_mismatch_count": len(payload_mismatches),
        "payload_mismatch_sample": payload_mismatches[:MAX_SAMPLES],
        "unresolved_idb_count": len(unresolved_idb),
        "unresolved_eccdb_count": len(unresolved_eccdb),
        "unresolved_idb_sample": unresolved_idb[:MAX_SAMPLES],
        "unresolved_eccdb_sample": unresolved_eccdb[:MAX_SAMPLES],
    }


def compare_input_models(idb: Any, eccdb: Any, *, source_def: Path | None = None) -> dict[str, Any]:
    diff = Diff()
    _compare_values(idb, eccdb, "", diff, numeric_tolerance=None)
    common_diff = Diff()
    _compare_values(_without_sections(idb, {"nets"}), _without_sections(eccdb, {"nets"}), "",
                    common_diff, numeric_tolerance=None)
    aliases = _net_alias_report(idb.get("nets") if isinstance(idb, dict) else None,
                                eccdb.get("nets") if isinstance(eccdb, dict) else None,
                                source_def, numeric_tolerance=None)
    return {
        "identity_policy": "exact raw JSON strings; escaped identifiers are not normalized",
        "sequence_policy": "all vectors are order-sensitive; order-only connectivity differences fail",
        "comparison": diff.report(),
        "non_net_comparison": common_diff.report(),
        "source_def_net_aliases": aliases,
        # This is diagnostic evidence only.  The raw comparison above remains
        # the pass/fail gate, including escaped names and pin net references.
        "source_alias_diagnostic_only": True,
        "idb_invariants": validate_input_model(idb),
        "eccdb_invariants": validate_input_model(eccdb),
    }


def compare_power_models(idb: Any, eccdb: Any, *, input_idb: Any = None, input_eccdb: Any = None,
                         source_def: Path | None = None, abs_tolerance: float = POWER_ABS_TOLERANCE_W,
                         rel_tolerance: float = POWER_REL_TOLERANCE) -> dict[str, Any]:
    diff = Diff()
    _compare_values(idb, eccdb, "", diff, numeric_tolerance=(abs_tolerance, rel_tolerance))
    common_diff = Diff()
    _compare_values(_without_sections(idb, {"net_power"}), _without_sections(eccdb, {"net_power"}), "",
                    common_diff, numeric_tolerance=(abs_tolerance, rel_tolerance))
    aliases = _net_alias_report(input_idb.get("nets") if isinstance(input_idb, dict) else None,
                                input_eccdb.get("nets") if isinstance(input_eccdb, dict) else None,
                                source_def, numeric_tolerance=None)
    # Validate the calculated per-net payload after a source-licensed alias
    # pairing.  The input pairing above proves the pin identities; this pass
    # proves the actual numeric power values attached to those same aliases.
    power_aliases = _net_alias_report(idb.get("net_power") if isinstance(idb, dict) else None,
                                      eccdb.get("net_power") if isinstance(eccdb, dict) else None,
                                      source_def,
                                      numeric_tolerance=(abs_tolerance, rel_tolerance))
    return {
        "numeric_policy": {"absolute_tolerance_w": abs_tolerance, "relative_tolerance": rel_tolerance},
        "comparison": diff.report(),
        "non_net_comparison": common_diff.report(),
        "source_def_input_net_aliases": aliases,
        "source_def_power_net_aliases": power_aliases,
        "source_alias_diagnostic_only": True,
        "idb_invariants": validate_power_model(idb, abs_tolerance, rel_tolerance),
        "eccdb_invariants": validate_power_model(eccdb, abs_tolerance, rel_tolerance),
    }


def _read_instance_tsv(path: Path) -> tuple[dict[str, Any], dict[str, dict[str, float]] | None]:
    if not path.is_file():
        return {"status": "missing", "path": str(path)}, None
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
        expected_header = [
            "instance_name", "voltage_v", "internal_power_w", "switching_power_w",
            "leakage_power_w", "total_power_w", "average_current_a",
        ]
        if len(lines) < 2 or lines[0] != "# iEMIR_INSTANCE_POWER_V1" or lines[1].split("\t") != expected_header:
            return {"status": "invalid", "path": str(path), "error": "unexpected_header"}, None
        rows: dict[str, dict[str, float]] = {}
        duplicate_names: list[str] = []
        for line_number, line in enumerate(lines[2:], start=3):
            fields = line.split("\t")
            if len(fields) != len(expected_header):
                return {"status": "invalid", "path": str(path), "error": f"column_count_line_{line_number}"}, None
            name = fields[0]
            if name in rows:
                duplicate_names.append(name)
            rows[name] = {header: float(value) for header, value in zip(expected_header[1:], fields[1:])}
        return {"status": "loaded", "path": str(path), "rows": len(rows), "duplicate_name_count": len(duplicate_names),
                "duplicate_name_sample": duplicate_names[:MAX_SAMPLES], "sha256": _sha256(path)}, rows
    except (OSError, ValueError) as error:
        return {"status": "invalid", "path": str(path), "error": repr(error)}, None


def compare_instance_tsv(idb_path: Path, eccdb_path: Path, *, abs_tolerance: float = POWER_ABS_TOLERANCE_W,
                         rel_tolerance: float = POWER_REL_TOLERANCE) -> dict[str, Any]:
    idb_meta, idb_rows = _read_instance_tsv(idb_path)
    eccdb_meta, eccdb_rows = _read_instance_tsv(eccdb_path)
    report: dict[str, Any] = {
        "numeric_policy": {"absolute_tolerance_w": abs_tolerance, "relative_tolerance": rel_tolerance},
        "idb": idb_meta,
        "eccdb": eccdb_meta,
    }
    if idb_rows is None or eccdb_rows is None:
        report["comparison"] = {"passed": False, "reason": "unavailable"}
        return report
    diff = Diff()
    _compare_values(idb_rows, eccdb_rows, "instance_power_tsv", diff,
                    numeric_tolerance=(abs_tolerance, rel_tolerance), identifier_map=True)
    invariants: list[dict[str, Any]] = []
    for backend, rows in (("idb", idb_rows), ("eccdb", eccdb_rows)):
        for name, row in rows.items():
            for field, value in row.items():
                if not math.isfinite(value):
                    invariants.append({"backend": backend, "instance": name, "field": field, "reason": "nonfinite"})
            expected = row["internal_power_w"] + row["switching_power_w"] + row["leakage_power_w"]
            if not _numbers_equal(expected, row["total_power_w"], abs_tolerance, rel_tolerance):
                invariants.append({"backend": backend, "instance": name, "reason": "component_total_mismatch"})
            expected_current = row["total_power_w"] / row["voltage_v"] if row["voltage_v"] > 0 else 0.0
            if not _numbers_equal(expected_current, row["average_current_a"], abs_tolerance, rel_tolerance):
                invariants.append({"backend": backend, "instance": name, "reason": "current_mismatch"})
            if len(invariants) >= MAX_SAMPLES:
                break
    report["comparison"] = diff.report()
    report["invariants"] = {"valid": not invariants, "errors": invariants[:MAX_SAMPLES]}
    return report


def _stage_pass(stage: dict[str, Any]) -> bool:
    comparison = stage.get("comparison", {})
    return bool(comparison.get("passed")) and bool(stage.get("idb_invariants", {}).get("valid")) and bool(stage.get("eccdb_invariants", {}).get("valid"))


def _tsv_pass(stage: dict[str, Any]) -> bool:
    return bool(stage.get("comparison", {}).get("passed")) and bool(stage.get("invariants", {}).get("valid"))


def _parse_manifest(manifest: Path, def_file: Path, spef_file: Path | None, vcd_file: Path | None) -> dict[str, Any]:
    value = json.loads(manifest.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"manifest must be an object: {manifest}")
    base = manifest.parent
    lefs = [_resolve(base, item) for item in value.get("lef_files", [])]
    liberty = [_resolve(base, item) for item in value.get("liberty_files", [])]
    sdc = _resolve(base, value.get("sdc_file"))
    if not lefs or not liberty:
        raise ValueError(f"manifest requires lef_files and liberty_files: {manifest}")
    return {
        "manifest": manifest.resolve(),
        "lefs": [path for path in lefs if path is not None],
        "liberty": [path for path in liberty if path is not None],
        "def": def_file.resolve(),
        "sdc": sdc,
        "spef": spef_file.resolve() if spef_file else None,
        "vcd": vcd_file.resolve() if vcd_file else None,
    }


def _source_metadata(spec: dict[str, Any]) -> dict[str, Any]:
    return {
        "manifest": _file_metadata(spec["manifest"]),
        "lef_files": [_file_metadata(path) for path in spec["lefs"]],
        "liberty_files": [_file_metadata(path) for path in spec["liberty"]],
        "def_file": _file_metadata(spec["def"]),
        "sdc_file": _file_metadata(spec["sdc"]) if spec["sdc"] else None,
        "spef_file": _file_metadata(spec["spef"]) if spec["spef"] else None,
        "vcd_file": _file_metadata(spec["vcd"]) if spec["vcd"] else None,
    }


def _worker_input_matches(result: Any, spec: dict[str, Any]) -> dict[str, Any]:
    if not isinstance(result, dict):
        return {"valid": False, "reason": "missing_result"}
    if result.get("status") != "passed":
        return {"valid": False, "reason": "worker_failed", "status": result.get("status"), "error": result.get("error")}
    input_value = result.get("input")
    if not isinstance(input_value, dict):
        return {"valid": False, "reason": "missing_worker_input"}
    expected = {
        "lef_files": [str(path) for path in spec["lefs"]],
        "liberty_files": [str(path) for path in spec["liberty"]],
        "def_file": str(spec["def"]),
        "sdc_file": str(spec["sdc"]) if spec["sdc"] else "",
        "spef_file": str(spec["spef"]) if spec["spef"] else "",
        "vcd_file": str(spec["vcd"]) if spec["vcd"] else "",
    }
    differences: dict[str, Any] = {}
    path_aliases: dict[str, Any] = {}

    def same_path(expected_path: str, actual_path: Any) -> tuple[bool, dict[str, str] | None]:
        if not isinstance(actual_path, str):
            return False, None
        if expected_path == actual_path:
            return True, None
        # A worker may preserve a user-facing symlink while the manifest was
        # canonicalized.  Accept only if both filesystem paths resolve to the
        # same target; retain the raw alias in the report for provenance.
        expected_resolved = str(Path(expected_path).resolve(strict=False))
        actual_resolved = str(Path(actual_path).resolve(strict=False))
        if expected_resolved == actual_resolved:
            return True, {
                "expected_raw": expected_path,
                "actual_raw": actual_path,
                "resolved": expected_resolved,
            }
        return False, None

    for key, expected_value in expected.items():
        actual_value = input_value.get(key)
        if isinstance(expected_value, list):
            if not isinstance(actual_value, list) or len(expected_value) != len(actual_value):
                differences[key] = {"expected": expected_value, "actual": actual_value}
                continue
            aliases: list[dict[str, str]] = []
            equal = True
            for index, (expected_path, actual_path) in enumerate(zip(expected_value, actual_value)):
                matched, alias = same_path(expected_path, actual_path)
                if not matched:
                    equal = False
                    break
                if alias:
                    aliases.append({"index": str(index), **alias})
            if not equal:
                differences[key] = {"expected": expected_value, "actual": actual_value}
            elif aliases:
                path_aliases[key] = aliases
            continue
        if expected_value == "" and actual_value == "":
            continue
        matched, alias = same_path(expected_value, actual_value)
        if not matched:
            differences[key] = {"expected": expected_value, "actual": actual_value}
        elif alias:
            path_aliases[key] = alias
    return {"valid": not differences, "differences": differences, "path_aliases": path_aliases}


def _worker_metrics(result: Any) -> dict[str, Any]:
    """Extract comparable worker cost metadata without inventing a benchmark."""
    if not isinstance(result, dict):
        return {"available": False, "reason": "missing_result"}
    timing = result.get("timing_ms")
    memory = result.get("memory")
    timing_values: dict[str, float] = {}
    if isinstance(timing, dict):
        for name, value in timing.items():
            if _is_number(value) and math.isfinite(float(value)):
                timing_values[str(name)] = float(value)
    memory_values: dict[str, int] = {}
    if isinstance(memory, dict):
        for name, value in memory.items():
            if isinstance(value, int) and value >= 0:
                memory_values[str(name)] = value
    return {
        "available": bool(timing_values or memory_values),
        "status": result.get("status"),
        "backend": result.get("backend"),
        "threads": result.get("threads"),
        "power_sim_seed": result.get("power_sim_seed"),
        "timing_ms": timing_values,
        "timing_total_ms": sum(timing_values.values()),
        "memory": memory_values,
    }


def _run_worker(binary: Path, backend: str, case: Case, output: Path, threads: int, seed: str,
                source_at_start: dict[str, Any]) -> dict[str, Any]:
    command = [str(binary), backend, str(case.manifest), str(output), f"--def-file={case.def_file}",
               f"--threads={threads}", f"--seed={seed}"]
    if case.spef_file:
        command.append(f"--spef-file={case.spef_file}")
    if case.vcd_file:
        command.append(f"--vcd-file={case.vcd_file}")
    output.mkdir(parents=True, exist_ok=False)
    _write_json(output / "source-inputs-at-start.json", source_at_start)
    environment = os.environ.copy()
    environment["IPW_POWER_SIM_SEED"] = seed
    begin = time.monotonic()
    process = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             env=environment, check=False)
    (output / "probe.stdout.log").write_text(process.stdout, encoding="utf-8")
    (output / "probe.stderr.log").write_text(process.stderr, encoding="utf-8")
    return {"command": command, "exit_code": process.returncode,
            "elapsed_s": time.monotonic() - begin,
            "stdout": _file_metadata(output / "probe.stdout.log"),
            "stderr": _file_metadata(output / "probe.stderr.log")}


def build_case_report(case: Case, root: Path, *, threads: int, seed: str,
                      abs_tolerance: float = POWER_ABS_TOLERANCE_W,
                      rel_tolerance: float = POWER_REL_TOLERANCE) -> dict[str, Any]:
    """Compare one persisted pair; safe for independently launched workers."""
    spec = _parse_manifest(case.manifest, case.def_file, case.spef_file, case.vcd_file)
    source_now = _source_metadata(spec)
    report: dict[str, Any] = {
        "case": case.name,
        "source": source_now,
        "provenance": {
            "source_hashes": "posthoc_current_files",
            "worker_start_hashes_available": False,
            "note": "Worker result.json records effective paths, not source hashes. Compare-only checks those paths and records current hashes; it cannot prove files were unchanged after an externally launched worker completed.",
        },
        "threads": threads,
        "power_sim_seed": seed,
        "backends": {},
    }
    loaded: dict[str, dict[str, Any]] = {}
    coverage_complete = True
    for backend in BACKENDS:
        backend_dir = root / case.name / backend
        backend_report: dict[str, Any] = {}
        result_meta, result = _load_json(backend_dir / "result.json")
        backend_report["result"] = result_meta
        backend_report["worker_metrics"] = _worker_metrics(result)
        backend_report["source_matches_worker"] = _worker_input_matches(result, spec)
        if not backend_report["source_matches_worker"]["valid"]:
            coverage_complete = False
        artifacts: dict[str, Any] = {}
        for name in ("input-model.json", "calculated-power.json"):
            metadata, value = _load_json(backend_dir / name)
            artifacts[name] = metadata
            if value is None:
                coverage_complete = False
            else:
                loaded.setdefault(name, {})[backend] = value
        backend_report["artifacts"] = artifacts
        start_meta, start_value = _load_json(backend_dir / "source-inputs-at-start.json")
        backend_report["source_at_worker_start"] = start_meta
        if start_value is not None:
            backend_report["source_at_worker_start_matches_current"] = start_value == source_now
            if start_value != source_now:
                coverage_complete = False
        report["backends"][backend] = backend_report

    start_hashes_available = all(
        report["backends"][backend]["source_at_worker_start"].get("status") == "loaded"
        for backend in BACKENDS
    )
    report["provenance"]["worker_start_hashes_available"] = start_hashes_available
    if start_hashes_available:
        report["provenance"]["note"] = "Worker start-time source hashes and current source hashes agree."

    if not coverage_complete or any(set(loaded.get(name, {})) != set(BACKENDS)
                                    for name in ("input-model.json", "calculated-power.json")):
        report["coverage_complete"] = False
        report["status"] = "unavailable"
        return report

    report["coverage_complete"] = True
    input_comparison = compare_input_models(loaded["input-model.json"]["idb"], loaded["input-model.json"]["eccdb"],
                                            source_def=spec["def"])
    power_comparison = compare_power_models(loaded["calculated-power.json"]["idb"], loaded["calculated-power.json"]["eccdb"],
                                            input_idb=loaded["input-model.json"]["idb"],
                                            input_eccdb=loaded["input-model.json"]["eccdb"], source_def=spec["def"],
                                            abs_tolerance=abs_tolerance, rel_tolerance=rel_tolerance)
    report["input_model"] = input_comparison
    report["calculated_power"] = power_comparison
    tsv = compare_instance_tsv(
        root / case.name / "idb" / "work" / "power_reporter" / "instance_power.tsv",
        root / case.name / "eccdb" / "work" / "power_reporter" / "instance_power.tsv",
        abs_tolerance=abs_tolerance,
        rel_tolerance=rel_tolerance,
    )
    report["instance_power_tsv"] = tsv
    report["passed"] = _stage_pass(input_comparison) and _stage_pass(power_comparison) and _tsv_pass(tsv)
    report["status"] = "passed" if report["passed"] else "different"
    return report


def _resource_summary(reports: Sequence[dict[str, Any]]) -> list[dict[str, Any]]:
    """Flat, machine-readable timings/RSS for the existing fresh workers."""
    rows: list[dict[str, Any]] = []
    for report in reports:
        for backend in BACKENDS:
            metrics = report.get("backends", {}).get(backend, {}).get("worker_metrics", {})
            rows.append({
                "case": report.get("case"),
                "backend": backend,
                **metrics,
            })
    return rows


def _case_from_values(values: Sequence[str], spef: str | None, vcd: str | None) -> Case:
    name, manifest_raw, def_raw = values
    manifest = Path(manifest_raw).resolve()
    return Case(name=name, manifest=manifest, def_file=Path(def_raw).resolve(),
                spef_file=Path(spef).resolve() if spef else None,
                vcd_file=Path(vcd).resolve() if vcd else None)


def _case_override(values: Sequence[str] | None) -> dict[str, str]:
    result: dict[str, str] = {}
    for value in values or ():
        if "=" not in value:
            raise ValueError("case file overrides use CASE=PATH")
        name, path = value.split("=", 1)
        if not name or not path:
            raise ValueError("case file overrides use CASE=PATH")
        if name in result:
            raise ValueError(f"duplicate override for {name}")
        result[name] = path
    return result


def parse_arguments(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path,
                        default=Path("build/upstream-integrate-20261013/bin/ipw_pair_probe"),
                        help="ipw_pair_probe binary for a fresh run")
    parser.add_argument("--output-root", type=Path, default=Path("artifacts/ipw-differential-20261022"))
    parser.add_argument("--case", nargs=3, action="append", metavar=("NAME", "MANIFEST", "DEF"), required=True,
                        help="repeatable frozen case specification")
    parser.add_argument("--case-spef", action="append", metavar="CASE=PATH",
                        help="optional SPEF per named case")
    parser.add_argument("--case-vcd", action="append", metavar="CASE=PATH",
                        help="optional VCD per named case")
    parser.add_argument("--threads", type=int, default=2)
    parser.add_argument("--seed", default="20261022")
    parser.add_argument("--absolute-tolerance-w", type=float, default=POWER_ABS_TOLERANCE_W)
    parser.add_argument("--relative-tolerance", type=float, default=POWER_REL_TOLERANCE)
    parser.add_argument("--build", action="store_true", help="build ipw_pair_probe before running")
    parser.add_argument("--build-dir", type=Path, default=Path("build/upstream-integrate-20261013"))
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--compare-only", action="store_true", help="compare already persisted workers only")
    parser.add_argument("--replace", action="store_true", help="remove an existing named case only before a fresh run")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    options = parse_arguments(argv)
    if options.threads <= 0 or options.jobs <= 0:
        raise SystemExit("--threads and --jobs must be positive")
    if options.absolute_tolerance_w < 0.0 or options.relative_tolerance < 0.0:
        raise SystemExit("numeric tolerances must be non-negative")
    try:
        spef = _case_override(options.case_spef)
        vcd = _case_override(options.case_vcd)
        cases = [_case_from_values(value, spef.get(value[0]), vcd.get(value[0])) for value in options.case]
    except ValueError as error:
        raise SystemExit(str(error)) from error
    names = [case.name for case in cases]
    if len(names) != len(set(names)):
        raise SystemExit("case names must be unique")
    if set(spef) - set(names) or set(vcd) - set(names):
        raise SystemExit("a --case-spef/--case-vcd name does not match --case")

    root = options.output_root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    invocation: dict[str, Any] = {
        "script": str(Path(__file__).resolve()),
        "binary": str(options.binary.resolve()),
        "compare_only": options.compare_only,
        "threads": options.threads,
        "power_sim_seed": options.seed,
        "numeric_policy": {"absolute_tolerance_w": options.absolute_tolerance_w,
                           "relative_tolerance": options.relative_tolerance},
        "cases": [],
    }
    if options.build:
        command = ["cmake", "--build", str(options.build_dir.resolve()), "--target", "ipw_pair_probe", "-j", str(options.jobs)]
        completed = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        build_dir = root / "build"
        build_dir.mkdir(exist_ok=True)
        (build_dir / "stdout.log").write_text(completed.stdout, encoding="utf-8")
        (build_dir / "stderr.log").write_text(completed.stderr, encoding="utf-8")
        invocation["build"] = {"command": command, "exit_code": completed.returncode}
        if completed.returncode != 0:
            _write_json(root / "invocation.json", invocation)
            return 2

    for case in cases:
        case_root = root / case.name
        if not options.compare_only:
            if case_root.exists():
                if not options.replace:
                    raise SystemExit(f"existing case artifact requires --replace or --compare-only: {case_root}")
                shutil.rmtree(case_root)
            if not options.binary.is_file():
                raise SystemExit(f"missing probe binary: {options.binary}")
            source_at_start = _source_metadata(_parse_manifest(case.manifest, case.def_file, case.spef_file, case.vcd_file))
            worker_runs = {}
            for backend in BACKENDS:
                worker_runs[backend] = _run_worker(options.binary.resolve(), backend, case,
                                                    case_root / backend, options.threads, options.seed, source_at_start)
            invocation["cases"].append({"name": case.name, "workers": worker_runs})
        else:
            invocation["cases"].append({"name": case.name, "workers": "externally_launched"})
    _write_json(root / "invocation.json", invocation)

    reports = []
    for case in cases:
        report = build_case_report(case, root, threads=options.threads, seed=options.seed,
                                   abs_tolerance=options.absolute_tolerance_w,
                                   rel_tolerance=options.relative_tolerance)
        _write_json(root / case.name / "comparison.json", report)
        reports.append(report)
    summary = {
        "schema": 1,
        "cases": [{"name": report["case"], "status": report["status"], "passed": report.get("passed", False),
                    "coverage_complete": report.get("coverage_complete", False)} for report in reports],
        "resource_summary": _resource_summary(reports),
    }
    summary["passed"] = bool(reports) and all(report.get("passed", False) for report in reports)
    summary["coverage_complete"] = bool(reports) and all(report.get("coverage_complete", False) for report in reports)
    _write_json(root / "summary.json", summary)
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0 if summary["passed"] else (2 if not summary["coverage_complete"] else 1)


if __name__ == "__main__":
    raise SystemExit(main())
