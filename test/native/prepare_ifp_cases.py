#!/usr/bin/env python3
# SPDX-License-Identifier: MulanPSL-2.0
"""Prepare local real iFP cases without changing any source design or PDK."""

import argparse
import copy
import gzip
import hashlib
import json
from pathlib import Path
import re


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n")


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--threads", type=int, default=2)
    args = parser.parse_args()
    workspace = args.workspace.resolve()
    repo = workspace / "ecc-tools"
    output = args.output_root.resolve()
    output.mkdir(parents=True, exist_ok=False)
    pdk = workspace / "reference/icsprout55-pdk"
    ics_lefs = [pdk / "prtech/techLEF/N551P6M_ecos.lef"] + [
        pdk / f"IP/STD_cell/ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7C{s}/lef/ics55_LLSC_H7C{s}_ecos.lef"
        for s in ("R", "L")
    ]
    ng = workspace / "OpenROAD/test/Nangate45"
    ng_lefs = [ng / "Nangate45_tech.lef", ng / "Nangate45_stdcell.lef"]
    ics_config = json.loads((repo / "test/fixtures/gcd/config/floorplan_ecc.json").read_text())
    ng_config = json.loads((repo / "test/fixtures/gcd/config/floorplan_nangate45.json").read_text())
    cases = [
        ("gcd-ics55", repo / "test/fixtures/gcd/input/route_in.def.gz", ics_lefs, ics_config),
        ("gcd-nangate45", workspace / "OpenROAD/test/gcd_nangate45.def", ng_lefs, ng_config),
        ("tinyrocket-nangate45", workspace / "reference/tinyrocket-flow-20261009/openroad-output/placed.def",
         ng_lefs + [ng / "fakeram45_64x32.lef"], ng_config),
        ("rocket-rv64-ics55", repo / "artifacts/rocket-rv64-ics55-20261011/placed.def", ics_lefs, ics_config),
    ]
    index = []
    for name, source, lefs, template in cases:
        for path in [source, *lefs]:
            if not path.is_file():
                raise FileNotFoundError(path)
        directory = output / name
        directory.mkdir()
        text = gzip.decompress(source.read_bytes()).decode() if source.suffix == ".gz" else source.read_text()
        if source.suffix == ".gz":
            actual_def = directory / "input.def"
            actual_def.write_text(text)
        else:
            actual_def = source
        dbu = int(re.search(r"UNITS DISTANCE MICRONS\s+(\d+)", text)[1])
        die = re.search(r"DIEAREA\s*\(\s*(-?\d+)\s+(-?\d+)\s*\)\s*\(\s*(-?\d+)\s+(-?\d+)\s*\)", text)
        x0, y0, x1, y1 = map(int, die.groups())
        if (x0, y0) != (0, 0):
            raise ValueError("iFP die_size cases must have zero-origin input die")
        config = copy.deepcopy(template)
        config["ifp"]["thread_number"] = args.threads
        config["ifp"]["temp_directory_path"] = "work"
        config["io_placer"].setdefault("mode", "auto")
        config["macro_placer"].setdefault("file_path", "")
        config["die_builder"]["mode"] = "die_size"
        config["die_builder"]["die_size"] = {"width_micron": x1 / dbu, "height_micron": y1 / dbu}
        if name == "gcd-nangate45":
            # The original 32.74um die is too small for the Tiny's 10um margins.
            config["die_builder"]["margin"] = dict.fromkeys(
                ("left_micron", "right_micron", "top_micron", "bottom_micron"), 1.4)
            for stripe in config["pdn_generator"]["stripe"]:
                stripe["pitch_micron"] = 10.0
                stripe["offset_micron"] = 1.4
        write_json(directory / "floorplan.json", config)
        manifest = {"case": name, "lef_files": list(map(str, lefs)), "def_file": str(actual_def),
                    "config": str(directory / "floorplan.json"), "binary_checkpoint": True}
        write_json(directory / "manifest.json", manifest)
        counts = {}
        for section in ("COMPONENTS", "PINS", "NETS", "SPECIALNETS"):
            match = re.search(rf"(?m)^\s*{section}\s+(\d+)\s*;", text)
            counts[section] = int(match[1]) if match else 0
        index.append({"case": name, "manifest": str(directory / "manifest.json"), "counts": counts,
                      "input": {str(path): sha256(path) for path in [source, *lefs]},
                      "def_sha256": sha256(actual_def), "config_sha256": sha256(directory / "floorplan.json"),
                      "threads": args.threads, "config_policy": "same-backend configuration; preserve original die size"})
    write_json(output / "cases.json", index)
    print(json.dumps(index, indent=2))


if __name__ == "__main__":
    main()
