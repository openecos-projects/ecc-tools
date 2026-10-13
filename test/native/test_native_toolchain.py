# SPDX-License-Identifier: MulanPSL-2.0
"""Real native operation integration; requires the public GCD PDK inputs."""

import json
import os
from pathlib import Path
import subprocess

import pytest

from toolchain_scenarios import execute, prepare


@pytest.fixture(scope="session")
def native_binary() -> Path:
    configured = os.environ.get("ECC_TOOLS_NATIVE_FLOW_BINARY")
    if configured is None:
        pytest.skip("Set ECC_TOOLS_NATIVE_FLOW_BINARY to the built ecc_native_flow")
    binary = Path(configured).resolve()
    assert binary.is_file(), f"Missing native flow binary: {binary}"
    return binary


def test_native_cts_routing_drc_chain(test_roots, native_binary):
    manifest = prepare(test_roots, ["cts", "routing", "drc"])
    report = execute(native_binary, manifest)
    cts, routing, drc = report["stages"]
    assert cts["change_notifications"] > 0
    assert routing["change_notifications"] > 0
    assert drc["before"] == drc["after"], "DRC unexpectedly changed the design"


def test_native_failed_stage_has_diagnostic(test_roots, native_binary):
    manifest = prepare(test_roots, ["routing"])
    plan = json.loads(manifest.read_text())
    plan["liberty_files"] = []
    plan["stages"] = [{"name": "unknown-stage"}]
    manifest.write_text(json.dumps(plan))
    completed = subprocess.run(
        [str(native_binary), str(manifest)], capture_output=True, text=True, timeout=60
    )
    assert completed.returncode == 1
    result = json.loads((Path(plan["output_directory"]) / "result.json").read_text())
    assert result["status"] == "failed"
    assert result["failed_stage"] == "unknown-stage"
    assert result["backend"] == "eccdb"
    assert "Unknown native tool stage" in result["error"]
