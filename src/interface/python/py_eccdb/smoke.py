#!/usr/bin/env python3
"""Small integration check for the optional ``ecc_py.eccdb`` submodule.

The script intentionally takes existing LEF/DEF paths.  It does not create a
second object model and can be used against any small valid design fixture:

  python smoke.py --lef tech.lef --lef cells.lef --def placed.def --output out
  python smoke.py --lef tech.lef --def placed.def --output out --viewer-first
"""

# ***************************************************************************************
# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2026 Peng Cheng Laboratory
# ***************************************************************************************

from __future__ import annotations

import argparse
import importlib
import json
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lef", action="append", required=True, type=Path)
    parser.add_argument("--def", dest="def_file", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument(
        "--viewer-first",
        action="store_true",
        help="import eccdb_viewer before ecc_py to exercise pybind module-local registration",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    if args.viewer_first:
        importlib.import_module("eccdb_viewer")

    ecc_py = importlib.import_module("ecc_py")
    if not hasattr(ecc_py, "eccdb"):
        raise RuntimeError("ecc_py was built without the native EccDB scripting entry")
    assert callable(ecc_py.eccdb.run_tool)

    database = ecc_py.eccdb.open_lef_def(args.lef, args.def_file)
    info = json.loads(database.info_json())
    assert isinstance(info, dict), info
    assert isinstance(database.diagnostics(), list)

    text_def = args.output / "roundtrip.def"
    technology = args.output / "technology.eccdb"
    library = args.output / "library.eccdb"
    design = args.output / "design.eccdb"
    database.write_def(text_def)
    database.write_binary(technology, library, design)
    assert text_def.is_file() and text_def.stat().st_size > 0
    assert technology.is_file() and library.is_file() and design.is_file()

    reopened = ecc_py.eccdb.open_binary(technology, library, design)
    assert json.loads(reopened.info_json())
    try:
        database.run_tool("not-a-tool")
    except Exception:
        pass
    else:
        raise AssertionError("unsupported tool did not report an error")

    # Importing Viewer after ecc_py verifies the inverse registration order.
    if not args.viewer_first:
        importlib.import_module("eccdb_viewer")

    print("ecc_py.eccdb smoke: PASS")


if __name__ == "__main__":
    main()
