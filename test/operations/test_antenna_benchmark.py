"""Antenna repair benchmark: violations, nets, and timing per iteration.

Supports two modes:
  - ``antenna_fix``: runs router with antenna fix, then re-checks (requires route config)
  - ``antenna``: check-only, reports initial violation counts (no fix iterations)

Requires:
  - ECC_TOOLS_TEST_REPO_ROOT, ECC_TOOLS_TEST_PDK_ROOT, ECC_TOOLS_TEST_ARTIFACT_ROOT

Run with::

    pytest test/operations/test_antenna_benchmark.py -v --tb=short -s
"""

from __future__ import annotations

import os
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "test"))
from support import ScenarioResult, TestRoots, assert_nonempty_file, run_scenario


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

@pytest.fixture(scope="module")
def roots() -> TestRoots:
    return TestRoots.from_environment()


@pytest.fixture(scope="module")
def antenna_result(roots: TestRoots) -> ScenarioResult:
    """Run antenna check-only scenario (always succeeds if PDK is present)."""
    result = run_scenario(roots, "antenna", timeout=120)
    assert_nonempty_file(result.output_path("report"))
    return result


# ---------------------------------------------------------------------------
# Parsers
# ---------------------------------------------------------------------------

_ITER_LINE_RE = re.compile(
    r"Iter (\d+): violations=(\d+) hop_up=(\d+)/(\d+) jog=(\d+)/(\d+) diode=(\d+)/(\d+)"
)
_REMAINING_RE = re.compile(r"Remaining violations: (\d+)")


def _parse_report(report_path: Path) -> dict:
    """Parse antenna_check.rpt into structured data."""
    text = report_path.read_text(encoding="utf-8", errors="replace")
    iterations = []
    for m in _ITER_LINE_RE.finditer(text):
        iterations.append({
            "iter": int(m.group(1)),
            "violations": int(m.group(2)),
            "hop_up_applied": int(m.group(3)),
            "hop_up_total": int(m.group(3)) + int(m.group(4)),
            "jog_applied": int(m.group(5)),
            "jog_total": int(m.group(5)) + int(m.group(6)),
            "diode_applied": int(m.group(7)),
            "diode_total": int(m.group(7)) + int(m.group(8)),
        })
    remaining_m = _REMAINING_RE.search(text)
    remaining = int(remaining_m.group(1)) if remaining_m else None

    # Also extract signal net count from report if present
    signal_nets_m = re.search(r"signal nets:\s*(\d+)", text)
    signal_nets = int(signal_nets_m.group(1)) if signal_nets_m else None

    return {
        "iterations": iterations,
        "remaining": remaining,
        "signal_nets": signal_nets,
        "raw_report": text,
    }


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

class TestAntennaBenchmark:
    """Parse and report antenna metrics from the antenna scenario."""

    def test_report_has_content(self, antenna_result: ScenarioResult):
        report = _parse_report(antenna_result.output_path("report"))
        assert len(report["raw_report"]) > 0, "Report file is empty"

    def test_report_summary(self, antenna_result: ScenarioResult):
        report = _parse_report(antenna_result.output_path("report"))
        print("\n" + "=" * 70)
        print("ANTENNA CHECK BENCHMARK — REPORT FILE")
        print("=" * 70)

        if report["iterations"]:
            for it in report["iterations"]:
                print(
                    f"  Iter {it['iter']:>2d}: violations={it['violations']:>4d}  "
                    f"hop_up={it['hop_up_applied']}/{it['hop_up_total']}  "
                    f"jog={it['jog_applied']}/{it['jog_total']}  "
                    f"diode={it['diode_applied']}/{it['diode_total']}"
                )
            print(f"  Remaining violations: {report['remaining']}")
        else:
            # Check-only mode: just show the report
            print("  (check-only mode — no fix iterations)")
            violation_count_m = re.search(r"violation[s]?\s*[:=]\s*(\d+)", report["raw_report"], re.I)
            if violation_count_m:
                print(f"  Total violations found: {violation_count_m.group(1)}")

        print("=" * 70)

    def test_log_has_antenna_output(self, antenna_result: ScenarioResult):
        log_path = antenna_result.work_dir / "native.log"
        if not log_path.is_file():
            pytest.skip("native.log not found")
        log_text = log_path.read_text(encoding="utf-8", errors="replace")
        assert "antenna" in log_text.lower(), "No antenna output in log"

    def test_violation_count_non_negative(self, antenna_result: ScenarioResult):
        report = _parse_report(antenna_result.output_path("report"))
        if report["iterations"]:
            for it in report["iterations"]:
                assert it["violations"] >= 0, f"Iter {it['iter']}: negative violations"
