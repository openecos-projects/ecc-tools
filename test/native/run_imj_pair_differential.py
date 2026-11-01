#!/usr/bin/env python3
"""Run and semantically compare iMJ iDB/EccDB stages.

The two providers deliberately run in separate processes because iDB and iMJ
legacy singletons are process-global.  This is a correctness harness, not a
performance benchmark: it records wall/RSS only to make a failure reproducible.

No identifier spelling is normalized.  A raw-name strict comparison and a
name-independent physical projection are reported separately so an escaped
identifier discrepancy cannot be hidden behind a string rewrite.
"""

from __future__ import annotations

import argparse
import collections
import dataclasses
import hashlib
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Counter, Iterable, Mapping, Sequence


ROOT = Path(__file__).resolve().parents[2]


@dataclasses.dataclass(frozen=True)
class InstanceRecord:
    master: str
    x: int | None
    y: int | None
    orientation: str | None
    status: str | None
    source: str | None

    def raw_key(self, name: str) -> tuple[object, ...]:
        return (name, self.master, self.x, self.y, self.orientation, self.status, self.source)

    def physical_key(self) -> tuple[object, ...]:
        return (self.master, self.x, self.y, self.orientation, self.status, self.source)


@dataclasses.dataclass
class DefSnapshot:
    components: dict[str, InstanceRecord]
    fills: Counter[tuple[str, int, int, int, int, int | None, bool]]
    pg_endpoints: Counter[tuple[str, str, str]]
    route_hashes: dict[str, str]
    unsupported_fill_records: list[str]


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8", errors="surrogateescape")).hexdigest()


def file_identity(path: Path) -> dict[str, object]:
    resolved = path.resolve(strict=True)
    return {
        "path": str(path),
        "resolved_path": str(resolved),
        "bytes": resolved.stat().st_size,
        "sha256": sha256_file(resolved),
    }


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def section(text: str, name: str) -> str:
    match = re.search(rf"(?ms)^[ \t]*{re.escape(name)}\b.*?^[ \t]*END\s+{re.escape(name)}\b[^\n]*$", text)
    return "" if match is None else match.group(0).strip()


def def_records(body: str) -> Iterable[str]:
    """Yield complete DEF `- ... ;` records without tokenizing identifiers."""
    yield from (match.group(0) for match in re.finditer(r"(?ms)^\s*-\s+.*?;", body))


def parse_components(text: str) -> dict[str, InstanceRecord]:
    values: dict[str, InstanceRecord] = {}
    for record in def_records(section(text, "COMPONENTS")):
        match = re.match(r"(?ms)^\s*-\s+(\S+)\s+(\S+)(.*);\s*$", record)
        if match is None:
            raise ValueError(f"cannot parse COMPONENTS record: {record[:160]!r}")
        name, master, body = match.groups()
        placement = re.search(
            r"\+\s+(PLACED|FIXED|COVER|UNPLACED)\b(?:\s+\(\s*(-?\d+)\s+(-?\d+)\s*\)\s+(\S+))?",
            body,
        )
        source = re.search(r"\+\s+SOURCE\s+(\S+)", body)
        if placement is None:
            value = InstanceRecord(master, None, None, None, None, source.group(1) if source else None)
        else:
            status, x, y, orientation = placement.groups()
            value = InstanceRecord(
                master,
                None if x is None else int(x),
                None if y is None else int(y),
                orientation,
                status,
                source.group(1) if source else None,
            )
        if name in values:
            raise ValueError(f"duplicate raw component name in DEF: {name!r}")
        values[name] = value
    return values


def parse_fills(text: str) -> tuple[Counter[tuple[str, int, int, int, int, int | None, bool]], list[str]]:
    values: Counter[tuple[str, int, int, int, int, int | None, bool]] = collections.Counter()
    unsupported: list[str] = []
    for record in def_records(section(text, "FILLS")):
        match = re.match(r"(?ms)^\s*-\s+LAYER\s+(\S+)(.*);\s*$", record)
        if match is None:
            unsupported.append(record[:240])
            continue
        layer, body = match.groups()
        mask_match = re.search(r"\+\s+MASK\s+(\d+)", body)
        mask = int(mask_match.group(1)) if mask_match else None
        opc = re.search(r"\+\s+OPC\b", body) is not None
        if re.search(r"\b(?:POLYGON|VIA)\b", body):
            unsupported.append(record[:240])
        rectangles = re.findall(r"\bRECT\s+\(\s*(-?\d+)\s+(-?\d+)\s*\)\s+\(\s*(-?\d+)\s+(-?\d+)\s*\)", body)
        for llx, lly, urx, ury in rectangles:
            values[(layer, int(llx), int(lly), int(urx), int(ury), mask, opc)] += 1
    return values, unsupported


def parse_pg_endpoints(text: str) -> Counter[tuple[str, str, str]]:
    values: Counter[tuple[str, str, str]] = collections.Counter()
    routing_start = re.compile(r"\+\s+(?:ROUTED|FIXED|COVER|SHIELD|USE|VOLTAGE|WIDTH|SPACING|SOURCE|ORIGINAL|PATTERN|ESTCAP|WEIGHT|PROPERTY)\b")
    for record in def_records(section(text, "SPECIALNETS")):
        match = re.match(r"(?ms)^\s*-\s+(\S+)(.*);\s*$", record)
        if match is None:
            continue
        net, body = match.groups()
        endpoint_body = routing_start.split(body, maxsplit=1)[0]
        for instance, pin in re.findall(r"\(\s*(\S+)\s+(\S+)\s*\)", endpoint_body):
            values[(net, instance, pin)] += 1
    return values


def snapshot_def(path: Path) -> DefSnapshot:
    text = path.read_text(encoding="utf-8", errors="surrogateescape")
    fills, unsupported = parse_fills(text)
    return DefSnapshot(
        components=parse_components(text),
        fills=fills,
        pg_endpoints=parse_pg_endpoints(text),
        route_hashes={
            "nets_raw_sha256": sha256_text(section(text, "NETS")),
            "specialnets_raw_sha256": sha256_text(section(text, "SPECIALNETS")),
        },
        unsupported_fill_records=unsupported,
    )


def stable_digest(values: Iterable[object]) -> str:
    digest = hashlib.sha256()
    for value in values:
        digest.update(json.dumps(value, ensure_ascii=False, separators=(",", ":"), sort_keys=True).encode("utf-8"))
        digest.update(b"\n")
    return digest.hexdigest()


def component_digest(values: Mapping[str, InstanceRecord], include_name: bool) -> str:
    if include_name:
        ordered = sorted(record.raw_key(name) for name, record in values.items())
    else:
        ordered = sorted(record.physical_key() for record in values.values())
    return stable_digest(ordered)


def counter_digest(values: Counter[tuple[object, ...]]) -> str:
    return stable_digest([(*key, count) for key, count in sorted(values.items())])


def counter_delta(after: Counter[tuple[Any, ...]], before: Counter[tuple[Any, ...]]) -> Counter[tuple[Any, ...]]:
    result = after.copy()
    result.subtract(before)
    return collections.Counter({key: count for key, count in result.items() if count > 0})


def counter_removed(before: Counter[tuple[Any, ...]], after: Counter[tuple[Any, ...]]) -> Counter[tuple[Any, ...]]:
    return counter_delta(before, after)


def counter_report(values: Counter[tuple[Any, ...]], limit: int = 16) -> dict[str, object]:
    return {
        "count": sum(values.values()),
        "unique": len(values),
        "complete_counter_sha256": counter_digest(values),
        "sample": [[*key, count] for key, count in values.most_common(limit)],
    }


def component_changes(before: Mapping[str, InstanceRecord], after: Mapping[str, InstanceRecord]) -> dict[str, object]:
    removed = sorted(set(before) - set(after))
    changed = sorted(name for name in set(before) & set(after) if before[name] != after[name])
    added = sorted(set(after) - set(before))
    return {
        # Filler is allowed to add components, while the other iMJ stages are not.
        # Keep both predicates explicit so callers cannot accidentally use the
        # filler-specific one for metal or antenna integrity.
        "unchanged": not removed and not changed,
        "no_component_mutation": not removed and not changed and not added,
        "removed_count": len(removed),
        "changed_count": len(changed),
        "added_count": len(added),
        "removed_sample": removed[:16],
        "changed_sample": [
            {"name": name, "before": dataclasses.asdict(before[name]), "after": dataclasses.asdict(after[name])}
            for name in changed[:8]
        ],
        "added_raw_name_sha256": stable_digest(added),
    }


def added_components(before: Mapping[str, InstanceRecord], after: Mapping[str, InstanceRecord]) -> dict[str, InstanceRecord]:
    return {name: after[name] for name in after if name not in before}


def components_projection(values: Mapping[str, InstanceRecord]) -> Counter[tuple[object, ...]]:
    return collections.Counter(record.physical_key() for record in values.values())


def pg_projection(values: Counter[tuple[str, str, str]], added: Mapping[str, InstanceRecord]) -> Counter[tuple[object, ...]]:
    """Project only endpoints that reference a newly inserted component."""
    result: Counter[tuple[object, ...]] = collections.Counter()
    for (net, instance, pin), count in values.items():
        record = added.get(instance)
        if record is not None:
            result[(net, *record.physical_key(), pin)] += count
    return result


def snapshot_summary(snapshot: DefSnapshot) -> dict[str, object]:
    return {
        "components": len(snapshot.components),
        "components_raw_sha256": component_digest(snapshot.components, include_name=True),
        "components_physical_sha256": component_digest(snapshot.components, include_name=False),
        "fills": counter_report(snapshot.fills),
        "pg_endpoints": counter_report(snapshot.pg_endpoints),
        "route_hashes": snapshot.route_hashes,
        "unsupported_fill_records": {
            "count": len(snapshot.unsupported_fill_records),
            "sample": snapshot.unsupported_fill_records[:4],
        },
    }


def antenna_payload(result: Mapping[str, Any]) -> dict[str, Any] | None:
    """Accept the probe wrapper and the shared iMJ report shape."""
    direct = result.get("antenna")
    if isinstance(direct, dict):
        return direct
    stage = result.get("stage_result")
    if isinstance(stage, dict) and isinstance(stage.get("antenna"), dict):
        return stage["antenna"]
    return None


def antenna_nonnegative_integer(report: Mapping[str, Any], field: str) -> int | None:
    """Return a trustworthy count; JSON booleans are not numeric counts here."""
    value = report.get(field)
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        return None
    return value


def complete_antenna_violations(report: Mapping[str, Any]) -> tuple[str, ...] | None:
    """Require the complete, unmodified violation vector before comparing it.

    The probe emits raw violation strings, including raw net/component spellings.
    Do not decode, sample, or otherwise normalize them here: an incomplete
    vector must not turn into an apparent equality. Result equality treats the
    complete vector as a multiset, while ``strict_raw_equal`` retains vector
    ordering as a diagnostic.
    """
    violation_count = antenna_nonnegative_integer(report, "violation_count")
    violations = report.get("violations")
    if violation_count is None or not isinstance(violations, list):
        return None
    if not all(isinstance(violation, str) for violation in violations):
        return None
    if len(violations) != violation_count:
        return None
    return tuple(violations)


def antenna_comparison(idb: Mapping[str, Any], eccdb: Mapping[str, Any]) -> dict[str, object]:
    left = antenna_payload(idb)
    right = antenna_payload(eccdb)
    left_checked = isinstance(left, dict) and left.get("checked") is True
    right_checked = isinstance(right, dict) and right.get("checked") is True
    strict_raw_equal = (isinstance(left, dict) and isinstance(right, dict)
                        and {key: value for key, value in left.items() if key != "report_path"}
                        == {key: value for key, value in right.items() if key != "report_path"})
    if not left_checked or not right_checked:
        return {
            "comparable": False,
            "reason": "both providers must expose a checked structured antenna report; empty legacy output is not a pass",
            "idb_checked": left_checked,
            "eccdb_checked": right_checked,
            "coverage_complete": False,
            "strict_raw_equal": strict_raw_equal,
            "strict_raw_equal_includes_raw_violation_names": True,
            "violation_count_equal": False,
            "net_count_equal": False,
            "idb_complete_violation_vector": False,
            "eccdb_complete_violation_vector": False,
            "complete_violations_equal": False,
            "violation_vector_policy": "full raw multiset; no sampling or identifier normalization; strict_raw_equal retains vector order",
            "result_equal": False,
            "idb_report": left,
            "eccdb_report": right,
        }
    left_violation_count = antenna_nonnegative_integer(left, "violation_count")
    right_violation_count = antenna_nonnegative_integer(right, "violation_count")
    left_net_count = antenna_nonnegative_integer(left, "net_count")
    right_net_count = antenna_nonnegative_integer(right, "net_count")
    left_violations = complete_antenna_violations(left)
    right_violations = complete_antenna_violations(right)
    violation_count_equal = left_violation_count is not None and left_violation_count == right_violation_count
    net_count_equal = left_net_count is not None and left_net_count == right_net_count
    complete_violations_equal = (left_violations is not None and right_violations is not None
                                 and collections.Counter(left_violations) == collections.Counter(right_violations))
    return {
        "comparable": True,
        "coverage_complete": left.get("coverage_complete") is True and right.get("coverage_complete") is True,
        # This is intentionally the full backend report.  It includes raw
        # violation strings (and therefore raw identifier spellings), plus
        # backend-specific diagnostics that are useful for investigation but
        # are not an algorithm-result equality gate.
        "strict_raw_equal": strict_raw_equal,
        "strict_raw_equal_includes_raw_violation_names": True,
        "violation_count_equal": violation_count_equal,
        "net_count_equal": net_count_equal,
        "idb_complete_violation_vector": left_violations is not None,
        "eccdb_complete_violation_vector": right_violations is not None,
        "complete_violations_equal": complete_violations_equal,
        "violation_vector_policy": "full raw multiset; no sampling or identifier normalization; strict_raw_equal retains vector order",
        "result_equal": violation_count_equal and net_count_equal and complete_violations_equal,
        "idb": left,
        "eccdb": right,
    }


def stage_integrity(stage: str, before: DefSnapshot, after: DefSnapshot) -> dict[str, object]:
    component = component_changes(before.components, after.components)
    route_unchanged = before.route_hashes == after.route_hashes
    fill_added = counter_delta(after.fills, before.fills)
    fill_removed = counter_removed(before.fills, after.fills)
    pg_added = counter_delta(after.pg_endpoints, before.pg_endpoints)
    pg_removed = counter_removed(before.pg_endpoints, after.pg_endpoints)
    if stage == "filler":
        intended = component["added_count"] > 0
        unintended = not component["unchanged"] or bool(fill_added) or bool(fill_removed) or not route_unchanged or bool(pg_removed)
    elif stage == "metal":
        intended = bool(fill_added)
        unintended = (not component["no_component_mutation"] or bool(fill_removed) or not route_unchanged
                      or bool(pg_added) or bool(pg_removed))
    else:
        intended = True
        unintended = (not component["no_component_mutation"] or bool(fill_added) or bool(fill_removed)
                      or not route_unchanged or bool(pg_added) or bool(pg_removed))
    return {
        "intended_effect_present": intended,
        "no_unintended_design_mutation": not unintended,
        "components": component,
        "routes_unchanged_raw": route_unchanged,
        "fills_added": counter_report(fill_added),
        "fills_removed": counter_report(fill_removed),
        "pg_endpoints_added": counter_report(pg_added),
        "pg_endpoints_removed": counter_report(pg_removed),
    }


def compare_filler(idb_before: DefSnapshot, idb_after: DefSnapshot, ecc_before: DefSnapshot, ecc_after: DefSnapshot) -> dict[str, object]:
    idb_added = added_components(idb_before.components, idb_after.components)
    ecc_added = added_components(ecc_before.components, ecc_after.components)
    raw_equal = idb_added == ecc_added
    idb_physical = components_projection(idb_added)
    ecc_physical = components_projection(ecc_added)
    physical_equal = idb_physical == ecc_physical
    idb_pg = pg_projection(counter_delta(idb_after.pg_endpoints, idb_before.pg_endpoints), idb_added)
    ecc_pg = pg_projection(counter_delta(ecc_after.pg_endpoints, ecc_before.pg_endpoints), ecc_added)
    return {
        "strict_raw_name_equal": raw_equal,
        "physical_projection_equal": physical_equal,
        "idb_added": {
            "count": len(idb_added),
            "raw_semantic_sha256": component_digest(idb_added, include_name=True),
            "physical_semantic": counter_report(idb_physical),
        },
        "eccdb_added": {
            "count": len(ecc_added),
            "raw_semantic_sha256": component_digest(ecc_added, include_name=True),
            "physical_semantic": counter_report(ecc_physical),
        },
        "only_idb_physical": counter_report(counter_delta(idb_physical, ecc_physical)),
        "only_eccdb_physical": counter_report(counter_delta(ecc_physical, idb_physical)),
        "idb_new_filler_pg": counter_report(idb_pg),
        "eccdb_new_filler_pg": counter_report(ecc_pg),
        "new_filler_pg_physical_equal": idb_pg == ecc_pg,
    }


def compare_metal(idb_before: DefSnapshot, idb_after: DefSnapshot, ecc_before: DefSnapshot, ecc_after: DefSnapshot) -> dict[str, object]:
    idb_added = counter_delta(idb_after.fills, idb_before.fills)
    ecc_added = counter_delta(ecc_after.fills, ecc_before.fills)
    return {
        "strict_raw_layer_rectangle_mask_opc_equal": idb_added == ecc_added,
        "idb_added": counter_report(idb_added),
        "eccdb_added": counter_report(ecc_added),
        "only_idb": counter_report(counter_delta(idb_added, ecc_added)),
        "only_eccdb": counter_report(counter_delta(ecc_added, idb_added)),
        "unsupported_fill_syntax": {
            "idb": idb_after.unsupported_fill_records[:4],
            "eccdb": ecc_after.unsupported_fill_records[:4],
            "idb_count": len(idb_after.unsupported_fill_records),
            "eccdb_count": len(ecc_after.unsupported_fill_records),
        },
    }


def compare_case(stage: str, idb_result: Mapping[str, Any], eccdb_result: Mapping[str, Any]) -> dict[str, object]:
    if idb_result.get("status") != "passed" or eccdb_result.get("status") != "passed":
        return {
            "stage": stage,
            "comparison_status": "not_run",
            "idb_status": idb_result.get("status"),
            "eccdb_status": eccdb_result.get("status"),
        }
    idb_before = snapshot_def(Path(idb_result["before_def"]))
    idb_after = snapshot_def(Path(idb_result["output_def"]))
    ecc_before = snapshot_def(Path(eccdb_result["before_def"]))
    ecc_after = snapshot_def(Path(eccdb_result["output_def"]))
    idb_input_physical = components_projection(idb_before.components)
    eccdb_input_physical = components_projection(ecc_before.components)
    output: dict[str, object] = {
        "schema": 1,
        "stage": stage,
        "comparison_status": "complete",
        "input_raw_components_equal": idb_before.components == ecc_before.components,
        "input_physical_components_equal": idb_input_physical == eccdb_input_physical,
        "input_physical_components": {
            "idb": counter_report(idb_input_physical),
            "eccdb": counter_report(eccdb_input_physical),
            "only_idb": counter_report(counter_delta(idb_input_physical, eccdb_input_physical)),
            "only_eccdb": counter_report(counter_delta(eccdb_input_physical, idb_input_physical)),
        },
        "idb_before": snapshot_summary(idb_before),
        "eccdb_before": snapshot_summary(ecc_before),
        "idb_after": snapshot_summary(idb_after),
        "eccdb_after": snapshot_summary(ecc_after),
        "idb_integrity": stage_integrity(stage, idb_before, idb_after),
        "eccdb_integrity": stage_integrity(stage, ecc_before, ecc_after),
    }
    if stage == "filler":
        output["stage_comparison"] = compare_filler(idb_before, idb_after, ecc_before, ecc_after)
    elif stage == "metal":
        output["stage_comparison"] = compare_metal(idb_before, idb_after, ecc_before, ecc_after)
    else:
        output["stage_comparison"] = antenna_comparison(idb_result, eccdb_result)
    return output


def acceptance(report: Mapping[str, Any]) -> tuple[bool, bool]:
    """Return (covered, equal). Successful execution alone is never a pass."""
    if report.get("comparison_status") != "complete":
        return False, False
    workers = report.get("worker_invocations")
    provenance = report.get("provenance_validation")
    workers_valid = isinstance(workers, Mapping) and all(
        isinstance(workers.get(backend), Mapping) and workers[backend].get("valid") is True
        for backend in ("idb", "eccdb")
    )
    provenance_valid = isinstance(provenance, Mapping) and provenance.get("valid") is True
    if not workers_valid or not provenance_valid:
        return False, False
    detail = report["stage_comparison"]
    integrity = all(report[f"{backend}_integrity"]["no_unintended_design_mutation"] for backend in ("idb", "eccdb"))
    input_physical_equal = report.get("input_physical_components_equal") is True
    if report["stage"] == "antenna":
        covered = detail.get("comparable") is True and detail.get("coverage_complete") is True
        return covered, covered and input_physical_equal and integrity and detail.get("result_equal") is True
    intended_effect_present = all(
        report[f"{backend}_integrity"].get("intended_effect_present") is True
        for backend in ("idb", "eccdb")
    )
    if report["stage"] == "filler":
        return True, (input_physical_equal and integrity and intended_effect_present
                      and detail["strict_raw_name_equal"] and detail["new_filler_pg_physical_equal"])
    syntax = detail["unsupported_fill_syntax"]
    covered = syntax["idb_count"] == 0 and syntax["eccdb_count"] == 0
    return covered, (covered and input_physical_equal and integrity and intended_effect_present
                     and detail["strict_raw_layer_rectangle_mask_opc_equal"])


def prepare_fresh_output(output: Path) -> None:
    """Reserve an empty result root without discarding earlier experiment evidence."""
    if output.exists():
        if not output.is_dir():
            raise ValueError(f"fresh run output is not a directory: {output}")
        if any(output.iterdir()):
            raise ValueError(f"fresh run refuses non-empty output directory: {output}; use a new --out path or --compare-only")
    else:
        output.mkdir(parents=True)


def _command_options(command: object) -> dict[str, str] | None:
    if not isinstance(command, list) or not command or not all(isinstance(value, str) for value in command):
        return None
    values: dict[str, str] = {}
    index = 1
    while index < len(command):
        argument = command[index]
        if not argument.startswith("--"):
            index += 1
            continue
        if index + 1 >= len(command) or argument in values:
            return None
        values[argument] = command[index + 1]
        index += 2
    return values


def worker_invocation_state(
    output: Path,
    backend: str,
    stage: str,
    manifest: Path,
    options: Path,
    def_file: Path | None,
    result: object,
) -> dict[str, object]:
    """Verify that a saved worker artifact belongs to this successful invocation."""
    invocation_path = output / "invocation.json"
    reasons: list[str] = []
    state: dict[str, object] = {"path": str(invocation_path), "valid": False, "reasons": reasons}
    if not invocation_path.is_file():
        reasons.append("missing invocation.json")
    else:
        try:
            invocation = read_json(invocation_path)
        except (OSError, ValueError, json.JSONDecodeError) as error:
            reasons.append(f"cannot read invocation.json: {error}")
        else:
            returncode = invocation.get("returncode") if isinstance(invocation, dict) else None
            if isinstance(returncode, bool) or not isinstance(returncode, int) or returncode != 0:
                reasons.append(f"worker returncode is not zero: {returncode!r}")
            command_options = _command_options(invocation.get("command") if isinstance(invocation, dict) else None)
            if command_options is None:
                reasons.append("invocation command is malformed")
            else:
                expected = {
                    "--backend": backend,
                    "--stage": stage,
                    "--manifest": str(manifest),
                    "--out": str(output),
                    "--options": str(options),
                }
                if def_file is not None:
                    expected["--def-file"] = str(def_file)
                elif "--def-file" in command_options:
                    reasons.append("invocation unexpectedly contains --def-file")
                for option, value in expected.items():
                    if command_options.get(option) != value:
                        reasons.append(f"invocation {option} does not match this comparison")
    if not isinstance(result, Mapping):
        reasons.append("result.json is not an object")
    else:
        if result.get("backend") != backend:
            reasons.append("result backend does not match worker directory")
        if result.get("stage") != stage:
            reasons.append("result stage does not match requested stage")
    state["valid"] = not reasons
    return state


def run_backend(
    probe: Path,
    backend: str,
    stage: str,
    manifest: Path,
    options: Path,
    output: Path,
    def_file: Path | None,
    timeout_seconds: int,
) -> None:
    # A fresh top-level output directory is reserved by main().  Refuse an
    # existing worker directory so a failed process can never inherit a prior
    # result.json, DEF, or invocation record.
    output.mkdir(parents=True, exist_ok=False)
    command = [
        str(probe),
        "--backend",
        backend,
        "--stage",
        stage,
        "--manifest",
        str(manifest),
        "--out",
        str(output),
        "--options",
        str(options),
    ]
    if def_file is not None:
        command.extend(["--def-file", str(def_file)])
    started = time.monotonic()
    try:
        completed = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout_seconds, check=False)
        stdout, stderr, returncode = completed.stdout, completed.stderr, completed.returncode
    except subprocess.TimeoutExpired as error:
        stdout = error.stdout or b""
        stderr = error.stderr or b""
        if isinstance(stdout, bytes):
            stdout = stdout.decode("utf-8", errors="replace")
        if isinstance(stderr, bytes):
            stderr = stderr.decode("utf-8", errors="replace")
        stderr += f"\nprobe timed out after {timeout_seconds} seconds\n"
        returncode = 124
    (output / "probe.stdout.log").write_text(stdout, encoding="utf-8")
    (output / "probe.stderr.log").write_text(stderr, encoding="utf-8")
    invocation = {
        "command": command,
        "returncode": returncode,
        "wall_ms": (time.monotonic() - started) * 1000.0,
    }
    (output / "invocation.json").write_text(json.dumps(invocation, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if returncode != 0 and not (output / "result.json").is_file():
        (output / "result.json").write_text(json.dumps({"status": "failed", "backend": backend,
            "stage": stage, "returncode": returncode, "error": "probe exited without a structured result"}, indent=2) + "\n", encoding="utf-8")


def input_provenance(manifest: Path, options: Path, def_file: Path | None) -> dict[str, object]:
    manifest_json = read_json(manifest)
    base = manifest.parent
    lefs = [Path(item) if Path(item).is_absolute() else base / item for item in manifest_json["lef_files"]]
    actual_def = def_file or (Path(manifest_json["def_file"]) if Path(manifest_json["def_file"]).is_absolute() else base / manifest_json["def_file"])
    return {
        "manifest": file_identity(manifest),
        "options": file_identity(options),
        "def": file_identity(actual_def),
        "lefs": [file_identity(path) for path in lefs],
    }


def make_provenance(probe: Path, manifest: Path, options: Path, def_file: Path | None) -> dict[str, object]:
    return {"probe": file_identity(probe), **input_provenance(manifest, options, def_file)}


def compare_only_provenance_state(
    saved: object,
    manifest: Path,
    options: Path,
    def_file: Path | None,
) -> dict[str, object]:
    """Keep saved provenance, while proving it still names the requested inputs."""
    reasons: list[str] = []
    state: dict[str, object] = {"valid": False, "mode": "compare_only", "reasons": reasons}
    if not isinstance(saved, Mapping):
        reasons.append("saved comparison has no provenance object")
        return state
    try:
        current = input_provenance(manifest, options, def_file)
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
        reasons.append(f"cannot fingerprint current inputs: {error}")
        return state
    for key in ("manifest", "options", "def", "lefs"):
        if saved.get(key) != current[key]:
            reasons.append(f"saved provenance does not match current {key}")
    if "probe" not in saved:
        reasons.append("saved provenance has no probe identity")
    state["valid"] = not reasons
    return state


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, default=ROOT / "build" / "bin" / "imj_pair_probe")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--options", type=Path, required=True, help="flat JSON object passed identically to both providers")
    parser.add_argument("--stage", choices=("filler", "metal", "antenna"), required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--case", default="unnamed")
    parser.add_argument("--def-file", type=Path)
    parser.add_argument("--timeout-seconds", type=int, default=3600)
    parser.add_argument("--compare-only", action="store_true")
    args = parser.parse_args(argv)

    probe = args.probe.resolve()
    manifest = args.manifest.resolve()
    options = args.options.resolve()
    output = args.out.resolve()
    def_file = args.def_file.resolve() if args.def_file else None
    fresh_provenance: dict[str, object] | None = None
    if not args.compare_only:
        if not probe.is_file():
            parser.error(f"probe does not exist: {probe}")
        try:
            prepare_fresh_output(output)
            # Capture source identities before either worker can consume them.
            fresh_provenance = make_provenance(probe, manifest, options, def_file)
        except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
            parser.error(str(error))
        for backend in ("idb", "eccdb"):
            run_backend(probe, backend, args.stage, manifest, options, output / backend, def_file, args.timeout_seconds)
    elif not output.is_dir():
        parser.error(f"--compare-only requires an existing output directory: {output}")

    idb_path = output / "idb" / "result.json"
    eccdb_path = output / "eccdb" / "result.json"
    if not idb_path.is_file() or not eccdb_path.is_file():
        parser.error("compare requires idb/result.json and eccdb/result.json")
    idb = read_json(idb_path)
    eccdb = read_json(eccdb_path)
    workers = {
        backend: worker_invocation_state(output / backend, backend, args.stage, manifest, options, def_file, result)
        for backend, result in (("idb", idb), ("eccdb", eccdb))
    }
    if all(state["valid"] is True for state in workers.values()):
        report = compare_case(args.stage, idb, eccdb)
    else:
        report = {
            "schema": 1,
            "stage": args.stage,
            "comparison_status": "not_run",
            "reason": "worker invocation or result does not belong to this successful request",
            "idb_status": idb.get("status") if isinstance(idb, Mapping) else None,
            "eccdb_status": eccdb.get("status") if isinstance(eccdb, Mapping) else None,
        }
    report["worker_invocations"] = workers
    report["case"] = args.case
    if args.compare_only:
        prior_comparison = output / "comparison.json"
        previous = read_json(prior_comparison) if prior_comparison.is_file() else {}
        saved_provenance = previous.get("provenance") if isinstance(previous, Mapping) else None
        report["provenance"] = saved_provenance
        report["provenance_validation"] = compare_only_provenance_state(saved_provenance, manifest, options, def_file)
    else:
        report["provenance"] = fresh_provenance
        report["provenance_validation"] = {"valid": fresh_provenance is not None, "mode": "captured_before_workers"}
    report["backend_result_paths"] = {"idb": str(idb_path), "eccdb": str(eccdb_path)}
    report["coverage_complete"], report["passed"] = acceptance(report)
    (output / "comparison.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report["passed"] else (1 if report["coverage_complete"] else 2)


if __name__ == "__main__":
    sys.exit(main())
