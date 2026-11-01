#!/usr/bin/env python3
# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

"""Run the embedded dense_map candidate once; reuse existing reference results."""

import argparse
import hashlib
import json
import resource
import shlex
import subprocess
from pathlib import Path


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def memory_limit():
    limit = 500 * 1024**3
    resource.setrlimit(resource.RLIMIT_AS, (limit, limit))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--references", type=Path, required=True)
    parser.add_argument("--trace", type=Path, required=True)
    parser.add_argument("--lef", type=Path, required=True)
    parser.add_argument("--def", dest="def_file", type=Path, required=True)
    parser.add_argument("--guides", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--cpu", type=int, default=25)
    args = parser.parse_args()
    output = args.output_dir.resolve()
    # A started or completed experiment must never be silently rerun.
    if output.exists():
        parser.error(f"output directory already exists; candidate was not run: {output}")
    references = args.references.resolve()
    reference_files = [references / f"{name}.json" for name in ("baseline", "candidate", "odb", "idb")]
    inputs = [args.candidate, args.trace, args.lef, args.def_file, args.guides, *reference_files]
    for path in inputs:
        if not path.is_file():
            parser.error(f"missing input: {path}")
    baseline = json.loads(reference_files[0].read_text())
    output.mkdir(parents=True)
    command = [
        "/usr/bin/time", "-v", "-o", str(output / "candidate.time.txt"),
        "taskset", "-c", str(args.cpu), str(args.candidate.resolve()),
        "--backend", "eccdb", "--trace", str(args.trace.resolve()),
        "--lef", str(args.lef.resolve()), "--def", str(args.def_file.resolve()),
        "--guides", str(args.guides.resolve()), "--output", str(output / "candidate.json"),
    ]
    (output / "candidate.command.txt").write_text("ulimit -v 524288000\n" + shlex.join(command) + "\n")
    metadata = {
        "branch": subprocess.check_output(["git", "branch", "--show-current"], text=True).strip(),
        "base_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
        "fresh_runs": {"eccdb_densemap_candidate": 1, "baseline": 0, "registry_partition": 0, "odb": 0, "idb": 0},
        "memory_limit_bytes": 500 * 1024**3,
        "cpu": args.cpu,
        "candidate": {"path": str(args.candidate.resolve()), "sha256": digest(args.candidate)},
        "references": [{"path": str(path), "sha256": digest(path)} for path in reference_files],
        "inputs": [{"path": str(path.resolve()), "bytes": path.stat().st_size} for path in inputs[1:5]],
        "reference_scope": "Frozen same-day single-pass measurements; references were not rerun concurrently.",
    }
    (output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    with (output / "candidate.stdout.log").open("w") as stdout, (output / "candidate.stderr.log").open("w") as stderr:
        subprocess.run(command, stdout=stdout, stderr=stderr, preexec_fn=memory_limit, check=True)
    candidate = json.loads((output / "candidate.json").read_text())
    # Validation is outside the backend's measured API regions.
    if candidate["events"] != baseline["events"] or candidate["operations"] != baseline["operations"]:
        raise RuntimeError("candidate did not replay the reference operation sequence")
    if candidate["validated_events"] != candidate["events"] or candidate["validation"] != baseline["validation"]:
        raise RuntimeError("candidate failed reference event/final-state validation")
    print(json.dumps({"output": str(output), "events": candidate["events"], "validated_events": candidate["validated_events"],
                      "api_ms": candidate["total_ns"] / 1e6, "fresh_candidate_runs": 1}))


if __name__ == "__main__":
    main()
