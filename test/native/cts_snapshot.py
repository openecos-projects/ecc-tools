#!/usr/bin/env python3
# SPDX-License-Identifier: MulanPSL-2.0
"""Extract and compare the CTS-relevant part of a DEF file.

This module intentionally does not try to be a DEF implementation.  It is a
small, read-only diagnostic view used by the iCTS differential experiments.
The view keeps the information needed to answer three questions without
loading either backend:

* which components were added (and where were they placed),
* which nets are marked ``USE CLOCK``, and
* which logical endpoints belong to each clock net (as a multiset).

The parser is line/record based and has no third-party dependencies.  It
accepts ordinary DEF output from both OpenDB/iDB and EccDB.  The original DEF
is never modified.  All public return values are JSON-serializable ``dict``
objects so reports can be written directly into an experiment artifact.
"""

from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import re
from typing import Any, Iterable, Mapping


_SECTION_START = re.compile(
    r"(?im)^\s*(?P<name>COMPONENTS|NETS|SPECIALNETS)\s+\d+\s*;\s*$"
)
_SECTION_END = re.compile(r"(?im)^\s*END\s+(?P<name>COMPONENTS|NETS|SPECIALNETS)\s*$")
_RECORD_START = re.compile(r"^\s*-\s+")
_COMPONENT_HEAD = re.compile(r"^\s*-\s+(?P<name>\S+)\s+(?P<master>\S+)")
_PLACEMENT = re.compile(
    r"\(\s*(?P<x>-?\d+)\s+(?P<y>-?\d+)\s*\)\s*(?P<orient>\S+)",
    re.IGNORECASE,
)
_PLACEMENT_STATUS = re.compile(r"\+\s*(?P<status>FIXED|PLACED|COVER|UNPLACED)\b", re.IGNORECASE)
_USE = re.compile(r"\+\s*USE\s+(?P<use>[A-Za-z_][A-Za-z0-9_]*)\b", re.IGNORECASE)
_PAREN_PAIR = re.compile(r"\(\s*(?P<first>[^\s()]+)\s+(?P<second>[^\s()]+)\s*\)")
_INTEGER = re.compile(r"^-?\d+$")


def _section_records(text: str, section: str) -> list[str]:
    """Return complete ``- ... ;`` records from one top-level DEF section."""

    wanted = section.upper()
    start: re.Match[str] | None = None
    for match in _SECTION_START.finditer(text):
        if match.group("name").upper() == wanted:
            start = match
            break
    if start is None:
        return []

    end = None
    for match in _SECTION_END.finditer(text, start.end()):
        if match.group("name").upper() == wanted:
            end = match
            break
    if end is None:
        raise ValueError(f"DEF section {section} has no END {section}")

    records: list[str] = []
    current: list[str] = []
    for line in text[start.end() : end.start()].splitlines(keepends=True):
        if _RECORD_START.match(line):
            if current:
                raise ValueError(f"unterminated {section} record before {line.strip()!r}")
            current.append(line)
        elif current:
            current.append(line)
        elif line.strip():
            # Comments/blank lines are harmless; non-record text is almost
            # always a malformed section and should not silently disappear.
            if not line.lstrip().startswith("#"):
                raise ValueError(f"unexpected text in {section}: {line.strip()!r}")
        if current and ";" in line:
            records.append("".join(current))
            current.clear()
    if current:
        raise ValueError(f"unterminated {section} record")
    return records


def _first_tokens(record: str) -> tuple[str, str]:
    match = _COMPONENT_HEAD.match(record)
    if match is None:
        # Net and special-net records use the same leading ``- name`` form,
        # but accepting escaped punctuation is useful for diagnostics.
        tokens = record.lstrip()[1:].split(None, 1)
        if not tokens:
            raise ValueError(f"DEF record has no name: {record[:80]!r}")
        return tokens[0], tokens[1].split(None, 1)[0] if len(tokens) > 1 else ""
    return match.group("name"), match.group("master")


def _parse_component(record: str) -> dict[str, Any]:
    match = _COMPONENT_HEAD.match(record)
    if match is None:
        raise ValueError(f"invalid COMPONENTS record: {record[:120]!r}")
    status_match = _PLACEMENT_STATUS.search(record)
    placement_match = _PLACEMENT.search(record)
    placement: dict[str, Any] | None = None
    if placement_match is not None:
        placement = {
            "x": int(placement_match.group("x")),
            "y": int(placement_match.group("y")),
            "orient": placement_match.group("orient"),
        }
    return {
        "name": match.group("name"),
        "master": match.group("master"),
        "status": status_match.group("status").upper() if status_match else None,
        "placement": placement,
    }


def _endpoint_pairs(record: str) -> list[dict[str, Any]]:
    endpoints: list[dict[str, Any]] = []
    for match in _PAREN_PAIR.finditer(record):
        first, second = match.group("first"), match.group("second")
        # Routing points, via dimensions, and offsets are numeric pairs.  A
        # logical DEF endpoint always starts with an instance name or PIN.
        if _INTEGER.fullmatch(first) and _INTEGER.fullmatch(second):
            continue
        if first.startswith("+") or second.startswith("+"):
            continue
        if first.upper() == "PIN":
            endpoints.append({"kind": "io", "instance": None, "pin": second})
        else:
            endpoints.append({"kind": "instance", "instance": first, "pin": second})
    return endpoints


def _endpoint_key(endpoint: Mapping[str, Any]) -> str:
    kind = str(endpoint.get("kind", "instance"))
    instance = endpoint.get("instance")
    pin = endpoint.get("pin", "")
    return f"{kind}:{'' if instance is None else instance}:{pin}"


def _parse_net(record: str, *, include_endpoints: bool = True) -> dict[str, Any]:
    name, _ = _first_tokens(record)
    use_match = _USE.search(record)
    use = use_match.group("use").upper() if use_match else None
    endpoints = _endpoint_pairs(record) if include_endpoints else []
    endpoint_keys = sorted(_endpoint_key(endpoint) for endpoint in endpoints)
    return {
        "name": name,
        "use": use,
        "endpoints": endpoints,
        "endpoint_multiset": endpoint_keys,
    }


def _component_map(snapshot: Mapping[str, Any]) -> dict[str, dict[str, Any]]:
    return {str(item["name"]): dict(item) for item in snapshot.get("components", [])}


def _net_map(snapshot: Mapping[str, Any]) -> dict[str, dict[str, Any]]:
    return {str(item["name"]): dict(item) for item in snapshot.get("nets", [])}


def _is_clock_candidate(name: str, use: str | None) -> bool:
    """Keep clock roots even when a legacy writer labels them SIGNAL."""

    lowered = name.lower()
    return use == "CLOCK" or lowered == "clock" or lowered.startswith("cts_flow_clk_")


def snapshot_def(path: str | Path, *, include_all_nets: bool = False) -> dict[str, Any]:
    """Read a DEF and return a JSON-serializable CTS snapshot.

    ``include_all_nets`` is useful when diagnosing a USE mismatch (for
    example CLOCK versus SIGNAL).  By default ``nets`` contains only clock
    nets while ``net_uses`` retains the compact name/use map for every regular
    net.  Special nets are reported separately and are never treated as CTS
    clock nets.
    """

    source = Path(path).resolve()
    text = source.read_text(encoding="utf-8", errors="replace")
    components = sorted(
        (_parse_component(record) for record in _section_records(text, "COMPONENTS")),
        key=lambda item: str(item["name"]),
    )
    regular: list[dict[str, Any]] = []
    for record in _section_records(text, "NETS"):
        name, _ = _first_tokens(record)
        use_match = _USE.search(record)
        use = use_match.group("use").upper() if use_match else None
        regular.append(_parse_net(record, include_endpoints=_is_clock_candidate(name, use)))
    # SPECIALNETS may contain millions of repeated PG endpoints.  They are
    # not part of the CTS topology snapshot; retaining only name/use keeps the
    # diagnostic bounded and avoids turning a comparison into a second DEF
    # parser.
    special = []
    for record in _section_records(text, "SPECIALNETS"):
        name, _ = _first_tokens(record)
        use_match = _USE.search(record)
        use = use_match.group("use").upper() if use_match else None
        special.append({"name": name, "use": use, "endpoints": [], "endpoint_multiset": []})
    regular.sort(key=lambda item: str(item["name"]))
    special.sort(key=lambda item: str(item["name"]))
    clock_nets = [item for item in regular if _is_clock_candidate(str(item["name"]), item["use"])]
    selected_nets = regular if include_all_nets else clock_nets
    return {
        "schema_version": 1,
        "source": str(source),
        "component_count": len(components),
        "components": components,
        "net_count": len(regular),
        "clock_net_count": len(clock_nets),
        "nets": selected_nets,
        "clock_nets": clock_nets,
        "net_uses": [
            {"name": item["name"], "use": item["use"]}
            for item in regular
        ],
        "special_net_count": len(special),
        "special_net_uses": [
            {"name": item["name"], "use": item["use"]}
            for item in special
        ],
    }


def _counter_diff(left: Iterable[str], right: Iterable[str]) -> dict[str, Any]:
    left_counter, right_counter = Counter(left), Counter(right)
    left_only = left_counter - right_counter
    right_only = right_counter - left_counter
    return {
        "equal": not left_only and not right_only,
        "left_only": sorted(left_only.elements()),
        "right_only": sorted(right_only.elements()),
    }


def compare_snapshots(left: Mapping[str, Any], right: Mapping[str, Any]) -> dict[str, Any]:
    """Compare two snapshots without declaring either backend authoritative."""

    left_components, right_components = _component_map(left), _component_map(right)
    left_names, right_names = set(left_components), set(right_components)
    added_left = sorted(left_names - right_names)
    added_right = sorted(right_names - left_names)
    placement_mismatches: list[dict[str, Any]] = []
    for name in sorted(left_names & right_names):
        l_item, r_item = left_components[name], right_components[name]
        for field in ("master", "status", "placement"):
            if l_item.get(field) != r_item.get(field):
                placement_mismatches.append({
                    "component": name,
                    "field": field,
                    "left": l_item.get(field),
                    "right": r_item.get(field),
                })

    # Prefer the explicit clock_nets field.  ``snapshot_def(...,
    # include_all_nets=True)`` intentionally puts every regular net in
    # ``nets``; using that field here would accidentally treat SIGNAL nets as
    # clock topology.
    left_nets = _net_map({"nets": left.get("clock_nets", left.get("nets", []))})
    right_nets = _net_map({"nets": right.get("clock_nets", right.get("nets", []))})
    left_clock, right_clock = set(left_nets), set(right_nets)
    clock_use_mismatches: list[dict[str, Any]] = []
    left_uses = _net_map({"nets": left.get("net_uses", [])})
    right_uses = _net_map({"nets": right.get("net_uses", [])})
    for name in sorted(set(left_uses) & set(right_uses)):
        # net_uses entries have only name/use; _net_map is convenient and
        # keeps this comparison independent of whether ``nets`` was filtered.
        l_use = left_uses[name].get("use")
        r_use = right_uses[name].get("use")
        if l_use != r_use:
            clock_use_mismatches.append({"net": name, "left": l_use, "right": r_use})

    endpoint_mismatches: list[dict[str, Any]] = []
    for name in sorted(left_clock & right_clock):
        l_endpoints = left_nets[name].get("endpoint_multiset", [])
        r_endpoints = right_nets[name].get("endpoint_multiset", [])
        endpoint_diff = _counter_diff(l_endpoints, r_endpoints)
        if not endpoint_diff["equal"]:
            endpoint_mismatches.append({"net": name, **endpoint_diff})

    components_equal = not added_left and not added_right and not placement_mismatches
    clocks_equal = not (left_clock ^ right_clock) and not clock_use_mismatches
    endpoints_equal = not endpoint_mismatches
    return {
        "schema_version": 1,
        "equal": components_equal and clocks_equal and endpoints_equal,
        "components": {
            "left_count": len(left_components),
            "right_count": len(right_components),
            "left_only": added_left,
            "right_only": added_right,
            "left_only_details": [left_components[name] for name in added_left],
            "right_only_details": [right_components[name] for name in added_right],
            "placement_mismatches": placement_mismatches,
            "equal": components_equal,
        },
        "clock_nets": {
            "left_count": len(left_clock),
            "right_count": len(right_clock),
            "left_only": sorted(left_clock - right_clock),
            "right_only": sorted(right_clock - left_clock),
            "use_mismatches": clock_use_mismatches,
            "equal": clocks_equal,
        },
        "endpoints": {
            "mismatches": endpoint_mismatches,
            "equal": endpoints_equal,
        },
        "note": "Comparison is represented-data equality only; neither backend is treated as ground truth.",
    }


def compare_def_files(left_path: str | Path, right_path: str | Path) -> dict[str, Any]:
    """Convenience API: parse two DEF files and compare their CTS views."""

    left = snapshot_def(left_path)
    right = snapshot_def(right_path)
    result = compare_snapshots(left, right)
    result["left"] = left
    result["right"] = right
    return result


def _main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("left", type=Path)
    parser.add_argument("right", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = compare_def_files(args.left, args.right)
    rendered = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    else:
        print(rendered, end="")
    return 0 if report["equal"] else 1


if __name__ == "__main__":
    raise SystemExit(_main())
