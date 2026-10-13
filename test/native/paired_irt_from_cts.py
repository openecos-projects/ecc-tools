#!/usr/bin/env python3
# SPDX-License-Identifier: MulanPSL-2.0
"""Reimport each provider's CTS DEF and compare fresh iRT runs.

This deliberately differs from paired_gcd_chain: no CTS is run and no binary
checkpoint is loaded. The two CTS DEF inputs may have known semantic differences;
those are reported before routing instead of being silently normalized away.
"""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import resource
import sys
import time
import traceback

import paired_gcd_chain as pair


def worker(args: argparse.Namespace) -> int:
    spec = pair._load_manifest(args.manifest)
    root = args.output_root.resolve() / args.backend
    root.mkdir(parents=True, exist_ok=False)
    imported = root / "import"
    imported.mkdir()
    source_def = (args.idb_def if args.backend == "idb" else args.eccdb_def).resolve()
    os.environ["ECC_LOGGER_THROW_ON_ERROR"] = "1"
    os.environ["OMP_NUM_THREADS"] = str(args.threads)
    started = time.perf_counter()
    result = {
        "backend": args.backend, "status": "running", "active_stage": "import",
        "requested_stages": ["import", "routing"], "completed_stages": [],
        "input_files": {
            "def_file": pair._file_metadata(source_def),
            "lef_files": [pair._file_metadata(p) for p in spec["lef_files"]],
            "liberty_files": [pair._file_metadata(p) for p in spec["liberty_files"]],
            "sdc_file": pair._file_metadata(spec["sdc_file"]),
        },
        "import": {"name": "import", "status": "running"}, "stages": [],
    }

    def flush() -> None:
        pair._flush_worker_result(root, result, started)

    def checkpoint(record: dict) -> None:
        result["active_stage"] = "routing"
        result["stages"] = [record]
        if record["status"] == "passed" and "routing" not in result["completed_stages"]:
            result["completed_stages"].append("routing")
        flush()

    flush()
    flow = None
    try:
        module = pair._load_extension(args.module_dir)
        result["module"] = pair._file_metadata(Path(module.__file__).resolve())
        result["python"] = sys.version
        stage = next(s for s in spec["stages"] if s["name"] == "routing")
        source_config = json.loads(stage["config"].read_text())
        route_dir = root / "routing"
        rendered, _, _ = pair._rewrite_runtime_config(
            source_config, stage="routing", temp_directory=route_dir / "work", threads=args.threads,
        )
        logical_config = pair._masked_runtime_config(rendered, root)
        if args.backend == "idb":
            rendered = pair._idb_legacy_routing_config(rendered)
        config = route_dir / "config.json"
        pair._json_dump(config, rendered)
        result["stage_configs"] = {"routing": {
            "source": pair._file_metadata(stage["config"]),
            "rendered": pair._file_metadata(config), "masked_runtime": logical_config,
            "effective_threads": args.threads,
        }}
        flush()
        begin = time.perf_counter()
        if args.backend == "eccdb":
            flow = module.NativeToolchain([str(p) for p in spec["lef_files"]], str(source_def))
            flow.load_liberty([str(p) for p in spec["liberty_files"]])
            flow.load_sdc(str(spec["sdc_file"]))
            result["import"]["summary"] = flow.summary()
            writer = flow.write_def
        else:
            pair._require(module.db_init(output_path=str(root)), "db_init")
            pair._require(module.tech_lef_init(str(spec["lef_files"][0])), "tech_lef_init")
            pair._require(module.lef_init([str(p) for p in spec["lef_files"][1:]]), "lef_init")
            pair._require(module.def_init(str(source_def)), "def_init")
            pair._require(module.lib_init([str(p) for p in spec["liberty_files"]]), "lib_init")
            pair._require(module.sdc_init(str(spec["sdc_file"])), "sdc_init")
            writer = module.def_save
        result["import"].update({
            "output_def": pair._record_def(imported / "output.def", writer, backend=args.backend, stage="import"),
            "elapsed_ms": (time.perf_counter() - begin) * 1000,
            "rss_kib": pair._current_rss_kib(),
            "peak_rss_kib": resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
            "status": "passed",
        })
        pair._json_dump(imported / "result.json", result["import"])
        result["completed_stages"].append("import")
        flush()
        if args.backend == "eccdb":
            pair._run_eccdb_stage(flow, stage, config, route_dir, args.threads, checkpoint)
        else:
            pair._run_idb_stage(module, stage, config, route_dir, args.threads, checkpoint)
        result["status"] = "passed"
    except BaseException as error:
        result.update({"status": "failed", "error": repr(error), "traceback": traceback.format_exc()})
    finally:
        if flow is not None:
            del flow
        flush()
    return 0 if result["status"] == "passed" else 1


def coordinator(args: argparse.Namespace) -> int:
    pair._load_manifest(args.manifest)
    for path in (args.idb_def, args.eccdb_def):
        pair._require_nonempty(path, "CTS DEF input")
    root = args.output_root.resolve()
    root.mkdir(parents=True, exist_ok=False)
    (root / "comparison").mkdir()
    command = [
        sys.executable, str(Path(__file__).resolve()), "--worker",
        "--manifest", str(args.manifest.resolve()), "--idb-def", str(args.idb_def.resolve()),
        "--eccdb-def", str(args.eccdb_def.resolve()), "--output-root", str(root),
        "--module-dir", str(args.module_dir.resolve()), "--threads", str(args.threads),
    ]
    pair._json_dump(root / "invocation.json", {
        "manifest": pair._file_metadata(args.manifest.resolve()), "threads": args.threads,
        "idb_def": pair._file_metadata(args.idb_def.resolve()),
        "eccdb_def": pair._file_metadata(args.eccdb_def.resolve()),
        "note": "Fresh LEF/DEF import then routing only; no CTS rerun or binary reload.",
    })
    with ThreadPoolExecutor(max_workers=2) as pool:
        jobs = {
            b: pool.submit(pair._run_worker_process, command + ["--backend", b], b, root, args.timeout_seconds)
            for b in ("idb", "eccdb")
        }
        processes = {b: job.result() for b, job in jobs.items()}
    results = {b: pair._load_worker_result(root / b / "result.json", b) for b in processes}
    comparison = {
        "worker_processes": processes,
        "execution_passed": all(p.get("returncode") == 0 for p in processes.values()),
        "environment": {
            "same_module": results["idb"].get("module") == results["eccdb"].get("module"),
            "same_lef_liberty_sdc": all(
                results["idb"].get("input_files", {}).get(k) == results["eccdb"].get("input_files", {}).get(k)
                for k in ("lef_files", "liberty_files", "sdc_file")
            ),
            "same_routing_config": pair._comparable_stage_configs(results["idb"]) == pair._comparable_stage_configs(results["eccdb"]),
            "same_def_bytes": results["idb"].get("input_files", {}).get("def_file", {}).get("sha256")
            == results["eccdb"].get("input_files", {}).get("def_file", {}).get("sha256"),
            "different_cts_def_inputs_intentional": args.idb_def.resolve() != args.eccdb_def.resolve(),
        },
        "comparisons": {},
    }
    for stage in ("import", "routing"):
        left, right = (root / b / stage / "output.def" for b in ("eccdb", "idb"))
        if left.is_file() and right.is_file():
            comparison["comparisons"][stage] = pair._run_semantic_probe(
                args.semantic_probe, args.manifest.resolve(), left, right,
                root / "comparison" / f"{stage}.semantic.json",
            )
    comparison["semantic_equal"] = (
        len(comparison["comparisons"]) == 2
        and all(c.get("equal") is True for c in comparison["comparisons"].values())
    )
    pair._json_dump(root / "comparison.json", comparison)
    print(json.dumps({k: comparison[k] for k in ("execution_passed", "environment", "semantic_equal")}, indent=2))
    return 0 if comparison["execution_passed"] else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--idb-def", type=Path, required=True)
    parser.add_argument("--eccdb-def", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--module-dir", type=Path, default=pair.DEFAULT_MODULE_DIR)
    parser.add_argument("--threads", type=int, default=24)
    parser.add_argument("--timeout-seconds", type=int, default=3600)
    parser.add_argument("--semantic-probe", type=Path, default=pair.DEFAULT_SEMANTIC_PROBE)
    parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--backend", choices=("idb", "eccdb"), help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.threads <= 0 or (args.worker and not args.backend):
        parser.error("threads must be positive; worker requires backend")
    return worker(args) if args.worker else coordinator(args)


if __name__ == "__main__":
    raise SystemExit(main())
