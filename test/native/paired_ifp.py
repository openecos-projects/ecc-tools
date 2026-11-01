#!/usr/bin/env python3
# SPDX-License-Identifier: MulanPSL-2.0
"""Run one iFP binary against iDB and EccDB, then preserve both projections.

The two iFP/database stacks are process-global.  This launcher therefore starts
one fresh worker process per backend.  It deliberately does not delete or reuse
an output directory: every invocation is an auditable experiment.

The worker contract is::

    ifp_worker <idb|eccdb> <manifest.json> <backend-output-directory>

Each worker writes ``input-model.json``, ``computed-model.json``,
``import.def``, ``output.def``, and ``result.json`` under its own output
directory.  Model comparison keeps strings, numeric values, escaping, duplicate
array entries, and provider order visible.  Named object arrays additionally
receive an order-independent comparison by exact ``name``; this distinguishes a
real object mismatch from provider iteration order without changing either
model.  ``cellarea`` is the sole tolerated floating-point field, reported with
its original values and an explicit absolute epsilon.
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from typing import Any


BACKENDS = ("idb", "eccdb")
MODEL_FILES = ("input-model.json", "computed-model.json")
BASE_WORKER_ARTIFACTS = (*MODEL_FILES, "import.def", "output.def", "result.json")
IO_ORDER_CONTROL_ARTIFACTS = ("controlled-input-model.json", "io-order-control.json")
IO_ORDER_CONTROL_CHOICES = ("none", "stable-exact-name")
MAX_SAMPLES = 40
MAX_ARRAY_SAMPLES = 12


def _json_dump(path: Path, value: Any) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _file_metadata(path: Path) -> dict[str, Any]:
    return {
        "path": str(path),
        "exists": path.is_file(),
        **({"bytes": path.stat().st_size, "sha256": _sha256(path)} if path.is_file() else {}),
    }


def _io_order_control_mode(value: Any, *, source: str) -> str:
    """Read the explicit test-only IO ordering mode from persisted metadata."""
    if value is None:
        return "none"
    if isinstance(value, dict):
        value = value.get("mode")
    if not isinstance(value, str) or value not in IO_ORDER_CONTROL_CHOICES:
        raise ValueError(f"{source} has an unsupported io-order-control mode")
    return value


def _worker_artifact_names(io_order_control: str = "none") -> tuple[str, ...]:
    if io_order_control not in IO_ORDER_CONTROL_CHOICES:
        raise ValueError(f"unsupported io-order-control mode: {io_order_control}")
    return (*BASE_WORKER_ARTIFACTS,
            *(IO_ORDER_CONTROL_ARTIFACTS if io_order_control != "none" else ()))


def _name_summary(names: list[str]) -> dict[str, Any]:
    """Keep comparison JSON bounded while the full exact names stay in the artifact."""
    encoded = json.dumps(names, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    return {
        "count": len(names),
        "sha256": hashlib.sha256(encoded).hexdigest(),
        "sample": names[:20],
    }


def _manifest_inputs(manifest: Path, value: dict[str, Any]) -> dict[str, Any]:
    def metadata(raw: str) -> dict[str, Any]:
        path = Path(raw)
        path = (path if path.is_absolute() else manifest.parent / path).resolve()
        if not path.is_file():
            raise ValueError(f"manifest input does not exist: {path}")
        return _file_metadata(path)

    return {
        "def": metadata(value["def_file"]),
        "lefs": [metadata(raw) for raw in value["lef_files"]],
        "config": metadata(value["config"]),
    }


def _validate_saved_inputs(
    root: Path,
    manifest: Path,
    current_inputs: dict[str, Any],
    previous: dict[str, Any],
    io_order_control: str = "none",
) -> dict[str, Any]:
    if not isinstance(previous, dict):
        raise ValueError("saved comparison is unavailable")
    _, invocation = _load_json(root / "invocation.json")
    if not isinstance(invocation, dict) or invocation.get("manifest", {}).get("sha256") != _sha256(manifest):
        raise ValueError("saved invocation is missing or manifest hash changed")
    saved_control = _io_order_control_mode(invocation.get("io_order_control"), source="saved invocation")
    if saved_control != io_order_control:
        raise ValueError(
            "saved io-order-control mode does not match compare-only request: "
            f"saved={saved_control}, requested={io_order_control}"
        )
    if io_order_control != "none":
        control_report = previous.get("io_order_control")
        if (not isinstance(control_report, dict)
                or control_report.get("mode") != io_order_control
                or control_report.get("status") != "applied"):
            raise ValueError("saved controlled IO-order experiment is missing or was not valid")
    saved_inputs = invocation.get("inputs")
    source = "invocation.json"
    if not isinstance(saved_inputs, dict):
        # Older runs recorded their inputs in the preparation catalog. Verify
        # those original hashes, rather than inventing provenance retroactively.
        catalog = root.parent.parent / "cases.json"
        _, entries = _load_json(catalog)
        entry = next((item for item in entries if isinstance(item, dict)
                      and Path(item.get("manifest", "")).resolve() == manifest), None) if isinstance(entries, list) else None
        if entry is None:
            raise ValueError("saved source hashes are unavailable; compare-only provenance cannot be verified")
        expected_lefs = entry.get("input", {})
        if (current_inputs["def"]["sha256"] != entry.get("def_sha256")
                or current_inputs["config"]["sha256"] != entry.get("config_sha256")
                or any(item["sha256"] != expected_lefs.get(item["path"]) for item in current_inputs["lefs"])):
            raise ValueError("saved DEF/LEF/config source hash changed")
        source = str(catalog)
    elif saved_inputs != current_inputs:
        raise ValueError("saved DEF/LEF/config source metadata changed")

    saved_workers = previous.get("workers", {})
    for backend in BACKENDS:
        saved_artifacts = saved_workers.get(backend, {}).get("artifacts", {})
        for name in _worker_artifact_names(io_order_control):
            path = root / backend / name
            expected_hash = saved_artifacts.get(name, {}).get("sha256")
            if not expected_hash or not path.is_file() or expected_hash != _sha256(path):
                raise ValueError(f"saved worker artifact is missing or changed: {backend}/{name}")
    return {"status": "verified", "source_hash_record": source,
            "worker_artifact_hash_record": str(root / "comparison.json")}


def _canonical(value: Any) -> str:
    """A lossless JSON key: it intentionally preserves strings and duplicates."""
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False)


def _preview(value: Any, limit: int = 600) -> Any:
    """Bound report size without changing the values used for comparison."""
    rendered = _canonical(value)
    if len(rendered) <= limit:
        return value
    return {"preview": rendered[:limit] + "...", "serialized_bytes": len(rendered)}


def _path_join(path: str, token: str) -> str:
    return f"{path}.{token}" if path else token


def _is_cellarea(path: str) -> bool:
    key = path.rsplit(".", 1)[-1].lower()
    return key in {"cellarea", "cell_area"}


@dataclass
class DiffStats:
    object_nodes: int = 0
    array_nodes: int = 0
    scalar_nodes: int = 0
    equal_nodes: int = 0
    type_mismatches: int = 0
    missing_left: int = 0
    missing_right: int = 0
    scalar_mismatches: int = 0
    array_order_mismatches: int = 0
    array_multiset_mismatches: int = 0
    named_array_nodes: int = 0
    cellarea_tolerated: int = 0
    cellarea_outside_epsilon: int = 0
    samples: list[dict[str, Any]] = field(default_factory=list)
    array_samples: list[dict[str, Any]] = field(default_factory=list)

    def sample(self, item: dict[str, Any]) -> None:
        if len(self.samples) < MAX_SAMPLES:
            self.samples.append(item)

    def array_sample(self, item: dict[str, Any]) -> None:
        if len(self.array_samples) < MAX_ARRAY_SAMPLES:
            self.array_samples.append(item)

    def report(self) -> dict[str, Any]:
        return {
            "object_nodes": self.object_nodes,
            "array_nodes": self.array_nodes,
            "scalar_nodes": self.scalar_nodes,
            "equal_nodes": self.equal_nodes,
            "type_mismatches": self.type_mismatches,
            "missing_left": self.missing_left,
            "missing_right": self.missing_right,
            "scalar_mismatches": self.scalar_mismatches,
            "array_order_mismatches": self.array_order_mismatches,
            "array_multiset_mismatches": self.array_multiset_mismatches,
            "named_array_nodes": self.named_array_nodes,
            "cellarea_tolerated": self.cellarea_tolerated,
            "cellarea_outside_epsilon": self.cellarea_outside_epsilon,
            "samples": self.samples,
            "array_samples": self.array_samples,
        }


def _all_named_objects(values: list[Any]) -> bool:
    return all(isinstance(value, dict) and isinstance(value.get("name"), str) for value in values)


def _record_multiset_difference(
    stats: DiffStats, path: str, left: list[Any], right: list[Any], order_equal: bool
) -> None:
    left_counts = Counter(_canonical(value) for value in left)
    right_counts = Counter(_canonical(value) for value in right)
    multiset_equal = left_counts == right_counts
    if not order_equal:
        stats.array_order_mismatches += 1
    if not multiset_equal:
        stats.array_multiset_mismatches += 1
    if order_equal and multiset_equal:
        return
    left_only = list((left_counts - right_counts).items())[:3]
    right_only = list((right_counts - left_counts).items())[:3]
    first_order_differences = []
    if not order_equal:
        for index, (a, b) in enumerate(zip(left, right)):
            if _canonical(a) != _canonical(b):
                first_order_differences.append({"index": index, "left": _preview(a), "right": _preview(b)})
            if len(first_order_differences) == 3:
                break
    stats.array_sample({
        "path": path,
        "order_equal": order_equal,
        "multiset_equal": multiset_equal,
        "left_count": len(left),
        "right_count": len(right),
        "left_only_count": sum((left_counts - right_counts).values()),
        "right_only_count": sum((right_counts - left_counts).values()),
        "left_only_serialized_sample": [item for item, _ in left_only],
        "right_only_serialized_sample": [item for item, _ in right_only],
        "first_order_differences": first_order_differences,
    })


def _compare_named_array(
    left: list[Any], right: list[Any], path: str, stats: DiffStats, cellarea_epsilon: float
) -> bool:
    """Compare named objects independent of provider order, retaining duplicates."""
    left_by_name: dict[str, list[dict[str, Any]]] = defaultdict(list)
    right_by_name: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for value in left:
        left_by_name[value["name"]].append(value)
    for value in right:
        right_by_name[value["name"]].append(value)

    equal = True
    for name in sorted(left_by_name.keys() | right_by_name.keys()):
        a_values = sorted(left_by_name.get(name, []), key=_canonical)
        b_values = sorted(right_by_name.get(name, []), key=_canonical)
        if len(a_values) != len(b_values):
            equal = False
            stats.sample({
                "kind": "named_object_multiplicity",
                "path": f'{path}[name={json.dumps(name, ensure_ascii=False)}]',
                "left_count": len(a_values),
                "right_count": len(b_values),
            })
        for index, (a_value, b_value) in enumerate(zip(a_values, b_values)):
            if not _compare_json(a_value, b_value, f'{path}[name={json.dumps(name, ensure_ascii=False)}][{index}]', stats, cellarea_epsilon):
                equal = False
    return equal


def _compare_json(left: Any, right: Any, path: str, stats: DiffStats, cellarea_epsilon: float) -> bool:
    if isinstance(left, dict) and isinstance(right, dict):
        stats.object_nodes += 1
        equal = True
        for key in sorted(left.keys() | right.keys()):
            child_path = _path_join(path, key)
            if key not in left:
                stats.missing_left += 1
                stats.sample({"kind": "missing_left_key", "path": child_path, "right": _preview(right[key])})
                equal = False
            elif key not in right:
                stats.missing_right += 1
                stats.sample({"kind": "missing_right_key", "path": child_path, "left": _preview(left[key])})
                equal = False
            elif not _compare_json(left[key], right[key], child_path, stats, cellarea_epsilon):
                equal = False
        if equal:
            stats.equal_nodes += 1
        return equal

    if isinstance(left, list) and isinstance(right, list):
        stats.array_nodes += 1
        order_equal = _canonical(left) == _canonical(right)
        _record_multiset_difference(stats, path, left, right, order_equal)
        if _all_named_objects(left) and _all_named_objects(right):
            stats.named_array_nodes += 1
            equal = _compare_named_array(left, right, path, stats, cellarea_epsilon)
        else:
            equal = len(left) == len(right)
            if len(left) != len(right):
                stats.sample({"kind": "array_length", "path": path, "left_count": len(left), "right_count": len(right)})
            # Positional objects such as a pin's port/shape list intentionally
            # retain order.  A provider-order discrepancy remains visible.
            for index, (a_value, b_value) in enumerate(zip(left, right)):
                if not _compare_json(a_value, b_value, f"{path}[{index}]", stats, cellarea_epsilon):
                    equal = False
        if equal:
            stats.equal_nodes += 1
        return equal

    stats.scalar_nodes += 1
    if (_is_cellarea(path) and isinstance(left, (int, float)) and not isinstance(left, bool)
            and isinstance(right, (int, float)) and not isinstance(right, bool)):
        delta = abs(float(left) - float(right))
        if math.isfinite(left) and math.isfinite(right) and delta <= cellarea_epsilon:
            stats.cellarea_tolerated += 1
            if left != right:
                stats.sample({"kind": "cellarea_within_epsilon", "path": path, "left": left, "right": right,
                              "absolute_difference": delta, "epsilon": cellarea_epsilon})
            stats.equal_nodes += 1
            return True
        stats.cellarea_outside_epsilon += 1
        stats.scalar_mismatches += 1
        stats.sample({"kind": "cellarea_outside_epsilon", "path": path, "left": left, "right": right,
                      "absolute_difference": delta, "epsilon": cellarea_epsilon})
        return False

    if type(left) is not type(right):
        stats.type_mismatches += 1
        stats.sample({"kind": "type", "path": path, "left_type": type(left).__name__, "right_type": type(right).__name__,
                      "left": _preview(left), "right": _preview(right)})
        return False

    if left == right:
        stats.equal_nodes += 1
        return True
    stats.scalar_mismatches += 1
    stats.sample({"kind": "scalar", "path": path, "left": _preview(left), "right": _preview(right)})
    return False


_COUNT_FIELDS = (
    "object_nodes", "array_nodes", "scalar_nodes", "equal_nodes", "type_mismatches",
    "missing_left", "missing_right", "scalar_mismatches", "array_order_mismatches",
    "array_multiset_mismatches", "named_array_nodes", "cellarea_tolerated", "cellarea_outside_epsilon",
)


def _difference_count(report: dict[str, Any]) -> int:
    """A visible count of leaf/key/collection discrepancies, not a truth claim."""
    return (
        report["type_mismatches"] + report["missing_left"] + report["missing_right"]
        + report["scalar_mismatches"] + report["array_multiset_mismatches"]
    )


def _merge_section_reports(sections: dict[str, dict[str, Any]]) -> dict[str, Any]:
    """Keep the old aggregate shape while preserving fair per-section samples."""
    result: dict[str, Any] = {field: 0 for field in _COUNT_FIELDS}
    samples: list[dict[str, Any]] = []
    array_samples: list[dict[str, Any]] = []
    # One sample per section first avoids IO pins consuming the entire global
    # sample budget.  The complete local samples remain under ``sections``.
    for section_name, section in sections.items():
        report = section.get("diff")
        if report is None:
            # A top-level section can be absent on only one side.  It has no
            # recursive ``diff`` report, but it must still contribute to the
            # legacy aggregate and leave a visible, section-scoped sample.
            status = section.get("status")
            if status in {"missing_left", "missing_right"}:
                result[status] += 1
                samples.append({
                    "section": section_name,
                    "kind": status,
                    "path": f"$.{section_name}",
                })
            continue
        for field in _COUNT_FIELDS:
            result[field] += report[field]
        if report["samples"]:
            samples.append({"section": section_name, **report["samples"][0]})
        if report["array_samples"]:
            array_samples.append({"section": section_name, **report["array_samples"][0]})
    for section_name, section in sections.items():
        report = section.get("diff")
        if report is None:
            continue
        for sample in report["samples"][1:]:
            if len(samples) >= MAX_SAMPLES:
                break
            samples.append({"section": section_name, **sample})
        for sample in report["array_samples"][1:]:
            if len(array_samples) >= MAX_ARRAY_SAMPLES:
                break
            array_samples.append({"section": section_name, **sample})
    result["samples"] = samples[:MAX_SAMPLES]
    result["array_samples"] = array_samples[:MAX_ARRAY_SAMPLES]
    result["difference_count"] = _difference_count(result)
    return result


def _named_object_summary(left: list[Any], right: list[Any]) -> dict[str, Any] | None:
    """Summarize exact-name membership without normalizing the names themselves."""
    if not _all_named_objects(left) or not _all_named_objects(right):
        return None
    left_by_name: dict[str, list[dict[str, Any]]] = defaultdict(list)
    right_by_name: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for item in left:
        left_by_name[item["name"]].append(item)
    for item in right:
        right_by_name[item["name"]].append(item)
    left_names = set(left_by_name)
    right_names = set(right_by_name)
    only_left = sorted(left_names - right_names)
    only_right = sorted(right_names - left_names)
    changed = []
    multiplicity_changed = []
    for name in sorted(left_names & right_names):
        left_values = left_by_name[name]
        right_values = right_by_name[name]
        if len(left_values) != len(right_values):
            multiplicity_changed.append(name)
        if Counter(_canonical(item) for item in left_values) != Counter(_canonical(item) for item in right_values):
            changed.append(name)
    return {
        "identity": "exact JSON string field name; no unescaping, stripping, or case folding",
        "left_object_count": len(left), "right_object_count": len(right),
        "left_unique_name_count": len(left_names), "right_unique_name_count": len(right_names),
        "only_left_name_count": len(only_left), "only_right_name_count": len(only_right),
        "changed_name_count": len(changed), "multiplicity_changed_name_count": len(multiplicity_changed),
        "only_left_name_sample": only_left[:10], "only_right_name_sample": only_right[:10],
        "changed_name_sample": changed[:10], "multiplicity_changed_name_sample": multiplicity_changed[:10],
    }


def _pin_tuple(pin: Any) -> tuple[tuple[str, str], ...]:
    """Exact pin metadata tuple; every present pin field participates."""
    if not isinstance(pin, dict):
        return (("<raw>", _canonical(pin)),)
    return tuple((key, _canonical(value)) for key, value in sorted(pin.items()))


def _pin_counter(pins: Any) -> tuple[Counter[tuple[tuple[str, str], ...]], dict[tuple[tuple[str, str], ...], list[Any]]]:
    counter: Counter[tuple[tuple[str, str], ...]] = Counter()
    values: dict[tuple[tuple[str, str], ...], list[Any]] = defaultdict(list)
    if not isinstance(pins, list):
        pins = [pins]
    for pin in pins:
        key = _pin_tuple(pin)
        counter[key] += 1
        values[key].append(pin)
    return counter, values


def _net_summary(left: list[Any], right: list[Any]) -> dict[str, Any] | None:
    """Net comparison by exact net name, exact metadata, and pin tuple multiset."""
    named = _named_object_summary(left, right)
    if named is None:
        return None
    left_by_name: dict[str, list[dict[str, Any]]] = defaultdict(list)
    right_by_name: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for item in left:
        left_by_name[item["name"]].append(item)
    for item in right:
        right_by_name[item["name"]].append(item)
    changed_names: set[str] = set()
    metadata_changed_names: set[str] = set()
    pin_changed_names: set[str] = set()
    samples = []
    for name in sorted(set(left_by_name) & set(right_by_name)):
        # Sorting only pairs duplicate copies of the same exact name.  It does
        # not alter the original model or hide list order, which is reported
        # independently by the generic section diff.
        left_values = sorted(left_by_name[name], key=_canonical)
        right_values = sorted(right_by_name[name], key=_canonical)
        if len(left_values) != len(right_values):
            changed_names.add(name)
        for occurrence, (a_value, b_value) in enumerate(zip(left_values, right_values)):
            a_metadata = {key: value for key, value in a_value.items() if key not in {"name", "pins"}}
            b_metadata = {key: value for key, value in b_value.items() if key not in {"name", "pins"}}
            metadata_equal = _canonical(a_metadata) == _canonical(b_metadata)
            a_pins, a_pin_values = _pin_counter(a_value.get("pins", []))
            b_pins, b_pin_values = _pin_counter(b_value.get("pins", []))
            pins_equal = a_pins == b_pins
            if metadata_equal and pins_equal:
                continue
            changed_names.add(name)
            if not metadata_equal:
                metadata_changed_names.add(name)
            if not pins_equal:
                pin_changed_names.add(name)
            if len(samples) < 20:
                left_only = a_pins - b_pins
                right_only = b_pins - a_pins
                samples.append({
                    "name": name,
                    "occurrence": occurrence,
                    "metadata_equal": metadata_equal,
                    "pins_tuple_multiset_equal": pins_equal,
                    "left_pin_count": a_pins.total(), "right_pin_count": b_pins.total(),
                    "left_only_pin_count": left_only.total(), "right_only_pin_count": right_only.total(),
                    "left_metadata": _preview(a_metadata), "right_metadata": _preview(b_metadata),
                    "left_only_pin_sample": [_preview(a_pin_values[key][0]) for key in list(left_only)[:3]],
                    "right_only_pin_sample": [_preview(b_pin_values[key][0]) for key in list(right_only)[:3]],
                })
    return {
        **named,
        "pin_tuple_schema": "sorted (field-name, exact JSON field-value) pairs for every pin field",
        "changed_name_count": len(changed_names),
        "metadata_changed_name_count": len(metadata_changed_names),
        "pin_changed_name_count": len(pin_changed_names),
        "changed_name_sample": sorted(changed_names)[:20],
        "metadata_changed_name_sample": sorted(metadata_changed_names)[:20],
        "pin_changed_name_sample": sorted(pin_changed_names)[:20],
        "changed_net_samples": samples,
    }


def _section_report(
    name: str, left: Any, right: Any, cellarea_epsilon: float
) -> dict[str, Any]:
    stats = DiffStats()
    strict_equal = _canonical(left) == _canonical(right)
    semantic_equal = _compare_json(left, right, f"$.{name}", stats, cellarea_epsilon)
    diff = stats.report()
    result: dict[str, Any] = {
        "strict_json_equal": strict_equal,
        # This is a diagnostic comparison of worker-projected JSON, not the
        # formal iFP semantic pass criterion.
        "diagnostic_equal": semantic_equal,
        "difference_count": _difference_count(diff),
        "diff": diff,
    }
    if isinstance(left, list) and isinstance(right, list):
        result["named_objects"] = _named_object_summary(left, right)
        if name == "nets":
            result["net_connectivity"] = _net_summary(left, right)
    return result


def _load_json(path: Path) -> tuple[dict[str, Any], Any | None]:
    if not path.is_file():
        return {"status": "missing", "path": str(path)}, None
    try:
        with path.open(encoding="utf-8") as stream:
            return {"status": "loaded", "path": str(path), "bytes": path.stat().st_size}, json.load(stream)
    except (OSError, json.JSONDecodeError) as error:
        return {"status": "invalid", "path": str(path), "error": repr(error)}, None


def _compare_models(left_path: Path, right_path: Path, cellarea_epsilon: float) -> dict[str, Any]:
    left_info, left = _load_json(left_path)
    right_info, right = _load_json(right_path)
    result: dict[str, Any] = {
        "left": left_info,
        "right": right_info,
        "cellarea_rule": {
            "paths": ["*.cellarea", "*.cell_area"],
            "absolute_epsilon": cellarea_epsilon,
            "note": "Only this float field may compare equal within epsilon; raw values remain in both model files and samples.",
        },
    }
    if left is None or right is None:
        result.update({"status": "unavailable", "diagnostic_equal": False, "strict_json_equal": False})
        return result
    if not isinstance(left, dict) or not isinstance(right, dict):
        stats = DiffStats()
        result["strict_json_equal"] = _canonical(left) == _canonical(right)
        result["diagnostic_equal"] = _compare_json(left, right, "$", stats, cellarea_epsilon)
        result["diff"] = stats.report()
        result["difference_count"] = _difference_count(result["diff"])
        result["status"] = "compared"
        return result
    sections: dict[str, dict[str, Any]] = {}
    for name in sorted(left.keys() | right.keys()):
        if name not in left:
            sections[name] = {"status": "missing_left", "diagnostic_equal": False, "strict_json_equal": False,
                              "difference_count": 1, "right": _preview(right[name])}
        elif name not in right:
            sections[name] = {"status": "missing_right", "diagnostic_equal": False, "strict_json_equal": False,
                              "difference_count": 1, "left": _preview(left[name])}
        else:
            sections[name] = _section_report(name, left[name], right[name], cellarea_epsilon)
    result["strict_json_equal"] = _canonical(left) == _canonical(right)
    result["diagnostic_equal"] = all(section["diagnostic_equal"] is True for section in sections.values())
    result["status"] = "compared"
    result["sections"] = sections
    result["diff"] = _merge_section_reports(sections)
    result["difference_count"] = sum(section["difference_count"] for section in sections.values())
    return result


def _read_worker_result(path: Path) -> dict[str, Any]:
    info, value = _load_json(path)
    if value is None:
        return info
    if not isinstance(value, dict):
        return {"status": "invalid", "path": str(path), "error": "result JSON must be an object"}
    return {"status": "loaded", "path": str(path), "worker": value}


def _result_io_order_mode(worker: dict[str, Any]) -> tuple[str | None, str | None]:
    """Return the worker-declared mode without allowing malformed JSON to look valid."""
    result = worker.get("result")
    payload = result.get("worker") if isinstance(result, dict) else None
    if not isinstance(payload, dict):
        return None, "worker result is unavailable"
    try:
        return _io_order_control_mode(payload.get("io_order_control"), source="worker result"), None
    except ValueError as error:
        return None, str(error)


def _inspect_io_order_control(
    backend_root: Path, worker: dict[str, Any], requested_mode: str
) -> tuple[dict[str, Any], list[str] | None, list[str] | None]:
    """Validate test-only sorting artifacts without treating them as formal semantics.

    The source artifacts retain the complete exact name arrays.  The comparison
    report contains only a digest/sample so a large IO set does not duplicate
    an entire work model in ``comparison.json``.
    """
    result_mode, result_error = _result_io_order_mode(worker)
    control_path = backend_root / "io-order-control.json"
    controlled_path = backend_root / "controlled-input-model.json"
    raw_path = backend_root / "input-model.json"
    control_info, control = _load_json(control_path)
    controlled_info, controlled = _load_json(controlled_path)
    raw_info, raw_model = _load_json(raw_path)
    errors: list[str] = []
    if result_error is not None:
        errors.append(result_error)
    elif result_mode != requested_mode:
        errors.append(f"worker result mode is {result_mode!r}, expected {requested_mode!r}")
    if not isinstance(control, dict):
        errors.append("io-order-control artifact must be a JSON object")
        control = {}

    raw_names = control.get("raw_names")
    controlled_names = control.get("controlled_names")
    pin_count = control.get("pin_count")
    artifact_mode = control.get("mode")
    index_rebuilt = control.get("index_rebuilt")
    if artifact_mode != requested_mode:
        errors.append(f"control artifact mode is {artifact_mode!r}, expected {requested_mode!r}")
    if not isinstance(raw_names, list) or not all(isinstance(name, str) for name in raw_names):
        errors.append("control artifact raw_names must be a string array")
        raw_names = None
    if not isinstance(controlled_names, list) or not all(isinstance(name, str) for name in controlled_names):
        errors.append("control artifact controlled_names must be a string array")
        controlled_names = None
    if isinstance(pin_count, bool) or not isinstance(pin_count, int):
        errors.append("control artifact pin_count must be an integer")
    elif raw_names is not None and (pin_count != len(raw_names) or pin_count != len(controlled_names or [])):
        errors.append("control artifact pin_count does not match name-array lengths")
    if index_rebuilt is not True:
        errors.append("control artifact does not confirm index_rebuilt=true")
    if raw_names is not None and controlled_names is not None and Counter(raw_names) != Counter(controlled_names):
        errors.append("control artifact changed the exact IO-name multiset")
    if raw_model is None:
        errors.append("raw input-model artifact is unavailable")
    if controlled is None:
        errors.append("controlled-input-model artifact is unavailable")

    report: dict[str, Any] = {
        "requested_mode": requested_mode,
        "worker_result_mode": result_mode,
        "artifact_mode": artifact_mode,
        "index_rebuilt": index_rebuilt is True,
        "pin_count": pin_count if isinstance(pin_count, int) and not isinstance(pin_count, bool) else None,
        "raw_input_model": raw_info,
        "controlled_input_model": controlled_info,
        "control_artifact": control_info,
        "raw_names": _name_summary(raw_names) if raw_names is not None else None,
        "controlled_names": _name_summary(controlled_names) if controlled_names is not None else None,
        "name_multiset_preserved": (
            Counter(raw_names) == Counter(controlled_names)
            if raw_names is not None and controlled_names is not None else False
        ),
        "status": "valid" if not errors else "invalid",
        "errors": errors,
    }
    return report, raw_names, controlled_names


def _build_io_order_control_comparison(
    root: Path,
    workers: dict[str, dict[str, Any]],
    requested_mode: str,
    cellarea_epsilon: float,
) -> dict[str, Any]:
    """Describe a controlled-order experiment; never influence the formal gate."""
    if requested_mode == "none":
        return {"mode": "none", "experimental": False, "status": "not_requested"}

    controls: dict[str, dict[str, Any]] = {}
    names: dict[str, tuple[list[str] | None, list[str] | None]] = {}
    for backend in BACKENDS:
        report, raw_names, controlled_names = _inspect_io_order_control(
            root / backend, workers[backend], requested_mode
        )
        workers[backend]["io_order_control"] = report
        controls[backend] = report
        names[backend] = (raw_names, controlled_names)

    valid = all(control["status"] == "valid" for control in controls.values())
    raw_idb, controlled_idb = names["idb"]
    raw_eccdb, controlled_eccdb = names["eccdb"]
    raw_names_order_equal = raw_idb == raw_eccdb if valid else None
    raw_name_multiset_equal = (
        Counter(raw_idb) == Counter(raw_eccdb)
        if valid and raw_idb is not None and raw_eccdb is not None else None
    )
    controlled_names_equal = controlled_idb == controlled_eccdb if valid else None
    controlled_model = (
        _compare_models(root / "idb" / "controlled-input-model.json",
                        root / "eccdb" / "controlled-input-model.json", cellarea_epsilon)
        if valid else None
    )
    controlled_model_equal = (
        controlled_model.get("diagnostic_equal") is True if isinstance(controlled_model, dict) else None
    )
    return {
        "mode": requested_mode,
        "experimental": True,
        "status": "applied" if valid else "invalid",
        "workers": controls,
        "raw_names_order_equal": raw_names_order_equal,
        "raw_name_multiset_equal": raw_name_multiset_equal,
        "controlled_names_equal": controlled_names_equal,
        "index_rebuilt_all": all(control["index_rebuilt"] for control in controls.values()) if valid else False,
        "controlled_input_model": controlled_model,
        "controlled_input_model_diagnostics_equal": controlled_model_equal,
        "input_equivalence_established": controlled_model_equal is True,
        "conclusion": (
            "This opt-in experiment only aligns the temporary iFP IO-vector order by exact name. "
            "It does not normalize names or prove the two imported work models are otherwise identical."
        ),
    }


def _run_worker(
    binary: Path,
    manifest: Path,
    backend_root: Path,
    backend: str,
    timeout_seconds: int,
    io_order_control: str = "none",
) -> dict[str, Any]:
    backend_root.mkdir(parents=True, exist_ok=False)
    command = [str(binary), backend, str(manifest), str(backend_root)]
    # Keep the original four-argument worker contract byte-for-byte for the
    # default experiment.  The temporary ordering experiment is opt-in only.
    if io_order_control != "none":
        command.append(f"--io-order-control={io_order_control}")
    stdout_path = backend_root / "worker.stdout.log"
    stderr_path = backend_root / "worker.stderr.log"
    started = time.perf_counter()
    timed_out = False
    spawn_error = None
    try:
        with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
            process = subprocess.Popen(command, stdout=stdout, stderr=stderr, cwd=str(manifest.parent), env=os.environ.copy())
            try:
                returncode = process.wait(timeout=timeout_seconds)
            except subprocess.TimeoutExpired:
                timed_out = True
                process.terminate()
                try:
                    returncode = process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    process.kill()
                    returncode = process.wait()
    except OSError as error:
        returncode = None
        spawn_error = repr(error)
    result = {
        "backend": backend,
        "command": command,
        "returncode": returncode,
        "timed_out": timed_out,
        "wall_ms": (time.perf_counter() - started) * 1000,
        "stdout": _file_metadata(stdout_path),
        "stderr": _file_metadata(stderr_path),
        "result": _read_worker_result(backend_root / "result.json"),
        "artifacts": {name: _file_metadata(backend_root / name)
                      for name in _worker_artifact_names(io_order_control)},
    }
    if spawn_error is not None:
        result["spawn_error"] = spawn_error
    return result


def _run_semantic_probe(
    probe: Path | None, manifest: Path, root: Path, phase: str, timeout_seconds: int
) -> dict[str, Any]:
    if probe is None:
        return {"status": "not_requested"}
    left = root / "idb" / ("import.def" if phase == "import" else "output.def")
    right = root / "eccdb" / ("import.def" if phase == "import" else "output.def")
    if not left.is_file() or not right.is_file():
        return {"status": "unavailable", "reason": "missing DEF", "left": _file_metadata(left), "right": _file_metadata(right)}
    comparison_dir = root / "comparison"
    comparison_dir.mkdir(exist_ok=True)
    report = comparison_dir / f"{phase}.semantic.json"
    stdout_path = comparison_dir / f"{phase}.semantic.stdout.log"
    stderr_path = comparison_dir / f"{phase}.semantic.stderr.log"
    command: list[str] = []
    started = time.perf_counter()
    try:
        # A failed new probe must never consume a successful old report.
        with tempfile.TemporaryDirectory(prefix=f"{phase}-probe-", dir=comparison_dir) as scratch:
            fresh_report = Path(scratch) / "report.json"
            command = [str(probe), str(manifest), str(left), str(right), str(fresh_report)]
            with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
                completed = subprocess.run(command, stdout=stdout, stderr=stderr, cwd=str(manifest.parent), timeout=timeout_seconds, check=False)
            _, semantic_report = _load_json(fresh_report)
        returncode = completed.returncode
        canonical = semantic_report.get("ifp_semantics_equal") if isinstance(semantic_report, dict) else None
        details = semantic_report.get("ifp_semantics", {}) if isinstance(semantic_report, dict) else {}
        valid = (returncode in (0, 1) and isinstance(semantic_report, dict)
                 and semantic_report.get("status") == "completed" and isinstance(canonical, bool)
                 and canonical == (returncode == 0) and isinstance(details, dict)
                 and (not canonical or (details.get("left_complete") is True and details.get("right_complete") is True)))
        status = ("equal" if canonical else "different") if valid else "failed"
        _json_dump(report, semantic_report if isinstance(semantic_report, dict) else {
            "status": "failed", "error": "probe did not produce a fresh report", "returncode": returncode
        })
        return {
            "status": status,
            "command": command,
            "returncode": returncode,
            "ifp_semantics_equal": canonical if valid else None,
            "represented_semantics_equal": semantic_report.get("represented_semantics_equal")
                if isinstance(semantic_report, dict) else None,
            "wall_ms": (time.perf_counter() - started) * 1000,
            "left": _file_metadata(left),
            "right": _file_metadata(right),
            "report": _file_metadata(report),
            "stdout": _file_metadata(stdout_path),
            "stderr": _file_metadata(stderr_path),
        }
    except subprocess.TimeoutExpired:
        return {
            "status": "timed_out", "command": command,
            "timeout_seconds": timeout_seconds,
            "wall_ms": (time.perf_counter() - started) * 1000,
            "left": _file_metadata(left), "right": _file_metadata(right),
        }
    except OSError as error:
        return {
            "status": "failed", "command": command, "error": repr(error),
            "wall_ms": (time.perf_counter() - started) * 1000,
            "left": _file_metadata(left), "right": _file_metadata(right),
        }


def _existing_worker(root: Path, backend: str, io_order_control: str = "none") -> dict[str, Any]:
    backend_root = root / backend
    return {
        "backend": backend,
        "mode": "compare_only_existing_artifacts",
        "result": _read_worker_result(backend_root / "result.json"),
        "artifacts": {name: _file_metadata(backend_root / name)
                      for name in _worker_artifact_names(io_order_control)},
    }


def _stored_worker_passed(worker: dict[str, Any]) -> bool:
    result = worker.get("result", {})
    payload = result.get("worker") if isinstance(result, dict) else None
    return isinstance(payload, dict) and payload.get("status") == "passed"


def _build_comparison(
    root: Path,
    manifest: Path,
    workers: dict[str, dict[str, Any]],
    cellarea_epsilon: float,
    probe: Path | None,
    timeout_seconds: int,
    existing_semantics: dict[str, Any] | None = None,
    io_order_control: str = "none",
) -> dict[str, Any]:
    def_semantics = {
        phase: _run_semantic_probe(probe, manifest, root, phase, timeout_seconds)
        for phase in ("import", "output")
    } if probe is not None else ({
        "import": {"status": "not_requested"}, "output": {"status": "not_requested"}
    })
    comparison = {
        "status": "completed",
        "workers": workers,
        "execution_passed": all(
            (worker.get("returncode") == 0 and not worker.get("timed_out", False))
            if "returncode" in worker else _stored_worker_passed(worker)
            for worker in workers.values()
        ),
        "models": {
            name.removesuffix(".json"): _compare_models(
                root / "idb" / name, root / "eccdb" / name, cellarea_epsilon
            )
            for name in MODEL_FILES
        },
        "def_semantics": def_semantics,
    }
    comparison["work_model_diagnostics_equal"] = all(
        entry.get("diagnostic_equal") is True for entry in comparison["models"].values()
    )
    canonical_results = [def_semantics[phase].get("ifp_semantics_equal")
                         if def_semantics[phase].get("status") in {"equal", "different"} else None
                         for phase in ("import", "output")]
    comparison["differential_passed"] = (
        comparison["execution_passed"] and all(value is True for value in canonical_results)
    ) if all(isinstance(value, bool) for value in canonical_results) else None
    comparison["pass_policy"] = (
        "Both workers complete and imported/final iFP canonical semantics match; "
        "work-model JSON and representation-only differences are diagnostics. "
        "A null differential_passed means canonical comparison is unavailable."
    )
    if io_order_control != "none":
        comparison["io_order_control"] = _build_io_order_control_comparison(
            root, workers, io_order_control, cellarea_epsilon
        )
    return comparison


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", type=Path, help="iFP worker binary; required unless --compare-only")
    parser.add_argument("--manifest", type=Path, required=True, help="case manifest consumed unchanged by both workers")
    parser.add_argument("--output-root", type=Path, required=True,
                        help="new experiment directory; with --compare-only, an existing experiment directory")
    parser.add_argument("--semantic-probe", type=Path, help="optional DEF semantic probe executable")
    parser.add_argument("--compare-only", action="store_true",
                        help="recompute comparison.json from existing worker artifacts; never starts iFP")
    parser.add_argument("--io-order-control", choices=IO_ORDER_CONTROL_CHOICES, default="none",
                        help=("test-only temporary iFP IO-vector order control; default 'none' preserves "
                              "the original worker contract and imported ordering"))
    parser.add_argument("--timeout-seconds", type=int, default=3600)
    parser.add_argument("--cellarea-epsilon", type=float, default=1e-9,
                        help="absolute epsilon only for JSON keys cellarea/cell_area (default: 1e-9)")
    return parser.parse_args()


def main() -> int:
    args = _parse_args()
    binary = args.binary.resolve() if args.binary else None
    manifest = args.manifest.resolve()
    root = args.output_root.resolve()
    probe = args.semantic_probe.resolve() if args.semantic_probe else None
    if not args.compare_only and binary is None:
        raise SystemExit("--binary is required unless --compare-only is used")
    if binary is not None and (not binary.is_file() or not os.access(binary, os.X_OK)):
        raise SystemExit(f"worker binary is not executable: {binary}")
    if not manifest.is_file():
        raise SystemExit(f"manifest does not exist: {manifest}")
    if probe is not None and (not probe.is_file() or not os.access(probe, os.X_OK)):
        raise SystemExit(f"semantic probe is not executable: {probe}")
    if args.compare_only:
        if not root.is_dir():
            raise SystemExit(f"--compare-only requires an existing output root: {root}")
    elif root.exists():
        raise SystemExit(f"refusing to overwrite existing output root: {root}")
    if args.timeout_seconds <= 0:
        raise SystemExit("timeout must be positive")
    if not math.isfinite(args.cellarea_epsilon) or args.cellarea_epsilon < 0:
        raise SystemExit("cellarea epsilon must be finite and non-negative")
    try:
        manifest_value = json.loads(manifest.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SystemExit(f"invalid manifest JSON: {error}") from error
    required = ("lef_files", "def_file", "config", "binary_checkpoint")
    missing = [key for key in required if key not in manifest_value]
    if missing:
        raise SystemExit(f"manifest missing required keys: {', '.join(missing)}")
    try:
        current_inputs = _manifest_inputs(manifest, manifest_value)
    except (OSError, ValueError, TypeError) as error:
        raise SystemExit(f"invalid manifest inputs: {error}") from error

    if not args.compare_only:
        root.mkdir(parents=True)
    (root / "comparison").mkdir(exist_ok=True)
    invocation = {
        "worker_contract": ["<binary>", "<idb|eccdb>", "<manifest.json>", "<backend-output-directory>"],
        "binary": _file_metadata(binary) if binary else None,
        "manifest": _file_metadata(manifest),
        "inputs": current_inputs,
        "semantic_probe": _file_metadata(probe) if probe else None,
        "timeout_seconds": args.timeout_seconds,
        "cellarea_epsilon": args.cellarea_epsilon,
        "note": "Fresh workers run concurrently in isolated directories; compare-only preserves worker artifacts and replaces comparison reports.",
    }
    if args.io_order_control != "none":
        invocation["io_order_control"] = {
            "mode": args.io_order_control,
            "experimental": True,
            "scope": "temporary iFP IO-vector order only; raw input-model.json remains preserved",
        }
    if args.compare_only:
        previous_info, previous = _load_json(root / "comparison.json")
        try:
            invocation["provenance"] = _validate_saved_inputs(
                root, manifest, current_inputs, previous or {}, args.io_order_control
            )
        except (OSError, ValueError, TypeError) as error:
            raise SystemExit(f"compare-only refused: {error}") from error
        invocation["mode"] = "compare_only"
        invocation["previous_comparison"] = previous_info
        _json_dump(root / "comparison-invocation.json", invocation)
        workers = {
            backend: _existing_worker(root, backend, args.io_order_control)
            for backend in BACKENDS
        }
        comparison = _build_comparison(
            root, manifest, workers, args.cellarea_epsilon, probe, args.timeout_seconds,
            io_order_control=args.io_order_control,
        )
        comparison["mode"] = "compare_only"
    else:
        _json_dump(root / "invocation.json", invocation)
        with ThreadPoolExecutor(max_workers=2) as executor:
            jobs = {
                backend: executor.submit(
                    _run_worker, binary, manifest, root / backend, backend,
                    args.timeout_seconds, args.io_order_control,
                )
                for backend in BACKENDS
            }
            workers = {backend: job.result() for backend, job in jobs.items()}
        comparison = _build_comparison(
            root, manifest, workers, args.cellarea_epsilon, probe, args.timeout_seconds,
            io_order_control=args.io_order_control,
        )
    _json_dump(root / "comparison.json", comparison)
    print(json.dumps({
        "output_root": str(root),
        "execution_passed": comparison["execution_passed"],
        "differential_passed": comparison["differential_passed"],
        "work_model_diagnostics_equal": comparison["work_model_diagnostics_equal"],
        "def_semantics": {key: value["status"] for key, value in comparison["def_semantics"].items()},
        "io_order_control": comparison.get("io_order_control", {"mode": "none"}),
    }, indent=2, sort_keys=True))
    if not comparison["execution_passed"] or comparison["differential_passed"] is False:
        return 1
    if comparison["differential_passed"] is None:
        return 2  # No formal result is never a successful differential run.
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
