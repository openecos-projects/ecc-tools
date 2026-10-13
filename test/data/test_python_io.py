import json

from ecc_tools_bin import ecc_py

from support import assert_nonempty_file, run_scenario


def test_ifp_python_api_matches_tcl_commands():
    for command in ("init_fp", "run_simple_fp", "run_fp", "destroy_fp"):
        assert hasattr(ecc_py, command), f"missing Python iFP binding: {command}"


def test_pyplacedb_timing_export_contract(test_roots):
    result = run_scenario(test_roots, "pyplacedb_timing", timeout=180)
    summary = result.output_path("summary")
    payload = json.loads(summary.read_text(encoding="utf-8"))
    assert payload["schema_version"] == 2
    assert payload["num_pins"] > 0
    assert payload["num_start_points"] > 0
    assert payload["num_end_points"] > 0
    assert payload["num_clocks"] > 0
    assert payload["num_clock_info"] == payload["num_clocks"]
    assert payload["num_cell_arcs"] > 0
    assert payload["num_net_arcs"] > 0
    assert payload["num_inst_arcs"] == payload["num_cell_arcs"]
    assert payload["num_constraint_arcs"] > 0
    assert payload["num_timing_check_arcs"] > 0
    assert payload["pin_graph_csr"] == payload["num_pins"] + 1
    assert payload["pin_pred_csr"] == payload["num_pins"] + 1
    assert payload["time_unit_ps"] == 1000.0
    assert payload["capacitance_unit_pf"] == 1.0
    assert payload["resistance_unit_ohm"] == 1000.0
    assert payload["rc_c_unit_pf_per_um"] > 0.0
    assert payload["rc_r_unit_ohm_per_um"] > 0.0
    assert "min analysis AAT/RAT use max-analysis TimingPoint fallback" in payload["diagnostics"]


def test_iemir_python_api_matches_tcl_commands():
    for command in ("init_emir", "run_emir", "destroy_emir"):
        assert hasattr(ecc_py, command), f"missing Python iEMIR binding: {command}"


def test_def_round_trip(test_roots):
    result = run_scenario(test_roots, "def_round_trip", timeout=120)
    output = result.output_path("def")

    assert_nonempty_file(output)
    assert "DESIGN gcd" in output.read_text(encoding="utf-8")
    run_scenario(test_roots, "def_verify", timeout=120, input_overrides={"def": output})


def test_verilog_round_trip(test_roots):
    result = run_scenario(test_roots, "verilog_round_trip", timeout=120)
    output = result.output_path("verilog")

    assert_nonempty_file(output)
    assert "module gcd" in output.read_text(encoding="utf-8")
    run_scenario(
        test_roots, "verilog_verify", timeout=120, input_overrides={"verilog": output}
    )


def test_def_and_verilog_exports(test_roots):
    result = run_scenario(test_roots, "combined_io", timeout=120)
    def_output = result.output_path("def")
    verilog_output = result.output_path("verilog")

    for path in (def_output, verilog_output, result.output_path("gds")):
        assert_nonempty_file(path)
    assert "DESIGN gcd" in def_output.read_text(encoding="utf-8")
    assert "module gcd" in verilog_output.read_text(encoding="utf-8")


def test_native_sizing_round_trip_and_preflight_rollback(test_roots):
    result = run_scenario(test_roots, "sizing_mutation", timeout=180)
    payload = json.loads(result.output_path("summary").read_text(encoding="utf-8"))
    assert payload["rejected"]["ok"] is False
    assert payload["rejected"]["accepted_count"] == 0
    assert payload["rejected"]["rejected_count"] == 1
    assert payload["committed"]["ok"] is True
    assert payload["committed"]["accepted_count"] == 1
    assert payload["committed"]["requires_refresh"] is True
    assert_nonempty_file(result.output_path("def"))
    assert_nonempty_file(result.output_path("verilog"))
    run_scenario(test_roots, "def_verify", timeout=120, input_overrides={"def": result.output_path("def")})
    run_scenario(
        test_roots,
        "verilog_verify",
        timeout=120,
        input_overrides={"verilog": result.output_path("verilog")},
    )


def test_native_full_refresh_and_second_mutation(test_roots):
    result = run_scenario(test_roots, "full_refresh_mutation", timeout=240)
    payload = json.loads(result.output_path("summary").read_text(encoding="utf-8"))
    assert payload["first"]["ok"] is True
    assert payload["second"]["ok"] is True
    assert payload["first_pydb_id"] != payload["second_pydb_id"]
    assert payload["second_pydb_id"] != payload["third_pydb_id"]
    assert payload["timing_schema_version"] == 2
    assert payload["endpoints"] > 0


def test_native_buffer_topology_round_trip_and_digest_rejection(test_roots):
    result = run_scenario(test_roots, "buffer_mutation", timeout=180)
    payload = json.loads(result.output_path("summary").read_text(encoding="utf-8"))
    assert payload["invalid"]["ok"] is False
    assert payload["invalid"]["failed_count"] == 1
    assert payload["rollback"]["ok"] is False
    assert payload["rollback"]["failed_count"] == 1
    assert payload["committed"]["ok"] is True
    assert payload["committed"]["accepted_count"] == 1
    assert payload["committed"]["topology_mutated"] is True
    assert payload["committed"]["requires_refresh"] is True
    assert_nonempty_file(result.output_path("def"))
    assert_nonempty_file(result.output_path("verilog"))
    run_scenario(test_roots, "def_verify", timeout=120, input_overrides={"def": result.output_path("def")})
    run_scenario(
        test_roots,
        "verilog_verify",
        timeout=120,
        input_overrides={"verilog": result.output_path("verilog")},
    )
