# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

"""Exercise native Context services through the existing Python viewer API."""

from pathlib import Path
from tempfile import TemporaryDirectory

import eccdb_viewer as native


LEF = """VERSION 5.8 ;
BUSBITCHARS "[]" ;
DIVIDERCHAR "/" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 0.2 ;
  WIDTH 0.1 ;
END M1
MACRO INV
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 1 BY 2 ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER M1 ;
      RECT 0.1 0.1 0.4 0.3 ;
    END
  END A
END INV
END LIBRARY
"""

DEF = """VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN smoke ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 20000 20000 ) ;
COMPONENTS 1 ;
- U1 INV + PLACED ( 1000 2000 ) N ;
END COMPONENTS
PINS 1 ;
- IN + NET N + DIRECTION INPUT + USE SIGNAL
  + LAYER M1 ( -50 -50 ) ( 50 50 ) + PLACED ( 0 2000 ) N ;
END PINS
NETS 1 ;
- N ( PIN IN ) ( U1 A )
  + ROUTED M1 ( 0 2000 ) ( 1200 2000 ) ( 1200 2200 ) ;
END NETS
END DESIGN
"""


def run(directory: Path) -> None:
    lef, definition = directory / "smoke.lef", directory / "smoke.def"
    lef.write_text(LEF)
    definition.write_text(DEF)
    database = native.Database.open_lef_def([str(lef)], str(definition))
    query = native.GeometryQueryService(database)
    payloads = native.GeometryPayloadService(database)
    occurrences = native.GeometryOccurrenceService(database)
    model = native.DesignModelService(database)
    edits = native.GeometryEditService(database)
    viewport = native.Rect(-100, -100, 21000, 21000)

    instances = model.list("instance").records
    assert [instance.name for instance in instances] == ["U1"]
    assert occurrences.fetch_instance_pins([instances[0].id]).occurrence_count == 1
    records = query.query_refs(viewport).records
    # Two routed segments, one BPin rectangle and the canonical DIEAREA.
    assert len(records) == 4
    assert payloads.fetch_shapes([record.id for record in records]).shape_count == 4
    shape = next(record for record in records if record.wire != (1 << 64) - 1)
    original_x = shape.bounds.ll_x
    epoch = query.geometry_epoch
    edits.move_shape(shape.id, 100, 0)
    moved = next(record for record in query.query_refs(viewport).records if record.id == shape.id)
    assert moved.bounds.ll_x == original_x + 100
    assert query.geometry_epoch > epoch
    assert edits.remove_shape(shape.id)
    assert not query.contains(shape.id)
    assert edits.restore_shape(shape.id)
    assert query.contains(shape.id)
    assert len(query.query_refs(viewport).records) == 4

    exported = directory / "edited.def"
    database.write_def(str(exported))
    reopened = native.Database.open_lef_def([str(lef)], str(exported))
    assert len(native.GeometryQueryService(reopened).query_refs(viewport).records) == 4
    binary = [str(directory / f"{kind}.bin") for kind in ("technology", "library", "design")]
    database.write_binary(*binary)
    restored = native.Database.open_binary(*binary)
    restored_query = native.GeometryQueryService(restored)
    assert len(restored_query.query_refs(viewport).records) == 4
    restored_shape = next(record for record in restored_query.query_refs(viewport).records if record.id == shape.id)
    assert restored_shape.bounds.ll_x == original_x + 100
    assert native.DesignModelService(restored).list("instance").records[0].name == "U1"
    print("PASS: Context-backed query/payload/occurrence/model/edit, stable Shape ID, DEF and binary round trips")


if __name__ == "__main__":
    with TemporaryDirectory(prefix="eccdb-service-bindings-") as path:
        run(Path(path))
