# Via Master Storage

`via_master/` stores concrete technology VIA definitions. Every ViaMaster is
one entity in the shared `TechRegistry`:

```text
Fixed ViaMaster entity
  TechViaMaster
  TechViaGeometry

Generated ViaMaster entity
  TechViaMaster
  TechViaGeometry
  TechGeneratedViaMaster
```

`TechGeneratedViaMaster` is optional; its presence distinguishes generated
VIAs from fixed VIAs. It stores the generation parameters and an optional
`TechViaRuleGenerateId`. `TechViaGeometry` remains materialized for router,
DRC, extraction, and UI consumers.

The bottom, CUT, and top shape sets are `GeometryHandle` fields in
`TechViaGeometry`. Each handle selects one entry in
`TechStore::geometryPool()`; the entry owns the internal contiguous Rect,
Polygon, and Point ranges. `TechViaMasterShapeInput` keeps three write-side
`GeometryInput` values only while a VIA is being built. After insertion,
individual Rects, Polygons, and Points have no EnTT IDs or independent
lifecycle.

The Tech geometry pool is append-only during normal construction. A failed
import rolls back to its checkpoint together with the registry transaction;
the imported Tech database is otherwise treated as immutable. Per-shape MASK
metadata is not represented yet.
