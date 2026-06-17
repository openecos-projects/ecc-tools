#!/usr/bin/env python3
"""Validate iDB binary data save/load by comparing exported snapshots.

The script drives the existing Tcl API:
  LEF/DEF -> save_data -> reset_data -> load_data

It exports source and restored ViewJson/connectivity/DEF snapshots and compares
them. A mismatch means data_builder did not preserve the exported IDB state.
"""

from __future__ import annotations

import argparse
import difflib
import json
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


REPO_ROOT = Path(__file__).resolve().parents[2]


@dataclass
class Difference:
    path: str
    kind: str
    detail: str


def tcl_word(value: Path | str) -> str:
    text = str(value)
    return "{" + text.replace("\\", "\\\\").replace("}", "\\}") + "}"


def flatten(values: list[list[str]]) -> list[str]:
    return [item for group in values for item in group]


def rel_files(root: Path) -> set[str]:
    if not root.exists():
        return set()
    return {
        str(path.relative_to(root))
        for path in root.rglob("*")
        if path.is_file()
    }


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def normalized_json_text(path: Path) -> str:
    data = json.loads(path.read_text(encoding="utf-8"))
    return json.dumps(data, ensure_ascii=False, indent=2, sort_keys=True) + "\n"


def unified_diff(before: str, after: str, before_name: str, after_name: str, limit: int) -> str:
    lines = list(
        difflib.unified_diff(
            before.splitlines(keepends=True),
            after.splitlines(keepends=True),
            fromfile=before_name,
            tofile=after_name,
        )
    )
    if len(lines) > limit:
        return "".join(lines[:limit]) + f"\n... diff truncated after {limit} lines ...\n"
    return "".join(lines)


def compare_file_pair(source: Path, restored: Path, rel_path: str, diff_limit: int) -> Difference | None:
    if source.suffix == ".json" and restored.suffix == ".json":
        try:
            before = normalized_json_text(source)
            after = normalized_json_text(restored)
        except json.JSONDecodeError as exc:
            return Difference(rel_path, "json_parse", str(exc))
        if before != after:
            return Difference(rel_path, "json_mismatch", unified_diff(before, after, f"source/{rel_path}", f"restored/{rel_path}", diff_limit))
        return None

    source_bytes = source.read_bytes()
    restored_bytes = restored.read_bytes()
    if source_bytes == restored_bytes:
        return None

    before = read_text(source)
    after = read_text(restored)
    return Difference(rel_path, "text_mismatch", unified_diff(before, after, f"source/{rel_path}", f"restored/{rel_path}", diff_limit))


def compare_trees(source: Path, restored: Path, diff_limit: int) -> list[Difference]:
    diffs: list[Difference] = []
    source_files = rel_files(source)
    restored_files = rel_files(restored)

    for rel_path in sorted(source_files - restored_files):
        diffs.append(Difference(rel_path, "missing_after_load", "file exists before save/load but not after load"))
    for rel_path in sorted(restored_files - source_files):
        diffs.append(Difference(rel_path, "extra_after_load", "file exists after load but not before save/load"))

    for rel_path in sorted(source_files & restored_files):
        diff = compare_file_pair(source / rel_path, restored / rel_path, rel_path, diff_limit)
        if diff is not None:
            diffs.append(diff)

    return diffs


def build_tcl(args: argparse.Namespace, out_dir: Path, data_dir: Path) -> str:
    source_dir = out_dir / "source"
    restored_dir = out_dir / "restored"
    source_view = source_dir / "view_json"
    restored_view = restored_dir / "view_json"

    lines = [
        "proc must {label body} {",
        "  puts \"\\[IdbRoundtrip\\] $label\"",
        "  set rc [catch {uplevel 1 $body} result]",
        "  if {$rc != 0} {",
        "    puts stderr \"\\[IdbRoundtrip\\] failed: $label: $result\"",
        "    exit 2",
        "  }",
        "}",
        f"file mkdir {tcl_word(source_dir)}",
        f"file mkdir {tcl_word(restored_dir)}",
    ]

    if args.tech_lef:
        lines.append(f"must \"read tech lef\" {{tech_lef_init -path {tcl_word(args.tech_lef)}}}")

    if args.lef:
        lef_words = " ".join(tcl_word(path) for path in args.lef)
        lines.append(f"set lef_files [list {lef_words}]")
        lines.append("must \"read lef\" {lef_init -path $lef_files}")

    lines.extend(
        [
            f"must \"read def\" {{def_init -path {tcl_word(args.def_file)}}}",
            f"must \"validate source connectivity\" {{idb_validate -path {tcl_word(source_dir / 'connectivity.json')} -check_floating {int(args.check_floating)}}}",
            f"must \"export source view json\" {{view_json_save -path {tcl_word(source_view)}}}",
        ]
    )

    if args.compare_def:
        lines.append(f"must \"export source def\" {{def_save -path {tcl_word(source_dir / 'design.def')}}}")

    lines.extend(
        [
            f"must \"save binary data\" {{save_data -path {tcl_word(data_dir)}}}",
            "must \"reset data\" {reset_data}",
            f"must \"load binary data\" {{load_data -path {tcl_word(data_dir)}}}",
            f"must \"validate restored connectivity\" {{idb_validate -path {tcl_word(restored_dir / 'connectivity.json')} -check_floating {int(args.check_floating)}}}",
            f"must \"export restored view json\" {{view_json_save -path {tcl_word(restored_view)}}}",
        ]
    )

    if args.compare_def:
        lines.append(f"must \"export restored def\" {{def_save -path {tcl_word(restored_dir / 'design.def')}}}")

    lines.append("exit 0")
    return "\n".join(lines) + "\n"


def require_file(path: Path, label: str) -> None:
    if not path.is_file():
        raise SystemExit(f"{label} does not exist or is not a file: {path}")


def prepare_output_dir(out_dir: Path, force: bool) -> None:
    if out_dir.exists():
        if not force:
            raise SystemExit(f"output directory already exists, pass --force to replace it: {out_dir}")
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True)


def run_ecc(ecc_bin: Path, tcl_path: Path, out_dir: Path, extra_args: Iterable[str]) -> int:
    log_path = out_dir / "roundtrip.log"
    cmd = [str(ecc_bin), "-script", str(tcl_path), *extra_args]
    with log_path.open("w", encoding="utf-8") as log:
        log.write("$ " + " ".join(cmd) + "\n\n")
        proc = subprocess.run(cmd, cwd=REPO_ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        log.write(proc.stdout)
    return proc.returncode


def write_summary(out_dir: Path, ok: bool, run_rc: int, diffs: list[Difference]) -> None:
    summary = {
        "ok": ok,
        "ecc_returncode": run_rc,
        "difference_count": len(diffs),
        "differences": [diff.__dict__ for diff in diffs],
    }
    (out_dir / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run an iDB save_data/load_data roundtrip and compare exported snapshots.",
    )
    parser.add_argument("--ecc-bin", type=Path, default=REPO_ROOT / "bin" / "ecc_bin", help="ecc_bin executable")
    parser.add_argument("--tech-lef", type=Path, help="technology LEF/TLEF loaded with tech_lef_init")
    parser.add_argument("--lef", action="append", nargs="+", default=[], help="LEF files loaded with lef_init; may be repeated")
    parser.add_argument("--def", dest="def_file", type=Path, required=True, help="DEF file loaded with def_init")
    parser.add_argument("--out-dir", type=Path, default=REPO_ROOT / "build" / "idb_data_roundtrip", help="output directory")
    parser.add_argument("--force", action="store_true", help="replace an existing output directory")
    parser.add_argument("--check-floating", action="store_true", help="ask idb_validate to report floating pins")
    parser.add_argument("--skip-def-compare", dest="compare_def", action="store_false", help="skip DEF export and comparison")
    parser.add_argument("--diff-lines", type=int, default=200, help="maximum diff lines stored per mismatch")
    parser.add_argument("extra_tcl_args", nargs=argparse.REMAINDER, help="extra arguments passed after the Tcl script")
    args = parser.parse_args(argv)

    args.lef = [Path(path) for path in flatten(args.lef)]
    return args


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    require_file(args.ecc_bin, "ecc binary")
    require_file(args.def_file, "DEF")
    if args.tech_lef:
        require_file(args.tech_lef, "tech LEF")
    for lef in args.lef:
        require_file(lef, "LEF")
    if not args.lef and not args.tech_lef:
        raise SystemExit("at least one --lef or --tech-lef file is required")

    out_dir = args.out_dir.resolve()
    prepare_output_dir(out_dir, args.force)

    data_dir = out_dir / "data"
    tcl_path = out_dir / "roundtrip.tcl"
    tcl_path.write_text(build_tcl(args, out_dir, data_dir), encoding="utf-8")

    run_rc = run_ecc(args.ecc_bin.resolve(), tcl_path, out_dir, args.extra_tcl_args)
    if run_rc != 0:
        write_summary(out_dir, False, run_rc, [])
        print(f"[IdbRoundtrip] ecc_bin failed with exit code {run_rc}. See {out_dir / 'roundtrip.log'}", file=sys.stderr)
        return run_rc

    diffs: list[Difference] = []
    diffs.extend(compare_trees(out_dir / "source" / "view_json", out_dir / "restored" / "view_json", args.diff_lines))
    diffs.extend(compare_trees(out_dir / "source", out_dir / "restored", args.diff_lines))
    diffs = [diff for diff in diffs if not diff.path.startswith("view_json/")]

    ok = len(diffs) == 0
    write_summary(out_dir, ok, run_rc, diffs)

    if ok:
        print(f"[IdbRoundtrip] PASS: source and restored snapshots match. Output: {out_dir}")
        return 0

    print(f"[IdbRoundtrip] FAIL: {len(diffs)} snapshot differences found. Output: {out_dir}", file=sys.stderr)
    for diff in diffs[:10]:
        print(f"- {diff.path}: {diff.kind}", file=sys.stderr)
    if len(diffs) > 10:
        print(f"... {len(diffs) - 10} more differences in {out_dir / 'summary.json'}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
