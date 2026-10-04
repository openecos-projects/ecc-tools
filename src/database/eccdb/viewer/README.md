# EccDB Viewer Facade

`GeometryQueryService` and `GeometryEditService` are small GUI-independent
boundaries over the canonical EccDB geometry registry. The query service returns
batches of opaque shape IDs and metadata/BBox values; the edit service routes
mutations through `DesignGeometryStorage` so the existing lazy R-tree is
invalidated by the normal geometry epoch mechanism. Keeping the boundaries
separate lets a renderer issue read-only viewport queries without acquiring an
editing API.

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
batch = query.query(eccdb_viewer.Rect(0, 0, 100000, 100000))
render_records = batch.as_array()

# Edits are explicit and use the same stable shape IDs returned by the query
# service.
if batch.records:
    edit.move_shape(batch.records[0].id, 10, 0)
```

The first facade does not copy or expose typed payloads. Point lists and
layer-filtered/GPU-ready batches will be added after the PySide6 canvas has a
consumer for this stable metadata boundary.
