#!/usr/bin/env python3
# SPDX-License-Identifier: MulanPSL-2.0
"""Run one CTS -> iRT -> iDRC chain through iDB and EccDB fairly.

There are two independent process-global database implementations in the
current extension.  A worker therefore owns exactly one provider, imports a
design once, and runs all three stages against that same owned design.  The
parent process deliberately launches the two workers separately; this makes
the comparison a provider comparison rather than a singleton-lifecycle test.

The input manifest follows ``ecc_native_flow``'s native manifest format.  It
must name exactly ``cts``, ``routing``, and ``drc`` in that order.  The runner
does not consider a zero shell exit code sufficient evidence of success: every
worker writes a stage status and every required DEF/DRC artifact is checked.

The optional common-importer semantic probe is supplied by the paired-flow
experiment under ``artifacts/chipyard-research-20261011/semantic-check``.  It
is intentionally external to this runner because it is a C++ precision
comparator, rather than another Python approximation of routing semantics.
"""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import resource
import subprocess
import sys
import time
import traceback
from typing import Any, Callable, Iterable


REPO = Path(__file__).resolve().parents[2]
DEFAULT_MODULE_DIR = REPO / "bin"
DEFAULT_SEMANTIC_PROBE = REPO / "artifacts/chipyard-research-20261011/semantic-check/semantic-probe"
DRC_COMPARATOR = REPO / "artifacts/operation-differential-20261010/drc-rt/compare_violation_maps.py"
REQUIRED_STAGES = ("cts", "routing", "drc")
STAGE_ORDER = ("import", *REQUIRED_STAGES)
THREAD_KEYS = frozenset({"-thread_number", "thread_number", "thread_num"})
TEMP_DIRECTORY_KEY = "-temp_directory_path"


class PairedFlowError(RuntimeError):
    """A test-harness error with a useful stage/provider boundary."""


def _json_dump(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    # A signal can terminate a worker while a native operation is active.  Use
    # replace instead of truncating the only status file in place, so the
    # coordinator still has the last complete checkpoint after that happens.
    temporary = path.with_name(f".{path.name}.tmp")
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    temporary.replace(path)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _file_metadata(path: Path) -> dict[str, Any]:
    if not path.is_file():
        raise FileNotFoundError(path)
    return {"path": str(path), "bytes": path.stat().st_size, "sha256": _sha256(path)}


def _current_rss_kib() -> int:
    for line in Path("/proc/self/status").read_text(encoding="utf-8").splitlines():
        if line.startswith("VmRSS:"):
            return int(line.split()[1])
    return 0


def _require(value: Any, operation: str) -> None:
    if value is not True:
        raise PairedFlowError(f"{operation} returned {value!r}")


def _require_nonempty(path: Path, description: str) -> None:
    if not path.is_file() or path.stat().st_size == 0:
        raise PairedFlowError(f"{description} is missing or empty: {path}")


def _resolve(base: Path, value: str) -> Path:
    candidate = Path(value)
    return candidate.resolve() if candidate.is_absolute() else (base / candidate).resolve()


def _as_string_paths(base: Path, values: Any, name: str) -> list[Path]:
    if not isinstance(values, list) or not values or not all(isinstance(value, str) for value in values):
        raise PairedFlowError(f"manifest {name} must be a non-empty string list")
    paths = [_resolve(base, value) for value in values]
    for path in paths:
        _require_nonempty(path, f"manifest {name} input")
    return paths


def _load_manifest(path: Path) -> dict[str, Any]:
    path = path.resolve()
    if not path.is_file():
        raise FileNotFoundError(path)
    raw = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(raw, dict):
        raise PairedFlowError("manifest root must be an object")
    base = path.parent
    lefs = _as_string_paths(base, raw.get("lef_files"), "lef_files")
    libraries = _as_string_paths(base, raw.get("liberty_files"), "liberty_files")
    def_file = raw.get("def_file")
    sdc_file = raw.get("sdc_file")
    if not isinstance(def_file, str) or not isinstance(sdc_file, str):
        raise PairedFlowError("manifest def_file and sdc_file must be strings")
    resolved_def = _resolve(base, def_file)
    resolved_sdc = _resolve(base, sdc_file)
    _require_nonempty(resolved_def, "manifest DEF")
    _require_nonempty(resolved_sdc, "manifest SDC")

    stages = raw.get("stages")
    if not isinstance(stages, list) or [item.get("name") if isinstance(item, dict) else None for item in stages] != list(REQUIRED_STAGES):
        raise PairedFlowError(
            "paired GCD runner requires manifest stages exactly: cts, routing, drc"
        )
    resolved_stages: list[dict[str, Any]] = []
    for item in stages:
        assert isinstance(item, dict)
        config_value = item.get("config")
        if not isinstance(config_value, str):
            raise PairedFlowError(f"stage {item['name']} has no string config")
        config_path = _resolve(base, config_value)
        _require_nonempty(config_path, f"{item['name']} config")
        options = item.get("options", {})
        if not isinstance(options, dict):
            raise PairedFlowError(f"stage {item['name']} options must be an object")
        resolved_stages.append({"name": item["name"], "config": config_path, "options": options})
    return {
        "manifest": path,
        "manifest_sha256": _sha256(path),
        "lef_files": lefs,
        "liberty_files": libraries,
        "def_file": resolved_def,
        "sdc_file": resolved_sdc,
        "stages": resolved_stages,
    }


def _find_thread_value(value: Any) -> int | None:
    if isinstance(value, dict):
        for key, child in value.items():
            if key in THREAD_KEYS:
                try:
                    candidate = int(child)
                except (TypeError, ValueError):
                    continue
                if candidate > 0:
                    return candidate
            nested = _find_thread_value(child)
            if nested is not None:
                return nested
    elif isinstance(value, list):
        for child in value:
            nested = _find_thread_value(child)
            if nested is not None:
                return nested
    return None


def _effective_threads(stage: dict[str, Any], config: Any, override: int) -> int:
    if override > 0:
        return override
    candidate = _find_thread_value(stage["options"])
    if candidate is None:
        candidate = _find_thread_value(config)
    return candidate if candidate is not None else 2


def _stages_through(stop_after: str) -> tuple[str, ...]:
    """Return the requested prefix of the import -> CTS -> RT -> DRC chain."""

    try:
        return STAGE_ORDER[:STAGE_ORDER.index(stop_after) + 1]
    except ValueError as error:  # argparse owns normal validation.
        raise PairedFlowError(f"unknown stop-after stage: {stop_after}") from error


def _coerce_like(value: Any, number: int) -> Any:
    return str(number) if isinstance(value, str) else number


def _rewrite_runtime_config(value: Any, *, stage: str, temp_directory: Path, threads: int) -> tuple[Any, bool, bool]:
    """Copy a source config and change only output/temp and thread settings.

    The returned flags identify whether a source key existed.  Routing configs
    often already contain ``RT.-temp_directory_path``; if an older config does
    not, an explicit RT key is added so legacy iDB and EccDB still receive the
    same output location.
    """

    saw_temp = False
    saw_threads = False

    def visit(item: Any) -> Any:
        nonlocal saw_temp, saw_threads
        if isinstance(item, dict):
            copied: dict[str, Any] = {}
            for key, child in item.items():
                if key == TEMP_DIRECTORY_KEY:
                    saw_temp = True
                    copied[key] = str(temp_directory)
                elif key in THREAD_KEYS:
                    saw_threads = True
                    copied[key] = _coerce_like(child, threads)
                else:
                    copied[key] = visit(child)
            return copied
        if isinstance(item, list):
            return [visit(child) for child in item]
        return item

    copied = visit(value)
    if stage == "routing" and isinstance(copied, dict):
        routing = copied.get("RT")
        if isinstance(routing, dict):
            if not saw_temp:
                routing[TEMP_DIRECTORY_KEY] = str(temp_directory)
            if not saw_threads:
                routing["-thread_number"] = threads
        elif not saw_threads:
            copied["-thread_number"] = threads
    return copied, saw_temp, saw_threads


def _idb_legacy_routing_config(value: Any) -> Any:
    """Serialize iRT's legacy Python JSON options without changing their value.

    The iDB Python iRT binding is built around the legacy JSON convention
    where every top-level scalar in the ``RT`` object is text (including
    ``-output_csv``).  It explicitly converts the typed options it consumes
    with ``stoi``.  Native EccDB accepts typed JSON directly, so this
    compatibility copy is emitted only for the iDB worker.  Nested values are
    left intact because the legacy binding has no typed contract for them.
    """

    if not isinstance(value, dict):
        raise PairedFlowError("iDB routing config root must be an object")
    copied = dict(value)
    routing = copied.get("RT")
    if not isinstance(routing, dict):
        raise PairedFlowError("iDB routing config must contain an RT object")
    compatible = dict(routing)
    for key, scalar in compatible.items():
        if isinstance(scalar, bool):
            compatible[key] = "1" if scalar else "0"
        elif isinstance(scalar, (int, float)):
            compatible[key] = str(scalar)
    copied["RT"] = compatible
    return copied


def _masked_runtime_config(value: Any, output_root: Path) -> Any:
    """Make provider-local output paths comparable without hiding inputs."""

    marker = "<backend-output>"

    def visit(item: Any) -> Any:
        if isinstance(item, dict):
            return {key: visit(child) for key, child in item.items()}
        if isinstance(item, list):
            return [visit(child) for child in item]
        if isinstance(item, str) and item.startswith(str(output_root)):
            return marker + item[len(str(output_root)):]
        return item

    return visit(value)


def _load_extension(module_dir: Path) -> Any:
    module_dir = module_dir.resolve()
    if str(module_dir) not in sys.path:
        sys.path.insert(0, str(module_dir))
    try:
        import ecc_py  # type: ignore[import-not-found]
    except Exception as error:  # pragma: no cover - emitted into test artifacts.
        raise PairedFlowError(f"cannot import ecc_py from {module_dir}: {error!r}") from error
    return ecc_py


def _stage_options(stage: dict[str, Any], *, stage_name: str, work: Path, threads: int) -> dict[str, Any]:
    options = dict(stage["options"])
    options["-thread_number"] = threads
    if stage_name == "cts":
        options["-work_dir"] = str(work)
    elif stage_name in {"routing", "drc"}:
        options[TEMP_DIRECTORY_KEY] = str(work)
    return options


def _record_def(output: Path, writer: Any, *, backend: str, stage: str) -> dict[str, Any]:
    started = time.perf_counter()
    writer(str(output))
    _require_nonempty(output, f"{backend} {stage} DEF")
    return {"path": str(output), "metadata": _file_metadata(output), "write_ms": (time.perf_counter() - started) * 1000.0}


def _write_binary(flow: Any, directory: Path) -> dict[str, Any]:
    started = time.perf_counter()
    flow.write_binary(str(directory))
    files = [directory / name for name in ("technology.edb", "library.edb", "design.edb")]
    for path in files:
        _require_nonempty(path, "EccDB binary checkpoint")
    return {"directory": str(directory), "files": [_file_metadata(path) for path in files], "write_ms": (time.perf_counter() - started) * 1000.0}


def _capture_violation_map(work: Path) -> dict[str, Any]:
    path = work / "violation_map.json"
    _require_nonempty(path, "iDRC violation map")
    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, list):
        raise PairedFlowError(f"violation map must be a JSON array: {path}")
    distribution: dict[str, int] = {}
    for item in payload:
        if isinstance(item, dict):
            name = str(item.get("type", "<missing>"))
            distribution[name] = distribution.get(name, 0) + 1
    return {"path": str(path), "metadata": _file_metadata(path), "count": len(payload), "distribution": dict(sorted(distribution.items()))}


def _run_idb_stage(
    ecc_py: Any,
    stage: dict[str, Any],
    rendered_config: Path,
    stage_dir: Path,
    threads: int,
    checkpoint: Callable[[dict[str, Any]], None],
) -> dict[str, Any]:
    name = stage["name"]
    work = stage_dir / "work"
    work.mkdir(parents=True, exist_ok=True)
    record: dict[str, Any] = {"name": name, "backend": "idb", "status": "running", "work_directory": str(work)}
    started = time.perf_counter()
    _json_dump(stage_dir / "result.json", record)
    checkpoint(record)
    try:
        if name == "cts":
            begin = time.perf_counter()
            _require(ecc_py.run_cts(str(rendered_config), str(work)), "run_cts")
            record["algorithm_writeback_ms"] = (time.perf_counter() - begin) * 1000.0
            begin = time.perf_counter()
            _require(ecc_py.cts_report(str(work)), "cts_report")
            record["report_ms"] = (time.perf_counter() - begin) * 1000.0
            record["clock_timing"] = ecc_py.cts_timing_feature()
            begin = time.perf_counter()
            _require(ecc_py.destroy_cts(), "destroy_cts")
            record["destroy_ms"] = (time.perf_counter() - begin) * 1000.0
        elif name == "routing":
            begin = time.perf_counter()
            _require(ecc_py.init_rt(str(rendered_config), {}), "init_rt")
            record["init_ms"] = (time.perf_counter() - begin) * 1000.0
            begin = time.perf_counter()
            _require(ecc_py.run_rt(), "run_rt")
            record["algorithm_ms"] = (time.perf_counter() - begin) * 1000.0
            begin = time.perf_counter()
            _require(ecc_py.destroy_rt(), "destroy_rt")
            record["writeback_destroy_ms"] = (time.perf_counter() - begin) * 1000.0
        elif name == "drc":
            begin = time.perf_counter()
            _require(ecc_py.init_drc(str(work), threads), "init_drc")
            record["init_ms"] = (time.perf_counter() - begin) * 1000.0
            begin = time.perf_counter()
            # The legacy Python binding ignores both arguments.  Keeping the
            # rendered config in the call record documents that fact rather
            # than pretending iDRC consumes a different configuration.
            _require(ecc_py.run_drc(str(rendered_config), ""), "run_drc")
            record["algorithm_destroy_ms"] = (time.perf_counter() - begin) * 1000.0
            record["legacy_run_drc_ignores_config"] = True
            record["save_drc_return"] = bool(ecc_py.save_drc(str(stage_dir / "drc.bin")))
            record["violation_map"] = _capture_violation_map(work)
        else:  # pragma: no cover - manifest validation owns this.
            raise PairedFlowError(f"unsupported stage: {name}")
        record["output_def"] = _record_def(stage_dir / "output.def", ecc_py.def_save, backend="idb", stage=name)
        record["status"] = "passed"
    except BaseException as error:
        record.update({"status": "failed", "error": repr(error), "traceback": traceback.format_exc()})
        raise
    finally:
        record["elapsed_ms"] = (time.perf_counter() - started) * 1000.0
        record["rss_kib"] = _current_rss_kib()
        record["peak_rss_kib"] = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
        _json_dump(stage_dir / "result.json", record)
        checkpoint(record)
    return record


def _run_eccdb_stage(
    flow: Any,
    stage: dict[str, Any],
    rendered_config: Path,
    stage_dir: Path,
    threads: int,
    checkpoint: Callable[[dict[str, Any]], None],
) -> dict[str, Any]:
    name = stage["name"]
    work = stage_dir / "work"
    work.mkdir(parents=True, exist_ok=True)
    record: dict[str, Any] = {"name": name, "backend": "eccdb", "status": "running", "work_directory": str(work), "before": flow.summary()}
    started = time.perf_counter()
    _json_dump(stage_dir / "result.json", record)
    checkpoint(record)
    try:
        operation = flow.run(name, str(rendered_config), _stage_options(stage, stage_name=name, work=work, threads=threads))
        record["operation"] = operation
        record["after"] = flow.summary()
        record["output_def"] = _record_def(stage_dir / "output.def", flow.write_def, backend="eccdb", stage=name)
        record["binary_checkpoint"] = _write_binary(flow, stage_dir / "binary")
        if name == "drc":
            record["violation_map"] = _capture_violation_map(work)
        record["status"] = "passed" if operation.get("status") == "passed" else "failed"
        if record["status"] != "passed":
            raise PairedFlowError(f"native {name} result status is {operation.get('status')!r}")
    except BaseException as error:
        record.update({"status": "failed", "error": repr(error), "traceback": traceback.format_exc()})
        raise
    finally:
        record["elapsed_ms"] = (time.perf_counter() - started) * 1000.0
        record["rss_kib"] = _current_rss_kib()
        record["peak_rss_kib"] = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
        _json_dump(stage_dir / "result.json", record)
        checkpoint(record)
    return record


def _flush_worker_result(backend_root: Path, result: dict[str, Any], started: float) -> None:
    """Persist the worker state at an operation boundary."""

    result["wall_seconds"] = time.perf_counter() - started
    result["rss_kib"] = _current_rss_kib()
    result["peak_rss_kib"] = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    _json_dump(backend_root / "result.json", result)


def _worker(args: argparse.Namespace) -> int:
    specification = _load_manifest(args.manifest)
    requested_stages = _stages_through(args.stop_after)
    output_root = args.output_root.resolve()
    backend_root = output_root / args.backend
    if backend_root.exists():
        raise FileExistsError(f"refusing to overwrite backend output: {backend_root}")
    backend_root.mkdir(parents=True)

    os.environ["ECC_LOGGER_THROW_ON_ERROR"] = "1"
    if args.threads > 0:
        os.environ["OMP_NUM_THREADS"] = str(args.threads)
    started = time.perf_counter()
    import_dir = backend_root / "import"
    import_dir.mkdir()
    import_record: dict[str, Any] = {
        "name": "import",
        "backend": args.backend,
        "status": "running",
        "work_directory": str(import_dir),
    }
    result: dict[str, Any] = {
        "schema_version": 1,
        "backend": args.backend,
        "status": "running",
        "stop_after": args.stop_after,
        "requested_stages": list(requested_stages),
        "requested_prefix_status": "running",
        "full_chain_status": "running",
        "manifest": {"path": str(specification["manifest"]), "sha256": specification["manifest_sha256"]},
        "input_files": {
            "lef_files": [_file_metadata(path) for path in specification["lef_files"]],
            "liberty_files": [_file_metadata(path) for path in specification["liberty_files"]],
            "def_file": _file_metadata(specification["def_file"]),
            "sdc_file": _file_metadata(specification["sdc_file"]),
        },
        "import": import_record,
        "stages": [],
        "completed_stages": [],
        "active_stage": "import",
    }
    _json_dump(import_dir / "result.json", import_record)
    _flush_worker_result(backend_root, result, started)

    def checkpoint_stage(record: dict[str, Any]) -> None:
        """Keep the aggregate result useful even if native code terminates."""

        for current in result["stages"]:
            if current.get("name") == record["name"]:
                break
        else:
            result["stages"].append(record)
        result["active_stage"] = record["name"]
        if record.get("status") == "passed" and record["name"] not in result["completed_stages"]:
            result["completed_stages"].append(record["name"])
        _flush_worker_result(backend_root, result, started)

    try:
        ecc_py = _load_extension(args.module_dir)
        module = Path(ecc_py.__file__).resolve()
        result["module"] = _file_metadata(module)
        result["python"] = sys.version
        stage_configs: list[tuple[dict[str, Any], Path, int, Any]] = []
        for stage in specification["stages"]:
            source = json.loads(stage["config"].read_text(encoding="utf-8"))
            threads = _effective_threads(stage, source, args.threads)
            stage_dir = backend_root / stage["name"]
            work = stage_dir / "work"
            rendered, saw_temp, saw_threads = _rewrite_runtime_config(source, stage=stage["name"], temp_directory=work, threads=threads)
            logical_runtime = _masked_runtime_config(rendered, backend_root)
            compatibility_applied = args.backend == "idb" and stage["name"] == "routing"
            if compatibility_applied:
                rendered = _idb_legacy_routing_config(rendered)
            config_path = stage_dir / "config.json"
            _json_dump(config_path, rendered)
            stage_configs.append((stage, config_path, threads, {
                "source": _file_metadata(stage["config"]),
                "rendered": _file_metadata(config_path),
                # Compare the same typed runtime intent across providers;
                # iDB's actual routing file may use its legacy string-only
                # encoding for the handful of options above.
                "masked_runtime": logical_runtime,
                "provider_config_compatibility": "idb_legacy_rt_string_scalars" if compatibility_applied else None,
                "source_had_temp_directory": saw_temp,
                "source_had_thread_option": saw_threads,
                "effective_threads": threads,
            }))
        result["stage_configs"] = {stage["name"]: metadata for stage, _path, _threads, metadata in stage_configs}
        _flush_worker_result(backend_root, result, started)

        import_started = time.perf_counter()
        if args.backend == "eccdb":
            flow = ecc_py.NativeToolchain([str(path) for path in specification["lef_files"]], str(specification["def_file"]))
            try:
                flow.load_liberty([str(path) for path in specification["liberty_files"]])
                flow.load_sdc(str(specification["sdc_file"]))
                import_record.update({
                    "status": "passed",
                    "summary": flow.summary(),
                    "output_def": _record_def(import_dir / "output.def", flow.write_def, backend="eccdb", stage="import"),
                    "binary_checkpoint": _write_binary(flow, import_dir / "binary"),
                })
                import_record["elapsed_ms"] = (time.perf_counter() - import_started) * 1000.0
                import_record["rss_kib"] = _current_rss_kib()
                import_record["peak_rss_kib"] = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
                _json_dump(import_dir / "result.json", import_record)
                result["completed_stages"].append("import")
                _flush_worker_result(backend_root, result, started)
                for stage, config_path, threads, _metadata in stage_configs:
                    if stage["name"] not in requested_stages:
                        continue
                    _run_eccdb_stage(flow, stage, config_path, backend_root / stage["name"], threads, checkpoint_stage)
            finally:
                # NativeToolchain itself owns the EccDB and clears all Operation
                # singleton source pointers at destruction.
                del flow
        else:
            _require(ecc_py.db_init(output_path=str(backend_root)), "db_init")
            _require(ecc_py.tech_lef_init(str(specification["lef_files"][0])), "tech_lef_init")
            _require(ecc_py.lef_init([str(path) for path in specification["lef_files"][1:]]), "lef_init")
            _require(ecc_py.def_init(str(specification["def_file"])), "def_init")
            _require(ecc_py.lib_init([str(path) for path in specification["liberty_files"]]), "lib_init")
            _require(ecc_py.sdc_init(str(specification["sdc_file"])), "sdc_init")
            import_record.update({
                "status": "passed",
                "output_def": _record_def(import_dir / "output.def", ecc_py.def_save, backend="idb", stage="import"),
                "elapsed_ms": (time.perf_counter() - import_started) * 1000.0,
                "rss_kib": _current_rss_kib(),
                "peak_rss_kib": resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
            })
            _json_dump(import_dir / "result.json", import_record)
            result["completed_stages"].append("import")
            _flush_worker_result(backend_root, result, started)
            for stage, config_path, threads, _metadata in stage_configs:
                if stage["name"] not in requested_stages:
                    continue
                _run_idb_stage(ecc_py, stage, config_path, backend_root / stage["name"], threads, checkpoint_stage)
        result["status"] = "passed"
        result["requested_prefix_status"] = "passed"
        result["full_chain_status"] = "passed" if args.stop_after == "drc" else "not_requested"
    except BaseException as error:
        active_stage = str(result.get("active_stage", "import"))
        active_record = import_record if active_stage == "import" else next(
            (record for record in result["stages"] if record.get("name") == active_stage), None
        )
        if isinstance(active_record, dict) and active_record.get("status") == "running":
            active_record.update({"status": "failed", "error": repr(error), "traceback": traceback.format_exc()})
            active_dir = import_dir if active_stage == "import" else backend_root / active_stage
            _json_dump(active_dir / "result.json", active_record)
        result.update({
            "status": "failed",
            "requested_prefix_status": "failed",
            "full_chain_status": "incomplete",
            "failed_stage": active_stage,
            "error": repr(error),
            "traceback": traceback.format_exc(),
        })
    _flush_worker_result(backend_root, result, started)
    print(json.dumps({key: result.get(key) for key in ("backend", "status", "stop_after", "failed_stage", "wall_seconds", "peak_rss_kib")}, sort_keys=True), flush=True)
    return 0 if result["status"] == "passed" else 1


def _load_module(path: Path, name: str) -> Any:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise PairedFlowError(f"cannot import helper: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _normal_clock_timing(value: Any, *, backend: str) -> dict[str, Any]:
    if backend == "eccdb":
        clocks = value.get("clock_timing", []) if isinstance(value, dict) else []
    else:
        clocks = value.get("clocks", []) if isinstance(value, dict) else []
    normalized: dict[str, dict[str, Any]] = {}
    for clock in clocks:
        if not isinstance(clock, dict) or "clock" not in clock:
            continue
        normalized[str(clock["clock"])] = {
            key: clock.get(key)
            for key in (
                "sink_count", "target_skew_ns", "initial_skew_ns", "optimized_skew_ns",
                "min_insertion_latency_ns", "max_insertion_latency_ns", "mean_insertion_latency_ns", "target_met",
            )
            if key in clock
        }
    return normalized


def _clock_values_equal(left: Any, right: Any) -> bool:
    if isinstance(left, bool) or isinstance(right, bool):
        return left is right
    if isinstance(left, (int, float)) and isinstance(right, (int, float)):
        # The same CTS kernels can differ by a few floating-point ULPs after
        # provider conversion; 1 ps is deliberately much tighter than the
        # 80 ps GCD skew target.
        return math.isclose(float(left), float(right), rel_tol=0.0, abs_tol=1.0e-6)
    return left == right


def _compare_clock_timing(eccdb: dict[str, Any], idb: dict[str, Any]) -> dict[str, Any]:
    common = sorted(set(eccdb) & set(idb))
    clock_report: dict[str, Any] = {}
    comparable_equal = True
    for name in common:
        left, right = eccdb[name], idb[name]
        fields = sorted(set(left) & set(right))
        differences = {
            field: {"eccdb": left[field], "idb": right[field]}
            for field in fields
            if not _clock_values_equal(left[field], right[field])
        }
        comparable_equal = comparable_equal and not differences
        clock_report[name] = {
            "shared_fields": fields,
            "different_shared_fields": differences,
            "eccdb_only_fields": sorted(set(left) - set(right)),
            "idb_only_fields": sorted(set(right) - set(left)),
        }
    return {
        "equal_on_shared_fields": comparable_equal and set(eccdb) == set(idb),
        "eccdb_only_clocks": sorted(set(eccdb) - set(idb)),
        "idb_only_clocks": sorted(set(idb) - set(eccdb)),
        "clocks": clock_report,
        "eccdb": eccdb,
        "idb": idb,
        "note": "NativeToolchain currently exposes a smaller CTS timing schema; missing fields are reported, not treated as a numeric mismatch.",
    }


def _snapshot_component_delta(before: dict[str, Any], after: dict[str, Any]) -> dict[str, Any]:
    """Return the materialized component delta without treating either side as truth."""

    before_map = {str(item["name"]): item for item in before.get("components", [])}
    after_map = {str(item["name"]): item for item in after.get("components", [])}
    before_names, after_names = set(before_map), set(after_map)
    added = sorted(after_names - before_names)
    removed = sorted(before_names - after_names)
    changed = []
    for name in sorted(before_names & after_names):
        left, right = before_map[name], after_map[name]
        if any(left.get(field) != right.get(field) for field in ("master", "status", "placement")):
            changed.append({"component": name, "before": left, "after": right})
    before_clocks = {str(item["name"]): item for item in before.get("clock_nets", [])}
    after_clocks = {str(item["name"]): item for item in after.get("clock_nets", [])}
    return {
        "before_component_count": len(before_map),
        "after_component_count": len(after_map),
        "added_component_count": len(added),
        "removed_component_count": len(removed),
        "added_components": [after_map[name] for name in added],
        "removed_components": [before_map[name] for name in removed],
        "changed_existing_components": changed,
        "before_clock_net_count": len(before_clocks),
        "after_clock_net_count": len(after_clocks),
        "added_clock_nets": sorted(after_clocks.keys() - before_clocks.keys()),
        "removed_clock_nets": sorted(before_clocks.keys() - after_clocks.keys()),
    }


def _run_semantic_probe(probe: Path | None, manifest: Path, left: Path, right: Path, output: Path) -> dict[str, Any]:
    if probe is None:
        return {"status": "not_run", "reason": "no semantic probe was supplied or built"}
    if not probe.is_file() or not os.access(probe, os.X_OK):
        return {"status": "not_run", "reason": f"semantic probe is not executable: {probe}"}
    command = [str(probe), str(manifest), str(left), str(right), str(output)]
    completed = subprocess.run(command, text=True, capture_output=True, check=False)
    logs = {
        "command": command,
        "returncode": completed.returncode,
        "stdout": completed.stdout,
        "stderr": completed.stderr,
    }
    _json_dump(output.with_suffix(".launcher.json"), logs)
    if completed.returncode == 2 or not output.is_file():
        return {"status": "failed", "probe": logs, "report": str(output) if output.is_file() else None}
    report = json.loads(output.read_text(encoding="utf-8"))
    # The probe documents 0 = equal and 1 = represented semantic differences.
    return {"status": "completed", "equal": completed.returncode == 0, "probe": logs, "report": report, "report_path": str(output)}


def _compare_drc(eccdb_map: Path, idb_map: Path) -> dict[str, Any]:
    comparator = _load_module(DRC_COMPARATOR, "paired_gcd_drc_comparator")
    raw = comparator.comparison(comparator.counter(eccdb_map), comparator.counter(idb_map))
    normalized = comparator.comparison(
        comparator.counter(eccdb_map, normalize_identifiers=True),
        comparator.counter(idb_map, normalize_identifiers=True),
    )
    return {
        "status": "completed",
        "equal": normalized["equal"],
        "raw": raw,
        "def_identifier_normalized": normalized,
        "note": "Reuses the existing DRC physical-key comparator; `equal` applies only identifier escape normalization.",
    }


def _stage_result(backend: dict[str, Any], name: str) -> dict[str, Any]:
    if name == "import":
        record = backend.get("import")
        if isinstance(record, dict):
            return record
    for stage in backend.get("stages", []):
        if stage.get("name") == name:
            return stage
    raise PairedFlowError(f"{backend.get('backend')} result omitted stage {name}")


def _comparable_stage_configs(payload: dict[str, Any]) -> dict[str, Any]:
    """Discard only backend-local file paths and metadata from config parity."""
    values = payload.get("stage_configs", {})
    if not isinstance(values, dict):
        return {}
    return {
        name: {
            "source_sha256": metadata.get("source", {}).get("sha256"),
            "masked_runtime": metadata.get("masked_runtime"),
            "source_had_temp_directory": metadata.get("source_had_temp_directory"),
            "source_had_thread_option": metadata.get("source_had_thread_option"),
            "effective_threads": metadata.get("effective_threads"),
        }
        for name, metadata in values.items()
        if isinstance(metadata, dict)
    }


def _load_worker_result(path: Path, backend: str) -> dict[str, Any]:
    """Return a diagnosable payload even when a worker died before writing."""

    if not path.is_file():
        return {"backend": backend, "status": "not_started", "error": f"missing result: {path}"}
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        return {"backend": backend, "status": "unreadable", "error": repr(error)}
    return value if isinstance(value, dict) else {"backend": backend, "status": "unreadable", "error": "result root is not an object"}


def _stage_status(payload: dict[str, Any], name: str) -> str:
    try:
        record = _stage_result(payload, name)
    except PairedFlowError:
        return "not_started"
    return str(record.get("status", "not_started"))


def _stage_output(record: dict[str, Any]) -> Path:
    output = record.get("output_def")
    if not isinstance(output, dict) or not isinstance(output.get("path"), str):
        raise PairedFlowError("passed stage has no output DEF record")
    path = Path(output["path"])
    _require_nonempty(path, "stage output DEF")
    return path


def _compare(output_root: Path, specification: dict[str, Any], probe: Path | None, stop_after: str) -> dict[str, Any]:
    idb = _load_worker_result(output_root / "idb" / "result.json", "idb")
    eccdb = _load_worker_result(output_root / "eccdb" / "result.json", "eccdb")
    requested_stages = _stages_through(stop_after)
    comparison: dict[str, Any] = {
        "schema_version": 1,
        "status": "incomplete",
        "stop_after": stop_after,
        "requested_stages": list(requested_stages),
        "workers": {
            backend: {
                "status": payload.get("status"),
                "requested_prefix_status": payload.get("requested_prefix_status"),
                "full_chain_status": payload.get("full_chain_status"),
                "failed_stage": payload.get("failed_stage"),
                "active_stage": payload.get("active_stage"),
                "completed_stages": payload.get("completed_stages", []),
                "wall_seconds": payload.get("wall_seconds"),
                "peak_rss_kib": payload.get("peak_rss_kib"),
            }
            for backend, payload in (("idb", idb), ("eccdb", eccdb))
        },
    }
    input_equal = idb.get("input_files") == eccdb.get("input_files")
    module_equal = idb.get("module", {}).get("sha256") == eccdb.get("module", {}).get("sha256")
    config_equal = _comparable_stage_configs(idb) == _comparable_stage_configs(eccdb)
    comparison["environment"] = {
        "same_input_file_metadata": input_equal,
        "same_ecc_py_module": module_equal,
        "same_masked_stage_configs": config_equal,
        "note": "Only backend-local output directories are intentionally different.",
    }
    report_root = output_root / "comparison"
    report_root.mkdir(exist_ok=True)
    stage_reports: dict[str, Any] = {}
    all_semantically_equal = True
    completed_stages: list[str] = []
    for stage_name in STAGE_ORDER:
        idb_status = _stage_status(idb, stage_name)
        eccdb_status = _stage_status(eccdb, stage_name)
        stage_report: dict[str, Any] = {
            "status": "not_comparable",
            "idb_status": idb_status,
            "eccdb_status": eccdb_status,
        }
        if idb_status == "passed" and eccdb_status == "passed":
            try:
                ecc_stage = _stage_result(eccdb, stage_name)
                idb_stage = _stage_result(idb, stage_name)
                left = _stage_output(ecc_stage)
                right = _stage_output(idb_stage)
                semantic = _run_semantic_probe(probe, specification["manifest"], left, right, report_root / f"{stage_name}.semantic.json")
                stage_report.update({
                    "status": "completed",
                    "eccdb_def": _file_metadata(left),
                    "idb_def": _file_metadata(right),
                    "semantic": semantic,
                })
                completed_stages.append(stage_name)
                if semantic.get("status") == "completed":
                    all_semantically_equal = all_semantically_equal and bool(semantic.get("equal"))
                elif semantic.get("status") == "failed":
                    all_semantically_equal = False
                if stage_name == "cts":
                    native_timing = _normal_clock_timing(ecc_stage.get("operation", {}), backend="eccdb")
                    legacy_timing = _normal_clock_timing(idb_stage.get("clock_timing", {}), backend="idb")
                    stage_report["clock_timing"] = _compare_clock_timing(native_timing, legacy_timing)
                    # The common semantic probe intentionally focuses on the
                    # complete DEF representation.  Keep a CTS-specific,
                    # human-auditable view as well: new instances, clock-net
                    # endpoint multisets and USE attributes.  This is a
                    # diagnostic projection only; it never mutates either
                    # backend and does not make iDB the reference.
                    try:
                        cts_snapshot_module = _load_module(
                            REPO / "test/native/cts_snapshot.py",
                            "paired_gcd_cts_snapshot",
                        )
                        ecc_import = _stage_output(_stage_result(eccdb, "import"))
                        idb_import = _stage_output(_stage_result(idb, "import"))
                        ecc_before = cts_snapshot_module.snapshot_def(ecc_import)
                        idb_before = cts_snapshot_module.snapshot_def(idb_import)
                        ecc_after = cts_snapshot_module.snapshot_def(left)
                        idb_after = cts_snapshot_module.snapshot_def(right)
                        ecc_before_path = report_root / "cts.snapshot.eccdb.before.json"
                        idb_before_path = report_root / "cts.snapshot.idb.before.json"
                        ecc_after_path = report_root / "cts.snapshot.eccdb.after.json"
                        idb_after_path = report_root / "cts.snapshot.idb.after.json"
                        _json_dump(ecc_before_path, ecc_before)
                        _json_dump(idb_before_path, idb_before)
                        _json_dump(ecc_after_path, ecc_after)
                        _json_dump(idb_after_path, idb_after)
                        snapshot_comparison = cts_snapshot_module.compare_snapshots(ecc_after, idb_after)
                        snapshot_path = report_root / "cts.snapshot.comparison.json"
                        _json_dump(snapshot_path, snapshot_comparison)
                        snapshot_report = {
                            "status": "completed",
                            "equal": bool(snapshot_comparison.get("equal")),
                            "comparison_path": str(snapshot_path),
                            "eccdb_before_path": str(ecc_before_path),
                            "eccdb_after_path": str(ecc_after_path),
                            "idb_before_path": str(idb_before_path),
                            "idb_after_path": str(idb_after_path),
                            "delta": {
                                "eccdb": _snapshot_component_delta(ecc_before, ecc_after),
                                "idb": _snapshot_component_delta(idb_before, idb_after),
                            },
                        }
                        stage_report["cts_snapshot"] = snapshot_report
                        all_semantically_equal = all_semantically_equal and snapshot_report["equal"]
                    except BaseException as snapshot_error:
                        stage_report["cts_snapshot"] = {
                            "status": "failed",
                            "error": repr(snapshot_error),
                            "traceback": traceback.format_exc(),
                        }
                        all_semantically_equal = False
                elif stage_name == "drc":
                    ecc_map = ecc_stage.get("violation_map", {}).get("path")
                    idb_map = idb_stage.get("violation_map", {}).get("path")
                    if isinstance(ecc_map, str) and isinstance(idb_map, str):
                        stage_report["drc"] = _compare_drc(Path(ecc_map), Path(idb_map))
            except BaseException as error:
                stage_report.update({"status": "comparison_failed", "error": repr(error), "traceback": traceback.format_exc()})
        stage_reports[stage_name] = stage_report
    comparison["stages"] = stage_reports
    comparison["completed_stages"] = completed_stages
    requested_prefix_completed = all(stage_name in completed_stages for stage_name in requested_stages)
    full_chain_completed = all(stage_name in completed_stages for stage_name in STAGE_ORDER)
    comparison["requested_prefix_status"] = "passed" if requested_prefix_completed else "incomplete"
    comparison["semantic_equal_for_completed_stages"] = all_semantically_equal if completed_stages else None
    # "completed" remains reserved for the complete import -> CTS -> RT -> DRC
    # chain.  A deliberately stopped prefix has useful completed stage reports,
    # but it must not look like a full-flow pass.
    comparison["status"] = "completed" if full_chain_completed else "incomplete"
    comparison["semantic_equal"] = all_semantically_equal if full_chain_completed else None
    if not full_chain_completed:
        comparison["reason"] = (
            f"only the completed stages were compared; requested stop-after={stop_after}"
            if requested_prefix_completed
            else "at least one requested stage did not complete on both backends"
        )
    return comparison


def _run_worker_process(command: list[str], backend: str, output_root: Path, timeout_seconds: int) -> dict[str, Any]:
    """Run one provider worker while retaining verbose native output on disk."""

    started = time.perf_counter()
    stdout_path = output_root / f"{backend}.stdout.log"
    stderr_path = output_root / f"{backend}.stderr.log"
    try:
        with stdout_path.open("w", encoding="utf-8") as stdout, stderr_path.open("w", encoding="utf-8") as stderr:
            completed = subprocess.run(
                command,
                text=True,
                stdout=stdout,
                stderr=stderr,
                check=False,
                timeout=timeout_seconds or None,
            )
        return {
            "command": command,
            "returncode": completed.returncode,
            "elapsed_seconds": time.perf_counter() - started,
            "stdout_log": str(stdout_path),
            "stderr_log": str(stderr_path),
        }
    except subprocess.TimeoutExpired:
        return {
            "command": command,
            "returncode": None,
            "timed_out": True,
            "elapsed_seconds": time.perf_counter() - started,
            "stdout_log": str(stdout_path),
            "stderr_log": str(stderr_path),
        }


def _coordinator(args: argparse.Namespace) -> int:
    specification = _load_manifest(args.manifest)
    requested_stages = _stages_through(args.stop_after)
    output_root = args.output_root.resolve()
    if output_root.exists():
        raise FileExistsError(f"refusing to overwrite paired output root: {output_root}")
    output_root.mkdir(parents=True)
    _json_dump(output_root / "manifest.resolved.json", {
        "manifest": str(specification["manifest"]),
        "manifest_sha256": specification["manifest_sha256"],
        "lef_files": [str(path) for path in specification["lef_files"]],
        "liberty_files": [str(path) for path in specification["liberty_files"]],
        "def_file": str(specification["def_file"]),
        "sdc_file": str(specification["sdc_file"]),
        "stop_after": args.stop_after,
        "requested_stages": list(requested_stages),
        "stages": [{"name": stage["name"], "config": str(stage["config"]), "options": stage["options"]} for stage in specification["stages"]],
    })
    script = Path(__file__).resolve()
    commands: dict[str, list[str]] = {}
    for backend in ("idb", "eccdb"):
        commands[backend] = [
            sys.executable, str(script), "--worker", "--backend", backend,
            "--manifest", str(specification["manifest"]), "--output-root", str(output_root),
            "--module-dir", str(args.module_dir.resolve()), "--threads", str(args.threads),
            "--stop-after", args.stop_after,
        ]
    process_results: dict[str, Any] = {}
    backends = ("idb", "eccdb")
    if args.parallel_backends:
        with ThreadPoolExecutor(max_workers=2) as executor:
            futures = {
                backend: executor.submit(_run_worker_process, commands[backend], backend, output_root, args.timeout_seconds)
                for backend in backends
            }
            for backend in backends:
                process_results[backend] = futures[backend].result()
    else:
        for backend in backends:
            process_results[backend] = _run_worker_process(commands[backend], backend, output_root, args.timeout_seconds)
    for backend in backends:
        _json_dump(output_root / f"{backend}.launcher.json", process_results[backend])
    probe = args.semantic_probe.resolve() if args.semantic_probe is not None else (DEFAULT_SEMANTIC_PROBE if DEFAULT_SEMANTIC_PROBE.exists() else None)
    try:
        comparison = _compare(output_root, specification, probe, args.stop_after)
    except BaseException as error:
        comparison = {"status": "failed", "error": repr(error), "traceback": traceback.format_exc()}
    comparison["worker_processes"] = process_results
    _json_dump(output_root / "comparison.json", comparison)
    worker_status = all(item.get("returncode") == 0 for item in process_results.values())
    full_completed = comparison.get("status") == "completed"
    requested_prefix_completed = comparison.get("requested_prefix_status") == "passed"
    semantic_difference = (
        comparison.get("semantic_equal") is False
        or comparison.get("semantic_equal_for_completed_stages") is False
    )
    full_success = worker_status and full_completed and not (args.fail_on_semantic_difference and semantic_difference)
    prefix_success = worker_status and requested_prefix_completed and not (args.fail_on_semantic_difference and semantic_difference)
    outcome = {
        # A prefix is intentionally useful for diagnosing import/CTS, but it
        # never claims full-flow success in the persisted result.
        "status": "passed" if full_success else ("incomplete" if prefix_success else "failed"),
        "worker_processes_ok": worker_status,
        "comparison_status": comparison.get("status"),
        "semantic_equal": comparison.get("semantic_equal"),
        "requested_stop_after": args.stop_after,
        "requested_prefix_status": comparison.get("requested_prefix_status"),
        "semantic_equal_for_completed_stages": comparison.get("semantic_equal_for_completed_stages"),
        "output_root": str(output_root),
    }
    _json_dump(output_root / "result.json", outcome)
    print(json.dumps(outcome, sort_keys=True), flush=True)
    # CI can use a successfully compared prefix as an independent acceptance
    # target, while its JSON result still makes clear that the full chain is
    # incomplete.
    return 0 if full_success or (args.stop_after != "drc" and prefix_success) else 1


def _arguments(argv: Iterable[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True, help="native CTS/routing/DRC manifest")
    parser.add_argument("--output-root", type=Path, required=True, help="new directory for both provider runs")
    parser.add_argument("--module-dir", type=Path, default=DEFAULT_MODULE_DIR, help="directory containing ecc_py")
    parser.add_argument("--threads", type=int, default=0, help="override thread count for both providers; 0 keeps manifest setting")
    parser.add_argument(
        "--stop-after",
        choices=STAGE_ORDER,
        default="drc",
        help="run only through this stage; a prefix never marks the full chain passed",
    )
    parser.add_argument(
        "--parallel-backends",
        action="store_true",
        help="run iDB and EccDB workers concurrently; each still owns a separate process and output directory",
    )
    parser.add_argument("--timeout-seconds", type=int, default=0, help="per-backend worker timeout; 0 disables timeout")
    parser.add_argument("--semantic-probe", type=Path, help="common-importer semantic comparator executable")
    parser.add_argument("--fail-on-semantic-difference", action="store_true", help="make represented-semantic differences a non-zero coordinator exit")
    parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--backend", choices=("idb", "eccdb"), help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    if args.threads < 0:
        parser.error("--threads must be non-negative")
    if args.timeout_seconds < 0:
        parser.error("--timeout-seconds must be non-negative")
    if args.worker and args.backend is None:
        parser.error("--worker requires --backend")
    if not args.worker and args.backend is not None:
        parser.error("--backend is only valid with --worker")
    return args


def main(argv: Iterable[str] | None = None) -> int:
    args = _arguments(argv)
    return _worker(args) if args.worker else _coordinator(args)


if __name__ == "__main__":
    raise SystemExit(main())
