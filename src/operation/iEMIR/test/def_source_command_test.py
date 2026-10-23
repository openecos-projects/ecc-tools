"""Check explicit DEF supply selection through real Tcl/Python solves."""
import csv
from pathlib import Path
import subprocess
import sys
import tempfile

from emir_command_fixture import write_lef


def write_inputs(root):
    write_lef(root)
    # This DEF has
    # one connected rail per net, IO sources at x=100 and file sources at x=108.
    # Implicitly combining the two would produce two ideal sources per net.
    pins = """PINS 2 ;
- PVDD + NET VDD + DIRECTION INOUT + USE POWER
  + LAYER M1 ( -100 -100 ) ( 100 100 ) + FIXED ( 100000 203000 ) N ;
- PVSS + NET VSS + DIRECTION INOUT + USE GROUND
  + LAYER M1 ( -100 -100 ) ( 100 100 ) + FIXED ( 100000 211000 ) N ;
END PINS
"""
    design = """VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN def_sources ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 500000 500000 ) ;
COMPONENTS 1 ;
- u0 PAD + FIXED ( 100000 200000 ) N ;
END COMPONENTS
{pins}
SPECIALNETS 2 ;
- VDD ( u0 VP ) {power_io} + USE POWER
  + ROUTED M1 200 ( 100000 203000 ) ( 108000 203000 ) ;
- VSS ( u0 VG ) {ground_io} + USE GROUND
  + ROUTED M1 200 ( 100000 211000 ) ( 108000 211000 ) ;
END SPECIALNETS
END DESIGN
"""
    (root / "io.def").write_text(design.format(pins=pins, power_io="( PIN PVDD )", ground_io="( PIN PVSS )"))
    (root / "no_io.def").write_text(design.format(pins="", power_io="", ground_io=""))
    (root / "supply.ploc").write_text("supply 108 203 M1 VDD\nreturn 108 211 M1 VSS\n")
    (root / "power.tsv").write_text(
        "# iEMIR_INSTANCE_POWER_V1\n"
        "instance_name\tvoltage_v\tinternal_power_w\tswitching_power_w\tleakage_power_w\ttotal_power_w\taverage_current_a\n"
        "u0\t1\t0.001\t0\t0\t0.001\t0.001\n"
    )
    (root / "tech.txt").write_text("METAL M1 { RESISTANCE 0.1 EM 1 }\nMETAL M2 { RESISTANCE 0.1 EM 1 }\n")
    (root / "protected").mkdir()
    (root / "protected" / "keep").write_text("prior result")


def source_counts(directory):
    with (directory / "ir_analyzer" / "solver_diagnostics.csv").open() as stream:
        rows = list(csv.DictReader(stream))
    return {row["net"]: int(row["source_count"]) for row in rows}


def check_tcl(binary, root):
    script = root / "def_sources.tcl"
    script.write_text(r'''
proc must_fail {command pattern} {
    if {![catch {uplevel 1 $command} message]} { error "accepted invalid source selection: $command" }
    if {![string match $pattern $message]} { error "unexpected error: $message" }
}
tech_lef_init -path pads.lef
def_init -path io.def
set common [list -instance_power_file_path power.tsv -technology_file_path tech.txt -thread_number 1]
must_fail {init_emir {*}$common -temp_directory_path protected} {*supply*add_ploc_from_top_def*}
must_fail {init_emir {*}$common -temp_directory_path protected -add_ploc_from_top_def 0} {*supply*add_ploc_from_top_def*}
must_fail {init_emir {*}$common -temp_directory_path protected -add_ploc_from_top_def 2} {*0 or 1*}
must_fail {init_emir {*}$common -temp_directory_path protected -add_ploc_from_top_def -1} {*0 or 1*}
foreach value {1garbage 1.5 garbage 0garbage {}} {
    must_fail {init_emir {*}$common -temp_directory_path protected -add_ploc_from_top_def $value} {*0 or 1*}
}
must_fail {init_emir {*}$common -temp_directory_path protected -add_ploc_from_top_def 1 -pad_files {supply.ploc}} {*mutually exclusive*}
must_fail {init_emir {*}$common -temp_directory_path protected -add_ploc_from_top_def 1 -ploc_file_path supply.ploc} {*mutually exclusive*}
if {![file exists protected/keep]} { error "invalid source selection deleted prior results" }
init_emir {*}$common -temp_directory_path tcl_io -add_ploc_from_top_def 1
run_emir
destroy_emir
# The omitted option must revert to off even after the preceding enabled run.
init_emir {*}$common -temp_directory_path tcl_file -pad_files {supply.ploc}
run_emir
destroy_emir
init_emir {*}$common -temp_directory_path tcl_zero -ploc_file_path supply.ploc -add_ploc_from_top_def 0
run_emir
destroy_emir
set fh [open passed w]
puts $fh PASS
close $fh
flow_exit
''')
    result = subprocess.run([binary, "-script", str(script)], cwd=root, capture_output=True, text=True, timeout=60)
    assert result.returncode == 0 and (root / "passed").exists(), result.stdout + result.stderr
    assert (root / "protected" / "keep").read_text() == "prior result"
    for name in ("tcl_io", "tcl_file", "tcl_zero"):
        assert source_counts(root / name) == {"VDD": 1, "VSS": 1}, name

    # Opt-in does not invent a source when DEF has only routed PG geometry.
    script.write_text("""tech_lef_init -path pads.lef
def_init -path no_io.def
init_emir -temp_directory_path no_io -instance_power_file_path power.tsv -technology_file_path tech.txt -thread_number 1 -add_ploc_from_top_def 1
run_emir
flow_exit
""")
    result = subprocess.run([binary, "-script", str(script)], cwd=root, capture_output=True, text=True, timeout=60)
    assert result.returncode != 0 and "does not have a PLOC or IO source" in result.stdout + result.stderr, result.stdout + result.stderr


def check_python(module_dir, root):
    sys.path.insert(0, str(Path(module_dir).resolve()))
    import ecc_py

    ecc_py.tech_lef_init(str(root / "pads.lef"))
    ecc_py.def_init(str(root / "io.def"))
    common = dict(instance_power_file_path=str(root / "power.tsv"), technology_file_path=str(root / "tech.txt"), thread_number=1)
    for options, detail in (({}, "add_ploc_from_top_def"), ({"add_ploc_from_top_def": False}, "add_ploc_from_top_def"),
                            ({"add_ploc_from_top_def": True, "pad_files": [str(root / "supply.ploc")]}, "mutually exclusive")):
        try:
            ecc_py.init_emir(temp_directory_path=str(root / "protected"), **common, **options)
        except RuntimeError as error:
            assert detail in str(error), error
        except ValueError as error:
            assert detail in str(error), error
        else:
            raise AssertionError("Python accepted invalid source selection")
    assert (root / "protected" / "keep").exists()
    for name, options in (("python_io", {"add_ploc_from_top_def": True}),
                          ("python_file", {"pad_files": [str(root / "supply.ploc")]}),
                          ("python_zero", {"ploc_file_path": str(root / "supply.ploc"), "add_ploc_from_top_def": False})):
        assert ecc_py.init_emir(temp_directory_path=str(root / name), **common, **options)
        assert ecc_py.run_emir()
        ecc_py.destroy_emir()
        assert source_counts(root / name) == {"VDD": 1, "VSS": 1}, name


def main():
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="iemir_def_source_") as folder:
        root = Path(folder)
        write_inputs(root)
        check_tcl(binary, root)
        if len(sys.argv) > 2:
            check_python(sys.argv[2], root)
    print("Explicit DEF source selection passed")


if __name__ == "__main__":
    main()
