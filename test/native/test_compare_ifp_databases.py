# SPDX-License-Identifier: MulanPSL-2.0
"""Unit tests for the adaptor-boundary iFP database comparison report."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).with_name("compare_ifp_databases.py")
SPEC = importlib.util.spec_from_file_location("compare_ifp_databases_under_test", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
compare = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = compare
SPEC.loader.exec_module(compare)


def _model(*, instances: list[dict[str, object]] | None = None,
           nets: list[dict[str, object]] | None = None,
           schema: int = 2) -> dict[str, object]:
    instances = instances if instances is not None else [
        {"name": "u0", "origin": [0, 0], "pin_shapes": []},
        {"name": "u1", "origin": [10, 0], "pin_shapes": []},
    ]
    nets = nets if nets is not None else [{"name": "N", "pins": []}]
    return {
        "snapshot_schema": schema,
        "database_info": {"design_name": "unit"},
        "index_maps": {
            "instances": {value["name"]: index for index, value in enumerate(instances)},
            "routing_layers": {},
            "io_pins": {},
            "pg_nets": {},
        },
        "sites": [], "masters": [], "routing_layers": [], "tracks": [], "rows": [], "new_rows": [],
        "instances": instances, "io_pins": [], "nets": nets, "pg_nets": [], "pg_segments": [],
    }


class IfpDatabaseDiffTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name) / "pair"
        for backend in compare.BACKENDS:
            (self.root / backend).mkdir(parents=True)

    def tearDown(self) -> None:
        self.temp.cleanup()

    def _write(self, backend: str, stage: str, value: dict[str, object]) -> None:
        (self.root / backend / stage).write_text(json.dumps(value), encoding="utf-8")

    def _write_required(self, left: dict[str, object], right: dict[str, object]) -> None:
        for filename in compare.REQUIRED_STAGES.values():
            self._write("idb", filename, left)
            self._write("eccdb", filename, right)

    def test_order_only_lists_and_self_consistent_ordinals_pass(self) -> None:
        left = _model()
        right = _model(instances=list(reversed(left["instances"])))
        self._write_required(left, right)

        report = compare.build_report(self.root)

        self.assertTrue(report["full_database_pass"])
        self.assertFalse(report["strict_equal"])
        self.assertTrue(report["values_equal"])
        self.assertEqual(report["stages"]["input"]["sections"]["instances"]["classification"], "order_only")
        self.assertEqual(report["stages"]["input"]["sections"]["index_maps"]["classification"], "order_only")
        self.assertEqual(compare._exit_code(report), 0)

    def test_same_exact_name_field_change_is_a_value_difference(self) -> None:
        left = _model()
        right = _model(instances=[
            {"name": "u0", "origin": [1, 0], "pin_shapes": []},
            {"name": "u1", "origin": [10, 0], "pin_shapes": []},
        ])
        self._write_required(left, right)

        report = compare.build_report(self.root)
        named = report["stages"]["input"]["sections"]["instances"]["named_objects"]

        self.assertFalse(report["full_database_pass"])
        self.assertEqual(report["stages"]["input"]["sections"]["instances"]["classification"], "value_difference")
        self.assertIn("origin", named["changed_field_counts"])
        self.assertEqual(compare._exit_code(report), 1)

    def test_coordinate_tuple_order_is_not_treated_as_a_collection_reorder(self) -> None:
        left = _model(instances=[{"name": "u0", "origin": [1, 2], "pin_shapes": []}])
        right = _model(instances=[{"name": "u0", "origin": [2, 1], "pin_shapes": []}])
        self._write_required(left, right)

        report = compare.build_report(self.root)
        named = report["stages"]["input"]["sections"]["instances"]["named_objects"]

        self.assertFalse(report["full_database_pass"])
        self.assertEqual(report["stages"]["input"]["sections"]["instances"]["classification"], "value_difference")
        self.assertEqual(named["field_samples"][0]["path"], "origin")
        self.assertEqual(named["field_samples"][0]["classification"], "value_difference")

    def test_nested_pin_collections_keep_order_only_and_duplicate_information(self) -> None:
        pins = [
            {"pin": "A", "layer": "M1", "bounds": [0, 0, 1, 1]},
            {"pin": "A", "layer": "M1", "bounds": [0, 0, 1, 1]},
            {"pin": "B", "layer": "M2", "bounds": [2, 0, 3, 1]},
        ]
        left = _model(
            instances=[{"name": "u0", "origin": [0, 0], "pin_shapes": pins}],
            nets=[{"name": "N", "pins": pins}],
        )
        right = _model(
            instances=[{"name": "u0", "origin": [0, 0], "pin_shapes": list(reversed(pins))}],
            nets=[{"name": "N", "pins": list(reversed(pins))}],
        )
        self._write_required(left, right)

        report = compare.build_report(self.root)
        instance_named = report["stages"]["input"]["sections"]["instances"]["named_objects"]
        net_named = report["stages"]["input"]["sections"]["nets"]["named_objects"]

        self.assertTrue(report["full_database_pass"])
        self.assertEqual(instance_named["nested_collection_diagnostics"]["order_only"]["pin_shapes"], 1)
        self.assertEqual(net_named["nested_collection_diagnostics"]["order_only"]["pins"], 1)

    def test_escaped_names_are_not_normalized(self) -> None:
        left = _model(instances=[{"name": "\\bus[0] ", "origin": [0, 0], "pin_shapes": []}])
        right = _model(instances=[{"name": "bus[0]", "origin": [0, 0], "pin_shapes": []}])
        self._write_required(left, right)

        report = compare.build_report(self.root)
        named = report["stages"]["input"]["sections"]["instances"]["named_objects"]

        self.assertFalse(report["full_database_pass"])
        self.assertEqual(named["only_left_name_sample"], ["\\bus[0] "])
        self.assertEqual(named["only_right_name_sample"], ["bus[0]"])

    def test_stale_index_is_an_invariant_failure_not_an_association_match(self) -> None:
        model = _model()
        model["index_maps"]["instances"]["u0"] = 1
        self._write_required(model, model)

        report = compare.build_report(self.root)
        invariant = report["stages"]["input"]["index_invariants"]["idb"]

        self.assertFalse(report["full_database_pass"])
        self.assertFalse(invariant["consistent"])
        self.assertTrue(invariant["maps"]["instances"]["invalid_entry_sample"])
        self.assertEqual(compare._exit_code(report), 1)

    def test_same_name_old_and_new_records_are_reported_without_false_stale_index_failure(self) -> None:
        # iFP retains both an old projection and a new replacement.  The
        # name-to-index map can only point to the new (last) same-name entry.
        records = [
            {"name": "CAP", "origin": [0, 0], "new_instance": False, "pin_shapes": []},
            {"name": "CAP", "origin": [10, 0], "new_instance": True, "pin_shapes": []},
        ]
        model = _model(instances=records)
        self._write_required(model, model)

        report = compare.build_report(self.root)
        instance_map = report["stages"]["input"]["index_invariants"]["idb"]["maps"]["instances"]

        self.assertTrue(report["full_database_pass"])
        self.assertTrue(instance_map["entry_targets_valid"])
        self.assertFalse(instance_map["unique_name_valid"])
        self.assertEqual(instance_map["duplicate_name_count"], 1)
        self.assertEqual(instance_map["map_count"], 1)

    def test_missing_required_or_partial_schema_is_unavailable_coverage(self) -> None:
        model = _model()
        self._write("idb", "input-model.json", model)
        self._write("eccdb", "input-model.json", model)
        self._write("idb", "computed-model.json", _model(schema=1))
        self._write("eccdb", "computed-model.json", _model(schema=1))

        report = compare.build_report(self.root)
        self.assertFalse(report["coverage_complete"])
        self.assertEqual(report["stages"]["computed"]["status"], "partial_schema")
        self.assertEqual(compare._exit_code(report), 2)

        (self.root / "eccdb" / "computed-model.json").unlink()
        missing = compare.build_report(self.root)
        self.assertEqual(missing["stages"]["computed"]["status"], "unavailable")
        self.assertEqual(compare._exit_code(missing), 2)

    def test_schema_two_still_requires_every_schema_two_top_level_section(self) -> None:
        left = _model()
        right = _model()
        right.pop("pg_segments")
        self._write_required(left, right)

        report = compare.build_report(self.root)
        schema = report["stages"]["input"]["schema"]

        self.assertFalse(report["coverage_complete"])
        self.assertIn("pg_segments", schema["eccdb_missing_top_level_sections"])
        self.assertEqual(compare._exit_code(report), 2)


if __name__ == "__main__":
    unittest.main()
