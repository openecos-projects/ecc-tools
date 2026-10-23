"""Exercise missing/invalid EM rule semantics through file import and real solves."""
import csv
from pathlib import Path
import subprocess
import sys
import tempfile

from def_source_command_test import write_inputs


def main():
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="iemir_em_rules_") as directory:
        root = Path(directory)
        write_inputs(root)
        for name, em in (("missing", ""), ("zero", "EM 0"), ("negative", "EM -1"),
                         ("nan", "EM nan"), ("infinite", "EM inf"), ("valid", "EM 1")):
            (root / "tech.txt").write_text(f"METAL M1 {{ RESISTANCE 0.1 {em} }}\n")
            script = root / "run.tcl"
            script.write_text(f'''tech_lef_init -path pads.lef
def_init -path io.def
init_emir -temp_directory_path {name} -instance_power_file_path power.tsv -technology_file_path tech.txt -add_ploc_from_top_def 1 -thread_number 1
run_emir
destroy_emir
flow_exit
''')
            result = subprocess.run([binary, "-script", str(script)], cwd=root,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                    universal_newlines=True)
            assert result.returncode == 0, result.stdout + result.stderr
            with (root / name / "emir_reporter/em_evaluation.csv").open() as stream:
                rows = [row for row in csv.DictReader(stream) if row["status"] != "EXCLUDED"]
            expected = "NO_RULE" if name in ("missing", "nan", "infinite") else "EVALUATED" if name == "valid" else "INVALID_LIMIT"
            assert rows and all(row["status"] == expected for row in rows), (name, rows)
            if expected != "EVALUATED":
                assert all(row["ratio_percent"] == "N/A" and row["result"] != "PASS" for row in rows)

        for name, limit, expected in (("legacy_alias", "MET1 2", "EVALUATED"),
                                      ("legacy_trailing", "M1 2junk", "NO_RULE"),
                                      ("legacy_nonfinite", "M1 inf", "NO_RULE")):
            (root / "tech.txt").write_text("METAL M1 { RESISTANCE 0.1 }\n")
            (root / "em_limits.txt").write_text(limit + "\n")
            script = root / "run.tcl"
            script.write_text(f'''tech_lef_init -path pads.lef
def_init -path io.def
init_emir -temp_directory_path {name} -instance_power_file_path power.tsv -technology_file_path tech.txt -em_limit_file_path em_limits.txt -add_ploc_from_top_def 1 -thread_number 1
run_emir
destroy_emir
flow_exit
''')
            result = subprocess.run([binary, "-script", str(script)], cwd=root,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                    universal_newlines=True)
            assert result.returncode == 0, result.stdout + result.stderr
            with (root / name / "emir_reporter/em_evaluation.csv").open() as stream:
                rows = [row for row in csv.DictReader(stream) if row["status"] != "EXCLUDED"]
            assert rows and all(row["status"] == expected for row in rows), (name, rows)
            if expected == "EVALUATED":
                assert all(row["rule"] == "MET1" and row["match"] == "ALIAS" for row in rows), rows
            else:
                assert all(row["ratio_percent"] == "N/A" and row["result"] == "NOT_EVALUATED" for row in rows), rows
        print("EM rule file integration passed")


if __name__ == "__main__":
    main()
