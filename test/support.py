import gzip
import json
import os
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any

INPUTS = {
    "combined_io": ("route_in.def.gz", "route_in.v.gz"),
    "cts": ("cts_in.def.gz", "cts_in.v.gz"),
    "def_round_trip": ("route_in.def.gz",),
    "drc": ("route_in.def.gz", "route_in.v.gz"),
    "floorplan": ("floorplan_in.v.gz",),
    "harden": ("harden_in.def.gz", "harden_in.v.gz"),
    "lvs": ("harden_in.def.gz", "harden_in.v.gz"),
    "rcx": ("route_in.def.gz", "route_in.v.gz"),
    "routing": ("route_in.def.gz",),
    "sta": ("route_in.def.gz", "route_in.v.gz"),
    "verilog_round_trip": ("floorplan_in.v.gz",),
}


@dataclass(frozen=True)
class TestRoots:
    repo_root: Path
    fixture_root: Path
    pdk_root: Path
    artifact_root: Path

    @classmethod
    def from_environment(cls) -> "TestRoots":
        repo_root = _required_directory("ECC_TOOLS_TEST_REPO_ROOT")
        pdk_root = _required_directory("ECC_TOOLS_TEST_PDK_ROOT")
        artifact_root = Path(_required_value("ECC_TOOLS_TEST_ARTIFACT_ROOT"))
        artifact_root.mkdir(parents=True, exist_ok=True)
        fixture_root = repo_root / "test" / "fixtures" / "gcd"
        if not fixture_root.is_dir():
            raise ValueError(f"GCD fixture directory does not exist: {fixture_root}")
        return cls(
            repo_root=repo_root,
            fixture_root=fixture_root,
            pdk_root=pdk_root,
            artifact_root=artifact_root,
        )


@dataclass(frozen=True)
class ScenarioResult:
    name: str
    work_dir: Path
    payload: dict[str, Any]

    def output_path(self, name: str) -> Path:
        return Path(self.payload["outputs"][name])


def run_scenario(
    roots: TestRoots,
    name: str,
    *,
    timeout: int,
    input_overrides: dict[str, Path] | None = None,
) -> ScenarioResult:
    work_dir = Path(tempfile.mkdtemp(prefix=f"{name}-", dir=roots.artifact_root))
    manifest_path = _prepare_manifest(roots, name, work_dir, input_overrides or {})
    log_path = work_dir / "native.log"
    environment = os.environ.copy()
    environment.setdefault("ECC_LOGGER_THROW_ON_ERROR", "1")
    environment.setdefault("OMP_NUM_THREADS", "2")
    command = [
        sys.executable,
        str(roots.repo_root / "test" / "native_scenarios.py"),
        str(manifest_path),
    ]
    try:
        completed = subprocess.run(
            command,
            cwd=roots.repo_root / "test",
            env=environment,
            text=True,
            capture_output=True,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired as error:
        _write_log(log_path, error.stdout or "", error.stderr or "")
        raise AssertionError(
            f"{name} timed out after {timeout}s; log: {log_path}"
        ) from error

    _write_log(log_path, completed.stdout, completed.stderr)
    result_path = work_dir / "result.json"
    if completed.returncode != 0 or not result_path.is_file():
        raise AssertionError(
            f"{name} failed with exit code {completed.returncode}; log tail:\n{_log_tail(log_path)}"
        )
    payload = json.loads(result_path.read_text(encoding="utf-8"))
    return ScenarioResult(name=name, work_dir=work_dir, payload=payload)


def assert_nonempty_file(path: Path) -> None:
    assert path.is_file(), f"expected file does not exist: {path}"
    assert path.stat().st_size > 0, f"expected file is empty: {path}"


def _prepare_manifest(
    roots: TestRoots,
    name: str,
    work_dir: Path,
    input_overrides: dict[str, Path],
) -> Path:
    input_dir = work_dir / "input"
    output_dir = work_dir / "output"
    config_dir = work_dir / "config"
    input_dir.mkdir()
    output_dir.mkdir()
    config_dir.mkdir()

    macro_locations = work_dir / "macro_locations.txt"
    macro_locations.write_text("# gcd has no macros\n", encoding="utf-8")
    config_paths = {
        name: _render_config(roots, config_dir, name, output_dir, macro_locations)
        for name in (
            "cts_ecc.json",
            "db_ecc.json",
            "drc_ecc.json",
            "floorplan_ecc.json",
            "rcx_ecc.json",
            "route_ecc.json",
        )
    }
    inputs = _materialize_inputs(roots, name, input_dir, input_overrides)
    pdk = _pdk_paths(roots)
    manifest = {
        "config": {
            key.removesuffix(".json"): str(value) for key, value in config_paths.items()
        },
        "inputs": {key: str(value) for key, value in inputs.items()},
        "name": name,
        "output_dir": str(output_dir),
        "pdk": {key: _stringify_paths(value) for key, value in pdk.items()},
        "result_path": str(work_dir / "result.json"),
        "work_dir": str(work_dir),
    }
    manifest_path = work_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    return manifest_path


def _materialize_inputs(
    roots: TestRoots,
    name: str,
    input_dir: Path,
    input_overrides: dict[str, Path],
) -> dict[str, Path]:
    if name in {"def_verify", "verilog_verify"}:
        key = "def" if name == "def_verify" else "verilog"
        source = input_overrides[key]
        assert_nonempty_file(source)
        return {key: source}

    inputs: dict[str, Path] = {}
    for source_name in INPUTS[name]:
        source_path = roots.fixture_root / "input" / source_name
        destination = input_dir / source_name.removesuffix(".gz")
        with gzip.open(source_path, "rb") as source, destination.open("wb") as target:
            target.write(source.read())
        key = "def" if destination.suffix == ".def" else "verilog"
        inputs[key] = destination
    return inputs


def _render_config(
    roots: TestRoots,
    config_dir: Path,
    name: str,
    output_dir: Path,
    macro_locations: Path,
) -> Path:
    source_path = roots.fixture_root / "config" / name
    payload = json.loads(source_path.read_text(encoding="utf-8"))
    rendered = _rewrite_value(payload, roots, output_dir, macro_locations)
    destination = config_dir / name
    destination.write_text(json.dumps(rendered, indent=2), encoding="utf-8")
    return destination


def _rewrite_value(
    value: Any,
    roots: TestRoots,
    output_dir: Path,
    macro_locations: Path,
    key: str = "",
) -> Any:
    if isinstance(value, dict):
        return {
            item_key: _rewrite_value(item, roots, output_dir, macro_locations, item_key)
            for item_key, item in value.items()
        }
    if isinstance(value, list):
        return [
            _rewrite_value(item, roots, output_dir, macro_locations, key)
            for item in value
        ]
    if key in {"thread_num", "thread_number", "-thread_number"}:
        return "2" if isinstance(value, str) else 2
    if not isinstance(value, str):
        return value
    if value.startswith("./icsprout55-pdk/"):
        return str(roots.pdk_root / value.removeprefix("./icsprout55-pdk/"))
    if value == "./fixtures/gcd/config/gcd.sdc":
        return str(roots.fixture_root / "config" / "gcd.sdc")
    if value == "./output":
        return str(output_dir)
    if value == "macro_locations.txt":
        return str(macro_locations)
    return value


def _pdk_paths(roots: TestRoots) -> dict[str, Path | list[Path]]:
    db_config = json.loads(
        (roots.fixture_root / "config" / "db_ecc.json").read_text(encoding="utf-8")
    )
    sta_config = json.loads(
        (roots.fixture_root / "config" / "sta_ecc.json").read_text(encoding="utf-8")
    )
    typical = next(item for item in sta_config["liberty"] if item["corner"] == "TYP")
    paths: dict[str, Path | list[Path]] = {
        "default_libs": [
            _pdk_path(roots, path) for path in db_config["INPUT"]["lib_path"]
        ],
        "lefs": [_pdk_path(roots, path) for path in db_config["INPUT"]["lef_paths"]],
        "sdc": roots.fixture_root / "config" / "gcd.sdc",
        "spef": roots.fixture_root / "spef" / "gcd_TYPICAL_25C.spef",
        "tech_lef": _pdk_path(roots, db_config["INPUT"]["tech_lef_path"]),
        "typical_libs": [_pdk_path(roots, path) for path in typical["path"]],
    }
    for value in paths.values():
        for path in value if isinstance(value, list) else [value]:
            assert_nonempty_file(path)
    return paths


def _pdk_path(roots: TestRoots, value: str) -> Path:
    prefix = "./icsprout55-pdk/"
    if not value.startswith(prefix):
        raise ValueError(f"expected PDK-relative path, got: {value}")
    return roots.pdk_root / value.removeprefix(prefix)


def _stringify_paths(value: Path | list[Path]) -> str | list[str]:
    if isinstance(value, list):
        return [str(path) for path in value]
    return str(value)


def _required_value(name: str) -> str:
    value = os.environ.get(name, "")
    if not value:
        raise ValueError(f"{name} must be set")
    return value


def _required_directory(name: str) -> Path:
    path = Path(_required_value(name)).resolve()
    if not path.is_dir():
        raise ValueError(f"{name} is not a directory: {path}")
    return path


def _write_log(path: Path, stdout: str, stderr: str) -> None:
    path.write_text(
        f"--- stdout ---\n{stdout}\n--- stderr ---\n{stderr}", encoding="utf-8"
    )


def _log_tail(path: Path) -> str:
    return "\n".join(
        path.read_text(encoding="utf-8", errors="replace").splitlines()[-100:]
    )
