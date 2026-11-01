# SPDX-License-Identifier: MulanPSL-2.0
"""Focused tests for strict iLVS report comparison semantics."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).with_name("compare_ilvs_reports.py")
SPEC = importlib.util.spec_from_file_location("compare_ilvs_reports_under_test", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
compare = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = compare
SPEC.loader.exec_module(compare)


class CompareIlvsReportsTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.def_file = self.root / "unit.def"
        self.lef_file = self.root / "unit.lef"
        self.verilog_file = self.root / "unit.v"
        for path in (self.def_file, self.lef_file, self.verilog_file):
            path.write_text(path.name + "\n", encoding="utf-8")
        self.inputs = {
            "lefs": [str(self.lef_file)],
            "def": str(self.def_file),
            "verilog": str(self.verilog_file),
            "top": "unit",
            "options": {"-thread_number": 1},
        }

    def tearDown(self) -> None:
        self.temp.cleanup()

    def _write_backend(self, name: str, violations: list[dict[str, object]], mapping: dict[str, str], *,
                       input_override: str | None = None, top_override: str | None = None) -> Path:
        root = self.root / name
        (root / "lvs_reporter").mkdir(parents=True)
        (root / "lvs_reporter" / "ilvs.json").write_text(json.dumps({
            "entity": [{"entity": "Net", "netlist": 1, "def": 1, "difference": 0}],
            "connectivity": [{"connectivity": "Routing", "total": 1, "open": {"count": 0}}],
            "violations": violations,
        }), encoding="utf-8")
        inputs = {
            str(self.lef_file.resolve()): compare._digest(self.lef_file),
            str(self.def_file.resolve()): compare._digest(self.def_file),
            str(self.verilog_file.resolve()): compare._digest(self.verilog_file),
        }
        if input_override is not None:
            inputs[str(self.def_file.resolve())] = input_override
        (root / "metadata.json").write_text(json.dumps({
            "inputs": inputs,
            "options": self.inputs["options"],
            "top": "unit" if top_override is None else top_override,
            "module_sha256": "same-module",
            "status": "passed",
            "wall_seconds": 1.25,
            "peak_rss_kib": 42,
        }), encoding="utf-8")
        (root / "stage.json").write_text(json.dumps({"stage": {"elapsed_ms": 100.0}}), encoding="utf-8")
        (root / "lvs_layer_mapping.json").write_text(json.dumps({"layer_id_to_name": mapping}), encoding="utf-8")
        return root

    @staticmethod
    def _violation(layer: int, label: str = "N") -> dict[str, object]:
        return {"type": "RoutingOpen", "net": label, "terminals": ["u/A"], "components": [],
                "shapes": [{"layer": layer, "rect": [0, 0, 1, 1]}]}

    def test_layer_names_not_numeric_ids_decide_semantic_equality(self) -> None:
        eccdb = self._write_backend("eccdb", [self._violation(101)], {"101": "M1"})
        idb = self._write_backend("idb", [self._violation(0)], {"0": "M1"})
        report = compare.build_report(eccdb, idb, self.inputs)
        self.assertFalse(report["comparison"]["violations_raw"]["equal"])
        self.assertTrue(report["comparison"]["violations_layer_name_normalized"]["equal"])
        self.assertTrue(report["comparison"]["strict_semantic_equal"])

        (idb / "lvs_layer_mapping.json").write_text(json.dumps({"layer_id_to_name": {"0": "M2"}}), encoding="utf-8")
        changed = compare.build_report(eccdb, idb, self.inputs)
        self.assertFalse(changed["comparison"]["violations_layer_name_normalized"]["equal"])
        self.assertFalse(changed["comparison"]["strict_semantic_equal"])

    def test_violation_order_is_ignored_but_duplicates_are_retained(self) -> None:
        first = self._violation(4, "A")
        second = self._violation(4, "B")
        eccdb = self._write_backend("eccdb", [first, second, first], {"4": "M1"})
        idb = self._write_backend("idb", [first, first, second], {"4": "M1"})
        report = compare.build_report(eccdb, idb, self.inputs)
        self.assertTrue(report["comparison"]["violations_raw"]["equal"])
        self.assertTrue(report["comparison"]["strict_semantic_equal"])

        idb = self._write_backend("idb_missing_duplicate", [first, second], {"4": "M1"})
        missing = compare.build_report(eccdb, idb, self.inputs)
        self.assertFalse(missing["comparison"]["violations_raw"]["equal"])
        self.assertEqual(missing["comparison"]["violations_raw"]["eccdb_only_count"], 1)

    def test_input_hash_mismatch_blocks_strict_result(self) -> None:
        eccdb = self._write_backend("eccdb", [], {})
        idb = self._write_backend("idb", [], {}, input_override="wrong-hash", top_override="wrong_top")
        report = compare.build_report(eccdb, idb, self.inputs)
        self.assertFalse(report["input_validation"]["complete"])
        self.assertEqual(report["input_validation"]["idb"]["top_state"], "mismatched")
        self.assertFalse(report["comparison"]["strict_semantic_equal"])
        self.assertEqual(report["comparison"]["status"], "inconclusive")

    def test_component_geometry_fingerprints_preserve_grouping(self) -> None:
        def grouped(component_a: int, component_b: int, first: list[list[int]], second: list[list[int]]) -> dict[str, object]:
            shapes = [
                {"component": component_a, "layer": 4, "rect": rect}
                for rect in first
            ] + [
                {"component": component_b, "layer": 4, "rect": rect}
                for rect in second
            ]
            return {"type": "RoutingShort", "net": "N", "terminals": [],
                    "components": [component_a, component_b], "shapes": shapes}

        # Both sides contain A/B/C rectangles.  The grouping is deliberately
        # different, so erasing internal component IDs would be a false pass.
        eccdb = self._write_backend("eccdb", [grouped(101, 102, [[0, 0, 1, 1], [2, 0, 3, 1]], [[4, 0, 5, 1]])], {"4": "M1"})
        idb = self._write_backend("idb", [grouped(7, 8, [[0, 0, 1, 1]], [[2, 0, 3, 1], [4, 0, 5, 1]])], {"4": "M1"})
        report = compare.build_report(eccdb, idb, self.inputs)
        self.assertFalse(report["comparison"]["violations_layer_name_normalized"]["equal"])
        self.assertFalse(report["comparison"]["strict_semantic_equal"])


if __name__ == "__main__":
    unittest.main()
