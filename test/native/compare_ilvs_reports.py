#!/usr/bin/env python3
# SPDX-License-Identifier: MulanPSL-2.0
"""Strict, order-independent semantic comparison for two iLVS runs.

This comparator deliberately does *not* treat an iLVS layer integer as a
portable physical-layer identity.  The iDB path records an ``IdbLayer`` ID,
while the EccDB path currently records a packed ``TechLayerId``.  A successful
comparison therefore needs an explicit mapping from each backend's report ID
to a physical layer name (for example ``M1``).  Without it we can still show
the raw difference, but must report the normalized result as inconclusive.

The command accepts the same input manifest shape as
``artifacts/.../analysis/run_backend.py``.  The manifest is intentionally part
of the evidence: it verifies LEF/DEF/Verilog hashes, options and (when the
runner records it) the Verilog top module before a result can be called strict.
"""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
from pathlib import Path
from typing import Any, Iterable, Mapping


SAMPLE_LIMIT = 20
NESTED_SAMPLE_LIMIT = 3
LAYER_MAP_FILENAMES = ("lvs_layer_mapping.json", "lvs-layer-mapping.json", "layer_mapping.json", "layer-map.json")


def _read_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def _stable(value: Any) -> Any:
    """Make mapping key order deterministic without changing list semantics."""
    if isinstance(value, float):
        return round(value, 9)
    if isinstance(value, Mapping):
        return {str(key): _stable(value[key]) for key in sorted(value, key=str)}
    if isinstance(value, list):
        return [_stable(item) for item in value]
    return value


def _encode(value: Any) -> str:
    return json.dumps(_stable(value), sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def _sha(value: Any) -> str:
    return hashlib.sha256(_encode(value).encode("utf-8")).hexdigest()


def _digest(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def _compact_for_output(value: Any) -> Any:
    """Bound diagnostics without weakening the exact in-memory comparison.

    A single power-grid component violation can contain tens of thousands of
    rectangles.  The original ``ilvs.json`` remains the complete artifact;
    comparison JSON keeps counts and full-content hashes plus a small sample.
    """
    if isinstance(value, Mapping):
        result: dict[str, Any] = {}
        for key, nested in value.items():
            if key in {"shapes", "terminals", "nets", "components"} and isinstance(nested, list) and len(nested) > NESTED_SAMPLE_LIMIT:
                result[key] = {
                    "count": len(nested),
                    "sha256": _sha(nested),
                    "sample": [_compact_for_output(item) for item in nested[:NESTED_SAMPLE_LIMIT]],
                    "display_truncated": True,
                }
            else:
                result[key] = _compact_for_output(nested)
        return result
    if isinstance(value, list) and len(value) > SAMPLE_LIMIT:
        return {
            "count": len(value),
            "sha256": _sha(value),
            "sample": [_compact_for_output(item) for item in value[:NESTED_SAMPLE_LIMIT]],
            "display_truncated": True,
        }
    if isinstance(value, list):
        return [_compact_for_output(item) for item in value]
    return value


def _counter_delta(left: Iterable[Any], right: Iterable[Any], limit: int = SAMPLE_LIMIT) -> dict[str, Any]:
    """Compare a collection as a multiset and retain duplicates in samples."""
    left_counter: collections.Counter[str] = collections.Counter()
    right_counter: collections.Counter[str] = collections.Counter()
    left_value: dict[str, Any] = {}
    right_value: dict[str, Any] = {}
    for item in left:
        encoded = _encode(item)
        left_counter[encoded] += 1
        left_value.setdefault(encoded, item)
    for item in right:
        encoded = _encode(item)
        right_counter[encoded] += 1
        right_value.setdefault(encoded, item)
    left_delta = left_counter - right_counter
    right_delta = right_counter - left_counter

    def sample(delta: collections.Counter[str], representative: Mapping[str, Any]) -> list[Any]:
        values: list[Any] = []
        for encoded, count in sorted(delta.items()):
            for _ in range(min(count, limit - len(values))):
                values.append(_compact_for_output(representative[encoded]))
            if len(values) >= limit:
                break
        return values

    left_only_count = sum(left_delta.values())
    right_only_count = sum(right_delta.values())
    return {
        "equal": not left_delta and not right_delta,
        "eccdb_only_count": left_only_count,
        "idb_only_count": right_only_count,
        "eccdb_only_sample": sample(left_delta, left_value),
        "idb_only_sample": sample(right_delta, right_value),
        "samples_display_truncated": left_only_count > limit or right_only_count > limit,
    }


def _sorted_multiset(values: Iterable[Any]) -> list[Any]:
    # Keep the canonical values rather than round-tripping every large
    # violation through JSON. ``_encode`` remains the ordering authority, but
    # retaining references avoids duplicating multi-megabyte PG records just
    # to sort them.
    pairs = [(_encode(value), value) for value in values]
    pairs.sort(key=lambda pair: pair[0])
    return [value for _, value in pairs]


def _canonicalise_violation(value: Any) -> Any:
    """Canonicalise only documented collection-valued violation fields.

    iLVS reports terminals, related nets, graph components and geometric shapes
    as vectors, but none of those vectors carries an ordering contract.  Their
    members are nevertheless multisets: duplicate shapes remain meaningful.
    Coordinate rectangles are *not* treated as collections; their four entries
    stay in ``[llx, lly, urx, ury]`` order.
    """
    if isinstance(value, Mapping):
        result: dict[str, Any] = {}
        for key, nested in value.items():
            if key in {"terminals", "nets", "components", "shapes"} and isinstance(nested, list):
                result[key] = _sorted_multiset(_canonicalise_violation(item) for item in nested)
            else:
                result[key] = _canonicalise_violation(nested)
        return result
    if isinstance(value, list):
        return [_canonicalise_violation(item) for item in value]
    return value


def _normalise_layer_map(value: Any) -> dict[str, str]:
    """Accept the small, explicit mapping formats used by runners and probes."""
    if not isinstance(value, Mapping):
        return {}
    for key in ("layer_id_to_name", "layers", "routing_layers", "mapping"):
        nested = value.get(key)
        if isinstance(nested, Mapping):
            value = nested
            break
        if isinstance(nested, list):
            result: dict[str, str] = {}
            for entry in nested:
                if not isinstance(entry, Mapping):
                    continue
                identifier = entry.get("id", entry.get("layer_id", entry.get("index")))
                name = entry.get("name", entry.get("layer_name"))
                if identifier is not None and isinstance(name, str):
                    result[str(identifier)] = name
            return result
    # A bare map is supported only when it is visibly an ID map.  In
    # particular, do not mistake metadata fields such as ``top`` or input
    # pathname hashes for layer names.
    if not all(_is_layer_identifier(identifier) for identifier in value):
        return {}
    result = {}
    for identifier, name in value.items():
        if isinstance(name, str):
            result[str(identifier)] = name
        elif isinstance(name, Mapping):
            nested_name = name.get("name", name.get("layer_name"))
            if isinstance(nested_name, str):
                result[str(identifier)] = nested_name
    return result


def _is_layer_identifier(value: Any) -> bool:
    text = str(value)
    return text.isdigit() or (text.startswith("-") and text[1:].isdigit())


def _extract_layer_map(root: Path, metadata: Mapping[str, Any], stage: Mapping[str, Any], report: Mapping[str, Any]) -> tuple[dict[str, str], str | None]:
    """Find a runner-provided layer mapping without guessing from numeric IDs."""
    for name, document in (("metadata.json", metadata), ("stage.json", stage), ("ilvs.json", report)):
        mapping = _normalise_layer_map(document)
        if mapping:
            return mapping, name
    for filename in LAYER_MAP_FILENAMES:
        candidate = root / filename
        if candidate.is_file():
            mapping = _normalise_layer_map(_read_json(candidate))
            if mapping:
                return mapping, filename
    return {}, None


def _used_layers(violations: Iterable[Mapping[str, Any]]) -> set[str]:
    return {
        str(shape["layer"])
        for violation in violations
        for shape in violation.get("shapes", [])
        if isinstance(shape, Mapping) and "layer" in shape
    }


def _replace_layers(value: Any, layer_map: Mapping[str, str]) -> Any:
    if isinstance(value, Mapping):
        result: dict[str, Any] = {}
        for key, nested in value.items():
            if key == "layer" and nested is not None:
                result[key] = layer_map[str(nested)]
            else:
                result[key] = _replace_layers(nested, layer_map)
        return result
    if isinstance(value, list):
        return [_replace_layers(item, layer_map) for item in value]
    return value


def _decode_one_escape(value: str) -> str:
    """Diagnostic only; never used to decide equality.

    This mirrors the minimal paired-escape interpretation used at the current
    EccDB iLVS boundary, but preserves unmatched escapes.  It is deliberately
    not a DEF parser and cannot justify an equality result.
    """
    output: list[str] = []
    index = 0
    while index < len(value):
        if value[index] == "\\" and index + 1 < len(value):
            output.append(value[index + 1])
            index += 2
        else:
            output.append(value[index])
            index += 1
    return "".join(output)


def _decode_identifier_fields(value: Any) -> Any:
    if isinstance(value, Mapping):
        decoded: dict[str, Any] = {}
        for key, nested in value.items():
            if key in {"net", "driver", "instance"} and isinstance(nested, str):
                decoded[key] = _decode_one_escape(nested)
            elif key in {"nets", "terminals"} and isinstance(nested, list):
                decoded[key] = [_decode_one_escape(item) if isinstance(item, str) else item for item in nested]
            else:
                decoded[key] = _decode_identifier_fields(nested)
        return decoded
    if isinstance(value, list):
        return [_decode_identifier_fields(item) for item in value]
    return value


def _summarise_backend(root: Path) -> dict[str, Any]:
    report_path = root / "lvs_reporter" / "ilvs.json"
    metadata_path = root / "metadata.json"
    stage_path = root / "stage.json"
    if not report_path.is_file():
        raise FileNotFoundError(f"missing iLVS report: {report_path}")
    report = _read_json(report_path)
    if not isinstance(report, Mapping):
        raise ValueError(f"iLVS report is not a JSON object: {report_path}")
    metadata = _read_json(metadata_path) if metadata_path.is_file() else {}
    stage = _read_json(stage_path) if stage_path.is_file() else {}
    if not isinstance(metadata, Mapping) or not isinstance(stage, Mapping):
        raise ValueError(f"metadata/stage JSON must be objects under {root}")

    entity = _sorted_multiset(report.get("entity", []))
    connectivity = _sorted_multiset(report.get("connectivity", []))
    report_violations = report.get("violations", [])
    if not isinstance(report_violations, list):
        raise ValueError(f"iLVS report violations must be a JSON array: {report_path}")
    violations = _sorted_multiset(_canonicalise_violation(item) for item in report_violations)
    layer_map, layer_map_source = _extract_layer_map(root, metadata, stage, report)
    used_layers = sorted(_used_layers(violations))
    missing_layers = [layer for layer in used_layers if layer not in layer_map]
    violation_types = collections.Counter(
        str(violation.get("type", "<missing>")) if isinstance(violation, Mapping) else "<invalid>"
        for violation in violations
    )
    return {
        "root": str(root),
        "metadata": dict(metadata),
        "stage": dict(stage),
        "entity": entity,
        "connectivity": connectivity,
        "violations": violations,
        "report_order_sha256": _sha(report_violations),
        "layer_mapping": {
            "source": layer_map_source,
            "id_to_name": dict(sorted(layer_map.items())),
            "used_ids": used_layers,
            "missing_ids": missing_layers,
            "complete_for_report": not missing_layers,
        },
        "summary": {
            "entity_count": len(entity),
            "connectivity_count": len(connectivity),
            "violation_count": len(violations),
            "violation_by_type": dict(sorted(violation_types.items())),
            "wall_seconds": metadata.get("wall_seconds"),
            "peak_rss_kib": metadata.get("peak_rss_kib"),
            "stage_elapsed_ms": stage.get("stage", {}).get("elapsed_ms") if isinstance(stage.get("stage"), Mapping) else None,
            "entity_sha256": _sha(entity),
            "connectivity_sha256": _sha(connectivity),
            "violations_raw_sha256": _sha(violations),
        },
    }


def _resolved_path(value: str | Path) -> Path:
    return Path(value).expanduser().resolve()


def _expected_input_paths(inputs: Mapping[str, Any]) -> list[Path]:
    paths: list[Path] = []
    for key in ("lefs", "libs"):
        values = inputs.get(key, [])
        if isinstance(values, list):
            paths.extend(_resolved_path(item) for item in values)
    for key in ("def", "verilog", "sdc", "spef"):
        value = inputs.get(key)
        if isinstance(value, str):
            paths.append(_resolved_path(value))
    return paths


def _input_validation(inputs: Mapping[str, Any], eccdb: Mapping[str, Any], idb: Mapping[str, Any]) -> dict[str, Any]:
    expected_paths = _expected_input_paths(inputs)
    missing_files = [str(path) for path in expected_paths if not path.is_file()]
    expected_hashes = {str(path): _digest(path) for path in expected_paths if path.is_file()}
    expected_options = inputs.get("options", {})
    expected_top = inputs.get("top")

    def backend(name: str, summary: Mapping[str, Any]) -> dict[str, Any]:
        metadata = summary["metadata"] if isinstance(summary.get("metadata"), Mapping) else {}
        actual_inputs = metadata.get("inputs") if isinstance(metadata.get("inputs"), Mapping) else {}
        actual_options = metadata.get("options")
        actual_top = metadata.get("top")
        input_hashes_match = actual_inputs == expected_hashes
        options_match = actual_options == expected_options
        if expected_top is None:
            top_state = "not_requested"
            top_match: bool | None = None
        elif actual_top is None:
            top_state = "not_recorded_by_runner"
            top_match = None
        else:
            top_state = "matched" if actual_top == expected_top else "mismatched"
            top_match = actual_top == expected_top
        return {
            "backend": name,
            "input_hashes_match": input_hashes_match,
            "input_hash_expected": expected_hashes,
            "input_hash_actual": actual_inputs,
            "options_match": options_match,
            "options_expected": expected_options,
            "options_actual": actual_options,
            "top_expected": expected_top,
            "top_actual": actual_top,
            "top_state": top_state,
            "top_match": top_match,
            "module": metadata.get("module"),
            "module_sha256": metadata.get("module_sha256"),
            "status": metadata.get("status"),
            "status_passed": metadata.get("status") == "passed",
        }

    eccdb_check = backend("eccdb", eccdb)
    idb_check = backend("idb", idb)
    module_hash_recorded = isinstance(eccdb_check["module_sha256"], str) and isinstance(idb_check["module_sha256"], str)
    module_hash_equal = module_hash_recorded and eccdb_check["module_sha256"] == idb_check["module_sha256"]
    complete = (
        not missing_files
        and eccdb_check["input_hashes_match"]
        and idb_check["input_hashes_match"]
        and eccdb_check["options_match"]
        and idb_check["options_match"]
        and eccdb_check["top_state"] in {"not_requested", "matched"}
        and idb_check["top_state"] in {"not_requested", "matched"}
        and eccdb_check["status_passed"]
        and idb_check["status_passed"]
        and module_hash_equal
    )
    return {
        "expected_manifest": _stable(dict(inputs)),
        "missing_requested_files": missing_files,
        "eccdb": eccdb_check,
        "idb": idb_check,
        "module_sha256_equal": module_hash_equal,
        "module_sha256_recorded": module_hash_recorded,
        "complete": complete,
        "boundary": (
            "The current runner may not record `top` in metadata. In that case output equality is reported, "
            "but strict_semantic_equal remains false because the requested Verilog top was not independently verified."
        ),
    }


def _mapped_violations(summary: Mapping[str, Any]) -> list[Any] | None:
    mapping = summary["layer_mapping"]
    if not isinstance(mapping, Mapping) or not mapping.get("complete_for_report"):
        return None
    layer_map = mapping.get("id_to_name", {})
    if not isinstance(layer_map, Mapping):
        return None
    component_evidence = _component_evidence(summary["violations"])
    if not component_evidence["complete"]:
        return None
    return _sorted_multiset(_normalise_component_ids(_replace_layers(violation, layer_map)) for violation in summary["violations"])


def _component_evidence(violations: Iterable[Any]) -> dict[str, Any]:
    """Determine whether component IDs can be replaced by their geometry.

    iLVS graph component IDs are constructed while each adapter builds its
    local routing graph.  They are not stable physical IDs.  The reporter
    emits a ``component`` field on component-derived shapes, so it is safe to
    remove the numeric component identity only when every referenced component
    has at least one such shape.  Otherwise equality would hide missing data.
    """
    unresolved: list[dict[str, Any]] = []
    for index, violation in enumerate(violations):
        if not isinstance(violation, Mapping):
            unresolved.append({"violation_index": index, "reason": "not_an_object"})
            continue
        component_ids = violation.get("components", [])
        if not isinstance(component_ids, list) or not component_ids:
            continue
        shape_component_ids = {
            _encode(shape.get("component"))
            for shape in violation.get("shapes", [])
            if isinstance(shape, Mapping) and "component" in shape
        }
        expected_component_ids = {_encode(identifier) for identifier in component_ids}
        missing = [identifier for identifier in component_ids if _encode(identifier) not in shape_component_ids]
        unexpected = [
            shape.get("component")
            for shape in violation.get("shapes", [])
            if isinstance(shape, Mapping) and "component" in shape and _encode(shape.get("component")) not in expected_component_ids
        ]
        if missing or unexpected:
            unresolved.append({
                "violation_index": index,
                "component_ids_without_geometry": _compact_for_output(missing),
                "component_ids_without_geometry_count": len(missing),
                "geometry_for_unlisted_component_ids": _compact_for_output(unexpected),
                "geometry_for_unlisted_component_ids_count": len(unexpected),
            })
    return {"complete": not unresolved, "unresolved_sample": unresolved[:SAMPLE_LIMIT]}


def _normalise_component_ids(violation: Any) -> Any:
    """Replace a graph component ID with its reported geometry fingerprint.

    Merely deleting the ID loses component grouping: two physically separate
    connected components could then look like one.  A token derived from the
    complete multiset of *reported* shapes preserves grouping while avoiding
    adapter-local numeric IDs.  It is intentionally labelled by behavior in
    the result documentation: iLVS's report does not prove that it emitted
    every shape in the whole layout graph.
    """
    if not isinstance(violation, Mapping):
        return violation
    component_ids = violation.get("components", [])
    if not isinstance(component_ids, list) or not component_ids:
        return _canonicalise_violation(violation)
    shape_groups: dict[str, list[Any]] = {_encode(identifier): [] for identifier in component_ids}
    for shape in violation.get("shapes", []):
        if not isinstance(shape, Mapping) or "component" not in shape:
            continue
        identifier = _encode(shape["component"])
        shape_without_component = {key: nested for key, nested in shape.items() if key != "component"}
        shape_groups[identifier].append(_canonicalise_violation(shape_without_component))
    token_by_id = {
        identifier: "reported-component-shapes:" + _sha(_sorted_multiset(shapes))
        for identifier, shapes in shape_groups.items()
    }
    result: dict[str, Any] = {}
    for key, nested in violation.items():
        if key == "components":
            result[key] = _sorted_multiset(token_by_id[_encode(identifier)] for identifier in component_ids)
        elif key == "shapes" and isinstance(nested, list):
            shapes: list[Any] = []
            for shape in nested:
                if isinstance(shape, Mapping) and "component" in shape:
                    shape = {
                        **{field: value for field, value in shape.items() if field != "component"},
                        "component_geometry": token_by_id[_encode(shape["component"])],
                    }
                shapes.append(_canonicalise_violation(shape))
            result[key] = _sorted_multiset(shapes)
        else:
            result[key] = _canonicalise_violation(nested)
    return result


def _escaped_name_diagnostic(eccdb: Mapping[str, Any], idb: Mapping[str, Any], raw: Mapping[str, Any]) -> dict[str, Any]:
    """Surface, but do not erase, likely spelling-only identifier differences."""
    raw_strings = {
        item
        for violation in eccdb["violations"] + idb["violations"]
        for item in _identifier_strings(violation)
        if "\\" in item
    }
    if not raw_strings:
        return {
            "diagnostic_only": True,
            "raw_violation_multiset_equal": raw["equal"],
            "status": "not_applicable_no_escaped_identifier",
            "note": "No name decoding participates in strict equality; neither report contains an escaped identifier.",
        }
    decoded_eccdb = _sorted_multiset(_decode_identifier_fields(value) for value in eccdb["violations"])
    decoded_idb = _sorted_multiset(_decode_identifier_fields(value) for value in idb["violations"])
    decoded = _counter_delta(decoded_eccdb, decoded_idb)
    return {
        "diagnostic_only": True,
        "raw_violation_multiset_equal": raw["equal"],
        "equal_if_only_identifier_escapes_are_decoded": decoded["equal"],
        "escaped_identifier_samples": sorted(raw_strings)[:SAMPLE_LIMIT],
        "note": "No name decoding participates in strict equality; this only narrows investigation.",
    }


def _identifier_strings(value: Any) -> list[str]:
    if not isinstance(value, Mapping):
        return []
    values: list[str] = []
    for key in ("net", "driver", "instance"):
        nested = value.get(key)
        if isinstance(nested, str):
            values.append(nested)
    for key in ("nets", "terminals"):
        nested = value.get(key)
        if isinstance(nested, list):
            values.extend(item for item in nested if isinstance(item, str))
    return values


def build_report(eccdb_root: Path, idb_root: Path, inputs: Mapping[str, Any]) -> dict[str, Any]:
    eccdb = _summarise_backend(eccdb_root)
    idb = _summarise_backend(idb_root)
    validation = _input_validation(inputs, eccdb, idb)

    entity_delta = _counter_delta(eccdb["entity"], idb["entity"])
    connectivity_delta = _counter_delta(eccdb["connectivity"], idb["connectivity"])
    raw_violation_delta = _counter_delta(eccdb["violations"], idb["violations"])
    component_evidence = {
        "eccdb": _component_evidence(eccdb["violations"]),
        "idb": _component_evidence(idb["violations"]),
    }
    mapped_eccdb = _mapped_violations(eccdb)
    mapped_idb = _mapped_violations(idb)
    if mapped_eccdb is None or mapped_idb is None:
        normalized_violation_delta: dict[str, Any] = {
            "equal": False,
            "status": "inconclusive_missing_layer_mapping_or_component_geometry",
            "eccdb_missing_layer_ids": eccdb["layer_mapping"]["missing_ids"],
            "idb_missing_layer_ids": idb["layer_mapping"]["missing_ids"],
            "component_evidence": component_evidence,
        }
    else:
        normalized_violation_delta = {"status": "completed", **_counter_delta(mapped_eccdb, mapped_idb)}

    entity_equal = entity_delta["equal"]
    connectivity_equal = connectivity_delta["equal"]
    mapped_equal = normalized_violation_delta["equal"]
    clean = {
        "eccdb": eccdb["summary"]["violation_count"] == 0,
        "idb": idb["summary"]["violation_count"] == 0,
    }
    clean["both"] = clean["eccdb"] and clean["idb"]
    strict = validation["complete"] and entity_equal and connectivity_equal and mapped_equal
    status = "passed" if strict else ("inconclusive" if not validation["complete"] or mapped_eccdb is None or mapped_idb is None else "mismatch")
    raw_canonical_eccdb = {
        "entity": eccdb["entity"],
        "connectivity": eccdb["connectivity"],
        "violations": eccdb["violations"],
    }
    raw_canonical_idb = {
        "entity": idb["entity"],
        "connectivity": idb["connectivity"],
        "violations": idb["violations"],
    }
    normalized_canonical_eccdb = {
        "entity": eccdb["entity"],
        "connectivity": eccdb["connectivity"],
        "violations": mapped_eccdb,
    }
    normalized_canonical_idb = {
        "entity": idb["entity"],
        "connectivity": idb["connectivity"],
        "violations": mapped_idb,
    }

    # Do not duplicate the full report in this comparison output.  The source
    # ilvs.json remains at each backend root; this file keeps exact hashes and
    # bounded samples so large PG components do not inflate diagnostics.
    def public_backend(summary: Mapping[str, Any]) -> dict[str, Any]:
        return {
            "root": summary["root"],
            "metadata": summary["metadata"],
            "stage": summary["stage"],
            "layer_mapping": summary["layer_mapping"],
            "summary": {
                **summary["summary"],
                "violation_reporting_scope": "full report retained in ilvs.json; comparison JSON stores exact hashes and bounded samples",
            },
        }

    return {
        "schema_version": 1,
        "task": "lvs",
        "input_validation": validation,
        "eccdb": public_backend(eccdb),
        "idb": public_backend(idb),
        "comparison": {
            "status": status,
            "strict_semantic_equal": strict,
            "entity": entity_delta,
            "connectivity": connectivity_delta,
            "violations_raw": raw_violation_delta,
            "report_order_diagnostic": {
                "eccdb_violation_order_sha256": eccdb["report_order_sha256"],
                "idb_violation_order_sha256": idb["report_order_sha256"],
                "equal": eccdb["report_order_sha256"] == idb["report_order_sha256"],
                "note": "Report vector order is retained as a diagnostic only; violation contents are compared as multisets.",
            },
            "violations_layer_name_normalized": normalized_violation_delta,
            "component_id_normalization": component_evidence,
            "escaped_identifier_diagnostic": _escaped_name_diagnostic(eccdb, idb, raw_violation_delta),
            "full_canonical_sha256": {
                "raw": {
                    "eccdb": _sha(raw_canonical_eccdb),
                    "idb": _sha(raw_canonical_idb),
                    "equal": _sha(raw_canonical_eccdb) == _sha(raw_canonical_idb),
                },
                "layer_name_normalized": (
                    {
                        "status": "completed",
                        "eccdb": _sha(normalized_canonical_eccdb),
                        "idb": _sha(normalized_canonical_idb),
                        "equal": _sha(normalized_canonical_eccdb) == _sha(normalized_canonical_idb),
                    }
                    if mapped_eccdb is not None and mapped_idb is not None
                    else {"status": "inconclusive", "eccdb": None, "idb": None, "equal": False}
                ),
            },
            "LVS_clean": clean,
            "note": (
                "strict_semantic_equal only describes backend agreement; it does not mean the design is LVS clean. "
                "Full reports remain in each backend's ilvs.json; large nested diagnostics are intentionally display-bounded."
            ),
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--eccdb", type=Path, required=True, help="EccDB iLVS stage directory")
    parser.add_argument("--idb", type=Path, required=True, help="iDB iLVS stage directory")
    parser.add_argument("--inputs", type=Path, required=True, help="Explicit run_backend.py-compatible JSON manifest")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    inputs = _read_json(args.inputs)
    if not isinstance(inputs, Mapping):
        raise ValueError(f"inputs manifest must be a JSON object: {args.inputs}")
    report = build_report(args.eccdb.resolve(), args.idb.resolve(), inputs)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    comparison = report["comparison"]
    print(json.dumps({
        "task": "lvs",
        "status": comparison["status"],
        "strict_semantic_equal": comparison["strict_semantic_equal"],
        "LVS_clean": comparison["LVS_clean"],
    }, sort_keys=True))
    return 0 if comparison["status"] == "passed" else 2 if comparison["status"] == "inconclusive" else 1


if __name__ == "__main__":
    raise SystemExit(main())
