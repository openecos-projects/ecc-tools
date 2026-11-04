"""Read absolute source files through Tcl/Python without a conversion step."""
import csv
from pathlib import Path
import subprocess
import sys
import tempfile

from def_source_command_test import source_counts, write_inputs


def check_reports(root, name):
    analysis = root / name
    assert source_counts(analysis) == {"VDD": 1, "VSS": 1}
    assert not (analysis / "data_manager/resolved_sources.ploc").exists()
    assert not (analysis / "data_manager/resolved_sources.ploc.csv").exists()
    with (analysis / "graph_builder/source_connections.csv").open() as stream:
        rows = list(csv.DictReader(stream))
    assert {row["source_name"] for row in rows} == {"OTHER_POWER_0", 'bump,"ground"'}, rows
    assert {row["net"] for row in rows} == {"VDD", "VSS"}, rows


def main():
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="iemir_ploc_command_") as folder:
        root = Path(folder)
        write_inputs(root)
        power = "OTHER_POWER_0 108 203 M1 POWER\n"
        ground = 'bump,"ground" 108 211 M1 GROUND net=VSS\n'
        (root / "power supplies.ploc").write_text(power)
        (root / "ground.ploc").write_text(ground)
        (root / "combined.txt").write_text("*PLOC\n" + power + "*PLOC\n" + ground)
        (root / "local.pad").write_text("u0 2 3 M1 POWER\n")
        (root / "local.pcell").write_text("PAD 2 3 M1 POWER\n")
        (root / "mixed.txt").write_text("*PLOC\n" + ground + "*PAD\nu0 2 3 M1 POWER\n")
        (root / "duplicate.ploc").write_text("OTHER_POWER_0 108 211 M1 VSS\n")
        protected = root / "protected"
        (protected / "supply.ploc").write_text(power)
        (root / "protected_alias").symlink_to(protected, target_is_directory=True)
        linked = root / "linked_analysis"
        linked.mkdir()
        (linked / "supply.ploc").symlink_to(root / "power supplies.ploc")
        (linked / "external").symlink_to(root, target_is_directory=True)
        script = root / "read_sources.tcl"
        script.write_text(r'''
proc must_fail {command pattern} {
    if {![catch {uplevel 1 $command} message]} { error "accepted invalid input: $command" }
    if {![string match $pattern $message]} { error "unexpected error: $message" }
}
if {[llength [info commands generate_ploc]] != 0} { error "obsolete generator command still registered" }
lef_init -path {pads.lef}
def_init -path io.def
set common [list -instance_power_file_path power.tsv -technology_file_path tech.txt -thread_number 1]
must_fail {init_emir -pad_files {{power supplies.ploc}} -ploc_file_path ground.ploc} {*mutually exclusive*}
must_fail {init_emir -temp_directory_path protected_alias -pad_files {protected/supply.ploc}} {*inside*analysis*}
if {![file exists protected/supply.ploc]} { error "initialization deleted a supply input" }
must_fail {init_emir {*}$common -temp_directory_path linked_analysis -pad_files {linked_analysis/supply.ploc}} {*inside*analysis*}
if {![file exists linked_analysis/supply.ploc]} { error "initialization deleted the input symlink" }
must_fail {init_emir {*}$common -temp_directory_path linked_analysis -pad_files {{linked_analysis/external/power supplies.ploc}}} {*inside*analysis*}
if {![file exists linked_analysis/external]} { error "initialization deleted the input's directory symlink" }
foreach path {local.pad local.pcell mixed.txt} {
    must_fail {init_emir {*}$common -temp_directory_path invalid -pad_files [list $path]} {*pre-generated PLOC*}
    destroy_emir
}
must_fail {init_emir {*}$common -temp_directory_path invalid -pad_files {{power supplies.ploc} duplicate.ploc}} {*duplicate supply source name*}
destroy_emir
init_emir {*}$common -temp_directory_path analysis -pad_files {{power supplies.ploc} ground.ploc}
run_emir
destroy_emir
init_emir {*}$common -temp_directory_path combined -pad_files {combined.txt}
run_emir
destroy_emir
set fh [open command_passed w]
puts $fh PASS
close $fh
flow_exit
''')
        result = subprocess.run([binary, "-script", str(script)], cwd=root, capture_output=True, text=True, timeout=60)
        assert result.returncode == 0 and (root / "command_passed").exists(), result.stdout + result.stderr
        check_reports(root, "analysis")
        check_reports(root, "combined")
        if len(sys.argv) > 2:
            sys.path.insert(0, str(Path(sys.argv[2]).resolve()))
            import ecc_py
            assert not hasattr(ecc_py, "generate_ploc"), "obsolete Python generator still exported"
            ecc_py.lef_init([str(root / "pads.lef")])
            ecc_py.def_init(str(root / "io.def"))
            common = dict(instance_power_file_path=str(root / "power.tsv"), thread_number=1,
                          technology_file_path=str(root / "tech.txt"))
            for path in ("local.pad", "local.pcell", "mixed.txt"):
                try:
                    ecc_py.init_emir(temp_directory_path=str(root / "python_invalid"), **common, pad_files=[str(root / path)])
                except RuntimeError as error:
                    assert "pre-generated PLOC" in str(error), error
                else:
                    raise AssertionError("Python accepted local definitions")
                finally:
                    ecc_py.destroy_emir()
            assert ecc_py.init_emir(temp_directory_path=str(root / "python_analysis"), **common,
                                   pad_files=[str(root / "power supplies.ploc"), str(root / "ground.ploc")])
            assert ecc_py.run_emir()
            ecc_py.destroy_emir()
            check_reports(root, "python_analysis")
        print("Absolute PLOC input integration passed")


if __name__ == "__main__":
    main()
