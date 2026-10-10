import hashlib
import json
import shutil
import sys
from pathlib import Path
from textwrap import dedent
from typing import Any

from data.timing_defaults import assert_placement_timing_defaults
from data.timing_qualification import assert_endpoint_qualification
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


def antenna(manifest: dict[str, Any]) -> dict[str, Path]:
    _setup(manifest)
    _read_design(manifest)
    output_dir = Path(manifest["output_dir"])
    temp_directory = output_dir / "mj"
    report_file = temp_directory / "antenna_checker" / "antenna_check.rpt"
    _require(
        ecc_py.init_mj("", {"-temp_directory_path": str(temp_directory)}),
        "init_mj",
    )
    _require(
        ecc_py.check_antenna(),
        "check_antenna",
    )
    _require(ecc_py.destroy_mj(), "destroy_mj")
    _require_file(report_file)
    return {"report": report_file}


def def_round_trip(manifest: dict[str, Any]) -> dict[str, Path]:
    _setup(manifest)
    _read_design(manifest)
    output = _output(manifest, "round_trip.def")
    _require(ecc_py.def_save(str(output)), "def_save")
    _require_file(output)
    return {"def": output}


def placement_map(manifest: dict[str, Any]) -> dict[str, Path]:
    _setup(manifest)
    _read_design(manifest)
    feature_dir = _output(manifest, "feature")
    _require(ecc_py.db_init(feature_path=str(feature_dir)), "db_init feature_path")
    output = feature_dir / "placement.json"
    _require(ecc_py.feature_pl_eval(str(output), 5), "feature_pl_eval")
    _require_file(output)
    return {"feature": output}


def def_verify(manifest: dict[str, Any]) -> dict[str, Path]:
    _setup(manifest)
    _read_design(manifest)
    return {}


def pyplacedb_rows(manifest: dict[str, Any]) -> dict[str, Path]:
    _setup(manifest)
    design_def = Path(manifest["work_dir"]) / "cut_rows.def"
    design_def.write_text(
        dedent(
            """
            VERSION 5.8 ;
            DESIGN cut_rows ;
            UNITS DISTANCE MICRONS 1000 ;
            DIEAREA ( 0 0 ) ( 10000 2800 ) ;
            ROW BOTTOM_LEFT core7 0 0 N DO 20 BY 1 STEP 200 0 ;
            ROW BOTTOM_RIGHT core7 6000 0 N DO 20 BY 1 STEP 200 0 ;
            ROW TOP core7 0 1400 FS DO 50 BY 1 STEP 200 0 ;
            COMPONENTS 1 ;
            - movable ADDFX1H7R + UNPLACED ;
            END COMPONENTS
            PINS 0 ;
            END PINS
            NETS 0 ;
            END NETS
            END DESIGN
            """
        ),
        encoding="utf-8",
    )
    _require(ecc_py.def_init(str(design_def)), "def_init")
    place_db = ecc_py.pydb(ecc_py.get_dmInst(), 2, 2, False, False)  # noqa: FBT003
    blockages = [
        [
            place_db.node_x[node_id],
            place_db.node_y[node_id],
            place_db.node_size_x[node_id],
            place_db.node_size_y[node_id],
        ]
        for node_id, name in enumerate(place_db.node_names)
        if name.startswith("blockage")
    ]
    summary = _output(manifest, "pyplacedb_rows.json")
    summary.write_text(
        json.dumps(
            {
                "blockages": blockages,
                "num_terminals": place_db.num_terminals,
                "rows": list(place_db.rows),
            },
            indent=2,
        ),
        encoding="utf-8",
    )
    return {"summary": summary}


def pyplacedb_timing(manifest: dict[str, Any]) -> dict[str, Path]:
    _setup(manifest)
    _read_design(manifest)
    _load_sta_inputs(manifest)
    output_dir = Path(manifest["output_dir"])
    sta_dir = output_dir / "pyplacedb_timing_sta"
    config = {
        "-max_paths": "20",
        "-output_timing_features": "0",
        "-output_timing_reports": "0",
        "-temp_directory_path": str(sta_dir),
        "-thread_number": "2",
        "-timing_path_limit": "20",
    }
    _require(ecc_py.init_sta("", config), "init_sta")
    try:
        _require(ecc_py.run_sta(), "run_sta")
        place_db = ecc_py.pydb(ecc_py.get_dmInst(), 4, 4, False, True)  # noqa: FBT003
        assert_placement_timing_defaults(place_db, manifest)
        assert_endpoint_qualification(place_db)
        num_pins = len(place_db.pin_names)
        for field_name in ("start_points", "end_points", "clock_pins", "endpoint_pin_ids", "start_pin_ids"):
            values = [int(value) for value in getattr(place_db, field_name)]
            if any(value < 0 or value >= num_pins for value in values):
                raise AssertionError(f"{field_name} contains a pin id outside [0, {num_pins})")
        for ids, fields in (
            (place_db.start_points, ("inrdelays", "infdelays", "inrtrans", "inftrans")),
            (place_db.end_points, ("outcaps", "endpoints_rRAT", "endpoints_fRAT")),
        ):
            if any(len(getattr(place_db, field)) != len(ids) for field in fields):
                raise AssertionError(f"timing boundary values must align with pin ids: {fields}")
        for field_name, final_length in (
            ("net_flat_arcs_start", len(place_db.net_flat_arcs)),
            ("inst_flat_arcs_start", len(place_db.inst_flat_arcs)),
            ("flat_inst_arcs_by_level_start", len(place_db.flat_inst_arcs_by_level)),
            ("inst_topo_start", len(place_db.inst_topo_ids)),
            ("flat_pin_to_graph_start", len(place_db.flat_pin_to_graph)),
            ("flat_pin_to_graph_start_reverse", len(place_db.flat_pin_to_graph_reverse)),
            ("pin_pred_start", len(place_db.pin_pred_pin)),
            ("pin_succ_start", len(place_db.pin_succ_pin)),
        ):
            offsets = [int(value) for value in getattr(place_db, field_name)]
            if not offsets or offsets[0] != 0 or offsets[-1] != final_length or any(
                left > right for left, right in zip(offsets, offsets[1:])
            ):
                raise AssertionError(f"{field_name} is not a valid monotonic CSR offset array")
        if len(place_db.inst_topo_start) != len(place_db.flat_inst_arcs_by_level_start):
            raise AssertionError("instance topology and arc levels must match")
        if any(int(index) < 0 or int(index) >= len(place_db.flat_inst_arcs_by_level)
               for index in place_db.inst_flat_arcs):
            raise AssertionError("instance CSR must contain level-arc indices")
        if any(int(index) != -1 and not 0 <= int(index) < len(place_db.flat_inst_arcs_by_level)
               for index in place_db.pin_pred_arc_id):
            raise AssertionError("predecessor CSR must address flat instance arcs or net edges")
        if sum(int(index) == -1 for index in place_db.pin_pred_arc_id) != len(place_db.net_flat_arcs):
            raise AssertionError("net predecessors must be represented exactly once")
        for arc in place_db.flat_inst_arcs_by_level:
            cell_id, lut_id = int(arc[2]), int(arc[3])
            if cell_id >= 0 and lut_id >= 0 and not (
                int(place_db.cell_id_2_arc_id_start[cell_id]) <= lut_id
                < int(place_db.cell_id_2_arc_id_start[cell_id + 1])
            ):
                raise AssertionError("instance arc must address its cell's global LUT row")
            if cell_id >= 0 and lut_id >= 0 and int(arc[6]) != lut_id - int(place_db.cell_id_2_arc_id_start[cell_id]):
                raise AssertionError("instance arc offset must remain cell-local")
        for field_name in (
            "backend_endpoint_rAAT",
            "backend_endpoint_fAAT",
            "backend_endpoint_rRAT",
            "backend_endpoint_fRAT",
            "backend_endpoint_rSlew",
            "backend_endpoint_fSlew",
            "backend_endpoint_min_rAAT",
            "backend_endpoint_min_fAAT",
            "backend_endpoint_min_rRAT",
            "backend_endpoint_min_fRAT",
        ):
            if len(getattr(place_db, field_name)) != len(place_db.end_points):
                raise AssertionError(f"{field_name} length does not match end_points")
        if len(place_db.outcaps) != len(place_db.end_points):
            raise AssertionError("outcaps length does not match end_points")
        if not any(float(value) > 0.0 for value in place_db.flat_lib_pin_cap):
            raise AssertionError("Liberty pin capacitance export is empty")
        mapped_pin_caps = []
        for pin_id, node_id in enumerate(place_db.pin2node_map):
            node_id = int(node_id)
            if node_id < 0 or node_id >= len(place_db.inst_main_id):
                continue
            main_id = int(place_db.inst_main_id[node_id])
            offset = int(place_db.pin_2_libpin_offset[pin_id])
            if main_id < 0 or offset < 0:
                continue
            cell_id = (
                int(place_db.main_id_2_cell_id_start[main_id])
                + int(place_db.inst_libcell_offset[node_id])
            )
            libpin_id = int(place_db.cell_id_2_libpin_id_start[cell_id]) + offset
            mapped_pin_caps.append(float(place_db.flat_lib_pin_cap[libpin_id]))
        if not mapped_pin_caps or not any(value > 0 for value in mapped_pin_caps):
            raise AssertionError("physical instance pins do not resolve to Liberty input caps")
        names = [str(name) for name in place_db.flat_libcell_names]
        low = place_db.flat_libcell_info[names.index("BUFX1P4H7L")]
        regular = place_db.flat_libcell_info[names.index("BUFX1P4H7R")]
        large = place_db.flat_libcell_info[names.index("BUFX3H7L")]
        if not (int(low[1]) == int(regular[1]) == int(large[1])):
            raise AssertionError("compatible buffer size/VT masters must share a family")
        if not (int(low[2]) < int(large[2]) and int(low[3]) != int(regular[3])):
            raise AssertionError("buffer family lacks distinct size and VT coordinates")
        if not bool(place_db.main_id_is_sizeable[int(low[1])]):
            raise AssertionError("physical buffer family must be sizeable")
        if int(low[1]) not in list(place_db.buffer_main_type_candidate_indices):
            raise AssertionError(
                "native buffer families omit BUF: "
                f"{place_db.buffer_main_type_status}, candidates={list(place_db.buffer_main_type_candidate_indices)}"
            )
        large_pin_start = int(place_db.cell_id_2_libpin_id_start[names.index("BUFX3H7L")])
        large_pin_end = int(place_db.cell_id_2_libpin_id_start[names.index("BUFX3H7L") + 1])
        if not any(float(value) > 0 for value in place_db.flat_lib_pin_offset_x[large_pin_start:large_pin_end]):
            raise AssertionError("physical BUF library pin geometry must come from LEF, not all-zero offsets")
        for cell_id in range(len(names)):
            start = int(place_db.cell_id_2_arc_id_start[cell_id])
            end = int(place_db.cell_id_2_arc_id_start[cell_id + 1])
            if [int(arc[3]) for arc in place_db.flat_libarc_info[start:end]] != list(range(end - start)):
                raise AssertionError("Liberty arc offsets must be cell-local, not global LUT rows")
        if not any(float(value) > 0.0 for value in place_db.flat_lib_pin_cap_limit):
            raise AssertionError("Liberty max-capacitance limits are missing")
        if not any(float(value) > 0.0 for value in place_db.flat_libcell_leakage):
            raise AssertionError("Liberty cell leakage is missing")
        mux_id = list(place_db.flat_libcell_names).index("MUX2X0P5H7R")
        mux_arcs = place_db.flat_libarc_info[
            int(place_db.cell_id_2_arc_id_start[mux_id]):
            int(place_db.cell_id_2_arc_id_start[mux_id + 1])
        ]
        if {int(arc[4]) for arc in mux_arcs if str(arc[0]) == "S0" and str(arc[1]) == "Y"} != {1, -1}:
            raise AssertionError("conditional MUX select arcs must preserve both transition senses")
        for index, pin_id in enumerate(place_db.end_points):
            if ":" not in str(place_db.pin_names[int(pin_id)]) and float(place_db.endpoints_rRAT[index]) < 8e7:
                raise AssertionError("output without SDC output delay must be unconstrained")
        clk2q_end = int(place_db.flat_inst_arcs_by_level_start[1])
        if clk2q_end == 0 or any(
            int(arc[0]) < 0 or int(arc[0]) >= len(place_db.clock_pins)
            for arc in place_db.flat_inst_arcs_by_level[:clk2q_end]
        ):
            raise AssertionError("level-0 clk2q arcs must use clock-local sources")
        q_pins = {int(place_db.flat_inst_arcs_by_level[index][1]) for index in range(clk2q_end)}
        if not q_pins.intersection(int(pin) for pin in place_db.start_points):
            raise AssertionError("sequential Q pins are missing from timing startpoints")
        for arc in place_db.endpoints_constraint_arcs:
            if int(arc[0]) < 0 or int(arc[0]) >= len(place_db.clock_pins):
                raise AssertionError("constraint arc source is not clock-local")
            cell_id, arc_id = int(arc[2]), int(arc[3])
            lut_id = arc_id
            if not (int(place_db.cell_id_2_arc_id_start[cell_id]) <= lut_id
                    < int(place_db.cell_id_2_arc_id_start[cell_id + 1])):
                raise AssertionError("setup constraint must address its cell's LUT row")
            if not any(float(value) > 0.0 for value in place_db.r_delay_flat_luts_values[lut_id]):
                raise AssertionError("setup constraint must reference a populated LUT")
        diagnostics = [str(value) for value in place_db.timing_diagnostics]
        if not any("min analysis AAT/RAT use max-analysis TimingPoint fallback" in value for value in diagnostics):
            raise AssertionError("the documented min-analysis fallback diagnostic is missing")
        summary = _output(manifest, "pyplacedb_timing.json")
        summary.write_text(
            json.dumps(
                {
                    "schema_version": place_db.timing_schema_version,
                    "num_pins": num_pins,
                    "num_start_points": len(place_db.start_points),
                    "num_end_points": len(place_db.end_points),
                    "num_clocks": len(place_db.timing_clock_names),
                    "num_clock_info": len(place_db.timing_clock_info),
                    "num_cell_arcs": len(place_db.flat_inst_arcs_by_level),
                    "num_net_arcs": len(place_db.net_flat_arcs),
                    "num_inst_arcs": len(place_db.inst_flat_arcs),
                    "num_constraint_arcs": len(place_db.endpoints_constraint_arcs),
                    "num_timing_check_arcs": len(place_db.endpoints_timing_check_arcs),
                    "num_libcells": len(place_db.flat_libcell_info),
                    "pin_graph_csr": len(place_db.flat_pin_to_graph_start),
                    "pin_pred_csr": len(place_db.pin_pred_start),
                    "time_unit_ps": place_db.timing_source_time_unit_ps,
                    "capacitance_unit_pf": place_db.timing_source_capacitance_unit_pf,
                    "resistance_unit_ohm": place_db.timing_source_resistance_unit_ohm,
                    "rc_c_unit_pf_per_um": place_db.c_unit,
                    "rc_r_unit_ohm_per_um": place_db.r_unit,
                    "timing_unit_provenance": place_db.timing_unit_provenance,
                    "parasitics_mode": str(place_db.timing_parasitics_initialization),
                    "endpoint_pin_ids": [int(value) for value in place_db.end_points],
                    "endpoint_names": [
                        str(place_db.pin_names[int(value)]) for value in place_db.end_points
                    ],
                    "endpoint_samples": [
                        {
                            "pin": str(place_db.pin_names[int(pin_id)]),
                            "rAAT_ps": float(place_db.backend_endpoint_rAAT[index]),
                            "fAAT_ps": float(place_db.backend_endpoint_fAAT[index]),
                            "rRAT_ps": float(place_db.backend_endpoint_rRAT[index]),
                            "fRAT_ps": float(place_db.backend_endpoint_fRAT[index]),
                            "outcap_pf": float(place_db.outcaps[index]),
                            "rSlew_ps": float(place_db.backend_endpoint_rSlew[index]),
                            "fSlew_ps": float(place_db.backend_endpoint_fSlew[index]),
                        }
                        for index, pin_id in enumerate(place_db.end_points)
                    ],
                    "diagnostics": diagnostics,
                },
                indent=2,
            ),
            encoding="utf-8",
        )
        _require_file(summary)
        return {"summary": summary}
    finally:
        ecc_py.destroy_sta()


def sizing_mutation(manifest: dict[str, Any]) -> dict[str, Path]:
    _setup(manifest)
    _read_design(manifest)
    place_db = ecc_py.pydb(ecc_py.get_dmInst(), 4, 4, False, False)  # noqa: FBT003
    candidate_ids = [
        node_id
        for node_id, name in enumerate(place_db.node_names)
        if "BUFX1P4H7L" in str(name)
    ]
    if len(candidate_ids) < 2:
        raise AssertionError("fixture needs two BUFX1P4H7L standard-cell instances")

    before = {
        node_id: (int(place_db.node_size_x[node_id]), int(place_db.node_size_y[node_id]), str(place_db.node_orient[node_id]))
        for node_id in candidate_ids[:2]
    }
    rejected = place_db.apply_sizing(
        candidate_ids[:2], ["BUFX3H7L", "__missing_master__"]
    )
    if rejected["ok"] or rejected["accepted_count"] != 0 or rejected["rejected_count"] != 1:
        raise AssertionError(f"invalid sizing transaction was not rejected atomically: {rejected}")
    for node_id, snapshot in before.items():
        after = (int(place_db.node_size_x[node_id]), int(place_db.node_size_y[node_id]), str(place_db.node_orient[node_id]))
        if after != snapshot:
            raise AssertionError("rejected sizing transaction changed the PyPlaceDB snapshot")
    if place_db.native_state_dirty:
        raise AssertionError("rejected sizing transaction marked the snapshot dirty")

    committed = place_db.apply_sizing([candidate_ids[0]], ["BUFX3H7L"])
    if not committed["ok"] or committed["accepted_count"] != 1 or not committed["requires_refresh"]:
        raise AssertionError(f"valid sizing transaction did not commit: {committed}")
    if not place_db.native_state_dirty or place_db.native_mutation_epoch != 1:
        raise AssertionError("successful sizing did not mark the native snapshot dirty")
    if str(place_db.node_orient[candidate_ids[0]]) != before[candidate_ids[0]][2]:
        raise AssertionError("sizing changed the node orientation")

    refreshed_db = ecc_py.pydb(ecc_py.get_dmInst(), 4, 4, False, False)  # noqa: FBT003
    refreshed_id = list(refreshed_db.node_names).index(str(place_db.node_names[candidate_ids[0]]))
    if int(refreshed_db.node_size_x[refreshed_id]) == before[candidate_ids[0]][0]:
        raise AssertionError("sizing did not update the dimensions in a new PyPlaceDB")
    if str(refreshed_db.node_orient[refreshed_id]) != before[candidate_ids[0]][2]:
        raise AssertionError("sizing changed the orientation in a new PyPlaceDB")

    def_output = _output(manifest, "sizing.def")
    verilog_output = _output(manifest, "sizing.v")
    _require(ecc_py.def_save(str(def_output)), "def_save")
    _require(ecc_py.netlist_save(str(verilog_output)), "netlist_save")
    _require_file(def_output)
    _require_file(verilog_output)
    def_text = def_output.read_text(encoding="utf-8")
    committed_name = str(committed["committed_instance_names"][0])
    if f"- {committed_name} BUFX3H7L " not in def_text:
        raise AssertionError("saved DEF does not contain the committed target master")
    summary = _output(manifest, "sizing.json")
    summary.write_text(
        json.dumps(
            {
                "rejected": dict(rejected),
                "committed": dict(committed),
                "instance_names": [str(place_db.node_names[node_id]) for node_id in candidate_ids[:2]],
                "before": {str(node_id): value for node_id, value in before.items()},
                "after_size_x": int(refreshed_db.node_size_x[refreshed_id]),
                "after_size_y": int(refreshed_db.node_size_y[refreshed_id]),
            },
            indent=2,
            default=list,
        ),
        encoding="utf-8",
    )
    return {"def": def_output, "verilog": verilog_output, "summary": summary}


def full_refresh_mutation(manifest: dict[str, Any]) -> dict[str, Path]:
    _setup(manifest)
    _read_design(manifest)
    first_spef = _run_rcx(manifest)
    _load_sta_inputs(manifest, spef_path=first_spef)
    output_dir = Path(manifest["output_dir"])
    sta_config = {
        "-max_paths": "20",
        "-output_timing_features": "0",
        "-output_timing_reports": "0",
        "-temp_directory_path": str(output_dir / "refresh_sta"),
        "-thread_number": "2",
        "-timing_path_limit": "20",
    }

    def run_sta():
        _require(ecc_py.init_sta("", sta_config), "init_sta")
        try:
            _require(ecc_py.run_sta(), "run_sta")
            return ecc_py.pydb(ecc_py.get_dmInst(), 4, 4, False, True)  # noqa: FBT003
        except Exception:
            ecc_py.destroy_sta()
            raise

    first_pydb = run_sta()
    candidate_ids = [
        node_id
        for node_id, name in enumerate(first_pydb.node_names)
        if "BUFX1P4H7L" in str(name)
    ]
    if len(candidate_ids) < 2:
        ecc_py.destroy_sta()
        raise AssertionError("fixture needs two sizing candidates for refresh")

    first = first_pydb.apply_sizing([candidate_ids[0]], ["BUFX3H7L"])
    if not first["ok"] or first["accepted_count"] != 1:
        ecc_py.destroy_sta()
        raise AssertionError(f"first sizing mutation failed: {first}")
    old_pydb_id = id(first_pydb)
    first_instance_name = str(first_pydb.node_names[candidate_ids[0]])
    ecc_py.destroy_sta()
    second_spef = _run_rcx(manifest)
    _load_sta_inputs(manifest, spef_path=second_spef)
    second_pydb = run_sta()
    if id(second_pydb) == old_pydb_id or int(second_pydb.timing_schema_version) != 2:
        ecc_py.destroy_sta()
        raise AssertionError("full refresh did not return a new valid timing PyPlaceDB")
    refreshed_id = list(second_pydb.node_names).index(str(first_pydb.node_names[candidate_ids[0]]))
    if int(second_pydb.node_size_x[refreshed_id]) == int(first_pydb.node_size_x[candidate_ids[0]]):
        ecc_py.destroy_sta()
        raise AssertionError("refreshed PyPlaceDB lost the first sizing mutation")

    refreshed_instance_id = list(second_pydb.node_names).index(first_instance_name)
    second = second_pydb.apply_sizing([refreshed_instance_id], ["BUFX7H7L"])
    if not second["ok"] or second["accepted_count"] != 1:
        ecc_py.destroy_sta()
        raise AssertionError(f"second sizing mutation failed after refresh: {second}")
    ecc_py.destroy_sta()
    third_spef = _run_rcx(manifest)
    _load_sta_inputs(manifest, spef_path=third_spef)
    third_pydb = run_sta()
    if int(third_pydb.timing_schema_version) != 2:
        ecc_py.destroy_sta()
        raise AssertionError("second refresh did not return a valid timing PyPlaceDB")
    first_qualification = assert_endpoint_qualification(first_pydb)
    second_qualification = assert_endpoint_qualification(second_pydb)
    third_qualification = assert_endpoint_qualification(third_pydb)
    if first_qualification != second_qualification or second_qualification != third_qualification:
        raise AssertionError("sizing refresh changed endpoint qualification identities")
    summary = _output(manifest, "full_refresh_mutation.json")
    summary.write_text(
        json.dumps(
            {
                "first": dict(first),
                "second": dict(second),
                "first_pydb_id": old_pydb_id,
                "second_pydb_id": id(second_pydb),
                "third_pydb_id": id(third_pydb),
                "timing_schema_version": int(third_pydb.timing_schema_version),
                "endpoints": len(third_pydb.end_points),
                "rcx_spef_paths": [first_spef, second_spef, third_spef],
            },
            indent=2,
            default=list,
        ),
        encoding="utf-8",
    )
    ecc_py.destroy_sta()
    return {"summary": summary}


def _buffer_action_digest(actions: list[dict[str, Any]]) -> str:
    serialized = json.dumps(actions, allow_nan=False, separators=(",", ":"), sort_keys=True)
    return hashlib.sha256(serialized.encode("utf-8")).hexdigest()


def buffer_mutation(manifest: dict[str, Any]) -> dict[str, Path]:
    _setup(manifest)
    _read_design(manifest)
    place_db = ecc_py.pydb(ecc_py.get_dmInst(), 4, 4, False, False)  # noqa: FBT003
    selected = None
    clock_nets = {str(name) for name in place_db.clock_net_names}
    for net_id, net_name_value in enumerate(place_db.net_names):
        net_name = str(net_name_value)
        if net_name in clock_nets:
            continue
        pin_ids = [int(pin_id) for pin_id in place_db.net2pin_map[net_id]]
        if len(pin_ids) >= 3:
            flat_start = int(place_db.flat_net2pin_start_map[net_id])
            driver_pin_id = int(place_db.flat_net2pin_map[flat_start])
            selected = net_name, driver_pin_id, [pin_id for pin_id in pin_ids if pin_id != driver_pin_id][:2]
            break
    if selected is None:
        raise AssertionError("fixture needs a regular net with one driver and two loads")
    net_name, driver_pin_id, load_pin_ids = selected
    driver_name = str(place_db.pin_names[driver_pin_id]).replace(":", "/")
    load_names = [str(place_db.pin_names[pin_id]).replace(":", "/") for pin_id in load_pin_ids]
    action = {
        "action_id": 0,
        "action_kind": "buffer_insert",
        "net_name": net_name,
        "buffer_main_type_index": 0,
        "bsu": 0,
        "buffer_master_id": 0,
        "buffer_master_name": "BUFX3H7L",
        "candidate_location_x_dbu": 10000,
        "candidate_location_y_dbu": 10000,
        "driver_pin_name": driver_name,
        "load_pin_name": load_names[0],
        "downstream_pin_names": load_names,
    }
    actions = [action]
    invalid = place_db.apply_buffer_actions(actions, "0" * 64)
    if invalid["ok"] or invalid["failed_count"] != 1 or place_db.native_state_dirty:
        raise AssertionError(f"invalid buffer digest was not rejected atomically: {invalid}")
    rollback_actions = [action, dict(action, action_id=1)]
    rollback = place_db.apply_buffer_actions(
        rollback_actions, _buffer_action_digest(rollback_actions)
    )
    if (
        rollback["ok"]
        or rollback["failed_count"] != 1
        or place_db.native_state_dirty
        or place_db.native_mutation_epoch != 0
    ):
        raise AssertionError(f"mid-operation buffer failure was not rolled back: {rollback}")
    committed = place_db.apply_buffer_actions(actions, _buffer_action_digest(actions))
    if not committed["ok"] or committed["accepted_count"] != 1 or not committed["topology_mutated"]:
        raise AssertionError(f"valid buffer transaction did not commit: {committed}")
    if not place_db.native_state_dirty or place_db.native_mutation_epoch != 1:
        raise AssertionError("successful buffer transaction did not mark the native snapshot dirty")

    refreshed_db = ecc_py.pydb(ecc_py.get_dmInst(), 4, 4, False, False)  # noqa: FBT003
    committed_buffer = str(committed["committed_buffer_instance_names"][0])
    downstream_net = str(committed["committed_downstream_net_names"][0])
    if committed_buffer not in {str(name) for name in refreshed_db.node_names}:
        raise AssertionError("new buffer instance is missing from a new PyPlaceDB")
    if downstream_net not in {str(name) for name in refreshed_db.net_names}:
        raise AssertionError("new downstream net is missing from a new PyPlaceDB")
    downstream_id = list(map(str, refreshed_db.net_names)).index(downstream_net)
    downstream_pin_names = {str(refreshed_db.pin_names[int(pin_id)]) for pin_id in refreshed_db.net2pin_map[downstream_id]}
    if not all(name.replace("/", ":") in downstream_pin_names for name in load_names):
        raise AssertionError("committed loads are not connected to the downstream net")

    # A real Segment batch can insert an ancestor and repeaters on a child
    # edge. Present it out of order to verify native topology ordering.
    ancestor = dict(action, action_id=2, net_name=downstream_net,
                    driver_pin_name=committed_buffer + "/Y", segment_tree_depth=0,
                    segment_parent_node_id=10, segment_child_node_id=11, segment_split_ratio=0.5)
    child = dict(ancestor, action_id=3, downstream_pin_names=load_names[:1],
                 segment_tree_depth=1, segment_parent_node_id=11, segment_child_node_id=12,
                 segment_split_ratio=0.25)
    repeated = dict(child, action_id=4, segment_split_ratio=0.75)
    chain_actions = [repeated, ancestor, child]
    chain = refreshed_db.apply_buffer_actions(chain_actions, _buffer_action_digest(chain_actions))
    if not chain["ok"] or chain["accepted_count"] != 3:
        raise AssertionError(f"nested Segment buffer batch did not commit: {chain}")
    chain_db = ecc_py.pydb(ecc_py.get_dmInst(), 4, 4, False, False)  # noqa: FBT003
    net_names = [str(name) for name in chain_db.net_names]
    pin_nets = {str(name): net_names[int(chain_db.pin2net_map[index])]
                for index, name in enumerate(chain_db.pin_names)}
    buffers = list(chain["committed_buffer_instance_names"])
    nets = list(chain["committed_downstream_net_names"])
    expected_connections = {
        buffers[0] + ":A": downstream_net,
        buffers[0] + ":Y": nets[0],
        buffers[1] + ":A": nets[0],
        buffers[1] + ":Y": nets[1],
        buffers[2] + ":A": nets[1],
        buffers[2] + ":Y": nets[2],
        load_names[0].replace("/", ":"): nets[2],
        load_names[1].replace("/", ":"): nets[0],
    }
    if {pin: pin_nets.get(pin) for pin in expected_connections} != expected_connections:
        raise AssertionError("native Segment chain lost its ancestor/child connectivity")

    fresh_spef = _run_rcx(manifest)
    _load_sta_inputs(manifest, spef_path=fresh_spef)
    _require(
        ecc_py.init_sta("", {
            "-temp_directory_path": str(_output(manifest, "buffer_refresh_sta")),
            "-thread_number": "2", "-output_timing_reports": "0",
        }),
        "init_sta",
    )
    try:
        _require(ecc_py.run_sta(), "run_sta")
        timing_db = ecc_py.pydb(ecc_py.get_dmInst(), 4, 4, False, True)  # noqa: FBT003
        assert_endpoint_qualification(timing_db)
        timing_pin_count = len(timing_db.pin_names)
        if timing_pin_count <= len(place_db.pin_names):
            raise AssertionError("timing refresh reused pre-buffer pin identities")
    finally:
        ecc_py.destroy_sta()

    def_output = _output(manifest, "buffer.def")
    verilog_output = _output(manifest, "buffer.v")
    _require(ecc_py.def_save(str(def_output)), "def_save")
    _require(ecc_py.netlist_save(str(verilog_output)), "netlist_save")
    _require_file(def_output)
    _require_file(verilog_output)
    summary = _output(manifest, "buffer.json")
    summary.write_text(
        json.dumps(
            {
                "invalid": dict(invalid),
                "rollback": dict(rollback),
                "committed": dict(committed),
                "segment_chain": dict(chain),
                "timing_schema_version": int(timing_db.timing_schema_version),
                "timing_pin_count": timing_pin_count,
                "source_net": net_name,
                "driver_pin": driver_name,
                "load_pins": load_names,
                "buffer_instance": committed_buffer,
                "downstream_net": downstream_net,
            },
            indent=2,
            default=list,
        ),
        encoding="utf-8",
    )
    return {"def": def_output, "verilog": verilog_output, "summary": summary}


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
    _require(ecc_py.gds_save(str(gds_output), manifest["gds_layer_map"]), "gds_save")
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
    _require(ecc_py.run_drc(), "run_drc")
    _require(ecc_py.destroy_drc(), "destroy_drc")
    violation_map = output_dir / "violation_map.json"
    output = _output(manifest, "drc.def")
    _require(ecc_py.def_save(str(output)), "def_save")
    for path in (violation_map, output):
        _require_file(path)
    return {"def": output, "violation_map": violation_map}


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
    _require(ecc_py.gds_save(str(hardened_gds), manifest["gds_layer_map"]), "gds_save")
    for path in (abstract_lef, hardened_gds, hardened_lib):
        _require_file(path)
    return {"gds": hardened_gds, "lef": abstract_lef, "lib": hardened_lib}


def _load_default_timing_inputs(manifest: dict[str, Any]) -> None:
    pdk = manifest["pdk"]
    _require(ecc_py.lib_init(pdk["default_libs"]), "lib_init")
    _require(ecc_py.sdc_init(pdk["sdc"]), "sdc_init")


def _run_rcx(manifest: dict[str, Any]) -> str:
    _require(ecc_py.init_rcx(manifest["config"]["rcx_ecc"], pdk="ics55"), "init_rcx")
    try:
        _require(ecc_py.run_rcx(), "run_rcx")
    finally:
        ecc_py.destroy_rcx()
    spefs = sorted((Path(manifest["output_dir"]) / "spef_writer").glob("gcd_*.spef"))
    if not spefs:
        raise AssertionError("RCX did not produce a SPEF file for timing refresh")
    typical = [path for path in spefs if "TYPICAL" in path.name]
    selected = typical[0] if typical else spefs[0]
    _require_file(selected)
    return str(selected)


def _load_sta_inputs(manifest: dict[str, Any], *, spef_path: str | None = None) -> None:
    pdk = manifest["pdk"]
    _require(ecc_py.lib_init(pdk["typical_libs"]), "lib_init")
    _require(ecc_py.sdc_init(pdk["sdc"]), "sdc_init")
    selected_spef = spef_path or pdk.get("spef") or ""
    if selected_spef:
        _require(ecc_py.spef_init(selected_spef), "spef_init")


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
    "antenna": antenna,
    "combined_io": combined_io,
    "cts": cts,
    "def_round_trip": def_round_trip,
    "def_verify": def_verify,
    "drc": drc,
    "floorplan": floorplan,
    "harden": harden,
    "lvs": lvs,
    "placement_map": placement_map,
    "pyplacedb_rows": pyplacedb_rows,
    "pyplacedb_timing": pyplacedb_timing,
    "sizing_mutation": sizing_mutation,
    "full_refresh_mutation": full_refresh_mutation,
    "buffer_mutation": buffer_mutation,
    "rcx": rcx,
    "routing": routing,
    "sta": sta,
    "verilog_round_trip": verilog_round_trip,
    "verilog_verify": verilog_verify,
}


if __name__ == "__main__":
    main()
