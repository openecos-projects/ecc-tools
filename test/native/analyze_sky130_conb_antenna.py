#!/usr/bin/env python3
# SPDX-License-Identifier: MulanPSL-2.0
"""Audit the conb annotation experiment without weakening the strict comparator."""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path


INVARIANTS = ("net_count", "wire_count", "connected_net_count", "rule_layer_count",
              "covered_rule_layer_count", "pins_with_gate_area", "via_geometry_count",
              "nets_without_gate", "components_without_gate", "conductors_out_of_range",
              "skipped_rule_layer_count", "skipped_segments", "unsupported_oxide_model_count",
              "unsupported_polygon_count", "unsupported_route_shape_count", "partial_areas_dropped")


def load(path: Path):
    return json.loads(path.read_text())


def same_file(left: dict, right: dict) -> bool:
    # A manifest can spell the same path with or without '..'.
    return all(left[key] == right[key] for key in ("resolved_path", "bytes", "sha256"))


def difference(left: list[str], right: list[str]) -> dict[str, object]:
    only_left = Counter(left) - Counter(right)
    only_right = Counter(right) - Counter(left)
    return {"equal": not only_left and not only_right,
            "only_left_count": only_left.total(), "only_right_count": only_right.total(),
            "only_left": dict(only_left), "only_right": dict(only_right)}


def analyze(root: Path, case: str) -> dict[str, object]:
    before = root / f"sky130-{case}-antenna-routed-coverage-final"
    after = root / f"sky130-{case}-antenna-conb-annotated"
    old_comparison = load(before / "comparison.json")
    new_comparison = load(after / "comparison.json")
    old_inputs = old_comparison["provenance"]
    new_inputs = new_comparison["provenance"]
    input_invariants = {field: same_file(old_inputs[field], new_inputs[field]) for field in ("def", "probe", "options")}
    input_invariants["technology_lef"] = same_file(old_inputs["lefs"][0], new_inputs["lefs"][0])
    results = {}
    vectors = {}
    for backend in ("idb", "eccdb"):
        old = load(before / backend / "result.json")
        new = load(after / backend / "result.json")
        old_antenna, new_antenna = old["antenna"], new["antenna"]
        vectors[backend] = new_antenna["violations"]
        results[backend] = {
            "worker_returncode": load(after / backend / "invocation.json")["returncode"],
            "missing_before": old_antenna["pins_missing_antenna_info"],
            "missing_after": new_antenna["pins_missing_antenna_info"],
            "coverage_before": old_antenna["coverage_complete"],
            "coverage_after": new_antenna["coverage_complete"],
            "violation_count_before": old_antenna["violation_count"],
            "violation_count_after": new_antenna["violation_count"],
            "invariants": {field: old_antenna[field] == new_antenna[field] for field in INVARIANTS},
            "violations_vs_baseline": difference(old_antenna["violations"], new_antenna["violations"]),
            "design_summary_unchanged": (old_comparison[f"{backend}_before"]
                                         == new_comparison[f"{backend}_before"]
                                         == new_comparison[f"{backend}_after"]),
            "stage_design_integrity": new_comparison[f"{backend}_integrity"]["no_unintended_design_mutation"],
            "summary": {key: value for key, value in new_antenna.items() if key != "violations"},
            "timing_ms": new["timing_ms"], "memory": new["memory"],
        }
    # These projections are diagnostics only; keep formal raw-name acceptance unchanged.
    decoded = {backend: [row.replace(r"\[", "[").replace(r"\]", "]") for row in values]
               for backend, values in vectors.items()}
    physical = {backend: [row.split(":", 1)[1] for row in values] for backend, values in vectors.items()}
    repaired = (all(input_invariants.values()) and new_comparison["provenance_validation"]["valid"]
                and all(item["worker_returncode"] == 0 and item["missing_after"] == 0
                        and item["coverage_after"] and all(item["invariants"].values())
                        and item["stage_design_integrity"] and item["design_summary_unchanged"]
                        for item in results.values()))
    return {"case": case, "annotation_repair_verified": repaired,
            "unchanged_inputs": input_invariants, "backends": results,
            "formal_comparison_passed": new_comparison["passed"],
            "raw_violation_difference": difference(vectors["idb"], vectors["eccdb"]),
            "diagnostic_bracket_decoded_difference": difference(decoded["idb"], decoded["eccdb"]),
            "diagnostic_layer_type_ratio_threshold_bbox_difference": difference(physical["idb"], physical["eccdb"])}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results-root", type=Path, required=True)
    args = parser.parse_args()
    preparation = load(args.results_root / "pdk-conb-antenna-overlay/preparation.json")
    installed = preparation["installed_lef"]
    unchanged = hashlib.sha256(Path(installed["path"]).read_bytes()).hexdigest() == installed["sha256"]
    cases = [analyze(args.results_root, case) for case in ("tiny", "rv64")]
    print(json.dumps({"schema": 1, "installed_pdk_unchanged": unchanged, "cases": cases,
                      "annotation_repair_verified": unchanged and all(case["annotation_repair_verified"] for case in cases)},
                     indent=2))


if __name__ == "__main__":
    main()
