#!/usr/bin/env python3
# SPDX-License-Identifier: MulanPSL-2.0
"""Mutation tests for the iPW iDB/EccDB differential comparator.

These are deliberately pure-Python fixtures.  They prove that a comparison
cannot silently pass after an input endpoint, full-Liberty fingerprint, power
value, instance row, or finite-value invariant changes.
"""

from __future__ import annotations

import copy
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


_MODULE_PATH = Path(__file__).with_name("run_ipw_pair_differential.py")
_SPEC = importlib.util.spec_from_file_location("run_ipw_pair_differential", _MODULE_PATH)
assert _SPEC is not None and _SPEC.loader is not None
ipw_pair = importlib.util.module_from_spec(_SPEC)
sys.modules[_SPEC.name] = ipw_pair
_SPEC.loader.exec_module(ipw_pair)


def _power(internal: float, switching: float, leakage: float) -> dict[str, float]:
    return {
        "internal_w": internal,
        "switching_w": switching,
        "leakage_w": leakage,
        "total_w": internal + switching + leakage,
    }


def _input_model() -> dict:
    return {
        "snapshot_schema": 1,
        "design_name": "unit",
        "instances": {
            "u0": {
                "cell": "CELL",
                "pins": ["u0/A"],
                "clock_gating": False,
                "clock_pin": "",
                "data_pin": "",
                "io_cell": False,
                "output_pin": "Y",
                "sequential": False,
            },
        },
        "pins": {
            "u0/A": {"instance": "u0", "pin": "A", "net": "N", "is_port": False},
        },
        "nets": {
            "N": {"pins": ["u0/A"], "drivers": ["u0/A"], "loads": []},
        },
        "vcd_activity": {},
        "timing_library": {
            "cell_count": 1,
            "table_count": 2,
            "table_value_count": 8,
            "all_table_payloads_hashed": True,
            "semantic_fingerprint_fnv1a64": "table-payload-fingerprint-a",
            "cells": {
                "CELL": {
                    "ports": 2,
                    "timing_arcs": 1,
                    "power_arcs": 1,
                    "leakage_entries": 1,
                    "sequential_entries": 0,
                    "table_count": 2,
                    "table_value_count": 8,
                    "semantic_fingerprint_fnv1a64": "cell-table-payload-a",
                },
            },
        },
        "timing_constraints": {},
        "parasitics": {},
        "counts": {"instances": 1, "pins": 1, "nets": 1, "vcd_activity": 0, "parasitic_nets": 0},
    }


def _power_model() -> dict:
    first = _power(0.1, 0.2, 0.3)
    second = _power(0.4, 0.5, 0.6)
    total = _power(0.5, 0.7, 0.9)
    return {
        "instance_power": {
            "u0": {"group": 1, "voltage_v": 1.2, "power": first},
            "u1": {"group": 1, "voltage_v": 1.2, "power": second},
        },
        "net_power": {"N": _power(0.01, 0.02, 0.03)},
        "power_activity": {"N": {"toggle_rate": 0.5}},
        "summary_total": total,
        "summary_groups": {"1": total},
        "counts": {"instance_power": 2, "net_power": 1, "power_activity": 1},
    }


class IpwPairComparatorMutationTest(unittest.TestCase):
    def test_baselines_are_valid_and_equal(self) -> None:
        source = _input_model()
        power = _power_model()
        self.assertTrue(ipw_pair.validate_input_model(source)["valid"])
        self.assertTrue(ipw_pair.validate_power_model(power)["valid"])
        self.assertTrue(ipw_pair.compare_input_models(source, copy.deepcopy(source))["comparison"]["passed"])
        self.assertTrue(ipw_pair.compare_power_models(power, copy.deepcopy(power))["comparison"]["passed"])

    def test_net_endpoint_mutation_fails_raw_comparison(self) -> None:
        idb = _input_model()
        eccdb = copy.deepcopy(idb)
        eccdb["nets"]["N"]["pins"] = ["u0/B"]
        comparison = ipw_pair.compare_input_models(idb, eccdb)
        self.assertFalse(comparison["comparison"]["passed"])
        self.assertGreater(comparison["comparison"]["value_mismatches"], 0)

    def test_table_payload_fingerprint_mutation_fails_raw_comparison(self) -> None:
        idb = _input_model()
        eccdb = copy.deepcopy(idb)
        eccdb["timing_library"]["cells"]["CELL"]["semantic_fingerprint_fnv1a64"] = "changed-table-values"
        comparison = ipw_pair.compare_input_models(idb, eccdb)
        self.assertFalse(comparison["comparison"]["passed"])

    def test_power_value_mutation_fails_even_when_accounting_is_repaired(self) -> None:
        idb = _power_model()
        eccdb = copy.deepcopy(idb)
        eccdb["instance_power"]["u0"]["power"] = _power(0.11, 0.2, 0.3)
        updated_total = _power(0.51, 0.7, 0.9)
        eccdb["summary_total"] = updated_total
        eccdb["summary_groups"] = {"1": updated_total}
        self.assertTrue(ipw_pair.validate_power_model(eccdb)["valid"])
        self.assertFalse(ipw_pair.compare_power_models(idb, eccdb)["comparison"]["passed"])

    def test_missing_instance_row_fails_even_when_remaining_totals_are_valid(self) -> None:
        idb = _power_model()
        eccdb = copy.deepcopy(idb)
        del eccdb["instance_power"]["u1"]
        eccdb["counts"]["instance_power"] = 1
        remaining = _power(0.1, 0.2, 0.3)
        eccdb["summary_total"] = remaining
        eccdb["summary_groups"] = {"1": remaining}
        self.assertTrue(ipw_pair.validate_power_model(eccdb)["valid"])
        comparison = ipw_pair.compare_power_models(idb, eccdb)
        self.assertFalse(comparison["comparison"]["passed"])
        self.assertGreater(comparison["comparison"]["exact_identifier_differences"], 0)

    def test_nan_is_never_accepted(self) -> None:
        power = _power_model()
        power["instance_power"]["u0"]["power"]["internal_w"] = float("nan")
        self.assertFalse(ipw_pair.validate_power_model(power)["valid"])
        comparison = ipw_pair.compare_power_models(_power_model(), power)
        self.assertFalse(comparison["comparison"]["passed"])
        self.assertGreater(comparison["comparison"]["nonfinite_values"], 0)

    def test_physical_only_missing_library_cell_is_reported_but_not_hidden_or_failed(self) -> None:
        source = _input_model()
        source["instances"]["tap"] = {"cell": "FILLTAP", "pins": []}
        source["counts"]["instances"] = 2
        validation = ipw_pair.validate_input_model(source)
        self.assertTrue(validation["valid"])
        coverage = validation["library_coverage"]
        self.assertEqual(coverage["missing_library_physical_only_instance_count"], 1)
        self.assertEqual(coverage["missing_library_signal_bearing_instance_count"], 0)

    def test_signal_bearing_missing_library_cell_fails_validation(self) -> None:
        source = _input_model()
        source["instances"]["bad"] = {"cell": "UNKNOWN", "pins": ["bad/A"]}
        source["counts"]["instances"] = 2
        validation = ipw_pair.validate_input_model(source)
        self.assertFalse(validation["valid"])
        self.assertEqual(validation["library_coverage"]["missing_library_signal_bearing_instance_count"], 1)

    def test_worker_path_symlink_alias_is_explicit_but_accepted(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "target.lef"
            target.write_text("LEF", encoding="utf-8")
            alias = root / "current.lef"
            alias.symlink_to(target)
            spec = {
                "lefs": [target],
                "liberty": [target],
                "def": target,
                "sdc": None,
                "spef": None,
                "vcd": None,
            }
            result = {
                "status": "passed",
                "input": {
                    "lef_files": [str(alias)],
                    "liberty_files": [str(alias)],
                    "def_file": str(alias),
                    "sdc_file": "",
                    "spef_file": "",
                    "vcd_file": "",
                },
            }
            match = ipw_pair._worker_input_matches(result, spec)
            self.assertTrue(match["valid"])
            self.assertIn("lef_files", match["path_aliases"])
            self.assertIn("def_file", match["path_aliases"])


if __name__ == "__main__":
    unittest.main()
