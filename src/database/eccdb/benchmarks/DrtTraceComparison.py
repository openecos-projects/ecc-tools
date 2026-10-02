#!/usr/bin/env python3
"""Replay one trace captured inside TritonRoute, without rerunning the router."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import statistics
import subprocess


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("trace", "lef", "def", "odb", "eccdb-idb", "output"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    parser.add_argument("--guides", type=Path)
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--cpu", type=int, default=24)
    parser.add_argument("--parallel", action="store_true",
                        help="run the three backends concurrently on CPU, CPU+1, and CPU+2")
    args = parser.parse_args()
    if args.repeat not in (1, 3):
        parser.error("--repeat must be 1 or 3")
    paths = {key: value.resolve() for key, value in vars(args).items() if isinstance(value, Path)}
    for key in ("trace", "lef", "def", "odb", "eccdb_idb"):
        if not paths[key].is_file():
            parser.error(f"missing {key}: {paths[key]}")
    if "guides" in paths and not paths["guides"].is_file():
        parser.error(f"missing guides: {paths['guides']}")
    output = paths["output"]
    directory = output.with_suffix(".runs")
    directory.mkdir(parents=True, exist_ok=True)
    backends = ("odb", "eccdb", "idb")
    orders = [("odb", "eccdb", "idb"), ("eccdb", "idb", "odb"), ("idb", "odb", "eccdb")][:args.repeat]
    results: dict[str, list[dict]] = {backend: [] for backend in backends}
    for index, order in enumerate(orders, 1):
        def run_backend(backend: str) -> tuple[str, dict]:
            executable = paths["odb"] if backend == "odb" else paths["eccdb_idb"]
            destination = directory / f"{backend}-{index}.json"
            cpu = args.cpu + backends.index(backend) if args.parallel else args.cpu
            command = ["taskset", "-c", str(cpu), str(executable), "--backend", backend,
                       "--trace", str(paths["trace"]), "--lef", str(paths["lef"]),
                       "--def", str(paths["def"]), "--output", str(destination)]
            if "guides" in paths:
                command.extend(("--guides", str(paths["guides"])))
            with (directory / f"{backend}-{index}.log").open("w") as log:
                subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)
            measured = json.loads(destination.read_text())
            unsupported = measured.get("unsupported_operations", {})
            unmeasured = measured.get("unmeasured_operations", {})
            measured_operations = measured.get("measured_operations", {})
            operations = measured["operations"]
            if (measured["backend"] != backend or not measured["validation"].get("all_supported_events", False)
                    or measured.get("validated_events", 0) + sum(unsupported.values()) != measured["events"]
                    or set(measured_operations) - set(operations)
                    or set(unsupported) - set(operations)
                    or set(unmeasured) - set(operations)
                    or any(measured_operations.get(op, 0) + unsupported.get(op, 0)
                           + unmeasured.get(op, 0) != count
                           for op, count in operations.items())
                    or (unsupported and measured["validation"]["all_events"])):
                raise RuntimeError(f"invalid replay result: {destination}")
            measured["command"] = command
            print(f"{backend} run {index}: measured native API sum "
                  f"{measured['measured_native_total_ns'] / 1e6:.3f} ms, "
                  f"validated {measured['validated_events']}/{measured['events']} events, "
                  f"unmeasured {sum(unmeasured.values())}, unsupported {sum(unsupported.values())}", flush=True)
            return backend, measured

        if args.parallel:
            with ThreadPoolExecutor(max_workers=3) as pool:
                runs = list(pool.map(run_backend, order))
        else:
            runs = [run_backend(backend) for backend in order]
        for backend, measured in runs:
            results[backend].append(measured)
    baseline = results["odb"][0]
    with paths["trace"].open() as source:
        header = json.loads(source.readline())
    declared_operations = sorted(header.get("supported_operations", baseline["operations"]))
    route_mutations = {"clear_wire", "replace_wire", "append_bterm_stack_wire"}
    via_mutations = {"define_design_via"}
    for runs in results.values():
        for measured in runs:
            if measured["events"] != baseline["events"] or measured["operations"] != baseline["operations"]:
                raise RuntimeError("backends did not replay identical operation counts")
            if (measured.get("unsupported_operations", {}) != runs[0].get("unsupported_operations", {})):
                raise RuntimeError("backend support changed between repetitions")
            unsupported = set(measured.get("unsupported_operations", {}))
            fields = []
            if "net_pins" not in unsupported:
                fields.append("final_net_pin_checks")
            if "read_wire" not in unsupported and not (unsupported & route_mutations):
                fields.extend(("final_wires", "final_primitives", "final_wire_checks",
                               "trace_endpoint_extension_fields"))
            if not (unsupported & via_mutations):
                fields.extend(("final_vias", "final_via_rectangles"))
            for field in fields:
                if measured["validation"][field] != baseline["validation"][field]:
                    raise RuntimeError(f"backends disagree on validation population: {field}")
    comparable_ops = {
        op for op, count in baseline["operations"].items()
        if all(run.get("measured_operations", {}).get(op, 0) == count
               for runs in results.values() for run in runs)
    }
    summary = {
        "schema": "tritonroute_db_trace_comparison_v3",
        "cpu_affinity": args.cpu,
        "execution": "parallel" if args.parallel else "serial",
        "backend_cpu_affinity": {backend: args.cpu + backends.index(backend) if args.parallel else args.cpu
                                 for backend in backends},
        "backends": list(backends),
        "repeat": args.repeat,
        "orders": orders,
        "inputs": {key: {"path": str(paths[key]), "sha256": sha256(paths[key])}
                   for key in ("trace", "lef", "def", "guides") if key in paths},
        "executables": {key: {"path": str(paths[key]), "sha256": sha256(paths[key])} for key in ("odb", "eccdb_idb")},
        "events": baseline["events"],
        "operations": baseline["operations"],
        "declared_operations": declared_operations,
        "unobserved_operations": sorted(set(declared_operations) - set(baseline["operations"])),
        "jointly_measured_operations": sorted(comparable_ops),
        "geometry_equivalent_only_operations": [op for op in sorted(route_mutations)
                                                 if op in baseline["operations"]],
        "wire_write_inputs": {backend: runs[0]["wire_write_input"] for backend, runs in results.items()},
        "results": {},
        "scope": "actual TritonRoute database API-group trace; joint timing compares matching requested work, not identical native function sequences; Wire writes compare supported geometry only",
        "validation_scope": "ODB/EccDB include segment endpoint extensions; IDB validates common geometry/connectivity only; runtime design VIA comparison covers name and fixed rectangles, not EccDB DEFAULT metadata",
        "timer_scope": "native database/service API batches only; parsing, binding, adaptor input conversion, result JSON projection and validation excluded; native unmeasured groups explicit",
    }
    for backend, runs in results.items():
        summary["results"][backend] = {
            "runs": runs,
            "median_ns": {
                op: (int(statistics.median(run["timing_ns"][op] for run in runs))
                     if all(run.get("measured_operations", {}).get(op, 0) == count for run in runs) else None)
                for op, count in baseline["operations"].items()
            },
            "median_measured_native_total_ns": int(statistics.median(run["measured_native_total_ns"] for run in runs)),
            "median_jointly_measured_native_ns": int(statistics.median(
                sum(run["timing_ns"][op] for op in comparable_ops) for run in runs)),
            "unsupported_operations": runs[0].get("unsupported_operations", {}),
            "unmeasured_operations": runs[0].get("unmeasured_operations", {}),
            "final_route_state_comparable": all(
                not (set(run.get("unsupported_operations", {})) & (route_mutations | {"read_wire"}))
                for run in runs),
        }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    print(f"Results: {output}")


if __name__ == "__main__":
    main()
