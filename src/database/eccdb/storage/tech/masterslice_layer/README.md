# MASTERSLICE Layer

A MASTERSLICE layer entity carries `TechLayerInfo` and
`TechMastersliceLayer`. `TechMastersliceType` represents the LEF58 TYPE
property variants such as `NWELL`, `PWELL`, `DIFFUSION`, `TRIMPOLY`, and
`TRIMMETAL`.

`TechTrimmedMetalRule` is an optional component on the same layer entity. It
represents `LEF58_TRIMMEDMETAL`, references a ROUTING layer, and can constrain
the target routing mask. It is valid only for subtype `TRIMMETAL`.

Fixed VIA geometry accepts a `TechConductorLayerRef`, so a fixed VIA may join
a MASTERSLICE layer and a ROUTING layer through a CUT layer. Macro pin and OBS
geometry remain CellMaster data and are intentionally outside this Tech module.
