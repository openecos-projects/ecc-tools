#!/usr/bin/env python3
# ***************************************************************************************
# Copyright (c) 2023-2025 Peng Cheng Laboratory
# Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
# Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
#
# iEDA is licensed under Mulan PSL v2.
# ***************************************************************************************

import argparse
import json
from pathlib import Path
from typing import Any

VALID_ORIENTS = {
    "N_R0",
    "W_R90",
    "S_R180",
    "E_R270",
    "FN_MY",
    "FS_MX",
    "FW_MX90",
    "FE_MY90",
    "N",
    "W",
    "S",
    "E",
    "FN",
    "FS",
    "FW",
    "FE",
    "R0",
    "R90",
    "R180",
    "R270",
    "MY",
    "MX",
    "MX90",
    "MY90",
}
VALID_STATUSES = {"NONE", "FIXED", "COVER", "PLACED", "UNPLACED"}


class ViewJsonError(RuntimeError):
    pass


def load_json(root: Path, relative_path: str) -> Any:
    path = root / relative_path
    if not path.is_file():
        raise ViewJsonError(f"missing file: {relative_path}")
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def require_array_data(root: Path, relative_path: str) -> list[dict[str, Any]]:
    data = load_json(root, relative_path).get("data")
    if not isinstance(data, list):
        raise ViewJsonError(f"{relative_path}: data is not an array")
    for index, item in enumerate(data):
        if not isinstance(item, dict):
            raise ViewJsonError(f"{relative_path}: data[{index}] is not an object")
    return data


def validate_manifest(root: Path) -> dict[str, Any]:
    manifest = load_json(root, "manifest.json")
    if manifest.get("schema") != "ieda.view.v1":
        raise ViewJsonError("manifest.json: unsupported schema")
    if manifest.get("format") != "layout_view_package":
        raise ViewJsonError("manifest.json: unsupported format")
    files = manifest.get("files")
    if not isinstance(files, dict):
        raise ViewJsonError("manifest.json: files is not an object")
    for key, relative_path in files.items():
        if not isinstance(relative_path, str):
            raise ViewJsonError(f"manifest.json: files.{key} is not a string")
        if not (root / relative_path).is_file():
            raise ViewJsonError(f"manifest.json: declared file is missing: {relative_path}")
    return manifest


def validate_dense_ids(root: Path, relative_path: str) -> None:
    data = require_array_data(root, relative_path)
    for index, item in enumerate(data):
        if "id" not in item:
            continue
        if item["id"] != index:
            raise ViewJsonError(f"{relative_path}: data[{index}].id is {item['id']}, expected {index}")


def validate_id(value: Any, count: int, owner: str, field: str, nullable: bool = False) -> None:
    if nullable and value is None:
        return
    if not isinstance(value, int):
        raise ViewJsonError(f"{owner}: {field} is not an integer id")
    if value < 0 or value >= count:
        raise ViewJsonError(f"{owner}: {field}={value} is out of range [0, {count})")


def validate_bbox(value: Any, owner: str, field: str = "bbox") -> None:
    if not (isinstance(value, list) and len(value) == 4 and all(isinstance(v, int) for v in value)):
        raise ViewJsonError(f"{owner}: {field} is not [lx, ly, ux, uy]")
    if value[0] > value[2] or value[1] > value[3]:
        raise ViewJsonError(f"{owner}: {field} has inverted bbox")


def validate_layers(value: Any, layer_count: int, owner: str) -> None:
    if not isinstance(value, list):
        raise ViewJsonError(f"{owner}: layers is not an array")
    previous = -1
    for index, layer_id in enumerate(value):
        validate_id(layer_id, layer_count, owner, f"layers[{index}]")
        if layer_id <= previous:
            raise ViewJsonError(f"{owner}: layers must be sorted and unique")
        previous = layer_id


def validate_layer_shape(shape: Any, layer_count: int, owner: str) -> None:
    if not isinstance(shape, dict):
        raise ViewJsonError(f"{owner}: layer shape is not an object")
    validate_id(shape.get("layer_id"), layer_count, owner, "layer_id")
    rects = shape.get("rects")
    if not isinstance(rects, list):
        raise ViewJsonError(f"{owner}: rects is not an array")
    for index, rect in enumerate(rects):
        if not (isinstance(rect, list) and len(rect) == 4 and all(isinstance(v, int) for v in rect)):
            raise ViewJsonError(f"{owner}: rects[{index}] is not [lx, ly, ux, uy]")
        if rect[0] > rect[2] or rect[1] > rect[3]:
            raise ViewJsonError(f"{owner}: rects[{index}] has inverted bbox")


def validate_references(root: Path, files: dict[str, str]) -> None:
    layers = require_array_data(root, files["layers"])
    vias = require_array_data(root, files["vias"])
    cell_masters = require_array_data(root, files["cell_masters"])
    instances = require_array_data(root, files["instances"])
    io_pins = require_array_data(root, files["io_pins"])
    regular_nets = require_array_data(root, files["regular_nets"])
    special_nets = require_array_data(root, files["special_nets"])

    layer_count = len(layers)
    via_count = len(vias)
    cell_master_count = len(cell_masters)
    instance_count = len(instances)
    io_pin_count = len(io_pins)
    regular_net_count = len(regular_nets)
    special_net_count = len(special_nets)

    for index, via in enumerate(vias):
        for shape_index, shape in enumerate(via.get("shapes", [])):
            validate_layer_shape(shape, layer_count, f"{files['vias']} data[{index}].shapes[{shape_index}]")

    for index, master in enumerate(cell_masters):
        for pin_index, pin in enumerate(master.get("pins", [])):
            for port_index, port in enumerate(pin.get("ports", [])):
                validate_layer_shape(port, layer_count, f"{files['cell_masters']} data[{index}].pins[{pin_index}].ports[{port_index}]")
        for obs_index, obs in enumerate(master.get("obs", [])):
            validate_layer_shape(obs, layer_count, f"{files['cell_masters']} data[{index}].obs[{obs_index}]")

    for index, inst in enumerate(instances):
        validate_id(inst.get("master_id"), cell_master_count, f"{files['instances']} data[{index}]", "master_id")
        validate_bbox(inst.get("bbox"), f"{files['instances']} data[{index}]")

    for index, pin in enumerate(io_pins):
        owner = f"{files['io_pins']} data[{index}]"
        validate_id(pin.get("net_id"), regular_net_count, f"{files['io_pins']} data[{index}]", "net_id", nullable=True)
        validate_id(pin.get("special_net_id"), special_net_count, f"{files['io_pins']} data[{index}]", "special_net_id", nullable=True)
        validate_bbox(pin.get("bbox"), owner)
        validate_layers(pin.get("layers"), layer_count, owner)
        for port_index, port in enumerate(pin.get("ports", [])):
            validate_layer_shape(port, layer_count, f"{files['io_pins']} data[{index}].ports[{port_index}]")
        for via_index, via in enumerate(pin.get("vias", [])):
            validate_id(via.get("via_master_id"), via_count, f"{files['io_pins']} data[{index}].vias[{via_index}]", "via_master_id")

    for index, net in enumerate(regular_nets):
        for pin_index, pin in enumerate(net.get("pins", [])):
            if pin.get("type") == "instance":
                validate_id(pin.get("inst_id"), instance_count, f"{files['regular_nets']} data[{index}].pins[{pin_index}]", "inst_id")
            elif pin.get("type") == "io":
                validate_id(pin.get("pin_id"), io_pin_count, f"{files['regular_nets']} data[{index}].pins[{pin_index}]", "pin_id")

    for index, net in enumerate(special_nets):
        for pin_index, pin in enumerate(net.get("pins", [])):
            if pin.get("type") == "instance":
                validate_id(pin.get("inst_id"), instance_count, f"{files['special_nets']} data[{index}].pins[{pin_index}]", "inst_id")
            elif pin.get("type") == "io":
                validate_id(pin.get("pin_id"), io_pin_count, f"{files['special_nets']} data[{index}].pins[{pin_index}]", "pin_id")

    for key, net_field in (("regular_wires", "net_id"), ("special_wires", "special_net_id")):
        for index, wire in enumerate(require_array_data(root, files[key])):
            owner = f"{files[key]} data[{index}]"
            if key == "regular_wires":
                validate_id(wire.get(net_field), regular_net_count, owner, net_field)
            else:
                validate_id(wire.get(net_field), special_net_count, owner, net_field)
            validate_bbox(wire.get("bbox"), owner)
            validate_layers(wire.get("layers"), layer_count, owner)
            if wire.get("kind") in ("path", "patch"):
                validate_id(wire.get("layer_id"), layer_count, owner, "layer_id")
            elif wire.get("kind") == "via":
                validate_id(wire.get("via_master_id"), via_count, owner, "via_master_id")

    for key in ("blockages", "fills"):
        if key not in files:
            continue
        for index, item in enumerate(require_array_data(root, files[key])):
            owner = f"{files[key]} data[{index}]"
            validate_bbox(item.get("bbox"), owner)
            validate_layers(item.get("layers"), layer_count, owner)
            if "layer_id" in item:
                validate_id(item.get("layer_id"), layer_count, owner, "layer_id")
            if item.get("kind") == "via":
                validate_id(item.get("via_master_id"), via_count, owner, "via_master_id")


def validate_point(value: Any, owner: str, field: str) -> None:
    if not (isinstance(value, list) and len(value) == 2 and all(isinstance(v, int) for v in value)):
        raise ViewJsonError(f"{owner}: {field} is not [x, y]")


def validate_optional_orient(value: Any, owner: str, field: str = "orient", required: bool = False) -> None:
    if value is None and not required:
        return
    if not isinstance(value, str):
        raise ViewJsonError(f"{owner}: {field} is not a string")
    if value.upper() not in VALID_ORIENTS:
        raise ViewJsonError(f"{owner}: {field} has unsupported orient {value!r}")


def validate_optional_status(value: Any, owner: str, field: str = "status", required: bool = False) -> None:
    if value is None and not required:
        return
    if not isinstance(value, str):
        raise ViewJsonError(f"{owner}: {field} is not a string")
    if value.upper() not in VALID_STATUSES:
        raise ViewJsonError(f"{owner}: {field} has unsupported status {value!r}")


def validate_dirty_list(value: Any, count: int, owner: str) -> None:
    if not isinstance(value, list):
        raise ViewJsonError(f"{owner}: dirty list is not an array")
    previous = -1
    for index, item_id in enumerate(value):
        validate_id(item_id, count, owner, f"[{index}]")
        if item_id <= previous:
            raise ViewJsonError(f"{owner}: ids must be sorted and unique")
        previous = item_id


def validate_edit_overlay(root: Path, files: dict[str, str], counts: dict[str, int]) -> None:
    relative_path = files.get("layout_edits")
    if not relative_path:
        return

    edits = load_json(root, relative_path)
    if not isinstance(edits, dict):
        raise ViewJsonError(f"{relative_path}: root is not an object")
    if edits.get("schema") != "ieda.view.edit.v1":
        raise ViewJsonError(f"{relative_path}: unsupported schema")
    if edits.get("kind") != "layout_edits":
        raise ViewJsonError(f"{relative_path}: unsupported kind")

    data = edits.get("data")
    if not isinstance(data, list):
        raise ViewJsonError(f"{relative_path}: data is not an array")

    dirty = edits.get("dirty")
    if dirty is not None:
        if not isinstance(dirty, dict):
            raise ViewJsonError(f"{relative_path}: dirty is not an object")
        for key in ("instances", "io_pins", "regular_nets", "special_nets"):
            if key in dirty:
                validate_dirty_list(dirty[key], counts[key], f"{relative_path}: dirty.{key}")

    for index, edit in enumerate(data):
        owner = f"{relative_path} data[{index}]"
        if not isinstance(edit, dict):
            raise ViewJsonError(f"{owner}: edit is not an object")
        op = edit.get("op")
        if not isinstance(op, str):
            raise ViewJsonError(f"{owner}: op is not a string")

        if op == "move_instance":
            validate_id(edit.get("inst_id"), counts["instances"], owner, "inst_id")
            validate_point(edit.get("origin"), owner, "origin")
            validate_optional_orient(edit.get("orient"), owner)
            validate_optional_status(edit.get("status"), owner)
        elif op == "orient_instance":
            validate_id(edit.get("inst_id"), counts["instances"], owner, "inst_id")
            validate_optional_orient(edit.get("orient"), owner, required=True)
        elif op == "set_status":
            validate_id(edit.get("inst_id"), counts["instances"], owner, "inst_id")
            validate_optional_status(edit.get("status"), owner, required=True)
        elif op == "move_io_pin":
            validate_id(edit.get("pin_id"), counts["io_pins"], owner, "pin_id")
            validate_point(edit.get("location"), owner, "location")
            validate_optional_orient(edit.get("orient"), owner)
        elif op == "orient_io_pin":
            validate_id(edit.get("pin_id"), counts["io_pins"], owner, "pin_id")
            validate_optional_orient(edit.get("orient"), owner, required=True)
        elif op == "delete_edit":
            continue
        else:
            raise ViewJsonError(f"{owner}: unsupported op {op!r}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate an iEDA layout view JSON package.")
    parser.add_argument("output_dir", type=Path, help="view_json_save output directory")
    args = parser.parse_args()

    root = args.output_dir.resolve()
    manifest = validate_manifest(root)
    files = manifest["files"]

    for relative_path in files.values():
        json_file = load_json(root, relative_path)
        if isinstance(json_file.get("data"), list):
            validate_dense_ids(root, relative_path)

    validate_references(root, files)
    counts = {
        "instances": len(require_array_data(root, files["instances"])),
        "io_pins": len(require_array_data(root, files["io_pins"])),
        "regular_nets": len(require_array_data(root, files["regular_nets"])),
        "special_nets": len(require_array_data(root, files["special_nets"])),
    }
    validate_edit_overlay(root, files, counts)
    print(f"OK: {root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
