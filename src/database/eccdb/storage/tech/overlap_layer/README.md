# OVERLAP Layer

An OVERLAP layer entity carries `TechLayerInfo` and the marker component
`TechOverlapLayer`. It has no routing, cut, or manufacturing-rule payload.

LEF uses OVERLAP OBS geometry to describe a macro's actual rectilinear
placement footprint inside its rectangular `SIZE` bounding box. That geometry
belongs to future CellMaster OBS storage, not to the shared technology layer.

OVERLAP entities are therefore excluded from `TechLayerSequence`: they have a
Tech ID and a name, but no physical stack position or routing level.
