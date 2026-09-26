#!/usr/bin/env python3
"""Compare OpenDB and EccDB LEF/DEF import on ISPD18 test6.

The source LEF has one malformed Metal9 PRL row.  This runner applies the
same narrowly-scoped repair once in a private temporary directory, then passes
the repaired file to both backends.  It never changes the reference corpus.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile
import time
from typing import Any


DEFAULT_CORPUS = Path("/home/zhuxu1/workspace/reference/ispd2018/ispd18_test6")
MALFORMED_METAL9_PRL = "    WIDTH 4.5   1.50\n    WIDTH 1.5   0.45 ;"
REPAIRED_METAL9_PRL = "    WIDTH 4.5   1.50 ;"
COMPARABLE_COUNT_FIELDS = (
    "layers",
    "routing_layers",
    "cut_layers",
    "tech_vias",
    "sites",
    "masters",
    "master_terms",
    "rows",
    "track_grid_axis_patterns",
    "instances",
    "instance_pins",
    "io_pins",
    "regular_nets",
    "special_nets",
    "design_vias",
    "blockages",
)
DIFFERENTIAL_FILTER = "Ispd18/OpenDbCorpusDifferentialTest.MatchesOpenDbNormalizedImportAndExportSemantics/test6"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def repair_lef(source: Path, destination: Path) -> None:
    contents = source.read_text(encoding="utf-8")
    if contents.count(MALFORMED_METAL9_PRL) != 1:
        raise RuntimeError(f"expected exactly one malformed ISPD18 Metal9 PRL row in {source}")
    destination.write_text(contents.replace(MALFORMED_METAL9_PRL, REPAIRED_METAL9_PRL), encoding="utf-8")


def odb_counts(database: Any, library: Any, chip: Any) -> dict[str, int]:
    technology = database.getTech()
    block = chip.getBlock()
    layers = technology.getLayers()
    nets = block.getNets()
    return {
        "layers": len(layers),
        "routing_layers": sum(layer.getType() == "ROUTING" for layer in layers),
        "cut_layers": sum(layer.getType() == "CUT" for layer in layers),
        "tech_vias": len(technology.getVias()),
        "sites": len(library.getSites()),
        "masters": len(library.getMasters()),
        "master_terms": sum(len(master.getMTerms()) for master in library.getMasters()),
        "rows": len(block.getRows()),
        # dbTrackGrid merges the X/Y TRACKS statements for a layer.  Keep the
        # raw container count for transparency, and compare its axis-pattern
        # count to EccDB's verbatim DEF TRACKS representation instead.
        "track_grid_axis_patterns": sum(
            grid.getNumGridPatternsX() + grid.getNumGridPatternsY() for grid in block.getTrackGrids()
        ),
        "track_grids": len(block.getTrackGrids()),
        "instances": len(block.getInsts()),
        "instance_pins": len(block.getITerms()),
        "io_pins": len(block.getBTerms()),
        "regular_nets": sum(not net.isSpecial() for net in nets),
        "special_nets": sum(net.isSpecial() for net in nets),
        "design_vias": len(block.getVias()),
        "blockages": len(block.getBlockages()),
    }


def import_odb(odb_module: Any, lef: Path, def_file: Path) -> dict[str, Any]:
    database = odb_module.dbDatabase.create()
    lef_begin = time.perf_counter_ns()
    library = odb_module.read_lef(database, str(lef))
    lef_elapsed_ns = time.perf_counter_ns() - lef_begin
    if library is None or database.getTech() is None:
        raise RuntimeError("OpenDB failed to import LEF")

    def_begin = time.perf_counter_ns()
    chip = odb_module.read_def(database.getTech(), str(def_file))
    def_elapsed_ns = time.perf_counter_ns() - def_begin
    if chip is None or chip.getBlock() is None:
        raise RuntimeError("OpenDB failed to import DEF")

    counts = odb_counts(database, library, chip)
    odb_module.dbDatabase.destroy(database)

    return {
        "schema": "odb_import_measurement_v1",
        "backend": "odb",
        "lef_passes": 1,
        "lef_elapsed_ns": lef_elapsed_ns,
        "def_elapsed_ns": def_elapsed_ns,
        "total_elapsed_ns": lef_elapsed_ns + def_elapsed_ns,
        "counts": counts,
    }


def import_eccdb(executable: Path, lef: Path, def_file: Path, output: Path) -> dict[str, Any]:
    completed = subprocess.run(
        [str(executable), "--lef", str(lef), "--def", str(def_file), "--output", str(output)],
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            "EccDB import failed with exit code "
            f"{completed.returncode}:\nstdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    return json.loads(output.read_text(encoding="utf-8"))


def compare_counts(odb_measurement: dict[str, Any], eccdb_measurement: dict[str, Any]) -> dict[str, Any]:
    odb_result = odb_measurement["counts"]
    eccdb_result = eccdb_measurement["counts"]
    equal = {field: odb_result[field] for field in COMPARABLE_COUNT_FIELDS if odb_result[field] == eccdb_result[field]}
    mismatch = {
        field: {"odb": odb_result[field], "eccdb": eccdb_result[field]}
        for field in COMPARABLE_COUNT_FIELDS
        if odb_result[field] != eccdb_result[field]
    }
    return {"all_equal": not mismatch, "equal": equal, "mismatch": mismatch}


def summarize_measurements(measurements: list[dict[str, Any]]) -> dict[str, Any]:
    if not measurements:
        raise RuntimeError("at least one measurement is required")
    first_counts = measurements[0]["counts"]
    if any(measurement["counts"] != first_counts for measurement in measurements[1:]):
        raise RuntimeError("object counts changed between repeated imports")

    elapsed_fields = ("lef_elapsed_ns", "def_elapsed_ns", "total_elapsed_ns")
    median = {
        field: int(statistics.median([measurement[field] for measurement in measurements])) for field in elapsed_fields
    }
    # EccDB splits LEF import into technology and library parser passes; retain
    # their medians without pretending that OpenDB has the same decomposition.
    for field in ("lef_tech_elapsed_ns", "lef_library_elapsed_ns"):
        if field in measurements[0]:
            median[field] = int(statistics.median([measurement[field] for measurement in measurements]))
    return {"runs": measurements, "median": median, "counts": first_counts}


def run_differential(test_binary: Path, corpus_root: Path, odb_python: Path, odb_pythonpath: Path) -> dict[str, Any]:
    environment = os.environ.copy()
    environment["ECCDB_ISPD18_ROOT"] = str(corpus_root)
    environment["OPENDB_PYTHON"] = str(odb_python)
    environment["OPENDB_PYTHONPATH"] = str(odb_pythonpath)
    completed = subprocess.run(
        [str(test_binary), f"--gtest_filter={DIFFERENTIAL_FILTER}"],
        check=False,
        capture_output=True,
        text=True,
        env=environment,
    )
    return {
        "command": [str(test_binary), f"--gtest_filter={DIFFERENTIAL_FILTER}"],
        "exit_code": completed.returncode,
        "passed": completed.returncode == 0,
        "stdout": completed.stdout,
        "stderr": completed.stderr,
    }


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lef", type=Path, default=DEFAULT_CORPUS / "ispd18_test6.input.lef")
    parser.add_argument("--def", dest="def_file", type=Path, default=DEFAULT_CORPUS / "ispd18_test6.input.def")
    parser.add_argument("--eccdb", type=Path, required=True, help="path to eccdb_ispd18_import_benchmark")
    parser.add_argument("--output", type=Path, required=True, help="combined machine-readable JSON result")
    parser.add_argument("--repeat", type=int, default=5, help="positive odd number of fresh imports per backend (default: 5)")
    parser.add_argument(
        "--opendb-python",
        type=Path,
        default=Path(os.environ.get("OPENDB_PYTHON", "/home/zhuxu1/.local/bin/python3")),
        help="Python executable supplied to the existing OpenDB differential test",
    )
    parser.add_argument(
        "--opendb-pythonpath",
        type=Path,
        default=Path(os.environ.get("OPENDB_PYTHONPATH", "/home/zhuxu1/workspace/OpenROAD/bazel-bin/src/odb")),
        help="directory containing OpenDB odb.py and _odb.so",
    )
    parser.add_argument(
        "--differential-test",
        type=Path,
        help="optionally run eccdb_opendb_design_differential_test for the same corpus case after measurement",
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    if not arguments.lef.is_file() or not arguments.def_file.is_file():
        raise RuntimeError("--lef and --def must name regular files")
    if not arguments.eccdb.is_file():
        raise RuntimeError("--eccdb must name the built EccDB import benchmark")
    if arguments.repeat <= 0 or arguments.repeat % 2 == 0:
        raise RuntimeError("--repeat must be a positive odd integer so every median is an observed run")
    if not (arguments.opendb_pythonpath / "odb.py").is_file() or not (arguments.opendb_pythonpath / "_odb.so").is_file():
        raise RuntimeError("--opendb-pythonpath must contain odb.py and _odb.so")
    # Do this after parsing options so normal invocation does not rely on a
    # shell-level PYTHONPATH export.  The binding still calls native C++ ODB.
    sys.path.insert(0, str(arguments.opendb_pythonpath))
    import odb  # pylint: disable=import-outside-toplevel

    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="eccdb-ispd18-test6-") as temporary_directory:
        repaired_lef = Path(temporary_directory) / "ispd18_test6.repaired.lef"
        eccdb_output = Path(temporary_directory) / "eccdb.json"
        repair_lef(arguments.lef, repaired_lef)

        # Exclude first-call initialization and cold-cache effects, then
        # alternate order to keep a runtime drift from favoring one backend.
        import_odb(odb, repaired_lef, arguments.def_file)
        import_eccdb(arguments.eccdb, repaired_lef, arguments.def_file, eccdb_output)
        odb_runs: list[dict[str, Any]] = []
        eccdb_runs: list[dict[str, Any]] = []
        for index in range(arguments.repeat):
            if index % 2 == 0:
                odb_runs.append(import_odb(odb, repaired_lef, arguments.def_file))
                eccdb_runs.append(import_eccdb(arguments.eccdb, repaired_lef, arguments.def_file, eccdb_output))
            else:
                eccdb_runs.append(import_eccdb(arguments.eccdb, repaired_lef, arguments.def_file, eccdb_output))
                odb_runs.append(import_odb(odb, repaired_lef, arguments.def_file))
        odb_measurement = summarize_measurements(odb_runs)
        eccdb_measurement = summarize_measurements(eccdb_runs)

        count_comparison = compare_counts(odb_measurement, eccdb_measurement)
        result: dict[str, Any] = {
            "schema": "eccdb_odb_ispd18_import_comparison_v1",
            "case": "ispd18_test6",
            "repeat": arguments.repeat,
            "warmup_pairs": 1,
            "run_order": "alternating ODB/EccDB and EccDB/ODB with fresh database objects",
            "inputs": {
                "source_lef": str(arguments.lef.resolve()),
                "source_lef_sha256": sha256(arguments.lef),
                "effective_lef_sha256": sha256(repaired_lef),
                "def": str(arguments.def_file.resolve()),
                "def_sha256": sha256(arguments.def_file),
                "repair": {
                    "kind": "remove_malformed_metal9_prl_row",
                    "source_match_count": 1,
                    "temporary": True,
                },
                "timing_scope": "native odb.read_lef/read_def or EccDB importer calls only; process and binding setup excluded",
            },
            "odb": odb_measurement,
            "eccdb": eccdb_measurement,
            "count_comparison": count_comparison,
            "correctness": {
                "differential_filter": DIFFERENTIAL_FILTER,
                "corpus_root": str(arguments.lef.resolve().parent.parent),
                "opendb_python": str(arguments.opendb_python),
                "opendb_pythonpath": str(arguments.opendb_pythonpath),
            },
        }
        if arguments.differential_test is not None:
            result["correctness"]["differential"] = run_differential(
                arguments.differential_test,
                arguments.lef.resolve().parent.parent,
                arguments.opendb_python,
                arguments.opendb_pythonpath,
            )

        arguments.output.write_text(json.dumps(result, sort_keys=True, indent=2) + "\n", encoding="utf-8")

    print(f"wrote {arguments.output}")
    if not result["count_comparison"]["all_equal"]:
        return 2
    if arguments.differential_test is not None and not result["correctness"]["differential"]["passed"]:
        return 3
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError, json.JSONDecodeError) as error:
        print(f"IspdImportComparison.py: {error}", file=sys.stderr)
        raise SystemExit(1)
