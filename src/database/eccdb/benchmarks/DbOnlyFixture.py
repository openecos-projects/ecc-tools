#!/usr/bin/env python3
# ***************************************************************************************
# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2023-2025 Peng Cheng Laboratory
# Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
# Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
#
# iEDA is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
# http://license.coscl.org.cn/MulanPSL2
#
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
#
# See the Mulan PSL v2 for more details.
# ***************************************************************************************

"""Materialize a parser-excluded logical-netlist fixture from OpenDB.

This tool deliberately performs its LEF/DEF parsing before either database
benchmark starts.  The resulting JSON is the shared, parser-independent input
for construction and lookup workloads; it is not itself a timed workload.

ISPD18 test6 contains one known malformed Metal9 PRL line.  The tool applies
the same one-line temporary repair used by IspdImportComparison.py so both
fixture and import workflows consume harmonized LEF content.  The corpus input
is never modified.
"""

from __future__ import annotations

import argparse
from contextlib import contextmanager
import importlib
import json
import os
from pathlib import Path
import sys
import tempfile
from typing import Any, Iterator


REPOSITORY_ROOT = Path(__file__).resolve().parents[4]
DEFAULT_CORPUS = REPOSITORY_ROOT / "reference" / "ispd2018" / "ispd18_test6"
DEFAULT_OPENDB_PYTHONPATH = Path("/home/zhuxu1/workspace/OpenROAD/bazel-bin/src/odb")
SCHEMA = "eccdb_odb_logical_v1"

# Keep this repair byte-for-byte aligned with IspdImportComparison.py.  It is
# preparation for fixture generation, never benchmark work.
MALFORMED_METAL9_PRL = "    WIDTH 4.5   1.50\n    WIDTH 1.5   0.45 ;"
REPAIRED_METAL9_PRL = "    WIDTH 4.5   1.50 ;"

SCOPE: dict[str, Any] = {
    "connections": "connected instance ITerms on regular nets",
    "excluded": [
        "BTerms and top-level IO pins",
        "instance placement, orientation, and floorplan data",
        "physical geometry, routing, tracks, rows, blockages, and vias",
        "special nets and their instance-ITerm connections",
    ],
    "masters": "masters referenced by DEF instances with OpenDB MTerm iteration order",
    "nets": "regular nets only",
}

TEST6_ORACLE = {
    "masters": 117,
    "master_terms": 748,
    "instances": 107_919,
    "nets": 107_701,
    "connections": 474_330,
}


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lef", type=Path, default=DEFAULT_CORPUS / "ispd18_test6.input.lef")
    parser.add_argument("--def", dest="def_file", type=Path, default=DEFAULT_CORPUS / "ispd18_test6.input.def")
    parser.add_argument("--output", type=Path, required=True, help="output fixture JSON")
    parser.add_argument(
        "--opendb-pythonpath",
        type=Path,
        help="directory containing the existing OpenDB odb.py and _odb.so binding",
    )
    return parser.parse_args()


def import_opendb(module_directory: Path | None) -> Any:
    """Import the existing OpenDB binding without starting an OpenROAD process."""

    candidates: list[Path] = []
    if module_directory is not None:
        candidates.append(module_directory)
    environment_paths = os.environ.get("OPENDB_PYTHONPATH", "")
    candidates.extend(Path(entry) for entry in environment_paths.split(os.pathsep) if entry)
    candidates.append(DEFAULT_OPENDB_PYTHONPATH)

    for candidate in candidates:
        if not (candidate / "odb.py").is_file() or not (candidate / "_odb.so").is_file():
            continue
        candidate_text = str(candidate)
        if candidate_text not in sys.path:
            sys.path.insert(0, candidate_text)
        module = importlib.import_module("odb")
        if hasattr(module, "dbDatabase") and hasattr(module, "read_lef") and hasattr(module, "read_def"):
            return module
        raise RuntimeError(f"{candidate} did not provide the expected OpenDB Python API")

    try:
        module = importlib.import_module("odb")
    except ModuleNotFoundError as error:
        searched = ", ".join(str(candidate) for candidate in candidates)
        raise RuntimeError(
            "could not import the OpenDB Python binding; set OPENDB_PYTHONPATH "
            f"or pass --opendb-pythonpath (searched: {searched})"
        ) from error
    if not (hasattr(module, "dbDatabase") and hasattr(module, "read_lef") and hasattr(module, "read_def")):
        raise RuntimeError("imported odb module does not provide dbDatabase, read_lef, and read_def")
    return module


@contextmanager
def lef_for_opendb(source: Path) -> Iterator[Path]:
    """Yield source, or a private repaired test6 LEF when that known defect exists."""

    contents = source.read_text(encoding="utf-8")
    occurrences = contents.count(MALFORMED_METAL9_PRL)
    if occurrences == 0:
        yield source
        return
    if occurrences != 1:
        raise RuntimeError(f"expected one malformed Metal9 PRL row in {source}, found {occurrences}")

    with tempfile.TemporaryDirectory(prefix="eccdb-odb-logical-fixture-") as temporary_directory:
        repaired = Path(temporary_directory) / source.name
        repaired.write_text(contents.replace(MALFORMED_METAL9_PRL, REPAIRED_METAL9_PRL), encoding="utf-8")
        yield repaired


def require_unique(names: list[str], description: str) -> None:
    seen: set[str] = set()
    for name in names:
        if name in seen:
            raise RuntimeError(f"duplicate {description}: {name}")
        seen.add(name)


def ordered_master_terms(master: Any, master_name: str) -> list[str]:
    raw_terms = list(master.getMTerms())
    # dbMTerm.getIndex() is the immutable master-local ITerm order after LEF
    # import.  Fall back to OpenDB's iterable order only for an older binding
    # that does not expose it.
    if all(hasattr(term, "getIndex") for term in raw_terms):
        indexed_terms = [(int(term.getIndex()), str(term.getName())) for term in raw_terms]
        indices = [index for index, _ in indexed_terms]
        if len(indices) != len(set(indices)):
            raise RuntimeError(f"master {master_name} has duplicate MTerm indices")
        terms = [name for _, name in sorted(indexed_terms)]
    else:
        terms = [str(term.getName()) for term in raw_terms]
    if not all(terms):
        raise RuntimeError(f"master {master_name} has an empty MTerm name")
    require_unique(terms, f"MTerm name in master {master_name}")
    return terms


def build_fixture(odb_module: Any, lef: Path, def_file: Path) -> dict[str, Any]:
    database = odb_module.dbDatabase.create()
    try:
        library = odb_module.read_lef(database, str(lef))
        if library is None or database.getTech() is None:
            raise RuntimeError(f"OpenDB failed to import LEF: {lef}")
        chip = odb_module.read_def(database.getTech(), str(def_file))
        if chip is None or chip.getBlock() is None:
            raise RuntimeError(f"OpenDB failed to import DEF: {def_file}")

        block = chip.getBlock()
        referenced_masters: dict[str, list[str]] = {}
        raw_instances: list[tuple[str, str, Any]] = []
        for instance in block.getInsts():
            instance_name = str(instance.getName())
            master = instance.getMaster()
            if master is None:
                raise RuntimeError(f"instance {instance_name} has no master")
            master_name = str(master.getName())
            if not instance_name or not master_name:
                raise RuntimeError("OpenDB returned an empty instance or master name")
            terms = ordered_master_terms(master, master_name)
            previous_terms = referenced_masters.setdefault(master_name, terms)
            if previous_terms != terms:
                raise RuntimeError(f"master {master_name} has inconsistent MTerm order across instances")
            raw_instances.append((instance_name, master_name, instance))

        instance_names = [item[0] for item in raw_instances]
        require_unique(instance_names, "instance name")

        regular_net_names: set[str] = set()
        for net in block.getNets():
            if net.isSpecial():
                continue
            net_name = str(net.getName())
            if not net_name:
                raise RuntimeError("OpenDB returned an empty regular-net name")
            if net_name in regular_net_names:
                raise RuntimeError(f"duplicate regular-net name: {net_name}")
            regular_net_names.add(net_name)

        master_names = sorted(referenced_masters)
        master_index = {name: index for index, name in enumerate(master_names)}
        masters = [{"name": name, "terms": referenced_masters[name]} for name in master_names]

        raw_instances.sort(key=lambda item: item[0])
        instance_index = {name: index for index, (name, _, _) in enumerate(raw_instances)}
        instances = [[name, master_index[master_name]] for name, master_name, _ in raw_instances]

        nets = sorted(regular_net_names)
        net_index = {name: index for index, name in enumerate(nets)}
        term_index = {
            master_name: {term_name: index for index, term_name in enumerate(referenced_masters[master_name])}
            for master_name in master_names
        }

        connections: list[list[int]] = []
        for instance_name, master_name, instance in raw_instances:
            for iterm in instance.getITerms():
                net = iterm.getNet()
                # The schema has regular nets only.  A null ITerm or one on a
                # special net is intentionally outside this logical scope.
                if net is None or net.isSpecial():
                    continue
                net_name = str(net.getName())
                if net_name not in net_index:
                    raise RuntimeError(f"ITerm references regular net absent from block: {net_name}")
                mterm = iterm.getMTerm()
                if mterm is None:
                    raise RuntimeError(f"ITerm on instance {instance_name} has no MTerm")
                term_name = str(mterm.getName())
                if term_name not in term_index[master_name]:
                    raise RuntimeError(
                        f"ITerm {instance_name}/{term_name} is absent from master {master_name}'s MTerm list"
                    )
                connections.append(
                    [net_index[net_name], instance_index[instance_name], term_index[master_name][term_name]]
                )
        connections.sort()

        fixture = {
            "schema": SCHEMA,
            "scope": SCOPE,
            "masters": masters,
            "instances": instances,
            "nets": nets,
            "connections": connections,
            "counts": {
                "masters": len(masters),
                "master_terms": sum(len(master["terms"]) for master in masters),
                "instances": len(instances),
                "nets": len(nets),
                "connections": len(connections),
            },
        }
        validate_fixture(fixture)
        return fixture
    finally:
        odb_module.dbDatabase.destroy(database)


def require_nonnegative_int(value: Any, description: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value < 0:
        raise RuntimeError(f"{description} must be a non-negative integer")
    return value


def validate_fixture(fixture: dict[str, Any]) -> None:
    required_keys = {"schema", "scope", "masters", "instances", "nets", "connections", "counts"}
    if set(fixture) != required_keys:
        raise RuntimeError("fixture keys do not match eccdb_odb_logical_v1")
    if fixture["schema"] != SCHEMA:
        raise RuntimeError(f"unexpected fixture schema: {fixture['schema']}")
    scope = fixture["scope"]
    if not isinstance(scope, dict) or not isinstance(scope.get("masters"), str) or not isinstance(scope.get("nets"), str):
        raise RuntimeError("fixture scope is incomplete")
    if not isinstance(scope.get("connections"), str) or not isinstance(scope.get("excluded"), list):
        raise RuntimeError("fixture scope is incomplete")
    if not all(isinstance(entry, str) and entry for entry in scope["excluded"]):
        raise RuntimeError("fixture scope exclusions must be non-empty strings")

    masters = fixture["masters"]
    instances = fixture["instances"]
    nets = fixture["nets"]
    connections = fixture["connections"]
    counts = fixture["counts"]
    if not all(isinstance(value, list) for value in (masters, instances, nets, connections)):
        raise RuntimeError("fixture collections must be arrays")
    if not isinstance(counts, dict):
        raise RuntimeError("fixture counts must be an object")

    master_names: list[str] = []
    master_terms: list[list[str]] = []
    for index, master in enumerate(masters):
        if not isinstance(master, dict) or set(master) != {"name", "terms"}:
            raise RuntimeError(f"master {index} must have name and terms")
        name = master["name"]
        terms = master["terms"]
        if not isinstance(name, str) or not name or not isinstance(terms, list):
            raise RuntimeError(f"master {index} is malformed")
        if not all(isinstance(term, str) and term for term in terms):
            raise RuntimeError(f"master {name} has an invalid MTerm name")
        require_unique(terms, f"MTerm name in master {name}")
        master_names.append(name)
        master_terms.append(terms)
    if master_names != sorted(master_names):
        raise RuntimeError("masters must be strictly sorted by name")
    require_unique(master_names, "master name")

    instance_names: list[str] = []
    referenced_master_indices: set[int] = set()
    for index, instance in enumerate(instances):
        if not isinstance(instance, list) or len(instance) != 2:
            raise RuntimeError(f"instance {index} must be [name, master_index]")
        name = instance[0]
        master_index = require_nonnegative_int(instance[1], f"instance {index} master_index")
        if not isinstance(name, str) or not name or master_index >= len(masters):
            raise RuntimeError(f"instance {index} has a dangling or malformed master reference")
        instance_names.append(name)
        referenced_master_indices.add(master_index)
    if instance_names != sorted(instance_names):
        raise RuntimeError("instances must be strictly sorted by name")
    require_unique(instance_names, "instance name")
    if referenced_master_indices != set(range(len(masters))):
        raise RuntimeError("fixture contains a master not referenced by an instance")

    if not all(isinstance(net, str) and net for net in nets):
        raise RuntimeError("nets must be non-empty strings")
    if nets != sorted(nets):
        raise RuntimeError("nets must be strictly sorted by name")
    require_unique(nets, "net name")

    previous_connection: tuple[int, int, int] | None = None
    connected_terms: set[tuple[int, int]] = set()
    for index, connection in enumerate(connections):
        if not isinstance(connection, list) or len(connection) != 3:
            raise RuntimeError(f"connection {index} must be [net_index, instance_index, term_index]")
        net_index, instance_index, term_index = (
            require_nonnegative_int(connection[field], f"connection {index} index {field}") for field in range(3)
        )
        if net_index >= len(nets) or instance_index >= len(instances):
            raise RuntimeError(f"connection {index} has a dangling net or instance reference")
        master_index = instances[instance_index][1]
        if term_index >= len(master_terms[master_index]):
            raise RuntimeError(f"connection {index} has a dangling MTerm reference")
        current_connection = (net_index, instance_index, term_index)
        if previous_connection is not None and current_connection <= previous_connection:
            raise RuntimeError("connections must be strictly sorted by integer tuple")
        if (instance_index, term_index) in connected_terms:
            raise RuntimeError(f"instance ITerm appears on multiple fixture connections: {instance_index}/{term_index}")
        previous_connection = current_connection
        connected_terms.add((instance_index, term_index))

    expected_counts = {
        "masters": len(masters),
        "master_terms": sum(len(terms) for terms in master_terms),
        "instances": len(instances),
        "nets": len(nets),
        "connections": len(connections),
    }
    if set(counts) != set(expected_counts):
        raise RuntimeError("fixture counts have unexpected keys")
    for name, expected in expected_counts.items():
        if require_nonnegative_int(counts[name], f"count {name}") != expected:
            raise RuntimeError(f"fixture count {name} does not match its collection")


def is_test6_case(def_file: Path) -> bool:
    return def_file.parent.name in {"ispd18_test6", "test6"} or def_file.stem in {
        "ispd18_test6",
        "ispd18_test6.input",
        "test6",
    }


def check_test6_oracle(fixture: dict[str, Any], def_file: Path) -> None:
    if not is_test6_case(def_file):
        return
    actual = fixture["counts"]
    if actual != TEST6_ORACLE:
        raise RuntimeError(f"ISPD18 test6 fixture counts differ from oracle: expected {TEST6_ORACLE}, got {actual}")


def write_fixture(output: Path, fixture: dict[str, Any]) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", dir=output.parent, prefix=f".{output.name}.", suffix=".tmp", delete=False
        ) as temporary:
            temporary_path = Path(temporary.name)
            json.dump(fixture, temporary, ensure_ascii=True, sort_keys=True, separators=(",", ":"))
            temporary.write("\n")
        temporary_path.replace(output)
    finally:
        if temporary_path is not None and temporary_path.exists():
            temporary_path.unlink()


def main() -> int:
    arguments = parse_arguments()
    if not arguments.lef.is_file() or not arguments.def_file.is_file():
        raise RuntimeError("--lef and --def must name regular files")
    odb_module = import_opendb(arguments.opendb_pythonpath)
    with lef_for_opendb(arguments.lef) as odb_lef:
        fixture = build_fixture(odb_module, odb_lef, arguments.def_file)
    check_test6_oracle(fixture, arguments.def_file)
    write_fixture(arguments.output, fixture)
    print(json.dumps(fixture["counts"], sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
