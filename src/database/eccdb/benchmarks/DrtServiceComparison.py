#!/usr/bin/env python3
# ***************************************************************************************
# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2023-2025 Peng Cheng Laboratory
# Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
# Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
#
# iEDA is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
# http://license.coscl.org.cn/MulanPSL2
#
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
#
# See the Mulan PSL v2 for more details.
# ***************************************************************************************

"""Compare native ODB and EccDB APIs used by the TritonRoute adapters."""

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
    "scan_regular_nets_ns",
    "find_instances_ns",
    "find_nets_ns",
    "find_pins_ns",
    "scan_net_pins_ns",
    "scan_guides_ns",
    "decode_routes_ns",
    "replace_guides_ns",
)
PIN_TIMING_FIELDS = ("find_pins_ns", "scan_net_pins_ns")
EXPERIMENT_FIELDS = ("find_pins_shadow_ns", "scan_net_pins_shadow_inline_ns")
ECCDB_EXPERIMENT_TIMING_FIELDS = (
    "find_pins_shadow_ns",
    "scan_net_pins_direct_registry_ns",
    "scan_net_pins_cached_ns",
    "scan_net_pins_shadow_registry_ns",
    "scan_net_pins_shadow_shared_ns",
    "scan_net_pins_shadow_inline_ns",
)
COUNT_FIELDS = (
    "instances",
    "regular_nets",
    "instance_pins",
    "connected_iterms",
    "io_pins",
    "guides",
    "routed_nets",
    "route_shapes",
    "route_vias",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("fixture", "lef", "routed-def", "preroute-odb", "routed-odb", "eccdb", "odb", "output"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--include-experiments", action="store_true")
    parser.add_argument("--pins-only", action="store_true",
                        help="measure only find_pins_ns and scan_net_pins_ns")
    return parser.parse_args()


def run(command: list[str]) -> None:
    completed = subprocess.run(command, capture_output=True, text=True, check=False)
    if completed.returncode != 0:
        raise RuntimeError(
            f"command failed ({completed.returncode}): {' '.join(command)}\n"
            f"stdout: {completed.stdout[-4000:]}\nstderr: {completed.stderr[-4000:]}"
        )


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def measure(executable: Path, arguments: list[str], backend: str, output_dir: Path,
            expected_fields: tuple[str, ...]) -> dict[str, Any]:
    with tempfile.TemporaryDirectory(prefix=f"drt-{backend}-", dir=output_dir) as temporary_directory:
        result_path = Path(temporary_directory) / "result.json"
        run([str(executable), *arguments, "--repeat", "1", "--output", str(result_path)])
        result = json.loads(result_path.read_text(encoding="utf-8"))
    if result.get("schema") != "drt_service_benchmark_v1" or result.get("backend") != backend:
        raise RuntimeError(f"{backend} returned an unexpected benchmark schema")
    if len(result.get("runs", [])) != 1:
        raise RuntimeError(f"{backend} did not return one timed run")
    measured_fields = result.get("measured_fields")
    if (not isinstance(measured_fields, list)
            or any(not isinstance(field, str) for field in measured_fields)
            or len(measured_fields) != len(set(measured_fields))
            or set(measured_fields) != set(expected_fields)):
        raise RuntimeError(
            f"{backend} measured fields differ from the requested mode: "
            f"expected {expected_fields}, got {measured_fields}"
        )
    run_result = result["runs"][0]
    for field in expected_fields:
        if not isinstance(run_result.get(field), int) or run_result[field] < 0:
            raise RuntimeError(f"{backend} returned an invalid {field}")
    counts = result.get("counts")
    if not isinstance(counts, dict):
        raise RuntimeError(f"{backend} returned no object counts")
    for field in COUNT_FIELDS:
        if not isinstance(counts.get(field), int) or counts[field] < 0:
            raise RuntimeError(f"{backend} returned an invalid {field} count")
    return result


def summarize(results: list[dict[str, Any]], fields: tuple[str, ...]) -> dict[str, Any]:
    counts = results[0]["counts"]
    if any(result["counts"] != counts for result in results[1:]):
        raise RuntimeError("object counts changed across repeated runs")
    runs = [result["runs"][0] for result in results]
    return {
        "counts": counts,
        "runs": runs,
        "measured_fields": list(fields),
        "median": {field: int(statistics.median(run[field] for run in runs)) for field in fields},
    }


def main() -> int:
    args = parse_args()
    if args.pins_only and args.include_experiments:
        raise RuntimeError("--pins-only cannot be combined with --include-experiments")
    if args.repeat <= 0 or args.repeat % 2 == 0:
        raise RuntimeError("--repeat must be a positive odd integer")
    for path in (args.fixture, args.lef, args.routed_def, args.preroute_odb, args.routed_odb, args.eccdb, args.odb):
        if not path.is_file():
            raise RuntimeError(f"missing input: {path}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    guides = args.output.with_suffix(".guides.jsonl")
    run([str(args.odb), "--preroute-odb", str(args.preroute_odb), "--export-guides", str(guides)])
    if not guides.is_file() or guides.stat().st_size == 0:
        raise RuntimeError("ODB did not export a nonempty guide fixture")

    commands = {
        "odb": ["--preroute-odb", str(args.preroute_odb), "--routed-odb", str(args.routed_odb),
                "--fixture", str(args.fixture)],
        "eccdb": ["--lef", str(args.lef), "--routed-def", str(args.routed_def),
                  "--fixture", str(args.fixture), "--guides", str(guides)],
    }
    if args.pins_only:
        commands["odb"].append("--pins-only")
        commands["eccdb"].append("--pins-only")
    if not args.include_experiments:
        commands["eccdb"].append("--no-experiments")
    comparison_fields = PIN_TIMING_FIELDS if args.pins_only else TIMING_FIELDS
    eccdb_extra_fields = EXPERIMENT_FIELDS if args.include_experiments else ()
    expected_fields = {
        "odb": comparison_fields,
        "eccdb": (*comparison_fields,
                  *(ECCDB_EXPERIMENT_TIMING_FIELDS if args.include_experiments else ())),
    }
    executables = {"odb": args.odb, "eccdb": args.eccdb}
    for backend in ("odb", "eccdb"):
        measure(executables[backend], commands[backend], backend, args.output.parent, expected_fields[backend])

    measurements: dict[str, list[dict[str, Any]]] = {"odb": [], "eccdb": []}
    for index in range(args.repeat):
        order = ("odb", "eccdb") if index % 2 == 0 else ("eccdb", "odb")
        for backend in order:
            measurements[backend].append(
                measure(executables[backend], commands[backend], backend, args.output.parent, expected_fields[backend])
            )
    odb = summarize(measurements["odb"], comparison_fields)
    eccdb = summarize(measurements["eccdb"], (*comparison_fields, *eccdb_extra_fields))
    mismatches = {
        field: {"odb": odb["counts"][field], "eccdb": eccdb["counts"][field]}
        for field in COUNT_FIELDS
        if odb["counts"][field] != eccdb["counts"][field]
    }
    if mismatches:
        raise RuntimeError(f"backend object counts differ: {mismatches}")

    result = {
        "schema": "drt_service_comparison_v1",
        "scope": "TritonRoute-facing native API operations on the same ISPD18 test6 design; imports excluded",
        "warning": "Current TritonRoute EccDB integration runs after ODB parsing; these are API timings, not alternative full-flow runtimes",
        "repeat": args.repeat,
        "warmup_pairs": 1,
        "measured_fields": list(comparison_fields),
        "inputs": {name: str(getattr(args, name).resolve()) for name in
                   ("fixture", "lef", "routed_def", "preroute_odb", "routed_odb")},
        "guides": {"path": str(guides.resolve()), "sha256": file_sha256(guides)},
        "odb": odb,
        "eccdb": eccdb,
        "eccdb_over_odb": {
            field: (eccdb["median"][field] / odb["median"][field] if odb["median"][field] else None)
            for field in comparison_fields
        },
    }
    if args.include_experiments:
        result["eccdb_shadow_over_odb"] = {
            "find_pins": eccdb["median"]["find_pins_shadow_ns"] / odb["median"]["find_pins_ns"],
            "scan_net_pins": eccdb["median"]["scan_net_pins_shadow_inline_ns"] / odb["median"]["scan_net_pins_ns"],
        }
        result["experiment_scope"] = "benchmark-only pointer caches; not EccDB production Service API timings"
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, json.JSONDecodeError) as error:
        print(f"DrtServiceComparison.py: {error}", file=sys.stderr)
        raise SystemExit(1)
