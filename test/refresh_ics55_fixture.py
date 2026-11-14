"""Regenerate native API inputs from an ECC GCD workspace completed with PDK v2."""

import argparse
import gzip
import hashlib
import json
from pathlib import Path
import shutil


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("workspace", type=Path)
    parser.add_argument("--pdk-root", type=Path, required=True)
    args = parser.parse_args()
    workspace = args.workspace.resolve()
    pdk_root = args.pdk_root.resolve()
    fixture = Path(__file__).parent / "fixtures/gcd"
    flow = json.loads((workspace / "home/flow.json").read_text())
    if not all(step["state"] == "Success" for step in flow["steps"]):
        raise ValueError("Regeneration requires a successfully completed workspace")
    db = json.loads((workspace / "config/db_ecc.json").read_text())
    if not any("ICsprout55_9TSVT_basic.lef" in path for path in db["INPUT"]["lef_paths"]):
        raise ValueError("Workspace must use ICS55 v2")
    inputs = {
        "floorplan_in.v.gz": "Synthesis_yosys/output/gcd_Synthesis.v.gz",
        "cts_in.def.gz": "diff_sizing_dreamplace/output/gcd_diff_sizing.def.gz",
        "cts_in.v.gz": "diff_sizing_dreamplace/output/gcd_diff_sizing.v.gz",
        "route_in.def.gz": "timing_optimization_sizer/output/gcd_timing_optimization.def.gz",
        "route_in.v.gz": "timing_optimization_sizer/output/gcd_timing_optimization.v.gz",
        "harden_in.def.gz": "filler_ecc/output/gcd_filler.def.gz",
        "harden_in.v.gz": "filler_ecc/output/gcd_filler.v.gz",
    }
    for name, relative in inputs.items():
        data = gzip.decompress((workspace / relative).read_bytes())
        if b"H7L" in data or b"H7R" in data or b"H7H" in data:
            raise ValueError(f"Legacy cells in {relative}")
        (fixture / "input" / name).write_bytes(gzip.compress(data, mtime=0))
    shutil.copyfile(
        workspace / "RCX_ecc/output/gcd_TYPICAL_25C.spef", fixture / "spef/gcd_TYPICAL_25C.spef"
    )
    implant = next(
        Path(path)
        for path in db["INPUT"]["lef_paths"]
        if Path(path).name == "ics55_v2_implants.lef"
    )
    shutil.copyfile(implant, fixture / "config/ics55_v2_implants.lef")
    shutil.copyfile(pdk_root / "techfile/icsprout55.layermap", fixture / "config/gcd.gds.map")

    def portable(value):
        if isinstance(value, dict):
            return {key: portable(item) for key, item in value.items()}
        if isinstance(value, list):
            return [portable(item) for item in value]
        if not isinstance(value, str) or not Path(value).is_absolute():
            return value
        path = Path(value)
        if path.is_relative_to(pdk_root):
            return "./icsprout55-pdk/" + str(path.relative_to(pdk_root))
        if path == implant:
            return "./fixtures/gcd/config/ics55_v2_implants.lef"
        return "./output"

    configs = ["cts", "db", "drc", "floorplan", "rcx", "route", "sta"]
    for name in configs:
        payload = portable(json.loads((workspace / f"config/{name}_ecc.json").read_text()))
        if name == "db":
            payload["INPUT"].update(
                def_path="", verilog_path="", sdc_path="./fixtures/gcd/config/gcd.sdc", spef=""
            )
        if name == "floorplan":
            payload["macro_placer"]["file_path"] = "macro_locations.txt"
        (fixture / f"config/{name}_ecc.json").write_text(json.dumps(payload, indent=4) + "\n")
    hashes = {
        str(path.relative_to(fixture)): hashlib.sha256(path.read_bytes()).hexdigest()
        for directory in ("input", "spef", "config")
        for path in sorted((fixture / directory).iterdir())
        if path.is_file()
    }
    (fixture / "provenance.json").write_text(
        json.dumps(
            {
                "pdk_release": "v2-2026.10.28",
                "workspace": workspace.name,
                "source_outputs": inputs,
                "sha256": hashes,
                "note": "Generated from completed ECC flow; GCD SDC retains the native API test's 50 MHz unconstrained-I/O contract.",
            },
            indent=2,
        )
        + "\n"
    )


if __name__ == "__main__":
    main()
