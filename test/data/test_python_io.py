from support import assert_nonempty_file, run_scenario


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
