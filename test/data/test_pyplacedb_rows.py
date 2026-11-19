import json

from support import assert_nonempty_file, run_scenario


def test_pyplacedb_converts_missing_row_area_to_fixed_blockage(test_roots):
    result = run_scenario(test_roots, "pyplacedb_rows", timeout=120)
    summary = result.output_path("summary")
    assert_nonempty_file(summary)
    result = json.loads(summary.read_text(encoding="utf-8"))
    assert result.pop("variant") in {"v1", "v2"}
    assert result in ({
        "blockages": [[4000, 0, 2000, 1800]],
        "num_terminals": 1,
        "rows": [[0, 0, 4000, 1800], [6000, 0, 10000, 1800], [0, 1800, 10000, 3600]],
    }, {
        "blockages": [[4000, 0, 2000, 1400], [0, 1400, 10000, 400]],
        "num_terminals": 2,
        "rows": [[0, 0, 4000, 1400], [6000, 0, 10000, 1400], [0, 1800, 10000, 3200]],
    })
