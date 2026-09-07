import json

from support import assert_nonempty_file, run_scenario


def test_floorplan(test_roots):
    result = run_scenario(test_roots, "floorplan", timeout=480)
    output = result.output_path("def")

    assert_nonempty_file(output)
    assert "DIEAREA" in output.read_text(encoding="utf-8")


def test_cts(test_roots):
    result = run_scenario(test_roots, "cts", timeout=480)
    report_dir = result.output_path("report_dir")

    assert_nonempty_file(result.output_path("def"))
    reports = [
        path
        for path in report_dir.rglob("*")
        if path.is_file() and path.stat().st_size > 0
    ]
    assert reports, f"CTS did not write a report under {report_dir}"


def test_routing(test_roots):
    result = run_scenario(test_roots, "routing", timeout=480)
    output = result.output_path("def")

    assert_nonempty_file(output)
    assert "NETS" in output.read_text(encoding="utf-8")


def test_drc(test_roots):
    result = run_scenario(test_roots, "drc", timeout=480)
    violation_map = result.output_path("violation_map")

    for name in ("database", "def", "violation_map"):
        assert_nonempty_file(result.output_path(name))
    assert isinstance(json.loads(violation_map.read_text(encoding="utf-8")), list)


def test_rcx(test_roots):
    result = run_scenario(test_roots, "rcx", timeout=480)
    spef = result.output_path("spef")

    assert_nonempty_file(result.output_path("def"))
    assert_nonempty_file(spef)
    assert "*SPEF" in spef.read_text(encoding="utf-8")


def test_sta(test_roots):
    result = run_scenario(test_roots, "sta", timeout=480)

    assert_nonempty_file(result.output_path("report"))


def test_lvs(test_roots):
    result = run_scenario(test_roots, "lvs", timeout=480)

    for name in ("def", "feature", "report"):
        assert_nonempty_file(result.output_path(name))
    json.loads(result.output_path("feature").read_text(encoding="utf-8"))


def test_harden(test_roots):
    result = run_scenario(test_roots, "harden", timeout=480)

    for name in ("gds", "lef", "lib"):
        assert_nonempty_file(result.output_path(name))
    assert "MACRO gcd" in result.output_path("lef").read_text(encoding="utf-8")
