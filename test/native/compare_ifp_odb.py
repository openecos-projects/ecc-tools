#!/usr/bin/env python3
# SPDX-License-Identifier: MulanPSL-2.0
"""Independently compare iDB and EccDB iFP DEF outputs through OpenDB.

The two adapters can serialize equivalent DEF geometry differently.  This
tool therefore asks the same OpenROAD/OpenDB build to read each backend's
``import.def`` and ``output.def``, snapshots physical objects, then compares
the snapshots as multisets.  It does not run iFP, CTS, routing, DRC, or write
any source DEF.

Expected pair-root layout (the alternate ``<stage>/output.def`` layout is also
accepted):

  pair-root/
    idb/import.def
    idb/output.def
    eccdb/import.def
    eccdb/output.def

The supplied manifest must contain ``lef_files``.  Names are deliberately
compared byte-for-byte as emitted by OpenDB: identifiers are never unescaped,
stripped, or renamed. Geometry and PORT groups are compared as multisets. For
PG vias, the snapshot omits the via-definition
name and compares only net, position, and expanded physical boxes.
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from typing import Any, Iterable


REPO = Path(__file__).resolve().parents[2]
SNAPSHOT_TCL = REPO / "test/native/ifp_odb_snapshot.tcl"
BACKENDS = ("idb", "eccdb")
STAGES = ("import", "output")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def file_metadata(path: Path) -> dict[str, Any]:
    return {
        "path": str(path),
        "bytes": path.stat().st_size,
        "sha256": sha256(path),
    }


def load_manifest(path: Path) -> tuple[dict[str, Any], list[Path]]:
    manifest = json.loads(path.read_text(encoding="utf-8"))
    raw_lefs = manifest.get("lef_files")
    if not isinstance(raw_lefs, list) or not raw_lefs:
        raise ValueError(f"{path}: expected a non-empty lef_files list")
    lefs = [Path(item).expanduser() for item in raw_lefs]
    lefs = [(item if item.is_absolute() else path.parent / item).resolve() for item in lefs]
    missing = [str(path) for path in lefs if not path.is_file()]
    if missing:
        raise FileNotFoundError(f"manifest LEF file(s) missing: {', '.join(missing)}")
    return manifest, lefs


def def_candidates(pair_root: Path, backend: str, stage: str) -> Iterable[Path]:
    """Known output layouts from native flow and standalone adapter runners."""

    backend_root = pair_root / backend
    yield backend_root / f"{stage}.def"
    yield backend_root / stage / "output.def"
    if stage == "output":
        yield backend_root / "floorplan" / "output.def"
        yield backend_root / "floorplan.def"
    yield pair_root / f"{backend}.{stage}.def"
    yield pair_root / f"{backend}-{stage}.def"


def find_def(pair_root: Path, backend: str, stage: str) -> Path:
    for candidate in def_candidates(pair_root, backend, stage):
        if candidate.is_file():
            return candidate.resolve()
    tried = "\n  ".join(str(path) for path in def_candidates(pair_root, backend, stage))
    raise FileNotFoundError(
        f"missing {backend}/{stage} DEF under {pair_root}; tried:\n  {tried}"
    )


def tsv_counter(path: Path) -> dict[str, Counter[tuple[str, ...]]]:
    """Read opaque TSV rows without altering identifier spelling or ordering."""

    records: dict[str, Counter[tuple[str, ...]]] = {}
    with path.open("r", encoding="utf-8", newline="") as stream:
        for line_number, line in enumerate(stream, 1):
            line = line.rstrip("\n")
            if not line:
                continue
            fields = tuple(line.split("\t"))
            if len(fields) < 2 or not fields[0]:
                raise ValueError(f"{path}:{line_number}: malformed snapshot row {line!r}")
            records.setdefault(fields[0], Counter())[fields] += 1
    return records


def duplicate_summary(counter: Counter[tuple[str, ...]], *, limit: int = 8) -> dict[str, Any]:
    duplicates = [(record, count) for record, count in counter.items() if count > 1]
    duplicates.sort(key=lambda item: (-item[1], item[0]))
    return {
        "distinct_duplicate_rows": len(duplicates),
        "extra_rows": sum(count - 1 for _, count in duplicates),
        "samples": [
            {"record": list(record), "count": count}
            for record, count in duplicates[:limit]
        ],
    }


def counter_summary(
    left: Counter[tuple[str, ...]], right: Counter[tuple[str, ...]], *, limit: int = 8
) -> dict[str, Any]:
    common = left & right
    only_left = left - right
    only_right = right - left

    def samples(counter: Counter[tuple[str, ...]]) -> list[dict[str, Any]]:
        return [
            {"record": list(record), "count": count}
            for record, count in sorted(counter.items(), key=lambda item: item[0])[:limit]
        ]

    return {
        "idb_total": sum(left.values()),
        "eccdb_total": sum(right.values()),
        "common": sum(common.values()),
        "idb_only": sum(only_left.values()),
        "eccdb_only": sum(only_right.values()),
        "equal": left == right,
        "idb_only_samples": samples(only_left),
        "eccdb_only_samples": samples(only_right),
        "idb_duplicate_counts": duplicate_summary(left, limit=limit),
        "eccdb_duplicate_counts": duplicate_summary(right, limit=limit),
    }


def compare_snapshots(idb_path: Path, eccdb_path: Path) -> dict[str, Any]:
    idb = tsv_counter(idb_path)
    eccdb = tsv_counter(eccdb_path)
    kinds = sorted(set(idb) | set(eccdb))
    result = {
        "idb_snapshot": file_metadata(idb_path),
        "eccdb_snapshot": file_metadata(eccdb_path),
        "kinds": {},
    }
    for kind in kinds:
        result["kinds"][kind] = counter_summary(
            idb.get(kind, Counter()), eccdb.get(kind, Counter())
        )
    # PORT ordinal is only the reader's iteration order. Compare the complete
    # group instead of dropping the ordinal from each individual rectangle:
    # flattening would incorrectly make split/merged strongly connected ports
    # equivalent. Duplicate PORTs and duplicate shapes remain significant.
    result["port_group_semantics"] = counter_summary(
        port_group_counter(idb), port_group_counter(eccdb)
    )
    result["diagnostic_rows_equal"] = all(item["equal"] for item in result["kinds"].values())
    result["equal"] = result["port_group_semantics"]["equal"] and all(
        item["equal"] for kind, item in result["kinds"].items()
        if kind not in {"BTERM", "IOBOX"}
    )
    return result


def port_group_counter(records: dict[str, Counter[tuple[str, ...]]]) -> Counter[tuple[str, ...]]:
    shapes: dict[tuple[str, str], Counter[tuple[str, ...]]] = defaultdict(Counter)
    for row, count in records.get("IOBOX", Counter()).items():
        if len(row) != 9:
            raise ValueError(f"malformed IOBOX snapshot: {row!r}")
        shapes[(row[1], row[2])][row[4:]] += count
    groups: Counter[tuple[str, ...]] = Counter()
    seen = set()
    for row, count in records.get("BTERM", Counter()).items():
        if len(row) != 5:
            raise ValueError(f"malformed BTERM snapshot: {row!r}")
        _, term, net, ordinal, status = row
        key = (term, ordinal)
        seen.add(key)
        primitives = [(list(shape), amount) for shape, amount in sorted(shapes.get(key, {}).items())]
        groups[("IOPORT", term, net, status,
                json.dumps(primitives, separators=(",", ":"), ensure_ascii=False))] += count
    if shapes.keys() - seen:
        raise ValueError("IOBOX references a PORT absent from the BTERM snapshot")
    return groups


def run_snapshot(
    openroad: Path,
    lefs: list[Path],
    source_def: Path,
    snapshot: Path,
    log_path: Path,
) -> dict[str, Any]:
    snapshot.parent.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment.update(
        {
            "ECC_IFP_LEFS": "\n".join(str(path) for path in lefs),
            "ECC_IFP_DEF": str(source_def),
            "ECC_IFP_SNAPSHOT": str(snapshot),
        }
    )
    command = [str(openroad), "-exit", str(SNAPSHOT_TCL)]
    completed = subprocess.run(command, text=True, capture_output=True, env=environment, check=False)
    log_path.write_text(
        "command: " + " ".join(command) + "\n\nstdout:\n" + completed.stdout
        + "\n\nstderr:\n" + completed.stderr,
        encoding="utf-8",
    )
    record: dict[str, Any] = {
        "command": command,
        "input_def": file_metadata(source_def),
        "returncode": completed.returncode,
        "log": str(log_path),
    }
    if completed.returncode == 0 and snapshot.is_file() and snapshot.stat().st_size > 0:
        record["snapshot"] = file_metadata(snapshot)
        record["status"] = "passed"
    else:
        record["status"] = "failed"
        record["error"] = "OpenROAD snapshot command failed or did not create its TSV"
    return record


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--openroad", type=Path, required=True, help="OpenROAD executable used for every snapshot")
    parser.add_argument("--manifest", type=Path, required=True, help="case manifest with lef_files")
    parser.add_argument("--pair-root", type=Path, required=True, help="root containing iDB/EccDB import and output DEFs")
    parser.add_argument("--result", type=Path, help="result JSON; defaults to <pair-root>/ifp-odb-result.json")
    parser.add_argument("--compare-only", action="store_true",
                        help="reuse the saved OpenDB TSVs and their input hashes; never launches OpenROAD")
    args = parser.parse_args()

    openroad = args.openroad.expanduser().resolve()
    manifest_path = args.manifest.expanduser().resolve()
    pair_root = args.pair_root.expanduser().resolve()
    if not openroad.is_file():
        parser.error(f"OpenROAD executable does not exist: {openroad}")
    if not SNAPSHOT_TCL.is_file():
        parser.error(f"missing snapshot Tcl script: {SNAPSHOT_TCL}")
    if not pair_root.is_dir():
        parser.error(f"pair root does not exist: {pair_root}")

    manifest, lefs = load_manifest(manifest_path)
    result_path = (args.result.expanduser().resolve() if args.result else pair_root / "ifp-odb-result.json")
    snapshots_root = pair_root / "odb-snapshots"
    previous = json.loads(result_path.read_text(encoding="utf-8")) if args.compare_only else None
    result: dict[str, Any] = {
        "schema": 1,
        "purpose": "iFP adapter differential through independent OpenDB physical snapshots",
        "comparison_policy": {
            "names": "raw OpenDB strings; no unescape, strip, or normalization",
            "collections": "multiset comparison with duplicate counts retained",
            "io_ports": "world-geometry groups; ignore PORT ordinal, retain grouping, placement, net and duplicates",
            "pg_vias": "via-definition names intentionally omitted; compare net, position, expanded boxes",
        },
        "manifest": file_metadata(manifest_path),
        "manifest_case": manifest.get("case"),
        "lef_files": [file_metadata(path) for path in lefs],
        "openroad": str(openroad),
        "stages": {},
    }

    source_defs: dict[tuple[str, str], Path] = {}
    try:
        for backend in BACKENDS:
            for stage in STAGES:
                source_defs[(backend, stage)] = find_def(pair_root, backend, stage)
    except FileNotFoundError as error:
        result.update({"status": "failed", "error": str(error)})
        result_path.parent.mkdir(parents=True, exist_ok=True)
        result_path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(result["error"], file=sys.stderr)
        return 2

    all_passed = True
    for stage in STAGES:
        stage_dir = snapshots_root / stage
        stage_result: dict[str, Any] = {"snapshots": {}}
        stage_passed = True
        for backend in BACKENDS:
            snapshot = stage_dir / f"{backend}.tsv"
            log_path = stage_dir / f"{backend}.openroad.log"
            if args.compare_only:
                run = previous["stages"][stage]["snapshots"][backend].copy()
                if (run["status"] != "passed" or not snapshot.is_file()
                        or run["input_def"]["sha256"] != sha256(source_defs[(backend, stage)])
                        or run["snapshot"]["sha256"] != sha256(snapshot)):
                    raise ValueError("saved OpenDB snapshot no longer matches its input or recorded hash")
                run["mode"] = "compare_only_saved_snapshot"
            else:
                run = run_snapshot(openroad, lefs, source_defs[(backend, stage)], snapshot, log_path)
            stage_result["snapshots"][backend] = run
            stage_passed = stage_passed and run["status"] == "passed"
        all_passed = all_passed and stage_passed
        if stage_passed and all(
            stage_result["snapshots"][backend]["status"] == "passed" for backend in BACKENDS
        ):
            stage_result["comparison"] = compare_snapshots(
                stage_dir / "idb.tsv", stage_dir / "eccdb.tsv"
            )
        result["stages"][stage] = stage_result

    comparisons = [
        stage.get("comparison") for stage in result["stages"].values()
        if stage.get("comparison") is not None
    ]
    result["status"] = "passed" if all_passed else "failed"
    result["equal"] = all_passed and len(comparisons) == len(STAGES) and all(
        comparison["equal"] for comparison in comparisons
    )
    result_path.parent.mkdir(parents=True, exist_ok=True)
    result_path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({"status": result["status"], "equal": result["equal"], "result": str(result_path)}, indent=2))
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
