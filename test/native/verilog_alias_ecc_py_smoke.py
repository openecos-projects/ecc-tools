#!/usr/bin/env python3
# SPDX-License-Identifier: MulanPSL-2.0
"""Regression for native EccDB Verilog alias-aware iLVS comparison.

Run after building the Python extension.  The native and legacy imports are
deliberately made in separate worker processes because both paths own native
singletons:

  PYTHONPATH=bin:build/merge-main-20261029/pythonpath \\
    python3 test/native/verilog_alias_ecc_py_smoke.py \\
      --output-root build/verilog-alias-ecc-py-smoke

``--output-root`` must be new.  The test only uses the small fixtures beside
this script; it neither builds the project nor runs a full design flow.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import os
from pathlib import Path
import resource
import subprocess
import sys
import time
import traceback
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
FIXTURE_ROOT = ROOT / "test/native/data/verilog_alias"
TECH_LEF = FIXTURE_ROOT / "tech.lef"
CELL_LEF = FIXTURE_ROOT / "cells.lef"
BACKENDS = ("legacy", "eccdb")


@dataclass(frozen=True)
class Case:
    name: str
    physical_def: str
    reference_verilog: str


CASES = (
    Case("same_def_equivalent", "alias.def", "reference-equivalent.v"),
    Case("reference_endpoint_mismatch", "alias.def", "reference-negative.v"),
    Case("physical_out_alias", "physical-out.def", "reference-equivalent.v"),
    Case("physical_split_alias", "physical-split.def", "reference-equivalent.v"),
    Case("alias_wrong_endpoint", "physical-alias-wrong-endpoint.def", "reference-equivalent.v"),
)
CASES_BY_NAME = {case.name: case for case in CASES}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def require_success(value: object, label: str) -> None:
    if value is False:
        raise RuntimeError(f"{label} returned False")


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def read_json(path: Path) -> dict[str, Any] | None:
    if not path.is_file():
        return None
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    return value if isinstance(value, dict) else None


def file_record(path: Path) -> dict[str, object]:
    resolved = path.resolve()
    return {
        "path": str(resolved),
        "exists": resolved.is_file(),
        "bytes": resolved.stat().st_size if resolved.is_file() else 0,
    }


def fixture_path(name: str) -> Path:
    path = FIXTURE_ROOT / name
    require(path.is_file(), f"missing fixture: {path}")
    return path


def validate_fixtures() -> None:
    fixture_path("tech.lef")
    fixture_path("cells.lef")
    for case in CASES:
        fixture_path(case.physical_def)
        fixture_path(case.reference_verilog)


def net_difference(report: dict[str, Any]) -> int:
    rows = [row for row in report.get("entity", []) if isinstance(row, dict) and row.get("entity") == "Net"]
    require(len(rows) == 1, f"iLVS report has no unique Net entity row: {report.get('entity')}")
    try:
        return int(rows[0]["difference"])
    except (KeyError, TypeError, ValueError) as error:
        raise AssertionError(f"invalid Net entity row: {rows[0]}") from error


def routing_row(report: dict[str, Any]) -> dict[str, Any]:
    rows = [row for row in report.get("connectivity", []) if isinstance(row, dict) and row.get("connectivity") == "Routing"]
    require(len(rows) == 1, f"iLVS report has no unique Routing row: {report.get('connectivity')}")
    return rows[0]


def routing_count(row: dict[str, Any], name: str) -> int:
    if name == "total":
        value = row.get(name)
    else:
        nested = row.get(name)
        value = nested.get("count") if isinstance(nested, dict) else None
    try:
        return int(value)
    except (TypeError, ValueError) as error:
        raise AssertionError(f"invalid Routing {name} count: {row}") from error


def net_violations(report: dict[str, Any]) -> list[dict[str, object]]:
    values: list[dict[str, object]] = []
    for row in report.get("violations", []):
        if not isinstance(row, dict) or row.get("type") != "Net":
            continue
        terminals = row.get("terminals", [])
        require(isinstance(terminals, list), f"Net violation has non-list terminals: {row}")
        values.append({
            "net": str(row.get("net", "")),
            "terminals": sorted(str(terminal) for terminal in terminals),
        })
    return values


def violation_terminals(report: dict[str, Any], name: str) -> set[str]:
    matches = [row for row in net_violations(report) if row["net"] == name]
    require(len(matches) == 1, f"expected one Net violation for {name}, got {matches}")
    terminals = matches[0]["terminals"]
    require(isinstance(terminals, list), f"invalid normalized terminals: {matches[0]}")
    return set(str(terminal) for terminal in terminals)


def summarize_report(report: dict[str, Any]) -> dict[str, object]:
    routing = routing_row(report)
    return {
        "entity": report.get("entity", []),
        "net_difference": net_difference(report),
        "routing": {
            "total": routing_count(routing, "total"),
            "connected": routing_count(routing, "connected"),
            "open": routing_count(routing, "open"),
            "short": routing_count(routing, "short"),
        },
        "net_violations": net_violations(report),
    }


def require_clean_small_routing(report: dict[str, Any], context: str) -> None:
    routing = routing_row(report)
    require(routing_count(routing, "total") == 3, f"{context}: expected three routed nets: {routing}")
    require(routing_count(routing, "connected") == 3, f"{context}: expected three connected nets: {routing}")
    require(routing_count(routing, "open") == 0, f"{context}: unexpected route open: {routing}")
    require(routing_count(routing, "short") == 0, f"{context}: unexpected route short: {routing}")


def validate_report(case: Case, backend: str, report: dict[str, Any]) -> list[str]:
    context = f"{backend}/{case.name}"
    checks: list[str] = []
    if case.name == "same_def_equivalent":
        require(net_difference(report) == 0, f"{context}: equivalent alias input has Net difference")
        require(not net_violations(report), f"{context}: equivalent alias input reported Net violations")
        require_clean_small_routing(report, context)
        checks.extend(("zero Net entity difference", "3/3 clean Routing"))
    elif case.name == "reference_endpoint_mismatch":
        expected = {
            "BAD": {"NETLIST/U0/A"},
            "IN": {"DEF/U0/A"},
        }
        actual = {str(row["net"]): set(str(term) for term in row["terminals"]) for row in net_violations(report)}
        require(net_difference(report) == 2, f"{context}: endpoint mismatch must retain two Net differences")
        require(actual == expected, f"{context}: endpoint mismatch did not report the real U0/A terminals: {actual}")
        require_clean_small_routing(report, context)
        checks.extend(("two real endpoint Net differences", "BAD/IN U0/A terminals retained", "3/3 clean Routing"))
    elif case.name == "physical_out_alias":
        if backend == "eccdb":
            require(net_difference(report) == 0, f"{context}: EccDB alias fallback did not match OUT to net16")
            require(not net_violations(report), f"{context}: EccDB alias fallback left Net violations")
            checks.append("EccDB alias fallback accepted OUT physical net")
        else:
            expected_terminals = {"PIN/OUT", "U0/Y"}
            actual = {str(row["net"]): set(str(term) for term in row["terminals"]) for row in net_violations(report)}
            expected = {"NETLIST/net16": expected_terminals, "DEF/OUT": expected_terminals}
            require(net_difference(report) == 2, f"{context}: legacy path must retain its unsaved alias-name pair")
            require(actual == expected, f"{context}: legacy result was not the expected name-only alias pair: {actual}")
            checks.append("legacy retains its two unsaved alias-name differences")
    elif case.name == "physical_split_alias":
        require(net_difference(report) > 0, f"{context}: split physical net was incorrectly consumed as an alias")
        require("NETLIST/PIN/OUT" in violation_terminals(report, "net16"),
                f"{context}: split net16 must retain its missing output endpoint")
        require(violation_terminals(report, "DEF/OUT") == {"PIN/OUT"},
                f"{context}: split OUT physical net must be reported independently")
        checks.extend(("split alias remains an Entity failure", "split endpoints are present in report"))
    elif case.name == "alias_wrong_endpoint":
        logical = violation_terminals(report, "NETLIST/net16")
        physical = violation_terminals(report, "DEF/OUT")
        require(net_difference(report) > 0, f"{context}: wrong-endpoint alias candidate was accepted")
        require(logical == {"PIN/OUT", "U0/Y"}, f"{context}: unexpected logical alias terminals: {logical}")
        require(physical == {"PIN/BAD", "PIN/OUT", "U0/Y"}, f"{context}: wrong physical alias terminals were hidden: {physical}")
        require(logical != physical, f"{context}: endpoint mismatch was not observable")
        checks.extend(("wrong-endpoint alias remains an Entity failure", "logical and physical endpoint sets differ"))
    else:
        raise AssertionError(f"unhandled case: {case.name}")
    return checks


def read_report(lvs_root: Path) -> tuple[dict[str, Any], dict[str, object]]:
    report_path = lvs_root / "lvs_reporter" / "ilvs.json"
    text_path = lvs_root / "lvs_reporter" / "ilvs.rpt"
    require(report_path.is_file(), f"iLVS did not write {report_path}")
    require(text_path.is_file() and text_path.stat().st_size > 0, f"iLVS did not write {text_path}")
    try:
        report = json.loads(report_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise AssertionError(f"invalid iLVS JSON report: {report_path}") from error
    require(isinstance(report, dict), f"iLVS JSON report is not an object: {report_path}")
    return report, {"json": file_record(report_path), "text": file_record(text_path)}


def run_legacy(def_file: Path, reference: Path, work: Path, threads: int) -> tuple[dict[str, float], str]:
    from ecc_tools_bin import ecc_py

    config_path = work / "legacy-platform-config.json"
    write_json(config_path, {"LayerSettings": {"routing_layer_1st": "met1"}})
    timings: dict[str, float] = {}
    import_started = time.monotonic()
    require_success(ecc_py.db_init(config_path=str(config_path), output_path=str(work / "legacy-idb")), "db_init")
    require_success(ecc_py.tech_lef_init(str(TECH_LEF.resolve())), "tech_lef_init")
    require_success(ecc_py.lef_init([str(CELL_LEF.resolve())]), "lef_init")
    require_success(ecc_py.def_init(str(def_file.resolve())), "def_init")
    require_success(ecc_py.lvs_verilog_init(str(reference.resolve()), "top"), "lvs_verilog_init")
    timings["import_ms"] = (time.monotonic() - import_started) * 1000.0
    lvs_root = work / "lvs"
    lvs_started = time.monotonic()
    initialized = False
    try:
        require_success(ecc_py.init_lvs(str(lvs_root), threads), "init_lvs")
        initialized = True
        require_success(ecc_py.run_lvs(), "run_lvs")
    finally:
        if initialized:
            require_success(ecc_py.destroy_lvs(), "destroy_lvs")
    timings["lvs_ms"] = (time.monotonic() - lvs_started) * 1000.0
    return timings, str(Path(ecc_py.__file__).resolve())


def run_eccdb(def_file: Path, reference: Path, work: Path, threads: int) -> tuple[dict[str, float], str]:
    import ecc_py

    require(hasattr(ecc_py, "eccdb"), "ecc_py was built without native EccDB support")
    require("lvs" in ecc_py.eccdb.supported_tools(), "native EccDB scripting does not expose iLVS")
    timings: dict[str, float] = {}
    import_started = time.monotonic()
    database = ecc_py.eccdb.open_lef_def(
        [str(TECH_LEF.resolve()), str(CELL_LEF.resolve())], str(def_file.resolve())
    )
    timings["import_ms"] = (time.monotonic() - import_started) * 1000.0
    lvs_started = time.monotonic()
    ecc_py.eccdb.run_lvs(database, options={
        "reference_verilog": str(reference.resolve()),
        "top_module": "top",
        "temp_directory_path": str(work / "lvs"),
        "thread_number": threads,
    })
    timings["lvs_ms"] = (time.monotonic() - lvs_started) * 1000.0
    return timings, str(Path(ecc_py.__file__).resolve())


def run_worker(args: argparse.Namespace) -> int:
    require(args.backend in BACKENDS, f"unknown backend: {args.backend}")
    case = CASES_BY_NAME.get(args.case)
    require(case is not None, f"unknown case: {args.case}")
    require(args.work is not None, "worker requires --work")
    work = args.work.resolve()
    require(work.is_dir(), f"worker directory was not prepared: {work}")
    result_path = work / "worker-result.json"
    require(not result_path.exists(), f"refusing to overwrite worker result: {result_path}")
    validate_fixtures()
    def_file = fixture_path(case.physical_def)
    reference = fixture_path(case.reference_verilog)
    started = time.monotonic()
    result: dict[str, Any] = {
        "schema": 1,
        "backend": args.backend,
        "case": case.name,
        "status": "running",
        "threads": args.threads,
        "inputs": {
            "tech_lef": file_record(TECH_LEF),
            "cell_lef": file_record(CELL_LEF),
            "physical_def": file_record(def_file),
            "reference_verilog": file_record(reference),
        },
    }
    try:
        if args.backend == "legacy":
            timings, module_path = run_legacy(def_file, reference, work, args.threads)
        else:
            timings, module_path = run_eccdb(def_file, reference, work, args.threads)
        report, report_files = read_report(work / "lvs")
        result["ecc_py_module"] = module_path
        result["stages_ms"] = timings
        result["reports"] = report_files
        result["report"] = summarize_report(report)
        result["checks"] = validate_report(case, args.backend, report)
        result["status"] = "passed"
    except BaseException as error:
        result["status"] = "failed"
        result["error_type"] = type(error).__name__
        result["error"] = str(error)
        result["traceback"] = traceback.format_exc()
    result["elapsed_ms"] = (time.monotonic() - started) * 1000.0
    result["peak_rss_kib"] = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    write_json(result_path, result)
    print(json.dumps(result, indent=2, sort_keys=True), flush=True)
    return 0 if result["status"] == "passed" else 2


def worker_summary(result: dict[str, Any] | None) -> dict[str, object]:
    if result is None:
        return {"status": "missing", "error": "worker did not write worker-result.json"}
    keys = ("status", "elapsed_ms", "peak_rss_kib", "stages_ms", "report", "checks", "error_type", "error")
    return {key: result[key] for key in keys if key in result}


def run_subprocess(case: Case, backend: str, output_root: Path, threads: int) -> dict[str, object]:
    work = output_root / case.name / backend
    work.mkdir(parents=True, exist_ok=False)
    command = [
        sys.executable,
        str(Path(__file__).resolve()),
        "--worker",
        "--backend", backend,
        "--case", case.name,
        "--work", str(work),
        "--threads", str(threads),
    ]
    environment = os.environ.copy()
    environment.setdefault("ECC_LOGGER_THROW_ON_ERROR", "1")
    stdout_path = work / "stdout.log"
    stderr_path = work / "stderr.log"
    started = time.monotonic()
    spawn_error: str | None = None
    try:
        with stdout_path.open("w", encoding="utf-8") as stdout, stderr_path.open("w", encoding="utf-8") as stderr:
            completed = subprocess.run(command, cwd=str(ROOT), env=environment, stdout=stdout, stderr=stderr, check=False)
        returncode: int | None = completed.returncode
    except OSError as error:
        returncode = None
        spawn_error = repr(error)
    result = read_json(work / "worker-result.json")
    status = "passed" if returncode == 0 and result is not None and result.get("status") == "passed" else "failed"
    invocation: dict[str, object] = {
        "backend": backend,
        "command": command,
        "status": status,
        "returncode": returncode,
        "wall_ms": (time.monotonic() - started) * 1000.0,
        "stdout": file_record(stdout_path),
        "stderr": file_record(stderr_path),
        "worker": worker_summary(result),
    }
    if spawn_error is not None:
        invocation["spawn_error"] = spawn_error
    return invocation


def run_master(args: argparse.Namespace) -> int:
    require(args.output_root is not None, "--output-root is required")
    output_root = args.output_root.resolve()
    require(not output_root.exists(), f"output root already exists: {output_root}")
    validate_fixtures()
    output_root.mkdir(parents=True)
    summary: dict[str, Any] = {
        "schema": 1,
        "status": "running",
        "fixture_root": str(FIXTURE_ROOT.resolve()),
        "threads": args.threads,
        "cases": [],
    }
    all_passed = True
    for case in CASES:
        case_summary: dict[str, object] = {
            "case": case.name,
            "physical_def": file_record(fixture_path(case.physical_def)),
            "reference_verilog": file_record(fixture_path(case.reference_verilog)),
            "backends": {},
        }
        backends = case_summary["backends"]
        require(isinstance(backends, dict), f"invalid summary backends for {case.name}")
        for backend in BACKENDS:
            invocation = run_subprocess(case, backend, output_root, args.threads)
            backends[backend] = invocation
            all_passed = all_passed and invocation["status"] == "passed"
        summary["cases"].append(case_summary)
    summary["status"] = "passed" if all_passed else "failed"
    write_json(output_root / "summary.json", summary)
    print(f"Verilog alias ecc_py smoke {summary['status']}: {output_root / 'summary.json'}", flush=True)
    return 0 if all_passed else 2


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-root", type=Path, help="New root for all reports and worker logs")
    parser.add_argument("--threads", type=int, default=1)
    parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--backend", choices=BACKENDS, help=argparse.SUPPRESS)
    parser.add_argument("--case", choices=tuple(CASES_BY_NAME), help=argparse.SUPPRESS)
    parser.add_argument("--work", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.threads <= 0:
        parser.error("--threads must be positive")
    if args.worker:
        if args.backend is None or args.case is None or args.work is None:
            parser.error("worker requires --backend, --case, and --work")
    elif args.output_root is None:
        parser.error("--output-root is required")
    return args


def main() -> int:
    args = parse_args()
    return run_worker(args) if args.worker else run_master(args)


if __name__ == "__main__":
    raise SystemExit(main())
