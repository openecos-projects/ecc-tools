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

The bottom, CUT, and top shape sets are `GeometryShapeId` vectors in
`TechViaGeometry`. Each ID resolves to an independent typed rectangle or
polygon entity in the Tech geometry registry; the component owns only the
ordered relation. `TechViaMasterShapeInput` remains a transient parser value;
insertion creates one free EnTT geometry ID per physical primitive.

Geometry entities can be edited or removed independently. There is no append
only pool, group ordinal, checkpoint, or hidden range backing a handle.
