import json
import tempfile
from pathlib import Path

import ecc_py


data = Path(__file__).parent / "data"
with tempfile.TemporaryDirectory(prefix="imj_bindings_") as temp:
    output = Path(temp) / "output"
    assert ecc_py.tech_lef_init(str(data / "tech.lef"))
    assert ecc_py.lef_init([str(data / "cell.lef")])
    assert ecc_py.def_init(str(data / "design.def.in"))
    assert ecc_py.init_mj(config_dict={"-temp_directory_path": str(output)})
    assert ecc_py.analyze_driver()
    report = (output / "driver_analyzer/driver_report.rpt").read_text()
    assert "MultiDriverNets : 1" in report
    assert "UnresolvedNets : 0" in report
    assert "PORT bus driver (inferred from inout)" in report
    try:
        ecc_py.analyze_driver(config_dict={"-source": "netlist"})
    except TypeError:
        pass
    else:
        raise AssertionError("analyze_driver must not accept source selection")

    config = Path(temp) / "cut.json"
    config.write_text(json.dumps({"MJ": {"-rect": "2 2 13 13"}}))
    assert ecc_py.cut_def(config=str(config))
    assert not (output / "def_cutter/cut.def").exists()
    assert ecc_py.def_save(str(output / "first.def"))
    first_def = (output / "first.def").read_text()
    assert "DIEAREA ( 2000 2000 ) ( 13000 13000 )" in first_def
    assert ecc_py.cut_def(config=str(config), config_dict={"-rect": "2.5 2.5 12 12"})
    assert not ecc_py.cut_def(config=str(Path(temp) / "missing.json"))
    assert ecc_py.destroy_mj()
    assert ecc_py.def_save(str(output / "cut.def"))
    assert ecc_py.netlist_save(str(output / "cut.v"))
    die_def = (output / "cut.def").read_text()
    assert "DESIGN crop_test_cut_cut ;" in die_def
    assert "DIEAREA ( 2500 2500 ) ( 12000 12000 )" in die_def
    assert "PINS 6 ;" in die_def
    assert "- VDD + NET VDD" not in die_def
    assert "- out + NET out + DIRECTION INPUT" in die_def
    assert "- multi + NET multi + DIRECTION OUTPUT" in die_def
    assert "- transit + NET transit + DIRECTION INOUT" in die_def
    assert "module crop_test_cut_cut" in (output / "cut.v").read_text()
    assert ecc_py.def_init(str(output / "cut.def"))
