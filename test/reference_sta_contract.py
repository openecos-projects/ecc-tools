#!/usr/bin/env python3
"""Compare a native PyPlaceDB timing snapshot with an external STA contract.

This is a Gate-D contract check, not a QoR or optimizer-parity test.  The
caller must provide snapshots from the same Liberty/SDC and parasitics mode.
Endpoints that have no external path are reported as diagnostics instead of
being treated as native timing failures (for example unconstrained outputs).
"""

import argparse
import json
import re
import subprocess
from pathlib import Path
from typing import Any

_NUMBER = r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?"


def _normalize_pin(name: str) -> str:
    return str(name).replace(":", "/")


def _parse_path_row(lines: list[str], endpoint: str) -> dict[str, float]:
    """Extract the final endpoint slew/cap columns when OpenSTA prints them."""
    candidates = [
        line for line in lines if endpoint in line and ("/" in line or f" {endpoint} " in line)
    ]
    if not candidates:
        return {}
    endpoint_rows = [
        line for line in candidates if re.search(rf"{re.escape(endpoint)}/(?:D|Q)\b", line)
    ]
    if endpoint_rows:
        candidates = endpoint_rows
    row = candidates[-1]
    transition_positions = [
        position for position in (row.find(" ^ "), row.find(" v ")) if position >= 0
    ]
    prefix = row[: min(transition_positions)] if transition_positions else row
    values = [float(value) for value in re.findall(_NUMBER, prefix)]
    if len(values) < 3:
        return {}
    result = {"slew_ns": values[-3]}
    if len(values) >= 4:
        result["cap_pf"] = values[-4]
    return result


def parse_opensta_report(report: str) -> dict[str, dict[str, float]]:
    parsed: dict[str, dict[str, float]] = {}
    for block in re.split(r"(?m)^Endpoint:\s*", report)[1:]:
        lines = block.splitlines()
        if not lines:
            continue
        endpoint = _normalize_pin(lines[0].strip())
        arrival = re.search(rf"(?m)^\s*({_NUMBER})\s+data arrival time\s*$", block)
        required = re.search(rf"(?m)^\s*({_NUMBER})\s+data required time\s*$", block)
        if not arrival or not required or endpoint in parsed:
            continue
        sample = {
            "arrival_ns": float(arrival.group(1)),
            "required_ns": float(required.group(1)),
        }
        sample.update(
            _parse_path_row(
                lines[
                    : lines.index(required.group(0).strip())
                    if required.group(0).strip() in lines
                    else len(lines)
                ],
                endpoint,
            )
        )
        parsed[endpoint] = sample
    return parsed


def run_opensta(sta: str, libraries: list[str], netlist: str, sdc: str) -> dict[str, Any]:
    commands = [
        *(f"read_liberty {path}" for path in libraries),
        f"read_verilog {netlist}",
        "link_design gcd",
        f"read_sdc {sdc}",
        "report_checks -path_delay max -endpoint_path_count 1000 -fields {slew cap} -digits 6",
        "exit",
    ]
    completed = subprocess.run(
        [sta, "-no_splash"],
        input="\n".join(commands) + "\n",
        text=True,
        capture_output=True,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            "reference STA failed with exit code "
            f"{completed.returncode}: {completed.stderr[-1000:]}"
        )
    return {
        "time_unit": "ns",
        "cap_unit": "pF",
        "endpoints": parse_opensta_report(completed.stdout),
    }


def compare(
    native: dict[str, Any],
    reference: dict[str, Any],
    *,
    abs_tolerance_ps: float,
    rel_tolerance: float,
    min_shared_endpoints: int,
) -> dict[str, Any]:
    native_names = {_normalize_pin(name) for name in native.get("endpoint_names", [])}
    reference_map: dict[str, dict[str, float]] = {}
    reference_only = []
    for reference_name, sample in reference["endpoints"].items():
        candidates = [reference_name, f"{reference_name}/D", f"{reference_name}/Q"]
        target = next((candidate for candidate in candidates if candidate in native_names), None)
        if target is None:
            reference_only.append(reference_name)
        else:
            reference_map[target] = sample
    shared_names = sorted(reference_map)
    missing_native = sorted(native_names - set(reference_map))
    samples = []
    failures = []
    native_samples = {
        _normalize_pin(item["pin"]): item
        for item in native.get("endpoint_samples", native.get("samples", []))
    }
    for name in shared_names:
        native_sample = native_samples.get(name)
        reference_sample = reference_map[name]
        if native_sample is None:
            continue
        checks = []
        for native_key, reference_key in (("rAAT_ps", "arrival_ns"), ("rRAT_ps", "required_ns")):
            native_value = float(native_sample[native_key])
            reference_value = float(reference_sample[reference_key]) * 1000.0
            tolerance = max(abs_tolerance_ps, abs(native_value) * rel_tolerance)
            checks.append(
                {
                    "field": native_key,
                    "native": native_value,
                    "reference": reference_value,
                    "abs_error_ps": abs(native_value - reference_value),
                    "tolerance_ps": tolerance,
                    "pass": abs(native_value - reference_value) <= tolerance,
                }
            )
        if "cap_pf" in reference_sample and float(native_sample.get("outcap_pf", 0.0)) > 0.0:
            native_cap = float(native_sample["outcap_pf"])
            reference_cap = float(reference_sample["cap_pf"])
            tolerance = max(1.0e-3, abs(native_cap) * rel_tolerance)
            checks.append(
                {
                    "field": "outcap_pf",
                    "native": native_cap,
                    "reference": reference_cap,
                    "abs_error_pf": abs(native_cap - reference_cap),
                    "tolerance_pf": tolerance,
                    "pass": abs(native_cap - reference_cap) <= tolerance,
                }
            )
        if "slew_ns" in reference_sample and native_sample.get("rSlew_ps") is not None:
            native_slew = float(native_sample["rSlew_ps"])
            reference_slew = float(reference_sample["slew_ns"]) * 1000.0
            tolerance = max(abs_tolerance_ps, abs(native_slew) * rel_tolerance)
            checks.append(
                {
                    "field": "rSlew_ps",
                    "native": native_slew,
                    "reference": reference_slew,
                    "abs_error_ps": abs(native_slew - reference_slew),
                    "tolerance_ps": tolerance,
                    "pass": abs(native_slew - reference_slew) <= tolerance,
                }
            )
        samples.append({"pin": name, "checks": checks})
        failures.extend(check for check in checks if not check["pass"])
    diagnostics = []
    if missing_native:
        diagnostics.append(
            f"{len(missing_native)} native endpoints have no OpenSTA path "
            "and remain diagnostic-only"
        )
    if reference_only:
        diagnostics.append(
            f"{len(reference_only)} OpenSTA endpoints are absent from the native endpoint set"
        )
    if len(shared_names) < min_shared_endpoints:
        failures.append(
            {
                "field": "shared_endpoint_count",
                "native": len(shared_names),
                "required": min_shared_endpoints,
            }
        )
    return {
        "status": "pass" if not failures else "fail",
        "mode": native.get("parasitics_mode", native.get("mode", "unspecified")),
        "native_endpoint_count": len(native_names),
        "reference_endpoint_count": len(reference["endpoints"]),
        "shared_endpoint_count": len(shared_names),
        "missing_native_endpoints": missing_native,
        "reference_only_endpoints": reference_only,
        "native_units": native.get(
            "units",
            {
                "time_ps": native.get("time_unit_ps"),
                "cap_pf": native.get("capacitance_unit_pf"),
                "resistance_ohm": native.get("resistance_unit_ohm"),
            },
        ),
        "reference_units": {"time": "ns", "cap": "pF"},
        "native_arc_counts": {
            "constraint": native.get("num_constraint_arcs", native.get("constraint_arc_count")),
            "timing_check": native.get(
                "num_timing_check_arcs", native.get("timing_check_arc_count")
            ),
            "cell": native.get("num_cell_arcs"),
            "net": native.get("num_net_arcs"),
        },
        "samples": samples,
        "failures": failures,
        "diagnostics": diagnostics,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--native-summary", required=True, type=Path)
    parser.add_argument("--sta", required=True)
    parser.add_argument("--lib", action="append", required=True)
    parser.add_argument("--netlist", required=True)
    parser.add_argument("--sdc", required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--abs-tolerance-ps", type=float, default=100.0)
    parser.add_argument("--rel-tolerance", type=float, default=0.20)
    parser.add_argument("--min-shared-endpoints", type=int, default=10)
    args = parser.parse_args()
    native = json.loads(args.native_summary.read_text(encoding="utf-8"))
    result = compare(
        native,
        run_opensta(args.sta, args.lib, args.netlist, args.sdc),
        abs_tolerance_ps=args.abs_tolerance_ps,
        rel_tolerance=args.rel_tolerance,
        min_shared_endpoints=args.min_shared_endpoints,
    )
    rendered = json.dumps(result, indent=2, sort_keys=True)
    if args.output:
        args.output.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    return 0 if result["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
