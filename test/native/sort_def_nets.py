#!/usr/bin/env python3
# SPDX-License-Identifier: MulanPSL-2.0
"""Make an experiment-only DEF copy with regular NETS records name-sorted.

The source DEF is never modified.  Only top-level ``NETS`` records are
reordered; COMPONENTS, SPECIALNETS and every record's text remain byte-for-
byte unchanged.  This isolates iRT's net-index/order effect from geometry and
connectivity changes.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re


def sort_nets(text: str) -> str:
    match = re.search(r"(?ms)^(?P<header>\s*NETS\s+\d+\s*;\s*\n)(?P<body>.*?)(?P<footer>^\s*END\s+NETS\s*$)", text)
    if match is None:
        raise ValueError("DEF has no NETS section")
    body = match.group("body")
    records: list[str] = []
    current: list[str] = []
    for line in body.splitlines(keepends=True):
        if not current and line.lstrip().startswith("-"):
            current.append(line)
        elif current:
            current.append(line)
        elif line.strip():
            raise ValueError(f"unexpected content in NETS section: {line!r}")
        if current and ";" in line:
            records.append("".join(current))
            current.clear()
    if current:
        raise ValueError("unterminated NETS record")
    def key(record: str) -> str:
        first = record.lstrip().split(None, 2)
        if len(first) < 2 or first[0] != "-":
            raise ValueError(f"invalid NETS record: {record[:100]!r}")
        return first[1]
    records.sort(key=key)
    replacement = match.group("header") + "".join(records) + match.group("footer")
    return text[: match.start()] + replacement + text[match.end() :]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    source = args.source.resolve()
    output = args.output.resolve()
    if source == output:
        raise SystemExit("refusing to overwrite source DEF")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(sort_nets(source.read_text(encoding="utf-8")), encoding="utf-8")
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
