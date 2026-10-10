"""Check placement RC and required-time seeds against the loaded fixture inputs."""

import json
import math
import re
from pathlib import Path


def assert_placement_timing_defaults(place_db, manifest):
    config = json.loads(Path(manifest["config"]["db_ecc"]).read_text())
    layer_name = config["LayerSettings"]["routing_layer_1st"]
    tech_lef = Path(manifest["pdk"]["tech_lef"]).read_text()
    layer = re.search(
        rf"\bLAYER\s+{re.escape(layer_name)}\b(.*?)\bEND\s+{re.escape(layer_name)}\b",
        tech_lef,
        re.DOTALL,
    ).group(1)

    def value(pattern):
        return float(re.search(pattern + r"\s+(\S+)\s*;", layer).group(1))

    width = value(r"\bWIDTH")
    expected_rc = (
        value(r"\bRESISTANCE\s+RPERSQ") / width,
        value(r"\bCAPACITANCE\s+CPERSQDIST") * width + 2 * value(r"\bEDGECAPACITANCE"),
    )
    actual_rc = (float(place_db.r_unit), float(place_db.c_unit))
    assert all(
        math.isclose(actual, expected, rel_tol=1e-12)
        for actual, expected in zip(actual_rc, expected_rc, strict=True)
    ), (actual_rc, expected_rc)
    assert any(
        f"LEF routing layer {layer_name} " in str(item) for item in place_db.timing_diagnostics
    )

    # This fixture has one ideal rising-edge clock and no output delays.
    assert len(place_db.timing_clock_info) == 1
    clock = place_db.timing_clock_info[0]
    capture_deadline = float(clock[1]) - float(clock[4])
    setup_pins = {int(arc[1]) for arc in place_db.endpoints_constraint_arcs}
    assert setup_pins
    corrected = 0
    preserved = 0
    for index, pin_id in enumerate(place_db.end_points):
        seeds = (
            float(place_db.endpoints_rRAT[index]),
            float(place_db.endpoints_fRAT[index]),
        )
        checked = (
            float(place_db.backend_endpoint_rRAT[index]),
            float(place_db.backend_endpoint_fRAT[index]),
        )
        if int(pin_id) in setup_pins:
            assert all(
                math.isclose(seed, capture_deadline, rel_tol=0, abs_tol=1e-3) for seed in seeds
            ), (str(place_db.pin_names[int(pin_id)]), seeds, capture_deadline)
            corrected += int(
                any(abs(seed - rat) > 1e-3 for seed, rat in zip(seeds, checked, strict=True))
            )
        elif ":" in str(place_db.pin_names[int(pin_id)]):
            # Recovery/removal endpoints do not receive the setup correction.
            assert seeds == checked
            preserved += 1
        else:
            assert seeds == (9e7, 9e7)
            preserved += 1
    assert corrected > 0
    assert preserved > 0
