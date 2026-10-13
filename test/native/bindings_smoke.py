# SPDX-License-Identifier: MulanPSL-2.0
"""Verify the native Python owner, inspections, export and release lifecycle."""

import json
from pathlib import Path
import re
import sys
import tempfile

import ecc_py


def main(manifest_file: str) -> None:
    manifest_path = Path(manifest_file).resolve()
    manifest = json.loads(manifest_path.read_text())
    base = manifest_path.parent

    def resolve(path: str) -> str:
        value = Path(path)
        return str(value if value.is_absolute() else base / value)

    lefs = [resolve(path) for path in manifest["lef_files"]]
    def_file = resolve(manifest["def_file"])
    components = Path(def_file).read_text().split("COMPONENTS ", 1)[1].split("END COMPONENTS", 1)[0]
    instance_name = re.search(r"^\s*-\s+(\S+)", components, re.MULTILINE)[1]
    flow = ecc_py.NativeToolchain(lefs, def_file)
    before = flow.summary()
    assert before["backend"] == "eccdb" and before["instances"] > 0
    instance = flow.inspect_instance(instance_name)
    assert instance["name"] == instance_name and instance["master"]
    assert instance["terms"]
    for term in instance["terms"]:
        assert term["name"]
        if term["net"]:
            net = flow.inspect_net(term["net"])
            assert any(item["term_id"] == term["id"] for item in net["instance_terms"])
    try:
        ecc_py.NativeToolchain(lefs, def_file)
    except RuntimeError as error:
        assert "one active NativeToolchain" in str(error)
    else:
        raise AssertionError("Two owners were allowed to share Operation singletons")
    output = Path(tempfile.mkdtemp(prefix="native-bindings-", dir=base))
    exported = output / "roundtrip.def"
    flow.write_def(str(exported))
    flow.write_binary(str(output))
    del flow
    binary = ecc_py.NativeToolchain(str(output))
    restored_instance = binary.inspect_instance(instance_name)
    assert restored_instance == instance, "Native instance/term IDs or connectivity changed after binary reload"
    binary_summary = binary.summary()
    for field in ("instances", "instance_terms", "block_terms", "block_pins", "nets", "wires", "design_shapes"):
        assert before[field] == binary_summary[field], (field, before, binary_summary)
    del binary
    restored = ecc_py.NativeToolchain(lefs, str(exported))
    after = restored.summary()
    for field in ("instances", "instance_terms", "block_terms", "block_pins", "nets"):
        assert before[field] == after[field], (field, before, after)
    del restored
    print(json.dumps({"status": "passed", "summary": before, "instance": instance_name, "artifacts": str(output)}))


if __name__ == "__main__":
    main(sys.argv[1])
