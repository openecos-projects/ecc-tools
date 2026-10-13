import csv
import json
from pathlib import Path

from support import run_scenario


def test_placement_map(test_roots):
    result = run_scenario(test_roots, "placement_map", timeout=480)
    summary = json.loads(result.output_path("feature").read_text(encoding="utf-8"))
    maps = {}
    for direction, filename in summary["Congestion"]["map"]["egr"].items():
        with Path(filename).open(encoding="utf-8") as stream:
            maps[direction] = [list(map(int, row)) for row in csv.reader(stream)]

    horizontal, vertical = maps["horizontal"], maps["vertical"]
    assert horizontal and horizontal[0]
    assert [len(row) for row in horizontal] == [len(row) for row in vertical]
    assert maps["union"] == [
        [h + v for h, v in zip(h_row, v_row, strict=True)]
        for h_row, v_row in zip(horizontal, vertical, strict=True)
    ]
    assert summary["Congestion"]["overflow"]["total"] == {
        direction: sum(map(sum, matrix)) for direction, matrix in maps.items()
    }
