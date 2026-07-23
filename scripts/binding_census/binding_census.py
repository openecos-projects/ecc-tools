#!/usr/bin/env python
"""Binding census for the ecc_py pybind11 module — CLI entry point.

Two cleanly separated parts:

1. Discovery (``lexer``): a small lexical scanner (no cross-line regex) that
   walks every register file with a state machine (code / line comment /
   block comment / string literal / char literal) and tracks paren/brace/
   bracket depth. It finds every module-level ``m.def(`` statement
   (including multiline and commented-out ones), extracts the
   ``py::arg("name") = default`` entries at paren depth 1, and resolves
   whether the binding is active or disabled (commented statement, or its
   enclosing register function is never called from ``python_moodule.cc``).

2. Semantics (curated spec): a hand-maintained JSON table
   (``binding_spec.json``) carrying, per binding parameter, the old/new C++
   types and defaults, scalar/list shape, required/optional shape, and the
   path / non_path / ambiguous classification with a written rationale.

The manifest (``manifest.json``) is the generated join of discovery + spec
(``manifest`` module), one entry per (binding, in-scope parameter). Only
string-carrying parameters (``std::string`` / ``std::vector<std::string>``-
typed, plus string containers such as ``std::set<std::string>``) are in
scope: they are the path candidates and name candidates needing adjudication.

Run via uv from the repository root:

    uv run --no-project --with jsonschema python scripts/binding_census/binding_census.py
    uv run --no-project --with jsonschema python scripts/binding_census/binding_census.py --check
"""
import argparse
import json
import sys
from pathlib import Path

from dead_bindings import audit_wrapper, render_dead_bindings_md
from lexer import discover, discovery_to_json
from manifest import check, generate_manifest_bytes

CENSUS_DIR = Path(__file__).resolve().parent
DEFAULT_REPO_ROOT = CENSUS_DIR.parents[1]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="run the census gates and exit nonzero on failure")
    parser.add_argument("--discovery", action="store_true", help="print discovery JSON to stdout")
    parser.add_argument(
        "--ecc-root",
        type=Path,
        default=None,
        help="path to the outer ecc repo; additionally writes dead_bindings.md",
    )
    parser.add_argument("--repo-root", type=Path, default=DEFAULT_REPO_ROOT, help=argparse.SUPPRESS)
    parser.add_argument("--census-dir", type=Path, default=CENSUS_DIR, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)

    repo_root: Path = args.repo_root.resolve()
    census_dir: Path = args.census_dir.resolve()

    if args.discovery:
        print(json.dumps(discovery_to_json(discover(repo_root)), indent=2, sort_keys=True))
        return 0

    if args.check:
        failures = check(repo_root, census_dir)
        if failures:
            for failure in failures:
                print(f"binding census check FAILED: {failure}", file=sys.stderr)
            return 1
        print("binding census check passed")
        return 0

    manifest_bytes = generate_manifest_bytes(repo_root, census_dir)
    (census_dir / "manifest.json").write_bytes(manifest_bytes)
    print(f"wrote {census_dir / 'manifest.json'}")

    if args.ecc_root is not None:
        module_py = args.ecc_root.resolve() / "chipcompiler/tools/ecc/module.py"
        audit = audit_wrapper(module_py, discover(repo_root))
        out = census_dir / "dead_bindings.md"
        out.write_text(render_dead_bindings_md(audit))
        print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
