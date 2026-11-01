#!/usr/bin/env python3
# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

"""Report one InstanceTerm dense-map replay against saved same-day references."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import sys


BACKENDS = ("baseline", "partition", "dense_map", "odb", "idb")
LABELS = {"baseline": "Baseline", "partition": "Partition", "dense_map": "Dense map",
          "odb": "ODB", "idb": "iDB"}
COLORS = {"baseline": "#9b4d4d", "partition": "#7066a8", "dense_map": "#16836b",
          "odb": "#3f6fa8", "idb": "#c88828"}
HOTSPOTS = ("find_pin", "read_instance_pins", "net_pins")


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--references", type=Path, required=True,
                        help="registry-partition replay summary.json, or its containing directory")
    parser.add_argument("--candidate", type=Path, required=True, help="fresh dense-map candidate.json")
    parser.add_argument("--candidate-time", type=Path, required=True, help="GNU time -v candidate.time.txt")
    parser.add_argument("--output-dir", type=Path, required=True)
    return parser.parse_args()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def time_stats(path: Path) -> dict:
    prefixes = {
        "user_seconds": "User time (seconds):",
        "system_seconds": "System time (seconds):",
        "wall_seconds": "Elapsed (wall clock) time (h:mm:ss or m:ss):",
        "max_rss_kib": "Maximum resident set size (kbytes):",
    }
    lines = [line.strip() for line in path.read_text(encoding="utf-8", errors="replace").splitlines()]
    values = {name: next(line[len(prefix):].strip() for line in lines if line.startswith(prefix))
              for name, prefix in prefixes.items()}
    clock = [float(value) for value in values["wall_seconds"].split(":")]
    wall_seconds = clock[-1] + (60 * clock[-2] if len(clock) > 1 else 0) + (3600 * clock[-3] if len(clock) > 2 else 0)
    return {"max_rss_kib": int(values["max_rss_kib"]), "wall_seconds": wall_seconds,
            "user_seconds": float(values["user_seconds"]), "system_seconds": float(values["system_seconds"])}


def reference_path(path: Path) -> Path:
    return path / "summary.json" if path.is_dir() else path


def validate(references: dict, candidate: dict) -> tuple[dict[str, dict], list[str], list[str]]:
    if references.get("schema") != "registry_partition_trace_replay_v1":
        raise RuntimeError("references are not a RegistryPartitionReplay summary")
    if candidate.get("schema") != "tritonroute_db_trace_replay_v2" or candidate.get("backend") != "eccdb":
        raise RuntimeError("candidate is not an EccDB trace replay result")
    raw = {"baseline": references["runs"]["baseline"]["replay"],
           "partition": references["runs"]["candidate"]["replay"],
           "dense_map": candidate, "odb": references["runs"]["odb"]["replay"],
           "idb": references["runs"]["idb"]["replay"]}
    baseline = raw["baseline"]
    if (candidate["validation"].get("all_events") is not True
            or candidate["validation"].get("all_supported_events") is not True
            or candidate["unsupported_operations"]):
        raise RuntimeError("candidate must support and validate every observed event")
    for name, result in raw.items():
        if result["events"] != baseline["events"] or result["operations"] != baseline["operations"]:
            raise RuntimeError(f"{name} does not have the reference event and operation counts")
    if (sum(candidate["operations"].values()) != candidate["events"]
            or candidate["validated_events"] != candidate["events"]
            or candidate["measured_operations"] != candidate["operations"]):
        raise RuntimeError("candidate did not measure every observed operation")
    if set(candidate["timing_ns"]) != set(candidate["operations"]) or sum(candidate["timing_ns"].values()) != candidate["total_ns"]:
        raise RuntimeError("candidate timing map is incomplete")
    for field in references["validation"]["full_population_fields_equal"]:
        if candidate["validation"][field] != baseline["validation"][field]:
            raise RuntimeError(f"candidate final population differs for {field}")
    if (candidate["validation"].get("shared_geometry") is not True
            or candidate["validation"].get("endpoint_extensions") is not True
            or candidate["validation"].get("unvalidated_wire_objects") != 0):
        raise RuntimeError("candidate did not complete final route validation")
    return raw, references["full_odb_eccdb_operations"], references["common_three_backend_operations"]


def ratio(numerator: int, denominator: int) -> float | None:
    return numerator / denominator if denominator else None


def percent_change(current: int, reference: int) -> float | None:
    return 100 * (current - reference) / reference if reference else None


def timing(result: dict, operation: str) -> int | None:
    return result["timing_ns"].get(operation)


def make_rows(raw: dict[str, dict], operations: dict[str, int], events: int) -> list[dict]:
    rows = []
    for operation in sorted(operations, key=lambda name: (-operations[name], name)):
        calls = operations[operation]
        idb_unsupported = raw["idb"]["unsupported_operations"].get(operation, 0)
        if idb_unsupported not in (0, calls):
            raise RuntimeError(f"iDB partially supports {operation}")
        values = {backend: timing(result, operation) for backend, result in raw.items()}
        rows.append({
            "operation": operation,
            "calls": calls,
            "event_share_percent": 100 * calls / events,
            "calls_per_million_events": 1_000_000 * calls / events,
            **{f"{backend}_ns": values[backend] for backend in BACKENDS},
            "idb_status": "unsupported" if idb_unsupported else "measured",
            "idb_unsupported_calls": idb_unsupported,
            "odb_over_dense_map": ratio(values["odb"], values["dense_map"]),
            "baseline_over_dense_map": ratio(values["baseline"], values["dense_map"]),
            "partition_over_dense_map": ratio(values["partition"], values["dense_map"]),
            "dense_map_change_vs_baseline_percent": percent_change(values["dense_map"], values["baseline"]),
            "dense_map_change_vs_partition_percent": percent_change(values["dense_map"], values["partition"]),
        })
    return rows


def total(raw: dict[str, dict], operations: list[str], scope: str) -> dict:
    values = {backend: sum(raw[backend]["timing_ns"][operation] for operation in operations)
              for backend in BACKENDS if backend != "idb" or scope == "common_18"}
    return {
        "scope": scope,
        "operations": len(operations),
        "calls": sum(raw["baseline"]["operations"][operation] for operation in operations),
        **{f"{backend}_ns": values.get(backend) for backend in BACKENDS},
        "idb_status": "measured" if scope == "common_18" else "not comparable: unsupported operations",
        "odb_over_dense_map": ratio(values["odb"], values["dense_map"]),
        "baseline_over_dense_map": ratio(values["baseline"], values["dense_map"]),
        "partition_over_dense_map": ratio(values["partition"], values["dense_map"]),
        "dense_map_change_vs_baseline_percent": percent_change(values["dense_map"], values["baseline"]),
        "dense_map_change_vs_partition_percent": percent_change(values["dense_map"], values["partition"]),
    }


def ms(value: int | None) -> str:
    return "--" if value is None else f"{value / 1_000_000:.3f}"


def speed(value: float | None) -> str:
    return "--" if value is None else f"{value:.3f}x"


def change(value: float | None) -> str:
    return "--" if value is None else f"{value:+.2f}%"


def write_csv(path: Path, rows: list[dict]) -> None:
    fields = ("operation", "calls", "event_share_percent", "calls_per_million_events", "baseline_ms", "partition_ms",
              "dense_map_ms", "odb_ms", "idb_ms", "idb_status", "idb_unsupported_calls", "odb_over_dense_map",
              "baseline_over_dense_map", "partition_over_dense_map", "dense_map_change_vs_baseline_percent",
              "dense_map_change_vs_partition_percent")
    with path.open("w", newline="", encoding="utf-8") as destination:
        writer = csv.DictWriter(destination, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow({
                "operation": row["operation"], "calls": row["calls"],
                "event_share_percent": f"{row['event_share_percent']:.8f}",
                "calls_per_million_events": f"{row['calls_per_million_events']:.4f}",
                **{f"{backend}_ms": "" if row[f"{backend}_ns"] is None else f"{row[f'{backend}_ns'] / 1e6:.6f}"
                   for backend in BACKENDS},
                "idb_status": row["idb_status"], "idb_unsupported_calls": row["idb_unsupported_calls"],
                "odb_over_dense_map": row["odb_over_dense_map"],
                "baseline_over_dense_map": row["baseline_over_dense_map"],
                "partition_over_dense_map": row["partition_over_dense_map"],
                "dense_map_change_vs_baseline_percent": row["dense_map_change_vs_baseline_percent"],
                "dense_map_change_vs_partition_percent": row["dense_map_change_vs_partition_percent"],
            })


def write_markdown(path: Path, summary: dict) -> None:
    lines = [
        "# InstanceTerm Dense Map Replay",
        "",
        "The dense-map candidate is one fresh replay. Baseline, partition, ODB, and iDB values are reused from one same-day reference round; the observations were not synchronized A/B trials and have no mean, standard deviation, or significance claim.",
        "",
        "Timed ms are cumulative per-event adapter execution plus returned JSON-field projection. Trace/LEF/DEF/guide parsing, token binding, canonicalization, and validation are outside the event timers. GNU time RSS is whole-process maximum RSS and includes trace reading, import, replay, and validation; no post-import or post-replay RSS was collected. This is trace replay, not a router or maze-routing rerun.",
        "",
        "Ratios use ODB/EccDB, baseline/dense-map, and partition/dense-map; a value above 1 means the dense-map candidate is faster. Positive candidate change percentages mean slower.",
        "",
        "## Totals",
        "",
        "| Scope | Groups | Calls | Baseline ms | Partition ms | Dense map ms | ODB ms | iDB ms | ODB/Dense | Baseline/Dense | Partition/Dense | Dense vs Baseline | Dense vs Partition |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for row in summary["aggregates"]:
        idb = ms(row["idb_ns"]) if row["idb_ns"] is not None else row["idb_status"]
        lines.append(
            f"| {row['scope']} | {row['operations']} | {row['calls']:,} | {ms(row['baseline_ns'])} | "
            f"{ms(row['partition_ns'])} | {ms(row['dense_map_ns'])} | {ms(row['odb_ns'])} | {idb} | "
            f"{speed(row['odb_over_dense_map'])} | {speed(row['baseline_over_dense_map'])} | "
            f"{speed(row['partition_over_dense_map'])} | {change(row['dense_map_change_vs_baseline_percent'])} | "
            f"{change(row['dense_map_change_vs_partition_percent'])} |"
        )
    lines.extend([
        "",
        "## API Groups",
        "",
        "| API group | Calls | Event share | Baseline ms | Partition ms | Dense map ms | ODB ms | iDB ms | ODB/Dense | Baseline/Dense | Partition/Dense |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ])
    for row in summary["rows"]:
        idb = ms(row["idb_ns"]) if row["idb_ns"] is not None else "unsupported"
        lines.append(
            f"| {row['operation']} | {row['calls']:,} | {row['event_share_percent']:.3f}% | "
            f"{ms(row['baseline_ns'])} | {ms(row['partition_ns'])} | {ms(row['dense_map_ns'])} | "
            f"{ms(row['odb_ns'])} | {idb} | {speed(row['odb_over_dense_map'])} | "
            f"{speed(row['baseline_over_dense_map'])} | {speed(row['partition_over_dense_map'])} |"
        )
    lines.extend([
        "",
        "## Whole-Process Resources",
        "",
        "| Run | Max RSS KiB | RSS Delta vs Baseline | RSS Delta vs Partition | Wall s | User s | System s |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: |",
    ])
    for name in ("baseline", "partition", "dense_map", "odb", "idb"):
        resource = summary["resources"][name]
        baseline_delta = resource["max_rss_kib"] - summary["resources"]["baseline"]["max_rss_kib"]
        partition_delta = resource["max_rss_kib"] - summary["resources"]["partition"]["max_rss_kib"]
        lines.append(f"| {LABELS[name]} | {resource['max_rss_kib']:,} | {baseline_delta:+,} | {partition_delta:+,} | "
                     f"{resource['wall_seconds']:.3f} | {resource['user_seconds']:.3f} | {resource['system_seconds']:.3f} |")
    lines.extend([
        "",
        "## Validation",
        "",
        "The fresh candidate has identical event and operation counts, validates all events, and matches the reference final net/wire/primitive/endpoint-extension populations. iDB rows remain explicitly unsupported where its replay metadata says so; its full-34 total is intentionally absent rather than zero.",
        "",
        f"Trace: `{summary['trace']['path']}`; SHA-256: `{summary['trace']['sha256']}`.",
        "",
    ])
    path.write_text("\n".join(lines), encoding="utf-8")


def plot(output_dir: Path, rows: list[dict], aggregates: list[dict]) -> None:
    os.environ.setdefault("MPLCONFIGDIR", "/tmp/matplotlib-instance-term-densemap")
    try:
        import matplotlib.pyplot as plt
    except ModuleNotFoundError as error:
        raise RuntimeError("matplotlib is required; use /home/zhuxu1/.venv/bin/python") from error

    by_operation = {row["operation"]: row for row in rows}
    groups = [by_operation[operation] for operation in HOTSPOTS]
    labels = list(HOTSPOTS)
    groups.extend(aggregates)
    total_labels = ["Full 34", "Common 18"]
    figure, axes = plt.subplots(1, 2, figsize=(13, 5.4), facecolor="white")
    figure.subplots_adjust(left=0.06, right=0.98, top=0.82, bottom=0.18, wspace=0.14)
    for axis, group_rows, group_labels, title in (
            (axes[0], groups[:3], labels, "Term-access groups"),
            (axes[1], groups[3:], total_labels, "Full and common aggregate groups")):
        width = 0.15
        positions = list(range(len(group_rows)))
        for index, backend in enumerate(BACKENDS):
            values = [float("nan") if row.get(f"{backend}_ns") is None else row[f"{backend}_ns"] / 1e6
                      for row in group_rows]
            axis.bar([position + (index - 2) * width for position in positions], values, width,
                     label=LABELS[backend], color=COLORS[backend])
        axis.set_xticks(positions, group_labels)
        for position, row in zip(positions, group_rows):
            elapsed = row["dense_map_ns"] / 1e6
            speedup = row["odb_ns"] / row["dense_map_ns"]
            axis.annotate(f"{speedup:.2f}× ODB", (position, elapsed),
                          xytext=(0, 6), textcoords="offset points", ha="center",
                          fontsize=8, color=COLORS["dense_map"])
        axis.set_ylabel("Aggregate API time (ms)")
        axis.set_title(title)
        axis.grid(axis="y", color="#d9d9d9", linewidth=0.7)
        axis.set_axisbelow(True)
    axes[1].legend(frameon=False, ncol=2, loc="upper right")
    figure.suptitle("InstanceTerm Dense Map: Candidate vs Same-Day References", fontsize=15, fontweight="bold")
    figure.text(0.5, 0.04,
                "One fresh dense-map run; references are reused single observations. API ms only, not RSS or router runtime.",
                ha="center", fontsize=9, color="#555555")
    for extension in ("png", "svg"):
        figure.savefig(output_dir / f"instance_term_densemap.{extension}", dpi=180,
                       facecolor="white", bbox_inches="tight")


def distribution(output_dir: Path, rows: list[dict]) -> None:
    os.environ.setdefault("MPLCONFIGDIR", "/tmp/matplotlib-instance-term-densemap")
    try:
        import matplotlib.pyplot as plt
    except ModuleNotFoundError as error:
        raise RuntimeError("matplotlib is required; use /home/zhuxu1/.venv/bin/python") from error

    def top_five(field: str) -> list[dict]:
        ordered = sorted(rows, key=lambda row: row[field], reverse=True)
        leaders, remainder = ordered[:5], ordered[5:]
        if remainder:
            leaders.append({"operation": f"Other {len(remainder)} groups",
                            field: sum(row[field] for row in remainder)})
        return leaders

    count_groups = top_five("calls")
    time_groups = top_five("dense_map_ns")
    count_labels = [f"{row['operation']}\n{row['calls']:,} calls" for row in count_groups]
    time_labels = [f"{row['operation']}\n{row['dense_map_ns'] / 1e6:.1f} ms" for row in time_groups]
    counts = [row["calls"] for row in count_groups]
    times = [row["dense_map_ns"] for row in time_groups]
    figure, axes = plt.subplots(1, 2, figsize=(14, 7.2), facecolor="white")
    figure.subplots_adjust(left=0.04, right=0.96, top=0.82, bottom=0.32, wspace=0.20)
    palette = ["#16836b", "#3f6fa8", "#c88828", "#9b4d4d", "#7066a8", "#a8a8a8"]
    for axis, values, labels, title in (
            (axes[0], counts, count_labels, "Event calls (frequency)"),
            (axes[1], times, time_labels, "Candidate elapsed timer share")):
        wedges, _, _ = axis.pie(values, colors=palette, autopct="%.1f%%", startangle=90,
                                textprops={"fontsize": 9})
        axis.set_title(title)
        axis.legend(wedges, [label.replace("\n", " — ") for label in labels],
                    loc="upper center", bbox_to_anchor=(0.5, -0.02), frameon=False,
                    fontsize=8, ncol=1)
    figure.suptitle("InstanceTerm Dense Map: Top Five API Groups Plus Other", fontsize=14, fontweight="bold")
    figure.text(0.5, 0.04,
                "Left: trace event calls. Right: cumulative candidate adapter time. These are different quantities.",
                ha="center", fontsize=9, color="#555555")
    for extension in ("png", "svg"):
        figure.savefig(output_dir / f"instance_term_densemap_distribution.{extension}", dpi=180,
                       facecolor="white", bbox_inches="tight")


def main() -> int:
    args = arguments()
    references_path = reference_path(args.references.resolve())
    candidate_path = args.candidate.resolve()
    candidate_time_path = args.candidate_time.resolve()
    for path in (references_path, candidate_path, candidate_time_path):
        if not path.is_file():
            raise RuntimeError(f"missing input: {path}")
    references = json.loads(references_path.read_text(encoding="utf-8"))
    candidate = json.loads(candidate_path.read_text(encoding="utf-8"))
    raw, full, common = validate(references, candidate)
    candidate_resources = time_stats(candidate_time_path)
    if sum(raw["idb"]["timing_ns"][operation] for operation in common) != raw["idb"]["total_ns"]:
        raise RuntimeError("reference iDB total is not restricted to common operations")
    rows = make_rows(raw, raw["baseline"]["operations"], raw["baseline"]["events"])
    resources = {"baseline": references["runs"]["baseline"]["resources"],
                 "partition": references["runs"]["candidate"]["resources"],
                 "dense_map": candidate_resources, "odb": references["runs"]["odb"]["resources"],
                 "idb": references["runs"]["idb"]["resources"]}
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    summary = {
        "schema": "instance_term_dense_map_report_v1",
        "observation": "one fresh dense-map replay; saved baseline, partition, ODB, and iDB references are one same-day earlier round",
        "comparison_caveat": "not synchronized A/B runs; no mean, standard deviation, or significance claim",
        "timer_scope": "per-event adapter execution and returned JSON-field projection; parsing, token binding, canonicalization, and validation excluded",
        "whole_process_resource_scope": "GNU time maximum RSS/wall/user/system include trace reading, import, replay, and validation",
        "phase_rss": {"post_import": "not_collected", "post_replay": "not_collected"},
        "architecture": {
            "instance_terms": "per-instance dense_map<string, Term> stores actual values; no separate Term entity",
            "compatibility": "ordered Term IDs remain for compatibility; one runtime ID-to-Term* vector resolves IDs",
            "persistence": "DesignRoot Term high-water remains persisted",
            "hash": "default dense_map hash",
            "caches": "owner/master caches and the new ID table remain; this is not a cache-free experiment",
        },
        "references": {"path": str(references_path), "sha256": sha256(references_path)},
        "candidate": {"path": str(candidate_path), "sha256": sha256(candidate_path),
                      "time_path": str(candidate_time_path), "time_sha256": sha256(candidate_time_path)},
        "trace": references["inputs"]["trace"],
        "events": raw["baseline"]["events"], "operations": raw["baseline"]["operations"],
        "full_34_operations": full, "common_18_operations": common,
        "validation": {"events_and_operation_counts_equal": True, "candidate_all_events_validated": True,
                       "candidate_final_populations_equal": references["validation"]["full_population_fields_equal"],
                       "idb_endpoint_extension_fields_cross_compared": False,
                       "idb_endpoint_extension_fields_reason": "iDB does not retain endpoint extensions"},
        "resources": resources,
        "candidate_rss_delta_kib": {"vs_baseline": candidate_resources["max_rss_kib"] - resources["baseline"]["max_rss_kib"],
                                    "vs_partition": candidate_resources["max_rss_kib"] - resources["partition"]["max_rss_kib"]},
        "aggregates": [total(raw, full, "full_34"), total(raw, common, "common_18")],
        "rows": rows,
    }
    (output_dir / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    write_csv(output_dir / "summary.csv", rows)
    write_markdown(output_dir / "summary.md", summary)
    plot(output_dir, rows, summary["aggregates"])
    distribution(output_dir, rows)
    print(f"wrote {output_dir / 'summary.json'}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, StopIteration, ValueError, json.JSONDecodeError) as error:
        print(f"InstanceTermDenseMapReport.py: {error}", file=sys.stderr)
        raise SystemExit(1)
