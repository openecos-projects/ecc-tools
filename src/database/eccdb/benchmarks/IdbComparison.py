#!/usr/bin/env python3
# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

"""Run strict, fresh-process IDB/EccDB benchmark comparisons.

``db-only`` compares separate native fixture-replay executables. ``wire``
compares the shared LEF/DEF target once per backend. Both modes deliberately
use fixed JSON schemas so a schema change fails rather than being guessed.
"""

from __future__ import annotations

import argparse
import filecmp
import hashlib
import json
from pathlib import Path
import shutil
import statistics
import subprocess
import sys
import tempfile
from typing import Any


DB_TIMINGS = (
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
DB_COUNTS = ("masters", "instances", "instance_pins", "nets", "connections")
WIRE_COUNTS = ("instances", "regular_nets", "special_nets", "instance_pins", "io_pins")
WIRE_TIMINGS = ("read_regular_wire_ns", "read_special_wire_ns")
GEOMETRY_FIELDS = ("wires", "paths", "points", "vias", "rectangles", "checksum_sum", "checksum_xor")
MEMORY_FIELDS = ("rss_after_load_kib", "peak_rss_kib")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("db-only", "wire"), required=True)
    parser.add_argument("--fixture", type=Path)
    parser.add_argument("--idb", type=Path)
    parser.add_argument("--eccdb", type=Path)
    parser.add_argument("--lef", type=Path)
    parser.add_argument("--def", dest="def_file", type=Path)
    parser.add_argument("--wire-benchmark", type=Path)
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--keep-raw", action="store_true")
    return parser.parse_args()


def require_file(path: Path | None, option: str) -> Path:
    if path is None or not path.is_file():
        raise RuntimeError(f"{option} is not a regular file: {path}")
    return path


def require_uint(value: object, field: str, backend: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value < 0:
        raise RuntimeError(f"{backend} returned invalid {field}")
    return value


def require_uint_fields(value: object, fields: tuple[str, ...], label: str, backend: str) -> dict[str, int]:
    if not isinstance(value, dict):
        raise RuntimeError(f"{backend} returned no {label}")
    return {field: require_uint(value.get(field), f"{label}.{field}", backend) for field in fields}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def snapshot_metadata(path: Path, backend: str) -> dict[str, int | str]:
    if not path.is_file():
        raise RuntimeError(f"{backend} did not write snapshot {path}")
    return {"bytes": path.stat().st_size, "sha256": sha256(path)}


def run(command: list[str], backend: str) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(command, capture_output=True, text=True, check=False)
    if completed.returncode != 0:
        raise RuntimeError(
            f"{backend} exited {completed.returncode}: {' '.join(command)}\n"
            f"stdout:\n{completed.stdout[-4000:]}\nstderr:\n{completed.stderr[-4000:]}"
        )
    return completed


def parse_json(text: str, backend: str) -> dict[str, Any]:
    try:
        value = json.loads(text)
    except json.JSONDecodeError as error:
        raise RuntimeError(f"{backend} did not emit one JSON document: {error}") from error
    if not isinstance(value, dict):
        raise RuntimeError(f"{backend} emitted a non-object JSON result")
    return value


def save_raw(directory: Path | None, name: str, json_text: str,
             completed: subprocess.CompletedProcess[str], snapshot: Path | None = None) -> None:
    if directory is None:
        return
    directory.mkdir(parents=True, exist_ok=True)
    (directory / f"{name}.json").write_text(json_text, encoding="utf-8")
    (directory / f"{name}.stdout.log").write_text(completed.stdout, encoding="utf-8")
    (directory / f"{name}.stderr.log").write_text(completed.stderr, encoding="utf-8")
    if snapshot is not None:
        shutil.copyfile(snapshot, directory / f"{name}.snapshot.jsonl")


def measure_db_only(executable: Path, fixture: Path, backend: str) -> tuple[dict[str, Any], str, subprocess.CompletedProcess[str]]:
    command = [str(executable), "--fixture", str(fixture), "--repeat", "1"]
    completed = run(command, backend)
    result = parse_json(completed.stdout, backend)
    if result.get("schema") != "db_only_benchmark_v1" or result.get("backend") != backend:
        raise RuntimeError(f"{backend} returned unexpected db-only schema/backend")
    counts = require_uint_fields(result.get("counts"), DB_COUNTS, "counts", backend)
    runs = result.get("runs")
    if not isinstance(runs, list) or len(runs) != 1:
        raise RuntimeError(f"{backend} did not return exactly one timed db-only run")
    timing = require_uint_fields(runs[0], DB_TIMINGS, "runs[0]", backend)
    checksums = result.get("checksums")
    if not isinstance(checksums, list) or len(checksums) != 1:
        raise RuntimeError(f"{backend} did not return exactly one native checksum")
    return ({"command": command, "counts": counts, "timing_ns": timing,
             "checksum": require_uint(checksums[0], "checksums[0]", backend)}, completed.stdout, completed)


def measure_wire(executable: Path, lef: Path, def_file: Path, backend: str, directory: Path,
                 snapshot: Path | None = None) -> tuple[dict[str, Any], str, subprocess.CompletedProcess[str]]:
    result_path = directory / f"{backend}.json"
    source = "entt" if backend == "eccdb" else "idb"
    command = [str(executable), "--lef", str(lef), "--def", str(def_file), "--source", source,
               "--output", str(result_path)]
    if snapshot is not None:
        command.extend(("--snapshot", str(snapshot)))
    completed = run(command, backend)
    if not result_path.is_file():
        raise RuntimeError(f"{backend} did not write {result_path}")
    json_text = result_path.read_text(encoding="utf-8")
    result = parse_json(json_text, backend)
    if result.get("schema") != "idb_wire_benchmark_v1" or result.get("backend") != backend:
        raise RuntimeError(f"{backend} returned unexpected wire schema/backend")
    counts = require_uint_fields(result.get("counts"), WIRE_COUNTS, "counts", backend)
    runs = result.get("runs")
    if not isinstance(runs, list) or len(runs) != 1:
        raise RuntimeError(f"{backend} did not return exactly one timed wire run")
    timing = require_uint_fields(runs[0], WIRE_TIMINGS, "runs[0]", backend)
    geometry_value = result.get("geometry")
    if not isinstance(geometry_value, dict):
        raise RuntimeError(f"{backend} returned no geometry")
    geometry = {
        kind: require_uint_fields(geometry_value.get(kind), GEOMETRY_FIELDS, f"geometry.{kind}", backend)
        for kind in ("regular", "special")
    }
    memory = require_uint_fields(result.get("memory"), MEMORY_FIELDS, "memory", backend)
    return ({"command": command, "counts": counts, "timing_ns": timing,
             "geometry": geometry, "memory_kib": memory}, json_text, completed)


def summarize(runs: list[dict[str, Any]], backend: str, timings: tuple[str, ...],
              include_wire_data: bool) -> dict[str, Any]:
    counts = runs[0]["counts"]
    if any(run["counts"] != counts for run in runs[1:]):
        raise RuntimeError(f"{backend} object counts changed between repeats")
    result: dict[str, Any] = {
        "counts": counts,
        "runs": runs,
        "median": {field: int(statistics.median(run["timing_ns"][field] for run in runs)) for field in timings},
    }
    if include_wire_data:
        geometry = runs[0]["geometry"]
        if any(run["geometry"] != geometry for run in runs[1:]):
            raise RuntimeError(f"{backend} geometry changed between repeats")
        result["geometry"] = geometry
        result["memory_median_kib"] = {
            field: int(statistics.median(run["memory_kib"][field] for run in runs)) for field in MEMORY_FIELDS
        }
    return result


def require_equal(label: str, idb: object, eccdb: object) -> None:
    if idb != eccdb:
        raise RuntimeError(f"IDB/EccDB {label} differ: {idb} vs {eccdb}")


def no_workload(geometry: dict[str, int]) -> bool:
    return all(geometry[field] == 0 for field in ("wires", "paths", "points", "vias", "rectangles"))


def wire_ratio(idb: dict[str, Any], eccdb: dict[str, Any], kind: str) -> dict[str, Any]:
    field = f"read_{kind}_wire_ns"
    empty = no_workload(idb["geometry"][kind])
    if empty:
        return {"status": "no_workload", "idb_ns": idb["median"][field], "eccdb_ns": eccdb["median"][field],
                "eccdb_over_idb": None}
    idb_time = idb["median"][field]
    return {"status": "measured", "idb_ns": idb_time, "eccdb_ns": eccdb["median"][field],
            "eccdb_over_idb": eccdb["median"][field] / idb_time if idb_time else None}


def run_db_only(args: argparse.Namespace, raw_directory: Path | None) -> dict[str, Any]:
    fixture = require_file(args.fixture, "--fixture")
    executables = {"idb": require_file(args.idb, "--idb"), "eccdb": require_file(args.eccdb, "--eccdb")}
    measurements: dict[str, list[dict[str, Any]]] = {"idb": [], "eccdb": []}
    order: list[str] = []
    for index in range(args.repeat):
        for backend in (("idb", "eccdb") if index % 2 == 0 else ("eccdb", "idb")):
            measurement, raw_json, completed = measure_db_only(executables[backend], fixture, backend)
            measurements[backend].append(measurement)
            save_raw(raw_directory, f"{index + 1:02d}-{backend}", raw_json, completed)
            order.append(backend)
    idb = summarize(measurements["idb"], "idb", DB_TIMINGS, False)
    eccdb = summarize(measurements["eccdb"], "eccdb", DB_TIMINGS, False)
    require_equal("object counts", idb["counts"], eccdb["counts"])
    return {
        "schema": "idb_eccdb_db_only_comparison_v1",
        "scope": "parser-independent logical-netlist API benchmark; native fixture validation is outside timed phases",
        "fixture": str(fixture.resolve()), "fixture_sha256": sha256(fixture), "repeat": args.repeat, "run_order": order,
        "executables": {backend: {"path": str(path.resolve()), "sha256": sha256(path)}
                        for backend, path in executables.items()},
        "idb": idb, "eccdb": eccdb,
        "eccdb_over_idb": {field: (eccdb["median"][field] / idb["median"][field]
                                      if idb["median"][field] else None) for field in DB_TIMINGS},
        "validation": {"counts_equal": True, "checksums_cross_backend_compared": False,
                       "checksum_reason": "native checksums can contain backend-specific handles"},
    }


def run_wire(args: argparse.Namespace, raw_directory: Path | None) -> dict[str, Any]:
    lef = require_file(args.lef, "--lef")
    def_file = require_file(args.def_file, "--def")
    executable = require_file(args.wire_benchmark, "--wire-benchmark")
    measurements: dict[str, list[dict[str, Any]]] = {"idb": [], "eccdb": []}
    order: list[str] = []
    warmup: dict[str, dict[str, Any]] = {}
    snapshot_result: dict[str, Any]
    with tempfile.TemporaryDirectory(prefix="eccdb-idb-wire-") as temporary:
        temporary_directory = Path(temporary)
        snapshots = {backend: temporary_directory / f"warmup-{backend}.snapshot.jsonl"
                     for backend in ("idb", "eccdb")}
        # Snapshots are intentionally emitted only once. They are an untimed
        # semantic gate, not a fourth measurement sample.
        for backend in ("idb", "eccdb"):
            measurement, raw_json, completed = measure_wire(
                executable, lef, def_file, backend, temporary_directory, snapshots[backend]
            )
            warmup[backend] = measurement
            save_raw(raw_directory, f"warmup-{backend}", raw_json, completed, snapshots[backend])
        require_equal("warmup object counts", warmup["idb"]["counts"], warmup["eccdb"]["counts"])
        require_equal("warmup regular geometry", warmup["idb"]["geometry"]["regular"],
                      warmup["eccdb"]["geometry"]["regular"])
        require_equal("warmup special geometry", warmup["idb"]["geometry"]["special"],
                      warmup["eccdb"]["geometry"]["special"])
        idb_snapshot = snapshot_metadata(snapshots["idb"], "idb")
        eccdb_snapshot = snapshot_metadata(snapshots["eccdb"], "eccdb")
        if not filecmp.cmp(snapshots["idb"], snapshots["eccdb"], shallow=False):
            raise RuntimeError(f"IDB/EccDB canonical snapshots differ: {idb_snapshot} vs {eccdb_snapshot}")
        snapshot_result = {"byte_equal": True, "idb": idb_snapshot, "eccdb": eccdb_snapshot}
        for index in range(args.repeat):
            for backend in (("idb", "eccdb") if index % 2 == 0 else ("eccdb", "idb")):
                measurement, raw_json, completed = measure_wire(executable, lef, def_file, backend, temporary_directory)
                measurements[backend].append(measurement)
                save_raw(raw_directory, f"{index + 1:02d}-{backend}", raw_json, completed)
                order.append(backend)
    idb = summarize(measurements["idb"], "idb", WIRE_TIMINGS, True)
    eccdb = summarize(measurements["eccdb"], "eccdb", WIRE_TIMINGS, True)
    require_equal("IDB warmup object counts", warmup["idb"]["counts"], idb["counts"])
    require_equal("EccDB warmup object counts", warmup["eccdb"]["counts"], eccdb["counts"])
    require_equal("IDB warmup regular geometry", warmup["idb"]["geometry"]["regular"], idb["geometry"]["regular"])
    require_equal("EccDB warmup regular geometry", warmup["eccdb"]["geometry"]["regular"], eccdb["geometry"]["regular"])
    require_equal("IDB warmup special geometry", warmup["idb"]["geometry"]["special"], idb["geometry"]["special"])
    require_equal("EccDB warmup special geometry", warmup["eccdb"]["geometry"]["special"], eccdb["geometry"]["special"])
    require_equal("object counts", idb["counts"], eccdb["counts"])
    require_equal("regular geometry", idb["geometry"]["regular"], eccdb["geometry"]["regular"])
    require_equal("special geometry", idb["geometry"]["special"], eccdb["geometry"]["special"])
    return {
        "schema": "idb_eccdb_wire_comparison_v1",
        "scope": "LEF/DEF import and shared-field validation are outside wire-read timers; exact canonical warmup comparison precedes measured repetitions",
        "inputs": {"lef": str(lef.resolve()), "def": str(def_file.resolve()),
                   "lef_sha256": sha256(lef), "def_sha256": sha256(def_file)},
        "executable": {"path": str(executable.resolve()), "sha256": sha256(executable)},
        "warmup": {"order": ["idb", "eccdb"], "timings_included": False, "snapshots": snapshot_result},
        "repeat": args.repeat, "run_order": order, "idb": idb, "eccdb": eccdb,
        "comparison": {"regular": wire_ratio(idb, eccdb, "regular"), "special": wire_ratio(idb, eccdb, "special")},
        "validation": {"counts_equal": True, "regular_geometry_equal": True, "special_geometry_equal": True,
                       "canonical_snapshots_byte_equal": True},
    }


def main() -> int:
    args = parse_arguments()
    if args.repeat <= 0 or args.repeat % 2 == 0:
        raise RuntimeError("--repeat must be a positive odd integer")
    raw_directory = args.output.parent / f"{args.output.stem}.runs" if args.keep_raw else None
    result = run_db_only(args, raw_directory) if args.mode == "db-only" else run_wire(args, raw_directory)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, json.JSONDecodeError) as error:
        print(f"IdbComparison.py: {error}", file=sys.stderr)
        raise SystemExit(1)
