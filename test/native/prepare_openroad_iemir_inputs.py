#!/usr/bin/env python3
# SPDX-License-Identifier: MulanPSL-2.0
"""Prepare one shared OpenROAD static-IR fixture for iDB and EccDB.

The OpenROAD export is deliberately an input to this program.  This program
does not run OpenROAD, iEMIR, or either database backend.  It validates the
export before making the two manifests so a backend comparison cannot silently
change power, source, or technology assumptions.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import sys
import tempfile
from dataclasses import dataclass
from decimal import Decimal, localcontext
from pathlib import Path
from typing import Any


POWER_MARKER = "# iEMIR_INSTANCE_POWER_V1"
POWER_COLUMNS = (
    "instance_name",
    "voltage_v",
    "internal_power_w",
    "switching_power_w",
    "leakage_power_w",
    "total_power_w",
    "average_current_a",
)
LAYER_COLUMNS = ("layer", "type", "resistance")
SUMMARY_COLUMNS = ("metric", "value")
SUMMARY_REQUIRED = (
    "physical_instances",
    "power_records",
    "negative_rows",
    "total_power_w",
    "activity_mode",
    "physical_only_zero_power_records",
    "dbu_per_micron",
)
NUMBER = r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?"
NUMBER_RE = re.compile(rf"^{NUMBER}$")
SOURCE_NODE_RE = re.compile(r"^Node_(?P<layer>.+)_(?P<x>-?\d+)_(?P<y>-?\d+)$")
SOURCE_NAME_RE = re.compile(r"^V\d+$")
SOURCES_HEADER_RE = re.compile(r"^\*\s+Sources\s*$", re.IGNORECASE)
FOOTER_HEADER_RE = re.compile(r"^\*\s+Footer\s*$", re.IGNORECASE)
UNITS_RE = re.compile(r"^\s*UNITS\s+DISTANCE\s+MICRONS\s+(\d+)\s*;\s*$", re.IGNORECASE)
COMPONENTS_RE = re.compile(r"^\s*COMPONENTS\s+(\d+)\s*;\s*$", re.IGNORECASE)
SECTION_RE = re.compile(r"^\s*(NETS|SPECIALNETS)\s+\d+\s*;", re.IGNORECASE)
END_SECTION_RE = re.compile(r"^\s*END\s+(NETS|SPECIALNETS)\s*$", re.IGNORECASE)
NET_RECORD_RE = re.compile(r"^\s*-\s+(\S+)")


class PreparationError(RuntimeError):
    """The supplied export cannot form a faithful iEMIR input fixture."""


@dataclass(frozen=True)
class InputFiles:
    config_path: Path
    def_file: Path
    sdc_file: Path
    spef_file: Path
    lef_files: tuple[Path, ...]
    liberty_files: tuple[Path, ...]
    activity_field: str
    activity: str
    activity_mode: str | None
    supply_net: str


@dataclass(frozen=True)
class DefMetadata:
    dbu_per_micron: int
    components: int
    supply_net_found: bool


@dataclass(frozen=True)
class PowerTable:
    records: int
    voltage_v: float
    total_power_w: float
    zero_total_power_records: int


@dataclass(frozen=True)
class ExportSummary:
    path: Path
    raw_text: str
    rows: tuple[dict[str, str], ...]
    values: dict[str, str]


@dataclass(frozen=True)
class LayerRecord:
    name: str
    layer_type: str
    resistance_raw: str
    resistance: float
    order: int


@dataclass(frozen=True)
class SpiceSource:
    name: str
    layer: str
    x_dbu: int
    y_dbu: int
    voltage_v: float


def fail(message: str) -> None:
    raise PreparationError(message)


def require_file(path: Path, description: str) -> Path:
    resolved = path.expanduser().resolve()
    if not resolved.is_file():
        fail(f"{description} is not a readable file: {resolved}")
    return resolved


def require_absolute_file(value: Any, description: str) -> Path:
    if not isinstance(value, str) or not value:
        fail(f"{description} must be a nonempty absolute path string")
    raw_path = Path(value).expanduser()
    if not raw_path.is_absolute():
        fail(f"{description} must be an absolute path: {value!r}")
    return require_file(raw_path, description)


def require_absolute_file_list(value: Any, description: str) -> tuple[Path, ...]:
    if not isinstance(value, list) or not value:
        fail(f"{description} must be a nonempty list of absolute paths")
    files = tuple(require_absolute_file(item, f"{description}[{index}]") for index, item in enumerate(value))
    if len(set(files)) != len(files):
        fail(f"{description} contains duplicate paths")
    return files


def parse_number(text: str, context: str) -> float:
    if not NUMBER_RE.fullmatch(text):
        fail(f"{context}: invalid decimal value {text!r}")
    value = float(text)
    if not math.isfinite(value):
        fail(f"{context}: non-finite decimal value {text!r}")
    return value


def parse_nonnegative_integer(text: str, context: str) -> int:
    if not re.fullmatch(r"\d+", text):
        fail(f"{context}: expected a nonnegative integer, got {text!r}")
    return int(text)


def close_enough(first: float, second: float) -> bool:
    return abs(first - second) <= max(1.0e-18, 1.0e-6 * max(abs(first), abs(second)))


def digest(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def digest_json(value: Any) -> str:
    data = json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode("utf-8")
    return hashlib.sha256(data).hexdigest()


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        mode="w", encoding="utf-8", dir=path.parent, prefix=f".{path.name}.", delete=False
    ) as temporary:
        temporary.write(text)
        temporary_path = Path(temporary.name)
    try:
        os.replace(temporary_path, path)
    finally:
        if temporary_path.exists():
            temporary_path.unlink()


def write_json(path: Path, value: Any) -> None:
    write_text(path, json.dumps(value, indent=2, sort_keys=True, ensure_ascii=True) + "\n")


def load_inputs(path: Path) -> InputFiles:
    config_path = require_file(path, "--inputs JSON")
    try:
        payload = json.loads(config_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        fail(f"invalid JSON in {config_path}: {error}")
    if not isinstance(payload, dict):
        fail(f"{config_path}: top-level JSON value must be an object")

    activity_field = ""
    activity = ""
    for candidate in ("activity", "activity_mode"):
        value = payload.get(candidate)
        if isinstance(value, str) and value:
            activity_field = candidate
            activity = value
            break
    if not activity_field:
        fail(f"{config_path}: requires an activity or activity_mode string for provenance")
    explicit_activity_mode = payload.get("activity_mode")
    if explicit_activity_mode is not None and (not isinstance(explicit_activity_mode, str) or not explicit_activity_mode):
        fail(f"{config_path}: activity_mode must be a nonempty string when present")

    supply_net = payload.get("supply_net", "VDD")
    if not isinstance(supply_net, str) or not supply_net or any(character.isspace() for character in supply_net):
        fail(f"{config_path}: supply_net must be a nonempty DEF net token")

    return InputFiles(
        config_path=config_path,
        def_file=require_absolute_file(payload.get("def"), "JSON field def"),
        sdc_file=require_absolute_file(payload.get("sdc"), "JSON field sdc"),
        spef_file=require_absolute_file(payload.get("spef"), "JSON field spef"),
        lef_files=require_absolute_file_list(payload.get("lefs"), "JSON field lefs"),
        liberty_files=require_absolute_file_list(payload.get("libs"), "JSON field libs"),
        activity_field=activity_field,
        activity=activity,
        activity_mode=explicit_activity_mode,
        supply_net=supply_net,
    )


def parse_def_metadata(path: Path, supply_net: str) -> DefMetadata:
    units: list[int] = []
    components: list[int] = []
    supply_net_found = False
    section = ""
    expected_net = supply_net.casefold()

    with path.open(encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, start=1):
            unit_match = UNITS_RE.match(line)
            if unit_match is not None:
                units.append(int(unit_match.group(1)))
            component_match = COMPONENTS_RE.match(line)
            if component_match is not None:
                components.append(int(component_match.group(1)))

            section_match = SECTION_RE.match(line)
            if section_match is not None:
                section = section_match.group(1).upper()
                continue
            end_match = END_SECTION_RE.match(line)
            if end_match is not None:
                section = ""
                continue
            if section:
                net_match = NET_RECORD_RE.match(line)
                if net_match is not None and net_match.group(1).casefold() == expected_net:
                    supply_net_found = True

    if len(units) != 1 or units[0] <= 0:
        fail(f"{path}: expected exactly one positive UNITS DISTANCE MICRONS declaration")
    if len(components) != 1:
        fail(f"{path}: expected exactly one COMPONENTS declaration")
    if components[0] <= 0:
        fail(f"{path}: COMPONENTS must be positive")
    if components[0] >= 100000:
        fail(f"{path}: COMPONENTS is {components[0]}, but this fixture is restricted to fewer than 100000 instances")
    if not supply_net_found:
        fail(f"{path}: supply net {supply_net!r} was not found in NETS or SPECIALNETS")
    return DefMetadata(dbu_per_micron=units[0], components=components[0], supply_net_found=supply_net_found)


def parse_instance_power(path: Path) -> PowerTable:
    lines = path.read_text(encoding="utf-8").splitlines()
    if len(lines) < 3 or lines[0] != POWER_MARKER:
        fail(f"{path}: first line must be the exact {POWER_MARKER!r} marker")
    if tuple(lines[1].split("\t")) != POWER_COLUMNS:
        fail(f"{path}: required seven-column iEMIR header is missing or reordered")

    names: set[str] = set()
    voltage_v: float | None = None
    powers: list[float] = []
    zero_total_power_records = 0
    for line_number, line in enumerate(lines[2:], start=3):
        fields = line.split("\t")
        if len(fields) != len(POWER_COLUMNS):
            fail(f"{path}:{line_number}: expected exactly seven tab-separated fields")
        name = fields[0]
        if not name or name != name.strip():
            fail(f"{path}:{line_number}: instance_name is empty or has surrounding whitespace")
        if name in names:
            fail(f"{path}:{line_number}: duplicate instance_name {name!r}")
        names.add(name)

        values: list[float] = []
        for column, text in zip(POWER_COLUMNS[1:], fields[1:], strict=True):
            if text != text.strip():
                fail(f"{path}:{line_number}: {column} has surrounding whitespace")
            value = parse_number(text, f"{path}:{line_number}:{column}")
            if value < 0.0:
                fail(f"{path}:{line_number}: {column} is negative; inputs are not clamped or reclassified")
            values.append(value)
        row_voltage, internal, switching, leakage, total, current = values
        if row_voltage <= 0.0:
            fail(f"{path}:{line_number}: voltage_v must be positive")
        if voltage_v is None:
            voltage_v = row_voltage
        elif not close_enough(voltage_v, row_voltage):
            fail(f"{path}:{line_number}: multiple power-table supply voltages are unsupported")

        component_sum = internal + switching + leakage
        if not math.isfinite(component_sum) or not close_enough(component_sum, total):
            fail(f"{path}:{line_number}: internal + switching + leakage does not equal total_power_w")
        vi_power = row_voltage * current
        if not math.isfinite(vi_power) or not close_enough(vi_power, total):
            fail(f"{path}:{line_number}: voltage_v * average_current_a does not equal total_power_w")
        powers.append(total)
        if total == 0.0:
            zero_total_power_records += 1

    if not names or voltage_v is None:
        fail(f"{path}: instance-power table has no records")
    total_power = math.fsum(powers)
    if not math.isfinite(total_power):
        fail(f"{path}: total instance power is non-finite")
    return PowerTable(
        records=len(names),
        voltage_v=voltage_v,
        total_power_w=total_power,
        zero_total_power_records=zero_total_power_records,
    )


def parse_export_summary(path: Path) -> ExportSummary:
    raw_text = path.read_text(encoding="utf-8")
    lines = raw_text.splitlines()
    if len(lines) < 2 or tuple(lines[0].split("\t")) != SUMMARY_COLUMNS:
        fail(f"{path}: expected exact metric/value TSV header")
    rows: list[dict[str, str]] = []
    values: dict[str, str] = {}
    for line_number, line in enumerate(lines[1:], start=2):
        fields = line.split("\t")
        if len(fields) != 2 or not fields[0] or fields[0] != fields[0].strip():
            fail(f"{path}:{line_number}: expected a nonempty metric and one value")
        if fields[0] in values:
            fail(f"{path}:{line_number}: duplicate metric {fields[0]!r}")
        values[fields[0]] = fields[1]
        rows.append({"metric": fields[0], "value": fields[1]})
    missing = [metric for metric in SUMMARY_REQUIRED if metric not in values]
    if missing:
        fail(f"{path}: missing required exporter summary metrics: {', '.join(missing)}")
    return ExportSummary(path=path, raw_text=raw_text, rows=tuple(rows), values=values)


def validate_export_summary(
    summary: ExportSummary, metadata: DefMetadata, power: PowerTable, expected_activity_mode: str | None
) -> dict[str, Any]:
    values = summary.values
    physical_instances = parse_nonnegative_integer(values["physical_instances"], f"{summary.path}:physical_instances")
    power_records = parse_nonnegative_integer(values["power_records"], f"{summary.path}:power_records")
    negative_rows = parse_nonnegative_integer(values["negative_rows"], f"{summary.path}:negative_rows")
    physical_only = parse_nonnegative_integer(
        values["physical_only_zero_power_records"], f"{summary.path}:physical_only_zero_power_records"
    )
    dbu_per_micron = parse_nonnegative_integer(values["dbu_per_micron"], f"{summary.path}:dbu_per_micron")
    total_power = parse_number(values["total_power_w"], f"{summary.path}:total_power_w")
    if total_power < 0.0:
        fail(f"{summary.path}:total_power_w is negative")
    if physical_instances != metadata.components:
        fail(f"{summary.path}: physical_instances={physical_instances} differs from DEF COMPONENTS={metadata.components}")
    if power_records != power.records:
        fail(f"{summary.path}: power_records={power_records} differs from TSV records={power.records}")
    if negative_rows != 0:
        fail(f"{summary.path}: negative_rows={negative_rows}; signed OpenSTA power needs a separate export contract")
    if not close_enough(total_power, power.total_power_w):
        fail(f"{summary.path}: total_power_w differs from the raw instance-power TSV total")
    if expected_activity_mode is not None and values["activity_mode"] != expected_activity_mode:
        fail(
            f"{summary.path}: activity_mode={values['activity_mode']!r} differs from input JSON activity_mode "
            f"{expected_activity_mode!r}"
        )
    if dbu_per_micron != metadata.dbu_per_micron:
        fail(f"{summary.path}: dbu_per_micron={dbu_per_micron} differs from DEF UNITS={metadata.dbu_per_micron}")
    if physical_only > power.zero_total_power_records:
        fail(
            f"{summary.path}: physical_only_zero_power_records={physical_only} exceeds raw zero-total-power rows "
            f"({power.zero_total_power_records})"
        )
    return {
        "physical_instances": physical_instances,
        "power_records": power_records,
        "negative_rows": negative_rows,
        "total_power_w": total_power,
        "activity_mode": values["activity_mode"],
        "physical_only_zero_power_records": physical_only,
        "dbu_per_micron": dbu_per_micron,
        "zero_total_power_records_in_raw_tsv": power.zero_total_power_records,
    }


def parse_layer_resistance(path: Path) -> list[LayerRecord]:
    lines = path.read_text(encoding="utf-8").splitlines()
    if len(lines) < 2 or tuple(lines[0].split("\t")) != LAYER_COLUMNS:
        fail(f"{path}: expected exact layer/type/resistance TSV header")
    records: list[LayerRecord] = []
    names: set[str] = set()
    for line_number, line in enumerate(lines[1:], start=2):
        fields = line.split("\t")
        if len(fields) != 3:
            fail(f"{path}:{line_number}: expected exactly three tab-separated fields")
        name, layer_type, resistance_raw = fields
        if not name or name != name.strip() or not layer_type or layer_type != layer_type.strip():
            fail(f"{path}:{line_number}: layer and type must be nonempty tokens")
        if resistance_raw != resistance_raw.strip():
            fail(f"{path}:{line_number}: resistance has surrounding whitespace")
        normalized_name = name.casefold()
        if normalized_name in names:
            fail(f"{path}:{line_number}: duplicate layer name {name!r}")
        names.add(normalized_name)
        resistance = parse_number(resistance_raw, f"{path}:{line_number}:resistance")
        if resistance < 0.0:
            fail(f"{path}:{line_number}: ODB getResistance value is negative")
        records.append(
            LayerRecord(
                name=name,
                layer_type=layer_type.upper(),
                resistance_raw=resistance_raw,
                resistance=resistance,
                order=len(records),
            )
        )
    if not records:
        fail(f"{path}: no OpenDB technology layers were exported")
    return records


def previous_routing_layer(records: list[LayerRecord], index: int) -> LayerRecord | None:
    for candidate in reversed(records[:index]):
        if candidate.layer_type == "ROUTING":
            return candidate
    return None


def next_routing_layer(records: list[LayerRecord], index: int) -> LayerRecord | None:
    for candidate in records[index + 1 :]:
        if candidate.layer_type == "ROUTING":
            return candidate
    return None


def build_technology_rules(records: list[LayerRecord]) -> tuple[list[dict[str, Any]], list[dict[str, Any]], list[dict[str, Any]]]:
    metals: list[dict[str, Any]] = []
    vias: list[dict[str, Any]] = []
    not_provided: list[dict[str, Any]] = []
    for record in records:
        if record.layer_type == "ROUTING":
            if record.resistance > 0.0:
                metals.append(
                    {
                        "layer": record.name,
                        "resistance_ohm_per_square": record.resistance,
                        "resistance_raw": record.resistance_raw,
                        "odb_layer_order": record.order,
                        "source": "OpenROAD ODB getResistance routing RPERSQ value",
                    }
                )
            else:
                not_provided.append(
                    {
                        "layer": record.name,
                        "type": record.layer_type,
                        "resistance_raw": record.resistance_raw,
                        "reason": "no positive ODB routing resistance; no METAL rule was fabricated",
                    }
                )
        elif record.layer_type == "CUT":
            if record.resistance <= 0.0:
                not_provided.append(
                    {
                        "layer": record.name,
                        "type": record.layer_type,
                        "resistance_raw": record.resistance_raw,
                        "reason": "no positive ODB CUT resistance; no VIA rule was fabricated",
                    }
                )
                continue
            lower = previous_routing_layer(records, record.order)
            upper = next_routing_layer(records, record.order)
            if lower is None or upper is None:
                fail(
                    f"CUT layer {record.name} has positive resistance but no preceding/following routing layers in "
                    "OpenDB layer order"
                )
            vias.append(
                {
                    "cut_layer": record.name,
                    "lower_layer": lower.name,
                    "upper_layer": upper.name,
                    "resistance_per_cut_ohm": record.resistance,
                    "resistance_raw": record.resistance_raw,
                    "odb_layer_order": record.order,
                    "source": "OpenROAD ODB getResistance; Sky130 LEF CUT RESISTANCE per cut, not fixed-VIA inference",
                }
            )
    if not metals:
        fail("OpenDB layer export contains no positive ROUTING resistance")
    return metals, vias, not_provided


def parse_spice_sources(path: Path) -> list[SpiceSource]:
    lines = path.read_text(encoding="utf-8").splitlines()
    source_headers = [index for index, line in enumerate(lines) if SOURCES_HEADER_RE.fullmatch(line.strip())]
    if len(source_headers) != 1:
        fail(f"{path}: expected exactly one complete '* Sources' section")
    source_start = source_headers[0]
    footer_indices = [
        index for index, line in enumerate(lines[source_start + 1 :], start=source_start + 1)
        if FOOTER_HEADER_RE.fullmatch(line.strip())
    ]
    if len(footer_indices) != 1:
        fail(f"{path}: expected exactly one '* Footer' after the Sources section")
    footer_start = footer_indices[0]
    if not any(line.strip().upper() == ".END" for line in lines[footer_start + 1 :]):
        fail(f"{path}: SPICE footer has no .END; export is incomplete")

    sources: list[SpiceSource] = []
    names: set[str] = set()
    nodes: set[tuple[str, int, int]] = set()
    for line_number, raw_line in enumerate(lines[source_start + 1 : footer_start], start=source_start + 2):
        line = raw_line.strip()
        if not line:
            continue
        fields = line.split()
        if len(fields) != 5 or not SOURCE_NAME_RE.fullmatch(fields[0]) or fields[2] != "0" or fields[3] != "DC":
            fail(f"{path}:{line_number}: Sources must contain only 'V# Node_<layer>_<x>_<y> 0 DC <voltage>' records")
        node_match = SOURCE_NODE_RE.fullmatch(fields[1])
        if node_match is None:
            fail(f"{path}:{line_number}: voltage source does not target a PDNSim Node_<layer>_<x>_<y>")
        voltage = parse_number(fields[4], f"{path}:{line_number}:source_voltage")
        if voltage <= 0.0:
            fail(f"{path}:{line_number}: source voltage must be positive")
        source = SpiceSource(
            name=fields[0],
            layer=node_match.group("layer"),
            x_dbu=int(node_match.group("x")),
            y_dbu=int(node_match.group("y")),
            voltage_v=voltage,
        )
        if not -(2**31) <= source.x_dbu < 2**31 or not -(2**31) <= source.y_dbu < 2**31:
            fail(f"{path}:{line_number}: source coordinate is outside iEMIR signed 32-bit DBU range")
        node_key = (source.layer.casefold(), source.x_dbu, source.y_dbu)
        if source.name in names or node_key in nodes:
            fail(f"{path}:{line_number}: duplicate source name or (layer, x, y) source point")
        names.add(source.name)
        nodes.add(node_key)
        sources.append(source)
    if not sources:
        fail(f"{path}: '* Sources' section contains no voltage sources")
    source_voltage = sources[0].voltage_v
    if any(not close_enough(source_voltage, source.voltage_v) for source in sources[1:]):
        fail(f"{path}: source voltages are inconsistent")
    return sources


def micron_text(value_dbu: int, dbu_per_micron: int) -> str:
    with localcontext() as context:
        context.prec = 40
        value = Decimal(value_dbu) / Decimal(dbu_per_micron)
        text = format(value, "f")
        if "." in text:
            text = text.rstrip("0").rstrip(".")
    return text if text and text != "-0" else "0"


def build_ploc(
    sources: list[SpiceSource], dbu_per_micron: int, supply_net: str, metal_names: dict[str, str]
) -> str:
    lines = [
        "# Exact OpenROAD PDNSim ideal-source nodes from write_pg_spice.",
        "# Coordinates are absolute microns, converted from DEF UNITS only.",
        "# This file models VDD sources only; it does not add VSS sources.",
        "*PLOC",
    ]
    for source in sources:
        layer = metal_names[source.layer.casefold()]
        x_um = micron_text(source.x_dbu, dbu_per_micron)
        y_um = micron_text(source.y_dbu, dbu_per_micron)
        if round(float(x_um) * dbu_per_micron) != source.x_dbu or round(float(y_um) * dbu_per_micron) != source.y_dbu:
            fail(f"cannot represent source {source.name} exactly enough in micron coordinates")
        lines.append(f"openroad_{source.name} {x_um} {y_um} {layer} {supply_net}")
    return "\n".join(lines) + "\n"


def build_technology_text(metals: list[dict[str, Any]], vias: list[dict[str, Any]]) -> str:
    lines = [
        "# Generated solely from OpenROAD layer_resistance.tsv in ODB layer order.",
        "# METAL values are positive ODB routing RPERSQ values at nominal conditions.",
        "# VIA values are positive ODB CUT values: Sky130 LEF CUT RESISTANCE per cut.",
        "# No fixed-VIA resistance inference, EM limits, or temperature coefficients are asserted.",
    ]
    for metal in metals:
        lines.append(f"METAL {metal['layer']} {{ RESISTANCE {metal['resistance_raw']} }}")
    for via in vias:
        lines.append(
            f"VIA {via['cut_layer']} {{ RESISTANCE {via['resistance_raw']} "
            f"LOWERLAYER {via['lower_layer']} UPPERLAYER {via['upper_layer']} }}"
        )
    return "\n".join(lines) + "\n"


def input_hashes(paths: list[Path]) -> dict[str, str]:
    return {str(path): digest(path) for path in sorted(set(paths), key=str)}


def build_manifest(
    backend: str,
    output_directory: Path,
    inputs: InputFiles,
    instance_power: Path,
    technology_file: Path,
    ploc_file: Path,
) -> dict[str, Any]:
    return {
        "backend": backend,
        "lef_files": [str(path) for path in inputs.lef_files],
        "def_file": str(inputs.def_file),
        "sdc_file": str(inputs.sdc_file),
        "spef_file": str(inputs.spef_file),
        "liberty_files": [str(path) for path in inputs.liberty_files],
        "output_directory": str(output_directory),
        "options": {
            "-thread_number": 4,
            "-temperature_c": 25.0,
            "-instance_power_file_path": str(instance_power),
            "-technology_file_path": str(technology_file),
            "-ploc_file_path": str(ploc_file),
        },
    }


def shared_manifest_fields(manifest: dict[str, Any]) -> dict[str, Any]:
    return {key: value for key, value in manifest.items() if key not in {"backend", "output_directory"}}


def activity_interpretation(export_activity_mode: str) -> str:
    if export_activity_mode == "clock_only":
        return "clock_only retains OpenSTA automatic clock activity and is not leakage-only."
    return "Activity is recorded exactly from the input JSON; this preparer does not alter power components."


def prepare(inputs: InputFiles, export_dir: Path, output_dir: Path) -> dict[str, Any]:
    export_dir = export_dir.expanduser().resolve()
    if not export_dir.is_dir():
        fail(f"--export-dir is not a directory: {export_dir}")
    instance_power = require_file(export_dir / "instance_power.tsv", "OpenROAD instance power TSV")
    layer_resistance = require_file(export_dir / "layer_resistance.tsv", "OpenROAD layer resistance TSV")
    spice = require_file(export_dir / "openroad-pg-bumps.sp", "OpenROAD PG bump SPICE")
    summary_file = require_file(export_dir / "power-export-summary.tsv", "OpenROAD power export summary")

    metadata = parse_def_metadata(inputs.def_file, inputs.supply_net)
    power = parse_instance_power(instance_power)
    if power.records != metadata.components:
        fail(
            f"{instance_power}: power records={power.records} differs from DEF COMPONENTS={metadata.components}; "
            "the table is not a complete physical-instance export"
        )
    summary = parse_export_summary(summary_file)
    summary_contract = validate_export_summary(summary, metadata, power, inputs.activity_mode)
    sources = parse_spice_sources(spice)
    if not close_enough(power.voltage_v, sources[0].voltage_v):
        fail(
            f"{instance_power}: power-table voltage {power.voltage_v:.12g} V differs from "
            f"SPICE source voltage {sources[0].voltage_v:.12g} V"
        )

    layer_records = parse_layer_resistance(layer_resistance)
    metal_rules, cut_rules, not_provided = build_technology_rules(layer_records)
    metal_names = {rule["layer"].casefold(): rule["layer"] for rule in metal_rules}
    missing_source_layers = sorted({source.layer for source in sources if source.layer.casefold() not in metal_names})
    if missing_source_layers:
        fail(
            "OpenROAD source layer(s) have no positive METAL resistance rule: " + ", ".join(missing_source_layers)
        )

    output_dir = output_dir.expanduser().resolve()
    technology_file = output_dir / "technology.tech"
    ploc_file = output_dir / "supply.ploc"
    write_text(technology_file, build_technology_text(metal_rules, cut_rules))
    write_text(ploc_file, build_ploc(sources, metadata.dbu_per_micron, inputs.supply_net, metal_names))
    technology_file = technology_file.resolve()
    ploc_file = ploc_file.resolve()

    manifests: dict[str, Path] = {}
    manifest_payloads: dict[str, dict[str, Any]] = {}
    for backend in ("eccdb", "idb"):
        backend_directory = (output_dir / backend).resolve()
        manifest = build_manifest(backend, backend_directory, inputs, instance_power, technology_file, ploc_file)
        manifest_path = backend_directory / "manifest.json"
        write_json(manifest_path, manifest)
        manifests[backend] = manifest_path.resolve()
        manifest_payloads[backend] = manifest
    if shared_manifest_fields(manifest_payloads["eccdb"]) != shared_manifest_fields(manifest_payloads["idb"]):
        fail("internal error: iDB and EccDB manifests diverged outside backend/output_directory")

    source_points = [
        {
            "spice_name": source.name,
            "layer": metal_names[source.layer.casefold()],
            "x_dbu": source.x_dbu,
            "y_dbu": source.y_dbu,
            "x_um": micron_text(source.x_dbu, metadata.dbu_per_micron),
            "y_um": micron_text(source.y_dbu, metadata.dbu_per_micron),
            "voltage_v": source.voltage_v,
        }
        for source in sources
    ]
    source_files = [
        inputs.config_path,
        inputs.def_file,
        inputs.sdc_file,
        inputs.spef_file,
        *inputs.lef_files,
        *inputs.liberty_files,
        instance_power,
        layer_resistance,
        spice,
        summary_file,
    ]
    generated_files = [technology_file, ploc_file, *manifests.values()]
    common_manifest = shared_manifest_fields(manifest_payloads["eccdb"])
    provenance = {
        "schema": 1,
        "preparer": str(Path(__file__).resolve()),
        "inputs_json": str(inputs.config_path),
        "input_paths": {
            "def": str(inputs.def_file),
            "sdc": str(inputs.sdc_file),
            "spef": str(inputs.spef_file),
            "lefs": [str(path) for path in inputs.lef_files],
            "libs": [str(path) for path in inputs.liberty_files],
        },
        "activity": {
            "input_json_field": inputs.activity_field,
            "input_json_string": inputs.activity,
            "input_json_activity_mode": inputs.activity_mode,
            "export_summary_activity_mode": summary_contract["activity_mode"],
            "interpretation": activity_interpretation(summary_contract["activity_mode"]),
        },
        "design": {
            "def_components": metadata.components,
            "dbu_per_micron": metadata.dbu_per_micron,
            "supply_net": inputs.supply_net,
        },
        "instance_power": {
            "path": str(instance_power),
            "records": power.records,
            "total_power_w": power.total_power_w,
            "voltage_v": power.voltage_v,
            "zero_total_power_records": power.zero_total_power_records,
            "validation": "all raw components are finite and nonnegative; component sum and V*I match each total; no clamp or reclassification was applied",
        },
        "export_summary": {
            "path": str(summary.path),
            "raw_text": summary.raw_text,
            "rows_in_original_order": list(summary.rows),
            "validated_contract": summary_contract,
            "physical_only_zero_power_note": (
                "The exporter-reported physical_only_zero_power_records are retained as raw 0 W records. "
                "This preparer does not fill, clamp, or reclassify power."
            ),
        },
        "supply": {
            "spice_file": str(spice),
            "spice_contract": "complete OpenROAD * Sources section with V# Node_<layer>_<x>_<y> 0 DC voltage records and .END footer",
            "net": inputs.supply_net,
            "voltage_v": sources[0].voltage_v,
            "source_points": source_points,
            "ploc_file": str(ploc_file),
        },
        "technology_model": {
            "layer_resistance_tsv": str(layer_resistance),
            "technology_file": str(technology_file),
            "metal_rules": metal_rules,
            "cut_rules": cut_rules,
            "not_provided": not_provided,
            "cut_rule_source": (
                "Positive CUT rows use the exported OpenROAD ODB getResistance value as Sky130 LEF CUT "
                "RESISTANCE per cut, with neighboring routing layers from ODB layer order. No fixed-VIA value is inferred."
            ),
        },
        "scope": {
            "analysis": "VDD static-IR iDB/EccDB input differential",
            "temperature": "nominal 25.0 C only",
            "em": "No EM limits or EM conclusion are provided.",
            "vss": "VSS sources and VSS-network analysis are outside this VDD-only fixture.",
        },
        "manifests": {
            "eccdb": str(manifests["eccdb"]),
            "idb": str(manifests["idb"]),
            "shared_input_manifest_sha256": digest_json(common_manifest),
            "only_backend_and_output_directory_differ": True,
        },
        "sha256": {
            "source_paths": input_hashes(source_files),
            "generated_paths": input_hashes(generated_files),
        },
    }
    provenance_path = output_dir / "provenance.json"
    write_json(provenance_path, provenance)
    return {
        "output_dir": str(output_dir),
        "def_components": metadata.components,
        "power_records": power.records,
        "total_power_w": power.total_power_w,
        "source_points": len(sources),
        "metal_rules": len(metal_rules),
        "cut_rules": len(cut_rules),
        "provenance": str(provenance_path.resolve()),
    }


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inputs", required=True, type=Path, help="JSON with absolute def/sdc/spef/lefs/libs paths and activity string")
    parser.add_argument("--export-dir", required=True, type=Path, help="OpenROAD directory containing TSV/SPICE exports")
    parser.add_argument("--output-dir", required=True, type=Path, help="case directory for generated technology, PLOC, manifests, and provenance")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    arguments = parse_args(argv)
    try:
        result = prepare(load_inputs(arguments.inputs), arguments.export_dir, arguments.output_dir)
    except PreparationError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True, ensure_ascii=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
