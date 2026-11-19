"""Regenerate native API inputs from an ECC GCD workspace for one ICS55 variant."""

import argparse
import gzip
import hashlib
import json
import shutil
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("workspace", type=Path)
    parser.add_argument("--pdk-root", type=Path, required=True)
    parser.add_argument("--variant", choices=("v1", "v2"), required=True)
    args = parser.parse_args()
    workspace = args.workspace.resolve()
    pdk_root = args.pdk_root.resolve()
    fixture = Path(__file__).parent / "fixtures/gcd" / args.variant
    flow = json.loads((workspace / "home/flow.json").read_text())
    if not all(step["state"] == "Success" for step in flow["steps"]):
        raise ValueError("Regeneration requires a successfully completed workspace")
    db = json.loads((workspace / "config/db_ecc.json").read_text())
    workspace_pdk_root = Path(db["INPUT"]["tech_lef_path"]).resolve().parents[2]
    variant_marker = "ICsprout55_9TSVT_basic.lef" if args.variant == "v2" else "ics55_LLSC_H7CR_ecos.lef"
    if not any(variant_marker in path for path in db["INPUT"]["lef_paths"]):
        raise ValueError(f"Workspace does not use ICS55 {args.variant}")
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
        if args.variant == "v2" and any(token in data for token in (b"H7L", b"H7R", b"H7H")):
            raise ValueError(f"Legacy cells in {relative}")
        (fixture / "input" / name).write_bytes(gzip.compress(data, mtime=0))
    shutil.copyfile(
        workspace / "RCX_ecc/output/gcd_TYPICAL_25C.spef", fixture / "spef/gcd_TYPICAL_25C.spef"
    )
    shutil.copyfile(pdk_root / "techfile/icsprout55.layermap", fixture / "config/gcd.gds.map")
    catalog = workspace / "config/cell_catalog.tsv"
    if catalog.is_file():
        shutil.copyfile(catalog, fixture / "config/cell_catalog.tsv")
    else:
        description = json.loads((pdk_root / "ecc-pdk.json").read_text(encoding="utf-8"))
        with (fixture / "config/cell_catalog.tsv").open("w", encoding="utf-8") as stream:
            for entry in description["cell_catalog"]:
                stream.write(
                    f"{entry['master']}\t{entry['family']}\t{entry['vt']}\t{entry['drive_rank']}\n"
                )

    if args.variant == "v1":
        profile = {
            "variant": "v1",
            "site": "core7",
            "sample_cell": "ADDFX1H7R",
            "buffer_low": "BUFX7H7L",
            "buffer_regular": "BUFX7H7R",
            "buffer_large": "BUFX8H7L",
            "buffer_larger": "BUFX12H7L",
            "mux": "MUX2X0P5H7R",
        }
    else:
        profile = {
            "variant": "v2",
            "site": "SC9T_site",
            "sample_cell": "ADDFX1_9TSVT",
            "buffer_low": "BUFX6_9TLVT",
            "buffer_regular": "BUFX6_9TSVT",
            "buffer_large": "BUFX8_9TLVT",
            "buffer_larger": "BUFX12_9TLVT",
            "mux": "MX2X0P5_9TSVT",
        }
    (fixture / "profile.json").write_text(json.dumps(profile, indent=2) + "\n")

    def portable(value):
        if isinstance(value, dict):
            return {key: portable(item) for key, item in value.items()}
        if isinstance(value, list):
            return [portable(item) for item in value]
        if not isinstance(value, str) or not Path(value).is_absolute():
            return value
        path = Path(value)
        if path.is_relative_to(workspace_pdk_root):
            return "./icsprout55-pdk/" + str(path.relative_to(workspace_pdk_root))
        if path == workspace / "config/cell_catalog.tsv":
            return "./fixtures/gcd/config/cell_catalog.tsv"
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
        if name == "sta":
            payload.setdefault("STA", {})["-cell_catalog"] = "./fixtures/gcd/config/cell_catalog.tsv"
        (fixture / f"config/{name}_ecc.json").write_text(json.dumps(payload, indent=4) + "\n")
    if args.variant == "v1":
        shutil.copyfile(
            Path(__file__).parent / "fixtures/gcd/v2/config/gcd.sdc",
            fixture / "config/gcd.sdc",
        )
    hashes = {
        str(path.relative_to(fixture)): hashlib.sha256(path.read_bytes()).hexdigest()
        for directory in ("input", "spef", "config")
        for path in sorted((fixture / directory).iterdir())
        if path.is_file()
    }
    (fixture / "provenance.json").write_text(
        json.dumps(
            {
                "pdk_release": "v1.10.103-r2" if args.variant == "v1" else "v2-2026.10.28-r1",
                "technology_lef_sha256": hashlib.sha256(
                    (pdk_root / "prtech/techLEF/N551P6M_ecos.lef").read_bytes()
                ).hexdigest(),
                "workspace": workspace.name,
                "source_outputs": inputs,
                "sha256": hashes,
                "note": (
                    f"Generated from completed ECC {args.variant} flow; GCD SDC retains the native API test's "
                    "50 MHz unconstrained-I/O contract."
                ),
            },
            indent=2,
        )
        + "\n"
    )


if __name__ == "__main__":
    main()
