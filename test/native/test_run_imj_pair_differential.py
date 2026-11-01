#!/usr/bin/env python3
"""Focused regression tests for the iMJ differential semantic comparator."""

from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).with_name("run_imj_pair_differential.py")
SPEC = importlib.util.spec_from_file_location("run_imj_pair_differential", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
IMJ = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = IMJ
SPEC.loader.exec_module(IMJ)


def design(components: str, fills: str = "", nets: str = "", specialnets: str = "") -> str:
    return f"""VERSION 5.8 ;
COMPONENTS 1 ;
{components}
END COMPONENTS
FILLS 1 ;
{fills}
END FILLS
NETS 1 ;
{nets}
END NETS
SPECIALNETS 1 ;
{specialnets}
END SPECIALNETS
END DESIGN
"""


class ImjComparatorTest(unittest.TestCase):
    def snapshot(self, text: str):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "case.def"
            path.write_text(text, encoding="utf-8")
            return IMJ.snapshot_def(path)

    @staticmethod
    def valid_acceptance_context(*, input_physical_components_equal: bool = True) -> dict[str, object]:
        return {
            "worker_invocations": {"idb": {"valid": True}, "eccdb": {"valid": True}},
            "provenance_validation": {"valid": True},
            "input_physical_components_equal": input_physical_components_equal,
        }

    def functionality_report(self, stage: str, *, idb_effect: bool, eccdb_effect: bool) -> dict[str, object]:
        if stage == "filler":
            comparison = {"strict_raw_name_equal": True, "new_filler_pg_physical_equal": True}
        else:
            self.assertEqual(stage, "metal")
            comparison = {
                "unsupported_fill_syntax": {"idb_count": 0, "eccdb_count": 0},
                "strict_raw_layer_rectangle_mask_opc_equal": True,
            }
        return {
            **self.valid_acceptance_context(),
            "stage": stage,
            "comparison_status": "complete",
            "stage_comparison": comparison,
            "idb_integrity": {
                "no_unintended_design_mutation": True,
                "intended_effect_present": idb_effect,
            },
            "eccdb_integrity": {
                "no_unintended_design_mutation": True,
                "intended_effect_present": eccdb_effect,
            },
        }

    def test_filler_escaped_name_remains_strict_difference(self) -> None:
        before = self.snapshot(design("- U0 CORE + PLACED ( 0 0 ) N ;"))
        idb_after = self.snapshot(design("""
- U0 CORE + PLACED ( 0 0 ) N ;
- fill\\[0\\] FILL + PLACED ( 10 0 ) N + SOURCE DIST ;"""))
        ecc_after = self.snapshot(design("""
- U0 CORE + PLACED ( 0 0 ) N ;
- fill[0] FILL + PLACED ( 10 0 ) N + SOURCE DIST ;"""))
        comparison = IMJ.compare_filler(before, idb_after, before, ecc_after)
        self.assertFalse(comparison["strict_raw_name_equal"])
        self.assertTrue(comparison["physical_projection_equal"])

    def test_existing_component_move_is_never_treated_as_filler_addition(self) -> None:
        before = self.snapshot(design("- U0 CORE + PLACED ( 0 0 ) N ;"))
        after = self.snapshot(design("""
- U0 CORE + PLACED ( 1 0 ) N ;
- FILL_0 FILL + PLACED ( 10 0 ) N + SOURCE DIST ;"""))
        integrity = IMJ.stage_integrity("filler", before, after)
        self.assertFalse(integrity["no_unintended_design_mutation"])
        self.assertEqual(integrity["components"]["changed_count"], 1)

    def test_metal_mask_and_opc_are_part_of_complete_key(self) -> None:
        before = self.snapshot(design("- U0 CORE + PLACED ( 0 0 ) N ;"))
        idb_after = self.snapshot(design("- U0 CORE + PLACED ( 0 0 ) N ;", "- LAYER M1 + MASK 1 + OPC RECT ( 0 0 ) ( 10 10 ) ;"))
        ecc_after = self.snapshot(design("- U0 CORE + PLACED ( 0 0 ) N ;", "- LAYER M1 + MASK 2 + OPC RECT ( 0 0 ) ( 10 10 ) ;"))
        comparison = IMJ.compare_metal(before, idb_after, before, ecc_after)
        self.assertFalse(comparison["strict_raw_layer_rectangle_mask_opc_equal"])
        self.assertEqual(comparison["only_idb"]["count"], 1)
        self.assertEqual(comparison["only_eccdb"]["count"], 1)

    def test_complete_counter_detects_same_count_different_rectangle(self) -> None:
        before = self.snapshot(design("- U0 CORE + PLACED ( 0 0 ) N ;"))
        idb_after = self.snapshot(design("- U0 CORE + PLACED ( 0 0 ) N ;", "- LAYER M2 RECT ( 0 0 ) ( 10 10 ) ;"))
        ecc_after = self.snapshot(design("- U0 CORE + PLACED ( 0 0 ) N ;", "- LAYER M2 RECT ( 1 0 ) ( 11 10 ) ;"))
        comparison = IMJ.compare_metal(before, idb_after, before, ecc_after)
        self.assertEqual(comparison["idb_added"]["count"], comparison["eccdb_added"]["count"])
        self.assertFalse(comparison["strict_raw_layer_rectangle_mask_opc_equal"])

    def test_antenna_empty_legacy_summary_is_not_a_pass(self) -> None:
        comparison = IMJ.antenna_comparison(
            {"antenna": {"checked": False, "violation_count": 0}},
            {"antenna": {"checked": True, "violation_count": 0, "violations": []}},
        )
        self.assertFalse(comparison["comparable"])
        self.assertFalse(comparison["coverage_complete"])
        self.assertFalse(comparison["result_equal"])
        self.assertIn("not a pass", comparison["reason"])

    def test_antenna_actual_results_ignore_backend_only_diagnostics(self) -> None:
        idb = {
            "antenna": {
                "checked": True,
                "coverage_complete": True,
                "net_count": 2,
                "violation_count": 1,
                "violations": [r"N\[0\]: M1 PAR 2.0 > 1.0"],
                "wire_count": 14,
                "nets_without_gate": 1,
                "coverage_diagnostics": ["legacy gate lookup diagnostic"],
            }
        }
        eccdb = {
            "antenna": {
                "checked": True,
                "coverage_complete": True,
                "net_count": 2,
                "violation_count": 1,
                "violations": [r"N\[0\]: M1 PAR 2.0 > 1.0"],
                "wire_count": 9,
                "nets_without_gate": 0,
                "coverage_diagnostics": ["native traversal diagnostic"],
            }
        }
        detail = IMJ.antenna_comparison(idb, eccdb)
        self.assertTrue(detail["coverage_complete"])
        self.assertFalse(detail["strict_raw_equal"])
        self.assertTrue(detail["violation_count_equal"])
        self.assertTrue(detail["net_count_equal"])
        self.assertTrue(detail["complete_violations_equal"])
        self.assertTrue(detail["result_equal"])
        report = {
            **self.valid_acceptance_context(),
            "stage": "antenna",
            "comparison_status": "complete",
            "stage_comparison": detail,
            "idb_integrity": {"no_unintended_design_mutation": True},
            "eccdb_integrity": {"no_unintended_design_mutation": True},
        }
        self.assertEqual(IMJ.acceptance(report), (True, True))

    def test_antenna_result_requires_a_complete_unsampled_violation_vector(self) -> None:
        truncated = {
            "checked": True,
            "coverage_complete": True,
            "net_count": 2,
            "violation_count": 2,
            "violations": ["N0: M1 PAR 2.0 > 1.0"],
        }
        detail = IMJ.antenna_comparison({"antenna": truncated}, {"antenna": truncated})
        self.assertTrue(detail["violation_count_equal"])
        self.assertTrue(detail["net_count_equal"])
        self.assertFalse(detail["idb_complete_violation_vector"])
        self.assertFalse(detail["complete_violations_equal"])
        self.assertFalse(detail["result_equal"])

    def test_antenna_result_accepts_reordered_complete_violation_vector(self) -> None:
        common = {
            "checked": True,
            "coverage_complete": True,
            "net_count": 2,
            "violation_count": 2,
        }
        idb = {"antenna": {**common, "violations": ["N0: M1 PAR 3 > 2", "N1: M1 PAR 4 > 2"]}}
        eccdb = {"antenna": {**common, "violations": ["N1: M1 PAR 4 > 2", "N0: M1 PAR 3 > 2"]}}
        detail = IMJ.antenna_comparison(idb, eccdb)
        self.assertFalse(detail["strict_raw_equal"])
        self.assertTrue(detail["complete_violations_equal"])
        self.assertTrue(detail["result_equal"])

    def test_antenna_result_keeps_raw_violation_names_strict(self) -> None:
        idb = {
            "antenna": {
                "checked": True,
                "coverage_complete": True,
                "net_count": 1,
                "violation_count": 1,
                "violations": [r"N\[0\]: M1 PAR 2.0 > 1.0"],
            }
        }
        eccdb = {
            "antenna": {
                "checked": True,
                "coverage_complete": True,
                "net_count": 1,
                "violation_count": 1,
                "violations": ["N[0]: M1 PAR 2.0 > 1.0"],
            }
        }
        detail = IMJ.antenna_comparison(idb, eccdb)
        self.assertTrue(detail["strict_raw_equal_includes_raw_violation_names"])
        self.assertFalse(detail["complete_violations_equal"])
        self.assertFalse(detail["result_equal"])

    def test_route_change_is_detected_even_without_geometry_dump(self) -> None:
        before = self.snapshot(design("- U0 CORE + PLACED ( 0 0 ) N ;", nets="- N0 ( U0 A ) + ROUTED M1 ( 0 0 ) ( 10 0 ) ;"))
        after = self.snapshot(design("- U0 CORE + PLACED ( 0 0 ) N ;", nets="- N0 ( U0 A ) + ROUTED M1 ( 0 0 ) ( 20 0 ) ;"))
        integrity = IMJ.stage_integrity("metal", before, after)
        self.assertFalse(integrity["routes_unchanged_raw"])
        self.assertFalse(integrity["no_unintended_design_mutation"])

    def test_metal_component_addition_is_an_unintended_mutation(self) -> None:
        before = self.snapshot(design("- U0 CORE + PLACED ( 0 0 ) N ;"))
        after = self.snapshot(design("""
- U0 CORE + PLACED ( 0 0 ) N ;
- UNEXPECTED BUF + PLACED ( 10 0 ) N ;"""))
        integrity = IMJ.stage_integrity("metal", before, after)
        self.assertEqual(integrity["components"]["added_count"], 1)
        self.assertFalse(integrity["no_unintended_design_mutation"])

    def test_antenna_component_addition_is_an_unintended_mutation(self) -> None:
        before = self.snapshot(design("- U0 CORE + PLACED ( 0 0 ) N ;"))
        after = self.snapshot(design("""
- U0 CORE + PLACED ( 0 0 ) N ;
- UNEXPECTED BUF + PLACED ( 10 0 ) N ;"""))
        integrity = IMJ.stage_integrity("antenna", before, after)
        self.assertEqual(integrity["components"]["added_count"], 1)
        self.assertFalse(integrity["no_unintended_design_mutation"])

    def test_unsupported_fill_syntax_is_reported_not_dropped(self) -> None:
        snapshot = self.snapshot(design("- U0 CORE + PLACED ( 0 0 ) N ;", "- LAYER M1 VIA V1 ( 0 0 ) ;"))
        self.assertEqual(len(snapshot.unsupported_fill_records), 1)

    def test_blank_lines_before_section_are_not_design_mutations(self) -> None:
        before = self.snapshot(design("- U0 CORE + PLACED ( 0 0 ) N ;", specialnets="- VDD + USE POWER ;"))
        after = self.snapshot("\n\n" + design("- U0 CORE + PLACED ( 0 0 ) N ;", specialnets="- VDD + USE POWER ;").replace("SPECIALNETS 1", "\n\nSPECIALNETS 1"))
        self.assertTrue(IMJ.stage_integrity("metal", before, after)["routes_unchanged_raw"])

    def test_equal_incomplete_antenna_reports_do_not_pass_acceptance(self) -> None:
        value = {"checked": True, "coverage_complete": False, "violation_count": 0}
        detail = IMJ.antenna_comparison({"antenna": value}, {"antenna": value})
        report = {**self.valid_acceptance_context(), "stage": "antenna", "comparison_status": "complete", "stage_comparison": detail,
                  "idb_integrity": {"no_unintended_design_mutation": True},
                  "eccdb_integrity": {"no_unintended_design_mutation": True}}
        self.assertEqual(IMJ.acceptance(report), (False, False))

    def test_physical_input_difference_blocks_differential_pass(self) -> None:
        report = {
            **self.valid_acceptance_context(input_physical_components_equal=False),
            "stage": "filler",
            "comparison_status": "complete",
            "stage_comparison": {"strict_raw_name_equal": True, "new_filler_pg_physical_equal": True},
            "idb_integrity": {"no_unintended_design_mutation": True, "intended_effect_present": True},
            "eccdb_integrity": {"no_unintended_design_mutation": True, "intended_effect_present": True},
        }
        self.assertEqual(IMJ.acceptance(report), (True, False))

    def test_raw_input_name_difference_does_not_override_equal_physical_projection(self) -> None:
        report = {
            **self.valid_acceptance_context(),
            "input_raw_components_equal": False,
            "stage": "filler",
            "comparison_status": "complete",
            "stage_comparison": {"strict_raw_name_equal": True, "new_filler_pg_physical_equal": True},
            "idb_integrity": {"no_unintended_design_mutation": True, "intended_effect_present": True},
            "eccdb_integrity": {"no_unintended_design_mutation": True, "intended_effect_present": True},
        }
        self.assertEqual(IMJ.acceptance(report), (True, True))

    def test_filler_dual_noop_is_covered_but_not_passed(self) -> None:
        report = self.functionality_report("filler", idb_effect=False, eccdb_effect=False)
        self.assertEqual(IMJ.acceptance(report), (True, False))

    def test_filler_single_backend_noop_is_covered_but_not_passed(self) -> None:
        for no_op_backend in ("idb", "eccdb"):
            with self.subTest(no_op_backend=no_op_backend):
                report = self.functionality_report(
                    "filler",
                    idb_effect=no_op_backend != "idb",
                    eccdb_effect=no_op_backend != "eccdb",
                )
                self.assertEqual(IMJ.acceptance(report), (True, False))

    def test_metal_dual_noop_is_covered_but_not_passed(self) -> None:
        report = self.functionality_report("metal", idb_effect=False, eccdb_effect=False)
        self.assertEqual(IMJ.acceptance(report), (True, False))

    def test_metal_single_backend_noop_is_covered_but_not_passed(self) -> None:
        for no_op_backend in ("idb", "eccdb"):
            with self.subTest(no_op_backend=no_op_backend):
                report = self.functionality_report(
                    "metal",
                    idb_effect=no_op_backend != "idb",
                    eccdb_effect=no_op_backend != "eccdb",
                )
                self.assertEqual(IMJ.acceptance(report), (True, False))

    def test_fresh_output_refuses_stale_worker_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "pair"
            root.mkdir()
            (root / "idb").mkdir()
            with self.assertRaisesRegex(ValueError, "non-empty output directory"):
                IMJ.prepare_fresh_output(root)

    def test_worker_invocation_rejects_nonzero_returncode(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            worker = root / "idb"
            worker.mkdir()
            manifest = root / "case.json"
            options = root / "options.json"
            command = ["/probe", "--backend", "idb", "--stage", "filler", "--manifest", str(manifest),
                       "--out", str(worker), "--options", str(options)]
            (worker / "invocation.json").write_text(json.dumps({"command": command, "returncode": 124}), encoding="utf-8")
            state = IMJ.worker_invocation_state(
                worker, "idb", "filler", manifest, options, None,
                {"backend": "idb", "stage": "filler", "status": "passed"},
            )
            self.assertFalse(state["valid"])
            self.assertTrue(any("returncode" in reason for reason in state["reasons"]))

    def test_worker_invocation_rejects_stale_stage_result(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            worker = root / "idb"
            worker.mkdir()
            manifest = root / "case.json"
            options = root / "options.json"
            command = ["/probe", "--backend", "idb", "--stage", "filler", "--manifest", str(manifest),
                       "--out", str(worker), "--options", str(options)]
            (worker / "invocation.json").write_text(json.dumps({"command": command, "returncode": 0}), encoding="utf-8")
            state = IMJ.worker_invocation_state(
                worker, "idb", "metal", manifest, options, None,
                {"backend": "idb", "stage": "filler", "status": "passed"},
            )
            self.assertFalse(state["valid"])
            self.assertTrue(any("stage" in reason for reason in state["reasons"]))

    def test_compare_only_provenance_requires_current_input_identity(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            probe = root / "probe"
            lef = root / "tech.lef"
            design_def = root / "input.def"
            manifest = root / "case.json"
            options = root / "options.json"
            probe.write_text("probe", encoding="utf-8")
            lef.write_text("VERSION 5.8 ;\n", encoding="utf-8")
            design_def.write_text("VERSION 5.8 ;\nEND DESIGN\n", encoding="utf-8")
            manifest.write_text(json.dumps({"lef_files": ["tech.lef"], "def_file": "input.def"}), encoding="utf-8")
            options.write_text("{}", encoding="utf-8")
            saved = IMJ.make_provenance(probe, manifest, options, None)
            self.assertTrue(IMJ.compare_only_provenance_state(saved, manifest, options, None)["valid"])
            options.write_text('{"changed": true}', encoding="utf-8")
            state = IMJ.compare_only_provenance_state(saved, manifest, options, None)
            self.assertFalse(state["valid"])
            self.assertTrue(any("options" in reason for reason in state["reasons"]))


if __name__ == "__main__":
    unittest.main()
