# EccDB Viewer Facade

The services in `service/query` are GUI-independent boundaries over the
canonical EccDB registries. The viewer library and Python extension bind those
services; they do not contain Qt, color, selection or camera state.

- `GeometryQueryService`: design bounds, filtered reference batches, overview
  tiles, exact hit/marquee tests and index readiness/preheating.
- `GeometryPayloadService`: exact segments, rectangles, polygons, points and
  expanded via geometry in an owning viewport snapshot.
- `GeometryEditService`: move/remove/restore and an atomic batch deletion
  journal. Deleted objects keep their allocated ID during the session; undo
  restores payload, DEF attributes and owner order. Known edits update a warm
  R-tree and invalidate only affected overview tiles. Unknown external changes
  still trigger a lazy epoch rebuild.
- `DesignModelService`: scoped, paginated entity descriptors and real layer/
  track metadata. A master pin and a design instance term are distinct objects.
- `GeometryOccurrenceService`: on-demand master-local or instance-transformed
  pin geometry. Occurrence headers contain typed references, not Shape IDs.

The optional `eccdb_viewer` shared library and Python module are only binding
boundaries; the service implementation lives in `service/query`.

The optional Python extension is enabled with:

```text
-DECCDB_BUILD_VIEWER_PYTHON=ON
```

Python usage is intentionally batch-oriented:

```python
import eccdb_viewer

db = eccdb_viewer.Database.open_binary("tech.bin", "library.bin", "design.bin")
query = eccdb_viewer.GeometryQueryService(db)
edit = eccdb_viewer.GeometryEditService(db)
payload = eccdb_viewer.GeometryPayloadService(db)
filter = eccdb_viewer.GeometryQueryFilter()
filter.layers = [2, 3]  # Real technology layer IDs, not routing ordinals.
box = eccdb_viewer.Rect(0, 0, 100000, 100000)
batch = payload.query_geometry(box, filter)
headers = batch.headers_array()  # Read-only structured array; id is uint64.
segments = batch.segments_array()
rectangles = batch.rectangles_array()
polygons = batch.polygons_array()
polygon_points = batch.polygon_points_array()

# At overview zoom, request derived sparse tiles instead of individual shapes.
# The tile batch contains occupancy/layer summaries only; the database keeps
# the canonical shapes and the same query service can be used after zooming in.
overview = query.query_overview(eccdb_viewer.Rect(0, 0, 100000, 100000), level=2)
overview_tiles = overview.as_array()

# Edits are explicit and use the same stable shape IDs returned by the query
# service.
if len(headers):
    shape_id = int(headers[0]["id"])
    edit.move_shape(shape_id, 10, 0)
    edit.remove_shapes([shape_id])
    edit.restore_shapes([shape_id])

# Persist current canonical state. The session undo journal is not serialized.
db.write_def("edited.def")
db.write_binary("tech.bin", "library.bin", "edited-design.bin")
```

Each NumPy array keeps its native snapshot alive through its base object. This
avoids an additional array copy without borrowing movable EnTT storage. Only
the queried viewport is materialized; these snapshots are derived data, not a
second canonical database. `query_refs` remains available when only IDs and
metadata are needed. Multi-layer vias are filtered after expansion on their
actual primitive layers.

Editing geometry does not automatically repair connectivity or establish DRC
legality. The journal belongs to its edit-service instance. Save/reload retains
the edited database state, not the ability to undo a prior process's removal.
