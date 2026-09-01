import json
import shutil
import sys
from pathlib import Path
from typing import Any

from ecc_tools_bin import ecc_py


def main() -> None:
    manifest = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    outputs = SCENARIOS[manifest["name"]](manifest)
    Path(manifest["result_path"]).write_text(
        json.dumps(
            {"outputs": {name: str(path) for name, path in outputs.items()}}, indent=2
        ),
        encoding="utf-8",
    )


def _setup(manifest: dict[str, Any]) -> None:
    pdk = manifest["pdk"]
    _require(
        ecc_py.db_init(
            config_path=manifest["config"]["db_ecc"], output_path=manifest["output_dir"]
        ),
        "db_init",
    )
    _require(ecc_py.tech_lef_init(pdk["tech_lef"]), "tech_lef_init")
    _require(ecc_py.lef_init(pdk["lefs"]), "lef_init")


def _read_design(manifest: dict[str, Any], *, lvs_verilog: bool = False) -> None:
    inputs = manifest["inputs"]
    if "def" in inputs:
        _require(ecc_py.def_init(inputs["def"]), "def_init")
    if "verilog" in inputs:
        reader = ecc_py.lvs_verilog_init if lvs_verilog else ecc_py.verilog_init
        _require(
            reader(inputs["verilog"], "gcd"),
            "lvs_verilog_init" if lvs_verilog else "verilog_init",
        )


def def_round_trip(manifest: dict[str, Any]) -> dict[str, Path]:
    _setup(manifest)
    _read_design(manifest)
    output = _output(manifest, "round_trip.def")
    _require(ecc_py.def_save(str(output)), "def_save")
    _require_file(output)
    return {"def": output}


def def_verify(manifest: dict[str, Any]) -> dict[str, Path]:
    _setup(manifest)
    _read_design(manifest)
    return {}


def verilog_round_trip(manifest: dict[str, Any]) -> dict[str, Path]:
    _setup(manifest)
    _read_design(manifest)
    output = _output(manifest, "round_trip.v")
    _require(ecc_py.netlist_save(str(output)), "netlist_save")
    _require_file(output)
    return {"verilog": output}


def verilog_verify(manifest: dict[str, Any]) -> dict[str, Path]:
    _setup(manifest)
    _read_design(manifest)
    return {}


def combined_io(manifest: dict[str, Any]) -> dict[str, Path]:
    _setup(manifest)
    _read_design(manifest)
    def_output = _output(manifest, "combined.def")
    verilog_output = _output(manifest, "combined.v")
    gds_output = _output(manifest, "combined.gds")
    _require(ecc_py.def_save(str(def_output)), "def_save")
    _require(ecc_py.netlist_save(str(verilog_output)), "netlist_save")
    _require(ecc_py.gds_save(str(gds_output)), "gds_save")
    for path in (def_output, verilog_output, gds_output):
        _require_file(path)
    return {"def": def_output, "gds": gds_output, "verilog": verilog_output}


def floorplan(manifest: dict[str, Any]) -> dict[str, Path]:
    _setup(manifest)
    _read_design(manifest)
    _require(ecc_py.init_fp(manifest["config"]["floorplan_ecc"]), "init_fp")
    try:
        _require(ecc_py.run_fp(), "run_fp")
    finally:
        ecc_py.destroy_fp()
    output = _output(manifest, "floorplan.def")
    _require(ecc_py.def_save(str(output)), "def_save")
    _require_file(output)
    return {"def": output}


def cts(manifest: dict[str, Any]) -> dict[str, Path]:
    _setup(manifest)
    _read_design(manifest)
    _load_default_timing_inputs(manifest)
    report_dir = Path(manifest["output_dir"]) / "cts"
    report_dir.mkdir()
    try:
        _require(
            ecc_py.run_cts(manifest["config"]["cts_ecc"], str(report_dir)), "run_cts"
        )
        ecc_py.cts_report(str(report_dir))
    finally:
        ecc_py.destroy_cts()
    output = _output(manifest, "cts.def")
    _require(ecc_py.def_save(str(output)), "def_save")
    _require_file(output)
    return {"def": output, "report_dir": report_dir}


def routing(manifest: dict[str, Any]) -> dict[str, Path]:
    _setup(manifest)
    _read_design(manifest)
    _require(ecc_py.init_rt(manifest["config"]["route_ecc"]), "init_rt")
    try:
        _require(ecc_py.run_rt(), "run_rt")
    finally:
        ecc_py.destroy_rt()
    output = _output(manifest, "routing.def")
    _require(ecc_py.def_save(str(output)), "def_save")
    _require_file(output)
    return {"def": output}


def drc(manifest: dict[str, Any]) -> dict[str, Path]:
    _setup(manifest)
    _read_design(manifest)
    output_dir = Path(manifest["output_dir"])
    _require(ecc_py.init_drc(str(output_dir), 2), "init_drc")
    _require(ecc_py.run_drc(manifest["config"]["drc_ecc"]), "run_drc")
    database = _output(manifest, "drc.bin")
    ecc_py.save_drc(str(database))
    violation_map = output_dir / "violation_map.json"
    output = _output(manifest, "drc.def")
    _require(ecc_py.def_save(str(output)), "def_save")
    for path in (database, violation_map, output):
        _require_file(path)
    return {"database": database, "def": output, "violation_map": violation_map}


def rcx(manifest: dict[str, Any]) -> dict[str, Path]:
    _setup(manifest)
    _read_design(manifest)
    _require(ecc_py.init_rcx(manifest["config"]["rcx_ecc"], pdk="ics55"), "init_rcx")
    try:
        _require(ecc_py.run_rcx(), "run_rcx")
    finally:
        ecc_py.destroy_rcx()
    output = _output(manifest, "rcx.def")
    _require(ecc_py.def_save(str(output)), "def_save")
    spefs = sorted((Path(manifest["output_dir"]) / "spef_writer").glob("gcd_*.spef"))
    if not spefs:
        raise AssertionError("RCX did not produce a SPEF file")
    for path in [output, *spefs]:
        _require_file(path)
    return {"def": output, "spef": spefs[0]}


def sta(manifest: dict[str, Any]) -> dict[str, Path]:
    _setup(manifest)
    _read_design(manifest)
    _load_sta_inputs(manifest)
    output_dir = Path(manifest["output_dir"]) / "sta"
    output_dir.mkdir()
    config = {
        "-max_paths": "20",
        "-output_timing_features": "1",
        "-output_timing_reports": "1",
        "-temp_directory_path": str(output_dir),
        "-thread_number": "2",
        "-timing_path_limit": "20",
    }
    _require(ecc_py.init_sta("", config), "init_sta")
    try:
        _require(ecc_py.run_sta(), "run_sta")
    finally:
        ecc_py.destroy_sta()
    reports = sorted((output_dir / "timing_reporter").glob("*.rpt"))
    if not reports:
        raise AssertionError("STA did not produce a timing report")
    for path in reports:
        _require_file(path)
    return {"report": reports[0]}


def lvs(manifest: dict[str, Any]) -> dict[str, Path]:
    _setup(manifest)
    _read_design(manifest, lvs_verilog=True)
    output_dir = Path(manifest["output_dir"])
    _require(ecc_py.init_lvs(str(output_dir), 2), "init_lvs")
    try:
        _require(ecc_py.run_lvs(), "run_lvs")
    finally:
        ecc_py.destroy_lvs()
    def_output = _output(manifest, "lvs.def")
    _require(ecc_py.def_save(str(def_output)), "def_save")
    report = output_dir / "lvs_reporter" / "ilvs.rpt"
    feature = output_dir / "lvs_reporter" / "ilvs.json"
    for path in (def_output, report, feature):
        _require_file(path)
    return {"def": def_output, "feature": feature, "report": report}


def harden(manifest: dict[str, Any]) -> dict[str, Path]:
    _setup(manifest)
    _read_design(manifest)
    output_dir = Path(manifest["output_dir"])
    abstract_lef = _output(manifest, "gcd.lef")
    hardened_gds = _output(manifest, "gcd.gds")
    hardened_lib = _output(manifest, "gcd.lib")
    sta_dir = output_dir / "sta"
    sta_dir.mkdir()
    _require(ecc_py.write_abstract_lef(str(abstract_lef)), "write_abstract_lef")
    _load_sta_inputs(manifest)
    _require(ecc_py.init_sta("", {"-temp_directory_path": str(sta_dir)}), "init_sta")
    try:
        _require(ecc_py.extract_lib(), "extract_lib")
    finally:
        ecc_py.destroy_sta()
    extracted_lib = sta_dir / "timing_characterizer" / "gcd_max.lib"
    _require_file(extracted_lib)
    shutil.copyfile(extracted_lib, hardened_lib)
    _require(ecc_py.gds_save(str(hardened_gds), True), "gds_save")
    for path in (abstract_lef, hardened_gds, hardened_lib):
        _require_file(path)
    return {"gds": hardened_gds, "lef": abstract_lef, "lib": hardened_lib}


def _load_default_timing_inputs(manifest: dict[str, Any]) -> None:
    pdk = manifest["pdk"]
    _require(ecc_py.lib_init(pdk["default_libs"]), "lib_init")
    _require(ecc_py.sdc_init(pdk["sdc"]), "sdc_init")


def _load_sta_inputs(manifest: dict[str, Any]) -> None:
    pdk = manifest["pdk"]
    _require(ecc_py.lib_init(pdk["typical_libs"]), "lib_init")
    _require(ecc_py.sdc_init(pdk["sdc"]), "sdc_init")
    _require(ecc_py.spef_init(pdk["spef"]), "spef_init")


def _output(manifest: dict[str, Any], name: str) -> Path:
    path = Path(manifest["output_dir"]) / name
    path.parent.mkdir(parents=True, exist_ok=True)
    return path


def _require(value: Any, operation: str) -> None:
    if value is not True:
        raise AssertionError(f"{operation} returned {value!r}")


def _require_file(path: Path) -> None:
    if not path.is_file() or path.stat().st_size == 0:
        raise AssertionError(f"expected non-empty output: {path}")


SCENARIOS = {
    "combined_io": combined_io,
    "cts": cts,
    "def_round_trip": def_round_trip,
    "def_verify": def_verify,
    "drc": drc,
    "floorplan": floorplan,
    "harden": harden,
    "lvs": lvs,
    "rcx": rcx,
    "routing": routing,
    "sta": sta,
    "verilog_round_trip": verilog_round_trip,
    "verilog_verify": verilog_verify,
}


if __name__ == "__main__":
    main()
