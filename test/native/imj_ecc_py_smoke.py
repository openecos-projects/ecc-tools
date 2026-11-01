# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2026 Peng Cheng Laboratory
"""Native EccDB iMJ smoke for ecc_py.

Run from the repository root after building ecc_py with the native EccDB targets:

  PYTHONPATH=bin python3 test/native/imj_ecc_py_smoke.py --out build/imj-ecc-py-smoke

The script deliberately uses small source-controlled LEF/DEF controls.  It
does not hash files, launch Docker, or depend on OpenDB/GDS support.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path

import ecc_py


ROOT = Path(__file__).resolve().parents[2]
ANTENNA = ROOT / "test/native/fixtures/imj-antenna"
EDITING = ROOT / "test/native/fixtures/imj-ecc-py"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def open_design(lefs: list[Path], design: Path):
    return ecc_py.eccdb.open_lef_def(lefs, design)


def output_text(database, path: Path) -> str:
    database.write_def(path)
    return path.read_text()


def section_count(text: str, name: str) -> int:
    match = re.search(rf"(?m)^{re.escape(name)}\s+(\d+)\s*;", text)
    return int(match.group(1)) if match else 0


def run_antenna(output: Path) -> None:
    expected = {"pass": 0, "violation": 1, "clock-violation": 1, "patch-violation": 1}
    for case, expected_count in expected.items():
        report = output / f"antenna-{case}.report"
        database = open_design([ANTENNA / "tech.lef", ANTENNA / "gate.lef"], ANTENNA / f"{case}.def")
        before = database.info_json()
        ecc_py.eccdb.run_mj(
            database,
            options={
                "operation": "antenna",
                "temp_directory_path": str(output / f"antenna-{case}-tmp"),
                "antenna_report_file": str(report),
            },
        )
        result = report.read_text()
        require("checked=1 coverage_complete=1" in result, f"{case}: incomplete antenna coverage\n{result}")
        match = re.search(r"violation_count=(\d+)", result)
        require(match is not None, f"{case}: no antenna violation count\n{result}")
        require(int(match.group(1)) == expected_count, f"{case}: unexpected antenna result\n{result}")
        require(database.info_json() == before, f"{case}: antenna check mutated the design")


def run_filler(output: Path) -> None:
    lefs = [EDITING / "tech.lef", EDITING / "cells.lef"]
    database = open_design(lefs, EDITING / "design.def")
    before = output_text(database, output / "filler-before.def")
    before_components = section_count(before, "COMPONENTS")
    ecc_py.eccdb.run_mj(
        database,
        options={
            "operation": "filler",
            "filler": "FILL2 FILL1",
            "min_filler_width": 1,
            "temp_directory_path": str(output / "filler-tmp"),
        },
    )
    after_path = output / "filler-after.def"
    after = output_text(database, after_path)
    require(section_count(after, "COMPONENTS") > before_components, "filler did not add a component")
    require(re.search(r"(?m)^-\s+\S+\s+FILL[12]\b", after) is not None and "+ SOURCE DIST" in after,
            "filler output has no distribution-sourced FILL1/FILL2 component")
    reread = open_design(lefs, after_path)
    reread_text = output_text(reread, output / "filler-reread.def")
    require(section_count(reread_text, "COMPONENTS") == section_count(after, "COMPONENTS"),
            "filler DEF did not round-trip its component count")


def run_metal(output: Path) -> None:
    lefs = [EDITING / "tech.lef", EDITING / "cells.lef"]
    database = open_design(lefs, EDITING / "design.def")
    before = output_text(database, output / "metal-before.def")
    require(section_count(before, "FILLS") == 0, "metal source fixture unexpectedly has FILLS")
    ecc_py.eccdb.run_mj(
        database,
        options={
            "operation": "metal",
            "min_fill_layer": "M1",
            "max_fill_layer": "M1",
            "temp_directory_path": str(output / "metal-tmp"),
        },
    )
    after_path = output / "metal-after.def"
    after = output_text(database, after_path)
    require(section_count(after, "FILLS") > 0, "metal fill did not write any FILLS")
    require(re.search(r"(?m)^- LAYER M1(?:\s|$)", after) is not None, "metal fill did not write M1 FILLS")
    reread = open_design(lefs, after_path)
    reread_text = output_text(reread, output / "metal-reread.def")
    require(section_count(reread_text, "FILLS") == section_count(after, "FILLS"),
            "metal DEF did not round-trip its FILL count")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True, help="New output directory; it must not already exist")
    args = parser.parse_args()
    output = args.out.resolve()
    require(not output.exists(), f"output directory already exists: {output}")
    output.mkdir(parents=True)
    require(hasattr(ecc_py, "eccdb"), "ecc_py was built without the native EccDB scripting entry")
    require("mj" in ecc_py.eccdb.supported_tools(), "native EccDB scripting does not expose iMJ")
    run_antenna(output)
    run_filler(output)
    run_metal(output)
    print("iMJ ecc_py smoke passed: antenna, filler, metal")


if __name__ == "__main__":
    main()
