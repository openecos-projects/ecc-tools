from pathlib import Path
from textwrap import dedent

from ecc_tools_bin import ecc_py


def test_pyplacedb_converts_missing_row_area_to_fixed_blockage(tmp_path: Path):
    tech_lef = tmp_path / "tech.lef"
    tech_lef.write_text(
        dedent(
            """
            VERSION 5.8 ;
            UNITS
              DATABASE MICRONS 1000 ;
            END UNITS
            LAYER METAL1
              TYPE ROUTING ;
              DIRECTION HORIZONTAL ;
              PITCH 0.2 ;
              WIDTH 0.1 ;
            END METAL1
            LAYER METAL2
              TYPE ROUTING ;
              DIRECTION VERTICAL ;
              PITCH 0.2 ;
              WIDTH 0.1 ;
            END METAL2
            SITE core7
              CLASS CORE ;
              SIZE 0.2 BY 1.4 ;
            END core7
            MACRO ADDFX1H7R
              CLASS CORE ;
              ORIGIN 0 0 ;
              SIZE 1.0 BY 1.4 ;
              SYMMETRY X Y ;
              SITE core7 ;
            END ADDFX1H7R
            END LIBRARY
            """
        ),
        encoding="utf-8",
    )
    design_def = tmp_path / "cut_rows.def"
    design_def.write_text(
        dedent(
            """
            VERSION 5.8 ;
            DESIGN cut_rows ;
            UNITS DISTANCE MICRONS 1000 ;
            DIEAREA ( 0 0 ) ( 10000 2800 ) ;
            ROW BOTTOM_LEFT core7 0 0 N DO 20 BY 1 STEP 200 0 ;
            ROW BOTTOM_RIGHT core7 6000 0 N DO 20 BY 1 STEP 200 0 ;
            ROW TOP core7 0 1400 FS DO 50 BY 1 STEP 200 0 ;
            COMPONENTS 1 ;
            - movable ADDFX1H7R + UNPLACED ;
            END COMPONENTS
            PINS 0 ;
            END PINS
            NETS 0 ;
            END NETS
            END DESIGN
            """
        ),
        encoding="utf-8",
    )

    assert ecc_py.tech_lef_init(str(tech_lef))
    assert ecc_py.def_init(str(design_def))
    place_db = ecc_py.pydb(ecc_py.get_dmInst(), 2, 2, False, False, False, False)
    blockages = [
        [
            place_db.node_x[node_id],
            place_db.node_y[node_id],
            place_db.node_size_x[node_id],
            place_db.node_size_y[node_id],
        ]
        for node_id, name in enumerate(place_db.node_names)
        if name.startswith("blockage")
    ]
    assert blockages == [[4000, 0, 2000, 1400]]
    assert place_db.num_terminals == 1
    assert list(place_db.rows) == [
        (0, 0, 4000, 1400),
        (6000, 0, 10000, 1400),
        (0, 1400, 10000, 2800),
    ]
