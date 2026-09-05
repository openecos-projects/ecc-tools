# Non-default rule storage

Each named LEF `NONDEFAULTRULE` is one entity with `TechNonDefaultRule` and
typed collection components:

```text
TechNonDefaultRule entity
├── TechNdrRoutingRules
├── TechNdrMinCutsRules
├── TechNdrUseVias
├── TechNdrUseViaRules
├── TechNdrProperties
├── TechNdrViaDefinitions
└── TechNdrSameNetSpacingRules
```

Ordinary clauses are value records in these vectors. They have no independent
identity or lifecycle and therefore consume no additional entity IDs.

An NDR-defined `VIA` is different: LEF permits a later `USEVIA` clause to
reference it by name. It remains a `TechViaMasterId` entity carrying:

```text
TechViaMaster
TechViaGeometry
TechNdrViaDefinition { owner }
[TechGeneratedViaMaster]
```

`TechNdrViaDefinitions` preserves the NDR's VIA declaration order. Layer, VIA,
and generated VIARULE references use their existing strongly typed entity IDs.
Reverse Layer-to-NDR queries scan only the `TechNonDefaultRule` view and inspect
the corresponding typed vector component; they do not scan unrelated entities.
