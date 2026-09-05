# IMPLANT Layer

Each IMPLANT layer is one entity carrying:

```text
TechLayerInfo
TechImplantLayer
TechImplantSpacingRules
TechLayerProperties
```

`TechImplantLayer::min_width` represents LEF `WIDTH minWidth` when its
`kHasMinWidth` flag is set.

Each LEF clause

```text
SPACING minSpacing [LAYER otherImplant]
```

is a `TechImplantSpacingRule` value in the layer's
`TechImplantSpacingRules::values` vector. A spacing clause has no independent
name, identity, or lifetime, so it does not allocate an entity. The optional
peer remains a strongly typed `TechImplantLayerId` reference.

Concrete implant geometry is not stored here. CellMaster OBS geometry owns the
shapes and references the technology layer.
