import json

from support import assert_nonempty_file, run_scenario


def test_pyplacedb_converts_missing_row_area_to_fixed_blockage(test_roots):
    result = run_scenario(test_roots, "pyplacedb_rows", timeout=120)
    summary = result.output_path("summary")
    assert_nonempty_file(summary)
    assert json.loads(summary.read_text(encoding="utf-8")) == {
        "blockages": [[4000, 0, 2000, 1800]],
        "num_terminals": 1,
        "rows": [
            [0, 0, 4000, 1800],
            [6000, 0, 10000, 1800],
            [0, 1800, 10000, 3600],
        ],
    }
