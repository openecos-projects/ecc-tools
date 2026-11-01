#!/usr/bin/env python3
# SPDX-License-Identifier: MulanPSL-2.0
"""Prepare an isolated Ciel HD LEF with upstream conb diffusion annotations.

This experiment changes exactly the HI/LO ANTENNADIFFAREA statements.  It
retains the Ciel physical view and never edits the installed PDK in place.
"""

from __future__ import annotations

import argparse
import difflib
import hashlib
import json
from pathlib import Path
import re


MACRO = b"sky130_fd_sc_hd__conb_1"


def fingerprint(path: Path, content: bytes) -> dict[str, object]:
    return {"path": str(path.resolve()), "bytes": len(content),
            "sha256": hashlib.sha256(content).hexdigest()}


def named_block(content: bytes, keyword: bytes, name: bytes) -> re.Match[bytes]:
    pattern = (rb"(?m)^[ \t]*" + keyword + rb"[ \t]+" + re.escape(name)
               + rb"[ \t]*\r?\n[\s\S]*?^[ \t]*END[ \t]+" + re.escape(name) + rb"[ \t]*(?:\r?\n|$)")
    blocks = list(re.finditer(pattern, content))
    if len(blocks) != 1:
        raise ValueError(f"expected exactly one {keyword.decode()} {name.decode()}, found {len(blocks)}")
    return blocks[0]


def prepare(installed: bytes, upstream: bytes) -> tuple[bytes, list[dict[str, str]]]:
    target_macro = named_block(installed, b"MACRO", MACRO)
    source_macro = named_block(upstream, b"MACRO", MACRO).group()
    patched_macro = target_macro.group()
    reverse = []
    annotations = []
    for pin in (b"HI", b"LO"):
        source_pin = named_block(source_macro, b"PIN", pin).group()
        fields = re.findall(rb"(?m)^[ \t]*(ANTENNADIFFAREA[ \t]+[^;\r\n]+[ \t]*;)[ \t]*\r?$", source_pin)
        if len(fields) != 1:
            raise ValueError(f"expected one upstream ANTENNADIFFAREA on {pin.decode()}")
        statement = fields[0]
        target_pin = named_block(patched_macro, b"PIN", pin)
        original_pin = target_pin.group()
        if re.search(rb"\bANTENNADIFFAREA\b", original_pin):
            raise ValueError(f"target {pin.decode()} already declares ANTENNADIFFAREA")
        header = re.match(rb"([ \t]*)PIN[^\r\n]*(\r?\n)", original_pin)
        assert header is not None
        inserted_line = header[1] + b"  " + statement + header[2]
        patched_pin = original_pin[:header.end()] + inserted_line + original_pin[header.end():]
        patched_macro = patched_macro[:target_pin.start()] + patched_pin + patched_macro[target_pin.end():]
        reverse.append((patched_pin, original_pin))
        annotations.append({"macro": MACRO.decode(), "pin": pin.decode(), "statement": statement.decode()})

    restored = patched_macro
    for patched_pin, original_pin in reversed(reverse):
        restored = restored.replace(patched_pin, original_pin, 1)
    if restored != target_macro.group():
        raise ValueError("patch unexpectedly altered content beyond the two pin statements")
    output = installed[:target_macro.start()] + patched_macro + installed[target_macro.end():]
    delta = list(difflib.ndiff(installed.decode().splitlines(), output.decode().splitlines()))
    added = [line[2:].strip() for line in delta if line.startswith("+ ")]
    removed = [line for line in delta if line.startswith("- ")]
    if removed or added != [item["statement"] for item in annotations]:
        raise ValueError("LEF diff must contain exactly the two upstream antenna statements and no removed lines")
    return output, annotations


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--installed-lef", type=Path, required=True)
    parser.add_argument("--upstream-lef", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--upstream-url", required=True)
    parser.add_argument("--manifest", type=Path, action="append", default=[])
    args = parser.parse_args()
    if args.out.exists():
        parser.error("--out must be a new experiment directory")
    installed = args.installed_lef.read_bytes()
    upstream = args.upstream_lef.read_bytes()
    output, annotations = prepare(installed, upstream)
    args.out.mkdir(parents=True)
    lef_path = args.out / "sky130_fd_sc_hd.conb-antenna.lef"
    lef_path.write_bytes(output)
    (args.out / "antenna-only.patch").write_text("".join(difflib.unified_diff(
        installed.decode().splitlines(keepends=True), output.decode().splitlines(keepends=True),
        fromfile=str(args.installed_lef), tofile=str(lef_path))))
    manifests = []
    for manifest_path in args.manifest:
        manifest = json.loads(manifest_path.read_text())
        replaced = 0
        for index, name in enumerate(manifest["lef_files"]):
            original = (manifest_path.parent / name).resolve()
            if original == args.installed_lef.resolve():
                manifest["lef_files"][index] = str(lef_path.resolve())
                replaced += 1
            else:
                manifest["lef_files"][index] = str(original)
        if replaced != 1:
            raise ValueError(f"expected one installed HD LEF reference in {manifest_path}")
        manifest["def_file"] = str((manifest_path.parent / manifest["def_file"]).resolve())
        destination = args.out / manifest_path.name
        destination.write_text(json.dumps(manifest, indent=2) + "\n")
        manifests.append({"original": str(manifest_path.resolve()), "patched": str(destination.resolve())})
    report = {"schema": 1, "source_url": args.upstream_url,
              "installed_lef": fingerprint(args.installed_lef, installed),
              "upstream_lef": fingerprint(args.upstream_lef, upstream),
              "patched_lef": fingerprint(lef_path, output), "annotations_added": annotations,
              "geometry_and_other_bytes_unchanged": True, "manifests": manifests}
    (args.out / "preparation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
