from textwrap import dedent

import pytest
from ecc_tools_bin import ecc_py


@pytest.mark.parametrize("routing_bins", [(2, 2), (5, 7)])
def test_pyplacedb_exports_layer_aligned_directional_capacity(tmp_path, routing_bins):
    tech_lef = tmp_path / "tech.lef"
    tech_lef.write_text(
        dedent(
            """
            VERSION 5.8 ;
            UNITS
              DATABASE MICRONS 1000 ;
            END UNITS
            LAYER M1
              TYPE ROUTING ; DIRECTION HORIZONTAL ;
              PITCH 0.4 ; WIDTH 0.1 ; SPACING 0.1 ;
            END M1
            LAYER M2
              TYPE ROUTING ; DIRECTION VERTICAL ;
              PITCH 0.4 ; WIDTH 0.1 ; SPACING 0.1 ;
            END M2
            LAYER M3
              TYPE ROUTING ; DIRECTION HORIZONTAL ;
              PITCH 0.4 ; WIDTH 0.1 ; SPACING 0.1 ;
            END M3
            SITE core
              CLASS CORE ; SIZE 0.2 BY 1.4 ; SYMMETRY X Y ;
            END core
            MACRO CELL
              CLASS CORE ; ORIGIN 0 0 ; SIZE 1 BY 1.4 ;
              SITE core ; SYMMETRY X Y ;
            END CELL
            END LIBRARY
            """
        ),
        encoding="utf-8",
    )
    design_def = tmp_path / "rectangular.def"
    design_def.write_text(
        dedent(
            """
            VERSION 5.8 ;
            DESIGN rectangular ;
            UNITS DISTANCE MICRONS 1000 ;
            DIEAREA ( 0 0 ) ( 10000 2800 ) ;
            ROW R0 core 0 0 N DO 50 BY 1 STEP 200 0 ;
            ROW R1 core 0 1400 FS DO 50 BY 1 STEP 200 0 ;
            TRACKS Y 200 DO 7 STEP 400 LAYER M1 ;
            TRACKS X 200 DO 25 STEP 400 LAYER M2 ;
            COMPONENTS 1 ;
            - movable CELL + UNPLACED ;
            END COMPONENTS
            PINS 0 ; END PINS
            NETS 0 ; END NETS
            END DESIGN
            """
        ),
        encoding="utf-8",
    )

    try:
        assert ecc_py.tech_lef_init(str(tech_lef))
        assert ecc_py.def_init(str(design_def))
        place_db = ecc_py.pydb(
            db=ecc_py.get_dmInst(),
            numRoutingGridsX=routing_bins[0],
            numRoutingGridsY=routing_bins[1],
            with_routability=True,
            with_sta=False,
            include_m2_pg_rail_blockage=False,
            include_m2_pg_rail_density=False,
        )

        assert (
            list(place_db.unit_horizontal_capacities),
            list(place_db.unit_vertical_capacities),
        ) == (
            pytest.approx([7 / 2800, 0, 0]),
            pytest.approx([0, 25 / 10000, 0]),
        )
    finally:
        ecc_py.reset_data()
