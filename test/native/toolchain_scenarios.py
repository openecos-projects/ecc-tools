# SPDX-License-Identifier: MulanPSL-2.0
"""Prepare real native tool stages using the existing GCD fixtures.

This harness never initializes physical iDB and never calls the legacy Python
scenarios. Missing extraction technology must be supplied explicitly; an old
reference SPEF is deliberately not substituted for a fresh RCX result.
"""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from support import TestRoots, _prepare_manifest  # noqa: E402


def prepare(roots: TestRoots, stages: list[str], rcx_config: Path | None = None) -> Path:
    work = Path(tempfile.mkdtemp(prefix="eccdb-flow-", dir=roots.artifact_root))
    scenario = "cts" if "cts" in stages else "routing"
    base_path = _prepare_manifest(roots, scenario, work, {})
    base = json.loads(base_path.read_text())
    pdk = base["pdk"]
    if "floorplan" in stages:
        # This fixture is already placed. Rebuilding a utilization-based,
        # smaller die would invalidate placement before the routing stage.
        text = Path(base["inputs"]["def"]).read_text()
        dbu = int(re.search(r"UNITS DISTANCE MICRONS\s+(\d+)", text)[1])
        die = re.search(r"DIEAREA\s*\(\s*(\d+)\s+(\d+)\s*\)\s*\(\s*(\d+)\s+(\d+)\s*\)", text)
        floorplan_config = Path(base["config"]["floorplan_ecc"])
        settings = json.loads(floorplan_config.read_text())
        settings["die_builder"]["mode"] = "die_size"
        settings["die_builder"]["die_size"] = {
            "width_micron": (int(die[3]) - int(die[1])) / dbu,
            "height_micron": (int(die[4]) - int(die[2])) / dbu,
        }
        floorplan_config.write_text(json.dumps(settings, indent=2) + "\n")
    flow_stages = []
    for stage in stages:
        item = {"name": stage, "options": {"-thread_number": 2}}
        if stage in {"floorplan", "cts", "routing", "drc"}:
            name = "route" if stage == "routing" else stage
            item["config"] = base["config"][name + "_ecc"]
        elif stage == "rcx":
            if rcx_config is None:
                raise ValueError("Native RCX requires --rcx-config with actual mapping/ITF/captab inputs")
            item["config"] = str(rcx_config.resolve())
        elif stage == "sta":
            item["options"].update({"-max_paths": 20, "-output_timing_reports": 1})
        flow_stages.append(item)
    manifest = {
        "backend": "eccdb",
        "lef_files": [pdk["tech_lef"], *pdk["lefs"]],
        "def_file": base["inputs"]["def"],
        "liberty_files": pdk["typical_libs"],
        "sdc_file": pdk["sdc"],
        "output_directory": str(work / "native-output"),
        "binary_checkpoints": True,
        "stages": flow_stages,
    }
    manifest_path = work / "native-manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest_path


def execute(binary: Path, manifest: Path, timeout: int = 480) -> dict:
    with (manifest.parent / "native.log").open("w") as log:
        completed = subprocess.run(
            [str(binary.resolve()), str(manifest.resolve())],
            cwd=manifest.parent,
            env={**os.environ, "ECC_LOGGER_THROW_ON_ERROR": "1", "OMP_NUM_THREADS": "2"},
            stdout=log,
            stderr=subprocess.STDOUT,
            timeout=timeout,
            check=False,
        )
    report_path = manifest.parent / "native-output" / "result.json"
    if completed.returncode or not report_path.is_file():
        raise RuntimeError(f"Native flow exit={completed.returncode}; artifacts: {manifest.parent}")
    report = json.loads(report_path.read_text())
    if report["status"] != "passed" or report["backend"] != "eccdb":
        raise AssertionError(f"Incomplete native flow: {report_path}")
    plan = json.loads(manifest.read_text())
    previous = report["input"]
    for index, (request, result) in enumerate(zip(plan["stages"], report["stages"], strict=True), 1):
        assert result["stage"] == request["name"]
        assert result["backend"] == "eccdb"
        assert result["before"] == previous, "Stages did not consume the preceding EccDB state"
        directory = report_path.parent / f"{index}-{request['name']}"
        assert (directory / "output.def").stat().st_size > 0
        if plan.get("binary_checkpoints"):
            for name in ("technology.edb", "library.edb", "design.edb"):
                assert (directory / name).stat().st_size > 0
        if request["name"] == "cts":
            assert result["after"]["instances"] > result["before"]["instances"], "GCD CTS inserted no buffers"
        if request["name"] == "routing":
            assert result["after"]["wires"] > 0, "Routing did not write routes to EccDB"
        if request["name"] == "sta" and result.get("timing_spef"):
            assert result["spef_design_revision"] == result["before"]["revision"]
        previous = result["after"]
    return report


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--stages", nargs="+", default=["routing", "drc"])
    parser.add_argument("--rcx-config", type=Path)
    parser.add_argument("--prepare-only", action="store_true")
    arguments = parser.parse_args()
    roots = TestRoots.from_environment()
    manifest = prepare(roots, arguments.stages, arguments.rcx_config)
    print(manifest, flush=True)
    if not arguments.prepare_only:
        print(json.dumps(execute(arguments.binary, manifest), indent=2))


if __name__ == "__main__":
    main()
