# EccDB Viewer Facade

`eccdb_viewer` is a small GUI-facing facade over the canonical EccDB geometry
registry. It returns batches of opaque shape IDs and metadata/BBox values, and
routes edits through `DesignGeometryStorage` so the existing lazy R-tree is
invalidated by the normal geometry epoch mechanism.

The optional Python extension is enabled with:

```text
-DECCDB_BUILD_VIEWER_PYTHON=ON
```

Python usage is intentionally batch-oriented:

```python
import eccdb_viewer

db = eccdb_viewer.Database.open_binary("tech.bin", "library.bin", "design.bin")
view = eccdb_viewer.GeometryView(db)
batch = view.query(eccdb_viewer.Rect(0, 0, 100000, 100000))
render_records = batch.as_array()
```

The first facade does not copy or expose typed payloads. Point lists and
layer-filtered/GPU-ready batches will be added after the PySide6 canvas has a
consumer for this stable metadata boundary.
