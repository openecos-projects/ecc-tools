# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2026 Peng Cheng Laboratory
"""Native EccDB iLVS smoke for ``ecc_py``.

Run from the repository root after building the EccDB Python entry:

  PYTHONPATH=bin python3 test/native/ilvs_ecc_py_smoke.py --out build/ilvs-ecc-py-smoke

The physical DEF and reference Verilog are deliberately separate inputs. This
script does not hash files, launch Docker, or use a large PDK case.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Callable

import ecc_py


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "test/native/fixtures/ilvs-ecc-py"
LEFS = [FIXTURE / "tech.lef", FIXTURE / "cells.lef"]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def open_physical():
    return ecc_py.eccdb.open_lef_def(LEFS, FIXTURE / "design.def")


def write_def(database: Any, output: Path) -> str:
    database.write_def(output)
    return output.read_text(encoding="utf-8")


def read_report(output: Path) -> dict[str, Any]:
    report = output / "lvs_reporter" / "ilvs.json"
    text_report = output / "lvs_reporter" / "ilvs.rpt"
    require(report.is_file(), f"iLVS did not write {report}")
    require(text_report.is_file() and text_report.stat().st_size > 0, f"iLVS did not write {text_report}")
    return json.loads(report.read_text(encoding="utf-8"))


def entity_difference_count(report: dict[str, Any]) -> int:
    return sum(int(row["difference"]) for row in report["entity"])


def routing_row(report: dict[str, Any]) -> dict[str, Any]:
    for row in report["connectivity"]:
        if row["connectivity"] == "Routing":
            return row
    raise AssertionError("iLVS report lacks the Routing summary")


def require_clean(report: dict[str, Any], context: str) -> None:
    require(entity_difference_count(report) == 0, f"{context}: unexpected entity difference: {report['entity']}")
    routing = routing_row(report)
    require(routing["total"] == 3, f"{context}: expected three routed nets: {routing}")
    require(routing["open"]["count"] == 0 and routing["short"]["count"] == 0, f"{context}: route is not clean: {routing}")
    require(not report["violations"], f"{context}: unexpected violations: {report['violations']}")


def lvs_options(reference: Path, output: Path, *, top_module: str = "top") -> dict[str, Any]:
    return {
        "reference_verilog": str(reference),
        "top_module": top_module,
        "temp_directory_path": str(output),
        "thread_number": 2,
    }


def expect_error(action: Callable[[], None], context: str, expected_text: str = "") -> None:
    try:
        action()
    except Exception as error:
        if expected_text:
            require(expected_text in str(error), f"{context}: unexpected error: {error}")
        return
    raise AssertionError(f"{context}: expected an exception")


def run_native_stage_failure(output: Path) -> None:
    diagonal_def = output / "diagonal.def"
    source = (FIXTURE / "design.def").read_text(encoding="utf-8")
    original = "( 1950 500 ) ( 2050 500 )"
    require(original in source, "fixture no longer has the route selected for the native failure check")
    diagonal_def.write_text(source.replace(original, "( 1950 500 ) ( 2050 550 )", 1), encoding="utf-8")
    diagonal = ecc_py.eccdb.open_lef_def(LEFS, diagonal_def)
    expect_error(
        lambda: ecc_py.eccdb.run_lvs(diagonal, options=lvs_options(FIXTURE / "reference.v", output / "diagonal")),
        "native iLVS Manhattan-route rejection",
        "native iLVS requires Manhattan route segments",
    )


def run_valid_reference(output: Path) -> None:
    # The preceding call initializes iLVS and then fails in the native EccDB
    # wrapper. This successful run must work immediately afterward: abortLVS
    # has to release singleton state and both borrowed pointers.
    database = open_physical()
    before_info = database.info_json()
    before_def = write_def(database, output / "before.def")

    # These are distinct caller-input boundaries. They fail before iLVS owns
    # a native session; the diagonal case above covers failure after initLVS.
    expect_error(
        lambda: ecc_py.eccdb.run_lvs(database, options={"temp_directory_path": str(output / "missing-reference")}),
        "missing reference_verilog",
    )
    expect_error(
        lambda: ecc_py.eccdb.run_lvs(database, options=lvs_options(output / "missing.v", output / "missing-file")),
        "nonexistent reference Verilog",
    )
    expect_error(
        lambda: ecc_py.eccdb.run_lvs(
            database,
            options=lvs_options(FIXTURE / "reference.v", output / "wrong-top", top_module="not_top"),
        ),
        "unknown top module",
    )

    config = output / "lvs-config.json"
    config.write_text(
        json.dumps(
            {
                "lvs": {
                    "reference_verilog": str(FIXTURE / "mismatch.v"),
                    "top_module": "not_top",
                    "temp_directory_path": str(output / "config-must-not-run"),
                    "thread_number": 1,
                }
            }
        ),
        encoding="utf-8",
    )
    # Explicit options take precedence over the file and accept their normal
    # command-style leading dash spelling.
    override = {
        "-reference_verilog": str(FIXTURE / "reference.v"),
        "-top_module": "top",
        "-temp_directory_path": str(output / "valid"),
        "-thread_number": 2,
    }
    ecc_py.eccdb.run_lvs(database, config=config, options=override)
    require_clean(read_report(output / "valid"), "valid direct native entry")
    require(not (output / "config-must-not-run").exists(), "LVS config file values overrode explicit options")
    require(database.info_json() == before_info, "iLVS changed the physical EccDB summary")
    require(write_def(database, output / "after.def") == before_def, "iLVS changed the physical DEF")

    # Verify the generic Database method dispatches the same native stage.
    database.run_tool("lvs", options=lvs_options(FIXTURE / "reference.v", output / "generic"))
    require_clean(read_report(output / "generic"), "generic native entry")

    # A different logical reference must report an entity mismatch. This is
    # specifically a guard against accidentally projecting the physical EccDB
    # as the reference netlist.
    ecc_py.eccdb.run_lvs(database, options=lvs_options(FIXTURE / "mismatch.v", output / "mismatch"))
    mismatch = read_report(output / "mismatch")
    require(entity_difference_count(mismatch) > 0, f"mismatched Verilog was accepted: {mismatch}")
    require(database.info_json() == before_info, "mismatched iLVS run changed the physical EccDB")
    require(write_def(database, output / "mismatch-after.def") == before_def, "mismatched iLVS run changed the physical DEF")


def run_binary_reference(output: Path) -> None:
    database = open_physical()
    snapshots = output / "binary"
    snapshots.mkdir()
    technology = snapshots / "technology.eccdb"
    library = snapshots / "library.eccdb"
    design = snapshots / "design.eccdb"
    database.write_binary(technology, library, design)
    reopened = ecc_py.eccdb.open_binary(technology, library, design)
    ecc_py.eccdb.run_lvs(reopened, options=lvs_options(FIXTURE / "reference.v", output / "binary-lvs"))
    require_clean(read_report(output / "binary-lvs"), "binary-reopened physical design")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True, help="New output directory; it must not already exist")
    args = parser.parse_args()
    output = args.out.resolve()
    require(not output.exists(), f"output directory already exists: {output}")
    output.mkdir(parents=True)
    require(hasattr(ecc_py, "eccdb"), "ecc_py was built without the native EccDB scripting entry")
    require("lvs" in ecc_py.eccdb.supported_tools(), "native EccDB scripting does not expose iLVS")
    run_native_stage_failure(output)
    run_valid_reference(output)
    run_binary_reference(output)
    print("iLVS ecc_py smoke passed: native-stage cleanup, config override, reference isolation, mismatch detection, binary reopen")


if __name__ == "__main__":
    main()
