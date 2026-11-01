#!/usr/bin/env python3
# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

"""Compare native OpenDB and EccDB logical-netlist operations without parsers."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile
from typing import Any


TIMING_FIELDS = (
    "create_instances_ns",
    "create_nets_ns",
    "connect_ns",
    "find_instances_ns",
    "find_nets_ns",
    "find_pins_ns",
    "traverse_instances_ns",
    "traverse_nets_ns",
    "traverse_net_pins_ns",
)
TOTAL_FIELDS = {
    "build_total_ns": TIMING_FIELDS[:3],
    "find_total_ns": TIMING_FIELDS[3:6],
    "traverse_total_ns": TIMING_FIELDS[6:],
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--eccdb", type=Path, required=True)
    parser.add_argument("--odb", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repeat", type=int, default=5)
    parser.add_argument("--peak-rss", action="store_true", help="record fresh-process peak RSS using /usr/bin/time")
    return parser.parse_args()


def measure(executable: Path, fixture: Path, backend: str, peak_rss: bool = False) -> dict[str, Any]:
    command = [str(executable), "--fixture", str(fixture), "--repeat", "1"]
    with tempfile.TemporaryDirectory(prefix="eccdb-bench-rss-") as temporary:
        rss_path = Path(temporary) / "peak_rss_kib"
        if peak_rss:
            command = ["/usr/bin/time", "-f", "%M", "-o", str(rss_path), *command]
        completed = subprocess.run(command, capture_output=True, text=True, check=False)
        rss_kib = int(rss_path.read_text().strip()) if peak_rss and completed.returncode == 0 else None
    if completed.returncode != 0:
        raise RuntimeError(
            f"{backend} exited {completed.returncode}: "
            f"{completed.stderr.strip()} {completed.stdout.strip()}"
        )
    result = json.loads(completed.stdout)
    if result.get("schema") != "db_only_benchmark_v1" or result.get("backend") != backend:
        raise RuntimeError(f"{backend} returned an unexpected schema/backend")
    if len(result.get("runs", [])) != 1:
        raise RuntimeError(f"{backend} did not return exactly one timed run")
    run = result["runs"][0]
    if any(not isinstance(run.get(field), int) or run[field] < 0 for field in TIMING_FIELDS):
        raise RuntimeError(f"{backend} returned an invalid timing field")
    for total, parts in TOTAL_FIELDS.items():
        run[total] = sum(run[field] for field in parts)
    if rss_kib is not None:
        run["peak_rss_kib"] = rss_kib
    return result


def summarize(results: list[dict[str, Any]]) -> dict[str, Any]:
    counts = results[0]["counts"]
    if any(result["counts"] != counts for result in results):
        raise RuntimeError("object counts changed between repeats")
    runs = [result["runs"][0] for result in results]
    fields = (*TIMING_FIELDS, *TOTAL_FIELDS)
    if "peak_rss_kib" in runs[0]:
        fields = (*fields, "peak_rss_kib")
    return {
        "counts": counts,
        "runs": runs,
        "median": {field: int(statistics.median(run[field] for run in runs)) for field in fields},
    }


def main() -> int:
    args = parse_args()
    if args.repeat <= 0 or args.repeat % 2 == 0:
        raise RuntimeError("--repeat must be a positive odd integer")
    for path in (args.fixture, args.eccdb, args.odb):
        if not path.is_file():
            raise RuntimeError(f"missing input: {path}")

    # Each process parses the neutral JSON fixture before its native timer.
    measure(args.odb, args.fixture, "odb", args.peak_rss)
    measure(args.eccdb, args.fixture, "eccdb", args.peak_rss)
    measured: dict[str, list[dict[str, Any]]] = {"odb": [], "eccdb": []}
    for index in range(args.repeat):
        order = ("odb", "eccdb") if index % 2 == 0 else ("eccdb", "odb")
        for backend in order:
            executable = args.odb if backend == "odb" else args.eccdb
            measured[backend].append(measure(executable, args.fixture, backend, args.peak_rss))

    odb = summarize(measured["odb"])
    eccdb = summarize(measured["eccdb"])
    if odb["counts"] != eccdb["counts"]:
        raise RuntimeError(f"backend object counts differ: {odb['counts']} vs {eccdb['counts']}")

    ratios = {
        field: (eccdb["median"][field] / odb["median"][field]
                if odb["median"][field] else None)
        for field in (*TIMING_FIELDS, *TOTAL_FIELDS)
    }
    result = {
        "schema": "db_only_comparison_v1",
        "scope": "logical instances/nets/instance-pin connections; no LEF/DEF parse, BTerms, placement, or wire geometry",
        "fixture": str(args.fixture.resolve()),
        "fixture_sha256": hashlib.sha256(args.fixture.read_bytes()).hexdigest(),
        "repeat": args.repeat,
        "warmup_pairs": 1,
        "run_order": "alternating native ODB/EccDB and EccDB/ODB in fresh processes",
        "odb": odb,
        "eccdb": eccdb,
        "eccdb_over_odb": ratios,
        "memory_scope": "fresh-process peak RSS, including fixture/library and validation temporaries" if args.peak_rss else None,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, json.JSONDecodeError) as error:
        print(f"DbOnlyComparison.py: {error}", file=sys.stderr)
        raise SystemExit(1)
